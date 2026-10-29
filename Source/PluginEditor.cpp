#include "PluginEditor.h"

namespace
{
    juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
    juce::String fmtDb (double v)  { return (v < 0 ? utf8 ("\xe2\x88\x92") : juce::String()) + juce::String (std::abs (v), 1) + " dB"; }
    juce::String fmtPct (double v) { return juce::String (juce::roundToInt (v)) + "%"; }
    juce::String fmtMs (double v)  { return (v < 10.0 ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " ms"; }

    void styleLearnButton (juce::TextButton& b)
    {
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2b1a1a));
        b.setColour (juce::TextButton::textColourOffId, theme::accent);
        b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xffff9a94));
        b.getProperties().set ("outline", theme::accent.toString());
    }

    void styleSegment (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonOnColourId, theme::raised);
        b.setColour (juce::TextButton::textColourOffId, theme::muted);
        b.setColour (juce::TextButton::textColourOnId, theme::text);
    }
}

//==============================================================================
BassLevelerEditor::BassLevelerEditor (BassLevelerProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;

    // ---- Top bar ----
    for (auto* b : { &dynamicTab, &noteMapTab })
    {
        styleSegment (*b);
        addAndMakeVisible (b);
    }
    dynamicTab.onClick = [this] { setParam (ids::mode, 0.0f); };
    noteMapTab.onClick = [this] { setParam (ids::mode, 1.0f); };

    // ---- Dynamic page ----
    addAndMakeVisible (spectrum);
    addAndMakeVisible (history);

    dynKnobs.push_back (std::make_unique<Knob> (s, ids::target,   "Target",   fmtDb));
    dynKnobs.push_back (std::make_unique<Knob> (s, ids::amount,   "Amount",   fmtPct));
    dynKnobs.push_back (std::make_unique<Knob> (s, ids::maxGain,  "Max gain", [] (double v) { return utf8 ("\xc2\xb1") + juce::String (v, 1) + " dB"; }, theme::gain));
    dynKnobs.push_back (std::make_unique<Knob> (s, ids::q,        "Q",        [] (double v) { return juce::String (v, 2); }));
    dynKnobs.push_back (std::make_unique<Knob> (s, ids::attack,   "Attack",   fmtMs));
    dynKnobs.push_back (std::make_unique<Knob> (s, ids::release,  "Release",  fmtMs));
    dynKnobs.push_back (std::make_unique<Knob> (s, ids::harmonic, "Harmonic", fmtPct));
    for (auto& k : dynKnobs) { addAndMakeVisible (*k); dynamicComps.push_back (k.get()); }

    addAndMakeVisible (snapToggle);
    addAndMakeVisible (lowLatToggle);
    snapAtt   = std::make_unique<ButtonAtt> (s, ids::snap, snapToggle);
    lowLatAtt = std::make_unique<ButtonAtt> (s, ids::lowLatency, lowLatToggle);

    for (auto* sl : { &minFreq, &maxFreq })
    {
        sl->setSliderStyle (juce::Slider::LinearBar);
        sl->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sl->setTextValueSuffix (" Hz");
        addAndMakeVisible (sl);
    }
    minFreq.setTitle ("Lowest note frequency");
    maxFreq.setTitle ("Highest note frequency");
    minFreqAtt = std::make_unique<SliderAtt> (s, ids::minFreq, minFreq);
    maxFreqAtt = std::make_unique<SliderAtt> (s, ids::maxFreq, maxFreq);

    styleLearnButton (learnTargetBtn);
    addAndMakeVisible (learnTargetBtn);
    learnTargetBtn.onClick = [this]
    {
        if (! proc.learningTarget.load())
        {
            proc.targetSum.store (0.0);
            proc.targetCount.store (0);
            proc.learningTarget.store (true);
        }
        else
        {
            proc.learningTarget.store (false);
            if (const int n = proc.targetCount.load(); n > 0)
                setParam (ids::target, (float) (proc.targetSum.load() / n));
        }
    };

    dynamicComps.insert (dynamicComps.end(), { &spectrum, &history, &snapToggle, &lowLatToggle, &minFreq, &maxFreq, &learnTargetBtn });

    // ---- Note map page ----
    addAndMakeVisible (noteMap);
    noteMap.onSelectionChanged = [this] { repaint (680, 68, 260, 382); };

    styleLearnButton (learnMapBtn);
    addAndMakeVisible (learnMapBtn);
    learnMapBtn.onClick = [this] { proc.learningMap.store (! proc.learningMap.load()); };

    styleSegment (applyBtn);
    applyBtn.setColour (juce::TextButton::buttonColourId, theme::raised);
    applyBtn.setColour (juce::TextButton::textColourOffId, theme::text);
    applyBtn.onClick = [this] { proc.applyLearnedSuggestion(); noteMap.repaint(); };
    addAndMakeVisible (applyBtn);

    clearBtn.getProperties().set ("outline", theme::line.toString());
    clearBtn.onClick = [this]
    {
        proc.clearLearned();
        for (int i = 0; i < BassLevelerProcessor::kNumMapNotes; ++i) proc.noteMap[(size_t) i].store (0.0f);
        noteMap.repaint();
    };
    addAndMakeVisible (clearBtn);

    mapKnobs.push_back (std::make_unique<Knob> (s, ids::amount,   "Amount",   fmtPct));
    mapKnobs.push_back (std::make_unique<Knob> (s, ids::q,        "Q",        [] (double v) { return juce::String (v, 2); }));
    mapKnobs.push_back (std::make_unique<Knob> (s, ids::glide,    "Glide",    fmtMs));
    mapKnobs.push_back (std::make_unique<Knob> (s, ids::harmonic, "Harmonic", fmtPct));
    for (auto& k : mapKnobs) { addAndMakeVisible (*k); mapComps.push_back (k.get()); }

    for (auto* b : { &everyNoteBtn, &pitchClassBtn, &pencilBtn, &smoothBtn, &flattenBtn })
    {
        styleSegment (*b);
        addAndMakeVisible (b);
    }
    everyNoteBtn.onClick  = [this] { setParam (ids::pitchClass, 0.0f); };
    pitchClassBtn.onClick = [this] { setParam (ids::pitchClass, 1.0f); };
    pencilBtn.onClick  = [this] { noteMap.tool = NoteMapView::Tool::pencil; };
    smoothBtn.onClick  = [this] { noteMap.tool = NoteMapView::Tool::smooth; };
    flattenBtn.onClick = [this] { noteMap.tool = NoteMapView::Tool::flatten; };

    blend.setSliderStyle (juce::Slider::LinearHorizontal);
    blend.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    blend.setTitle ("Blend dynamic");
    blend.onValueChange = [this] { repaint (860, 566, 70, 36); };
    addAndMakeVisible (blend);
    blendAtt = std::make_unique<SliderAtt> (s, ids::blend, blend);

    mapComps.insert (mapComps.end(), { &noteMap, &learnMapBtn, &applyBtn, &clearBtn, &everyNoteBtn, &pitchClassBtn,
                                       &pencilBtn, &smoothBtn, &flattenBtn, &blend });

    setSize (960, 640);
    lastMode = ! isNoteMapMode();
    timerCallback();
    startTimerHz (30);
}

