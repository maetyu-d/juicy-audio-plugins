#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "JuicinessAnalyzer.h"

class JuicyMeterPanel : public juce::Component
{
public:
    void setMetrics(const JuicinessMetrics& newMetrics);
    void paint(juce::Graphics& g) override;

private:
    void drawBar(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& name, float value, juce::Colour colour);

    JuicinessMetrics metrics;
};
