#include "PluginProcessor.h"
#include "../../shared/JuicyPluginEditor.h"
#include <array>

namespace
{
struct SaturatorPreset
{
    const char* name;
    float drive;
    float asymmetry;
    float tone;
    float mix;
    float output;
};

constexpr std::array<SaturatorPreset, 5> saturatorPresets { {
    { "Amber Heat", 6.0f, 0.1f, 0.55f, 1.0f, -3.0f },
    { "Velvet Burn", 11.0f, 0.2f, 0.4f, 0.85f, -6.0f },
    { "Mirror Glow", 8.0f, -0.15f, 0.75f, 0.7f, -4.0f },
    { "Grain Reactor", 18.0f, 0.35f, 0.32f, 1.0f, -10.0f },
    { "Crystal Edge", 4.0f, -0.05f, 0.9f, 0.55f, -1.0f }
} };
}

JuicySaturatorAudioProcessor::JuicySaturatorAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMS", createParameterLayout())
{
    juicinessParameter = parameters.getParameter("juiciness");
    setCurrentProgram(0);
}

void JuicySaturatorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    analyzer.prepare(sampleRate, samplesPerBlock, getTotalNumInputChannels());
    toneState.assign(static_cast<size_t>(juce::jmax(1, getTotalNumOutputChannels())), 0.0f);
}

void JuicySaturatorAudioProcessor::releaseResources()
{
}

bool JuicySaturatorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != layouts.getMainOutputChannelSet())
        return false;
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void JuicySaturatorAudioProcessor::pushJuicinessToHost(float score)
{
    if (juicinessParameter == nullptr)
        return;
    const auto range = juicinessParameter->getNormalisableRange();
    juicinessParameter->setValueNotifyingHost(range.convertTo0to1(score));
}

void JuicySaturatorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalInputChannels = getTotalNumInputChannels();
    const int totalOutputChannels = getTotalNumOutputChannels();
    for (int i = totalInputChannels; i < totalOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const float driveDb = *parameters.getRawParameterValue("drive");
    const float asym = *parameters.getRawParameterValue("asymmetry");
    const float tone = *parameters.getRawParameterValue("tone");
    const float mix = *parameters.getRawParameterValue("mix");
    const float outputDb = *parameters.getRawParameterValue("output");

    const auto preMetrics = analyzer.analyze(buffer);
    const float inGain = juce::Decibels::decibelsToGain(driveDb);
    const float outGain = juce::Decibels::decibelsToGain(outputDb);
    const float cutoff = juce::jmap(tone, 0.0f, 1.0f, 2500.0f, 16000.0f);
    const float toneCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * cutoff / static_cast<float>(getSampleRate()));

    for (int ch = 0; ch < totalInputChannels; ++ch)
    {
        auto* x = buffer.getWritePointer(ch);
        auto& state = toneState[static_cast<size_t>(ch)];
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float dry = x[i];
            const float driven = dry * inGain;
            const float skewed = driven + asym * driven * driven;
            const float soft = std::tanh(skewed);
            state += toneCoeff * (soft - state);
            const float toned = state;
            const float wet = toned * outGain;
            x[i] = dry + mix * (wet - dry);
        }
    }

    auto metrics = analyzer.analyze(buffer);
    latestPreScore.store(preMetrics.score, std::memory_order_relaxed);
    latestPostScore.store(metrics.score, std::memory_order_relaxed);
    latestScore.store(metrics.score, std::memory_order_relaxed);
    latestPunch.store(metrics.punch, std::memory_order_relaxed);
    latestRichness.store(metrics.richness, std::memory_order_relaxed);
    latestClarity.store(metrics.clarity, std::memory_order_relaxed);
    latestWidth.store(metrics.width, std::memory_order_relaxed);
    latestMonoSafety.store(metrics.monoSafety, std::memory_order_relaxed);
    pushJuicinessToHost(metrics.score);
}

juce::AudioProcessorEditor* JuicySaturatorAudioProcessor::createEditor()
{
    return new JuicyPluginEditor(*this, parameters, [this]() { return getLatestMetrics(); }, "Juicy Saturator");
}

void JuicySaturatorAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void JuicySaturatorAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState == nullptr)
        return;
    if (xmlState->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

int JuicySaturatorAudioProcessor::getNumPrograms()
{
    return static_cast<int>(saturatorPresets.size());
}

int JuicySaturatorAudioProcessor::getCurrentProgram()
{
    return currentProgram;
}

void JuicySaturatorAudioProcessor::setCurrentProgram(int index)
{
    currentProgram = juce::jlimit(0, getNumPrograms() - 1, index);
    const auto& p = saturatorPresets[static_cast<size_t>(currentProgram)];

    auto setParam = [this](const char* id, float value)
    {
        if (auto* param = parameters.getParameter(id))
        {
            const auto range = param->getNormalisableRange();
            param->setValueNotifyingHost(range.convertTo0to1(value));
        }
    };

    setParam("drive", p.drive);
    setParam("asymmetry", p.asymmetry);
    setParam("tone", p.tone);
    setParam("mix", p.mix);
    setParam("output", p.output);
}

const juce::String JuicySaturatorAudioProcessor::getProgramName(int index)
{
    const int safe = juce::jlimit(0, getNumPrograms() - 1, index);
    return saturatorPresets[static_cast<size_t>(safe)].name;
}

void JuicySaturatorAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

JuicinessMetrics JuicySaturatorAudioProcessor::getLatestMetrics() const noexcept
{
    JuicinessMetrics m;
    m.preScore = latestPreScore.load(std::memory_order_relaxed);
    m.postScore = latestPostScore.load(std::memory_order_relaxed);
    m.score = latestScore.load(std::memory_order_relaxed);
    m.punch = latestPunch.load(std::memory_order_relaxed);
    m.richness = latestRichness.load(std::memory_order_relaxed);
    m.clarity = latestClarity.load(std::memory_order_relaxed);
    m.width = latestWidth.load(std::memory_order_relaxed);
    m.monoSafety = latestMonoSafety.load(std::memory_order_relaxed);
    return m;
}

juce::AudioProcessorValueTreeState::ParameterLayout JuicySaturatorAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("drive", "Drive (dB)", 0.0f, 24.0f, 6.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("asymmetry", "Asymmetry", -0.5f, 0.5f, 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("tone", "Tone", 0.0f, 1.0f, 0.55f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 1.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output (dB)", -18.0f, 18.0f, -3.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("juiciness", "Juiciness Score", 0.0f, 100.0f, 0.0f));
    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuicySaturatorAudioProcessor();
}
