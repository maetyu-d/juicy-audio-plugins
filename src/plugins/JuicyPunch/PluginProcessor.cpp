#include "PluginProcessor.h"
#include "../../shared/JuicyPluginEditor.h"

JuicyPunchAudioProcessor::JuicyPunchAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMS", createParameterLayout())
{
    juicinessParameter = parameters.getParameter("juiciness");
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
    const float mix = *parameters.getRawParameterValue("mix");
    const float outDb = *parameters.getRawParameterValue("output");
    const float outGain = juce::Decibels::decibelsToGain(outDb);

    const float fastCoeff = std::exp(-1.0f / static_cast<float>(sr * 0.0025));
    const float slowCoeff = std::exp(-1.0f / static_cast<float>(sr * 0.080));

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
            const float punchGain = 1.0f + punchAmt * transient * 8.0f;
            const float sustainGain = 1.0f + sustainAmt * juce::jmax(0.0f, sEnv - transient) * 2.0f;
            const float wet = juce::jlimit(-1.0f, 1.0f, dry * punchGain * sustainGain);

            x[i] = (dry + mix * (wet - dry)) * outGain;
        }
    }

    auto metrics = analyzer.analyze(buffer);
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

JuicinessMetrics JuicyPunchAudioProcessor::getLatestMetrics() const noexcept
{
    JuicinessMetrics m;
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
    params.push_back(std::make_unique<juce::AudioParameterFloat>("punch", "Punch", 0.0f, 1.0f, 0.55f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sustain", "Sustain", 0.0f, 1.0f, 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 1.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output (dB)", -18.0f, 18.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("juiciness", "Juiciness Score", 0.0f, 100.0f, 0.0f));
    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuicyPunchAudioProcessor();
}
