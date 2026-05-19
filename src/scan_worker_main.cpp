#include <JuceHeader.h>
#include "ScanSubprocess.h"

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String commandLine;

    for (int i = 0; i < argc; ++i)
        commandLine << argv[i] << " ";

    return ScanSubprocess::runWorker(commandLine.trim());
}
