#pragma once
#include <JuceHeader.h>

// Returns the base data directory used for all persistent app files.
//
// Portable mode  — activated by placing "portable.txt" next to the exe.
//                  Data goes to  <exe-dir>/data/
// Installed mode — default (no portable.txt present).
//                  Data goes to  %APPDATA%/AmpForge/
//
// The result is computed once on first call and cached for the process lifetime.
namespace AppDataDir
{
    inline const juce::File& get()
    {
        static const juce::File dir = []
        {
            const auto exeDir = juce::File::getSpecialLocation(
                                    juce::File::currentExecutableFile).getParentDirectory();
            if (exeDir.getChildFile("portable.txt").existsAsFile())
                return exeDir.getChildFile("data");
            return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("AmpForge");
        }();
        return dir;
    }
}
