#pragma once

#include <JuceHeader.h>
#include "PluginChain.h"

// ── VolumeControl ──────────────────────────────────────────────────────────────

/** Small button for per-slot and per-section output gain.
    Clicking opens a CallOutBox with a horizontal slider (-30 dB … +6 dB)
    and a reset-to-0-dB button.
    onGainChanged(float linearGain) fires on every slider change. */
class VolumeControl : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    std::function<void(float)> onGainChanged;

    VolumeControl();

    void   setValue(double dB, juce::NotificationType = juce::dontSendNotification);
    double getValue() const { return currentdB; }

    static float  toLinear(double dB);
    static double fromLinear(float gain);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    double currentdB = 0.0;
    bool   hovered   = false;

    void showCallout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VolumeControl)
};

// ── ChainRow ───────────────────────────────────────────────────────────────────

/** A single row in the chain list: either a section header or a plugin slot. */
struct ChainRow
{
    enum class Kind { sectionHeader, slot };
    Kind kind = Kind::slot;

    // Section-header fields (used when kind == sectionHeader):
    int          sectionId   = 0;
    juce::String sectionName;
    PluginChain::SectionDef::Type sectionType = PluginChain::SectionDef::Type::stomp;
    bool isFirstSection  = false;
    bool isLastSection   = false;
    bool sectionBypassed = false;
    float sectionGain    = 1.0f;   // section output gain (linear)

    // Slot fields (used when kind == slot):
    PluginChain::SlotInfo slotInfo;
    int  slotIndex         = -1;   // absolute index into the flat SlotList
    bool isFirstInSection  = false;
    bool isLastInSection   = false;
    juce::String controlHint;      // e.g. "BYP: CC 5" or "ACT: Key 65" when a binding is learned
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
    std::function<void(int)> onDuplicate;
    std::function<void(int)> onEditor;
    std::function<void(int)> onSelect;
    std::function<void(int)> onRename;
    std::function<void(int)> onResetName;
    std::function<void(int)> onLearnControl;

    // Gain callbacks:
    std::function<void(int, float)> onSlotGainChanged;     // (slotIndex, linearGain)
    std::function<void(int, float)> onSectionGainChanged;  // (sectionId, linearGain)
    std::function<float(int)>       onGetSectionLevel;     // (sectionId) → peak 0..1+

    // Section-action callbacks (receive sectionId):
    std::function<void(int)>       onSectionMoveUp;
    std::function<void(int)>       onSectionMoveDown;
    std::function<void(int)>       onSectionRemove;
    std::function<void(int)>       onSectionRename;
    std::function<void(int, bool)> onSectionBypass;

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

class SectionHeaderComponent : public juce::Component,
                               private juce::Timer
{
public:
    explicit SectionHeaderComponent(ChainListModel& ownerModel);

    void update(int rowIdx, const ChainRow& row, bool isRowSelected);
    void setHorizontalMode(bool horizontal);
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    bool horizontalMode = false;
    void timerCallback() override;

    ChainListModel& model;

    int  rowIndex   = -1;
    int  sectionId  = 0;
    bool isFirst         = false;
    bool isLast          = false;
    bool selected        = false;
    bool sectionBypassed = false;
    juce::String sectionName;
    PluginChain::SectionDef::Type sectionType = PluginChain::SectionDef::Type::stomp;

    juce::TextButton upButton, downButton, bypassButton;
    VolumeControl    volumeControl;

    float displayLevel    = 0.0f;   // smoothed peak level for the meter
    float peakHold        = 0.0f;   // peak-hold value
    int   peakHoldCounter = 0;      // frames before peak starts decaying

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

    juce::String controlHint;

    bool dragStarted = false;
    static constexpr int kDragThreshold = 6;

    juce::TextButton bypassButton;
    VolumeControl    volumeControl;

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

// ── Level meter bar ────────────────────────────────────────────────────────────

/** Horizontal dBFS level meter.
    Assign getLevel, then let the component update itself at 24 fps.
    Click to reset the peak-hold marker. */
class LevelMeterBar : public juce::Component,
                      public juce::SettableTooltipClient,
                      private juce::Timer
{
public:
    static constexpr float kMinDb = -60.0f;
    static constexpr float kMaxDb =   6.0f;

    /** Assign before the component is shown. Returns raw linear peak (0..2+). */
    std::function<float()> getLevel;

    LevelMeterBar();

    void resetPeak();

    static float toDB(float linear) noexcept;
    static float toNorm(float dB)   noexcept;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    float displayLevel    = 0.0f;
    float peakHold        = 0.0f;
    int   peakHoldCounter = 0;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeterBar)
};

// ── Horizontal view: one column per section ────────────────────────────────────

/** A column that shows one section: header at top, plugin slot rows stacked below.
    Acts as a DragAndDropTarget so slots can be dropped into it from other columns. */
class SectionColumnComponent : public juce::Component,
                               public juce::DragAndDropTarget
{
public:
    explicit SectionColumnComponent(ChainListModel& ownerModel);

    /** Update with a header row, slot rows, and positional flags.
        flatRowOffset is the row index of headerRow in the flat ChainListModel row array. */
    void setRows(int flatRowOffset,
                 const ChainRow& headerRow,
                 const juce::Array<ChainRow>& slotRows);

    int getNumSlots() const noexcept { return slotIndices.size(); }

    std::function<void(int fromSlot, int toSlot, int sectionIdOverride)> onMovePlugin;

    static constexpr int kColumnWidth = 240;
    static constexpr int kRowHeight   = 46;

    void resized() override;
    void paintOverChildren(juce::Graphics&) override;

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

private:
    ChainListModel& model;

    int            sectionId  = 0;
    juce::Array<int> slotIndices;

    std::unique_ptr<SectionHeaderComponent> headerComp;
    juce::OwnedArray<ChainSlotComponent>    slotComps;

    int  dropSlotRow = -1;
    bool dropBefore  = true;

    static int slotIndexFromDescription(const juce::var& description);
    int resolveDropTarget(juce::Point<int> pos, bool& outBefore, int& outSectionIdOverride) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SectionColumnComponent)
};

/** Horizontal signal-chain view: sections as side-by-side columns, scrollable.
    Call setRows() with the same ChainRow array used for ChainListBoxView. */
class ChainHorizontalView : public juce::Component
{
public:
    explicit ChainHorizontalView(ChainListModel& ownerModel);

    void setRows(const juce::Array<ChainRow>& rows);

    std::function<void(int fromSlot, int toSlot, int sectionIdOverride)> onMovePlugin;

    void resized() override;

private:
    ChainListModel& model;

    juce::Viewport viewport;
    juce::Component contentArea;
    juce::OwnedArray<SectionColumnComponent> columns;

    void layoutColumns();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainHorizontalView)
};
