#include "GrindstoneLogoType.h"

GrindstoneLogoType::GrindstoneLogoType(const juce::File& imageFile, const juce::String& fallbackText_)
    : fallbackText(fallbackText_)
{
    if (!imageFile.existsAsFile())
        return;

    logoImage = juce::ImageFileFormat::loadFrom(imageFile);
    if (!logoImage.isValid())
        return;

    logoImage = logoImage.convertedToFormat(juce::Image::ARGB);
    const int lw = logoImage.getWidth();
    const int lh = logoImage.getHeight();
    juce::Random rng(42);
    for (int iy = 0; iy < lh; ++iy)
    {
        const float rt = rng.nextFloat();
        const float la = (rt > 0.76f) ? 0.06f : (rt < 0.10f ? -0.06f : 0.0f);
        for (int ix = 0; ix < lw; ++ix)
        {
            auto c = logoImage.getPixelAt(ix, iy);
            float lm = (c.getRed()*0.299f + c.getGreen()*0.587f + c.getBlue()*0.114f) / 255.0f;
            float t  = juce::jlimit(0.0f, 1.0f, 1.0f - lm);
            float a  = t * t * (3.0f - 2.0f * t);
            const float gx     = (float)ix / (float)lw - 0.5f;
            const float gy     = (float)iy / (float)lh - 0.5f;
            const float bright = 0.72f - gx*0.12f - gy*0.15f + la;
            const auto v = (uint8_t)juce::jlimit(0, 255, (int)(210.0f * bright));
            logoImage.setPixelAt(ix, iy, juce::Colour(v, v, v).withAlpha((uint8_t)juce::roundToInt(a * 255.0f)));
        }
    }
}

void GrindstoneLogoType::paint(juce::Graphics& g)
{
    if (logoImage.isValid())
    {
        g.drawImage(logoImage, getLocalBounds().toFloat(), juce::RectanglePlacement::centred);
        return;
    }

    if (fallbackText.isEmpty())
        return;

    // Engraved silver text — dark shadow below-right, light highlight above-left, mid silver on top
    const auto bounds = getLocalBounds().toFloat();
    const float charW  = bounds.getWidth() / (float)juce::jmax(1, fallbackText.length());
    const float fontSize = juce::jmin(bounds.getHeight() * 0.70f, charW * 1.35f);
    juce::Font font(juce::Font::getDefaultMonospacedFontName(), fontSize, juce::Font::bold);
    g.setFont(font);

    // Shadow (engraved shadow below-right)
    g.setColour(juce::Colours::black.withAlpha(0.75f));
    g.drawText(fallbackText, bounds.translated(1.0f, 1.2f), juce::Justification::centred, false);

    // Specular highlight (above-left)
    g.setColour(juce::Colour(0xffA8A8A8).withAlpha(0.45f));
    g.drawText(fallbackText, bounds.translated(-0.6f, -0.6f), juce::Justification::centred, false);

    // Main brushed-aluminum text body
    g.setColour(juce::Colour(0xff727272));
    g.drawText(fallbackText, bounds, juce::Justification::centred, false);
}
