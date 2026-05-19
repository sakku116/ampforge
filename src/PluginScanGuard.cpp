#include "PluginScanGuard.h"

#if JUCE_WINDOWS
 #include <eh.h>
 #include <excpt.h>
#endif

namespace PluginScanGuard
{
namespace
{
#if JUCE_WINDOWS
void __cdecl sehToCppTranslator(unsigned int code, _EXCEPTION_POINTERS*)
{
    juce::ignoreUnused(code);
    throw std::runtime_error("Windows SEH exception during VST3 operation");
}

struct SehTranslatorScope
{
    SehTranslatorScope()  { _set_se_translator(sehToCppTranslator); }
    ~SehTranslatorScope() { _set_se_translator(nullptr); }
};
#endif
}

ScanOutcome scanFile(juce::KnownPluginList& list,
                     const juce::String& path,
                     juce::AudioPluginFormat& format)
{
#if JUCE_WINDOWS
    const SehTranslatorScope sehScope;
#endif

    try
    {
        const auto typesBefore = list.getNumTypes();
        juce::OwnedArray<juce::PluginDescription> foundTypes;
        list.scanAndAddFile(path, true, foundTypes, format);
        return list.getNumTypes() > typesBefore ? ScanOutcome::addedTypes : ScanOutcome::noTypes;
    }
    catch (const std::exception& ex)
    {
        juce::ignoreUnused(ex);
        return ScanOutcome::crashed;
    }
    catch (...)
    {
        return ScanOutcome::crashed;
    }
}

LoadOutcome createPluginInstance(juce::AudioPluginFormatManager& formatManager,
                                 const juce::PluginDescription& description,
                                 double sampleRate,
                                 int blockSize)
{
    LoadOutcome outcome;

#if JUCE_WINDOWS
    const SehTranslatorScope sehScope;
#endif

    try
    {
        outcome.instance = formatManager.createPluginInstance(description, sampleRate, blockSize, outcome.error);
    }
    catch (const std::exception& ex)
    {
        outcome.crashed = true;
        outcome.instance.reset();
        outcome.error = ex.what();
    }
    catch (...)
    {
        outcome.crashed = true;
        outcome.instance.reset();
        outcome.error = "Plugin load crashed the host (SEH)";
    }

    return outcome;
}

}
