#include <JuceHeader.h>
#include "MainComponent.h"

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(juce::String name) : juce::DocumentWindow(name,
                                                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                              .findColour(juce::ResizableWindow::backgroundColourId),
                                                          juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(), true);
        centreWithSize(900, 600);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class GuitarVST3HostApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Guitar VST3 Host"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(GuitarVST3HostApplication)
