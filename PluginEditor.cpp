#include "PluginEditor.h"

namespace
{
    // Built with fromUTF8 so the bytes are never mistaken for Latin-1 (which garbles them on Windows)
    const juce::String kMinus  = juce::String::fromUTF8 ("\xe2\x88\x92");
    const juce::String kPlusMinus = juce::String::fromUTF8 ("\xc2\xb1");
    const juce::String kDegree = juce::String::fromUTF8 ("\xc2\xb0");
    const juce::String kDash   = juce::String::fromUTF8 ("\xe2\x80\x94");
    const juce::String kPencil = juce::String::fromUTF8 ("\xe2\x9c\x8e");
    const juce::String kArrow  = juce::String::fromUTF8 ("\xe2\x86\x92");
    const juce::String kDot    = juce::String::fromUTF8 ("\xc2\xb7");

    juce::Font uiFont (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }
    juce::Font monoFont (float h)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
    }
    juce::Font titleFont()
    {
        return uiFont (11.0f, true).withExtraKerningFactor (0.1f);
    }
    void drawPanel (juce::Graphics& g, juce::Rectangle<int> r)
    {
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (theme::border);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }
    juce::String signedMs (float v, int decimals = 2)
    {
        return (v >= 0.0f ? juce::String ("+") : kMinus) + juce::String (std::abs (v), decimals) + " ms";
    }
}

