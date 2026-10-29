#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include "PluginProcessor.h"
#include "Theme.h"

inline juce::String noteName (int midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}

//==============================================================================
/** Rotary knob with value readout and caption, attached to a parameter. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& caption,
          std::function<juce::String (double)> fmt, juce::Colour colour = theme::accent)
        : label (caption), format (std::move (fmt))
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        slider.setTitle (caption);
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    }

    void resized() override { slider.setBounds (getLocalBounds().removeFromTop (56).withSizeKeepingCentre (56, 56)); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().withTrimmedTop (58);
        g.setColour (theme::text);
        g.setFont (theme::mono (12.0f));
        g.drawText (format (slider.getValue()), r.removeFromTop (16), juce::Justification::centred);
        g.setColour (theme::muted);
        g.setFont (theme::sans (11.0f));
        g.drawText (label, r.removeFromTop (16), juce::Justification::centred);
    }

private:
    juce::Slider slider;
    juce::String label;
    std::function<juce::String (double)> format;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
/** Spectrum of the output with the live tracking-bell curve drawn on top. */
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (BassLevelerProcessor& p) : proc (p)
    {
        ring.assign (kSize, 0.0f);
        smoothed.assign (kSize / 2, -120.0f);
        window.resize (kSize);
        for (int i = 0; i < kSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (kSize - 1));
        fftData.assign (kSize * 2, 0.0f);
    }

    void update()
    {
        auto& fifo = proc.specFifo;
        const int ready = fifo.getNumReady();
        if (ready > 0)
        {
            fifo.read (ready).forEach ([this] (int src)
            {
                ring[(size_t) ringWrite] = proc.specBuffer[(size_t) src];
                ringWrite = (ringWrite + 1) % kSize;
            });

            for (int i = 0; i < kSize; ++i)
                fftData[(size_t) i] = ring[(size_t) ((ringWrite + i) % kSize)] * window[(size_t) i];
            std::fill (fftData.begin() + kSize, fftData.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

            const float norm = 4.0f / (float) kSize;
            for (int i = 0; i < kSize / 2; ++i)
            {
                const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] * norm, -120.0f);
                auto& s = smoothed[(size_t) i];
                s = db > s ? db : s + 0.25f * (db - s);
            }
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float plotH = r.getHeight() - 22.0f;
        const float mid = plotH * 0.5f;

        // Grid
        g.setColour (theme::grid);
        for (float f : { 30.f, 40.f, 50.f, 60.f, 80.f, 100.f, 200.f, 300.f, 400.f })
            g.drawVerticalLine ((int) xForFreq (f), 0.0f, plotH);
        for (float db : { -12.f, -6.f, 6.f, 12.f })
            g.drawHorizontalLine ((int) yForEq (db, mid), 0.0f, r.getWidth());
        g.setColour (juce::Colour (0xff3a404b));
        g.drawHorizontalLine ((int) mid, 0.0f, r.getWidth());

        // Spectrum
        const double fs = proc.getCurrentSampleRate();
        juce::Path spec;
        spec.startNewSubPath (0.0f, plotH);
        for (int x = 0; x <= (int) r.getWidth(); x += 2)
        {
            const float f = freqForX ((float) x);
            const float bin = (float) (f * kSize / fs);
            const int b0 = juce::jlimit (0, kSize / 2 - 2, (int) bin);
            const float frac = bin - (float) b0;
            const float db = smoothed[(size_t) b0] * (1.0f - frac) + smoothed[(size_t) b0 + 1] * frac;
            const float y = juce::jmap (juce::jlimit (-96.0f, -6.0f, db), -96.0f, -6.0f, plotH, 8.0f);
            spec.lineTo ((float) x, y);
        }
        spec.lineTo (r.getWidth(), plotH);
        spec.closeSubPath();
        g.setColour (theme::text.withAlpha (0.07f));
        g.fillPath (spec);
        g.setColour (juce::Colour (0xff5b6270));
        g.strokePath (spec, juce::PathStrokeType (1.0f));

        // EQ curve
        const float f0 = proc.uiFreq.load();
        const float gainDb = proc.uiGainDb.load();
        const float q = proc.apvts.getRawParameterValue (ids::q)->load();
        const float harm = proc.apvts.getRawParameterValue (ids::harmonic)->load() * 0.01f;
        const bool voiced = proc.uiVoiced.load();

        juce::Path eq;
        for (int x = 0; x <= (int) r.getWidth(); ++x)
        {
            const float f = freqForX ((float) x);
            const float db = bellDb (f, f0, q, gainDb) + bellDb (f, f0 * 2.0f, q, gainDb * harm);
            const float y = yForEq (db, mid);
            if (x == 0) eq.startNewSubPath ((float) x, y); else eq.lineTo ((float) x, y);
        }
        juce::Path eqFill (eq);
        eqFill.lineTo (r.getWidth(), mid);
        eqFill.lineTo (0.0f, mid);
        eqFill.closeSubPath();
        g.setColour (theme::accent.withAlpha (0.14f));
        g.fillPath (eqFill);
        g.setColour (theme::accent);
        g.strokePath (eq, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (voiced && f0 > 15.0f)
        {
            const float x0 = xForFreq (f0);
            const float y0 = yForEq (gainDb, mid);
            g.setColour (theme::accent.withAlpha (0.6f));
            const float dashes[] = { 3.0f, 4.0f };
            g.drawDashedLine ({ x0, 18.0f, x0, plotH }, dashes, 2, 1.0f);

            g.setColour (theme::bg);
            g.fillEllipse (x0 - 6.0f, y0 - 6.0f, 12.0f, 12.0f);
            g.setColour (theme::accent);
            g.drawEllipse (x0 - 6.0f, y0 - 6.0f, 12.0f, 12.0f, 2.0f);

            const auto tag = noteName ((int) std::round (proc.uiMidi.load())) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (f0, 1) + " Hz";
            auto box = juce::Rectangle<float> (x0 + 8.0f, 6.0f, 104.0f, 22.0f);
            if (box.getRight() > r.getWidth()) box.setX (x0 - 112.0f);
            g.setColour (theme::well);
            g.fillRoundedRectangle (box, 5.0f);
            g.setColour (theme::line);
            g.drawRoundedRectangle (box, 5.0f, 1.0f);
            g.setColour (theme::text);
            g.setFont (theme::mono (11.0f));
            g.drawText (tag, box, juce::Justification::centred);
        }

        // Labels
        g.setColour (theme::faint);
        g.setFont (theme::sans (11.0f));
        for (auto [f, t] : std::initializer_list<std::pair<float, const char*>> { { 30.f, "30" }, { 50.f, "50" }, { 100.f, "100" }, { 200.f, "200" }, { 400.f, "400 Hz" } })
            g.drawText (t, juce::Rectangle<float> (xForFreq (f) - 30.0f, plotH + 4.0f, 60.0f, 16.0f), juce::Justification::centred);
        g.drawText ("+6",   juce::Rectangle<float> (6.0f, yForEq (6.0f, mid) - 16.0f, 40.0f, 14.0f), juce::Justification::left);
        g.drawText ("0 dB", juce::Rectangle<float> (6.0f, mid - 16.0f, 40.0f, 14.0f), juce::Justification::left);
        g.drawText ("-6",   juce::Rectangle<float> (6.0f, yForEq (-6.0f, mid) - 16.0f, 40.0f, 14.0f), juce::Justification::left);
    }

private:
    static constexpr int kOrder = 13, kSize = 1 << kOrder;

    float xForFreq (float f) const { return (float) getWidth() * std::log (f / 20.0f) / std::log (25.0f); }
    float freqForX (float x) const { return 20.0f * std::pow (25.0f, x / (float) getWidth()); }
    static float yForEq (float db, float mid) { return mid - db * (mid - 8.0f) / 15.0f; }

    static float bellDb (float f, float f0, float q, float gainDb)
    {
        if (f0 <= 0.0f || std::abs (gainDb) < 0.01f) return 0.0f;
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float k = 1.0f / (q * A);
        const float w = f / f0;
        const std::complex<float> s (0.0f, w);
        const auto bp = s / (s * s + k * s + 1.0f);
        const auto h = 1.0f + k * (A * A - 1.0f) * bp;
        return 20.0f * std::log10 (std::abs (h));
    }

    BassLevelerProcessor& proc;
    juce::dsp::FFT fft { kOrder };
    std::vector<float> ring, window, fftData, smoothed;
    int ringWrite = 0;
};

