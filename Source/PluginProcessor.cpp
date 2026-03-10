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
//  Waveshaper forward voltages
//
//  f(x) = Vf * asinh(x / Vf)
//    - slope = 1 at x = 0  (unity gain, no attenuation of quiet signals)
//    - compresses logarithmically above |x| ≈ Vf
//
//  As specified in the plugin design brief:
//    LED      symmetric:  Vf = 2.0
//    Silicon  symmetric:  Vf = 0.6
//    Germanium asymmetric: Vf_pos = 0.3, Vf_neg = 0.5  (adds even harmonics)
//    None     tanh × 3.0  (hard op-amp rail saturation)
// ─────────────────────────────────────────────────────────────────────────────
static constexpr float kVf_LED    = 2.0f;
static constexpr float kVf_Si     = 0.6f;
static constexpr float kVf_Ge_pos = 0.3f;
static constexpr float kVf_Ge_neg = 0.5f;

// ─────────────────────────────────────────────────────────────────────────────
//  Gain range  — from the Nightshade schematic
//  R6 (input) = 1 kΩ,  R2 (fixed feedback) = 10 kΩ,  GAIN pot = 500 kΩ
//    min gain = R2 / R6          = 10k / 1k   = 10×  (+20 dB)
//    max gain = (R2 + R5) / R6   = 510k / 1k  = 510× (+54 dB)
// ─────────────────────────────────────────────────────────────────────────────
static constexpr float kGainMin = 10.0f;
static constexpr float kGainMax = 510.0f;

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

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        kClipID, "Clip Mode",
        juce::StringArray { "LED", "Silicon", "Germanium", "None" }, 0));

    return layout;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
//  Parameter pointers are cached here so they are valid for the entire
//  processor lifetime, including any early processBlock call.
// ─────────────────────────────────────────────────────────────────────────────
NightshadeAudioProcessor::NightshadeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "NightshadeState", createParameterLayout())
{
    rawGain = apvts.getRawParameterValue (kGainID);
    rawTone = apvts.getRawParameterValue (kToneID);
    rawVol  = apvts.getRawParameterValue (kVolID);
    rawClip = apvts.getRawParameterValue (kClipID);

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

    // Prepare base-rate per-channel filters
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels      = 1;

    for (auto& ch : channels)
    {
        ch.inputHPF.prepare   (spec);
        ch.warmthLPF.prepare  (spec);
        ch.toneLPF.prepare    (spec);
        ch.toneHPF.prepare    (spec);
        ch.outputLPF.prepare  (spec);

        ch.inputHPF.reset();
        ch.warmthLPF.reset();
        ch.toneLPF.reset();
        ch.toneHPF.reset();
        ch.outputLPF.reset();
    }

    // Prepare 4× oversampler
    oversampler.initProcessing (static_cast<size_t> (samplesPerBlock));
    oversampler.reset();

    // Build initial filter coefficients
    lastTone     = -1.0f;
    lastGainKnob = -1.0f;
    updateFilters    (rawTone->load());
    updateOutputLPF  (rawGain->load());
}