BassLevelerEditor::~BassLevelerEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

bool BassLevelerEditor::isNoteMapMode() const
{
    return proc.apvts.getRawParameterValue (ids::mode)->load() > 0.5f;
}

void BassLevelerEditor::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

//==============================================================================
void BassLevelerEditor::timerCallback()
{
    const bool mapMode = isNoteMapMode();
    if (mapMode != lastMode)
    {
        lastMode = mapMode;
        for (auto* c : dynamicComps) c->setVisible (! mapMode);
        for (auto* c : mapComps) c->setVisible (mapMode);
        dynamicTab.setToggleState (! mapMode, juce::dontSendNotification);
        noteMapTab.setToggleState (mapMode, juce::dontSendNotification);
        repaint();
    }

    const bool pc = proc.apvts.getRawParameterValue (ids::pitchClass)->load() > 0.5f;
    everyNoteBtn.setToggleState (! pc, juce::dontSendNotification);
    pitchClassBtn.setToggleState (pc, juce::dontSendNotification);
    pencilBtn.setToggleState  (noteMap.tool == NoteMapView::Tool::pencil,  juce::dontSendNotification);
    smoothBtn.setToggleState  (noteMap.tool == NoteMapView::Tool::smooth,  juce::dontSendNotification);
    flattenBtn.setToggleState (noteMap.tool == NoteMapView::Tool::flatten, juce::dontSendNotification);

    // Learn buttons reflect processor state
    const bool lt = proc.learningTarget.load();
    learnTargetBtn.setToggleState (lt, juce::dontSendNotification);
    learnTargetBtn.getProperties().set ("outline", (lt ? theme::rec : theme::accent).toString());
    learnTargetBtn.setButtonText (lt ? utf8 ("Learning\xe2\x80\xa6 click to set") : "Learn target");

    const bool lm = proc.learningMap.load();
    learnMapBtn.setToggleState (lm, juce::dontSendNotification);
    learnMapBtn.getProperties().set ("outline", (lm ? theme::rec : theme::accent).toString());
    const int secs = (int) proc.learnSeconds.load();
    learnMapBtn.setButtonText (lm ? utf8 ("Learning\xe2\x80\xa6 ") + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2)
                                  : (secs > 0 ? "Resume learning" : "Learn"));

    if (mapMode)
    {
        noteMap.repaint();
        repaint (680, 68, 260, 382);
    }
    else
    {
        spectrum.update();
        history.repaint();
        repaint (680, 68, 260, 292);
    }
    repaint (800, 0, 160, 52);
}

