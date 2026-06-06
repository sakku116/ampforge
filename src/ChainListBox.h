#pragma once

#include <JuceHeader.h>
#include "PluginChain.h"

/** ListBox model for the signal chain. Each row is a full ChainSlotComponent with its own
    inline controls (bypass / move up / move down / editor / remove), so the chain is operated
    directly per-pedal instead of via global buttons on the current selection. */
class ChainListModel : public juce::ListBoxModel
{
public:
    // Row-action callbacks; each receives the row index. Set by the owner (MainComponent).
    std::function<void(int)> onBypass;
    std::function<void(int)> onMoveUp;
    std::function<void(int)> onMoveDown;
    std::function<void(int)> onRemove;
    std::function<void(int)> onEditor;
    std::function<void(int)> onSelect;

    void setInfos(juce::Array<PluginChain::SlotInfo> newInfos) { infos = std::move(newInfos); }
    const juce::Array<PluginChain::SlotInfo>& getInfos() const { return infos; }

    int getNumRows() override { return infos.size(); }
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}   // rows are components
    juce::Component* refreshComponentForRow(int row, bool isRowSelected, juce::Component* existing) override;

private:
    juce::Array<PluginChain::SlotInfo> infos;
};

/** One pedal slot row: index badge + name/format + inline action buttons. */
class ChainSlotComponent : public juce::Component
{
public:
    explicit ChainSlotComponent(ChainListModel& ownerModel);

    void update(int newRow, bool selected);

    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    ChainListModel& model;

    int  row = -1;
    bool selected = false;
    bool bypassed = false;
    juce::String name, format;
    juce::Rectangle<int> textArea;

    juce::TextButton bypassButton, upButton, downButton, editorButton, removeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainSlotComponent)
};
