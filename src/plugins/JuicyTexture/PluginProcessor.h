#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <array>
#include "../../shared/JuicinessAnalyzer.h"

class JuicyTextureAudioProcessor : public juce::AudioProcessor
{
public:
    JuicyTextureAudioProcessor();
    ~JuicyTextureAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    JuicinessMetrics getLatestMetrics() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void pushJuicinessToHost(float score);

    juce::AudioProcessorValueTreeState parameters;
    JuicinessAnalyzer analyzer;
    juce::RangedAudioParameter* juicinessParameter = nullptr;

    std::atomic<float> latestPreScore { 0.0f };
    std::atomic<float> latestPostScore { 0.0f };
    std::atomic<float> latestScore { 0.0f };
    std::atomic<float> latestPunch { 0.0f };
    std::atomic<float> latestRichness { 0.0f };
    std::atomic<float> latestClarity { 0.0f };
    std::atomic<float> latestWidth { 0.0f };
    std::atomic<float> latestMonoSafety { 1.0f };

    struct ChannelState
    {
        float tail = 0.0f;
        float lp = 0.0f;
        float hp = 0.0f;
        float env = 0.0f;
        float wetEnv = 0.0f;
        float noiseHp = 0.0f;
        float dcIn = 0.0f;
        float dcOut = 0.0f;
        float protectGain = 1.0f;
        float springPos = 0.0f;
        float springVel = 0.0f;
        float fleshPosA = 0.0f;
        float fleshVelA = 0.0f;
        float fleshPosB = 0.0f;
        float fleshVelB = 0.0f;
        float prevWave = 0.0f;
        std::array<float, 4> modalY1 { 0.0f, 0.0f, 0.0f, 0.0f };
        std::array<float, 4> modalY2 { 0.0f, 0.0f, 0.0f, 0.0f };
        std::vector<float> waveguide;
        int waveIdx = 0;
    };

    std::array<ChannelState, 2> channels;
    double sr = 44100.0;
    uint32_t rng = 0x12345678u;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JuicyTextureAudioProcessor)
};
