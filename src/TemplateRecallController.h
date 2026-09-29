#pragma once

#include "PluginHost.h"
#include "TemplateManager.h"
#include <atomic>
#include <mutex>

/** Commits template selection, map, and dirty state only after the host activates the chain. */
class TemplateRecallController
{
    struct State
    {
        std::atomic<int> epoch { 0 };
        std::atomic<PluginHost*> host { nullptr };
    };

public:
    TemplateRecallController() : state(std::make_shared<State>()) {}
    ~TemplateRecallController() { cancel(); }

    bool recall(PluginHost& host, TemplateManager& templates, ControlMap& activeMap,
                std::recursive_mutex& mapMutex, bool& dirty, int index,
                std::function<void(bool)> onComplete)
    {
        if (! juce::isPositiveAndBelow(index, templates.getNumScenes())) return false;
        cancel();
        const auto shared = state;
        const int request = shared->epoch.load();
        shared->host.store(&host);
        const auto scene = templates.getScene(index);
        host.switchChainAsync(scene.specs, scene.sections, 25,
            [shared, request, hostPtr = &host, &templates, &activeMap, &mapMutex, &dirty, index,
             map = scene.controlMap, done = std::move(onComplete)](bool activated) mutable
            {
                if (shared->epoch.load() != request) return;
                auto* expectedHost = hostPtr;
                shared->host.compare_exchange_strong(expectedHost, nullptr);
                if (activated)
                {
                    templates.setCurrentIndex(index);
                    { std::lock_guard<std::recursive_mutex> lock(mapMutex); activeMap = map; }
                    dirty = false;
                }
                if (done) done(activated);
            });
        return true;
    }

    void cancel()
    {
        state->epoch.fetch_add(1);
        if (auto* host = state->host.exchange(nullptr))
            host->cancelPendingSwitch();
    }

private:
    std::shared_ptr<State> state;
};
