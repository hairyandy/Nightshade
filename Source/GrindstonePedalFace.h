#pragma once
#include <JuceHeader.h>
#include "GWasherButton.h"
#include "GrindstoneKnob.h"
#include "GrindstoneLogoType.h"

class GrindstonePedalFace : public juce::Component
{
public:
    // Constructor using file paths (original API)
    GrindstonePedalFace(
        juce::Colour backgroundColor,
        int numKnobs,
        const juce::StringArray& knobLabels,
        const juce::File& gWasherSvgFile,
        const juce::File& logotypeImageFile,
        bool showSurrounds = true
    );

    // Constructor using embedded SVG binary data and optional fallback logo text
    GrindstonePedalFace(
        juce::Colour backgroundColor,
        int numKnobs,
        const juce::StringArray& knobLabels,
        const char* washerSvgXml, size_t washerSvgXmlSize,
        const juce::String& fallbackLogoText,
        bool showSurrounds = true
    );

    ~GrindstonePedalFace() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void addKnobAttachment(int knobIndex,
                           juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& parameterID);

    // Optional extra component placed between logotype and washer.
    // Caller retains ownership; GrindstonePedalFace only lays it out.
    void setExtraComponent(juce::Component* component);
    void setLogoImage(const juce::Image& img) { logotype.setImage(img); }

    std::function<void(bool)> onBypassChanged;

private:
    void commonInit(const juce::StringArray& knobLabels, int count);

    juce::Colour backgroundColor;
    bool showSurrounds;

    juce::OwnedArray<GrindstoneKnob> knobs;
    GrindstoneLogoType logotype;
    std::unique_ptr<GWasherButton> washer;

    juce::Component* extraComponent = nullptr;

    juce::StringArray storedKnobLabels;   // kept for painting labels in paint()

    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> knobAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrindstonePedalFace)
};
