#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// ─────────────────────────────────────────────────────────────────────────────
//  ClipModeSelector
//
//  Four labeled toggle buttons laid out horizontally.  Exactly one is active
//  at a time (radio-group behavior).  Reports changes via a std::function
//  callback to avoid coupling to the parent editor.
// ─────────────────────────────────────────────────────────────────────────────
class ClipModeSelector : public juce::Component
{
public:
    std::function<void (int)> onModeChanged;   // called with 0..3

    ClipModeSelector()
    {
        static const char* labels[] = { "LED", "Silicon", "Germanium", "None" };

        for (int i = 0; i < 4; ++i)
        {
            auto& btn = buttons[i];
            btn.setButtonText (labels[i]);
            btn.setRadioGroupId (1, juce::dontSendNotification);
            btn.setClickingTogglesState (true);

            btn.setColour (juce::TextButton::buttonColourId,
                           juce::Colour (0xff2a2a2a));
            btn.setColour (juce::TextButton::buttonOnColourId,
                           juce::Colour (0xffcc4400));  // warm orange-red when active
            btn.setColour (juce::TextButton::textColourOffId,
                           juce::Colours::lightgrey);
            btn.setColour (juce::TextButton::textColourOnId,
                           juce::Colours::white);

            addAndMakeVisible (btn);

            btn.onClick = [this, i]
            {
                if (buttons[i].getToggleState() && onModeChanged)
                    onModeChanged (i);
            };
        }

        buttons[0].setToggleState (true, juce::dontSendNotification);
    }

    void setSelectedMode (int mode)
    {
        jassert (mode >= 0 && mode < 4);
        buttons[mode].setToggleState (true, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area  = getLocalBounds();
        int  btnW  = area.getWidth() / 4;
        for (auto& btn : buttons)
            btn.setBounds (area.removeFromLeft (btnW));
    }

private:
    std::array<juce::TextButton, 4> buttons;
};

// ─────────────────────────────────────────────────────────────────────────────
//  LabeledKnob
//
//  A rotary slider with a label centred below it.
// ─────────────────────────────────────────────────────────────────────────────
struct LabeledKnob : public juce::Component
{
    juce::Slider      slider;
    juce::Label       label;

    explicit LabeledKnob (const juce::String& labelText)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
        slider.setRange (0.0, 1.0, 0.001);

        // Dark rotary look
        slider.setColour (juce::Slider::rotarySliderFillColourId,
                          juce::Colour (0xffcc4400));
        slider.setColour (juce::Slider::rotarySliderOutlineColourId,
                          juce::Colour (0xff555555));
        slider.setColour (juce::Slider::thumbColourId,
                          juce::Colours::white);
        slider.setColour (juce::Slider::textBoxTextColourId,
                          juce::Colours::lightgrey);
        slider.setColour (juce::Slider::textBoxBackgroundColourId,
                          juce::Colour (0xff1a1a1a));
        slider.setColour (juce::Slider::textBoxOutlineColourId,
                          juce::Colours::transparentBlack);

        label.setText (labelText, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        label.setFont (juce::Font (13.0f, juce::Font::bold));

        addAndMakeVisible (slider);
        addAndMakeVisible (label);
    }

    void resized() override
    {
        auto area   = getLocalBounds();
        int  lblH   = 20;
        label.setBounds  (area.removeFromBottom (lblH));
        slider.setBounds (area);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  NightshadeAudioProcessorEditor
// ─────────────────────────────────────────────────────────────────────────────
class NightshadeAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit NightshadeAudioProcessorEditor (NightshadeAudioProcessor&);
    ~NightshadeAudioProcessorEditor() override;

    void paint  (juce::Graphics&) override;
    void resized() override;

private:
    NightshadeAudioProcessor& processorRef;

    // Knobs
    LabeledKnob gainKnob  { "GAIN" };
    LabeledKnob toneKnob  { "TONE" };
    LabeledKnob volKnob   { "VOL"  };

    // Clip mode selector
    ClipModeSelector clipSelector;
    juce::Label      clipLabel;

    // APVTS attachments (keep alive for the lifetime of the editor)
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> gainAttach;
    std::unique_ptr<SliderAttachment> toneAttach;
    std::unique_ptr<SliderAttachment> volAttach;

    // ComboBox attachment for the clip mode —  we use a helper class since
    // ClipModeSelector is custom (not a juce::ComboBox).
    // We attach via an AudioProcessorValueTreeState::ComboBoxAttachment to a
    // hidden combo, keeping the custom buttons in sync manually.
    juce::ComboBox hiddenClipCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> clipAttach;

    void syncClipButtonsFromCombo();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NightshadeAudioProcessorEditor)
};
