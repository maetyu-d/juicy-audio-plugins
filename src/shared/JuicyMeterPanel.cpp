#include "JuicyMeterPanel.h"

float JuicyMeterPanel::smoothValue(float current, float target)
{
    const float alpha = target > current ? 0.28f : 0.12f;
    return current + (target - current) * alpha;
}

void JuicyMeterPanel::setMetrics(const JuicinessMetrics& newMetrics)
{
    const float newPre = newMetrics.preScore > 0.0f ? newMetrics.preScore : newMetrics.score;
    const float newPost = newMetrics.postScore > 0.0f ? newMetrics.postScore : newMetrics.score;

    metrics.preScore = smoothValue(metrics.preScore, newPre);
    metrics.postScore = smoothValue(metrics.postScore, newPost);
    updateStats(punchStats, newMetrics.punch);
    updateStats(richnessStats, newMetrics.richness);
    updateStats(clarityStats, newMetrics.clarity);
    updateStats(widthStats, newMetrics.width);
    updateStats(monoSafetyStats, newMetrics.monoSafety);
    updateStats(emphasisStats, newMetrics.emphasis);
    updateStats(coherenceStats, newMetrics.coherence);
    updateStats(synesthesiaStats, newMetrics.synesthesia);
    updateStats(fatigueStats, newMetrics.fatigueRisk);
    updateStats(repetitionStats, newMetrics.repetitionDensity);

    metrics.score = smoothValue(metrics.score, newPost);
    metrics.punch = smoothValue(metrics.punch, newMetrics.punch);
    metrics.richness = smoothValue(metrics.richness, newMetrics.richness);
    metrics.clarity = smoothValue(metrics.clarity, newMetrics.clarity);
    metrics.width = smoothValue(metrics.width, newMetrics.width);
    metrics.monoSafety = smoothValue(metrics.monoSafety, newMetrics.monoSafety);
    repaint();
}

void JuicyMeterPanel::setAccentColour(juce::Colour colour)
{
    accent = colour;
    repaint();
}

void JuicyMeterPanel::setShowGhostStats(bool shouldShow)
{
    showGhostStats = shouldShow;
    repaint();
}

void JuicyMeterPanel::setShowTriangleMetrics(bool shouldShow)
{
    showTriangleMetrics = shouldShow;
    repaint();
}

void JuicyMeterPanel::updateStats(MetricStats& stats, float value)
{
    const float v = juce::jlimit(0.0f, 1.0f, value);
    if (stats.count == 0)
    {
        stats.min = v;
        stats.max = v;
        stats.avg = v;
        stats.count = 1;
        return;
    }

    stats.min = juce::jmin(stats.min, v);
    stats.max = juce::jmax(stats.max, v);
    ++stats.count;
    const float n = static_cast<float>(stats.count);
    stats.avg += (v - stats.avg) / n;
}

void JuicyMeterPanel::drawBar(juce::Graphics& g,
                              juce::Rectangle<int> area,
                              const juce::String& name,
                              float value,
                              juce::Colour colour,
                              const MetricStats& stats)
{
    auto bg = area.reduced(0, 4);
    g.setColour(juce::Colour(0xff171c22));
    g.fillRect(bg);
    g.setColour(juce::Colour(0xff2a313a));
    g.drawRect(bg, 1);

    if (showGhostStats && stats.count > 2)
    {
        const int minX = bg.getX() + static_cast<int>(std::round(stats.min * bg.getWidth()));
        const int maxX = bg.getX() + static_cast<int>(std::round(stats.max * bg.getWidth()));
        const int avgX = bg.getX() + static_cast<int>(std::round(stats.avg * bg.getWidth()));

        auto ghost = juce::Rectangle<int>(juce::jmin(minX, maxX), bg.getY() + 2, juce::jmax(2, std::abs(maxX - minX)), bg.getHeight() - 4);
        g.setColour(juce::Colour(0xffcad4df).withAlpha(0.12f));
        g.fillRect(ghost);

        g.setColour(juce::Colour(0xffe4ebf2).withAlpha(0.46f));
        g.drawVerticalLine(avgX, static_cast<float>(bg.getY() + 2), static_cast<float>(bg.getBottom() - 2));
    }

    const int fillWidth = static_cast<int>(std::round(value * static_cast<float>(bg.getWidth())));
    auto fillArea = bg.withWidth(juce::jlimit(0, bg.getWidth(), fillWidth));
    g.setColour(colour.interpolatedWith(accent, 0.32f).withMultipliedSaturation(0.72f));
    g.fillRect(fillArea);

    auto labelArea = bg.removeFromLeft(146);
    g.setColour(juce::Colour(0xffd8dee7));
    g.setFont(juce::FontOptions(12.0f, juce::Font::plain));
    g.drawText(name, labelArea.reduced(10, 0), juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xffe8edf2));
    g.drawText(juce::String(value * 100.0f, 1) + "%", bg, juce::Justification::centredRight);
}

void JuicyMeterPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.setColour(juce::Colour(0xff12161b));
    g.fillRect(bounds);
    g.setColour(juce::Colour(0xff2a323b));
    g.drawRect(bounds, 1);

    auto top = bounds.removeFromTop(108);
    const float preNorm = juce::jlimit(0.0f, 1.0f, metrics.preScore / 100.0f);
    const float postNorm = juce::jlimit(0.0f, 1.0f, metrics.postScore / 100.0f);

    g.setColour(juce::Colour(0xffe4e9ef));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText("JUICINESS INDEX", top.removeFromTop(26), juce::Justification::centredLeft);
    auto metersRow = top.removeFromTop(70).reduced(0, 4);
    auto leftMeter = metersRow.removeFromLeft((metersRow.getWidth() - 10) / 2);
    metersRow.removeFromLeft(10);
    auto rightMeter = metersRow;

    auto drawScoreMeter = [&](juce::Rectangle<int> meterBox, const juce::String& label, float norm, float score, juce::Colour colour)
    {
        g.setColour(juce::Colour(0xff161c23));
        g.fillRect(meterBox);
        g.setColour(juce::Colour(0xff2f3843));
        g.drawRect(meterBox, 1);

        auto barArea = meterBox.reduced(10, 24);
        g.setColour(juce::Colour(0xff11161c));
        g.fillRect(barArea);
        g.setColour(colour.interpolatedWith(accent, 0.3f).withMultipliedSaturation(0.75f));
        g.fillRect(barArea.withWidth(static_cast<int>(std::round(norm * static_cast<float>(barArea.getWidth())))));

        g.setColour(juce::Colour(0xffdfe5ec).withAlpha(0.08f));
        for (int i = 1; i < 5; ++i)
        {
            const int x = barArea.getX() + (barArea.getWidth() * i) / 5;
            g.drawVerticalLine(x, static_cast<float>(barArea.getY() + 2), static_cast<float>(barArea.getBottom() - 2));
        }

        g.setColour(juce::Colour(0xffc9d1db));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(label, meterBox.removeFromTop(15).reduced(8, 0), juce::Justification::centredLeft);
        g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xffedf1f6));
        g.drawText(juce::String(score, 1), meterBox.removeFromBottom(20), juce::Justification::centredRight);
    };

    drawScoreMeter(leftMeter, "PRE", preNorm, metrics.preScore, juce::Colour(0xff8294a6));
    drawScoreMeter(rightMeter, "POST", postNorm, metrics.postScore, accent);

    auto bars = bounds.reduced(14, 10);
    const int footerHeight = showGhostStats ? 16 : 0;
    auto barsArea = bars;
    if (footerHeight > 0)
        barsArea.removeFromBottom(footerHeight);

    const int barCount = 5;
    const int gap = 5;
    const int row = juce::jmax(26, (barsArea.getHeight() - gap * (barCount - 1)) / barCount);

    if (showTriangleMetrics)
    {
        drawBar(g, barsArea.removeFromTop(row), "Emphasis", metrics.emphasis, juce::Colour(0xfff39c12), emphasisStats);
        barsArea.removeFromTop(gap);
        drawBar(g, barsArea.removeFromTop(row), "Coherence", metrics.coherence, juce::Colour(0xff56e39f), coherenceStats);
        barsArea.removeFromTop(gap);
        drawBar(g, barsArea.removeFromTop(row), "Synesthesia", metrics.synesthesia, juce::Colour(0xff6ecbff), synesthesiaStats);
        barsArea.removeFromTop(gap);
        drawBar(g, barsArea.removeFromTop(row), "Fatigue Risk", metrics.fatigueRisk, juce::Colour(0xfff26d6d), fatigueStats);
        barsArea.removeFromTop(gap);
        drawBar(g, barsArea.removeFromTop(row), "Repetition", metrics.repetitionDensity, juce::Colour(0xffc39bff), repetitionStats);
    }
    else
    {
        drawBar(g, barsArea.removeFromTop(row), "Punch", metrics.punch, juce::Colour(0xffe67e22), punchStats);
        barsArea.removeFromTop(gap);
        drawBar(g, barsArea.removeFromTop(row), "Richness", metrics.richness, juce::Colour(0xfff1c40f), richnessStats);
        barsArea.removeFromTop(gap);
        drawBar(g, barsArea.removeFromTop(row), "Clarity", metrics.clarity, juce::Colour(0xff2ecc71), clarityStats);
        barsArea.removeFromTop(gap);
        drawBar(g, barsArea.removeFromTop(row), "Width", metrics.width, juce::Colour(0xff3498db), widthStats);
        barsArea.removeFromTop(gap);
        drawBar(g, barsArea.removeFromTop(row), "Mono Safety", metrics.monoSafety, juce::Colour(0xff9b59b6), monoSafetyStats);
    }

    if (showGhostStats && footerHeight > 0)
    {
        g.setColour(juce::Colour(0xffb9c2cd).withAlpha(0.6f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::plain));
        g.drawText("ghost: min-max range | avg marker", bars.removeFromBottom(footerHeight), juce::Justification::centredRight);
    }
}
