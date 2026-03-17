#include "GrindstonePedalFace.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Shared initialisation
//  Knobs are created with empty label strings so the built-in label rendering
//  is suppressed; labels are drawn by this component's own paint() so they can
//  be positioned freely (below VOL/GAIN, above TONE).
// ─────────────────────────────────────────────────────────────────────────────
void GrindstonePedalFace::commonInit(const juce::StringArray& knobLabels, int count)
{
    storedKnobLabels = knobLabels;

    for (int i = 0; i < count; ++i)
    {
        // Pass empty string → suppress built-in label; we paint them ourselves.
        knobs.add(new GrindstoneKnob(juce::String(), showSurrounds));
        addAndMakeVisible(knobs.getLast());
    }

    addAndMakeVisible(logotype);
    addAndMakeVisible(*washer);

    washer->onBypassChanged = [this](bool active)
    {
        if (onBypassChanged)
            onBypassChanged(active);
        repaint();
    };
}

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor — file paths
// ─────────────────────────────────────────────────────────────────────────────
GrindstonePedalFace::GrindstonePedalFace(
    juce::Colour backgroundColor_,
    int numKnobs,
    const juce::StringArray& knobLabels,
    const juce::File& gWasherSvgFile,
    const juce::File& logotypeImageFile,
    bool showSurrounds_)
    : backgroundColor(backgroundColor_),
      showSurrounds(showSurrounds_),
      logotype(logotypeImageFile),
      washer(std::make_unique<GWasherButton>(gWasherSvgFile))
{
    commonInit(knobLabels, juce::jlimit(1, 3, numKnobs));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor — embedded SVG + fallback logo text
// ─────────────────────────────────────────────────────────────────────────────
GrindstonePedalFace::GrindstonePedalFace(
    juce::Colour backgroundColor_,
    int numKnobs,
    const juce::StringArray& knobLabels,
    const char* washerSvgXml, size_t washerSvgXmlSize,
    const juce::String& fallbackLogoText,
    bool showSurrounds_)
    : backgroundColor(backgroundColor_),
      showSurrounds(showSurrounds_),
      logotype(juce::File(), fallbackLogoText),
      washer(std::make_unique<GWasherButton>(washerSvgXml, washerSvgXmlSize))
{
    commonInit(knobLabels, juce::jlimit(1, 3, numKnobs));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Extra component
// ─────────────────────────────────────────────────────────────────────────────
void GrindstonePedalFace::setExtraComponent(juce::Component* component)
{
    extraComponent = component;
    if (extraComponent != nullptr)
        addAndMakeVisible(extraComponent);
    resized();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Attachment
// ─────────────────────────────────────────────────────────────────────────────
void GrindstonePedalFace::addKnobAttachment(int knobIndex,
                                             juce::AudioProcessorValueTreeState& apvts,
                                             const juce::String& parameterID)
{
    if (knobIndex < 0 || knobIndex >= knobs.size())
        return;

    knobAttachments.add(new juce::AudioProcessorValueTreeState::SliderAttachment(
        apvts, parameterID, knobs[knobIndex]->getSlider()));
}

// ─────────────────────────────────────────────────────────────────────────────
//  paint
// ─────────────────────────────────────────────────────────────────────────────
void GrindstonePedalFace::paint(juce::Graphics& g)
{
    // ── Background ───────────────────────────────────────────────────────────
    g.setColour(backgroundColor);
    g.fillRect(getLocalBounds().toFloat());
    {
        juce::Random rng(17);
        for (float ly = 0.0f; ly < (float)getHeight(); ly += 1.0f)
        {
            const float t = rng.nextFloat();
            if (t > 0.76f)
            {
                g.setColour(juce::Colours::white.withAlpha(0.032f));
                g.drawHorizontalLine((int)ly, 0.0f, (float)getWidth());
            }
            else if (t < 0.10f)
            {
                g.setColour(juce::Colours::black.withAlpha(0.038f));
                g.drawHorizontalLine((int)ly, 0.0f, (float)getWidth());
            }
        }
    }
    {
        const float cx = (float)getWidth()  * 0.5f;
        const float cy = (float)getHeight() * 0.5f;
        juce::ColourGradient vignette(juce::Colours::white.withAlpha(0.07f), cx, cy,
                                      juce::Colours::transparentBlack, 0.0f, cy, true);
        g.setGradientFill(vignette);
        g.fillRect(getLocalBounds().toFloat());
    }

    // ── LED ──────────────────────────────────────────────────────────────────
    {
        const bool  active = washer->getBypassState();
        const float w      = (float)getWidth();
        const float ledCx  = w * 0.5f;
        const float ledCy  = 16.0f;
        const float ledR   = 5.0f;

        if (active)
        {
            g.setColour(juce::Colour(0xffFF4040).withAlpha(0.22f));
            g.fillEllipse(ledCx - ledR*2.4f, ledCy - ledR*2.4f, ledR*4.8f, ledR*4.8f);
        }

        juce::ColourGradient dome(
            active ? juce::Colour(0xffFF4040).brighter(0.55f) : juce::Colour(0xff882020),
            ledCx - ledR*0.3f, ledCy - ledR*0.4f,
            active ? juce::Colour(0xffFF4040).darker(0.4f)    : juce::Colour(0xff3A0808),
            ledCx + ledR*0.2f, ledCy + ledR*0.3f, true);
        g.setGradientFill(dome);
        g.fillEllipse(ledCx - ledR, ledCy - ledR, ledR*2.0f, ledR*2.0f);

        g.setColour(juce::Colour(0xff2A2A2A));
        g.drawEllipse(ledCx - ledR, ledCy - ledR, ledR*2.0f, ledR*2.0f, 0.8f);
    }

    // ── Knob labels ──────────────────────────────────────────────────────────
    // Drawn here (behind knob components) so they appear in the gap between
    // the top-row knobs and the TONE knob.
    // • knobs[0] VOL  — label below, centered on the knob
    // • knobs[1] GAIN — label below, centered on the knob
    // • knobs[2] TONE — label above, centered on the knob
    if (knobs.size() >= 1)
    {
        const float fontSize = 9.5f;
        g.setFont(juce::Font(fontSize, juce::Font::bold));

        const int labelCount = juce::jmin(knobs.size(), storedKnobLabels.size());

        for (int i = 0; i < labelCount; ++i)
        {
            const juce::String& lbl = storedKnobLabels[i];
            if (lbl.isEmpty()) continue;

            const auto kb = knobs[i]->getBounds().toFloat();
            const float lh = fontSize + 3.0f;
            float ly;

            // TONE (index 2 in 3-knob layout) gets its label above
            if (knobs.size() == 3 && i == 2)
                ly = kb.getY() - lh - 1.0f;
            else
                ly = kb.getBottom() + 1.0f;

            const juce::Rectangle<float> shadow { kb.getX() + 0.5f, ly + 1.0f, kb.getWidth(), lh };
            const juce::Rectangle<float> main   { kb.getX(),          ly,         kb.getWidth(), lh };

            g.setColour(juce::Colour(0xff111111).withAlpha(0.90f));
            g.drawText(lbl, shadow, juce::Justification::centred, false);

            g.setColour(juce::Colour(0xffC8C8C8));
            g.drawText(lbl, main, juce::Justification::centred, false);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  resized
//
//  Fixed values derived from Reveal to ensure exact visual match:
//    • Washer height: 185 px  (Reveal: displayH = 185.0f)
//    • Washer width:  full panel width (gear ring clips symmetrically on both sides)
//    • Washer position: flush at panel bottom  → center at h−92 ≈ 287 for h=380
//
//  3-knob layout — INVERTED TRIANGLE:
//    knobs[0] VOL  top-left   ┐
//    knobs[1] GAIN top-right  ┘  same row, same size (35% of width)
//    knobs[2] TONE bottom-center — SAME SIZE as top row (fixes Issue 1)
//
//  Between the two knob rows: a 9 px label strip shared by:
//    • VOL/GAIN labels (below their knobs)
//    • TONE label (above its knob)
//  These are drawn in paint() at different horizontal positions — no overlap.
// ─────────────────────────────────────────────────────────────────────────────
void GrindstonePedalFace::resized()
{
    const int w = getWidth();
    const int h = getHeight();
    const int count = knobs.size();

    // ── G-Washer (fixed, Reveal-matched) ─────────────────────────────────────
    const int washerH = 185;
    washer->setBounds(0, h - washerH, w, washerH);

    // Available vertical space above the washer
    const int washerTop = h - washerH;

    // ── Logo + extra-component strip (stacked upward from washerTop) ──────────
    const int logoH  = 8;
    const int logoW  = juce::roundToInt((float)w * 0.80f);
    const int logoX  = (w - logoW) / 2;

    // Clip selector (extra component) fills the gap between logo and washer
    int logoY = washerTop - logoH;  // default: logo flush above washer

    if (extraComponent != nullptr)
    {
        // Leave 1 px gap at bottom; clip fills remaining space above logo
        const int clipH = 14;
        const int clipY = washerTop - clipH - 1;
        extraComponent->setBounds(logoX, clipY, logoW, clipH);
        logoY = clipY - logoH - 1;
    }

    logotype.setBounds(logoX, logoY, logoW, logoH);

    // ── Knobs ─────────────────────────────────────────────────────────────────
    if (count == 1)
    {
        const int knobSize = juce::roundToInt((float)w * 0.55f);
        const int knobX    = (w - knobSize) / 2;
        const int knobY    = 24;
        knobs[0]->setBounds(knobX, knobY, knobSize, knobSize);
    }
    else if (count == 2)
    {
        const int knobSize = juce::roundToInt((float)w * 0.35f);
        const int spacing  = juce::roundToInt((float)w * 0.08f);
        const int totalW   = knobSize * 2 + spacing;
        const int startX   = (w - totalW) / 2;
        const int knobY    = 24;
        knobs[0]->setBounds(startX,                       knobY, knobSize, knobSize);
        knobs[1]->setBounds(startX + knobSize + spacing,  knobY, knobSize, knobSize);
    }
    else // 3 knobs — inverted triangle
    {
        // Top row: knobs[0] VOL (left) + knobs[1] GAIN (right)
        const int topKnobSize = juce::roundToInt((float)w * 0.35f);   // 70 px at w=200
        const int spacing     = juce::roundToInt((float)w * 0.06f);   // 12 px at w=200
        const int totalTopW   = topKnobSize * 2 + spacing;
        const int topStartX   = (w - totalTopW) / 2;
        const int topKnobY    = 22;
        knobs[0]->setBounds(topStartX,                          topKnobY, topKnobSize, topKnobSize);
        knobs[1]->setBounds(topStartX + topKnobSize + spacing,  topKnobY, topKnobSize, topKnobSize);

        // Label strip (9 px) between the two rows — labels drawn in paint()
        const int labelStripH = 9;

        // Bottom center: knobs[2] TONE — SAME SIZE as top row (Issue 1 fix)
        const int botKnobSize = topKnobSize;
        const int botKnobX    = (w - botKnobSize) / 2;
        const int botKnobY    = topKnobY + topKnobSize + labelStripH;  // 22+70+9 = 101
        knobs[2]->setBounds(botKnobX, botKnobY, botKnobSize, botKnobSize);
    }
}
