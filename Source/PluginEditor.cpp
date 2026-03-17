#include "PluginEditor.h"
#include <BinaryData.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────
NightshadeAudioProcessorEditor::NightshadeAudioProcessorEditor(NightshadeAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processorRef(p),
      pedalFace(juce::Colour(28, 28, 28),           // near-black enclosure
                3,                                   // three knobs
                juce::StringArray{"VOL", "GAIN", "TONE"},
                BinaryData::GWasher_jpg_svg,          // embedded SVG
                (size_t)BinaryData::GWasher_jpg_svgSize,
                "NIGHTSHADE",                        // logotype fallback text
                true)                                // show surrounds
{
    // ── Knob → APVTS wiring ───────────────────────────────────────────────
    // knobs[0] = VOL (top-left), knobs[1] = GAIN (top-right), knobs[2] = TONE (bottom-center)
    pedalFace.addKnobAttachment(0, p.apvts, "vol");
    pedalFace.addKnobAttachment(1, p.apvts, "gain");
    pedalFace.addKnobAttachment(2, p.apvts, "tone");

    // ── Clip mode selector → APVTS ────────────────────────────────────────
    hiddenClipCombo.addItem("LED",       1);
    hiddenClipCombo.addItem("Silicon",   2);
    hiddenClipCombo.addItem("Germanium", 3);
    hiddenClipCombo.addItem("None",      4);

    clipAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        p.apvts, "clipmode", hiddenClipCombo);

    syncClipButtonsFromCombo();

    clipSelector.onModeChanged = [this](int mode)
    {
        hiddenClipCombo.setSelectedItemIndex(mode, juce::sendNotification);
    };

    hiddenClipCombo.onChange = [this]
    {
        syncClipButtonsFromCombo();
    };

    // ── Bypass LED  (no APVTS bypass param — cosmetic only for now) ───────
    pedalFace.onBypassChanged = [](bool /*active*/) {};

    // ── Layout ────────────────────────────────────────────────────────────
    addAndMakeVisible(pedalFace);
    // clipSelector renders on top of pedalFace, flanking the TONE knob.
    addAndMakeVisible(clipSelector);
    addChildComponent(hiddenClipCombo);   // invisible — layout not needed

    loadNightshadeLogo();

    setSize(200, 380);
    setResizable(false, false);
}

NightshadeAudioProcessorEditor::~NightshadeAudioProcessorEditor() {}

// ─────────────────────────────────────────────────────────────────────────────
//  NIGHTSHADE logotype — extracted from hardware photo
//
//  The hardware photo (800×800 JPG) shows silver text on a near-black enclosure.
//  We crop the text region, then apply luminance directly as alpha (bright silver
//  pixels → opaque; dark background → transparent).  Same brushed-aluminum
//  colouring as the Reveal logotype but with t=lm instead of t=1-lm.
// ─────────────────────────────────────────────────────────────────────────────
void NightshadeAudioProcessorEditor::loadNightshadeLogo()
{
    // ── Load hardware photo ───────────────────────────────────────────────────
    // Tightly cropped screenshot (748×1493): pedal fills the full frame.
    juce::File photoFile("/Users/andy/Downloads/Nightshade_Screenshot4.png");
    if (!photoFile.existsAsFile())
        return;

    auto fullPhoto = juce::ImageFileFormat::loadFrom(photoFile);
    if (!fullPhoto.isValid())
        return;

    // ── Crop to the NIGHTSHADE text region ───────────────────────────────────
    // Text sits between the TONE knob bottom (~43 %) and the G-washer (~60 %)
    // in the 1493 px tall image: approximately y=670, h=160.
    const juce::Rectangle<int> cropRect =
        juce::Rectangle<int>(5, 565, 720, 280)
            .getIntersection(fullPhoto.getBounds());
    if (cropRect.isEmpty())
        return;

    auto cropped = fullPhoto.getClippedImage(cropRect)
                            .convertedToFormat(juce::Image::ARGB);

    // ── Colour-key transparency ───────────────────────────────────────────────
    // The enclosure is near-black; the NIGHTSHADE lettering is silver.
    // Remove any pixel whose R, G, and B are all below the threshold so that
    // only the bright silver text remains.  No recolouring — keep photo colours.
    {
        juce::Image::BitmapData bm(cropped, juce::Image::BitmapData::readWrite);
        for (int iy = 0; iy < cropped.getHeight(); ++iy)
        {
            for (int ix = 0; ix < cropped.getWidth(); ++ix)
            {
                const auto c = cropped.getPixelAt(ix, iy);
                if (c.getRed() < 60 && c.getGreen() < 60 && c.getBlue() < 60)
                    cropped.setPixelAt(ix, iy, juce::Colours::transparentBlack);
            }
        }
    }

    pedalFace.setLogoImage(cropped);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Sync helper
// ─────────────────────────────────────────────────────────────────────────────
void NightshadeAudioProcessorEditor::syncClipButtonsFromCombo()
{
    const int modeIndex = hiddenClipCombo.getSelectedItemIndex();
    if (modeIndex >= 0)
        clipSelector.setSelectedMode(modeIndex);
}

// ─────────────────────────────────────────────────────────────────────────────
//  paint  (pedalFace paints itself; editor background is just black)
// ─────────────────────────────────────────────────────────────────────────────
void NightshadeAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

// ─────────────────────────────────────────────────────────────────────────────
//  resized
// ─────────────────────────────────────────────────────────────────────────────
void NightshadeAudioProcessorEditor::resized()
{
    pedalFace.setBounds(getLocalBounds());

    // Clip selector: occupies the band that flanks the TONE knob.
    // topKnobY=22, topKnobSize=70, labelStripH=9 → TONE starts at y=101, height=70.
    clipSelector.setBounds(0, 91, getWidth(), 70);
}
