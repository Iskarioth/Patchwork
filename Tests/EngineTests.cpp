// Offline engine tests: build patches, render audio through the graph without a
// sound card, and check the result. Run after every engine change.
#include "../Source/engine/Patch.h"
#include "../Source/engine/ModuleRegistry.h"
#include "../Source/modules/PianoRollModule.h"
#include "../Source/modules/DrumSequencerModule.h"
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
    for (auto [seqType, instType] : { std::pair { "pianoroll", "synth" }, std::pair { "pianoroll", "fmsynth" },
                                      std::pair { "drumseq", "drums" } })
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
        check (r.peak <= 1.0, "instrument never exceeds 0 dBFS");
        t.stop();
        render (p, t, 2.0);
        check (render (p, t, 1.0).rms < 0.002, "instrument goes quiet after stop");
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

    // 8. mixer: sums its inputs with level and pan, ramping gain changes
    {
        auto mix = createModule ("mixer");
        mix->prepareToPlay (48000, 512);
        juce::AudioBuffer<float> buf (8, 512);
        // one block with constant inputs (value per stereo input); returns the buffer
        auto run = [&] (std::array<float, 4> inputs) -> const juce::AudioBuffer<float>& {
            for (int c = 0; c < 8; ++c)
                juce::FloatVectorOperations::fill (buf.getWritePointer (c), inputs[(size_t) c / 2], 512);
            juce::MidiBuffer midi;
            mix->processBlock (buf, midi);
            return buf;
        };
        auto near = [] (float a, float b) { return std::abs (a - b) < 1.0e-4f; };

        auto& b1 = run ({ 0.5f, 0, 0, 0 });
        check (near (b1.getSample (0, 0), 0.5f) && near (b1.getSample (1, 511), 0.5f), "mixer passes a centred input through unchanged");
        auto& b2 = run ({ 0.5f, 0, 0.25f, 0 });
        check (near (b2.getSample (0, 100), 0.75f) && near (b2.getSample (1, 100), 0.75f), "mixer sums its inputs");

        Patch::setParam (mix.get(), "pan1", 1.0f);
        auto& b3 = run ({ 0.5f, 0, 0, 0 });
        check (b3.getSample (0, 0) > 0.49f && b3.getSample (0, 511) < 0.01f, "gain changes ramp across a block (no zipper)");
        auto& b4 = run ({ 0.5f, 0, 0, 0 });
        check (near (b4.getSample (0, 0), 0.0f) && near (b4.getSample (1, 0), 0.5f), "hard-right pan silences the left side");

        Patch::setParam (mix.get(), "pan1", 0.0f);
        Patch::setParam (mix.get(), "level1", -60.0f);
        run ({ 0.5f, 0, 0, 0 });
        auto& b5 = run ({ 0.5f, 0, 0.25f, 0 });
        check (near (b5.getSample (0, 0), 0.25f), "level at minimum mutes that input only");
    }

    // 9. mixer in a patch: both starter instruments through one mixer
    {
        Transport t;
        Patch p (t);
        prepare (p);
        p.buildDefault();
        Patch::NodeID verb, drums, out;
        for (auto id : p.getModuleIds())
        {
            if (p.getModuleType (id) == "reverb") verb = id;
            if (p.getModuleType (id) == "drums") drums = id;
            if (p.getModuleType (id) == "output") out = id;
        }
        p.disconnect ({ verb, 0, out, 0 });
        p.disconnect ({ drums, 0, out, 0 });
        const auto mix = p.addModule ("mixer", {});
        check (p.connect ({ verb, 0, mix, 0 }) && p.connect ({ drums, 0, mix, 1 }) && p.connect ({ mix, 0, out, 0 }),
               "connect two instruments -> mixer -> output");
        check (! p.canConnect ({ mix, 0, mix, 2 }), "mixer cannot feed itself");

        t.start();
        const auto both = render (p, t, 2.0);
        Patch::setParam (p.getModule (mix), "level2", -60.0f);
        render (p, t, 0.1);
        const auto synthOnly = render (p, t, 2.0);
        std::printf ("      mixer: both %.4f  drums muted %.4f\n", both.rms, synthOnly.rms);
        check (both.finite && both.rms > 0.01, "mixed patch is audible");
        check (synthOnly.rms > 0.005 && synthOnly.rms < both.rms * 0.9, "muting a mixer channel removes that instrument");

        Transport t2;
        Patch lone (t2);
        prepare (lone);
        const auto m2 = lone.addModule ("mixer", {});
        lone.connect ({ m2, 0, lone.addModule ("output", {}), 0 });
        t2.start();
        check (render (lone, t2, 0.5).peak == 0.0, "mixer with nothing plugged in is silent");
    }

    // 10. FM synth: index 0 is a pure sine at the note's pitch; index adds harmonics
    {
        // Plays A3 (220 Hz) and measures brightness over 10 ms blocks [from, to): rms of the
        // sample-to-sample difference over rms of the signal. A pure sine gives 2 sin (pi f / sr).
        using Settings = std::initializer_list<std::pair<const char*, float>>;
        auto brightness = [] (Settings settings, float indexMod = 0, int from = 5, int to = 50) {
            auto fm = createModule ("fmsynth");
            for (auto [id, v] : settings)
                Patch::setParam (fm.get(), id, v);
            fm->prepareToPlay (48000, 480);
            juce::AudioBuffer<float> buf (2, 480);
            double sumSq = 0, diffSq = 0;
            float prev = 0;
            for (int b = 0; b < to; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 57, 1.0f), 0);
                juce::FloatVectorOperations::fill (buf.getWritePointer (0), indexMod, 480);
                buf.clear (1, 0, 480);
                fm->processBlock (buf, midi);
                for (int i = 0; i < 480; ++i)
                {
                    const float v = buf.getSample (0, i);
                    if (b >= from) { sumSq += (double) v * v; diffSq += (double) (v - prev) * (v - prev); }
                    prev = v;
                }
            }
            return std::sqrt (diffSq / std::max (1.0e-12, sumSq));
        };

        const double sine = 2.0 * std::sin (juce::MathConstants<double>::pi * 220.0 / 48000.0);
        const double pure = brightness ({ { "index", 0.0f }, { "envindex", 0.0f } });
        const double bright = brightness ({ { "index", 6.0f }, { "envindex", 0.0f } });
        const double modded = brightness ({ { "index", 0.0f }, { "envindex", 0.0f } }, 1.0f);
        const double dry = brightness ({ { "index", 2.0f }, { "envindex", 0.0f }, { "ratio", 1.0f } });
        const double fed = brightness ({ { "index", 2.0f }, { "envindex", 0.0f }, { "ratio", 1.0f }, { "feedback", 1.0f } });
        const Settings pluck { { "index", 0.0f }, { "envindex", 8.0f }, { "envdecay", 0.1f } };
        const double attackPart = brightness (pluck, 0, 0, 3), tailPart = brightness (pluck, 0, 40, 50);
        std::printf ("      fm brightness: sine %.4f  pure %.4f  index 6 %.3f  index mod %.3f  index 2 %.3f -> feedback %.3f\n"
                     "                     index env: attack %.3f  tail %.4f\n",
                     sine, pure, bright, modded, dry, fed, attackPart, tailPart);
        check (std::abs (pure / sine - 1.0) < 0.02, "fm with index 0 is a sine at the note's pitch");
        check (bright > pure * 3, "fm index adds harmonics");
        check (modded > pure * 3, "fm index mod input adds harmonics");
        check (fed > dry * 1.2, "fm feedback adds harmonics");
        check (fed < 0.5, "fm full feedback stays tonal (no noise)");
        check (attackPart > tailPart * 3 && std::abs (tailPart / sine - 1.0) < 0.05, "fm index envelope starts bright and decays to the base index");
    }

    // 11. pattern slots: a newly selected pattern takes over at the end of the loop
    {
        Transport t;
        t.bpm = 120.0;                     // 6000 samples per step at 48 kHz
        constexpr int64_t bar = 96000;
        PianoRollModule roll;
        roll.setTransport (&t);
        roll.setNotes (0, { { 60, 0, 1, 0.8f } });
        roll.setBars (0, 1);
        roll.setNotes (1, { { 72, 0, 1, 0.8f } });
        roll.setBars (1, 2);
        roll.prepareToPlay (48000, 500);

        // note-ons as (transport sample, pitch)
        std::vector<std::pair<int64_t, int>> ons;
        juce::AudioBuffer<float> buf (1, 500);
        juce::MidiBuffer midi;
        auto play = [&] (int64_t untilSample) {
            do
            {
                t.beginBlock();
                const int64_t at = t.position.load();
                roll.processBlock (buf, midi);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn())
                        ons.push_back ({ at + meta.samplePosition, meta.getMessage().getNoteNumber() });
                t.endBlock (500);
            } while (t.position.load() < untilSample);
        };
        using On = std::vector<std::pair<int64_t, int>>;

        t.start();
        play (bar / 2);
        Patch::setParam (&roll, "pattern", 1.0f);
        play (bar / 2 + 1000);
        check (ons == On { { 0, 60 } } && roll.getPlayingPattern() == 0, "selected pattern waits for the loop to end");
        play (bar * 3 + 6000);
        check (ons == On { { 0, 60 }, { bar, 72 }, { bar * 3, 72 } } && roll.getPlayingPattern() == 1,
               "new pattern starts exactly at the loop boundary and loops at its own length");

        t.stop();
        play (0);
        check (roll.getPlayingPattern() == -1, "no pattern is playing when stopped");
        Patch::setParam (&roll, "pattern", 0.0f);
        ons.clear();
        t.start();
        play (bar + 1000);
        check (ons == On { { 0, 60 }, { bar, 60 } }, "after a restart the selected pattern plays from its start");
    }

    // 12. pattern slots survive save/load; patches from before pattern slots load into A
    {
        PianoRollModule a, b;
        a.setNotes (0, { { 60, 0, 1, 0.8f } });
        a.setNotes (5, { { 72, 4, 2, 0.5f } });
        a.setBars (5, 4);
        juce::ValueTree extra ("EXTRA");
        a.saveExtra (extra);
        b.loadExtra (extra);
        check (b.getNotes (0).size() == 1 && b.getNotes (5).size() == 1 && b.getNotes (5)[0].pitch == 72
                   && b.getBars (5) == 4 && b.getNotes (1).empty(),
               "piano roll patterns survive save/load");

        juce::ValueTree old ("EXTRA");
        old.setProperty ("bars", 4, nullptr);
        juce::ValueTree note ("NOTE");
        note.setProperty ("p", 64, nullptr).setProperty ("s", 2.0, nullptr).setProperty ("l", 1.0, nullptr).setProperty ("v", 0.7f, nullptr);
        old.appendChild (note, nullptr);
        b.loadExtra (old);
        check (b.getBars (0) == 4 && b.getNotes (0).size() == 1 && b.getNotes (0)[0].pitch == 64 && b.getNotes (5).empty(),
               "old piano roll patch loads into pattern A");

        DrumSequencerModule d, e;
        d.setStep (0, 0, 0, 3);
        d.setStep (3, 2, 5, 2);
        juce::ValueTree dx ("EXTRA");
        d.saveExtra (dx);
        e.loadExtra (dx);
        check (e.getStep (0, 0, 0) == 3 && e.getStep (3, 2, 5) == 2 && e.getStep (0, 2, 5) == 0 && e.isEmpty (1),
               "drum patterns survive save/load");

        juce::ValueTree oldDrums ("EXTRA");
        oldDrums.setProperty ("lane1", "0000100000001000", nullptr);
        e.loadExtra (oldDrums);
        check (e.getStep (0, 1, 4) == 1 && e.getStep (0, 1, 12) == 1 && e.isEmpty (3), "old drum patch loads into pattern A");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
