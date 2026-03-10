#pragma once
#include <JuceHeader.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Clipping modes
// ─────────────────────────────────────────────────────────────────────────────
enum class ClipMode : int
{
    LED       = 0,
    Silicon   = 1,
    Germanium = 2,
    None      = 3
};

// ─────────────────────────────────────────────────────────────────────────────
//  Per-channel filter state
//  All filters run at base sample rate except the clipper, which runs at 4×
//  inside the oversampler block.
// ─────────────────────────────────────────────────────────────────────────────
struct ChannelState
{
    juce::dsp::IIR::Filter<float> inputHPF;    // pre-gain  — 1591 Hz coupling cap model
    juce::dsp::IIR::Filter<float> warmthLPF;   // post-clip — 6 kHz one-pole warmth
    juce::dsp::IIR::Filter<float> toneLPF;     // tone LP path (blended)
    juce::dsp::IIR::Filter<float> toneHPF;     // tone HP path (blended)
    juce::dsp::IIR::Filter<float> outputLPF;   // post-tone — gain-dependent 47 pF model
};

// ─────────────────────────────────────────────────────────────────────────────
//  NightshadeAudioProcessor
// ─────────────────────────────────────────────────────────────────────────────
class NightshadeAudioProcessor : public juce::AudioProcessor
{
public:
    NightshadeAudioProcessor();
    ~NightshadeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool supportsDoublePrecisionProcessing() const override { return false; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Nightshade"; }
    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    // ── Waveshaper ────────────────────────────────────────────────────────
    // f(x) = Vf * asinh(x / Vf)  — unity gain for small x, log compression above Vf
    static float diodeClip (float x, float Vf) noexcept;
    static float opAmpSat  (float x) noexcept;
    static float applyClipping (float x, int mode) noexcept;

    // ── Filter management ─────────────────────────────────────────────────
    void updateFilters    (float tone);
    void updateOutputLPF  (float gainKnob);

    // ── DSP state ─────────────────────────────────────────────────────────
    double currentSampleRate { 44100.0 };
    std::array<ChannelState, 2> channels;

    float lastTone     { -1.0f };
    float lastGainKnob { -1.0f };

    // 4× oversampler — used exclusively for the clipping stage to prevent aliasing.
    // Constructed with 2 channels, factor 2 (2^2 = 4×).
    juce::dsp::Oversampling<float> oversampler {
        2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };

    // Direct APVTS parameter pointers — cached in constructor, always valid.
    std::atomic<float>* rawGain { nullptr };
    std::atomic<float>* rawTone { nullptr };
    std::atomic<float>* rawVol  { nullptr };
    std::atomic<float>* rawClip { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NightshadeAudioProcessor)
};
