// Offline engine tests: build patches, render audio through the graph without a
// sound card, and check the result. Run after every engine change.
#include "../Source/engine/Patch.h"
#include "../Source/engine/ModuleRegistry.h"
#include <cstdio>

static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++failures;
}

struct Render { double rms = 0, peak = 0; bool finite = true; };

static Render render (Patch& patch, Transport& t, double seconds, double sr = 48000, int block = 512)
{
    auto& g = patch.getGraph();
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    double sumSq = 0;
    Render r;
    const int blocks = (int) (seconds * sr / block);
    for (int b = 0; b < blocks; ++b)
    {
        t.beginBlock();
        buf.clear();
        midi.clear();
        g.processBlock (buf, midi);
        t.endBlock (block);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < block; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) r.finite = false;
                sumSq += (double) v * v;
                r.peak = std::max (r.peak, (double) std::abs (v));
            }
    }
    r.rms = std::sqrt (sumSq / std::max (1, blocks * block * 2));
    return r;
}

static void prepare (Patch& p, double sr = 48000, int block = 512)
{
    p.getGraph().setPlayConfigDetails (0, 2, sr, block);
    p.getGraph().prepareToPlay (sr, block);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // 1. starter patch makes sound, stays finite and below clipping
    {
        Transport t;
        Patch p (t);
        prepare (p);
        p.buildDefault();       // nodes added after prepare must get prepared too
        t.start();
        const auto r = render (p, t, 4.0);
        std::printf ("      default patch: rms %.4f  peak %.3f\n", r.rms, r.peak);
        check (r.finite, "default patch output is finite");
        check (r.rms > 0.01, "default patch is audible");
        check (r.peak <= 1.0, "default patch never exceeds 0 dBFS");

        t.stop();
        render (p, t, 2.0);     // let tails ring out
        const auto quiet = render (p, t, 1.0);
        std::printf ("      after stop:    rms %.6f\n", quiet.rms);
        check (quiet.rms < 0.002, "everything goes quiet after stop (no stuck notes)");
    }

    // 2. every module type can be created, prepared and processed on its own
    {
        Transport t;
        Patch p (t);
        prepare (p);
        int x = 0;
        for (auto& type : getModuleTypes())
            check (p.addModule (type.id, { x += 10, 0 }) != Patch::NodeID {}, ("create " + type.id).toRawUTF8());
        t.start();
        check (render (p, t, 1.0).finite, "all modules process without NaNs");
    }

    // 3. each instrument on its own through an output
    for (auto [seqType, instType] : { std::pair { "pianoroll", "synth" }, std::pair { "drumseq", "drums" } })
    {
        Transport t;
        Patch ref (t);
        ref.buildDefault();
        const auto tree = ref.toValueTree();

        Patch p (t);
        prepare (p);
        // copy the sequencer's content from the starter patch
        Patch::NodeID src;
        for (auto id : ref.getModuleIds())
            if (ref.getModuleType (id) == seqType) src = id;
        const auto seq = p.addModule (seqType, {});
        juce::ValueTree extra ("EXTRA");
        ref.getModule (src)->saveExtra (extra);
        p.getModule (seq)->loadExtra (extra);

        const auto inst = p.addModule (instType, {});
        const auto out = p.addModule ("output", {});
        check (p.connect ({ seq, 0, inst, 0 }), "connect sequencer -> instrument");
        check (p.connect ({ inst, 0, out, 0 }), "connect instrument -> output");
        t.start();
        const auto r = render (p, t, 2.0);
        std::printf ("      %s -> %s: rms %.4f\n", seqType, instType, r.rms);
        check (r.rms > 0.005, "instrument is audible on its own");
    }

    // 4. cable rules
    {
        Transport t;
        Patch p (t);
        const auto seq = p.addModule ("pianoroll", {});
        const auto syn = p.addModule ("synth", {});
        const auto fil = p.addModule ("filter", {});
        const auto del = p.addModule ("delay", {});
        const auto lfo = p.addModule ("lfo", {});
        check (! p.canConnect ({ seq, 0, fil, 0 }), "note output cannot plug into audio input");
        check (! p.canConnect ({ lfo, 0, syn, 0 }), "mod output cannot plug into note input");
        check (p.canConnect ({ lfo, 0, syn, 1 }), "mod output plugs into mod input");
        check (p.connect ({ syn, 0, fil, 0 }) && p.connect ({ fil, 0, del, 0 }), "audio chain connects");
        check (! p.canConnect ({ del, 0, fil, 0 }), "feedback loops are refused");
        check (! p.canConnect ({ syn, 0, fil, 0 }), "duplicate cables are refused");
        p.removeModule (fil);
        check (p.getCables().empty(), "removing a module removes its cables");
    }

    // 5. save / load round trip
    {
        Transport t;
        Patch a (t);
        a.buildDefault();
        const auto xml1 = a.toValueTree().toXmlString();
        Patch b (t);
        b.fromValueTree (a.toValueTree());
        const auto xml2 = b.toValueTree().toXmlString();
        check (xml1 == xml2, "save -> load -> save gives identical patch");
        check (b.getCables().size() == a.getCables().size(), "cables survive save/load");
    }

    // 6. envelope follows its note gate, sample by sample
    {
        using M = juce::MidiMessage;
        auto env = createModule ("envelope");
        Patch::setParam (env.get(), "attack", 0.01f);
        Patch::setParam (env.get(), "decay", 0.1f);
        Patch::setParam (env.get(), "sustain", 0.5f);
        Patch::setParam (env.get(), "release", 0.05f);
        env->prepareToPlay (48000, 480);

        // runs 10 ms blocks with the events at the start of the first one; returns the last sample
        juce::AudioBuffer<float> buf (1, 480);
        float last = 0, peak = 0, maxStep = 0;
        auto run = [&] (int blocks, std::initializer_list<M> events = {}) {
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0)
                    for (auto& m : events) midi.addEvent (m, 0);
                env->processBlock (buf, midi);
                for (int i = 0; i < buf.getNumSamples(); ++i)
                {
                    const float v = buf.getSample (0, i);
                    maxStep = std::max (maxStep, std::abs (v - last));
                    peak = std::max (peak, v);
                    last = v;
                }
            }
            return last;
        };

        check (run (5) == 0.0f, "envelope rests at zero with no notes");
        const float sustained = run (30, { M::noteOn (1, 60, 0.8f) });
        check (peak > 0.99f, "envelope attacks to full");
        check (std::abs (sustained - 0.5f) < 0.01f, "envelope holds at sustain");
        run (5, { M::noteOn (1, 64, 0.8f) });
        check (std::abs (run (15, { M::noteOff (1, 60) }) - 0.5f) < 0.01f, "envelope stays open while another note is held");
        check (run (10, { M::noteOff (1, 64) }) == 0.0f, "envelope releases to zero after the last note-off");
        run (1, { M::noteOn (1, 60, 0.8f) });
        run (2, { M::noteOff (1, 60) });
        run (30, { M::noteOn (1, 60, 0.8f) });      // retrigger mid-release
        check (run (10, { M::allNotesOff (1) }) == 0.0f, "all-notes-off releases the envelope");
        std::printf ("      envelope: largest step between samples %.4f\n", maxStep);
        check (maxStep < 0.01f, "envelope never jumps (retrigger and release are click-free)");

        Patch::setParam (env.get(), "depth", -1.0f);
        check (std::abs (run (30, { M::noteOn (1, 60, 0.8f) }) + 0.5f) < 0.01f, "negative depth inverts the envelope");
    }

    // 7. envelope driven by a sequencer over a cable, released by stopping the transport
    {
        Transport t;
        Patch p (t);
        prepare (p);
        p.buildDefault();
        Patch::NodeID roll, syn;
        for (auto id : p.getModuleIds())
        {
            if (p.getModuleType (id) == "pianoroll") roll = id;
            if (p.getModuleType (id) == "synth") syn = id;
        }
        const auto env = p.addModule ("envelope", {});
        check (! p.canConnect ({ env, 0, syn, 0 }), "envelope output cannot plug into note input");
        check (p.connect ({ roll, 0, env, 0 }), "connect sequencer -> envelope");
        check (p.connect ({ env, 0, syn, 1 }), "connect envelope -> synth cutoff mod");

        t.start();
        const auto r = render (p, t, 0.5);         // first chord is held for ~1.8 s
        const float open = p.getModule (env)->getMeterLevel();
        std::printf ("      envelope from piano roll: level %.3f\n", open);
        check (r.finite, "patch with envelope modulation is finite");
        check (open > 0.4f, "envelope opens when the sequencer plays a note");
        t.stop();
        render (p, t, 1.0);
        check (p.getModule (env)->getMeterLevel() == 0.0f, "envelope closes when the transport stops");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
