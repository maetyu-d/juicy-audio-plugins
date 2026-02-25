#include "PluginProcessor.h"
#include "../../shared/JuicyPluginEditor.h"

JuicySaturatorAudioProcessor::JuicySaturatorAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMS", createParameterLayout())
{
    juicinessParameter = parameters.getParameter("juiciness");
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

JuicinessMetrics JuicySaturatorAudioProcessor::getLatestMetrics() const noexcept
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