//==============================================================================
PLLookAndFeel::PLLookAndFeel()
{
    setColour (juce::TextButton::textColourOffId, theme::textSoft);
    setColour (juce::TextButton::textColourOnId, theme::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void PLLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                      juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const float x0 = (float) x + 7.0f, x1 = (float) (x + w) - 7.0f;
    g.setColour (theme::line);
    g.fillRoundedRectangle (x0, cy - 2.0f, x1 - x0, 4.0f, 2.0f);

    const bool bipolar = s.getProperties()["bipolar"];
    const float from = bipolar ? (float) s.getPositionOfValue (0.0) : x0;
    const auto accent = s.findColour (juce::Slider::trackColourId);
    g.setColour (accent);
    g.fillRoundedRectangle (juce::jmin (from, pos), cy - 2.0f, std::abs (pos - from), 4.0f, 2.0f);

    g.setColour (s.isEnabled() ? theme::text : theme::muted);
    g.fillEllipse (pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
    if (s.hasKeyboardFocus (false))
    {
        g.setColour (theme::bassSoft);
        g.drawEllipse (pos - 10.0f, cy - 10.0f, 20.0f, 20.0f, 1.5f);
    }
}

void PLLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto style = b.getProperties()["style"].toString();
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();

    if (style == "pill")
    {
        r = r.reduced (0.5f);
        g.setColour (on ? theme::text : (over ? theme::controlOn : theme::control));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (on ? theme::text : theme::line);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    }
    else if (style == "seg")
    {
        g.setColour (on ? theme::controlOn : (over ? theme::controlOn.withAlpha (0.6f) : theme::control));
        g.fillRect (r);
    }
    else
    {
        r = r.reduced (0.5f);
        g.setColour (down ? theme::controlOn : (over ? theme::controlOn.withAlpha (0.7f) : theme::control));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (theme::line);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }
}

juce::Font PLLookAndFeel::getTextButtonFont (juce::TextButton& b, int)
{
    if (b.getProperties()["mono"]) return monoFont (12.0f);
    return uiFont (12.5f);
}

//==============================================================================
void CurveButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const bool on = getToggleState();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (on ? theme::kick : (over ? theme::controlOn : theme::control));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? theme::kick : theme::line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    // icon: the release shape, from fully ducked (bottom) to full level (top)
    auto a = r.reduced (9.0f, 7.0f);
    const slope::CustomCurve dummy {};
    juce::Path p;
    for (int i = 0; i <= 48; ++i)
    {
        const float x = (float) i / 48.0f;
        const float v = slope::shape (shapeIdx, x, dummy);
        const juce::Point<float> pt (a.getX() + x * a.getWidth(), a.getBottom() - v * a.getHeight());
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    g.setColour (on ? juce::Colour (0xff14171c) : (over ? theme::text : theme::muted));
    g.strokePath (p, juce::PathStrokeType (on ? 2.0f : 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
ShiftRangeBar::ShiftRangeBar (PhaseLockProcessor& p)
    : proc (p),
      loAtt (*p.apvts.getParameter (ids::shiftMin), [this] (float) { repaint(); }),
      hiAtt (*p.apvts.getParameter (ids::shiftMax), [this] (float) { repaint(); })
{
    setTooltip ("Drag the edges to limit how far earlier / later the bass may move. "
                "Drag the middle to slide the window. Shift-drag = symmetric. Double-click = " + kPlusMinus + "5 ms.");
}

float ShiftRangeBar::scaleMs() const
{
    static const float s[] = { 2.0f, 5.0f, 10.0f, 20.0f };
    return s[juce::jlimit (0, 3, (int) proc.apvts.getRawParameterValue (ids::viewScale)->load())];
}

juce::Rectangle<float> ShiftRangeBar::trackArea() const
{
    return getLocalBounds().toFloat().reduced (8.0f, 0.0f).withTrimmedBottom (14.0f);
}

float ShiftRangeBar::msToX (float ms) const
{
    const auto t = trackArea();
    const float s = scaleMs();
    return t.getX() + (ms + s) / (2.0f * s) * t.getWidth();
}

float ShiftRangeBar::xToMs (float x) const
{
    const auto t = trackArea();
    const float s = scaleMs();
    return (x - t.getX()) / t.getWidth() * 2.0f * s - s;
}

ShiftRangeBar::Target ShiftRangeBar::hitTest (float x) const
{
    const auto [lo, hi] = proc.getShiftLimits();
    const auto t = trackArea();
    const float xl = juce::jlimit (t.getX(), t.getRight(), msToX (lo));
    const float xh = juce::jlimit (t.getX(), t.getRight(), msToX (hi));
    const float dl = std::abs (x - xl), dh = std::abs (x - xh);
    if (juce::jmin (dl, dh) <= 9.0f) return dl < dh || (dl == dh && x < xl) ? Target::Lo : Target::Hi;
    if (x > xl && x < xh) return Target::Both;
    return x < xl ? Target::Lo : Target::Hi;
}

void ShiftRangeBar::setLimits (float lo, float hi)
{
    const float m = PhaseLockProcessor::maxOffsetMs;
    lo = std::round (juce::jlimit (-m, m, lo) * 10.0f) / 10.0f;
    hi = std::round (juce::jlimit (-m, m, hi) * 10.0f) / 10.0f;
    loAtt.setValueAsPartOfGesture (lo);
    hiAtt.setValueAsPartOfGesture (hi);
    repaint();
}

void ShiftRangeBar::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (hitTest (e.position.x) == Target::Both ? juce::MouseCursor::DraggingHandCursor
                                                           : juce::MouseCursor::LeftRightResizeCursor);
}

void ShiftRangeBar::mouseDown (const juce::MouseEvent& e)
{
    const auto lim = proc.getShiftLimits();
    startLo = lim.first; startHi = lim.second;
    startMs = xToMs (e.position.x);
    dragging = hitTest (e.position.x);
    loAtt.beginGesture();
    hiAtt.beginGesture();
    if (dragging != Target::Both)
        mouseDrag (e);   // clicking outside the window jumps that edge there
}

void ShiftRangeBar::mouseDrag (const juce::MouseEvent& e)
{
    const float m = PhaseLockProcessor::maxOffsetMs, minWidth = 0.1f;
    const float ms = xToMs (e.position.x);
    const bool symmetric = e.mods.isShiftDown();

    if (dragging == Target::Lo)
    {
        const float lo = juce::jmin (ms, (symmetric ? 0.0f : startHi) - (symmetric ? minWidth * 0.5f : minWidth));
        setLimits (lo, symmetric ? -lo : startHi);
    }
    else if (dragging == Target::Hi)
    {
        const float hi = juce::jmax (ms, (symmetric ? 0.0f : startLo) + (symmetric ? minWidth * 0.5f : minWidth));
        setLimits (symmetric ? -hi : startLo, hi);
    }
    else if (dragging == Target::Both)
    {
        const float delta = juce::jlimit (-m - startLo, m - startHi, ms - startMs);
        setLimits (startLo + delta, startHi + delta);
    }
}

void ShiftRangeBar::mouseUp (const juce::MouseEvent&)
{
    if (dragging != Target::None) { loAtt.endGesture(); hiAtt.endGesture(); }
    dragging = Target::None;
}

void ShiftRangeBar::mouseDoubleClick (const juce::MouseEvent&)
{
    loAtt.setValueAsCompleteGesture (-5.0f);
    hiAtt.setValueAsCompleteGesture (5.0f);
}

void ShiftRangeBar::paint (juce::Graphics& g)
{
    const auto t = trackArea();
    const float cy = t.getCentreY();
    const float s = scaleMs();
    const auto [lo, hi] = proc.getShiftLimits();

    // track + ticks
    g.setColour (theme::well);
    g.fillRoundedRectangle (t.getX() - 8.0f, t.getY(), t.getWidth() + 16.0f, t.getHeight(), 6.0f);
    g.setColour (theme::line);
    g.fillRoundedRectangle (t.getX(), cy - 3.0f, t.getWidth(), 6.0f, 3.0f);
    for (int i = -4; i <= 4; ++i)
    {
        const float x = msToX (s * (float) i / 4.0f);
        g.setColour (i == 0 ? theme::muted : theme::controlOn);
        g.fillRect (x - 0.5f, cy + (i == 0 ? -10.0f : -6.0f), 1.0f, i == 0 ? 20.0f : 12.0f);
    }

    // allowed window
    const float xl = msToX (lo), xh = msToX (hi);
    const float cl = juce::jlimit (t.getX(), t.getRight(), xl), ch = juce::jlimit (t.getX(), t.getRight(), xh);
    if (ch > cl)
    {
        g.setColour (theme::bass.withAlpha (0.35f));
        g.fillRoundedRectangle (cl, cy - 7.0f, ch - cl, 14.0f, 4.0f);
        g.setColour (theme::bass);
        g.drawRoundedRectangle (cl, cy - 7.0f, ch - cl, 14.0f, 4.0f, 1.0f);
    }

    auto handle = [&] (float x, bool clipped, bool left)
    {
        g.setColour (theme::text);
        if (! clipped)
            g.fillRoundedRectangle (x - 3.0f, cy - 11.0f, 6.0f, 22.0f, 3.0f);
        else
        {
            juce::Path a;   // window continues past the visible scale
            const float d = left ? -1.0f : 1.0f;
            a.addTriangle (x + d * 6.0f, cy, x - d * 2.0f, cy - 7.0f, x - d * 2.0f, cy + 7.0f);
            g.fillPath (a);
        }
    };
    handle (cl, xl < t.getX() - 0.5f || xl > t.getRight() + 0.5f, true);
    handle (ch, xh > t.getRight() + 0.5f || xh < t.getX() - 0.5f, false);

    // current offset (white line) and best match (dot)
    const float off = proc.getEffectiveOffsetMs();
    const float xo = msToX (off);
    if (xo >= t.getX() && xo <= t.getRight())
    {
        g.setColour (theme::kick);
        g.fillRect (xo - 1.0f, cy - 9.0f, 2.0f, 18.0f);
    }
    if (proc.hasAnalysis())
    {
        const float xb = msToX (proc.getAnalysedOffsetMs());
        if (xb >= t.getX() && xb <= t.getRight())
        {
            g.setColour (theme::bassSoft);
            g.fillEllipse (xb - 2.5f, t.getY() + 1.0f, 5.0f, 5.0f);
        }
    }

    // scale labels
    g.setFont (monoFont (10.0f));
    g.setColour (theme::faint);
    auto lab = getLocalBounds().removeFromBottom (12).reduced (2, 0);
    const auto sv = juce::String ((int) s);
    g.drawText (kMinus + sv + " ms", lab, juce::Justification::centredLeft);
    g.drawText ("0", lab, juce::Justification::centred);
    g.drawText ("+" + sv + " ms", lab, juce::Justification::centredRight);
}

//==============================================================================
void LockButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const bool on = getToggleState();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (on ? theme::bass : (over ? theme::controlOn : theme::control));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (on ? theme::bass : theme::controlOn);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    const juce::String label = on ? "LOCKED" : "LOCK TO KICK";
    const auto f = uiFont (14.0f, true).withExtraKerningFactor (0.04f);
    const float tw = juce::GlyphArrangement::getStringWidth (f, label);
    const float total = 16.0f + 8.0f + tw;
    float x = r.getCentreX() - total * 0.5f;
    const float cy = r.getCentreY();
    const auto ink = on ? juce::Colour (0xff0e1014) : theme::text;

    juce::Path lock;
    lock.addRoundedRectangle (x + 1.5f, cy - 1.5f, 13.0f, 9.0f, 2.0f);
    lock.startNewSubPath (x + 4.5f, cy - 1.5f);
    lock.lineTo (x + 4.5f, cy - 4.5f);
    lock.addCentredArc (x + 8.0f, cy - 4.5f, 3.5f, 3.5f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
    lock.lineTo (x + 11.5f, cy - 1.5f);
    g.setColour (ink);
    g.strokePath (lock, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setFont (f);
    g.drawText (label, juce::Rectangle<float> (x + 24.0f, r.getY(), tw + 4.0f, r.getHeight()), juce::Justification::centredLeft);
}

//==============================================================================
ScopeView::ScopeView (PhaseLockProcessor& p)
    : proc (p), vblank (this, [this] { update(); })
{
}

void ScopeView::update()
{
    if (frozen) return;
    const auto now = juce::Time::getMillisecondCounter();
    const bool wantLong = proc.apvts.getRawParameterValue (ids::scopeLen)->load() > 0.5f;
    PhaseLockProcessor::ScopeFrame f;
    f.serial = seenSerial;
    if (proc.copyScope (f))
    {
        seenSerial = f.serial;
        if (f.envelope == wantLong)   // ignore a leftover picture from the other kind of view
        {
            if (hasFrame) { prevFrame = frame; hasPrev = true; }
            frame = f;
            hasFrame = true;
            lastFrameTime = now;
        }
    }
    if (proc.copyLiveScope (live))
    {
        hasLive = true;
        liveTime = now;
        // first sweep in a new length: start from a blank picture of the right size
        if (live.active && (! hasFrame || ! frame.envelope || std::abs (frame.lengthQ - live.lengthQ) > 1.0e-4f))
        {
            frame = live;
            frame.kMin.fill (0.0f); frame.kMax.fill (0.0f);
            frame.bMin.fill (0.0f); frame.bMax.fill (0.0f);
            frame.gain.fill (1.0f);
            hasFrame = true; hasPrev = false;
            lastFrameTime = now;
        }
    }
    repaint();
}

void ScopeView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::well);
    g.fillRoundedRectangle (r, 6.0f);

    const float w = r.getWidth(), h = r.getHeight();
    const float envTop = 14.0f, envH = 50.0f;
    const float mid = 70.0f + (h - 90.0f) * 0.5f, amp = (h - 90.0f) * 0.5f - 6.0f;

    const int wantedLen = (int) proc.apvts.getRawParameterValue (ids::scopeLen)->load();
    const bool wantLong = wantedLen > 0;
    const bool showLong = hasFrame && frame.envelope && wantLong;
    const bool showShort = hasFrame && ! frame.envelope && ! wantLong;

    g.setColour (theme::grid);
    g.drawHorizontalLine ((int) (envTop + envH + 6.0f), 0.0f, w);
    g.setFont (monoFont (10.0f));

    juce::String info;
    if (showLong)
    {
        // grid on 1/16 notes, stronger lines on beats
        const float q = juce::jmax (0.25f, frame.lengthQ);
        const int sixteenths = juce::jmax (1, (int) std::round (q * 4.0f));
        for (int i = 1; i < sixteenths; ++i)
        {
            const float x = w * (float) i / (float) sixteenths;
            g.setColour (i % 4 == 0 ? theme::line : theme::grid);
            g.drawVerticalLine ((int) x, 0.0f, h);
        }
        g.setColour (theme::faint);
        if (q > 1.01f)
        {
            for (int b = 0; b < (int) std::round (q); ++b)
                g.drawText ("beat " + juce::String (b + 1), (int) (w * (float) (b * 4) / (float) sixteenths) + 4, (int) h - 16, 70, 12, juce::Justification::left);
        }
        else
        {
            for (int i = 0; i < sixteenths; ++i)
                g.drawText (juce::String ((int) std::round (frame.lengthMs * (float) i / (float) sixteenths)) + " ms",
                            (int) (w * (float) i / (float) sixteenths) + 4, (int) h - 16, 60, 12, juce::Justification::left);
        }
        static const char* lenNames[] = { "40 ms", "1/16", "1/8", "1/4", "1/2", "1 bar" };
        info = juce::String (lenNames[juce::jlimit (0, 5, wantedLen)]) + " " + kDot + " " + juce::String (frame.bpm, 1) + " BPM "
             + kDot + (frame.synced ? " synced" : " free run (stopped)");
    }
    else
    {
        g.setColour (theme::grid);
        for (int i = 1; i < 8; ++i) g.drawVerticalLine ((int) (w * i / 8.0f), 0.0f, h);
        g.setColour (theme::faint);
        for (int i = 0; i <= 8; i += 2)
            g.drawText (juce::String (i * 5) + " ms", (int) (w * i / 8.0f) + 4, (int) h - 16, 50, 12, juce::Justification::left);
        info = "trig: kick " + kDot + " 40 ms";
    }
    g.setColour (theme::line);
    g.drawHorizontalLine ((int) mid, 0.0f, w);

    // sidechain gain: measured over the period (long views) or from current settings (40 ms)
    {
        juce::Path env;
        if (showLong)
        {
            const bool sw = hasLive && live.active && ! frozen && std::abs (live.lengthQ - frame.lengthQ) < 1.0e-4f;
            const int split = sw ? juce::jlimit (0, (int) frame.gain.size(), (int) (live.progress * (float) frame.gain.size())) : 0;
            for (size_t i = 0; i < frame.gain.size(); ++i)
            {
                const float gv = (int) i < split ? live.gain[i] : frame.gain[i];
                const float x = w * (float) i / (float) (frame.gain.size() - 1);
                const float y = envTop + (1.0f - juce::jlimit (0.0f, 1.0f, gv)) * envH;
                if (i == 0) env.startNewSubPath (x, y); else env.lineTo (x, y);
            }
        }
        else
        {
            auto& ap = proc.apvts;
            const float depth = ap.getRawParameterValue (ids::depth)->load() / 100.0f;
            const float att = ap.getRawParameterValue (ids::attack)->load();
            const float rel = ap.getRawParameterValue (ids::release)->load();
            const int sl = (int) ap.getRawParameterValue (ids::slope)->load();
            const auto curve = proc.getCustomCurve();
            for (int i = 0; i <= 200; ++i)
            {
                const float t = PhaseLockProcessor::scopeMs * (float) i / 200.0f;
                const float gn = slope::gainAt (t, att, rel, depth, sl, curve);
                const float x = w * (float) i / 200.0f, y = envTop + (1.0f - gn) * envH;
                if (i == 0) env.startNewSubPath (x, y); else env.lineTo (x, y);
            }
        }
        juce::Path dashed;
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, env, dashes, 2);
        g.setColour (theme::muted.withAlpha (0.8f));
        g.fillPath (dashed);
    }

    const float ageLimit = juce::jmax (2500.0f, (hasFrame ? frame.lengthMs : 0.0f) * 3.0f);
    const bool stale = ! (showLong || showShort) || (! frozen && (float) (juce::Time::getMillisecondCounter() - lastFrameTime) > ageLimit);
    if (frozen) info = juce::String ("FROZEN ") + kDot + " " + info;

    const auto now = juce::Time::getMillisecondCounter();

    if (showShort)
    {
        // a new kick picture wipes in from the left over the old one
        const float wipeMs = 160.0f;
        const float reveal = hasPrev && ! frozen ? juce::jlimit (0.0f, 1.0f, (float) (now - lastFrameTime) / wipeMs) : 1.0f;
        auto trace = [&] (const std::array<float, PhaseLockProcessor::scopePoints>& d, juce::Colour c, float from, float to, float alpha)
        {
            float peak = 1.0e-6f;
            for (auto v : d) peak = juce::jmax (peak, std::abs (v));
            const int n = (int) d.size();
            const int i0 = (int) std::floor (from * (float) (n - 1)), i1 = (int) std::ceil (to * (float) (n - 1));
            if (i1 <= i0) return;
            juce::Path p;
            for (int i = i0; i <= i1; ++i)
            {
                const float x = w * (float) i / (float) (n - 1);
                const float y = mid - d[(size_t) i] / peak * amp;
                if (i == i0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (c.withAlpha (stale ? alpha * 0.35f : alpha));
            g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
        };
        if (reveal < 1.0f && ! prevFrame.envelope)
        {
            trace (prevFrame.kick, theme::kick, reveal, 1.0f, 0.3f);
            trace (prevFrame.bass, theme::bass, reveal, 1.0f, 0.3f);
        }
        trace (frame.kick, theme::kick, 0.0f, reveal, 1.0f);
        trace (frame.bass, theme::bass, 0.0f, reveal, 1.0f);
        if (reveal < 1.0f)
        {
            const float x = reveal * w;
            g.setColour (theme::text.withAlpha (0.9f * (1.0f - reveal)));
            g.fillRect (x - 1.0f, 0.0f, 2.0f, h);
        }
    }
    else if (showLong)
    {
        // Sweep: the period being recorded right now is drawn from the left up to the
        // playhead; the previous complete picture stays visible (dimmed) to the right.
        const bool sweeping = hasLive && live.active && ! frozen && std::abs (live.lengthQ - frame.lengthQ) < 1.0e-4f;
        float head = 1.0f;
        if (sweeping)
        {
            // move the playhead smoothly between audio-block updates
            const float extra = (float) (now - liveTime) / juce::jmax (1.0f, live.lengthMs);
            head = juce::jlimit (0.0f, 1.0f, live.progress + juce::jmin (extra, 0.03f));
        }

        const int n = PhaseLockProcessor::scopePoints;
        auto peakOf = [] (const std::array<float, PhaseLockProcessor::scopePoints>& lo,
                          const std::array<float, PhaseLockProcessor::scopePoints>& hi, int count)
        {
            float pk = 1.0e-6f;
            for (int i = 0; i < count; ++i) pk = juce::jmax (pk, std::abs (lo[(size_t) i]), std::abs (hi[(size_t) i]));
            return pk;
        };
        const int liveCount = sweeping ? juce::jlimit (0, n, (int) (live.progress * (float) n)) : 0;
        // keep the scale steady during the sweep: old picture's peak, grown if the new one is louder
        const float kPeak = juce::jmax (peakOf (frame.kMin, frame.kMax, n), sweeping ? peakOf (live.kMin, live.kMax, liveCount) : 0.0f);
        const float bPeak = juce::jmax (peakOf (frame.bMin, frame.bMax, n), sweeping ? peakOf (live.bMin, live.bMax, liveCount) : 0.0f);

        auto band = [&] (const std::array<float, PhaseLockProcessor::scopePoints>& lo,
                         const std::array<float, PhaseLockProcessor::scopePoints>& hi,
                         float peak, int i0, int i1, juce::Colour c, float alpha)
        {
            i1 = juce::jmin (i1, n - 1);
            if (i1 <= i0) return;
            juce::Path p;
            for (int i = i0; i <= i1; ++i)
            {
                const float x = w * (float) i / (float) (n - 1);
                const float y = mid - hi[(size_t) i] / peak * amp - 0.5f;
                if (i == i0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            for (int i = i1; i >= i0; --i)
                p.lineTo (w * (float) i / (float) (n - 1), mid - lo[(size_t) i] / peak * amp + 0.5f);
            p.closeSubPath();
            g.setColour (c.withAlpha (stale ? alpha * 0.4f : alpha));
            g.fillPath (p);
        };

        const int split = sweeping ? liveCount : n;
        // previous picture to the right of the playhead
        if (sweeping)
        {
            band (frame.bMin, frame.bMax, bPeak, split, n - 1, theme::bass, 0.18f);
            band (frame.kMin, frame.kMax, kPeak, split, n - 1, theme::kick, 0.25f);
        }
        const auto& src = sweeping ? live : frame;
        band (src.bMin, src.bMax, bPeak, 0, split, theme::bass, 0.5f);
        band (src.kMin, src.kMax, kPeak, 0, split, theme::kick, 0.7f);

        if (sweeping)
        {
            const float x = head * w;
            // soft trail + playhead line, like a DAW playlist
            juce::ColourGradient trail (theme::text.withAlpha (0.0f), x - 40.0f, 0.0f, theme::text.withAlpha (0.10f), x, 0.0f, false);
            g.setGradientFill (trail);
            g.fillRect (x - 40.0f, 0.0f, 40.0f, h);
            g.setColour (theme::text.withAlpha (0.9f));
            g.fillRect (x - 1.0f, 0.0f, 2.0f, h);
            juce::Path tri;
            tri.addTriangle (x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 6.0f);
            g.fillPath (tri);
        }
    }

    g.setFont (monoFont (10.0f));
    {
        const int tw = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), info) + 12;
        auto box = juce::Rectangle<int> (getWidth() - tw - 6, 6, tw, 16);
        g.setColour (theme::well.withAlpha (0.9f));
        g.fillRoundedRectangle (box.toFloat(), 4.0f);
        g.setColour (theme::faint);
        g.drawText (info, box, juce::Justification::centred);
    }

    if (stale)
    {
        g.setColour (theme::muted);
        g.setFont (uiFont (13.0f));
        juce::String msg;
        if (! proc.isSidechainActive())       msg = "Waiting for a kick on the sidechain input";
        else if (wantLong && ! showLong)      msg = "Collecting the first period" + juce::String::fromUTF8 ("\xe2\x80\xa6");
        else                                  msg = "Sidechain is quiet: lower the kick threshold";
        g.drawText (msg, getLocalBounds().withTrimmedTop (70), juce::Justification::centred);
    }
}

//==============================================================================
SlopeEditor::SlopeEditor (PhaseLockProcessor& p, std::function<void()> switchToCustom)
    : proc (p), onSwitchToCustom (std::move (switchToCustom))
{
    setTooltip ("Right-click a point: curve type / delete. Right-click empty: add point. "
                "Drag the small circles to bend a segment. Double-click: add / delete / reset bend.");
}

SlopeEditor::Geometry SlopeEditor::geometry() const
{
    auto& ap = proc.apvts;
    const float att = ap.getRawParameterValue (ids::attack)->load();
    const float rel = ap.getRawParameterValue (ids::release)->load();
    Geometry g;
    g.w = (float) getWidth(); g.h = (float) getHeight();
    g.top = 14.0f;
    g.ch = g.h - g.top - 22.0f;
    g.total = att + rel;
    g.right = g.w - 10.0f;                       // keep the end point fully visible
    g.ax = att / g.total * g.right;
    return g;
}

juce::Point<float> SlopeEditor::nodeToScreen (const slope::Node& n, const Geometry& g) const
{
    return { g.ax + n.x * (g.right - g.ax), g.top + (1.0f - n.y) * g.ch };
}

float SlopeEditor::screenToX (float px, const Geometry& g) const { return juce::jlimit (0.0f, 1.0f, (px - g.ax) / juce::jmax (1.0f, g.right - g.ax)); }
float SlopeEditor::screenToY (float py, const Geometry& g) const { return juce::jlimit (0.0f, 1.0f, 1.0f - (py - g.top) / g.ch); }

juce::Point<float> SlopeEditor::handlePos (const slope::Nodes& n, int seg, const Geometry& g) const
{
    const float x = 0.5f * (n[(size_t) seg].x + n[(size_t) seg + 1].x);
    return nodeToScreen ({ x, slope::evaluate (n, x) }, g);
}

int SlopeEditor::findNode (juce::Point<float> p, const slope::Nodes& n) const
{
    const auto g = geometry();
    int best = -1; float bd = 9.0f;
    for (int i = 0; i < (int) n.size(); ++i)
    {
        const float d = nodeToScreen (n[(size_t) i], g).getDistanceFrom (p);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

int SlopeEditor::findHandle (juce::Point<float> p, const slope::Nodes& n) const
{
    const auto g = geometry();
    for (int i = 0; i + 1 < (int) n.size(); ++i)
        if (slope::segmentHasTension (n[(size_t) i].type) && handlePos (n, i, g).getDistanceFrom (p) < 7.0f)
            return i;
    return -1;
}

slope::Nodes SlopeEditor::editableNodes()
{
    const int sl = (int) proc.apvts.getRawParameterValue (ids::slope)->load();
    if (sl != slope::Custom)
    {
        proc.setNodes (slope::presetAsNodes (sl));
        if (onSwitchToCustom) onSwitchToCustom();
    }
    return proc.getNodes();
}

void SlopeEditor::addNodeAt (juce::Point<float> p)
{
    const auto g = geometry();
    if (p.x < g.ax) return;
    auto n = editableNodes();
    const float x = screenToX (p.x, g);
    for (size_t i = 0; i + 1 < n.size(); ++i)
        if (x > n[i].x + 0.002f && x < n[i + 1].x - 0.002f)
        {
            // the new point keeps the segment's type so the shape stays familiar
            slope::Node nn { x, screenToY (p.y, g), n[i].type, n[i].tension };
            n.insert (n.begin() + (long) i + 1, nn);
            proc.setNodes (n);
            break;
        }
    repaint();
}

void SlopeEditor::showMenu (juce::Point<float> p)
{
    auto n = editableNodes();
    const int node = findNode (p, n);
    int seg = findHandle (p, n);
    if (node > 0) seg = node - 1;   // like FL: a point's menu sets the curve leading into it

    juce::PopupMenu m;
    juce::Component::SafePointer<SlopeEditor> safe (this);

    if (seg >= 0)
    {
        m.addSectionHeader ("Curve type");
        for (int t = 0; t < slope::NumSegmentTypes; ++t)
            m.addItem (slope::segmentNames[t], true, n[(size_t) seg].type == t, [safe, seg, t]
            {
                if (safe == nullptr) return;
                auto nn = safe->proc.getNodes();
                if (seg + 1 < (int) nn.size()) { nn[(size_t) seg].type = t; safe->proc.setNodes (nn); safe->repaint(); }
            });
        m.addItem ("Reset bend", [safe, seg]
        {
            if (safe == nullptr) return;
            auto nn = safe->proc.getNodes();
            if (seg + 1 < (int) nn.size()) { nn[(size_t) seg].tension = 0.0f; safe->proc.setNodes (nn); safe->repaint(); }
        });
        m.addSeparator();
    }
    if (node > 0 && node < (int) n.size() - 1)
        m.addItem ("Delete point", [safe, node]
        {
            if (safe == nullptr) return;
            auto nn = safe->proc.getNodes();
            if (node < (int) nn.size() - 1) { nn.erase (nn.begin() + node); safe->proc.setNodes (nn); safe->repaint(); }
        });
    if (node < 0)
        m.addItem ("Add point here", [safe, p] { if (safe != nullptr) safe->addNodeAt (p); });
    m.addItem ("Reset curve", [safe]
    {
        if (safe == nullptr) return;
        safe->proc.setNodes (slope::defaultNodes()); safe->repaint();
    });

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                        .withTargetScreenArea (juce::Rectangle<int> (1, 1).withPosition (localPointToGlobal (p.toInt()))));
}

void SlopeEditor::mouseMove (const juce::MouseEvent& e)
{
    const bool custom = (int) proc.apvts.getRawParameterValue (ids::slope)->load() == slope::Custom;
    const auto n = proc.getNodes();
    const int hn = custom ? findNode (e.position, n) : -1;
    const int hh = custom && hn < 0 ? findHandle (e.position, n) : -1;
    if (hn != hoverNode || hh != hoverHandle) { hoverNode = hn; hoverHandle = hh; repaint(); }
    setMouseCursor (hn >= 0 ? juce::MouseCursor::DraggingHandCursor
                            : hh >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor);
}

void SlopeEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu (e.position); return; }

    const auto n = editableNodes();
    dragNode = findNode (e.position, n);
    dragHandle = dragNode < 0 ? findHandle (e.position, n) : -1;
    if (dragHandle >= 0)
    {
        dragStartTension = n[(size_t) dragHandle].tension;
        dragStartY = e.position.y;
    }
    repaint();
}

void SlopeEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto g = geometry();
    auto n = proc.getNodes();
    if (dragNode >= 0 && dragNode < (int) n.size())
    {
        auto& p = n[(size_t) dragNode];
        p.y = screenToY (e.position.y, g);
        if (dragNode > 0 && dragNode < (int) n.size() - 1)
            p.x = juce::jlimit (n[(size_t) dragNode - 1].x + 0.005f, n[(size_t) dragNode + 1].x - 0.005f, screenToX (e.position.x, g));
        proc.setNodes (n);
    }
    else if (dragHandle >= 0 && dragHandle + 1 < (int) n.size())
    {
        // drag up = bend towards the top, whichever way the segment goes
        const float dir = n[(size_t) dragHandle + 1].y >= n[(size_t) dragHandle].y ? 1.0f : -1.0f;
        n[(size_t) dragHandle].tension = juce::jlimit (-1.0f, 1.0f, dragStartTension + dir * (dragStartY - e.position.y) / 120.0f);
        proc.setNodes (n);
    }
    repaint();
}

void SlopeEditor::mouseUp (const juce::MouseEvent&)
{
    dragNode = dragHandle = -1;
    repaint();
}

void SlopeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto n = editableNodes();
    const int node = findNode (e.position, n);
    const int handle = node < 0 ? findHandle (e.position, n) : -1;
    if (node > 0 && node < (int) n.size() - 1)          { n.erase (n.begin() + node); proc.setNodes (n); }
    else if (handle >= 0)                                { n[(size_t) handle].tension = 0.0f; proc.setNodes (n); }
    else if (node < 0)                                   addNodeAt (e.position);
    repaint();
}

void SlopeEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::well);
    g.fillRoundedRectangle (r, 6.0f);

    auto& ap = proc.apvts;
    const int sl = (int) ap.getRawParameterValue (ids::slope)->load();
    const bool custom = sl == slope::Custom;
    const float depth = ap.getRawParameterValue (ids::depth)->load() / 100.0f;
    const auto geo = geometry();
    const auto nodes = proc.getNodes();
    const slope::CustomCurve none {};

    // grid: top = full level, bottom = full duck (Depth)
    g.setColour (theme::grid);
    for (int i = 0; i <= 4; ++i) g.drawHorizontalLine ((int) (geo.top + geo.ch * (float) i / 4.0f), 0.0f, geo.w);
    g.setColour (theme::line);
    {
        juce::Path ap2; ap2.startNewSubPath (geo.ax, 0.0f); ap2.lineTo (geo.ax, geo.h);
        juce::Path dashed; const float dashes[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, ap2, dashes, 2);
        g.fillPath (dashed);
    }

    // the curve
    juce::Path line;
    const int steps = 400;
    for (int i = 0; i <= steps; ++i)
    {
        const float px = geo.w * (float) i / (float) steps;
        float v;
        if (px < geo.ax) v = 1.0f - px / juce::jmax (1.0f, geo.ax);
        else
        {
            const float x = juce::jmin (1.0f, (px - geo.ax) / juce::jmax (1.0f, geo.right - geo.ax));
            v = custom ? slope::evaluate (nodes, x) : slope::shape (sl, x, none);
        }
        const float y = geo.top + (1.0f - v) * geo.ch;
        if (i == 0) line.startNewSubPath (px, y); else line.lineTo (px, y);
    }
    juce::Path fill (line);
    fill.lineTo (geo.w, geo.h); fill.lineTo (0.0f, geo.h); fill.closeSubPath();
    g.setColour (theme::bass.withAlpha (0.14f));
    g.fillPath (fill);
    g.setColour (theme::bassSoft);
    g.strokePath (line, juce::PathStrokeType (2.0f));

    if (custom)
    {
        // bend handles
        for (int i = 0; i + 1 < (int) nodes.size(); ++i)
        {
            if (! slope::segmentHasTension (nodes[(size_t) i].type)) continue;
            const auto hp = handlePos (nodes, i, geo);
            const bool hot = i == hoverHandle || i == dragHandle;
            g.setColour (theme::well);
            g.fillEllipse (hp.x - 4.5f, hp.y - 4.5f, 9.0f, 9.0f);
            g.setColour (hot ? theme::kick : theme::text.withAlpha (0.8f));
            g.drawEllipse (hp.x - 4.5f, hp.y - 4.5f, 9.0f, 9.0f, 1.5f);
        }
        // points
        for (int i = 0; i < (int) nodes.size(); ++i)
        {
            const auto p = nodeToScreen (nodes[(size_t) i], geo);
            const bool hot = i == hoverNode || i == dragNode;
            g.setColour (hot ? theme::kick : theme::text);
            g.fillEllipse (p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);
            g.setColour (theme::well);
            g.drawEllipse (p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f, 1.5f);
        }
        // name of the hovered segment's curve type
        const int seg = hoverHandle >= 0 ? hoverHandle : (hoverNode > 0 ? hoverNode - 1 : -1);
        if (seg >= 0 && seg + 1 < (int) nodes.size())
        {
            g.setFont (uiFont (11.0f));
            g.setColour (theme::textSoft);
            g.drawText (slope::segmentNames[nodes[(size_t) seg].type], getLocalBounds().reduced (8, 0).removeFromTop (14), juce::Justification::centredRight);
        }
    }

    g.setColour (theme::faint);
    g.setFont (monoFont (10.0f));
    const auto rh = getLocalBounds().removeFromBottom (18).reduced (6, 2);
    g.drawText ("0 ms", rh, juce::Justification::centredLeft);
    g.drawText (juce::String ((int) std::round (geo.total)) + " ms", rh, juce::Justification::centredRight);
    g.drawText (depth > 0.0005f ? (depth >= 0.9995f ? juce::String ("bottom = -inf dB") : "bottom = " + juce::String (juce::Decibels::gainToDecibels (1.0f - depth), 1) + " dB")
                                : juce::String ("depth 0"), rh, juce::Justification::centred);
}

