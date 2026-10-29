#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
    const juce::Colour bg       { 0xff121418 };
    const juce::Colour topBar   { 0xff16191e };
    const juce::Colour panel    { 0xff1a1d23 };
    const juce::Colour well     { 0xff0e1013 };
    const juce::Colour raised   { 0xff262a33 };
    const juce::Colour line     { 0xff2a2f38 };
    const juce::Colour grid     { 0xff262a33 };
    const juce::Colour text     { 0xffe7e9ed };
    const juce::Colour muted    { 0xffa3aab6 };
    const juce::Colour faint    { 0xff8a919c };
    const juce::Colour dot      { 0xff6b7280 };
    const juce::Colour accent   { 0xff52c7db };
    const juce::Colour gain     { 0xfff5a14e };
    const juce::Colour rec      { 0xffe5625c };

    inline juce::Font sans (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }
    inline juce::Font mono (float size)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
    }

    inline void panelBox (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (line);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    }
}

//==============================================================================
class BassLevelerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BassLevelerLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderFillColourId, theme::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, theme::line);
        setColour (juce::Slider::trackColourId, theme::accent);
        setColour (juce::Slider::backgroundColourId, theme::well);
        setColour (juce::Slider::thumbColourId, theme::text);
        setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        setColour (juce::TextButton::buttonOnColourId, theme::raised);
        setColour (juce::TextButton::textColourOffId, theme::muted);
        setColour (juce::TextButton::textColourOnId, theme::text);
        setColour (juce::ToggleButton::textColourId, theme::text);
        setColour (juce::ToggleButton::tickColourId, theme::accent);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto c = bounds.getCentre();
        const float r = size * 0.5f - 4.0f;
        const float angle = start + pos * (end - start);

        juce::Path track;
        track.addCentredArc (c.x, c.y, r, r, 0.0f, start, end, true);
        g.setColour (theme::line);
        g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (c.x, c.y, r, r, 0.0f, start, angle, true);
        g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float inner = r * 0.65f;
        g.setColour (theme::raised);
        g.fillEllipse (c.x - inner, c.y - inner, inner * 2.0f, inner * 2.0f);

        const auto tip = c.getPointOnCircumference (inner * 0.75f, angle);
        g.setColour (theme::text);
        g.drawLine ({ c.getPointOnCircumference (inner * 0.25f, angle), tip }, 2.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                           float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style == juce::Slider::LinearBar)
        {
            const auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
            g.setColour (theme::well);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (theme::accent.withAlpha (0.22f));
            g.fillRoundedRectangle (r.withRight (sliderPos), 6.0f);
            g.setColour (theme::line);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (theme::text);
            g.setFont (theme::mono (12.0f));
            g.drawText (s.getTextFromValue (s.getValue()), r, juce::Justification::centred);
            return;
        }

        if (style == juce::Slider::LinearHorizontal)
        {
            const float cy = (float) y + (float) h * 0.5f;
            g.setColour (theme::well);
            g.fillRoundedRectangle ((float) x, cy - 3.0f, (float) w, 6.0f, 3.0f);
            g.setColour (theme::accent);
            g.fillRoundedRectangle ((float) x, cy - 3.0f, sliderPos - (float) x, 6.0f, 3.0f);
            g.setColour (theme::text);
            g.fillEllipse (sliderPos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
            return;
        }

        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, minPos, maxPos, style, s);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        auto fill = b.findColour (on ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId);
        if (over && ! on) fill = fill.isTransparent() ? theme::raised.withAlpha (0.4f) : fill.brighter (0.08f);
        if (down) fill = fill.brighter (0.12f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 6.0f);

        if (b.getProperties().contains ("outline"))
        {
            g.setColour (juce::Colour::fromString (b.getProperties()["outline"].toString()));
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return theme::sans (13.0f, true); }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        g.setColour (theme::text);
        g.setFont (theme::sans (13.0f));
        g.drawText (b.getButtonText(), r.withTrimmedRight (44.0f), juce::Justification::centredLeft);

        auto sw = r.removeFromRight (34.0f).withSizeKeepingCentre (34.0f, 18.0f);
        const bool on = b.getToggleState();
        g.setColour (on ? theme::accent : theme::line);
        g.fillRoundedRectangle (sw, 9.0f);
        g.setColour (on ? theme::well : theme::muted);
        const float kx = on ? sw.getRight() - 16.0f : sw.getX() + 2.0f;
        g.fillEllipse (kx, sw.getY() + 2.0f, 14.0f, 14.0f);
    }
};
