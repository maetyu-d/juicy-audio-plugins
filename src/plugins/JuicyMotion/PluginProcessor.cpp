#include "PluginProcessor.h"
#include "../../shared/JuicyPluginEditor.h"

JuicyMotionAudioProcessor::JuicyMotionAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMS", createParameterLayout())
{
    juicinessParameter = parameters.getParameter("juiciness");
}

void JuicyMotionAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    analyzer.prepare(sampleRate, samplesPerBlock, getTotalNumInputChannels());
    env = 0.0f;
    repetition = 0.0f;
    budgetEnv = 0.0f;
    onsetCooldown = 0;
    tailL = 0.0f;
    tailR = 0.0f;
    lpL = 0.0f;
    lpR = 0.0f;
    prevL = 0.0f;
    prevR = 0.0f;
    variationTone = variationTransient = variationTail = 0.0f;
    variationToneTarget = variationTransientTarget = variationTailTarget = 0.0f;
    motionPhase = 0.0f;
}

void JuicyMotionAudioProcessor::releaseResources() {}

bool JuicyMotionAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != layouts.getMainOutputChannelSet())
        return false;
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void JuicyMotionAudioProcessor::pushJuicinessToHost(float score)
{
    if (juicinessParameter != nullptr)
        juicinessParameter->setValueNotifyingHost(juicinessParameter->getNormalisableRange().convertTo0to1(score));
}

void JuicyMotionAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int inCh = getTotalNumInputChannels();
    const int outCh = getTotalNumOutputChannels();
    for (int i = inCh; i < outCh; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const auto preMetrics = analyzer.analyze(buffer);

    const float microVar = *parameters.getRawParameterValue("microvar");
    const float motionDepth = *parameters.getRawParameterValue("motiondepth");
    const float repeatCtrl = *parameters.getRawParameterValue("repeatctrl");
    const float contrastBudget = *parameters.getRawParameterValue("budget");
    const float mix = *parameters.getRawParameterValue("mix");
    const float outDb = *parameters.getRawParameterValue("output");
    const float outGain = juce::Decibels::decibelsToGain(outDb);

    const float envCoeff = std::exp(-1.0f / static_cast<float>(sr * 0.015));
    const float budgetCoeff = std::exp(-1.0f / static_cast<float>(sr * 0.080));
    const float tailFeedback = juce::jmap(repeatCtrl, 0.0f, 1.0f, 0.15f, 0.88f);
    const float depth = juce::jlimit(0.0f, 2.0f, motionDepth);
    const float motionRateHz = juce::jmap(microVar, 0.0f, 1.0f, 0.25f, 2.0f) * juce::jmap(depth, 0.0f, 2.0f, 0.75f, 1.6f);
    const float motionInc = (2.0f * juce::MathConstants<float>::pi * motionRateHz) / static_cast<float>(sr);
    const float varSlew = std::exp(-1.0f / static_cast<float>(sr * 0.020));

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float mono = 0.5f * (buffer.getSample(0, i) + buffer.getSample(juce::jmin(1, inCh - 1), i));
        const float absMono = std::abs(mono);
        env = envCoeff * env + (1.0f - envCoeff) * absMono;

        if (onsetCooldown > 0)
            --onsetCooldown;
        if (absMono > env * 1.35f + 0.02f && onsetCooldown <= 0)
        {
            onsetCooldown = static_cast<int>(sr * 0.04);
            repetition += 1.0f;
            rng = 1664525u * rng + 1013904223u;
            variationToneTarget = ((static_cast<float>((rng >> 7) & 0x7FFF) / 16384.0f) - 1.0f) * microVar * 0.9f;
            rng = 1664525u * rng + 1013904223u;
            variationTransientTarget = ((static_cast<float>((rng >> 9) & 0x7FFF) / 16384.0f) - 1.0f) * microVar * 0.8f;
            rng = 1664525u * rng + 1013904223u;
            variationTailTarget = ((static_cast<float>((rng >> 11) & 0x7FFF) / 16384.0f) - 1.0f) * microVar * 0.8f;
        }
        repetition *= 0.997f;
    }

    const float repNorm = juce::jlimit(0.0f, 1.0f, repetition * 0.08f);
    const float repetitionScale = 1.0f - repeatCtrl * repNorm * 0.65f;
    const float recovery = 1.0f + repeatCtrl * (1.0f - repNorm) * 0.25f;

    for (int ch = 0; ch < inCh; ++ch)
    {
        auto* x = buffer.getWritePointer(ch);
        float& tail = (ch == 0 ? tailL : tailR);
        float& lp = (ch == 0 ? lpL : lpR);
        float& prev = (ch == 0 ? prevL : prevR);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            variationTone = varSlew * variationTone + (1.0f - varSlew) * variationToneTarget;
            variationTransient = varSlew * variationTransient + (1.0f - varSlew) * variationTransientTarget;
            variationTail = varSlew * variationTail + (1.0f - varSlew) * variationTailTarget;
            motionPhase += motionInc;
            if (motionPhase > 2.0f * juce::MathConstants<float>::pi)
                motionPhase -= 2.0f * juce::MathConstants<float>::twoPi;

            const float dry = x[i];
            const float motionLfo = std::sin(motionPhase + (ch == 0 ? 0.0f : 0.85f));
            const float motionLfoDepth = (250.0f + 550.0f * microVar) * (0.5f + 0.9f * depth);
            const float cutoff = juce::jlimit(120.0f, 4200.0f, 900.0f + variationTone * 1100.0f * (0.6f + 0.6f * depth) + motionLfo * motionLfoDepth);
            const float lpCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * cutoff / static_cast<float>(sr));
            lp += lpCoeff * (dry - lp);
            const float hp = dry - lp;
            const float transient = dry - prev;
            prev = dry;

            const float transientBoost = 1.0f + variationTransient * 1.2f * (0.6f + 0.7f * depth) + 0.35f * microVar * motionLfo * (0.6f + 0.8f * depth);
            const float toneShift = lp * (1.0f + variationTone * 0.65f * (0.55f + 0.7f * depth))
                + hp * transientBoost
                + transient * (0.12f + 0.30f * microVar) * (0.5f + 0.8f * depth);
            tail = toneShift + tail * juce::jlimit(0.0f, 0.93f, tailFeedback + variationTail * 0.06f);

            float wet = toneShift * repetitionScale * recovery + (0.26f + 0.24f * microVar) * (0.6f + 0.7f * depth) * tail;
            budgetEnv = budgetCoeff * budgetEnv + (1.0f - budgetCoeff) * std::abs(wet);
            const float budgetTarget = juce::jmap(contrastBudget, 0.0f, 1.0f, 0.8f, 0.25f);
            const float limiterGain = budgetEnv > budgetTarget ? budgetTarget / (budgetEnv + 1.0e-5f) : 1.0f;
            wet *= limiterGain;

            const float wetBoost = 1.0f + 0.9f * microVar * (0.55f + 0.9f * depth);
            x[i] = (dry + mix * (wet * wetBoost - dry)) * outGain;
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

juce::AudioProcessorEditor* JuicyMotionAudioProcessor::createEditor()
{
    return new JuicyPluginEditor(*this, parameters, [this]() { return getLatestMetrics(); }, "Juicy Motion");
}

void JuicyMotionAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void JuicyMotionAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

JuicinessMetrics JuicyMotionAudioProcessor::getLatestMetrics() const noexcept
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

juce::AudioProcessorValueTreeState::ParameterLayout JuicyMotionAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("microvar", "Micro Variation", 0.0f, 1.0f, 0.55f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("motiondepth", "Motion Depth", 0.0f, 2.0f, 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("repeatctrl", "Repetition Control", 0.0f, 1.0f, 0.65f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("budget", "Contrast Budget", 0.0f, 1.0f, 0.5f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 1.0f, 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output (dB)", -18.0f, 18.0f, -2.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("juiciness", "Juiciness Score", 0.0f, 100.0f, 0.0f));
    return { p.begin(), p.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuicyMotionAudioProcessor();
}