//==============================================================================
PhaseLockEditor::PhaseLockEditor (PhaseLockProcessor& p)
    : AudioProcessorEditor (&p), proc (p), shiftBar (p), scope (p),
      slopeEditor (p, [this] { slopeAtt->setValueAsCompleteGesture ((float) slope::Custom); })
{
    setLookAndFeel (&lnf);
    auto& ap = proc.apvts;

    addAndMakeVisible (scope);
    addAndMakeVisible (slopeEditor);

    auto noop = [] (float) {};
    slopeAtt  = std::make_unique<juce::ParameterAttachment> (*ap.getParameter (ids::slope), noop);
    modeAtt   = std::make_unique<juce::ParameterAttachment> (*ap.getParameter (ids::mode), noop);
    offsetAtt = std::make_unique<juce::ParameterAttachment> (*ap.getParameter (ids::offset), noop);

    auto makeButtons = [this] (juce::OwnedArray<juce::TextButton>& arr, const std::vector<juce::String>& names,
                               const char* style, juce::ParameterAttachment* att, bool mono)
    {
        for (int i = 0; i < (int) names.size(); ++i)
        {
            auto* b = arr.add (new juce::TextButton (names[(size_t) i]));
            b->getProperties().set ("style", style);
            if (mono) b->getProperties().set ("mono", true);
            if (att != nullptr)
                b->onClick = [att, i] { att->setValueAsCompleteGesture ((float) i); };
            addAndMakeVisible (b);
        }
    };
    for (int i = 0; i < slope::numPresets; ++i)
    {
        auto* b = curveButtons.add (new CurveButton (i));
        b->onClick = [this, i] { slopeAtt->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }
    customButton.setButtonText ("Custom " + kPencil);
    customButton.getProperties().set ("style", "pill");
    customButton.setTooltip ("Draw your own curve");
    customButton.setColour (juce::TextButton::textColourOnId, theme::window);
    customButton.onClick = [this] { slopeAtt->setValueAsCompleteGesture ((float) slope::Custom); };
    addAndMakeVisible (customButton);
    makeButtons (modeButtons, { "Correlation", "Zero-cross" }, "seg", modeAtt.get(), false);
    makeButtons (nudgeButtons, { kMinus + "1", kMinus + ".1", "+.1", "+1" }, "plain", nullptr, true);
    const float nudges[] = { -1.0f, -0.1f, 0.1f, 1.0f };
    for (int i = 0; i < 4; ++i)
    {
        const float d = nudges[i];
        nudgeButtons[i]->onClick = [this, d] { nudge (d); };
    }

    for (auto* s : { &depthSlider, &attackSlider, &releaseSlider, &thresholdSlider, &offsetSlider })
        setupSlider (*s);
    depthSlider.setColour (juce::Slider::trackColourId, theme::bassSoft);
    attackSlider.setColour (juce::Slider::trackColourId, theme::bassSoft);
    releaseSlider.setColour (juce::Slider::trackColourId, theme::bassSoft);
    thresholdSlider.setColour (juce::Slider::trackColourId, theme::kick);
    offsetSlider.setColour (juce::Slider::trackColourId, theme::bass);
    offsetSlider.getProperties().set ("bipolar", true);

    depthAtt     = std::make_unique<SA> (ap, ids::depth, depthSlider);
    attackAtt    = std::make_unique<SA> (ap, ids::attack, attackSlider);
    releaseAtt   = std::make_unique<SA> (ap, ids::release, releaseSlider);
    thresholdAtt = std::make_unique<SA> (ap, ids::threshold, thresholdSlider);

    addAndMakeVisible (shiftBar);
    scaleAtt = std::make_unique<juce::ParameterAttachment> (*ap.getParameter (ids::viewScale), [this] (float) { shiftBar.repaint(); });
    scopeLenAtt = std::make_unique<juce::ParameterAttachment> (*ap.getParameter (ids::scopeLen), [this] (float) { scope.repaint(); });
    makeButtons (scopeLenButtons, { "40ms", "1/16", "1/8", "1/4", "1/2", "Bar" }, "seg", scopeLenAtt.get(), true);
    freezeButton.setClickingTogglesState (true);
    freezeButton.getProperties().set ("style", "pill");
    freezeButton.setColour (juce::TextButton::textColourOnId, theme::window);
    freezeButton.setTooltip ("Hold the current picture");
    freezeButton.onClick = [this] { scope.setFrozen (freezeButton.getToggleState()); };
    addAndMakeVisible (freezeButton);
    for (auto* b : scopeLenButtons) b->setTooltip ("Oscilloscope length. Note values follow the host tempo and sync to the bar grid while playing.");
    makeButtons (scaleButtons, { kPlusMinus + "2", kPlusMinus + "5", kPlusMinus + "10", kPlusMinus + "20" }, "seg", scaleAtt.get(), true);
    for (auto* b : scaleButtons) b->setTooltip ("Zoom of the shift-limit bar");

    // Offset slider: range follows the search range, and moving it takes over from Lock.
    offsetSlider.setRange (-10.0, 10.0, 0.01);
    offsetSlider.setDoubleClickReturnValue (true, 0.0);
    offsetSlider.onDragStart = [this]
    {
        draggingOffset = true;
        proc.unlockAt ((float) offsetSlider.getValue());
        offsetAtt->beginGesture();
    };
    offsetSlider.onValueChange = [this]
    {
        if (draggingOffset) offsetAtt->setValueAsPartOfGesture ((float) offsetSlider.getValue());
        else proc.unlockAt ((float) offsetSlider.getValue());
    };
    offsetSlider.onDragEnd = [this] { offsetAtt->endGesture(); draggingOffset = false; };

    lockButton.setClickingTogglesState (true);
    addAndMakeVisible (lockButton);
    lockAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ap, ids::lock, lockButton);

    setSize (1040, 800);
    timerCallback();
    startTimerHz (30);
}

