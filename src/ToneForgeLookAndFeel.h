#pragma once

#include <JuceHeader.h>

/** Shared dark "stage" palette for the whole app. Tweak here to re-skin everything. */
namespace tf::colour
{
    const juce::Colour background { 0xff15171c };   // window backdrop
    const juce::Colour surface    { 0xff20242c };   // panels
    const juce::Colour surface2   { 0xff2a2f39 };   // raised controls (buttons, combo)
    const juce::Colour surface3   { 0xff333a45 };   // hover / selected row
    const juce::Colour outline    { 0xff3a414d };   // hairline borders
    const juce::Colour text       { 0xffe8eaed };   // primary text
    const juce::Colour textDim    { 0xff8b93a0 };   // secondary text
    const juce::Colour accent     { 0xff21c08a };   // teal-green accent
    const juce::Colour accentDim  { 0xff17795a };   // accent, muted
    const juce::Colour danger     { 0xffe0655f };   // destructive (remove)
    const juce::Colour warn       { 0xffe2b53a };   // learn / armed state
}

/** Dark, rounded, accent-driven look. Set as the default LookAndFeel so popups,
    the audio-settings window and plugin editor chrome all inherit the theme. */
class ToneForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ToneForgeLookAndFeel();

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    int getDefaultScrollbarWidth() override { return 10; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneForgeLookAndFeel)
};
