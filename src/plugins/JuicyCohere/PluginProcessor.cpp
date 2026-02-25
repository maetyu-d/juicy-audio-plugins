#include "PluginProcessor.h"
#include "../../shared/JuicyPluginEditor.h"

JuicyCohereAudioProcessor::JuicyCohereAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMS", createParameterLayout())
{
    juicinessParameter = parameters.getParameter("juiciness");
    contextFitParameter = parameters.getParameter("contextfit");
}

void JuicyCohereAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    analyzer.prepare(sampleRate, samplesPerBlock, getTotalNumInputChannels());
    lowCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 220.0f / static_cast<float>(sampleRate));
    highCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 2400.0f / static_cast<float>(sampleRate));
    tailL = 0.0f;
    tailR = 0.0f;
    lowLp = 0.0f;
    highLp = 0.0f;
}

void JuicyCohereAudioProcessor::releaseResources()
{
}

bool JuicyCohereAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != layouts.getMainOutputChannelSet())
        return false;
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void JuicyCohereAudioProcessor::pushJuicinessToHost(float score)
{
    if (juicinessParameter != nullptr)
        juicinessParameter->setValueNotifyingHost(juicinessParameter->getNormalisableRange().convertTo0to1(score));
}

void JuicyCohereAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int inCh = getTotalNumInputChannels();
    const int outCh = getTotalNumOutputChannels();
    for (int i = inCh; i < outCh; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const auto preMetrics = analyzer.analyze(buffer);

    const float matchAmt = *parameters.getRawParameterValue("match");
    const bool learn = *parameters.getRawParameterValue("learn") > 0.5f;
    const float tailAmt = *parameters.getRawParameterValue("tail");
    const float decay = *parameters.getRawParameterValue("decay");
    const float mix = *parameters.getRawParameterValue("mix");
    const float outDb = *parameters.getRawParameterValue("output");
    const float outGain = juce::Decibels::decibelsToGain(outDb);

    float lowEnergy = 0.0f, midEnergy = 0.0f, highEnergy = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float mono = 0.5f * (buffer.getSample(0, i) + buffer.getSample(juce::jmin(1, inCh - 1), i));
        lowLp += lowCoeff * (mono - lowLp);
        highLp += highCoeff * (mono - highLp);
        const float low = lowLp;
        const float high = mono - highLp;
        const float mid = mono - low - high;
        lowEnergy += low * low;
        midEnergy += mid * mid;
        highEnergy += high * high;
    }
    const float n = 1.0f / static_cast<float>(juce::jmax(1, buffer.getNumSamples()));
    lowEnergy *= n; midEnergy *= n; highEnergy *= n;

    if (learn)
    {
        const float a = 0.02f;
        targetLow += (lowEnergy - targetLow) * a;
        targetMid += (midEnergy - targetMid) * a;
        targetHigh += (highEnergy - targetHigh) * a;
    }

    const float lowErr = std::abs(juce::Decibels::gainToDecibels((lowEnergy + 1.0e-6f) / (targetLow + 1.0e-6f)));
    const float midErr = std::abs(juce::Decibels::gainToDecibels((midEnergy + 1.0e-6f) / (targetMid + 1.0e-6f)));
    const float highErr = std::abs(juce::Decibels::gainToDecibels((highEnergy + 1.0e-6f) / (targetHigh + 1.0e-6f)));
    const float deviation = (lowErr + midErr + highErr) / 3.0f;
    const float contextFit = juce::jlimit(0.0f, 100.0f, 100.0f - deviation * 10.0f);
    if (contextFitParameter != nullptr)
        contextFitParameter->setValueNotifyingHost(contextFitParameter->getNormalisableRange().convertTo0to1(contextFit));

    const float lowComp = juce::jlimit(0.5f, 1.8f, std::pow((targetLow + 1.0e-6f) / (lowEnergy + 1.0e-6f), 0.25f * matchAmt));
    const float midComp = juce::jlimit(0.5f, 1.8f, std::pow((targetMid + 1.0e-6f) / (midEnergy + 1.0e-6f), 0.25f * matchAmt));
    const float highComp = juce::jlimit(0.5f, 1.8f, std::pow((targetHigh + 1.0e-6f) / (highEnergy + 1.0e-6f), 0.25f * matchAmt));
    const float fb = juce::jlimit(0.0f, 0.93f, decay);

    for (int ch = 0; ch < inCh; ++ch)
    {
        auto* x = buffer.getWritePointer(ch);
        float& tail = (ch == 0 ? tailL : tailR);
        float lpA = 0.0f;
        float lpB = 0.0f;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float dry = x[i];
            lpA += lowCoeff * (dry - lpA);
            lpB += highCoeff * (dry - lpB);
            const float low = lpA * lowComp;
            const float high = (dry - lpB) * highComp;
            const float mid = (dry - lpA - (dry - lpB)) * midComp;
            const float matched = low + mid + high;

            tail = matched + tail * fb;
            const float wet = matched + tailAmt * 0.35f * tail;
            x[i] = (dry + mix * (wet - dry)) * outGain;
        }
    }

    const auto metrics = analyzer.analyze(buffer);
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

juce::AudioProcessorEditor* JuicyCohereAudioProcessor::createEditor()
{
    return new JuicyPluginEditor(*this, parameters, [this]() { return getLatestMetrics(); }, "Juicy Cohere");
}

void JuicyCohereAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void JuicyCohereAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

JuicinessMetrics JuicyCohereAudioProcessor::getLatestMetrics() const noexcept
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

juce::AudioProcessorValueTreeState::ParameterLayout JuicyCohereAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("match", "Spectral Match", 0.0f, 1.0f, 0.65f));
    p.push_back(std::make_unique<juce::AudioParameterBool>("learn", "Learn Target", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("tail", "Tail Coherence", 0.0f, 1.0f, 0.45f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("decay", "Tail Decay", 0.1f, 0.95f, 0.65f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 1.0f, 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output (dB)", -18.0f, 18.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("contextfit", "Context Fit", 0.0f, 100.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("juiciness", "Juiciness Score", 0.0f, 100.0f, 0.0f));
    return { p.begin(), p.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuicyCohereAudioProcessor();
}