PhaseLockEditor::~PhaseLockEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PhaseLockEditor::setupSlider (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setMouseDragSensitivity (300);
    addAndMakeVisible (s);
}

void PhaseLockEditor::nudge (float delta)
{
    const auto [lo, hi] = limits();
    const float v = juce::jlimit (lo, hi, std::round ((proc.getEffectiveOffsetMs() + delta) * 100.0f) / 100.0f);
    proc.unlockAt (v);
}

void PhaseLockEditor::timerCallback()
{
    auto& ap = proc.apvts;
    const int sl = (int) ap.getRawParameterValue (ids::slope)->load();
    const int md = (int) ap.getRawParameterValue (ids::mode)->load();
    const bool locked = ap.getRawParameterValue (ids::lock)->load() > 0.5f;
    for (int i = 0; i < curveButtons.size(); ++i) curveButtons[i]->setToggleState (i == sl, juce::dontSendNotification);
    customButton.setToggleState (sl == slope::Custom, juce::dontSendNotification);
    for (int i = 0; i < modeButtons.size(); ++i)  modeButtons[i]->setToggleState (i == md, juce::dontSendNotification);

    const int sc = (int) ap.getRawParameterValue (ids::viewScale)->load();
    for (int i = 0; i < scaleButtons.size(); ++i) scaleButtons[i]->setToggleState (i == sc, juce::dontSendNotification);
    const int sLen = (int) ap.getRawParameterValue (ids::scopeLen)->load();
    for (int i = 0; i < scopeLenButtons.size(); ++i) scopeLenButtons[i]->setToggleState (i == sLen, juce::dontSendNotification);

    const auto [lo, hi] = limits();
    if (std::abs (lo - shownLo) > 0.001f || std::abs (hi - shownHi) > 0.001f)
    {
        offsetSlider.setRange (lo, juce::jmax (hi, lo + 0.01f), 0.01);
        shownLo = lo; shownHi = hi;
        const float cur = ap.getRawParameterValue (ids::offset)->load();
        if (! locked && (cur < lo - 0.001f || cur > hi + 0.001f))
            proc.unlockAt (juce::jlimit (lo, hi, cur));   // pull a manual offset inside the new limits
    }
    const float eff = proc.getEffectiveOffsetMs();
    const float shownOffset = locked ? eff : ap.getRawParameterValue (ids::offset)->load();
    if (! draggingOffset)
        offsetSlider.setValue (shownOffset, juce::dontSendNotification);

    offsetText = signedMs (shownOffset);
    const bool valid = proc.hasAnalysis();
    const float best = proc.getAnalysedOffsetMs();
    const float hz = proc.getFundamentalHz();
    scActive = proc.isSidechainActive();

    float deg = 0.0f;
    if (valid && hz > 0.0f)
    {
        deg = (eff - best) * hz / 1000.0f * 360.0f;
        deg = std::fmod (deg + 180.0f, 360.0f);
        if (deg < 0.0f) deg += 360.0f;
        deg -= 180.0f;
        phaseText = (deg >= 0.0f ? juce::String ("+") : kMinus) + juce::String ((int) std::round (std::abs (deg))) + kDegree;
    }
    else phaseText = kDash;

    bestText = valid ? signedMs (best) : kDash;
    fundText = hz > 0.0f ? juce::String (hz, 1) + " Hz" : kDash;
    corrValue = proc.getCorrelation();
    corrText = (corrValue >= 0.0f ? juce::String ("+") : kMinus) + juce::String (std::abs (corrValue), 2);
    scText = scActive ? juce::String (proc.getSidechainLevelDb(), 1) + " dB" : juce::String ("no signal");

    if (! scActive)             { statusText = "No sidechain"; statusColour = theme::muted; }
    else if (! valid)           { statusText = "Listening";    statusColour = theme::muted; }
    else if (locked)            { statusText = "Locked";       statusColour = theme::bassSoft; }
    else if (std::abs (deg) < 15.0f) { statusText = "In phase"; statusColour = theme::bassSoft; }
    else                        { statusText = "Out of phase"; statusColour = theme::kick; }

    scope.update();
    slopeEditor.repaint();
    repaint (0, 0, getWidth(), 52);
    repaint (meterArea.expanded (120, 4));
    repaint (alignPanel);
    repaint (readoutGrid);
    repaint (slopePanel.withTrimmedLeft (slopePanel.getWidth() - 240));
    repaint (slopePanel.withHeight (40));
    repaint (detectPanel.withHeight (220));
}

