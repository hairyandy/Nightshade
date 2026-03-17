#pragma once
#include <JuceHeader.h>

class GrindstoneKnob : public juce::Component
{
public:
    GrindstoneKnob(const juce::String& labelText, bool showSurround = true);
    ~GrindstoneKnob() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::Slider& getSlider() { return slider; }

private:
    class GrindstoneKnobLAF : public juce::LookAndFeel_V4
    {
    public:
        GrindstoneKnobLAF() = default;
        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPosProportional, float rotaryStartAngle,
                              float rotaryEndAngle, juce::Slider& slider) override;
    };

    juce::Slider slider;
    juce::String labelText;
    bool showSurround;
    GrindstoneKnobLAF laf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrindstoneKnob)
};
