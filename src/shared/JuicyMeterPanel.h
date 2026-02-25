#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "JuicinessAnalyzer.h"

class JuicyMeterPanel : public juce::Component
{
public:
    void setMetrics(const JuicinessMetrics& newMetrics);
    void setAccentColour(juce::Colour colour);
    void setShowGhostStats(bool shouldShow);
    void setShowTriangleMetrics(bool shouldShow);
    void paint(juce::Graphics& g) override;

private:
    struct MetricStats
    {
        float min = 1.0f;
        float max = 0.0f;
        float avg = 0.0f;
        int count = 0;
    };

    void drawBar(juce::Graphics& g,
                 juce::Rectangle<int> area,
                 const juce::String& name,
                 float value,
                 juce::Colour colour,
                 const MetricStats& stats);
    void updateStats(MetricStats& stats, float value);
    static float smoothValue(float current, float target);

    JuicinessMetrics metrics;
    juce::Colour accent = juce::Colour(0xfff39c12);
    bool showGhostStats = false;
    bool showTriangleMetrics = false;
    MetricStats punchStats;
    MetricStats richnessStats;
    MetricStats clarityStats;
    MetricStats widthStats;
    MetricStats monoSafetyStats;
    MetricStats emphasisStats;
    MetricStats coherenceStats;
    MetricStats synesthesiaStats;
    MetricStats fatigueStats;
    MetricStats repetitionStats;
};
