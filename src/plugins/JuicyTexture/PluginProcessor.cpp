#include "PluginProcessor.h"
#include "../../shared/JuicyPluginEditor.h"

JuicyTextureAudioProcessor::JuicyTextureAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMS", createParameterLayout())
{
    juicinessParameter = parameters.getParameter("juiciness");
}

void JuicyTextureAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    analyzer.prepare(sampleRate, samplesPerBlock, getTotalNumInputChannels());
    rng = 0x12345678u;

    const int maxDelay = juce::jmax(2048, static_cast<int>(sr * 0.08));
    for (auto& ch : channels)
    {
        ch = {};
        ch.waveguide.assign(static_cast<size_t>(maxDelay), 0.0f);
        ch.waveIdx = 0;
    }
}

void JuicyTextureAudioProcessor::releaseResources() {}

bool JuicyTextureAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != layouts.getMainOutputChannelSet())
        return false;
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void JuicyTextureAudioProcessor::pushJuicinessToHost(float score)
{
    if (juicinessParameter != nullptr)
        juicinessParameter->setValueNotifyingHost(juicinessParameter->getNormalisableRange().convertTo0to1(score));
}

void JuicyTextureAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int inCh = getTotalNumInputChannels();
    const int outCh = getTotalNumOutputChannels();
    for (int i = inCh; i < outCh; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const auto preMetrics = analyzer.analyze(buffer);

    const int mode = static_cast<int>(*parameters.getRawParameterValue("material"));
    const float tailShape = *parameters.getRawParameterValue("tailshape");
    const float damping = *parameters.getRawParameterValue("damping");
    const float weight = *parameters.getRawParameterValue("weight");
    const float texture = *parameters.getRawParameterValue("texture");
    const float mix = *parameters.getRawParameterValue("mix");
    const float outDb = *parameters.getRawParameterValue("output");
    const float outGain = juce::Decibels::decibelsToGain(outDb);

    const float dampingAmt = juce::jlimit(0.0f, 1.0f, damping);
    const float dampingMul = juce::jmap(dampingAmt, 0.0f, 1.0f, 1.35f, 0.40f); // lower values ring longer
    const float decay = juce::jmap(tailShape, 0.0f, 1.0f, 0.30f, 0.985f) * juce::jmap(dampingAmt, 0.0f, 1.0f, 1.0f, 0.80f);
    const float lowBoost = 1.0f + weight * 1.0f;
    const float splitLowCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 140.0f / static_cast<float>(sr));
    const float splitHighCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 2600.0f / static_cast<float>(sr));
    const float envAtk = std::exp(-1.0f / static_cast<float>(sr * 0.0025));
    const float envRel = std::exp(-1.0f / static_cast<float>(sr * 0.080));
    const float wetEnvAttack = std::exp(-1.0f / static_cast<float>(sr * 0.005));
    const float wetEnvRelease = std::exp(-1.0f / static_cast<float>(sr * 0.090));
    const float dcR = 0.995f;
    const float autoGainBase = juce::jmap(texture, 0.0f, 1.0f, 0.78f, 0.54f);

    const auto modeStep = [this](ChannelState& st, int modeIdx, float excitation, float freqHz, float t60, float gain) -> float
    {
        const float f = juce::jlimit(20.0f, 0.45f * static_cast<float>(sr), freqHz);
        const float t = juce::jmax(0.02f, t60);
        const float r = std::exp(std::log(0.001f) / (t * static_cast<float>(sr)));
        const float theta = 2.0f * juce::MathConstants<float>::pi * f / static_cast<float>(sr);
        const float a1 = 2.0f * r * std::cos(theta);
        const float a2 = -r * r;
        const float y = excitation * gain + a1 * st.modalY1[static_cast<size_t>(modeIdx)] + a2 * st.modalY2[static_cast<size_t>(modeIdx)];
        st.modalY2[static_cast<size_t>(modeIdx)] = st.modalY1[static_cast<size_t>(modeIdx)];
        st.modalY1[static_cast<size_t>(modeIdx)] = y;
        return y;
    };

    const auto waveguideRead = [](const std::vector<float>& line, int writeIdx, float delaySamples) -> float
    {
        const int size = static_cast<int>(line.size());
        if (size <= 1)
            return 0.0f;
        float pos = static_cast<float>(writeIdx) - delaySamples;
        while (pos < 0.0f)
            pos += static_cast<float>(size);
        while (pos >= static_cast<float>(size))
            pos -= static_cast<float>(size);
        const int i0 = static_cast<int>(pos);
        const int i1 = (i0 + 1) % size;
        const float frac = pos - static_cast<float>(i0);
        return juce::jmap(frac, line[static_cast<size_t>(i0)], line[static_cast<size_t>(i1)]);
    };

    for (int ch = 0; ch < inCh; ++ch)
    {
        auto* x = buffer.getWritePointer(ch);
        auto& st = channels[static_cast<size_t>(juce::jlimit(0, 1, ch))];
        if (st.waveguide.empty())
            st.waveguide.assign(2048, 0.0f);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float dry = x[i];
            const float materialInputTrim = (mode == 1 ? 0.58f : (mode == 2 ? 0.62f : (mode == 3 ? 0.60f : 1.0f)));
            const float driven = dry * materialInputTrim;
            const float adry = std::abs(dry);
            const float envCoeff = adry > st.env ? envAtk : envRel;
            st.env = envCoeff * st.env + (1.0f - envCoeff) * adry;
            const float impact = juce::jlimit(0.0f, 1.0f, juce::jmax(0.0f, adry - st.env) * 10.0f);
            const float body = juce::jlimit(0.0f, 1.0f, st.env * 3.2f);
            const float trail = juce::jlimit(0.0f, 1.0f, 1.0f - impact) * tailShape;

            st.lp += splitLowCoeff * (driven - st.lp);
            st.hp += splitHighCoeff * (driven - st.hp);
            const float low = st.lp * lowBoost;
            const float high = (driven - st.hp);
            const float mid = driven - st.lp - high;
            float core = low + mid + high * (0.9f + texture * 1.3f);

            float shaped = core;
            float materialTrim = 1.0f;
            switch (mode)
            {
                case 0: // Gel: viscoelastic blob (mass-spring-damper)
                {
                    const float f0 = 42.0f + texture * 88.0f;
                    const float omega = 2.0f * juce::MathConstants<float>::pi * f0 / static_cast<float>(sr);
                    const float k = omega * omega;
                    const float zeta = juce::jmap(trail, 0.62f, 1.45f);
                    const float c = 2.0f * zeta * omega;
                    const float force = core * (0.52f + 0.62f * body);
                    const float acc = k * (force - st.springPos) - c * st.springVel;
                    st.springVel += acc;
                    st.springPos += st.springVel;
                    shaped = 0.48f * core + 1.85f * st.springPos;
                    shaped = std::tanh(shaped * (0.96f + 0.28f * texture));
                    break;
                }
                case 1: // Metal: inharmonic modal plate
                {
                    const float exc = core * (0.19f + 0.52f * impact);
                    const float f0 = 320.0f + 140.0f * texture;
                    const float bend = 1.0f + 0.09f * impact;
                    const float metalDamp = juce::jmap(dampingAmt, 0.0f, 1.0f, 1.0f, 0.55f);
                    const float tScale = juce::jmap(tailShape, 0.18f, 0.72f) * dampingMul * metalDamp;
                    // Approximate thin plate inharmonic modes.
                    const float m0 = modeStep(st, 0, exc, f0 * 1.00f * bend, 0.56f * tScale, 0.34f);
                    const float m1 = modeStep(st, 1, exc, f0 * 2.31f * bend, 0.40f * tScale, 0.20f);
                    const float m2 = modeStep(st, 2, exc, f0 * 4.18f * bend, 0.26f * tScale, 0.13f);
                    const float m3 = modeStep(st, 3, exc, f0 * 6.87f * bend, 0.17f * tScale, 0.09f);
                    const float modes = m0 + m1 + m2 + m3;
                    const float brightExcite = 0.03f * impact * (core - st.hp);
                    shaped = (0.44f * core + 0.42f * modes + brightExcite) * (0.78f + 0.10f * texture);
                    materialTrim = 0.62f;
                    break;
                }
                case 2: // Wood: cavity + modal body resonance
                {
                    const float exc = core * (0.10f + 0.34f * impact);
                    const float cavityHz = 92.0f + 95.0f * (0.5f * weight + 0.5f * texture);
                    const float delaySamp = juce::jlimit(16.0f, static_cast<float>(st.waveguide.size() - 2), static_cast<float>(sr) / cavityHz);
                    const float delayed = waveguideRead(st.waveguide, st.waveIdx, delaySamp);
                    const float damp = juce::jmap(tailShape, 0.26f, 0.90f) * juce::jmap(dampingAmt, 0.0f, 1.0f, 1.0f, 0.72f);
                    const float newWave = damp * (0.62f * delayed + 0.38f * st.prevWave) + exc * (0.09f + 0.04f * body);
                    st.waveguide[static_cast<size_t>(st.waveIdx)] = newWave;
                    st.waveIdx = (st.waveIdx + 1) % static_cast<int>(st.waveguide.size());
                    st.prevWave = delayed;

                    const float woodDamp = juce::jmap(dampingAmt, 0.0f, 1.0f, 1.0f, 0.64f);
                    const float tScale = juce::jmap(tailShape, 0.18f, 0.62f) * dampingMul * woodDamp;
                    // Typical wooden body: strong low/mid modes, shorter high-mode tails.
                    const float w0 = modeStep(st, 0, exc, 155.0f, 0.40f * tScale, 0.32f);
                    const float w1 = modeStep(st, 1, exc, 355.0f, 0.27f * tScale, 0.18f);
                    const float w2 = modeStep(st, 2, exc, 690.0f, 0.16f * tScale, 0.10f);
                    const float w3 = modeStep(st, 3, exc, 1130.0f, 0.10f * tScale, 0.06f);
                    shaped = (0.56f * core + 0.24f * delayed + 0.30f * (w0 + w1 + w2 + w3)) * (0.74f + 0.08f * texture);
                    materialTrim = 0.54f;
                    break;
                }
                case 3: // Plastic: stiff shell with short cavity resonance
                {
                    const float exc = core * (0.20f + 0.60f * impact);
                    const float tubeHz = 210.0f + 340.0f * texture;
                    const float delaySamp = juce::jlimit(8.0f, static_cast<float>(st.waveguide.size() - 2), static_cast<float>(sr) / tubeHz);
                    const float delayed = waveguideRead(st.waveguide, st.waveIdx, delaySamp);
                    const float damp = juce::jmap(tailShape, 0.22f, 0.91f) * juce::jmap(dampingAmt, 0.0f, 1.0f, 1.0f, 0.82f);
                    const float newWave = damp * (0.76f * delayed + 0.24f * st.prevWave) + 0.14f * exc;
                    st.waveguide[static_cast<size_t>(st.waveIdx)] = newWave;
                    st.waveIdx = (st.waveIdx + 1) % static_cast<int>(st.waveguide.size());
                    st.prevWave = delayed;

                    const float tScale = juce::jmap(tailShape, 0.16f, 0.72f) * dampingMul;
                    const float p0 = modeStep(st, 0, exc, 280.0f, 0.28f * tScale, 0.34f);
                    const float p1 = modeStep(st, 1, exc, 690.0f, 0.18f * tScale, 0.22f);
                    const float p2 = modeStep(st, 2, exc, 1320.0f, 0.11f * tScale, 0.16f);
                    const float p3 = modeStep(st, 3, exc, 2360.0f, 0.07f * tScale, 0.11f);
                    shaped = (0.52f * core + 0.36f * delayed + 0.40f * (p0 + p1 + p2 + p3)) * (0.80f + 0.10f * texture);
                    materialTrim = 0.62f;
                    break;
                }
                default: // Flesh-like: coupled compliant masses
                {
                    const float force = core * (0.55f + 0.65f * body);
                    const float wA = 2.0f * juce::MathConstants<float>::pi * (38.0f + 52.0f * texture) / static_cast<float>(sr);
                    const float wB = 2.0f * juce::MathConstants<float>::pi * (88.0f + 72.0f * texture) / static_cast<float>(sr);
                    const float kA = wA * wA;
                    const float kB = wB * wB;
                    const float cA = 2.0f * juce::jmap(tailShape, 0.56f, 1.18f) * wA;
                    const float cB = 2.0f * juce::jmap(tailShape, 0.70f, 1.34f) * wB;
                    const float kCouple = 0.14f + 0.24f * texture;

                    const float accA = kA * (force - st.fleshPosA) - cA * st.fleshVelA - kCouple * (st.fleshPosA - st.fleshPosB);
                    const float accB = kB * (st.fleshPosA - st.fleshPosB) - cB * st.fleshVelB;
                    st.fleshVelA += accA;
                    st.fleshVelB += accB;
                    st.fleshPosA += st.fleshVelA;
                    st.fleshPosB += st.fleshVelB;

                    const float tissue = 0.92f * st.fleshPosA + 0.58f * st.fleshPosB;
                    const float nl = tissue - 0.19f * tissue * tissue * tissue;
                    shaped = std::tanh((0.50f * core + 1.34f * nl) * (0.98f + 0.16f * texture));
                    break;
                }
            }

            rng = 1664525u * rng + 1013904223u;
            const float white = (static_cast<float>((rng >> 8) & 0xFFFF) / 32768.0f - 1.0f);
            st.noiseHp += 0.08f * (white - st.noiseHp);
            const float rough = white - st.noiseHp;
            shaped += rough * (0.004f + 0.022f * texture) * (0.14f + 0.64f * impact);

            const float dynamics = 1.0f + impact * (0.18f + texture * 0.12f) + body * 0.06f;
            shaped *= dynamics * materialTrim;

            const float tailInput = juce::jlimit(-2.0f, 2.0f, shaped) * (0.45f + 0.55f * trail);
            st.tail = tailInput + st.tail * decay;
            float wet = shaped + st.tail * (0.30f + 0.45f * trail);

            // Keep modeled materials level-stable as resonance rises.
            const float wetAbs = std::abs(wet);
            const float wetCoeff = wetAbs > st.wetEnv ? wetEnvAttack : wetEnvRelease;
            st.wetEnv = wetCoeff * st.wetEnv + (1.0f - wetCoeff) * wetAbs;
            const float autoComp = autoGainBase / (1.0f + 1.8f * st.wetEnv);
            wet *= juce::jlimit(0.18f, 1.0f, autoComp);

            float mixed = dry + mix * (wet - dry);
            float out = mixed * outGain;

            // Remove DC that can accumulate in nonlinear physical models.
            const float dcBlocked = out - st.dcIn + dcR * st.dcOut;
            st.dcIn = out;
            st.dcOut = dcBlocked;

            // Transparent peak protection: prevent hard clipping when material engages.
            const float peak = std::abs(dcBlocked);
            const float ceiling = 0.88f;
            if (peak > ceiling)
                st.protectGain = juce::jmin(st.protectGain, (ceiling / peak) * 0.98f);
            else
                st.protectGain += (1.0f - st.protectGain) * 0.0028f;

            out = dcBlocked * juce::jlimit(0.2f, 1.0f, st.protectGain);
            x[i] = juce::jlimit(-0.98f, 0.98f, out);
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

juce::AudioProcessorEditor* JuicyTextureAudioProcessor::createEditor()
{
    return new JuicyPluginEditor(*this, parameters, [this]() { return getLatestMetrics(); }, "Juicy Texture");
}

void JuicyTextureAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void JuicyTextureAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

JuicinessMetrics JuicyTextureAudioProcessor::getLatestMetrics() const noexcept
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

juce::AudioProcessorValueTreeState::ParameterLayout JuicyTextureAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterChoice>("material", "Material", juce::StringArray { "Gel", "Metal", "Wood", "Plastic", "Flesh-like" }, 0));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("tailshape", "Tail Shape", 0.0f, 1.0f, 0.55f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("damping", "Damping", 0.0f, 1.0f, 0.5f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("weight", "Low-end Weight", 0.0f, 1.0f, 0.45f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("texture", "Texture Layer", 0.0f, 1.0f, 0.5f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 1.0f, 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output (dB)", -18.0f, 18.0f, -2.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("juiciness", "Juiciness Score", 0.0f, 100.0f, 0.0f));
    return { p.begin(), p.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuicyTextureAudioProcessor();
}