void NightshadeAudioProcessor::releaseResources()
{
    oversampler.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
//  updateFilters  — tone-dependent coefficients
// ─────────────────────────────────────────────────────────────────────────────
void NightshadeAudioProcessor::updateFilters (float tone)
{
    lastTone      = tone;
    const double fs = currentSampleRate;

    // ── Input HPF ─────────────────────────────────────────────────────────
    // C3 (0.1 µF) into R6 (1 kΩ): fc = 1 / (2π × 1k × 0.1µ) = 1591 Hz
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, 1591.0, 0.707);
        for (auto& ch : channels) *ch.inputHPF.coefficients = *c;
    }

    // ── Warmth LPF ────────────────────────────────────────────────────────
    // One-pole low-pass at 6 kHz applied immediately after the clipping stage.
    // This is the single biggest contributor to the pedal's thick, syrupy
    // character — it smooths the upper harmonics produced by the waveshaper
    // before they reach the tone and volume stages.
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (fs, 6000.0);
        for (auto& ch : channels) *ch.warmthLPF.coefficients = *c;
    }

    // ── Tone shelf ────────────────────────────────────────────────────────
    // The 50k TONE pot blends between the 6.8 nF (treble) and 1 µF (bass)
    // capacitor paths, acting as a variable treble control.
    //
    // Modelled as a first-order high shelf centred at 1 kHz:
    //   tone=0.0 (CCW) → -14 dB shelf  — warm, full bass, highs rolled off
    //   tone=0.5 (centre) → 0 dB       — flat, no insertion loss
    //   tone=1.0 (CW) → +14 dB shelf   — bright, upper harmonics accentuated
    //
    // Using toneHPF as the shelf; toneLPF is unused.
    {
        const float shelfGainDb = (tone - 0.5f) * 28.0f;       // -14 … +14 dB
        const float shelfGain   = juce::Decibels::decibelsToGain (shelfGainDb);
        auto c = juce::dsp::IIR::Coefficients<float>::makeHighShelf (fs, 1000.0, 0.707, shelfGain);
        for (auto& ch : channels)
            *ch.toneHPF.coefficients = *c;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  updateOutputLPF  — gain-dependent 47 pF feedback cap model
// ─────────────────────────────────────────────────────────────────────────────
void NightshadeAudioProcessor::updateOutputLPF (float gainKnob)
{
    lastGainKnob = gainKnob;

    // In the real circuit the 47 pF cap sits in parallel with the feedback
    // resistor (R2 + the GAIN pot wiper), so its cutoff is:
    //   fc = 1 / (2π × R_feedback × 47pF)
    //
    // As gain increases, R_feedback increases and fc moves down into the audio
    // band, rolling off harshness and adding the characteristic warmth.
    //
    //   gainKnob=0  → R = 10 kΩ   → fc ≈ 338 kHz  (inaudible)
    //   gainKnob=0.5 → R = 260 kΩ  → fc ≈  13 kHz
    //   gainKnob=1.0 → R = 510 kΩ  → fc ≈   6.6 kHz  (clearly audible warmth)

    const float R_feedback = 10000.0f + gainKnob * 500000.0f;  // 10k – 510k Ω
    const double fc = 1.0 / (juce::MathConstants<double>::twoPi
                             * static_cast<double> (R_feedback)
                             * 47e-12);

    // Clamp to a useful audio range; at low gain the cutoff is above hearing
    const double fc_clamped = juce::jlimit (2000.0, 20000.0, fc);

    // One RC pole in the feedback path = first-order roll-off, not biquad.
    auto c = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (
        currentSampleRate, fc_clamped);
    for (auto& ch : channels)
        *ch.outputLPF.coefficients = *c;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Waveshaper
// ─────────────────────────────────────────────────────────────────────────────

// f(x) = Vf * asinh(x / Vf)
// For |x| << Vf : output ≈ x          (linear, no distortion)
// For |x| >> Vf : output ≈ Vf*ln(2x/Vf)  (logarithmic compression)
float NightshadeAudioProcessor::diodeClip (float x, float Vf) noexcept
{
    return Vf * std::asinh (x / Vf);
}

// Hard op-amp rail saturation — models the output stage clipping when no
// diodes are present in the feedback path.
float NightshadeAudioProcessor::opAmpSat (float x) noexcept
{
    return std::tanh (x * 3.0f);
}

float NightshadeAudioProcessor::applyClipping (float x, int mode) noexcept
{
    mode = juce::jlimit (0, 3, mode);

    switch (static_cast<ClipMode> (mode))
    {
        case ClipMode::LED:
            // f(x) = 2.0 * asinh(x / 2.0)   — soft, symmetric
            return diodeClip (x, kVf_LED);

        case ClipMode::Silicon:
            // f(x) = 0.6 * asinh(x / 0.6)   — tighter knee, symmetric
            return diodeClip (x, kVf_Si);

        case ClipMode::Germanium:
            // Positive half: f(x) =  0.3 * asinh(x  / 0.3)
            // Negative half: f(x) = -0.5 * asinh(-x / 0.5)
            // Asymmetric response introduces even-order harmonics (warmth).
            if (x >= 0.0f)
                return  diodeClip ( x, kVf_Ge_pos);
            else
                return -diodeClip (-x, kVf_Ge_neg);

        case ClipMode::None:
            // f(x) = tanh(x * 3.0) — harder saturation modelling op-amp rails
            return opAmpSat (x);
    }

    return x;
}

// ─────────────────────────────────────────────────────────────────────────────
//  isBusesLayoutSupported
// ─────────────────────────────────────────────────────────────────────────────
bool NightshadeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() &&
        out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  processBlock
// ─────────────────────────────────────────────────────────────────────────────
void NightshadeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    // ── Parameter reads (APVTS atomics, lock-free) ────────────────────────
    const float gainKnob = rawGain->load();
    const float toneKnob = rawTone->load();
    const float volKnob  = rawVol->load();
    const int   clipMode = juce::jlimit (0, 3, static_cast<int> (rawClip->load()));

    // ── Gain staging ──────────────────────────────────────────────────────
    // Log-space interpolation from schematic values: 10× – 510×
    const float gainLin = kGainMin * std::pow (kGainMax / kGainMin, gainKnob);

    // Post-clip normalisation: scale so LED mode at current gain + 0 dBFS ≈ ±1.
    // As gain increases, the normaliser compensates to keep loudness consistent.
    // The result is that the Volume knob is the primary level control regardless
    // of gain setting — just like the real pedal.
    const float clipNorm = 1.0f / juce::jmax (1.0f, kVf_LED * std::asinh (gainLin / kVf_LED));

    // Square-law volume taper
    const float volLin = volKnob * volKnob;


    // ── Rebuild filter coefficients if parameters moved ───────────────────
    if (toneKnob != lastTone)
        updateFilters (toneKnob);

    if (gainKnob != lastGainKnob)
        updateOutputLPF (gainKnob);

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // ── Stage 1: Input HPF at base rate ───────────────────────────────────
    // Applied before upsampling so the oversampler never sees DC or sub-bass.
    for (int ch = 0; ch < numChannels && ch < 2; ++ch)
    {
        float* data = buffer.getWritePointer (ch);
        for (int n = 0; n < numSamples; ++n)
            data[n] = channels[static_cast<size_t> (ch)].inputHPF.processSample (data[n]);
    }

    // ── Stage 2: 4× upsample → gain + clip → downsample ──────────────────
    // Running the waveshaper at 4× sample rate drastically reduces aliasing
    // artefacts that cause the harsh, buzzy quality at high gain settings.
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto osBlock = oversampler.processSamplesUp (block);

        const int osChannels = static_cast<int> (osBlock.getNumChannels());
        const int osSamples  = static_cast<int> (osBlock.getNumSamples());

        for (int ch = 0; ch < osChannels && ch < 2; ++ch)
        {
            float* data = osBlock.getChannelPointer (static_cast<size_t> (ch));

            for (int n = 0; n < osSamples; ++n)
            {
                float x = data[n];

                // Gain stage — drives signal into the waveshaper
                x *= gainLin;

                // Waveshaper (diode / op-amp model)
                x = applyClipping (x, clipMode);

                // Normalise back to ±1 before returning to base rate
                x *= clipNorm;

                data[n] = x;
            }
        }

        oversampler.processSamplesDown (block);
    }

    // ── Stage 3: Post-clip chain at base rate ─────────────────────────────
    for (int ch = 0; ch < numChannels && ch < 2; ++ch)
    {
        auto&  state = channels[static_cast<size_t> (ch)];
        float* data  = buffer.getWritePointer (ch);

        for (int n = 0; n < numSamples; ++n)
        {
            float x = data[n];

            // Warmth LPF (6 kHz one-pole) — the primary source of the
            // thick, syrupy character.  Smooths upper harmonics from the
            // waveshaper before they reach the tone stack.
            x = state.warmthLPF.processSample (x);

            // Tone shelf — high-shelf from -14 dB (CCW) to +14 dB (CW),
            // flat at centre.  Models the 50k pot / 6.8 nF + 1 µF RC network
            // via SECRETB with no insertion loss at the mid position.
            x = state.toneHPF.processSample (x);

            // Output LPF — gain-dependent 47 pF feedback cap model.
            // At high gain this rolls off ~6.6 kHz, adding smoothness.
            x = state.outputLPF.processSample (x);

            // Volume trim
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
