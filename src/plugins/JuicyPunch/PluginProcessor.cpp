#include "PluginProcessor.h"
#include "../../shared/JuicyPluginEditor.h"
#include <array>

namespace
{
struct PunchPreset
{
    const char* name;
    float punch;
    float sustain;
    float slam;
    float clip;
    float mix;
    float output;
};

constexpr std::array<PunchPreset, 5> punchPresets { {
    { "Solar Snap", 0.9f, 0.35f, 0.65f, 0.25f, 1.0f, -4.0f },
    { "Crater Impact", 1.4f, 0.2f, 0.95f, 0.65f, 1.0f, -8.0f },
    { "Elastic Slam", 1.1f, 0.8f, 0.8f, 0.4f, 0.85f, -6.0f },
    { "Steel Bounce", 0.7f, 0.55f, 0.45f, 0.1f, 0.75f, -2.0f },
    { "Apocalypse Tap", 1.5f, 1.1f, 1.0f, 1.0f, 1.0f, -12.0f }
} };
}

JuicyPunchAudioProcessor::JuicyPunchAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMS", createParameterLayout())
{
    juicinessParameter = parameters.getParameter("juiciness");
    setCurrentProgram(0);
}

void JuicyPunchAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    analyzer.prepare(sampleRate, samplesPerBlock, getTotalNumInputChannels());
    fastEnv.assign(static_cast<size_t>(juce::jmax(1, getTotalNumOutputChannels())), 0.0f);
    slowEnv.assign(static_cast<size_t>(juce::jmax(1, getTotalNumOutputChannels())), 0.0f);
}

void JuicyPunchAudioProcessor::releaseResources()
{
}

bool JuicyPunchAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != layouts.getMainOutputChannelSet())
        return false;
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void JuicyPunchAudioProcessor::pushJuicinessToHost(float score)
{
    if (juicinessParameter == nullptr)
        return;
    const auto range = juicinessParameter->getNormalisableRange();
    juicinessParameter->setValueNotifyingHost(range.convertTo0to1(score));
}

void JuicyPunchAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalInputChannels = getTotalNumInputChannels();
    const int totalOutputChannels = getTotalNumOutputChannels();
    for (int i = totalInputChannels; i < totalOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const float punchAmt = *parameters.getRawParameterValue("punch");
    const float sustainAmt = *parameters.getRawParameterValue("sustain");
    const float slamAmt = *parameters.getRawParameterValue("slam");
    const float clipAmt = *parameters.getRawParameterValue("clip");
    const float mix = *parameters.getRawParameterValue("mix");
    const float outDb = *parameters.getRawParameterValue("output");
    const float outGain = juce::Decibels::decibelsToGain(outDb);

    const auto preMetrics = analyzer.analyze(buffer);
    const float fastCoeff = std::exp(-1.0f / static_cast<float>(sr * 0.0015));
    const float slowCoeff = std::exp(-1.0f / static_cast<float>(sr * 0.110));

    for (int ch = 0; ch < totalInputChannels; ++ch)
    {
        auto* x = buffer.getWritePointer(ch);
        float& fEnv = fastEnv[static_cast<size_t>(ch)];
        float& sEnv = slowEnv[static_cast<size_t>(ch)];

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float dry = x[i];
            const float adry = std::abs(dry);
            fEnv = (1.0f - fastCoeff) * adry + fastCoeff * fEnv;
            sEnv = (1.0f - slowCoeff) * adry + slowCoeff * sEnv;

            const float transient = juce::jmax(0.0f, fEnv - sEnv);
            const float transientCurve = std::pow(transient, juce::jmap(slamAmt, 0.0f, 1.0f, 0.95f, 0.55f));
            const float punchGain = 1.0f + (punchAmt * 12.0f + slamAmt * 22.0f) * transientCurve;
            const float sustainGain = 1.0f + (sustainAmt * 4.0f + slamAmt * 1.5f) * juce::jmax(0.0f, sEnv - transient * 0.6f);

            float wet = dry * punchGain * sustainGain;
            const float drive = 1.0f + clipAmt * 8.0f + slamAmt * 4.0f;
            const float soft = std::tanh(wet * drive) / std::tanh(drive);
            const float hard = juce::jlimit(-0.95f, 0.95f, wet * (1.0f + clipAmt * 2.0f));
            wet = soft + clipAmt * (hard - soft);

            x[i] = (dry + mix * (wet - dry)) * outGain;
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

juce::AudioProcessorEditor* JuicyPunchAudioProcessor::createEditor()
{
    return new JuicyPluginEditor(*this, parameters, [this]() { return getLatestMetrics(); }, "Juicy Punch");
}

void JuicyPunchAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void JuicyPunchAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState == nullptr)
        return;
    if (xmlState->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

int JuicyPunchAudioProcessor::getNumPrograms()
{
    return static_cast<int>(punchPresets.size());
}

int JuicyPunchAudioProcessor::getCurrentProgram()
{
    return currentProgram;
}

void JuicyPunchAudioProcessor::setCurrentProgram(int index)
{
    currentProgram = juce::jlimit(0, getNumPrograms() - 1, index);
    const auto& p = punchPresets[static_cast<size_t>(currentProgram)];

    auto setParam = [this](const char* id, float value)
    {
        if (auto* param = parameters.getParameter(id))
        {
            const auto range = param->getNormalisableRange();
            param->setValueNotifyingHost(range.convertTo0to1(value));
        }
    };

    setParam("punch", p.punch);
    setParam("sustain", p.sustain);
    setParam("slam", p.slam);
    setParam("clip", p.clip);
    setParam("mix", p.mix);
    setParam("output", p.output);
}

const juce::String JuicyPunchAudioProcessor::getProgramName(int index)
{
    const int safe = juce::jlimit(0, getNumPrograms() - 1, index);
    return punchPresets[static_cast<size_t>(safe)].name;
}

void JuicyPunchAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

JuicinessMetrics JuicyPunchAudioProcessor::getLatestMetrics() const noexcept
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

juce::AudioProcessorValueTreeState::ParameterLayout JuicyPunchAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("punch", "Punch", 0.0f, 1.5f, 0.9f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sustain", "Sustain", 0.0f, 1.5f, 0.35f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("slam", "Slam", 0.0f, 1.0f, 0.65f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("clip", "Clip", 0.0f, 1.0f, 0.25f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 1.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output (dB)", -24.0f, 18.0f, -4.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("juiciness", "Juiciness Score", 0.0f, 100.0f, 0.0f));
    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuicyPunchAudioProcessor();
}
