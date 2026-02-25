#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

struct JuicinessMetrics
{
    float score = 0.0f;
    float punch = 0.0f;
    float richness = 0.0f;
    float clarity = 0.0f;
    float width = 0.0f;
    float monoSafety = 1.0f;
};

class JuicinessAnalyzer
{
public:
    void prepare(double sampleRate, int samplesPerBlock, int numChannels);
    void reset();
    JuicinessMetrics analyze(const juce::AudioBuffer<float>& buffer);

private:
    float updateEnvelope(float input, float attackCoeff, float releaseCoeff, float& env) const noexcept;

    double sr = 44100.0;
    int channels = 2;
    float shortEnv = 0.0f;
    float longEnv = 0.0f;
    float lowBandState = 0.0f;
    float highBandState = 0.0f;
    float lowCoeff = 0.0f;
    float highCoeff = 0.0f;
};