//==============================================================================
/** Before/after level at the fundamental for the most recent notes. */
class HistoryView : public juce::Component
{
public:
    explicit HistoryView (BassLevelerProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float mid = r.getHeight() * 0.5f;
        const float pxPerDb = 4.5f;

        const float dashes[] = { 4.0f, 4.0f };
        g.setColour (juce::Colour (0xff4a505c));
        g.drawDashedLine ({ 0.0f, mid, r.getWidth(), mid }, dashes, 2, 1.0f);

        int count = 0;
        const auto hist = proc.getHistory (count);
        const float target = proc.apvts.getRawParameterValue (ids::target)->load();
        const int n = BassLevelerProcessor::kHistory;
        const float colW = r.getWidth() / (float) n;

        if (count == 0)
        {
            g.setColour (theme::faint);
            g.setFont (theme::sans (12.0f));
            g.drawText ("Play some bass to see the leveling", r, juce::Justification::centred);
            return;
        }

        for (int i = 0; i < count; ++i)
        {
            const int slot = n - count + i;
            const float x = colW * ((float) slot + 0.5f);
            auto yOf = [&] (float db) { return juce::jlimit (4.0f, r.getHeight() - 4.0f, mid - (db - target) * pxPerDb); };
            const float yb = yOf (hist[(size_t) i].before);
            const float ya = yOf (hist[(size_t) i].after);
            g.setColour (juce::Colour (0xff3a404b));
            g.fillRect (x - 1.0f, juce::jmin (yb, ya), 2.0f, std::abs (yb - ya));
            g.setColour (theme::dot);
            g.fillEllipse (x - 4.0f, yb - 4.0f, 8.0f, 8.0f);
            g.setColour (theme::accent);
            g.fillEllipse (x - 4.0f, ya - 4.0f, 8.0f, 8.0f);
        }
    }

private:
    BassLevelerProcessor& proc;
};