//==============================================================================
void BassLevelerEditor::paintSegmentWell (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (theme::well);
    g.fillRoundedRectangle (r.toFloat(), 8.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
}

void BassLevelerEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    // ---- Top bar ----
    g.setColour (theme::topBar);
    g.fillRect (0, 0, getWidth(), 52);
    g.setColour (theme::line);
    g.drawHorizontalLine (51, 0.0f, (float) getWidth());

    juce::Path logo;
    logo.startNewSubPath (22.0f, 30.0f);
    logo.cubicTo (26.0f, 30.0f, 27.0f, 22.0f, 31.0f, 22.0f);
    logo.cubicTo (35.0f, 22.0f, 36.0f, 30.0f, 40.0f, 30.0f);
    g.setColour (theme::accent);
    g.strokePath (logo, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff5b6270));
    g.drawLine (22.0f, 34.0f, 40.0f, 34.0f, 1.5f);

    g.setColour (theme::text);
    g.setFont (theme::sans (15.0f, true));
    g.drawText ("Bass Leveler", 50, 0, 120, 52, juce::Justification::centredLeft);

    paintSegmentWell (g, { 176, 10, 190, 32 });

    g.setColour (theme::muted);
    g.setFont (theme::mono (12.0f));
    g.drawText ("Latency " + juce::String (juce::roundToInt (proc.uiLatencyMs.load())) + " ms",
                800, 0, 140, 52, juce::Justification::centredRight);

    if (! isNoteMapMode())
    {
        theme::panelBox (g, { 20.0f, 68.0f, 648.0f, 292.0f });
        theme::panelBox (g, { 680.0f, 68.0f, 260.0f, 292.0f });
        theme::panelBox (g, { 20.0f, 372.0f, 920.0f, 110.0f });
        theme::panelBox (g, { 20.0f, 494.0f, 920.0f, 130.0f });

        // History header + legend
        g.setColour (theme::text);
        g.setFont (theme::sans (12.0f, true));
        g.drawText (utf8 ("Level at fundamental \xe2\x80\x94 last 24 notes"), 36, 382, 400, 18, juce::Justification::centredLeft);
        g.setFont (theme::sans (12.0f));
        int lx = 640;
        auto legend = [&] (juce::Colour c, const juce::String& t, bool dash)
        {
            g.setColour (c);
            if (dash) { const float d[] = { 3.0f, 3.0f }; g.drawDashedLine ({ (float) lx, 391.0f, (float) lx + 14.0f, 391.0f }, d, 2, 1.0f); }
            else g.fillEllipse ((float) lx + 3.0f, 387.0f, 8.0f, 8.0f);
            g.setColour (theme::muted);
            g.drawText (t, lx + 20, 382, 70, 18, juce::Justification::centredLeft);
            lx += 92;
        };
        legend (theme::dot, "Before", false);
        legend (theme::accent, "After", false);
        legend (theme::muted, "Target", true);

        g.setColour (theme::line);
        g.fillRect (600, 506, 1, 106);
        g.setColour (theme::muted);
        g.setFont (theme::sans (12.0f));
        g.drawText ("Range", 616, 562, 60, 24, juce::Justification::centredLeft);

        paintNowPlaying (g, { 680.0f, 68.0f, 260.0f, 292.0f });
    }
    else
    {
        theme::panelBox (g, { 20.0f, 68.0f, 648.0f, 382.0f });
        theme::panelBox (g, { 680.0f, 68.0f, 260.0f, 382.0f });
        theme::panelBox (g, { 20.0f, 462.0f, 920.0f, 162.0f });

        g.setColour (theme::text);
        g.setFont (theme::sans (12.0f, true));
        g.drawText ("Correction per note", 36, 80, 300, 18, juce::Justification::centredLeft);
        g.setFont (theme::sans (12.0f));
        g.setColour (theme::faint);
        g.fillRect (420, 88, 12, 2);
        g.setColour (theme::muted);
        g.drawText ("Learned level", 438, 80, 90, 18, juce::Justification::centredLeft);
        g.setColour (theme::gain);
        g.fillRoundedRectangle (532.0f, 85.0f, 8.0f, 8.0f, 2.0f);
        g.setColour (theme::muted);
        g.drawText ("Correction (drag)", 546, 80, 110, 18, juce::Justification::centredLeft);

        g.setColour (theme::line);
        g.fillRect (360, 478, 1, 130);

        g.setColour (theme::muted);
        g.drawText ("Map by", 380, 489, 92, 28, juce::Justification::centredLeft);
        g.drawText ("Draw tool", 380, 529, 92, 28, juce::Justification::centredLeft);
        g.drawText ("Blend dynamic", 380, 570, 100, 28, juce::Justification::centredLeft);
        paintSegmentWell (g, { 480, 485, 240, 36 });
        paintSegmentWell (g, { 480, 525, 252, 36 });
        g.setColour (theme::text);
        g.setFont (theme::mono (12.0f));
        g.drawText (fmtPct (blend.getValue()), 870, 570, 54, 28, juce::Justification::centredRight);

        paintLearnedProfile (g, { 680.0f, 68.0f, 260.0f, 382.0f });
    }
}

