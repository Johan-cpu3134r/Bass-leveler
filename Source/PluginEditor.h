#pragma once

#include "PluginProcessor.h"
#include "Theme.h"
#include "Views.h"

class BassLevelerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BassLevelerEditor (BassLevelerProcessor&);
    ~BassLevelerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setParam (const juce::String& id, float plainValue);
    bool isNoteMapMode() const;
    void paintNowPlaying (juce::Graphics&, juce::Rectangle<float>);
    void paintLearnedProfile (juce::Graphics&, juce::Rectangle<float>);
    void paintSegmentWell (juce::Graphics&, juce::Rectangle<int> around);

    BassLevelerProcessor& proc;
    BassLevelerLookAndFeel lnf;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // Top bar
    juce::TextButton dynamicTab { "Dynamic" }, noteMapTab { "Note map" };

    // Dynamic page
    SpectrumView spectrum { proc };
    HistoryView history { proc };
    std::vector<std::unique_ptr<Knob>> dynKnobs;
    juce::ToggleButton snapToggle { "Snap to semitone" }, lowLatToggle { "Low latency" };
    std::unique_ptr<ButtonAtt> snapAtt, lowLatAtt;
    juce::Slider minFreq, maxFreq;
    std::unique_ptr<SliderAtt> minFreqAtt, maxFreqAtt;
    juce::TextButton learnTargetBtn { "Learn target" };

    // Note map page
    NoteMapView noteMap { proc };
    juce::TextButton learnMapBtn { "Learn" }, applyBtn { "Apply suggestion" }, clearBtn { "Clear" };
    std::vector<std::unique_ptr<Knob>> mapKnobs;
    juce::TextButton everyNoteBtn { "Every note" }, pitchClassBtn { "Pitch class (12)" };
    juce::TextButton pencilBtn { "Pencil" }, smoothBtn { "Smooth" }, flattenBtn { "Flatten" };
    juce::Slider blend;
    std::unique_ptr<SliderAtt> blendAtt;

    std::vector<juce::Component*> dynamicComps, mapComps;
    bool lastMode = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassLevelerEditor)
};
