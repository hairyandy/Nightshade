#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Parameter IDs
// ─────────────────────────────────────────────────────────────────────────────
static const juce::String kGainID = "gain";
static const juce::String kToneID = "tone";
static const juce::String kVolID  = "vol";
static const juce::String kClipID = "clipmode";

// ─────────────────────────────────────────────────────────────────────────────
//  Clipping thresholds
//
//  Vf is the "forward voltage" constant that sets the knee of each clipper.
//  Used inside:  f(x) = (Vf / tanh(1)) * tanh(x / Vf)  which gives:
//    - slope = 1 at x = 0   (unity gain — no attenuation of quiet signals)
//    - hard ceiling at ± Vf  (then normalised to ± 1 by dividing by Vf)
//  Smaller Vf → knee occurs at a lower drive level → harder / earlier clip.
// ─────────────────────────────────────────────────────────────────────────────
// Normalisation constant: tanh(1) ≈ 0.7616 — ensures slope = 1 at x = 0
static const float kTanh1 = std::tanh (1.0f);

static constexpr float kVf_LED    = 2.00f;  // gentle — LEDs in feedback (real circuit)
static constexpr float kVf_Si     = 0.60f;  // medium — 1N4148 silicon diode
static constexpr float kVf_Ge_pos = 0.30f;  // hard positive half — 1N34A germanium
static constexpr float kVf_Ge_neg = 0.25f;  // harder negative half (asymmetric even harmonics)
static constexpr float kVf_None   = 0.40f;  // tightest — op-amp rail limiting

// ─────────────────────────────────────────────────────────────────────────────
//  Gain staging — derived from the Nightshade schematic:
//
//    R6 (input resistor)          =   1 kΩ
//    R2 (feedback, sets min gain) =  10 kΩ   → min gain = 10k / 1k = 10×
//    GAIN pot                     = 500 kΩ   → max gain = 510k / 1k = 510×
//
//  knob [0, 1] is mapped through log-space so the sweep feels linear in dB.
// ─────────────────────────────────────────────────────────────────────────────
static constexpr float kGainMin = 10.0f;    // R2 / R6
static constexpr float kGainMax = 510.0f;   // (R2 + Rgain) / R6

// ─────────────────────────────────────────────────────────────────────────────
//  Parameter layout
// ─────────────────────────────────────────────────────────────────────────────
juce::AudioProcessorValueTreeState::ParameterLayout
NightshadeAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kGainID, "Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kToneID, "Tone",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kVolID, "Vol",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.75f));

    // AudioParameterChoice raw value is the integer index (0, 1, 2, 3)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        kClipID, "Clip Mode",
        juce::StringArray { "LED", "Silicon", "Germanium", "None" }, 0));

    return layout;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
//  IMPORTANT: APVTS parameter pointers are cached HERE so they are valid
//  for the entire processor lifetime — including any processBlock call that
//  might theoretically arrive before prepareToPlay in some hosts.
// ─────────────────────────────────────────────────────────────────────────────
NightshadeAudioProcessor::NightshadeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "NightshadeState", createParameterLayout())
{
    // Cache direct pointers into the APVTS parameter atomics.
    // These are std::atomic<float>* — lock-free reads are safe on the audio thread.
    rawGain = apvts.getRawParameterValue (kGainID);
    rawTone = apvts.getRawParameterValue (kToneID);
    rawVol  = apvts.getRawParameterValue (kVolID);
    rawClip = apvts.getRawParameterValue (kClipID);

    // Paranoia check — these must never be null after APVTS construction
    jassert (rawGain != nullptr && rawTone != nullptr &&
             rawVol  != nullptr && rawClip != nullptr);
}

NightshadeAudioProcessor::~NightshadeAudioProcessor() {}

// ─────────────────────────────────────────────────────────────────────────────
//  prepareToPlay
// ─────────────────────────────────────────────────────────────────────────────
void NightshadeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels      = 1;   // each ChannelState owns one channel

    for (auto& ch : channels)
    {
        ch.inputHPF.prepare   (spec);
        ch.toneFilter.prepare (spec);
        ch.outputLPF.prepare  (spec);

        ch.inputHPF.reset();
        ch.toneFilter.reset();
        ch.outputLPF.reset();
    }

    // Build filter coefficients using the current tone value
    lastTone = -1.0f;
    updateFilters (rawTone->load());
}

void NightshadeAudioProcessor::releaseResources() {}

// ─────────────────────────────────────────────────────────────────────────────
//  updateFilters
// ─────────────────────────────────────────────────────────────────────────────
void NightshadeAudioProcessor::updateFilters (float tone)
{
    lastTone      = tone;
    const double fs = currentSampleRate;

    // ── Input HPF ─────────────────────────────────────────────────────────
    // C3 (0.1 µF) in series with R6 (1 kΩ):
    //   fc = 1 / (2π × 1kΩ × 0.1µF) ≈ 1591 Hz
    // Rolls off everything below ~1.6 kHz before the gain stage, which is
    // why the real pedal sounds focused / tight rather than muddy.
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, 1591.0, 0.707);
        for (auto& ch : channels) *ch.inputHPF.coefficients = *c;
    }

    // ── Tone stack ────────────────────────────────────────────────────────
    // 50k pot with 6.8 nF and 1 µF caps (post-clip, between op-amp stages).
    // tone = 0  → dark  (low-shelf cut   at ~1 kHz, –12 dB)
    // tone = 0.5 → flat  (shelf gain = 0 dB)
    // tone = 1  → bright (high-shelf boost at ~8 kHz, +12 dB)
    {
        const double fcLow  = 1000.0;
        const double fcHigh = 8000.0;
        const double fc     = fcLow * std::pow (fcHigh / fcLow, static_cast<double> (tone));

        if (tone < 0.5f)
        {
            const float dB = juce::jmap (tone, 0.0f, 0.5f, -12.0f, 0.0f);
            auto c = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
                fs, fc, 0.707, juce::Decibels::decibelsToGain (dB));
            for (auto& ch : channels) *ch.toneFilter.coefficients = *c;
        }
        else
        {
            const float dB = juce::jmap (tone, 0.5f, 1.0f, 0.0f, 12.0f);
            auto c = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                fs, fc, 0.707, juce::Decibels::decibelsToGain (dB));
            for (auto& ch : channels) *ch.toneFilter.coefficients = *c;
        }
    }

    // ── Output LPF ────────────────────────────────────────────────────────
    // 47 pF feedback cap across ~100k: fc = 1/(2π×100k×47pF) ≈ 34 kHz.
    // Its audio role is minimal (op-amp stability), so we model it as a
    // very gentle air roll-off at 20 kHz just to tame aliasing artifacts.
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makeLowPass (fs, 20000.0, 0.707);
        for (auto& ch : channels) *ch.outputLPF.coefficients = *c;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Clipping functions
