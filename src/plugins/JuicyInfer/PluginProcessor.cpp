#include "PluginProcessor.h"
#include "../../shared/JuicyPluginEditor.h"
#include <array>

namespace
{
struct InferPreset
{
    const char* name;
    float trim;
    float sensitivity;
};

constexpr std::array<InferPreset, 5> inferPresets { {
    { "Reference Lens", 0.0f, 1.0f },
    { "Detail Hunter", 0.0f, 1.45f },
    { "Macro Meter", -6.0f, 1.7f },
    { "Subtle Scout", 0.0f, 0.75f },
    { "Overdrive Audit", -9.0f, 2.0f }
} };
}

JuicyInferAudioProcessor::JuicyInferAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMS", createParameterLayout())
{
    juicinessParameter = parameters.getParameter("juiciness");
    emphasisParameter = parameters.getParameter("emphasis");
    coherenceParameter = parameters.getParameter("coherence");
    synesthesiaParameter = parameters.getParameter("synesthesia");
    fatigueParameter = parameters.getParameter("fatigue");
    repetitionParameter = parameters.getParameter("repetition");
    setCurrentProgram(0);
}

void JuicyInferAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    analyzer.prepare(sampleRate, samplesPerBlock, getTotalNumInputChannels());
}

void JuicyInferAudioProcessor::releaseResources()
{
}

bool JuicyInferAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != layouts.getMainOutputChannelSet())
        return false;
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void JuicyInferAudioProcessor::pushJuicinessToHost(float score)
{
    if (juicinessParameter == nullptr)
        return;

    const auto range = juicinessParameter->getNormalisableRange();
    const float normalized = range.convertTo0to1(score);
    juicinessParameter->setValueNotifyingHost(normalized);
}

void JuicyInferAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalInputChannels = getTotalNumInputChannels();
    const int totalOutputChannels = getTotalNumOutputChannels();
    for (int i = totalInputChannels; i < totalOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const float trimDb = *parameters.getRawParameterValue("trim");
    const float trimGain = juce::Decibels::decibelsToGain(trimDb);
    const float sensitivity = *parameters.getRawParameterValue("sensitivity");

    const auto preMetrics = analyzer.analyze(buffer);
    buffer.applyGain(trimGain);
    auto metrics = analyzer.analyze(buffer);
    metrics.score = juce::jlimit(0.0f, 100.0f, metrics.score * sensitivity);
    latestPreScore.store(preMetrics.score, std::memory_order_relaxed);
    latestPostScore.store(metrics.score, std::memory_order_relaxed);
    latestScore.store(metrics.score, std::memory_order_relaxed);
    latestPunch.store(metrics.emphasis, std::memory_order_relaxed);
    latestRichness.store(metrics.coherence, std::memory_order_relaxed);
    latestClarity.store(metrics.synesthesia, std::memory_order_relaxed);
    latestWidth.store(metrics.fatigueRisk, std::memory_order_relaxed);
    latestMonoSafety.store(metrics.repetitionDensity, std::memory_order_relaxed);

    auto setOut = [](juce::RangedAudioParameter* p, float v)
    {
        if (p != nullptr)
            p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(v));
    };
    setOut(emphasisParameter, metrics.emphasis);
    setOut(coherenceParameter, metrics.coherence);
    setOut(synesthesiaParameter, metrics.synesthesia);
    setOut(fatigueParameter, metrics.fatigueRisk);
    setOut(repetitionParameter, metrics.repetitionDensity);
    pushJuicinessToHost(metrics.score);
}

juce::AudioProcessorEditor* JuicyInferAudioProcessor::createEditor()
{
    return new JuicyPluginEditor(*this, parameters, [this]() { return getLatestMetrics(); }, "Juicy Infer", true, true);
}

void JuicyInferAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void JuicyInferAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState == nullptr)
        return;
    if (xmlState->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

int JuicyInferAudioProcessor::getNumPrograms()
{
    return static_cast<int>(inferPresets.size());
}

int JuicyInferAudioProcessor::getCurrentProgram()
{
    return currentProgram;
}

void JuicyInferAudioProcessor::setCurrentProgram(int index)
{
    currentProgram = juce::jlimit(0, getNumPrograms() - 1, index);
    const auto& p = inferPresets[static_cast<size_t>(currentProgram)];

    auto setParam = [this](const char* id, float value)
    {
        if (auto* param = parameters.getParameter(id))
        {
            const auto range = param->getNormalisableRange();
            param->setValueNotifyingHost(range.convertTo0to1(value));
        }
    };

    setParam("trim", p.trim);
    setParam("sensitivity", p.sensitivity);
}

const juce::String JuicyInferAudioProcessor::getProgramName(int index)
{
    const int safe = juce::jlimit(0, getNumPrograms() - 1, index);
    return inferPresets[static_cast<size_t>(safe)].name;
}

void JuicyInferAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

JuicinessMetrics JuicyInferAudioProcessor::getLatestMetrics() const noexcept
{
    JuicinessMetrics m;
    m.preScore = latestPreScore.load(std::memory_order_relaxed);
    m.postScore = latestPostScore.load(std::memory_order_relaxed);
    m.score = latestScore.load(std::memory_order_relaxed);
    m.emphasis = latestPunch.load(std::memory_order_relaxed);
    m.coherence = latestRichness.load(std::memory_order_relaxed);
    m.synesthesia = latestClarity.load(std::memory_order_relaxed);
    m.fatigueRisk = latestWidth.load(std::memory_order_relaxed);
    m.repetitionDensity = latestMonoSafety.load(std::memory_order_relaxed);
    m.punch = m.emphasis;
    m.richness = m.coherence;
    m.clarity = m.synesthesia;
    m.width = m.fatigueRisk;
    m.monoSafety = m.repetitionDensity;
    return m;
}

juce::AudioProcessorValueTreeState::ParameterLayout JuicyInferAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("trim", "Output Trim (dB)", -18.0f, 18.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sensitivity", "Sensitivity", 0.5f, 2.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("juiciness", "Juiciness Score", 0.0f, 100.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("emphasis", "Emphasis", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("coherence", "Coherence", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("synesthesia", "Synesthesia", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("fatigue", "Fatigue Risk", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("repetition", "Repetition Density", 0.0f, 1.0f, 0.0f));
    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuicyInferAudioProcessor();
}
