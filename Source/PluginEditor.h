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
        // Labels: LED, Si, Ge, X — fits in narrow 20 px buttons
        static const char* labels[] = { "LED", "Si", "Ge", "X" };

        for (int i = 0; i < 4; ++i)
        {
            auto& btn = buttons[i];
            btn.setButtonText(labels[i]);
            btn.setRadioGroupId(1, juce::dontSendNotification);
            btn.setClickingTogglesState(true);
            btn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff1e1e1e));
            btn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff8B1A1A));
            btn.setColour(juce::TextButton::textColourOffId,  juce::Colour(0xff888888));
            btn.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
            btn.setLookAndFeel(&smallButtonLAF);
            addAndMakeVisible(btn);

            btn.onClick = [this, i]
            {
                if (buttons[i].getToggleState() && onModeChanged)
                    onModeChanged(i);
            };
        }

        buttons[0].setToggleState(true, juce::dontSendNotification);
    }

    ~ClipModeSelector()
    {
        for (auto& btn : buttons)
            btn.setLookAndFeel(nullptr);
    }

    void setSelectedMode(int mode)
    {
        jassert(mode >= 0 && mode < 4);
        buttons[mode].setToggleState(true, juce::dontSendNotification);
    }

    void resized() override
    {
        // Buttons are 50 % of their previous size: 20 px wide × 17 px tall.
        // Two vertical columns flanking the TONE knob, centred in the band:
        //   Left  (LED, Si) — outer edge left-aligned with VOL  outer edge  x=24
        //   Right (Ge,  X)  — outer edge right-aligned with GAIN outer edge x=176
        const int btnW   = 20;
        const int btnH   = 17;
        const int startY = (getHeight() - btnH * 2) / 2;   // centre vertically

        buttons[0].setBounds(24,  startY,           btnW, btnH);   // LED
        buttons[1].setBounds(24,  startY + btnH,    btnW, btnH);   // Si
        buttons[2].setBounds(156, startY,           btnW, btnH);   // Ge
        buttons[3].setBounds(156, startY + btnH,    btnW, btnH);   // X
    }

    // Only capture mouse events in the button columns; let the centre (TONE
    // knob area, x 44..156) pass through to the component below.
    bool hitTest(int x, int /*y*/) override
    {
        return (x >= 24 && x < 44) || (x >= 156 && x < 176);
    }

private:
    // Minimal look-and-feel: flat rect, silver border, small bold font.
    struct SmallButtonLAF : public juce::LookAndFeel_V4
    {
        void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                                  const juce::Colour&, bool, bool) override
        {
            auto bounds = b.getLocalBounds().toFloat().reduced(0.3f);
            g.setColour(b.getToggleState() ? juce::Colour(0xff8B1A1A)
                                           : juce::Colour(0xff1e1e1e));
            g.fillRect(bounds);
            g.setColour(juce::Colour(0xff888888));   // silver outline
            g.drawRect(bounds, 0.5f);
        }

        void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            g.setFont(juce::Font(7.5f, juce::Font::bold));
            g.setColour(b.findColour(b.getToggleState()
                                         ? juce::TextButton::textColourOnId
                                         : juce::TextButton::textColourOffId));
            g.drawText(b.getButtonText(), b.getLocalBounds().reduced(1),
                       juce::Justification::centred, false);
        }
    };

    SmallButtonLAF                smallButtonLAF;   // must outlive buttons
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
    void loadNightshadeLogo();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NightshadeAudioProcessorEditor)
};
