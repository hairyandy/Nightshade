#pragma once

#include <JuceHeader.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Clipping modes
// ─────────────────────────────────────────────────────────────────────────────
enum class ClipMode : int
{
    LED       = 0,  // Soft, symmetric       — LEDs in feedback (real circuit)
    Silicon   = 1,  // Medium, symmetric     — 1N4148 model
    Germanium = 2,  // Warm, asymmetric      — 1N34A model (even harmonics)
    None      = 3   // No diodes             — op-amp rail saturation only
};

// ─────────────────────────────────────────────────────────────────────────────
//  Per-channel biquad filter state
// ─────────────────────────────────────────────────────────────────────────────
struct ChannelState
{
    juce::dsp::IIR::Filter<float> inputHPF;    // input coupling cap model
    juce::dsp::IIR::Filter<float> toneFilter;  // sweepable tone stack
    juce::dsp::IIR::Filter<float> outputLPF;   // HF rolloff / stability
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

    // Explicitly disable double-precision so hosts always call the float path
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

    // Public so the editor can bind to it
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    // ── Clipping functions ────────────────────────────────────────────────
    // f(x, Vf) = (Vf / tanh(1)) * tanh(x / Vf)
    //   - Slope = 1 at x = 0  (unity gain for small signals)
    //   - Saturates to ± Vf   (hard ceiling set by forward voltage)
    // Dividing by Vf afterwards normalises the output to ± 1.
    static float diodeClip (float x, float Vf) noexcept;
    static float opAmpSat  (float x) noexcept;
    static float applyClipping (float x, int mode) noexcept;

    // ── Filter management ─────────────────────────────────────────────────
    void updateFilters (float tone);

    // ── DSP state ─────────────────────────────────────────────────────────
    double currentSampleRate { 44100.0 };
    std::array<ChannelState, 2> channels;
    float lastTone { -1.0f };

    // Raw APVTS parameter pointers — cached in the constructor so they are
    // valid for the entire lifetime of the processor, including the very
    // first processBlock call.  Reading an atomic<float>* on the audio
    // thread is lock-free and safe.
    std::atomic<float>* rawGain { nullptr };
    std::atomic<float>* rawTone { nullptr };
    std::atomic<float>* rawVol  { nullptr };
    std::atomic<float>* rawClip { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NightshadeAudioProcessor)
};
