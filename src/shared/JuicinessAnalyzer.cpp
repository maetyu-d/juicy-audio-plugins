#include "JuicinessAnalyzer.h"

void JuicinessAnalyzer::prepare(double sampleRate, int samplesPerBlock, int numChannels)
{
    juce::ignoreUnused(samplesPerBlock);
    sr = sampleRate;
    channels = juce::jmax(1, numChannels);
    lowCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 250.0f / static_cast<float>(sampleRate));
    highCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 2500.0f / static_cast<float>(sampleRate));
    reset();
}

void JuicinessAnalyzer::reset()
{
    shortEnv = 0.0f;
    longEnv = 0.0f;
    lowBandState = 0.0f;
    highBandState = 0.0f;
    repetitionEma = 0.0f;
    fatigueEma = 0.0f;
    onsetCooldown = 0;
}

float JuicinessAnalyzer::updateEnvelope(float input, float attackCoeff, float releaseCoeff, float& env) const noexcept
{
    const auto coeff = input > env ? attackCoeff : releaseCoeff;
    env = (1.0f - coeff) * input + coeff * env;
    return env;
}

JuicinessMetrics JuicinessAnalyzer::analyze(const juce::AudioBuffer<float>& buffer)
{
    JuicinessMetrics m;
    const auto numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return m;

    const float attackShort = std::exp(-1.0f / static_cast<float>(sr * 0.003));
    const float releaseShort = std::exp(-1.0f / static_cast<float>(sr * 0.030));
    const float attackLong = std::exp(-1.0f / static_cast<float>(sr * 0.050));
    const float releaseLong = std::exp(-1.0f / static_cast<float>(sr * 0.300));

    float transientAccum = 0.0f;
    int onsetCount = 0;
    float rmsAccum = 0.0f;
    float peak = 0.0f;
    float lowAccum = 0.0f;
    float highAccum = 0.0f;
    float sideAccum = 0.0f;
    float midAccum = 0.0f;
    float corrAccum = 0.0f;
    int corrCount = 0;

    const auto* left = buffer.getReadPointer(0);
    const float* right = channels > 1 ? buffer.getReadPointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i];
        const float r = right != nullptr ? right[i] : l;
        const float mono = 0.5f * (l + r);
        const float absMono = std::abs(mono);

        updateEnvelope(absMono, attackShort, releaseShort, shortEnv);
        updateEnvelope(absMono, attackLong, releaseLong, longEnv);

        const float transient = juce::jmax(0.0f, shortEnv - longEnv);
        transientAccum += transient;
        if (onsetCooldown > 0)
            --onsetCooldown;
        if (transient > 0.045f && onsetCooldown <= 0)
        {
            ++onsetCount;
            onsetCooldown = static_cast<int>(sr * 0.035);
        }
        rmsAccum += mono * mono;
        peak = juce::jmax(peak, std::abs(mono));

        lowBandState += lowCoeff * (mono - lowBandState);
        highBandState += highCoeff * (mono - highBandState);
        const float low = lowBandState;
        const float high = mono - highBandState;
        lowAccum += low * low;
        highAccum += high * high;

        const float mid = 0.5f * (l + r);
        const float side = 0.5f * (l - r);
        midAccum += mid * mid;
        sideAccum += side * side;
        corrAccum += l * r;
        ++corrCount;
    }

    const float invN = 1.0f / static_cast<float>(numSamples);
    const float rms = std::sqrt(rmsAccum * invN + 1.0e-12f);
    const float crest = peak / (rms + 1.0e-6f);
    const float lowEnergy = lowAccum * invN;
    const float highEnergy = highAccum * invN;
    const float lowHighRatio = lowEnergy / (highEnergy + 1.0e-8f);
    const float widthRatio = sideAccum / (midAccum + sideAccum + 1.0e-8f);

    float corr = 1.0f;
    if (corrCount > 0)
    {
        const float lEnergy = buffer.getRMSLevel(0, 0, numSamples);
        const float rEnergy = channels > 1 ? buffer.getRMSLevel(1, 0, numSamples) : lEnergy;
        corr = corrAccum * invN / (lEnergy * rEnergy + 1.0e-6f);
        corr = juce::jlimit(-1.0f, 1.0f, corr);
    }

    const float punch = juce::jlimit(0.0f, 1.0f, 6.0f * transientAccum * invN / (rms + 1.0e-5f));
    const float richness = juce::jlimit(0.0f, 1.0f, (2.3f - crest) * 0.65f + (rms * 2.0f));

    float clarity = 1.0f;
    if (lowHighRatio > 2.5f)
        clarity -= juce::jlimit(0.0f, 0.6f, (lowHighRatio - 2.5f) * 0.15f);
    if (highEnergy > 0.03f)
        clarity -= juce::jlimit(0.0f, 0.5f, (highEnergy - 0.03f) * 8.0f);
    clarity = juce::jlimit(0.0f, 1.0f, clarity);

    const float width = juce::jlimit(0.0f, 1.0f, widthRatio * 2.0f);
    const float monoSafety = juce::jlimit(0.0f, 1.0f, 0.5f * (corr + 1.0f));

    const float blockSeconds = static_cast<float>(numSamples) / static_cast<float>(sr);
    const float onsetRate = blockSeconds > 0.0f ? static_cast<float>(onsetCount) / blockSeconds : 0.0f;
    repetitionEma += (onsetRate - repetitionEma) * 0.08f;
    const float repetitionDensity = juce::jlimit(0.0f, 1.0f, repetitionEma / 12.0f);

    const float emphasis = juce::jlimit(0.0f, 1.0f, 0.62f * punch + 0.38f * juce::jlimit(0.0f, 1.0f, transientAccum * invN * 8.5f));
    const float coherence = juce::jlimit(0.0f, 1.0f, 0.50f * clarity + 0.30f * monoSafety + 0.20f * (1.0f - std::abs(width - 0.45f)));
    const float synesthesia = juce::jlimit(0.0f, 1.0f, 0.45f * richness + 0.30f * juce::jlimit(0.0f, 1.0f, lowHighRatio / 3.5f) + 0.25f * juce::jlimit(0.0f, 1.0f, transientAccum * invN * 5.0f));

    const float crestPenalty = juce::jlimit(0.0f, 1.0f, (1.8f - crest) * 1.1f);
    const float harshPenalty = juce::jlimit(0.0f, 1.0f, highEnergy * 12.0f);
    const float instantFatigue = juce::jlimit(0.0f, 1.0f, 0.35f * crestPenalty + 0.35f * harshPenalty + 0.30f * repetitionDensity);
    fatigueEma += (instantFatigue - fatigueEma) * 0.06f;
    const float fatigueRisk = juce::jlimit(0.0f, 1.0f, fatigueEma);

    float score = 100.0f * (0.30f * punch + 0.25f * richness + 0.25f * clarity + 0.20f * width);
    score *= (0.6f + 0.4f * monoSafety);
    score = juce::jlimit(0.0f, 100.0f, score);

    m.score = score;
    m.emphasis = emphasis;
    m.coherence = coherence;
    m.synesthesia = synesthesia;
    m.fatigueRisk = fatigueRisk;
    m.repetitionDensity = repetitionDensity;
    m.punch = punch;
    m.richness = richness;
    m.clarity = clarity;
    m.width = width;
    m.monoSafety = monoSafety;
    return m;
}
