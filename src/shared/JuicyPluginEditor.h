#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <functional>
#include "JuicyMeterPanel.h"

class JuicyPluginEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using MetricsProvider = std::function<JuicinessMetrics()>;

    JuicyPluginEditor(juce::AudioProcessor& processor,
                      juce::AudioProcessorValueTreeState& valueTreeState,
                      MetricsProvider metricsFn,
                      const juce::String& pluginTitle,
                      bool showGhostStats = false,
                      bool showTriangleMetrics = false);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct ParamControl
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<Attachment> attachment;
    };

    void timerCallback() override;
    void createControls();

    juce::AudioProcessorValueTreeState& state;
    MetricsProvider metricsProvider;
    juce::Label titleLabel;
    JuicyMeterPanel meterPanel;
    std::vector<ParamControl> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JuicyPluginEditor)
};