//==============================================================================
/** Drawable per-note correction map with a piano strip underneath. */
class NoteMapView : public juce::Component
{
public:
    enum class Tool { pencil, smooth, flatten };

    explicit NoteMapView (BassLevelerProcessor& p) : proc (p) {}

    Tool tool = Tool::pencil;
    int selected = 5;   // A1
    std::function<void()> onSelectionChanged;

    void paint (juce::Graphics& g) override
    {
        const int n = BassLevelerProcessor::kNumMapNotes;
        const auto chart = chartArea();
        const float colW = chart.getWidth() / (float) n;
        const float mid = chart.getCentreY();
        const int playing = proc.uiVoiced.load() ? (int) std::round (proc.uiMidi.load()) - BassLevelerProcessor::kFirstMapNote : -1;

        // Learned average, for the deviation ticks
        float mean = 0.0f; int heard = 0;
        for (int i = 0; i < n; ++i)
            if (const int c = proc.learnCount[(size_t) i].load(); c > 0) { mean += proc.learnSum[(size_t) i].load() / (float) c; ++heard; }
        if (heard > 0) mean /= (float) heard;

        const float dashes[] = { 4.0f, 4.0f };
        g.setColour (theme::grid);
        g.drawDashedLine ({ chart.getX(), yForDb (6.0f), chart.getRight(), yForDb (6.0f) }, dashes, 2, 1.0f);
        g.drawDashedLine ({ chart.getX(), yForDb (-6.0f), chart.getRight(), yForDb (-6.0f) }, dashes, 2, 1.0f);
        g.setColour (juce::Colour (0xff3a404b));
        g.drawHorizontalLine ((int) mid, chart.getX(), chart.getRight());

        for (int i = 0; i < n; ++i)
        {
            const auto col = juce::Rectangle<float> (chart.getX() + colW * (float) i, chart.getY(), colW, chart.getHeight());
            const float cx = col.getCentreX();

            if (i == selected || i == playing)
            {
                g.setColour (theme::accent.withAlpha (i == playing ? 0.14f : 0.07f));
                g.fillRoundedRectangle (col.reduced (1.0f, 0.0f), 4.0f);
            }

            const float v = proc.noteMap[(size_t) i].load();
            const float yv = yForDb (v);
            g.setColour (theme::gain.withAlpha (0.85f));
            g.fillRoundedRectangle (cx - 6.0f, juce::jmin (yv, mid), 12.0f, juce::jmax (1.0f, std::abs (yv - mid)), 3.0f);
            g.setColour (theme::bg);
            g.fillEllipse (cx - 6.0f, yv - 6.0f, 12.0f, 12.0f);
            g.setColour (theme::gain);
            g.drawEllipse (cx - 6.0f, yv - 6.0f, 12.0f, 12.0f, 2.0f);

            if (const int c = proc.learnCount[(size_t) i].load(); c > 0)
            {
                const float dev = proc.learnSum[(size_t) i].load() / (float) c - mean;
                g.setColour (theme::faint);
                g.fillRect (cx - 8.0f, yForDb (dev) - 1.0f, 16.0f, 2.0f);
            }

            // Piano key
            const int midi = i + BassLevelerProcessor::kFirstMapNote;
            const auto name = noteName (midi);
            const bool black = name.containsChar ('#');
            const auto key = juce::Rectangle<float> (col.getX() + 1.0f, chart.getBottom() + 4.0f, colW - 2.0f, 34.0f);
            g.setColour (i == selected ? theme::accent : (black ? theme::line : juce::Colour (0xffc9cdd4)));
            g.fillRoundedRectangle (key, 3.0f);
            if (midi % 12 == 0 || i == 0 || i == selected)
            {
                g.setColour (i == selected ? theme::well : (black ? theme::muted : theme::line));
                g.setFont (theme::sans (10.0f, true));
                g.drawText (name, key.withTrimmedBottom (3.0f), juce::Justification::centredBottom);
            }
        }

        g.setColour (theme::faint);
        g.setFont (theme::sans (11.0f));
        g.drawText ("+12", juce::Rectangle<float> (chart.getX() + 2.0f, chart.getY(), 30.0f, 14.0f), juce::Justification::left);
        g.drawText ("-12", juce::Rectangle<float> (chart.getX() + 2.0f, chart.getBottom() - 14.0f, 30.0f, 14.0f), juce::Justification::left);
    }

