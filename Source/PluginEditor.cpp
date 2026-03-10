#include "PluginEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────
NightshadeAudioProcessorEditor::NightshadeAudioProcessorEditor (NightshadeAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    // ── APVTS slider attachments ──────────────────────────────────────────
    gainAttach = std::make_unique<SliderAttachment> (p.apvts, "gain", gainKnob.slider);
    toneAttach = std::make_unique<SliderAttachment> (p.apvts, "tone", toneKnob.slider);
    volAttach  = std::make_unique<SliderAttachment> (p.apvts, "vol",  volKnob.slider);

    // ── Clip mode: hidden combo attached to APVTS ─────────────────────────
    // We populate the combo with items that match the parameter choices.
    // (Indices 1..4 in a juce::ComboBox, so we offset by 1 vs the 0-based
    //  AudioParameterChoice convention.)
    hiddenClipCombo.addItem ("LED",       1);
    hiddenClipCombo.addItem ("Silicon",   2);
    hiddenClipCombo.addItem ("Germanium", 3);
    hiddenClipCombo.addItem ("None",      4);

    clipAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        p.apvts, "clipmode", hiddenClipCombo);

    // Sync the visible buttons from the combo's current value
    syncClipButtonsFromCombo();

    // When a button is clicked, push the index into the hidden combo
    clipSelector.onModeChanged = [this] (int mode)
    {
        hiddenClipCombo.setSelectedItemIndex (mode, juce::sendNotification);
    };

    // When the hidden combo changes (e.g. host automation), sync buttons
    hiddenClipCombo.onChange = [this]
    {
        syncClipButtonsFromCombo();
    };

    // ── Labels ────────────────────────────────────────────────────────────
    clipLabel.setText ("CLIPPING MODE", juce::dontSendNotification);
    clipLabel.setJustificationType (juce::Justification::centred);
    clipLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    clipLabel.setFont (juce::Font (11.0f, juce::Font::bold));

    // ── Add children ──────────────────────────────────────────────────────
    addAndMakeVisible (gainKnob);
    addAndMakeVisible (toneKnob);
    addAndMakeVisible (volKnob);
    addAndMakeVisible (clipSelector);
    addAndMakeVisible (clipLabel);
    addChildComponent (hiddenClipCombo);   // invisible — layout not needed

    // ── Window size ───────────────────────────────────────────────────────
    setSize (480, 280);
    setResizable (true, true);
    setResizeLimits (360, 220, 800, 500);
}

NightshadeAudioProcessorEditor::~NightshadeAudioProcessorEditor() {}

// ─────────────────────────────────────────────────────────────────────────────
//  Sync helper
// ─────────────────────────────────────────────────────────────────────────────
void NightshadeAudioProcessorEditor::syncClipButtonsFromCombo()
{
    // juce::ComboBox is 1-indexed; our mode enum is 0-indexed
    int modeIndex = hiddenClipCombo.getSelectedItemIndex();   // 0-based
    if (modeIndex >= 0)
        clipSelector.setSelectedMode (modeIndex);
}

// ─────────────────────────────────────────────────────────────────────────────
//  paint
// ─────────────────────────────────────────────────────────────────────────────
void NightshadeAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Background gradient: very dark charcoal
    g.fillAll (juce::Colour (0xff181818));

    // Subtle top-bar header area
    const int headerH = 36;
    g.setColour (juce::Colour (0xff0f0f0f));
    g.fillRect (0, 0, getWidth(), headerH);

    // Plugin title
    g.setColour (juce::Colour (0xffcc4400));
    g.setFont (juce::Font (22.0f, juce::Font::bold));
    g.drawText ("NIGHTSHADE", 0, 0, getWidth(), headerH, juce::Justification::centred);

    // Subtitle
    g.setColour (juce::Colour (0xff888888));
    g.setFont (juce::Font (10.0f));
    g.drawText ("overdrive / distortion", 0, headerH - 1, getWidth(), 16,
                juce::Justification::centred);

    // Thin separator line under header
    g.setColour (juce::Colour (0xff333333));
    g.drawHorizontalLine (headerH, 0.0f, static_cast<float> (getWidth()));
}

// ─────────────────────────────────────────────────────────────────────────────
//  resized
// ─────────────────────────────────────────────────────────────────────────────
void NightshadeAudioProcessorEditor::resized()
{
    const int headerH    = 36;
    const int padding    = 12;
    const int selectorH  = 30;
    const int labelH     = 18;
    const int bottomPad  = 14;

    auto area = getLocalBounds()
                    .withTrimmedTop (headerH + padding)
                    .withTrimmedBottom (bottomPad)
                    .withTrimmedLeft (padding)
                    .withTrimmedRight (padding);

    // ── Bottom strip: clip mode selector ──────────────────────────────────
    auto bottomStrip = area.removeFromBottom (selectorH + labelH + padding);
    bottomStrip.removeFromTop (padding / 2);
    clipLabel.setBounds    (bottomStrip.removeFromTop (labelH));
    clipSelector.setBounds (bottomStrip.removeFromTop (selectorH));

    // ── Remaining area: three knobs, equally spaced ───────────────────────
    const int knobW = area.getWidth() / 3;
    gainKnob.setBounds (area.removeFromLeft (knobW));
    toneKnob.setBounds (area.removeFromLeft (knobW));
    volKnob.setBounds  (area);
}
