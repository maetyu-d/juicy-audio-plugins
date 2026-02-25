#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "../../shared/JuicinessAnalyzer.h"

class JuicyInferAudioProcessor : public juce::AudioProcessor
{
public:
    JuicyInferAudioProcessor();
    ~JuicyInferAudioProcessor() override = default;

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
    void setCurrentProgram(int index) override { juce::ignoreUnused(index); }
    const juce::String getProgramName(int index) override { juce::ignoreUnused(index); return {}; }
    void changeProgramName(int index, const juce::String& newName) override { juce::ignoreUnused(index, newName); }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    JuicinessMetrics getLatestMetrics() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void pushJuicinessToHost(float score);

    juce::AudioProcessorValueTreeState parameters;
    JuicinessAnalyzer analyzer;
    juce::RangedAudioParameter* juicinessParameter = nullptr;
    std::atomic<float> latestScore { 0.0f };
    std::atomic<float> latestPunch { 0.0f };
    std::atomic<float> latestRichness { 0.0f };
    std::atomic<float> latestClarity { 0.0f };
    std::atomic<float> latestWidth { 0.0f };
    std::atomic<float> latestMonoSafety { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JuicyInferAudioProcessor)
};
