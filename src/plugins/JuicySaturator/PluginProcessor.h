#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "../../shared/JuicinessAnalyzer.h"

class JuicySaturatorAudioProcessor : public juce::AudioProcessor
{
public:
    JuicySaturatorAudioProcessor();
    ~JuicySaturatorAudioProcessor() override = default;

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

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    JuicinessMetrics getLatestMetrics() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void pushJuicinessToHost(float score);

    juce::AudioProcessorValueTreeState parameters;
    JuicinessAnalyzer analyzer;
    juce::RangedAudioParameter* juicinessParameter = nullptr;
    std::vector<float> toneState;
    std::atomic<float> latestPreScore { 0.0f };
    std::atomic<float> latestPostScore { 0.0f };
    std::atomic<float> latestScore { 0.0f };
    std::atomic<float> latestPunch { 0.0f };
    std::atomic<float> latestRichness { 0.0f };
    std::atomic<float> latestClarity { 0.0f };
    std::atomic<float> latestWidth { 0.0f };
    std::atomic<float> latestMonoSafety { 1.0f };
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JuicySaturatorAudioProcessor)
};
