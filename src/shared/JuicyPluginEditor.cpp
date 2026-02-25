#include "JuicyPluginEditor.h"

static juce::Colour accentFromTitle(const juce::String& title)
{
    const juce::uint32 h = static_cast<juce::uint32>(title.hashCode());
    const float t = static_cast<float>(h % 1000) / 1000.0f;
    const float hue = juce::jmap(t, 0.54f, 0.60f);
    return juce::Colour::fromHSV(hue, 0.24f, 0.78f, 1.0f);
}

JuicyPluginEditor::JuicyPluginEditor(juce::AudioProcessor& audioProcessor,
                                     juce::AudioProcessorValueTreeState& valueTreeState,
                                     MetricsProvider metricsFn,
                                     const juce::String& pluginTitle,
                                     bool showGhostStats,
                                     bool showTriangleMetrics)
    : AudioProcessorEditor(audioProcessor),
      state(valueTreeState),
      metricsProvider(std::move(metricsFn))
{
    const auto accent = accentFromTitle(pluginTitle);

    titleLabel.setText(pluginTitle, juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffedf1f6));
    addAndMakeVisible(titleLabel);

    meterPanel.setAccentColour(accent);
    meterPanel.setShowGhostStats(showGhostStats);
    meterPanel.setShowTriangleMetrics(showTriangleMetrics);
    addAndMakeVisible(meterPanel);
    createControls();

    setSize(880, 560);
    startTimerHz(20);
}

void JuicyPluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff101216));
}

void JuicyPluginEditor::resized()
{
    auto bounds = getLocalBounds().reduced(22, 20);
    auto header = bounds.removeFromTop(36);
    titleLabel.setBounds(header);
    bounds.removeFromTop(10);

    const int meterHeight = juce::jlimit(214, 260, static_cast<int>(bounds.getHeight() * 0.46f));
    meterPanel.setBounds(bounds.removeFromTop(meterHeight));
    bounds.removeFromTop(14);

    auto controlsArea = bounds;
    if (controls.empty())
        return;

    const int columns = controls.size() > 4 ? 2 : 1;
    const int gap = 14;
    const int columnWidth = columns == 1
        ? controlsArea.getWidth()
        : (controlsArea.getWidth() - gap) / 2;
    const int rows = (static_cast<int>(controls.size()) + columns - 1) / columns;
    const int rowHeight = juce::jmax(58, controlsArea.getHeight() / juce::jmax(1, rows));

    for (int i = 0; i < static_cast<int>(controls.size()); ++i)
    {
        auto& control = controls[static_cast<size_t>(i)];
        const int col = i % columns;
        const int rowIndex = i / columns;
        juce::Rectangle<int> row(
            controlsArea.getX() + col * (columnWidth + gap),
            controlsArea.getY() + rowIndex * rowHeight,
            columnWidth,
            rowHeight);

        row = row.reduced(4, 6);
        control.label->setBounds(row.removeFromTop(18));
        row.removeFromTop(2);
        control.slider->setBounds(row);
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
        if (withID == nullptr)
            continue;

        if (withID->paramID == "juiciness"
            || withID->paramID == "emphasis"
            || withID->paramID == "coherence"
            || withID->paramID == "synesthesia"
            || withID->paramID == "fatigue"
            || withID->paramID == "repetition"
            || withID->paramID == "contextfit")
            continue;

        ParamControl control;
        control.slider = std::make_unique<juce::Slider>();
        control.slider->setSliderStyle(juce::Slider::LinearHorizontal);
        control.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 84, 20);
        control.slider->setScrollWheelEnabled(false);
        control.slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff9aa8b6));
        control.slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xffdbe2ea));
        control.slider->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff242a31));
        control.slider->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff171b21));
        control.slider->setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe4e9ee));
        control.slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff313740));

        control.label = std::make_unique<juce::Label>();
        control.label->setText(parameter->getName(32), juce::dontSendNotification);
        control.label->setColour(juce::Label::textColourId, juce::Colour(0xffcfd6df));
        control.label->setFont(juce::FontOptions(12.0f, juce::Font::plain));
        control.label->setJustificationType(juce::Justification::centredLeft);

        control.attachment = std::make_unique<Attachment>(state, withID->paramID, *control.slider);

        addAndMakeVisible(*control.label);
        addAndMakeVisible(*control.slider);
        controls.push_back(std::move(control));
    }
}