void PhaseLockEditor::drawSliderCaption (juce::Graphics& g, const juce::Slider& s, const juce::String& name, const juce::String& value)
{
    auto row = s.getBounds().withHeight (16).translated (0, -16).reduced (2, 0);
    g.setFont (uiFont (12.0f));
    g.setColour (theme::muted);
    g.drawText (name, row, juce::Justification::centredLeft);
    g.setFont (monoFont (12.0f));
    g.setColour (theme::text);
    g.drawText (value, row, juce::Justification::centredRight);
}

void PhaseLockEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::window);
    auto& ap = proc.apvts;

    // header
    g.setColour (theme::header);
    g.fillRect (0, 0, getWidth(), 52);
    g.setColour (theme::border);
    g.drawHorizontalLine (52, 0.0f, (float) getWidth());
    {
        juce::Path k, b;
        k.startNewSubPath (20, 26); k.cubicTo (22, 20, 24, 20, 26, 26); k.cubicTo (28, 32, 30, 32, 32, 26);
        b.startNewSubPath (28, 26); b.cubicTo (30, 20, 32, 20, 34, 26); b.cubicTo (36, 32, 38, 32, 40, 26);
        g.setColour (theme::kick); g.strokePath (k, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme::bass); g.strokePath (b, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme::text);
        g.setFont (uiFont (14.0f, true).withExtraKerningFactor (0.08f));
        g.drawText ("PHASE LOCK", 50, 0, 120, 52, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff8c95a5));
        g.setFont (uiFont (12.0f));
        g.drawText ("kick " + kArrow + " bass aligner", 160, 0, 200, 52, juce::Justification::centredLeft);

        auto pill = juce::Rectangle<float> ((float) getWidth() - 220.0f, 10.0f, 200.0f, 32.0f);
        g.setColour (scActive ? juce::Colour (0xff2a2219) : theme::control);
        g.fillRoundedRectangle (pill, 16.0f);
        g.setColour (scActive ? theme::kick : theme::muted);
        g.fillEllipse (pill.getX() + 14.0f, pill.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setFont (uiFont (12.0f, true));
        g.drawText ("SC in: " + scText, pill.withTrimmedLeft (30.0f).toNearestInt(), juce::Justification::centredLeft);
    }

    // scope panel
    drawPanel (g, scopePanel);
    {
        auto row = scopePanel.reduced (16, 0).withY (scopePanel.getY() + 12).withHeight (20);
        g.setFont (titleFont()); g.setColour (theme::muted);
        g.drawText ("OSCILLOSCOPE", row, juce::Justification::centredLeft);
        int x = row.getX() + 120;
        auto legend = [&] (juce::Colour c, const juce::String& t, bool dashed)
        {
            g.setColour (c);
            if (dashed) { g.fillRect (x, row.getCentreY() - 1, 5, 2); g.fillRect (x + 9, row.getCentreY() - 1, 5, 2); }
            else g.fillRoundedRectangle ((float) x, (float) row.getCentreY() - 1.5f, 14.0f, 3.0f, 1.5f);
            g.setColour (theme::textSoft); g.setFont (uiFont (12.0f));
            const int tw = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 2;
            g.drawText (t, x + 20, row.getY(), tw, row.getHeight(), juce::Justification::centredLeft);
            x += 20 + tw + 18;
        };
        legend (theme::kick, "Kick", false);
        legend (theme::bass, "Bass", false);
        legend (theme::muted, "SC gain", true);
        auto r = scopeLenButtons.getFirst()->getBounds().getUnion (scopeLenButtons.getLast()->getBounds()).toFloat().expanded (0.5f);
        g.setColour (theme::line); g.drawRoundedRectangle (r, 6.0f, 1.0f);

        // correlation meter
        g.setFont (uiFont (11.0f)); g.setColour (theme::muted);
        g.drawText ("Correlation", meterArea.getX() - 104, meterArea.getY() - 2, 80, 20, juce::Justification::centredLeft);
        g.setFont (monoFont (10.0f)); g.setColour (theme::faint);
        g.drawText (kMinus + "1", meterArea.getX() - 22, meterArea.getY() - 2, 18, 20, juce::Justification::centredRight);
        g.drawText ("+1", meterArea.getRight() + 4, meterArea.getY() - 2, 18, 20, juce::Justification::centredLeft);
        auto bar = meterArea.toFloat().withSizeKeepingCentre ((float) meterArea.getWidth(), 8.0f);
        g.setColour (theme::border); g.fillRoundedRectangle (bar, 4.0f);
        const float cx = bar.getCentreX();
        g.setColour (juce::Colour (0xff4a5263)); g.fillRect (cx, bar.getY() - 3.0f, 1.0f, 14.0f);
        const float v = juce::jlimit (-1.0f, 1.0f, corrValue);
        const float ex = cx + v * bar.getWidth() * 0.5f;
        g.setColour (v > 0.6f ? theme::bass : (v > 0.0f ? juce::Colour (0xff8c95a5) : theme::kick));
        g.fillRoundedRectangle (juce::jmin (cx, ex), bar.getY(), std::abs (ex - cx), 8.0f, 4.0f);
        g.setFont (monoFont (13.0f)); g.setColour (theme::text);
        g.drawText (corrText, meterArea.getRight() + 24, meterArea.getY() - 2, 52, 20, juce::Justification::centredRight);
    }

    // slope panel
    drawPanel (g, slopePanel);
    {
        auto row = slopePanel.reduced (16, 0).withY (slopePanel.getY() + 12).withHeight (20);
        g.setFont (titleFont()); g.setColour (theme::muted);
        g.drawText ("SIDECHAIN SLOPE", row, juce::Justification::centredLeft);
        g.setFont (uiFont (11.0f)); g.setColour (juce::Colour (0xff8c95a5));
        const bool custom = (int) ap.getRawParameterValue (ids::slope)->load() == slope::Custom;
        g.drawText (juce::String (slope::names[juce::jlimit (0, slope::NumShapes - 1, (int) ap.getRawParameterValue (ids::slope)->load())])
                        + "  " + kDot + "  " + (custom ? "right-click points for curve types" : "click the curve to edit it"),
                    row.withTrimmedLeft (130).withRight (customButton.getX() - 8), juce::Justification::centredLeft);
        drawSliderCaption (g, depthSlider, "Depth", ap.getParameter (ids::depth)->getCurrentValueAsText());
        drawSliderCaption (g, attackSlider, "Attack", ap.getParameter (ids::attack)->getCurrentValueAsText());
        drawSliderCaption (g, releaseSlider, "Release", ap.getParameter (ids::release)->getCurrentValueAsText());
    }

    // align panel
    drawPanel (g, alignPanel);
    {
        auto row = alignPanel.reduced (16, 0).withY (alignPanel.getY() + 14).withHeight (20);
        g.setFont (titleFont()); g.setColour (theme::muted);
        g.drawText ("ALIGN", row, juce::Justification::centredLeft);
        g.setFont (uiFont (12.0f, true)); g.setColour (statusColour);
        const int tw = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), statusText) + 2;
        g.drawText (statusText, row.getRight() - tw, row.getY(), tw, row.getHeight(), juce::Justification::centredRight);
        g.fillEllipse ((float) (row.getRight() - tw - 14), (float) row.getCentreY() - 4.0f, 8.0f, 8.0f);

        g.setColour (theme::text); g.setFont (monoFont (38.0f));
        g.drawText (offsetText, offsetReadout.withHeight (48), juce::Justification::centred);
        g.setColour (juce::Colour (0xff8c95a5)); g.setFont (uiFont (12.0f));
        g.drawText ("bass delay offset", offsetReadout.withTrimmedTop (48), juce::Justification::centredTop);

        drawSliderCaption (g, offsetSlider, "Offset", {});
        auto ticks = offsetSlider.getBounds().translated (0, 22).withHeight (12).reduced (2, 0);
        g.setFont (monoFont (10.0f)); g.setColour (theme::faint);
        const auto lim = limits();
        g.drawText (signedMs (lim.first, 1), ticks, juce::Justification::centredLeft);
        g.drawText (signedMs (lim.second, 1), ticks, juce::Justification::centredRight);

        // shift limits caption
        g.setFont (uiFont (12.0f)); g.setColour (theme::muted);
        g.drawText ("Shift limits", limitsCaption, juce::Justification::centredLeft);
        g.setFont (monoFont (12.0f)); g.setColour (theme::text);
        g.drawText (signedMs (lim.first, 1).upToLastOccurrenceOf (" ms", false, false) + juce::String::fromUTF8 (" \xe2\x80\xa6 ")
                        + signedMs (lim.second, 1), limitsCaption, juce::Justification::centredRight);
    }

    // detection panel
    drawPanel (g, detectPanel);
    {
        const int x = detectPanel.getX() + 16, w = detectPanel.getWidth() - 32;
        g.setFont (titleFont()); g.setColour (theme::muted);
        g.drawText ("DETECTION", x, detectPanel.getY() + 14, w, 20, juce::Justification::centredLeft);
        g.setFont (uiFont (12.0f));
        g.drawText ("Mode", modeButtons[0]->getX(), modeButtons[0]->getY() - 20, w, 16, juce::Justification::centredLeft);
        auto segOutline = [&] (juce::OwnedArray<juce::TextButton>& arr)
        {
            auto r = arr.getFirst()->getBounds().getUnion (arr.getLast()->getBounds()).toFloat().expanded (0.5f);
            g.setColour (theme::line); g.drawRoundedRectangle (r, 6.0f, 1.0f);
        };
        segOutline (modeButtons);
    }
    {
        auto r = scaleButtons.getFirst()->getBounds().getUnion (scaleButtons.getLast()->getBounds()).toFloat().expanded (0.5f);
        g.setColour (theme::line); g.drawRoundedRectangle (r, 6.0f, 1.0f);
        drawSliderCaption (g, thresholdSlider, "Kick threshold", ap.getParameter (ids::threshold)->getCurrentValueAsText());

        const juce::String labels[] = { "Phase @ " + (proc.getFundamentalHz() > 0.0f ? juce::String ((int) std::round (proc.getFundamentalHz())) + " Hz" : juce::String ("fund.")),
                                        "Best offset", "Bass fundamental", "Sidechain" };
        const juce::String values[] = { phaseText, bestText, fundText, scText };
        const int cw = (readoutGrid.getWidth() - 8) / 2, chh = (readoutGrid.getHeight() - 8) / 2;
        for (int i = 0; i < 4; ++i)
        {
            auto cell = juce::Rectangle<int> (readoutGrid.getX() + (i % 2) * (cw + 8), readoutGrid.getY() + (i / 2) * (chh + 8), cw, chh);
            g.setColour (theme::well); g.fillRoundedRectangle (cell.toFloat(), 6.0f);
            auto in = cell.reduced (10, 0).withSizeKeepingCentre (cell.getWidth() - 20, 32);
            g.setColour (juce::Colour (0xff8c95a5)); g.setFont (uiFont (10.5f));
            g.drawText (labels[i], in.removeFromTop (13), juce::Justification::centredLeft);
            g.setColour (theme::text); g.setFont (monoFont (13.0f));
            g.drawText (values[i], in.withTrimmedTop (2), juce::Justification::centredLeft);
        }
    }
}