// ─────────────────────────────────────────────────────────────────────────────

// Normalised soft-clipper:
//   f(x, Vf) = (Vf / tanh(1)) * tanh(x / Vf)
//
// Why this formula?
//   tanh alone has slope 1/Vf at x=0, which attenuates quiet signals.
//   Multiplying by Vf/tanh(1) restores slope = 1 at x = 0, giving
//   true unity gain when the signal is below the knee.
//   The output is then divided by Vf so the saturation ceiling is ± 1,
//   giving a consistent output level regardless of which mode is selected.
//
//   Combined: (Vf / tanh(1)) * tanh(x / Vf) / Vf  =  tanh(x / Vf) / tanh(1)
float NightshadeAudioProcessor::diodeClip (float x, float Vf) noexcept
{
    return std::tanh (x / Vf) / kTanh1;
}

// Op-amp rail saturation — hardest of the four modes.
// Uses the tightest Vf (kVf_None = 0.40) through the same normalised function.
float NightshadeAudioProcessor::opAmpSat (float x) noexcept
{
    return std::tanh (x / kVf_None) / kTanh1;
}

float NightshadeAudioProcessor::applyClipping (float x, int mode) noexcept
{
    // Clamp mode to valid range — guards against unexpected host values
    mode = juce::jlimit (0, 3, mode);

    switch (static_cast<ClipMode> (mode))
    {
        case ClipMode::LED:
            return diodeClip (x, kVf_LED);

        case ClipMode::Silicon:
            return diodeClip (x, kVf_Si);

        case ClipMode::Germanium:
            // Asymmetric: different Vf on each half introduces even-order harmonics.
            if (x >= 0.0f)
                return  diodeClip ( x, kVf_Ge_pos);
            else
                return -diodeClip (-x, kVf_Ge_neg);

        case ClipMode::None:
            return opAmpSat (x);
    }

    return x; // unreachable
}

// ─────────────────────────────────────────────────────────────────────────────
//  isBusesLayoutSupported
// ─────────────────────────────────────────────────────────────────────────────
bool NightshadeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::mono() &&
        out != juce::AudioChannelSet::stereo())
        return false;

    return in == out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  processBlock
// ─────────────────────────────────────────────────────────────────────────────
void NightshadeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    // ── Read parameters directly from APVTS atomics ───────────────────────
    // Pointers are cached in the constructor — guaranteed non-null.
    const float gainKnob = rawGain->load();   // normalised [0, 1]
    const float toneKnob = rawTone->load();   // normalised [0, 1]
    const float volKnob  = rawVol->load();    // normalised [0, 1]
    const int   clipMode = static_cast<int> (rawClip->load());   // 0..3

    // Gain: log-space interpolation across schematic range [10×, 510×]
    // knob=0.0 → 10×  (+20 dB, minimum gain from R2/R6)
    // knob=0.5 → ~71× (+37 dB)
    // knob=1.0 → 510× (+54 dB, full pot travel)
    const float gainLin = kGainMin * std::pow (kGainMax / kGainMin, gainKnob);

    // Volume: square-law taper approximates an audio pot
    const float volLin = volKnob * volKnob;

    // Rebuild tone coefficients if the knob has moved (cheap comparison)
    if (toneKnob != lastTone)
        updateFilters (toneKnob);

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    for (int ch = 0; ch < numChannels && ch < 2; ++ch)
    {
        auto&  state = channels[static_cast<size_t> (ch)];
        float* data  = buffer.getWritePointer (ch);

        for (int n = 0; n < numSamples; ++n)
        {
            float x = data[n];

            // 1. Input coupling HPF  (C3 / R6 model, fc ≈ 1591 Hz)
            x = state.inputHPF.processSample (x);

            // 2. Gain stage  (inverting amp, R6 input / R2+GAIN feedback)
            x *= gainLin;

            // 3. Clipping stage  (diodes in feedback loop)
            x = applyClipping (x, clipMode);

            // 4. Tone stack  (50k pot, 6.8 nF / 1 µF caps)
            x = state.toneFilter.processSample (x);

            // 5. Output HF rolloff  (47 pF feedback cap model)
            x = state.outputLPF.processSample (x);

            // 6. Volume trim
            x *= volLin;

            data[n] = x;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  State persistence
// ─────────────────────────────────────────────────────────────────────────────
void NightshadeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void NightshadeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Factory
// ─────────────────────────────────────────────────────────────────────────────
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NightshadeAudioProcessor();
}

juce::AudioProcessorEditor* NightshadeAudioProcessor::createEditor()
{
    return new NightshadeAudioProcessorEditor (*this);
}
