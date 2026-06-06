#include "ToneForgeLookAndFeel.h"

ToneForgeLookAndFeel::ToneForgeLookAndFeel()
{
    using namespace tf::colour;

    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::DocumentWindow::textColourId,        text);

    setColour(juce::Label::textColourId,                 text);

    setColour(juce::TextButton::buttonColourId,          surface2);
    setColour(juce::TextButton::buttonOnColourId,        accent);
    setColour(juce::TextButton::textColourOffId,         text);
    setColour(juce::TextButton::textColourOnId,          juce::Colours::black);

    setColour(juce::ListBox::backgroundColourId,         surface);
    setColour(juce::ListBox::outlineColourId,            outline);

    setColour(juce::ComboBox::backgroundColourId,        surface2);
    setColour(juce::ComboBox::textColourId,              text);
    setColour(juce::ComboBox::outlineColourId,           outline);
    setColour(juce::ComboBox::arrowColourId,             accent);
    setColour(juce::ComboBox::buttonColourId,            surface2);

    setColour(juce::PopupMenu::backgroundColourId,            surface2);
    setColour(juce::PopupMenu::textColourId,                  text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour(juce::PopupMenu::highlightedTextColourId,       juce::Colours::black);

    setColour(juce::ScrollBar::thumbColourId,            outline);
    setColour(juce::ScrollBar::trackColourId,            surface);

    setColour(juce::TextEditor::backgroundColourId,      surface2);
    setColour(juce::TextEditor::textColourId,            text);
    setColour(juce::TextEditor::outlineColourId,         outline);
    setColour(juce::TextEditor::focusedOutlineColourId,  accent);
    setColour(juce::CaretComponent::caretColourId,       accent);
}

void ToneForgeLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                const juce::Colour& /*backgroundColour*/,
                                                bool shouldDrawButtonAsHighlighted,
                                                bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const float radius = 6.0f;

    auto base = button.findColour(button.getToggleState() ? juce::TextButton::buttonOnColourId
                                                          : juce::TextButton::buttonColourId);

    if (! button.isEnabled())
        base = base.withMultipliedAlpha(0.4f);
    else if (shouldDrawButtonAsDown)
        base = base.darker(0.25f);
    else if (shouldDrawButtonAsHighlighted)
        base = base.brighter(0.12f);

    g.setColour(base);
    g.fillRoundedRectangle(bounds, radius);

    g.setColour(tf::colour::outline.withAlpha(button.isEnabled() ? 1.0f : 0.4f));
    g.drawRoundedRectangle(bounds, radius, 1.0f);
}

juce::Font ToneForgeLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return juce::FontOptions(juce::jmin(15.0f, (float) buttonHeight * 0.5f));
}

void ToneForgeLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                        int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
    const float radius = 6.0f;

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, radius);

    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, radius, 1.0f);

    // Accent chevron on the right.
    juce::Rectangle<float> arrow((float) width - 22.0f, 0.0f, 18.0f, (float) height);
    juce::Path p;
    const float cx = arrow.getCentreX();
    const float cy = arrow.getCentreY();
    p.addTriangle(cx - 5.0f, cy - 2.5f, cx + 5.0f, cy - 2.5f, cx, cy + 3.5f);

    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 1.0f : 0.4f));
    g.fillPath(p);
}

void ToneForgeLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(10, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont(juce::FontOptions(14.0f));
}
