#include "JuicyMeterPanel.h"

void JuicyMeterPanel::setMetrics(const JuicinessMetrics& newMetrics)
{
    metrics = newMetrics;
    repaint();
}

void JuicyMeterPanel::drawBar(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& name, float value, juce::Colour colour)
{
    auto bg = area.reduced(0, 4);
    g.setColour(juce::Colour(0xff1e2229));
    g.fillRoundedRectangle(bg.toFloat(), 5.0f);

    const int fillWidth = static_cast<int>(std::round(value * static_cast<float>(bg.getWidth())));
    auto fillArea = bg.withWidth(juce::jlimit(0, bg.getWidth(), fillWidth));
    g.setColour(colour);
    g.fillRoundedRectangle(fillArea.toFloat(), 5.0f);

    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(13.0f);
    g.drawText(name, bg.removeFromLeft(110), juce::Justification::centredLeft);
    g.drawText(juce::String(value * 100.0f, 1) + "%", bg, juce::Justification::centredRight);
}

void JuicyMeterPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff11151c), 0.0f, 0.0f,
                                           juce::Colour(0xff212833), 0.0f, static_cast<float>(bounds.getBottom()), false));
    g.fillRoundedRectangle(bounds.toFloat(), 12.0f);
    g.setColour(juce::Colour(0xff3d4a5f));
    g.drawRoundedRectangle(bounds.toFloat().reduced(1.0f), 12.0f, 1.0f);

    auto top = bounds.removeFromTop(82);
    const float normalizedScore = juce::jlimit(0.0f, 1.0f, metrics.score / 100.0f);
    const int meterWidth = static_cast<int>(std::round(normalizedScore * static_cast<float>(top.getWidth() - 24)));

    g.setColour(juce::Colour(0xff1b2027));
    g.fillRoundedRectangle(top.reduced(12, 24).toFloat(), 8.0f);
    g.setColour(juce::Colour(0xfff39c12));
    g.fillRoundedRectangle(top.reduced(12, 24).withWidth(meterWidth).toFloat(), 8.0f);

    g.setColour(juce::Colours::white.withAlpha(0.92f));
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("Juiciness " + juce::String(metrics.score, 1), top, juce::Justification::centred);

    auto bars = bounds.reduced(12, 8);
    const int row = 34;
    drawBar(g, bars.removeFromTop(row), "Punch", metrics.punch, juce::Colour(0xffe67e22));
    drawBar(g, bars.removeFromTop(row), "Richness", metrics.richness, juce::Colour(0xfff1c40f));
    drawBar(g, bars.removeFromTop(row), "Clarity", metrics.clarity, juce::Colour(0xff2ecc71));
    drawBar(g, bars.removeFromTop(row), "Width", metrics.width, juce::Colour(0xff3498db));
    drawBar(g, bars.removeFromTop(row), "Mono Safety", metrics.monoSafety, juce::Colour(0xff9b59b6));
}
