#pragma once

#include <JuceHeader.h>
#include "PluginChain.h"

/** A single row in the chain list: either a section header or a plugin slot. */
struct ChainRow
{
    enum class Kind { sectionHeader, slot };
    Kind kind = Kind::slot;

    // Section-header fields (used when kind == sectionHeader):
    int          sectionId   = 0;
    juce::String sectionName;
    PluginChain::SectionDef::Type sectionType = PluginChain::SectionDef::Type::stomp;
    bool isFirstSection = false;
    bool isLastSection  = false;

    // Slot fields (used when kind == slot):
    PluginChain::SlotInfo slotInfo;
    int  slotIndex         = -1;   // absolute index into the flat SlotList
    bool isFirstInSection  = false;
    bool isLastInSection   = false;
};

class ChainSlotComponent;
class SectionHeaderComponent;

/** ListBox model for the signal chain. Each row is either a SectionHeaderComponent
    (32 px) or a ChainSlotComponent (52 px). */
class ChainListModel : public juce::ListBoxModel
{
public:
    // Slot-action callbacks (receive slotIndex, not row index):
    std::function<void(int)> onBypass;
    std::function<void(int)> onRemove;
    std::function<void(int)> onEditor;
    std::function<void(int)> onSelect;
    std::function<void(int)> onRename;
    std::function<void(int)> onResetName;

    // Section-action callbacks (receive sectionId):
    std::function<void(int)> onSectionMoveUp;
    std::function<void(int)> onSectionMoveDown;
    std::function<void(int)> onSectionRemove;
    std::function<void(int)> onSectionRename;

    void setRows(juce::Array<ChainRow> newRows) { rows = std::move(newRows); }
    const juce::Array<ChainRow>& getRows() const { return rows; }

    int getSlotCount() const
    {
        int n = 0;
        for (const auto& r : rows)
            if (r.kind == ChainRow::Kind::slot) ++n;
        return n;
    }

    int  getNumRows() override { return rows.size(); }
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow(int row, bool isRowSelected, juce::Component* existing) override;

private:
    juce::Array<ChainRow> rows;
};

// ── Section header row ─────────────────────────────────────────────────────────

class SectionHeaderComponent : public juce::Component
{
public:
    explicit SectionHeaderComponent(ChainListModel& ownerModel);

    void update(int rowIdx, const ChainRow& row, bool isRowSelected);
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    ChainListModel& model;

    int  rowIndex   = -1;
    int  sectionId  = 0;
    bool isFirst    = false;
    bool isLast     = false;
    bool selected   = false;
    juce::String sectionName;
    PluginChain::SectionDef::Type sectionType = PluginChain::SectionDef::Type::stomp;

    juce::TextButton upButton, downButton, removeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SectionHeaderComponent)
};

// ── Plugin slot row ────────────────────────────────────────────────────────────

class ChainSlotComponent : public juce::Component
{
public:
    explicit ChainSlotComponent(ChainListModel& ownerModel);

    void update(int rowIdx, const ChainRow& row, bool selected);

    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    ChainListModel& model;

    int  rowIndex  = -1;
    int  slotIndex = -1;
    bool selected  = false;
    bool bypassed  = false;
    bool isPreset  = false;
    bool hasCustomName   = false;
    bool isFirstInSection = false;
    bool isLastInSection  = false;
    juce::String name, originalName, format;
    juce::Rectangle<int> textArea;
    juce::Rectangle<int> gripArea;

    bool dragStarted = false;
    static constexpr int kDragThreshold = 6;

    juce::TextButton bypassButton, editorButton, removeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainSlotComponent)
};

// ── Drag-and-drop list box ─────────────────────────────────────────────────────

/** A ListBox subclass that also acts as a DragAndDropTarget for chain-slot drags.
    Fires onMovePlugin(fromSlotIndex, toSlotIndex) when a drop is resolved. */
class ChainListBoxView : public juce::ListBox,
                         public juce::DragAndDropTarget
{
public:
    explicit ChainListBoxView(ChainListModel& model);

    /** fromSlotIndex, toSlotIndex, sectionIdOverride (-1 = inherit from displaced slot). */
    std::function<void(int fromSlotIndex, int toSlotIndex, int sectionIdOverride)> onMovePlugin;

    // DragAndDropTarget interface:
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
    void paintOverChildren(juce::Graphics&) override;

private:
    ChainListModel& chainModel;

    int  dropIndicatorRow = -1;
    bool dropBefore       = true;

    /** Returns the target flat slot index (or getSlotCount() for "append at end").
        outSectionIdOverride >= 0 when dropping on an empty section header. */
    int  resolveTargetSlotIndex(juce::Point<int> posLocal,
                                int& outRow, bool& outBefore,
                                int& outSectionIdOverride) const;
    static int slotIndexFromDescription(const juce::var& description);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainListBoxView)
};