void BassLevelerEditor::paintNowPlaying (juce::Graphics& g, juce::Rectangle<float> panel)
{
    auto r = panel.reduced (18.0f);
    const bool voiced = proc.uiVoiced.load();

    g.setColour (theme::muted);
    g.setFont (theme::sans (11.0f, true));
    g.drawText ("NOW PLAYING", r.removeFromTop (16.0f), juce::Justification::centredLeft);
    r.removeFromTop (12.0f);

    auto noteRow = r.removeFromTop (60.0f);
    g.setFont (theme::mono (52.0f));
    g.setColour (voiced ? theme::accent : theme::line);
    const auto note = voiced ? noteName ((int) std::round (proc.uiMidi.load())) : juce::String ("--");
    g.drawText (note, noteRow.removeFromLeft (110.0f), juce::Justification::bottomLeft);
    if (voiced)
    {
        const float midi = proc.uiMidi.load();
        const int cents = juce::roundToInt ((midi - std::round (midi)) * 100.0f);
        g.setColour (theme::muted);
        g.setFont (theme::mono (13.0f));
        g.drawText (juce::String (proc.uiFreq.load(), 1) + " Hz" + (cents != 0 ? "  " + juce::String (cents) + " ct" : juce::String()),
                    noteRow.withTrimmedBottom (6.0f), juce::Justification::bottomLeft);
    }
    r.removeFromTop (12.0f);

    const float conf = voiced ? proc.uiConfidence.load() : 0.0f;
    auto confRow = r.removeFromTop (16.0f);
    g.setColour (theme::muted);
    g.setFont (theme::sans (12.0f));
    g.drawText ("Confidence", confRow, juce::Justification::centredLeft);
    g.setColour (theme::text);
    g.setFont (theme::mono (12.0f));
    g.drawText (juce::String (juce::roundToInt (conf * 100.0f)) + "%", confRow, juce::Justification::centredRight);
    r.removeFromTop (6.0f);
    auto bar = r.removeFromTop (6.0f);
    g.setColour (theme::well);
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (theme::accent);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * conf), 3.0f);
    r.removeFromTop (14.0f);
    g.setColour (theme::line);
    g.fillRect (r.removeFromTop (1.0f));
    r.removeFromTop (12.0f);

    auto stats = r.removeFromTop (42.0f);
    auto left = stats.removeFromLeft (stats.getWidth() * 0.5f);
    auto stat = [&] (juce::Rectangle<float> a, const juce::String& label, const juce::String& value)
    {
        g.setColour (theme::muted);
        g.setFont (theme::sans (12.0f));
        g.drawText (label, a.removeFromTop (16.0f), juce::Justification::centredLeft);
        g.setColour (theme::text);
        g.setFont (theme::mono (17.0f));
        g.drawText (value, a, juce::Justification::centredLeft);
    };
    stat (left, "Input at f0", voiced ? fmtDb (proc.uiLevelDb.load()) : utf8 ("\xe2\x80\x94"));
    stat (stats, "Target", fmtDb (proc.apvts.getRawParameterValue (ids::target)->load()));

    auto corr = r.removeFromBottom (52.0f);
    g.setColour (theme::muted);
    g.setFont (theme::sans (12.0f));
    g.drawText ("Correction", corr.removeFromTop (16.0f), juce::Justification::centredLeft);
    const float gdb = proc.uiGainDb.load();
    g.setColour (theme::gain);
    g.setFont (theme::mono (26.0f));
    g.drawText ((gdb >= 0.05f ? "+" : "") + fmtDb (std::abs (gdb) < 0.05f ? 0.0 : gdb), corr, juce::Justification::centredLeft);
}

