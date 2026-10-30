#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace theme
{
    const juce::Colour window   { 0xff14171c };
    const juce::Colour header   { 0xff1a1e25 };
    const juce::Colour panel    { 0xff1c2028 };
    const juce::Colour well     { 0xff15181e };
    const juce::Colour control  { 0xff242934 };
    const juce::Colour controlOn{ 0xff3a4150 };
    const juce::Colour line     { 0xff2e3440 };
    const juce::Colour grid     { 0xff232833 };
    const juce::Colour border   { 0xff262b35 };
    const juce::Colour text     { 0xffe6e9ef };
    const juce::Colour textSoft { 0xffc9ced8 };
    const juce::Colour muted    { 0xff9aa3b2 };
    const juce::Colour faint    { 0xff7d8697 };
    const juce::Colour kick     { 0xffffb15c };
    const juce::Colour bass     { 0xff4c8dff };
    const juce::Colour bassSoft { 0xff7fb0ff };
}

class PLLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PLLookAndFeel();
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
};

class ScopeView : public juce::Component
{
public:
    explicit ScopeView (PhaseLockProcessor& p);
    void update();
    void paint (juce::Graphics&) override;
    void setFrozen (bool f) { frozen = f; repaint(); }
private:
    PhaseLockProcessor& proc;
    bool frozen = false;
    PhaseLockProcessor::ScopeFrame frame, prevFrame, live;
    bool hasFrame = false, hasPrev = false, hasLive = false;
    juce::uint32 lastFrameTime = 0, liveTime = 0;
    int seenSerial = 0;
    juce::VBlankAttachment vblank;   // redraws in step with the screen for a smooth sweep
};

// FL-style curve editor for the sidechain slope. Presets are shown read-only;
// any edit turns the current shape into an editable Custom curve.
//   drag a point ............ move it
//   drag a small circle ..... bend that segment (tension)
//   right-click a point ..... curve type of the segment leading into it, delete
//   right-click empty ....... add a point
//   double-click ............ add point / delete point / reset tension
class SlopeEditor : public juce::Component, public juce::SettableTooltipClient
{
public:
    SlopeEditor (PhaseLockProcessor& p, std::function<void()> switchToCustom);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverNode = hoverHandle = -1; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    struct Geometry { float w, h, top, ch, ax, right, total; };
    Geometry geometry() const;
    juce::Point<float> nodeToScreen (const slope::Node&, const Geometry&) const;
    float screenToX (float px, const Geometry&) const;
    float screenToY (float py, const Geometry&) const;
    juce::Point<float> handlePos (const slope::Nodes&, int segment, const Geometry&) const;
    int findNode (juce::Point<float>, const slope::Nodes&) const;
    int findHandle (juce::Point<float>, const slope::Nodes&) const;
    slope::Nodes editableNodes();          // converts a preset to points if needed
    void showMenu (juce::Point<float>);
    void addNodeAt (juce::Point<float>);

    PhaseLockProcessor& proc;
    std::function<void()> onSwitchToCustom;
    int dragNode = -1, dragHandle = -1, hoverNode = -1, hoverHandle = -1;
    float dragStartTension = 0.0f, dragStartY = 0.0f;
};

// A slope preset drawn as a small curve icon.
class CurveButton : public juce::Button
{
public:
    explicit CurveButton (int shapeIndex) : juce::Button (slope::names[shapeIndex]), shapeIdx (shapeIndex)
    {
        setTooltip (slope::names[shapeIndex]);
    }
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    int shapeIdx;
};

// Shift-limit window: drag either edge to set how far earlier / later the bass may
// go, or drag the middle to slide the whole window. Shift-drag an edge = symmetric.
class ShiftRangeBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ShiftRangeBar (PhaseLockProcessor&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Target { None, Lo, Hi, Both };
    float scaleMs() const;
    juce::Rectangle<float> trackArea() const;
    float msToX (float ms) const;
    float xToMs (float x) const;
    Target hitTest (float x) const;
    void setLimits (float lo, float hi);

    PhaseLockProcessor& proc;
    juce::ParameterAttachment loAtt, hiAtt;
    Target dragging = Target::None;
    float startLo = 0.0f, startHi = 0.0f, startMs = 0.0f;
};

class LockButton : public juce::TextButton
{
public:
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

class PhaseLockEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PhaseLockEditor (PhaseLockProcessor&);
    ~PhaseLockEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    std::pair<float, float> limits() const { return proc.getShiftLimits(); }
    void setupSlider (juce::Slider&);
    void drawSliderCaption (juce::Graphics&, const juce::Slider&, const juce::String& name, const juce::String& value);
    void nudge (float deltaMs);

    PhaseLockProcessor& proc;
    PLLookAndFeel lnf;

    ScopeView scope;
    SlopeEditor slopeEditor;

    juce::OwnedArray<CurveButton> curveButtons;
    juce::TextButton customButton;
    juce::TooltipWindow tooltips { this, 400 };
    juce::OwnedArray<juce::TextButton> modeButtons, nudgeButtons, scaleButtons, scopeLenButtons;
    ShiftRangeBar shiftBar;
    std::unique_ptr<juce::ParameterAttachment> slopeAtt, modeAtt, offsetAtt, scaleAtt, scopeLenAtt;

    juce::Slider depthSlider, attackSlider, releaseSlider, thresholdSlider, offsetSlider;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SA> depthAtt, attackAtt, releaseAtt, thresholdAtt;

    LockButton lockButton;
    juce::TextButton freezeButton { "Freeze" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lockAtt;

    bool draggingOffset = false;
    float shownLo = 1.0e9f, shownHi = 1.0e9f;
    juce::Rectangle<int> limitsCaption;

    // readouts
    juce::String offsetText, statusText, phaseText, bestText, fundText, corrText, scText;
    juce::Colour statusColour = theme::muted;
    float corrValue = 0.0f;
    bool scActive = false;

    // layout
    juce::Rectangle<int> scopePanel, slopePanel, alignPanel, detectPanel, offsetReadout, meterArea, readoutGrid;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaseLockEditor)
};
