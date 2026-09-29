#include <JuceHeader.h>
#include "PluginHost.h"
#include "TemplateRecallController.h"

namespace
{
class FakeEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FakeEditor(juce::AudioProcessor& p) : AudioProcessorEditor(p) { setSize(100, 60); }
    void paint(juce::Graphics&) override {}
    void resized() override {}
};

class FakePlugin final : public juce::AudioPluginInstance
{
public:
    explicit FakePlugin(juce::String n) : AudioPluginInstance(BusesProperties().withInput("In", juce::AudioChannelSet::stereo(), true).withOutput("Out", juce::AudioChannelSet::stereo(), true)), name(std::move(n)) {}
    const juce::String getName() const override { return name; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new FakeEditor(*this); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& d) const override { d.name = name; d.pluginFormatName = "Fake"; d.fileOrIdentifier = name; }
private:
    juce::String name;
};

juce::Array<PluginChain::SectionDef> sections()
{
    juce::Array<PluginChain::SectionDef> s;
    s.add({ 10, "Stomp", PluginChain::SectionDef::Type::stomp });
    s.add({ 20, "Presets", PluginChain::SectionDef::Type::preset });
    return s;
}

juce::Array<PluginChain::SlotSpec> specs(juce::StringArray names)
{
    juce::Array<PluginChain::SlotSpec> result;
    for (int i = 0; i < names.size(); ++i)
    {
        PluginChain::SlotSpec x;
        x.description.name = names[i]; x.description.pluginFormatName = "Fake";
        x.sectionId = i == 0 ? 10 : 20; x.slotId = i + 1;
        x.bypassed = i == 2; x.customName = "Custom " + names[i]; x.postGain = 0.75f;
        result.add(x);
    }
    return result;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::UnitTestRunner runner;
    class SwitchTests final : public juce::UnitTest
    {
    public:
        SwitchTests() : UnitTest("PluginHost staged switching") {}
        void runTest() override
        {
            juce::AudioPluginFormatManager formats;
            std::atomic<bool> delayBuild { false };
            auto factory = [&delayBuild](juce::AudioPluginFormatManager&, const juce::PluginDescription& d, double, int, juce::String& error)
            {
                if (delayBuild.load()) juce::Thread::sleep(80);
                if (d.name == "fail") { error = "injected failure"; return std::unique_ptr<juce::AudioPluginInstance>{}; }
                return std::unique_ptr<juce::AudioPluginInstance>(new FakePlugin(d.name));
            };
            PluginHost host(factory);
            beginTest("failed synchronous middle slot preserves current chain and stable identities");
            expect(host.switchChainWithCrossfade(specs({ "old-a", "old-b" }), sections(), 0));
            const auto before = host.getSlotInfos();
            expectEquals(before.size(), 2);
            host.openEditorWindow(0);
            expect(host.hasOpenEditor());
            expect(!host.switchChainWithCrossfade(specs({ "new-a", "fail", "new-c" }), sections(), 25));
            expect(host.hasOpenEditor());
            const auto after = host.getSlotInfos();
            expectEquals(after.size(), before.size());
            for (int i = 0; i < before.size(); ++i) { expectEquals(after[i].slotId, before[i].slotId); expectEquals(after[i].name, before[i].name); }

            beginTest("async failure completes false without publishing staged slots or sections");
            bool completed = false, outcome = true;
            host.switchChainAsync(specs({ "new-a", "fail", "new-c" }), sections(), 25, [&](bool ok) { outcome = ok; completed = true; });
            const auto deadline = juce::Time::getMillisecondCounter() + 5000;
            while (!completed && juce::Time::getMillisecondCounter() < deadline) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            expect(completed); expect(!outcome); expectEquals(host.getSlotInfos().size(), before.size()); expect(host.hasOpenEditor());

            beginTest("rebuild supersedes a pending async switch");
            delayBuild.store(true);
            bool supersededCallback = false;
            host.switchChainAsync(specs({ "late-a", "late-b" }), sections(), 25, [&](bool) { supersededCallback = true; });
            expect(host.rebuildChain(specs({ "rebuilt-a", "rebuilt-b" }), sections()));
            delayBuild.store(false);
            const auto rebuildDeadline = juce::Time::getMillisecondCounter() + 300;
            while (juce::Time::getMillisecondCounter() < rebuildDeadline) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            expect(!supersededCallback); expectEquals(host.getSlotInfos()[0].name, juce::String("Custom rebuilt-a"));

            beginTest("cancellation suppresses pending completion and publication");
            delayBuild.store(true);
            bool cancelledCallback = false;
            host.switchChainAsync(specs({ "cancel-a", "cancel-b" }), sections(), 25, [&](bool) { cancelledCallback = true; });
            host.cancelPendingSwitch();
            const auto cancelDeadline = juce::Time::getMillisecondCounter() + 300;
            while (juce::Time::getMillisecondCounter() < cancelDeadline) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            delayBuild.store(false);
            expect(!cancelledCallback); expectEquals(host.getSlotInfos().size(), before.size());

            beginTest("template recall commits selection, map, and dirty state only after activation");
            TemplateManager templates;
            ControlMap oldMap, failedMap, successMap, activeMap;
            oldMap.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::F1Key }, { ControlAction::Type::nextTemplate, 0 } });
            failedMap.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::F2Key }, { ControlAction::Type::nextTemplate, 0 } });
            successMap.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::F3Key }, { ControlAction::Type::nextTemplate, 0 } });
            activeMap = oldMap;
            templates.addScene("Current", specs({ "old-a", "old-b" }), sections(), oldMap);
            templates.addScene("Broken", specs({ "new-a", "fail", "new-c" }), sections(), failedMap);
            templates.addScene("Working", specs({ "new-a", "new-b", "new-c" }), sections(), successMap);
            templates.setCurrentIndex(0);
            bool dirty = true, recallDone = false, recallOk = true;
            std::recursive_mutex mapMutex;
            TemplateRecallController recall;
            delayBuild.store(true);
            expect(recall.recall(host, templates, activeMap, mapMutex, dirty, 1, [&](bool ok) { recallOk = ok; recallDone = true; }));
            expectEquals(templates.getCurrentIndex(), 0); expect(activeMap.matchKey(juce::KeyPress::F1Key).isValid()); expect(dirty);
            const auto failedDeadline = juce::Time::getMillisecondCounter() + 5000;
            while (!recallDone && juce::Time::getMillisecondCounter() < failedDeadline) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            expect(recallDone); expect(!recallOk); expectEquals(templates.getCurrentIndex(), 0);
            expect(activeMap.matchKey(juce::KeyPress::F1Key).isValid()); expect(dirty);

            recallDone = false; recallOk = false;
            expect(recall.recall(host, templates, activeMap, mapMutex, dirty, 2, [&](bool ok) { recallOk = ok; recallDone = true; }));
            expectEquals(templates.getCurrentIndex(), 0); expect(activeMap.matchKey(juce::KeyPress::F1Key).isValid()); expect(dirty);
            const auto successDeadline = juce::Time::getMillisecondCounter() + 5000;
            while (!recallDone && juce::Time::getMillisecondCounter() < successDeadline) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            delayBuild.store(false);
            expect(recallDone); expect(recallOk); expectEquals(templates.getCurrentIndex(), 2);
            expect(activeMap.matchKey(juce::KeyPress::F3Key).isValid()); expect(!dirty); expect(host.getChain().isTransitioning());
            expect(!host.hasOpenEditor());

            beginTest("controller cancel aborts pending activation without changing recall state");
            dirty = true; recallDone = false;
            delayBuild.store(true);
            recall.recall(host, templates, activeMap, mapMutex, dirty, 0, [&](bool) { recallDone = true; });
            recall.cancel();
            const auto controllerCancelDeadline = juce::Time::getMillisecondCounter() + 300;
            while (juce::Time::getMillisecondCounter() < controllerCancelDeadline) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            delayBuild.store(false);
            expect(!recallDone); expectEquals(templates.getCurrentIndex(), 2);
            expect(activeMap.matchKey(juce::KeyPress::F3Key).isValid()); expect(dirty);

            beginTest("completed recall clears host reference before host destruction");
            TemplateRecallController survivor;
            TemplateManager temporaryTemplates;
            ControlMap temporaryMap;
            std::recursive_mutex temporaryMutex;
            bool temporaryDirty = true, temporaryDone = false;
            temporaryTemplates.addScene("Empty", {}, {}, {});
            {
                PluginHost temporaryHost(factory);
                survivor.recall(temporaryHost, temporaryTemplates, temporaryMap, temporaryMutex, temporaryDirty, 0,
                               [&](bool ok) { temporaryDone = ok; });
                const auto temporaryDeadline = juce::Time::getMillisecondCounter() + 3000;
                while (!temporaryDone && juce::Time::getMillisecondCounter() < temporaryDeadline) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
                expect(temporaryDone);
            }
            survivor.cancel();

            beginTest("successful switch retains metadata and starts crossfade");
            expect(host.switchChainWithCrossfade(specs({ "new-a", "new-b", "new-c" }), sections(), 100));
            expectEquals(host.getSlotInfos().size(), 3);
            expect(host.getChain().isTransitioning());
            expectEquals(host.getSlotInfos()[2].name, juce::String("Custom new-c"));
        }
    } test;
    runner.runAllTests();
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (runner.getResult(i)->failures > 0) return 1;
    return runner.getNumResults() == 0 ? 1 : 0;
}
