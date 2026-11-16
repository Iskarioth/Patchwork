#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/Ports.h"

namespace Theme
{
inline const juce::Colour background  { 0xff111214 };
inline const juce::Colour gridLine    { 0xff17191c };
inline const juce::Colour node        { 0xff1c1f23 };
inline const juce::Colour nodeTitle   { 0xff252930 };
inline const juce::Colour line        { 0xff2e3238 };
inline const juce::Colour text        { 0xffd8dbe0 };
inline const juce::Colour dim         { 0xff80868f };

inline juce::Colour forPort (PortType t)
{
    switch (t)
    {
        case PortType::Audio: return juce::Colour (0xffff8a3d);
        case PortType::Note:  return juce::Colour (0xff4fd1c5);
        case PortType::Mod:   return juce::Colour (0xffb28cff);
    }
    return text;
}

inline juce::Colour forCategory (const juce::String& c)
{
    if (c == "Sequencers")  return juce::Colour (0xff4fd1c5);
    if (c == "Instruments") return juce::Colour (0xffff8a3d);
    if (c == "Effects")     return juce::Colour (0xff5aa9ff);
    if (c == "Modulators")  return juce::Colour (0xffb28cff);
    return juce::Colour (0xffa0a6ae);
}
} // namespace Theme

class PatchLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PatchLookAndFeel()
    {
        setColourScheme ({ 0xff1c1f23, 0xff15171a, 0xff1c1f23, 0xff3a3f46, 0xffd8dbe0,
                           0xff2a2e34, 0xffffffff, 0xff3a5f8a, 0xffd8dbe0 });
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff15171a));
        setColour (juce::ComboBox::outlineColourId, Theme::line);
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xff23262b));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1f3a35));
        setColour (juce::Slider::textBoxOutlineColourId, Theme::line);
        setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xff2a2e34));
        setColour (juce::BubbleComponent::outlineColourId, Theme::line);
        setColour (juce::TooltipWindow::textColourId, Theme::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (5);
        const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const juce::PathStrokeType stroke (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc (c.x, c.y, r, r, 0, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff2c3036));
        g.strokePath (track, stroke);

        juce::Path value;
        value.addCentredArc (c.x, c.y, r, r, 0, startAngle, angle, true);
        g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (value, stroke);

        g.setColour (Theme::text);
        g.drawLine ({ c.getPointOnCircumference (r * 0.2f, angle), c.getPointOnCircumference (r - 4, angle) }, 2.0f);
    }

    juce::Font getPopupMenuFont() override { return juce::FontOptions (14.0f); }
};
