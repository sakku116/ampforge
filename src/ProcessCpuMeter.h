#pragma once

#include <JuceHeader.h>
#include <windows.h>

// Real per-process CPU load, Task-Manager style:
//   (delta kernel+user time) / (delta wall time * numCores) * 100.
// Aggregates all threads in this process (UI, audio, plugin background threads).
// Call from the message thread (e.g. a timer) — not real-time safe, which is fine.
class ProcessCpuMeter
{
public:
    double sample()
    {
        FILETIME c, e, k, u, now;
        if (! GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
            return lastPct;
        GetSystemTimeAsFileTime(&now);

        const auto cpu  = toI64(k) + toI64(u);   // 100ns units, all threads
        const auto wall = toI64(now);

        if (lastWall != 0)
        {
            const double dCpu  = double(cpu  - lastCpu);
            const double dWall = double(wall - lastWall);
            if (dWall > 0.0)
                lastPct = juce::jlimit(0.0, 100.0, 100.0 * dCpu / (dWall * numCores));
        }

        lastCpu  = cpu;
        lastWall = wall;
        return lastPct;
    }

private:
    static juce::int64 toI64(const FILETIME& f)
    {
        ULARGE_INTEGER x;
        x.LowPart  = f.dwLowDateTime;
        x.HighPart = f.dwHighDateTime;
        return (juce::int64) x.QuadPart;
    }

    const int   numCores = juce::jmax(1, juce::SystemStats::getNumCpus());
    juce::int64 lastCpu  = 0;
    juce::int64 lastWall = 0;
    double      lastPct  = 0.0;
};