    void mouseDown (const juce::MouseEvent& e) override { edit (e, true); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e, false); }

private:
    juce::Rectangle<float> chartArea() const { return getLocalBounds().toFloat().withTrimmedBottom (38.0f); }
    float yForDb (float db) const { const auto c = chartArea(); return c.getCentreY() - db * (c.getHeight() * 0.5f - 6.0f) / 12.0f; }
    float dbForY (float y) const { const auto c = chartArea(); return (c.getCentreY() - y) * 12.0f / (c.getHeight() * 0.5f - 6.0f); }

    void edit (const juce::MouseEvent& e, bool isDown)
    {
        const int n = BassLevelerProcessor::kNumMapNotes;
        const auto chart = chartArea();
        const int i = juce::jlimit (0, n - 1, (int) ((e.position.x - chart.getX()) / (chart.getWidth() / (float) n)));

        if (isDown && i != selected) { selected = i; if (onSelectionChanged) onSelectionChanged(); }
        if (e.position.y > chart.getBottom()) { repaint(); return; }   // clicking the piano only selects

        const float cur = proc.noteMap[(size_t) i].load();
        float v = cur;
        switch (tool)
        {
            case Tool::pencil:
                v = std::round (dbForY (e.position.y) * 10.0f) / 10.0f;
                if (e.mods.isAltDown()) v = 0.0f;
                break;
            case Tool::smooth:
            {
                const float l = proc.noteMap[(size_t) juce::jmax (0, i - 1)].load();
                const float r = proc.noteMap[(size_t) juce::jmin (n - 1, i + 1)].load();
                v = cur + 0.3f * ((l + r) * 0.5f - cur);
                break;
            }
            case Tool::flatten:
                v = cur * 0.7f;
                break;
        }
        proc.setNoteMapValue (i, juce::jlimit (-12.0f, 12.0f, v));
        if (i != selected) { selected = i; if (onSelectionChanged) onSelectionChanged(); }
        repaint();
    }

    BassLevelerProcessor& proc;
};