void PhaseLockEditor::resized()
{
    const int pad = 14, rightW = 286;
    auto content = getLocalBounds().withTrimmedTop (52).reduced (pad);
    auto right = content.removeFromRight (rightW);
    content.removeFromRight (pad);
    auto left = content;

    scopePanel = left.removeFromTop (382);
    left.removeFromTop (pad);
    slopePanel = left;

    alignPanel = right.removeFromTop (396);
    right.removeFromTop (pad);
    detectPanel = right;

    // scope
    scope.setBounds (scopePanel.getX() + 16, scopePanel.getY() + 42, scopePanel.getWidth() - 32, 300);
    {
        const int bw = 42, bh = 24;
        int x = scopePanel.getRight() - 16 - bw * scopeLenButtons.size();
        freezeButton.setBounds (x - 8 - 70, scopePanel.getY() + 10, 70, bh);
        for (auto* b : scopeLenButtons) { b->setBounds (x, scopePanel.getY() + 10, bw, bh); x += bw; }
    }
    meterArea = juce::Rectangle<int> (scopePanel.getX() + 16 + 104, scope.getBottom() + 12, scopePanel.getWidth() - 32 - 104 - 104, 16);

    // slope
    {
        auto in = slopePanel.reduced (16, 0).withTrimmedTop (42).withTrimmedBottom (14);
        auto sliders = in.removeFromRight (200);
        in.removeFromRight (18);
        customButton.setBounds (in.getRight() - 84, slopePanel.getY() + 9, 84, 26);
        const int cols = 8, gap = 6, rowH = 30;
        const int cw = (in.getWidth() - gap * (cols - 1)) / cols;
        for (int i = 0; i < curveButtons.size(); ++i)
            curveButtons[i]->setBounds (in.getX() + (i % cols) * (cw + gap), in.getY() + (i / cols) * (rowH + gap), cw, rowH);
        in.removeFromTop (2 * rowH + gap + 10);
        slopeEditor.setBounds (in);

        sliders.removeFromTop (16);
        for (auto* s : { &depthSlider, &attackSlider, &releaseSlider })
        {
            sliders.removeFromTop (16);
            s->setBounds (sliders.removeFromTop (24));
            sliders.removeFromTop (14);
        }
    }

    // align
    {
        auto in = alignPanel.reduced (16, 0).withTrimmedTop (44);
        offsetReadout = in.removeFromTop (62);
        in.removeFromTop (20);
        offsetSlider.setBounds (in.removeFromTop (24));
        in.removeFromTop (22);
        auto nudgeRow = in.removeFromTop (32);
        const int nw = (nudgeRow.getWidth() - 18) / 4;
        for (auto* b : nudgeButtons) { b->setBounds (nudgeRow.removeFromLeft (nw)); nudgeRow.removeFromLeft (6); }
        in.removeFromTop (12);
        lockButton.setBounds (in.removeFromTop (44));
        in.removeFromTop (16);
        limitsCaption = in.removeFromTop (16).reduced (2, 0);
        in.removeFromTop (6);
        shiftBar.setBounds (in.removeFromTop (46));
        in.removeFromTop (8);
        auto scaleRow = in.removeFromTop (26);
        const int sw = scaleRow.getWidth() / 4;
        for (auto* b : scaleButtons) b->setBounds (scaleRow.removeFromLeft (sw));
    }

    // detection
    {
        auto in = detectPanel.reduced (16, 0).withTrimmedTop (64);
        auto modeRow = in.removeFromTop (30);
        const int mw = modeRow.getWidth() / 2;
        for (auto* b : modeButtons) b->setBounds (modeRow.removeFromLeft (mw));
        in.removeFromTop (24);
        thresholdSlider.setBounds (in.removeFromTop (24));
        in.removeFromTop (10);
        readoutGrid = in.withTrimmedBottom (12);
    }
}