void BassLevelerEditor::paintLearnedProfile (juce::Graphics& g, juce::Rectangle<float> panel)
{
    auto r = panel.reduced (18.0f);
    g.setColour (theme::muted);
    g.setFont (theme::sans (11.0f, true));
    g.drawText ("LEARNED PROFILE", r.removeFromTop (16.0f), juce::Justification::centredLeft);
    r.removeFromTop (4.0f + 40.0f + 12.0f);   // learn button

    g.setFont (theme::sans (12.0f));
    g.drawFittedText ("Play the bassline through. Each note's level at its fundamental is averaged, then a correction is suggested.",
                      r.removeFromTop (48.0f).toNearestInt(), juce::Justification::topLeft, 3);
    r.removeFromTop (10.0f);

    int heard = 0;
    float lo = 1000.0f, hi = -1000.0f;
    for (int i = 0; i < BassLevelerProcessor::kNumMapNotes; ++i)
        if (const int c = proc.learnCount[(size_t) i].load(); c > 0)
        {
            const float a = proc.learnSum[(size_t) i].load() / (float) c;
            lo = juce::jmin (lo, a); hi = juce::jmax (hi, a); ++heard;
        }

    auto stats = r.removeFromTop (42.0f);
    auto left = stats.removeFromLeft (stats.getWidth() * 0.5f);
    auto stat = [&] (juce::Rectangle<float> a, const juce::String& label, const juce::String& value)
    {
        g.setColour (theme::muted);
        g.setFont (theme::sans (12.0f));
        g.drawText (label, a.removeFromTop (16.0f), juce::Justification::centredLeft);
        g.setColour (theme::text);
        g.setFont (theme::mono (17.0f));
        g.drawText (value, a, juce::Justification::centredLeft);
    };
    stat (left, "Notes heard", juce::String (heard) + " / " + juce::String (BassLevelerProcessor::kNumMapNotes));
    stat (stats, "Spread", heard > 1 ? juce::String (hi - lo, 1) + " dB" : utf8 ("\xe2\x80\x94"));

    r.removeFromTop (12.0f);
    g.setColour (theme::line);
    g.fillRect (r.removeFromTop (1.0f));
    r.removeFromTop (12.0f);

    const int sel = noteMap.selected;
    g.setColour (theme::muted);
    g.setFont (theme::sans (12.0f));
    g.drawText ("Selected note", r.removeFromTop (16.0f), juce::Justification::centredLeft);
    const float v = proc.noteMap[(size_t) sel].load();
    auto row = r.removeFromTop (22.0f);
    g.setFont (theme::mono (15.0f));
    g.setColour (theme::text);
    const auto name = noteName (sel + BassLevelerProcessor::kFirstMapNote) + utf8 (" \xc2\xb7 ");
    g.drawText (name, row, juce::Justification::centredLeft);
    g.setColour (theme::gain);
    g.drawText ((v >= 0.05f ? "+" : "") + fmtDb (std::abs (v) < 0.05f ? 0.0 : v),
                row.withTrimmedLeft ((float) juce::GlyphArrangement::getStringWidthInt (theme::mono (15.0f), name)),
                juce::Justification::centredLeft);
}

