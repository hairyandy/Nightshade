#include "GrindstoneKnob.h"

void GrindstoneKnob::GrindstoneKnobLAF::drawRotarySlider(juce::Graphics& g,
    int x, int y, int width, int height,
    float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider& /*slider*/)
{
    const float cx = (float)x + (float)width  * 0.5f;
    const float cy = (float)y + (float)height * 0.5f;
    const float radius = (float)juce::jmin(width, height) * 0.5f - 2.0f;
    const float toAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const float lipW  = radius * 0.12f;
    const float dishR = radius - lipW;
    const float dx = std::sin(toAngle);
    const float dy = -std::cos(toAngle);

    // 1. Recessed inner dish
    {
        juce::ColourGradient grad(juce::Colour(0xffA4A4A4),
                                  cx - dishR*0.28f, cy - dishR*0.38f,
                                  juce::Colour(0xff606060),
                                  cx + dishR*0.22f, cy + dishR*0.32f, true);
        grad.addColour(0.45, juce::Colour(0xff888888));
        g.setGradientFill(grad);
        g.fillEllipse(cx - dishR, cy - dishR, dishR*2.0f, dishR*2.0f);
    }
    // 2. Brush-finish lines clipped to dish
    {
        g.saveState();
        juce::Path clip;
        clip.addEllipse(cx - dishR, cy - dishR, dishR*2.0f, dishR*2.0f);
        g.reduceClipRegion(clip);
        juce::Random rng(99);
        for (float ly = cy - dishR; ly <= cy + dishR; ly += 1.0f) {
            const float t = rng.nextFloat();
            if (t > 0.77f) { g.setColour(juce::Colours::white.withAlpha(0.08f)); g.drawHorizontalLine((int)ly, cx-dishR, cx+dishR); }
            else if (t < 0.09f) { g.setColour(juce::Colours::black.withAlpha(0.07f)); g.drawHorizontalLine((int)ly, cx-dishR, cx+dishR); }
        }
        g.restoreState();
    }
    // 3. Shadow at dish edge
    g.setColour(juce::Colour(0xff222222).withAlpha(0.80f));
    g.drawEllipse(cx-dishR, cy-dishR, dishR*2.0f, dishR*2.0f, 2.5f);
    // 4. Raised outer lip
    {
        juce::Path lip;
        lip.addEllipse(cx-radius, cy-radius, radius*2.0f, radius*2.0f);
        lip.addEllipse(cx-dishR,  cy-dishR,  dishR*2.0f,  dishR*2.0f);
        lip.setUsingNonZeroWinding(false);
        juce::ColourGradient grad(juce::Colour(0xffDEDEDE),
                                  cx - radius*0.32f, cy - radius*0.42f,
                                  juce::Colour(0xff8A8A8A),
                                  cx + radius*0.28f, cy + radius*0.38f, false);
        grad.addColour(0.5, juce::Colour(0xffBCBCBC));
        g.setGradientFill(grad);
        g.fillPath(lip);
    }
    // 5. Outer rim edge
    g.setColour(juce::Colour(0xffAAAAAA));
    g.drawEllipse(cx-radius+0.5f, cy-radius+0.5f, (radius-0.5f)*2.0f, (radius-0.5f)*2.0f, 1.0f);
    // 6. Indicator line
    g.setColour(juce::Colours::black.withAlpha(0.92f));
    g.drawLine(cx + dx*(radius*0.12f), cy + dy*(radius*0.12f),
               cx + dx*(radius-1.5f),  cy + dy*(radius-1.5f), 3.75f);
}

GrindstoneKnob::GrindstoneKnob(const juce::String& labelText_, bool showSurround_)
    : labelText(labelText_), showSurround(showSurround_)
{
    slider.setLookAndFeel(&laf);
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible(slider);
}

GrindstoneKnob::~GrindstoneKnob()
{
    slider.setLookAndFeel(nullptr);
}

