#pragma once
#include <JuceHeader.h>

class GWasherButton : public juce::Component
{
public:
    GWasherButton(const juce::File& svgFile, float buttonSizeProportion = 0.26f);
    GWasherButton(const char* svgXml, size_t svgXmlSize, float buttonSizeProportion = 0.26f);
    ~GWasherButton() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setBypassState(bool active);
    bool getBypassState() const;

    std::function<void(bool)> onBypassChanged;

private:
    class GrindstoneFootswitchLAF : public juce::LookAndFeel_V4
    {
    public:
        GrindstoneFootswitchLAF() = default;
        void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                  const juce::Colour& backgroundColour,
                                  bool isMouseOverButton, bool isButtonDown) override;
    };

    void buildCache();

    juce::File svgFile;
    juce::String svgXmlData;
    float buttonSizeProportion;
    int cachedHeight = 0;

    juce::Image cachedGearImage;
    juce::TextButton bypassButton;
    GrindstoneFootswitchLAF laf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GWasherButton)
};
