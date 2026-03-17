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

    // ── Clip selector sits between logotype and G-Washer ──────────────────
    pedalFace.setExtraComponent(&clipSelector);

    // ── Layout ────────────────────────────────────────────────────────────
    addAndMakeVisible(pedalFace);
    addChildComponent(hiddenClipCombo);   // invisible — layout not needed

    setSize(200, 380);
    setResizable(false, false);
}

NightshadeAudioProcessorEditor::~NightshadeAudioProcessorEditor() {}

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
}
