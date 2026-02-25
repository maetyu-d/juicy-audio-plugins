#include "PluginProcessor.h"
#include "../../shared/JuicyPluginEditor.h"
#include <array>

namespace
{
struct WidthPreset
{
    const char* name;
    float width;
    float haasMs;
    float monoSafe;
    float mix;
    float output;
};

constexpr std::array<WidthPreset, 5> widthPresets { {
    { "Prism Arc", 0.45f, 12.0f, 0.7f, 1.0f, 0.0f },
    { "Outer Halo", 0.9f, 22.0f, 0.35f, 1.0f, -1.5f },
    { "Studio Spine", 0.35f, 8.0f, 0.95f, 0.8f, 0.0f },
    { "Ribbon Drift", 0.7f, 16.0f, 0.55f, 0.65f, -0.5f },
    { "Monolith Wide", 1.0f, 30.0f, 0.2f, 1.0f, -3.0f }
} };
}

JuicyWidthAudioProcessor::JuicyWidthAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMS", createParameterLayout())
{
    juicinessParameter = parameters.getParameter("juiciness");
    setCurrentProgram(0);
}

void JuicyWidthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    analyzer.prepare(sampleRate, samplesPerBlock, getTotalNumInputChannels());
    const int delaySamples = static_cast<int>(sampleRate * 0.060);
    delayBuffer.setSize(2, juce::jmax(1, delaySamples));
    delayBuffer.clear();
    delayWritePosition = 0;
}

void JuicyWidthAudioProcessor::releaseResources()
{
}

bool JuicyWidthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != layouts.getMainOutputChannelSet())
        return false;
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void JuicyWidthAudioProcessor::pushJuicinessToHost(float score)
{
    if (juicinessParameter == nullptr)
        return;
    const auto range = juicinessParameter->getNormalisableRange();
    juicinessParameter->setValueNotifyingHost(range.convertTo0to1(score));
}

void JuicyWidthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalInputChannels = getTotalNumInputChannels();
    const int totalOutputChannels = getTotalNumOutputChannels();
    for (int i = totalInputChannels; i < totalOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const auto preMetrics = analyzer.analyze(buffer);

    if (totalInputChannels < 2)
    {
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
        return;
    }

    const int delayBufferSize = delayBuffer.getNumSamples();
    const int delaySamples = static_cast<int>(getSampleRate() * (*parameters.getRawParameterValue("haasMs") * 0.001f));
    float width = *parameters.getRawParameterValue("width");
    const float monoSafe = *parameters.getRawParameterValue("monoSafe");
    const float mix = *parameters.getRawParameterValue("mix");
    const float outputDb = *parameters.getRawParameterValue("output");
    const float outputGain = juce::Decibels::decibelsToGain(outputDb);

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    auto* delayLeft = delayBuffer.getWritePointer(0);
    auto* delayRight = delayBuffer.getWritePointer(1);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float dryL = left[i];
        const float dryR = right[i];

        const float corrProxy = juce::jlimit(-1.0f, 1.0f, dryL * dryR * 12.0f);
        const float dynamicLimit = juce::jmap(monoSafe, 0.0f, 1.0f, 1.0f, 0.35f);
        if (corrProxy < -0.1f)
            width *= dynamicLimit;

        const float mid = 0.5f * (dryL + dryR);
        const float side = 0.5f * (dryL - dryR) * (1.0f + width);
        float wetL = mid + side;
        float wetR = mid - side;

        delayLeft[delayWritePosition] = wetL;
        delayRight[delayWritePosition] = wetR;

        int readPos = delayWritePosition - delaySamples;
        if (readPos < 0)
            readPos += delayBufferSize;

        // Haas shift: delay right relative to left for controlled decorrelation.
        const float haasL = wetL;
        const float haasR = delayRight[readPos];
        wetL = haasL;
        wetR = haasR;

        left[i] = (dryL + mix * (wetL - dryL)) * outputGain;
        right[i] = (dryR + mix * (wetR - dryR)) * outputGain;

        ++delayWritePosition;
        if (delayWritePosition >= delayBufferSize)
            delayWritePosition = 0;
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

juce::AudioProcessorEditor* JuicyWidthAudioProcessor::createEditor()
{
    return new JuicyPluginEditor(*this, parameters, [this]() { return getLatestMetrics(); }, "Juicy Width");
}

void JuicyWidthAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void JuicyWidthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState == nullptr)
        return;
    if (xmlState->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

int JuicyWidthAudioProcessor::getNumPrograms()
{
    return static_cast<int>(widthPresets.size());
}

int JuicyWidthAudioProcessor::getCurrentProgram()
{
    return currentProgram;
}

void JuicyWidthAudioProcessor::setCurrentProgram(int index)
{
    currentProgram = juce::jlimit(0, getNumPrograms() - 1, index);
    const auto& p = widthPresets[static_cast<size_t>(currentProgram)];

    auto setParam = [this](const char* id, float value)
    {
        if (auto* param = parameters.getParameter(id))
        {
            const auto range = param->getNormalisableRange();
            param->setValueNotifyingHost(range.convertTo0to1(value));
        }
    };

    setParam("width", p.width);
    setParam("haasMs", p.haasMs);
    setParam("monoSafe", p.monoSafe);
    setParam("mix", p.mix);
    setParam("output", p.output);
}

const juce::String JuicyWidthAudioProcessor::getProgramName(int index)
{
    const int safe = juce::jlimit(0, getNumPrograms() - 1, index);
    return widthPresets[static_cast<size_t>(safe)].name;
}

void JuicyWidthAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

JuicinessMetrics JuicyWidthAudioProcessor::getLatestMetrics() const noexcept
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

juce::AudioProcessorValueTreeState::ParameterLayout JuicyWidthAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("width", "Stereo Width", 0.0f, 1.0f, 0.45f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("haasMs", "Haas Delay (ms)", 0.0f, 35.0f, 12.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("monoSafe", "Mono Safety", 0.0f, 1.0f, 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 1.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output (dB)", -18.0f, 18.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("juiciness", "Juiciness Score", 0.0f, 100.0f, 0.0f));
    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuicyWidthAudioProcessor();
}
