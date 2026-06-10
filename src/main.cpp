#include <JuceHeader.h>
#include "HostDebug.h"
#include "MainComponent.h"

namespace
{
std::unique_ptr<juce::FileLogger> fileLogger;

void setupLogging()
{
    auto logDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("AmpForge");

    logDir.createDirectory();
    const auto logFile = logDir.getChildFile("host.log");

    fileLogger = std::make_unique<juce::FileLogger>(logFile,
                                                    "=== Amp Forge session ===",
                                                    512 * 1024);

    juce::Logger::setCurrentLogger(fileLogger.get());
    HostDebug::log("Log file: " + logFile.getFullPathName());
}
}

class MainWindow : public juce::DocumentWindow
{
public:
    static constexpr const char* kWindowStateKey = "windowState";

    MainWindow(juce::String name) : juce::DocumentWindow(name,
                                                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                              .findColour(juce::ResizableWindow::backgroundColourId),
                                                          juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setContentOwned(new MainComponent(), true);

        const auto savedState = getSettingsValue(kWindowStateKey);
        if (savedState.isNotEmpty())
            restoreWindowStateFromString(savedState);
        else
            centreWithSize(900, 600);

        setVisible(true);
    }

    ~MainWindow() override
    {
        saveWindowState();
    }

    void closeButtonPressed() override
    {
        saveWindowState();
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    juce::String getSettingsValue(const juce::String& key)
    {
        if (auto* mc = dynamic_cast<MainComponent*>(getContentComponent()))
            if (auto* s = mc->getAppSettingsFile())
                return s->getValue(key);
        return {};
    }

    void saveWindowState()
    {
        if (auto* mc = dynamic_cast<MainComponent*>(getContentComponent()))
            if (auto* s = mc->getAppSettingsFile())
                s->setValue(kWindowStateKey, getWindowStateAsString());
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class AmpForgeApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Amp Forge"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise(const juce::String&) override
    {
        setupLogging();
        HostDebug::log("Application starting");
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
        HostDebug::log("Main window created");
    }

    void shutdown() override
    {
        HostDebug::log("Application shutting down");
        mainWindow.reset();
        juce::Logger::setCurrentLogger(nullptr);
        fileLogger.reset();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(AmpForgeApplication)
