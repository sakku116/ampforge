#pragma once

#include <JuceHeader.h>

/** Prefixes and writes to the active juce::Logger (console + host.log when configured). */
namespace HostDebug
{
inline void log(const juce::String& message)
{
    juce::Logger::writeToLog("[GuitarHost] " + message);
}

inline void log(const char* message)
{
    log(juce::String(message));
}
}
