#include "JuicyPluginEditor.h"

JuicyPluginEditor::JuicyPluginEditor(juce::AudioProcessor& audioProcessor,
                                     juce::AudioProcessorValueTreeState& valueTreeState,
                                     MetricsProvider metricsFn,
                                     const juce::String& pluginTitle)
    : AudioProcessorEditor(audioProcessor),
      state(valueTreeState),
      metricsProvider(std::move(metricsFn))
{
    titleLabel.setText(pluginTitle, juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    addAndMakeVisible(meterPanel);
    createControls();

    setSize(880, 560);
    startTimerHz(20);
}

void JuicyPluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0a0d12));
}

void JuicyPluginEditor::resized()
{
    auto bounds = getLocalBounds().reduced(16);
    titleLabel.setBounds(bounds.removeFromTop(42));
    meterPanel.setBounds(bounds.removeFromTop(260).reduced(0, 8));

    auto controlsArea = bounds.reduced(0, 8);
    const int rowHeight = 64;
    for (auto& control : controls)
    {
        auto row = controlsArea.removeFromTop(rowHeight);
        if (row.getHeight() <= 0)
            break;
        control.label->setBounds(row.removeFromLeft(180));
        control.slider->setBounds(row.reduced(8, 12));
    }
}

void JuicyPluginEditor::timerCallback()
{
    if (!metricsProvider)
        return;
    meterPanel.setMetrics(metricsProvider());
}

void JuicyPluginEditor::createControls()
{
    auto* processor = getAudioProcessor();
    if (processor == nullptr)
        return;

    for (auto* parameter : processor->getParameters())
    {
        auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);
        if (withID == nullptr || withID->paramID == "juiciness")
            continue;

        ParamControl control;
        control.slider = std::make_unique<juce::Slider>();
        control.slider->setSliderStyle(juce::Slider::LinearHorizontal);
        control.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 22);
        control.slider->setColour(juce::Slider::trackColourId, juce::Colour(0xfff39c12));
        control.slider->setColour(juce::Slider::thumbColourId, juce::Colours::white);
        control.slider->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1c2330));

        control.label = std::make_unique<juce::Label>();
        control.label->setText(parameter->getName(32), juce::dontSendNotification);
        control.label->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.9f));
        control.label->setJustificationType(juce::Justification::centredLeft);

        control.attachment = std::make_unique<Attachment>(state, withID->paramID, *control.slider);

        addAndMakeVisible(*control.label);
        addAndMakeVisible(*control.slider);
        controls.push_back(std::move(control));
    }
}