//==============================================================================
void BassLevelerEditor::resized()
{
    dynamicTab.setBounds (180, 14, 84, 24);
    noteMapTab.setBounds (266, 14, 96, 24);

    // Dynamic
    spectrum.setBounds (36, 84, 616, 260);
    history.setBounds (36, 408, 888, 64);
    for (size_t i = 0; i < dynKnobs.size(); ++i)
        dynKnobs[i]->setBounds (36 + (int) i * 80, 512, 68, 96);
    snapToggle.setBounds (616, 506, 308, 24);
    lowLatToggle.setBounds (616, 534, 308, 24);
    minFreq.setBounds (690, 562, 110, 24);
    maxFreq.setBounds (810, 562, 114, 24);
    learnTargetBtn.setBounds (616, 592, 308, 26);

    // Note map
    noteMap.setBounds (36, 108, 616, 330);
    learnMapBtn.setBounds (698, 106, 224, 40);
    applyBtn.setBounds (698, 398, 150, 34);
    clearBtn.setBounds (856, 398, 66, 34);
    for (size_t i = 0; i < mapKnobs.size(); ++i)
        mapKnobs[i]->setBounds (36 + (int) i * 80, 495, 68, 96);
    everyNoteBtn.setBounds (484, 489, 100, 28);
    pitchClassBtn.setBounds (586, 489, 130, 28);
    pencilBtn.setBounds (484, 529, 80, 28);
    smoothBtn.setBounds (566, 529, 80, 28);
    flattenBtn.setBounds (648, 529, 80, 28);
    blend.setBounds (480, 570, 380, 28);
}