void GrindstoneKnob::paint(juce::Graphics& g)
{
    if (showSurround)
    {
        const float scale    = (float)getWidth() / 156.0f;
        const float cx       = (float)getWidth() * 0.5f;
        const float innerR   = 61.0f * scale;
        const float outerR   = 78.0f * scale;
        const float cy       = outerR;
        const juce::Colour silver{0xffC8C8C8};
        const float startAngle = juce::MathConstants<float>::pi * 1.25f;
        const float endAngle   = juce::MathConstants<float>::pi * 2.75f;

        g.setColour(silver);
        for (int i = 0; i < 11; ++i)
        {
            const float frac  = (float)i / 10.0f;
            const float angle = startAngle + frac * (endAngle - startAngle);
            const float dx    = std::sin(angle);
            const float dy    = -std::cos(angle);
            g.drawLine(cx + dx*innerR, cy + dy*innerR,
                       cx + dx*outerR, cy + dy*outerR,
                       6.0f * scale);
        }
        {
            juce::Path arc;
            for (int i = 0; i <= 120; ++i)
            {
                const float t     = (float)i / 120.0f;
                const float angle = startAngle + t * (endAngle - startAngle);
                const float px    = cx + std::sin(angle) * innerR;
                const float py    = cy - std::cos(angle) * innerR;
                if (i == 0) arc.startNewSubPath(px, py); else arc.lineTo(px, py);
            }
            g.strokePath(arc, juce::PathStrokeType(2.5f * scale));
        }

        // Label text — engraved silver style
        if (labelText.isNotEmpty())
        {
            const float fontSize = 13.0f * scale;
            g.setFont(juce::Font(fontSize, juce::Font::bold));
            const float labelY = outerR * 2.0f + 2.0f * scale;
            const float labelH = fontSize + 4.0f * scale;

            // Dark shadow
            g.setColour(juce::Colour(0xff222222).withAlpha(0.85f));
            g.drawText(labelText,
                       juce::Rectangle<float>(1.0f, labelY + 1.0f, (float)getWidth() - 2.0f, labelH),
                       juce::Justification::centred, false);
            // Lighter text offset 1px up-left
            g.setColour(juce::Colour(0xffC8C8C8));
            g.drawText(labelText,
                       juce::Rectangle<float>(0.0f, labelY, (float)getWidth() - 2.0f, labelH),
                       juce::Justification::centred, false);
        }
    }
    else
    {
        // No surround — label below the knob
        if (labelText.isNotEmpty())
        {
            const float scale    = (float)juce::jmin(getWidth(), getHeight()) / 156.0f;
            const float fontSize = 13.0f * scale;
            g.setFont(juce::Font(fontSize, juce::Font::bold));
            const float knobSize = (float)juce::jmin(getWidth(), getHeight()) * 0.85f;
            const float knobY    = ((float)getHeight() - knobSize) * 0.5f;
            const float labelY   = knobY + knobSize + 2.0f * scale;
            const float labelH   = fontSize + 4.0f * scale;

            g.setColour(juce::Colour(0xff222222).withAlpha(0.85f));
            g.drawText(labelText,
                       juce::Rectangle<float>(1.0f, labelY + 1.0f, (float)getWidth() - 2.0f, labelH),
                       juce::Justification::centred, false);
            g.setColour(juce::Colour(0xffC8C8C8));
            g.drawText(labelText,
                       juce::Rectangle<float>(0.0f, labelY, (float)getWidth() - 2.0f, labelH),
                       juce::Justification::centred, false);
        }
    }
}

void GrindstoneKnob::resized()
{
    if (showSurround)
    {
        const float scale  = (float)getWidth() / 156.0f;
        const float innerR = 61.0f * scale;
        const float outerR = 78.0f * scale;
        const float cy     = outerR;
        const float cx     = (float)getWidth() * 0.5f;
        const float knobDiam = (innerR - scale) * 2.0f * 1.20f;
        slider.setBounds(juce::roundToInt(cx - knobDiam * 0.5f),
                         juce::roundToInt(cy - knobDiam * 0.5f),
                         juce::roundToInt(knobDiam),
                         juce::roundToInt(knobDiam));
    }
    else
    {
        const int knobSize = juce::roundToInt((float)juce::jmin(getWidth(), getHeight()) * 0.85f);
        slider.setBounds((getWidth()  - knobSize) / 2,
                         (getHeight() - knobSize) / 2,
                         knobSize, knobSize);
    }
}
