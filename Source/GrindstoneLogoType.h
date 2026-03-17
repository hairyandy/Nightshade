#pragma once
#include <JuceHeader.h>

class GrindstoneLogoType : public juce::Component
{
public:
    GrindstoneLogoType(const juce::File& imageFile, const juce::String& fallbackText = {});
    ~GrindstoneLogoType() override = default;

    void paint(juce::Graphics& g) override;

    // Replace the image (e.g. with a processed logo loaded externally).
    void setImage(const juce::Image& img) { logoImage = img; repaint(); }

private:
    juce::Image logoImage;
    juce::String fallbackText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrindstoneLogoType)
};
