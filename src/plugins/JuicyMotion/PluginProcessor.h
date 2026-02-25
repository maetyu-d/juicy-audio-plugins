#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "../../shared/JuicinessAnalyzer.h"

class JuicyMotionAudioProcessor : public juce::AudioProcessor
{
public:
    JuicyMotionAudioProcessor();
    ~JuicyMotionAudioProcessor() override = default;

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

    double sr = 44100.0;
    float env = 0.0f;
    float repetition = 0.0f;
    float budgetEnv = 0.0f;
    float variationTone = 0.0f;
    float variationTransient = 0.0f;
    float variationTail = 0.0f;
    float variationToneTarget = 0.0f;
    float variationTransientTarget = 0.0f;
    float variationTailTarget = 0.0f;
    int onsetCooldown = 0;
    uint32_t rng = 0x93ab12f0u;
    float tailL = 0.0f;
    float tailR = 0.0f;
    float lpL = 0.0f;
    float lpR = 0.0f;
    float prevL = 0.0f;
    float prevR = 0.0f;
    float motionPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JuicyMotionAudioProcessor)
};
