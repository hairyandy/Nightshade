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
static constexpr float kVf_Ge_pos = 0.15f;   // 1N34A: very low Vf, hard asymmetric clip
static constexpr float kVf_Ge_neg = 0.25f;

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
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));  // 0.5² × 4.0 = 1.0 = unity gain

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
    // C3 (0.1 µF) + R6 (1 kΩ) into virtual ground gives fc = 1591 Hz if
    // the source impedance is 0 Ω.  In hardware a guitar's ~250 kΩ output
    // impedance dominates, lowering the effective fc to ~6 Hz.  We model
    // this as an 80 Hz one-pole HPF — passes all guitar fundamentals
    // (low E = 82 Hz), blocks sub-bass mud before the high-gain stage.
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (fs, 80.0);
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

    // ── Tone LP ───────────────────────────────────────────────────────────
    // First-order swept low-pass models the 50k TONE pot + RC network.
    // Cut-only (never boosts) so the tone control cannot drive the output
    // into clipping regardless of position.
    //
    //   tone=0.0 (CCW) → fc =  400 Hz  — heavy treble cut, warm and dark
    //   tone=0.5 (ctr) → fc ≈ 1.8 kHz  — natural presence
    //   tone=1.0 (CW)  → fc = 8 kHz   — essentially flat, full harmonics
    //
    // fc = 400 × 20^tone  (logarithmic sweep)
    // Using toneLPF; toneHPF is unused.
    {
        const double fc = 400.0 * std::pow (20.0, static_cast<double> (tone));
        auto c = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (fs, fc);
        for (auto& ch : channels)
            *ch.toneLPF.coefficients = *c;
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
    // tanh × 2.0 gives a slightly softer rail saturation than × 3.0,
    // better modelling the TL072's finite slew rate at the clipping boundary.
    return std::tanh (x * 2.0f);
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
    // Square-root knob shaping front-loads the gain curve so that distortion
    // kicks in early in the knob travel (around 1 o'clock ≈ 25%), matching
    // the feel of the real pedal where gain beyond noon is already quite heavy.
    const float gainKnobShaped = std::sqrt (gainKnob);
    const float gainLin = kGainMin * std::pow (kGainMax / kGainMin, gainKnobShaped);

    // Per-mode post-clip normalisation — each mode is normalised to the same
    // peak output level so the audible difference is tonal character (soft/hard
    // knee, symmetric vs asymmetric harmonics), not just a volume shift.
    const float clipNormMax = [&]() -> float
    {
        switch (static_cast<ClipMode> (clipMode))
        {
            case ClipMode::LED:
                return kVf_LED * std::asinh (gainLin / kVf_LED);
            case ClipMode::Silicon:
                return kVf_Si  * std::asinh (gainLin / kVf_Si);
            case ClipMode::Germanium:
                // Use the softer (negative) half's Vf so we don't over-compress
                return kVf_Ge_neg * std::asinh (gainLin / kVf_Ge_neg);
            case ClipMode::None:
                // tanh produces a near-square wave at high gain; its RMS is
                // significantly higher than the diode modes at the same peak.
                // A factor of 1.8 brings the perceived loudness in line.
                return 1.8f;
        }
        return 1.0f;
    }();
    const float clipNorm = 1.0f / juce::jmax (1.0f, clipNormMax);

    // Square-law volume taper with +12 dB of headroom above the normalised
    // clipper level — a real overdrive pedal is significantly louder than
    // the dry signal at max volume.
    const float volLin = volKnob * volKnob * 4.0f;


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

            // Tone LP — swept first-order LPF (400 Hz → 8 kHz).
            // Cut-only; never boosts so it cannot interact with clipping.
            x = state.toneLPF.processSample (x);

            // Output LPF — gain-dependent 47 pF feedback cap model.
            // At high gain this rolls off ~6.6 kHz, adding smoothness.
            x = state.outputLPF.processSample (x);

            // Pre-volume clip: the filter chain (cut-only tone LP + output LPF)
            // keeps the signal within ±1.  This clip is a safety net for any
            // unexpected spikes and — crucially — ensures the volume control
            // operates on a clean bounded signal so it never interacts with
            // the distortion character.
            x = juce::jlimit (-1.0f, 1.0f, x);

            // Volume trim (+12 dB headroom: 0.5² × 4 = unity at default)
            x *= volLin;

            // Post-volume safety clip — stops runaway at extreme settings
            x = juce::jlimit (-4.0f, 4.0f, x);

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
