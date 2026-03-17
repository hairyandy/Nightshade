#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GrindstonePedalFace.h"

// ─────────────────────────────────────────────────────────────────────────────
//  ClipModeSelector — four radio toggle buttons (LED / Silicon / Germanium / None)
// ─────────────────────────────────────────────────────────────────────────────
class ClipModeSelector : public juce::Component
{
public:
    std::function<void(int)> onModeChanged;   // called with 0..3

    ClipModeSelector()
    {
        static const char* labels[] = { "LED", "Si", "Ge", "None" };

        for (int i = 0; i < 4; ++i)
        {
            auto& btn = buttons[i];
            btn.setButtonText(labels[i]);
            btn.setRadioGroupId(1, juce::dontSendNotification);
            btn.setClickingTogglesState(true);
            btn.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff1e1e1e));
            btn.setColour(juce::TextButton::buttonOnColourId,  juce::Colour(0xff8B1A1A));
            btn.setColour(juce::TextButton::textColourOffId,   juce::Colour(0xff888888));
            btn.setColour(juce::TextButton::textColourOnId,    juce::Colours::white);
            addAndMakeVisible(btn);

            btn.onClick = [this, i]
            {
                if (buttons[i].getToggleState() && onModeChanged)
                    onModeChanged(i);
            };
        }

        buttons[0].setToggleState(true, juce::dontSendNotification);
    }

    void setSelectedMode(int mode)
    {
        jassert(mode >= 0 && mode < 4);
        buttons[mode].setToggleState(true, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const int btnW = area.getWidth() / 4;
        for (auto& btn : buttons)
            btn.setBounds(area.removeFromLeft(btnW));
    }

private:
    std::array<juce::TextButton, 4> buttons;
};

// ─────────────────────────────────────────────────────────────────────────────
//  NightshadeAudioProcessorEditor
// ─────────────────────────────────────────────────────────────────────────────
class NightshadeAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit NightshadeAudioProcessorEditor(NightshadeAudioProcessor&);
    ~NightshadeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    NightshadeAudioProcessor& processorRef;

    // pedalFace must be declared before clipSelector so that clipSelector
    // is destroyed first (safe: it removes itself from pedalFace's child list).
    GrindstonePedalFace pedalFace;
    ClipModeSelector    clipSelector;

    // Hidden combo box attached to the "clipmode" APVTS parameter.
    // The visible ClipModeSelector buttons drive this; host automation drives
    // the buttons back via onChange.
    juce::ComboBox hiddenClipCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> clipAttach;

    void syncClipButtonsFromCombo();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NightshadeAudioProcessorEditor)
};
