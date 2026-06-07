#include "ChainListBox.h"
#include "ToneForgeLookAndFeel.h"

namespace
{
    const juce::String kUp             = juce::String::fromUTF8("\xE2\x96\xB2");   // ▲
    const juce::String kDown           = juce::String::fromUTF8("\xE2\x96\xBC");   // ▼
    const juce::String kRemove         = juce::String::fromUTF8("\xE2\x9C\x95");   // ✕
    const juce::String kBypass         = juce::String::fromUTF8("\xE2\x8A\x98");   // ⊘
    const juce::String kSlotActive     = juce::String::fromUTF8("\xE2\x97\x8F");   // ●
    const juce::String kPresetActive   = juce::String::fromUTF8("\xE2\x96\xB6");   // ▶
    const juce::String kPresetInactive = juce::String::fromUTF8("\xE2\x97\x8B");   // ○
    const juce::String kEditor         = juce::String::fromUTF8("\xE2\x9A\x99");   // ⚙
}

// ── Model: refreshComponentForRow ─────────────────────────────────────────────
juce::Component* ChainListModel::refreshComponentForRow(int row, bool isRowSelected,
                                                        juce::Component* existing)
{
    if (! juce::isPositiveAndBelow(row, rows.size()))
    {
        delete existing;
        return nullptr;
    }

    const auto& rowData = rows.getReference(row);

    if (rowData.kind == ChainRow::Kind::sectionHeader)
    {
        auto* comp = dynamic_cast<SectionHeaderComponent*>(existing);
        if (comp == nullptr) { delete existing; comp = new SectionHeaderComponent(*this); }
        comp->update(row, rowData, isRowSelected);
        return comp;
    }
    else
    {
        auto* comp = dynamic_cast<ChainSlotComponent*>(existing);
        if (comp == nullptr) { delete existing; comp = new ChainSlotComponent(*this); }
        comp->update(row, rowData, isRowSelected);
        return comp;
    }
}

// ── SectionHeaderComponent ────────────────────────────────────────────────────

SectionHeaderComponent::SectionHeaderComponent(ChainListModel& ownerModel) : model(ownerModel)
{
    auto styleIcon = [this](juce::TextButton& b, const juce::String& text, juce::Colour c)
    {
        b.setButtonText(text);
        b.setColour(juce::TextButton::textColourOffId, c);
        b.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible(b);
    };

    styleIcon(upButton,     kUp,     tf::colour::textDim);
    styleIcon(downButton,   kDown,   tf::colour::textDim);
    styleIcon(removeButton, kRemove, tf::colour::danger);
    styleIcon(bypassButton, kBypass, tf::colour::textDim);

    upButton.onClick     = [this] { if (model.onSectionMoveUp)   model.onSectionMoveUp(sectionId);   };
    downButton.onClick   = [this] { if (model.onSectionMoveDown) model.onSectionMoveDown(sectionId); };
    removeButton.onClick = [this] { if (model.onSectionRemove)   model.onSectionRemove(sectionId);   };
    bypassButton.onClick = [this]
    {
        sectionBypassed = ! sectionBypassed;
        bypassButton.setColour(juce::TextButton::buttonColourId,
                               sectionBypassed ? tf::colour::warn.withAlpha(0.28f) : juce::Colour{});
        if (model.onSectionBypass)
            model.onSectionBypass(sectionId, sectionBypassed);
    };
}

void SectionHeaderComponent::update(int rowIdx, const ChainRow& row, bool isRowSelected)
{
    rowIndex    = rowIdx;
    sectionId   = row.sectionId;
    sectionName = row.sectionName;
    sectionType = row.sectionType;
    isFirst     = row.isFirstSection;
    isLast      = row.isLastSection;
    selected    = isRowSelected;

    sectionBypassed = row.sectionBypassed;
    bypassButton.setColour(juce::TextButton::buttonColourId,
                           sectionBypassed ? tf::colour::warn.withAlpha(0.28f) : juce::Colour{});

    upButton.setEnabled(! isFirst);
    downButton.setEnabled(! isLast);
    repaint();
}

void SectionHeaderComponent::resized()
{
    auto area = getLocalBounds().reduced(4, 3);
    removeButton.setBounds(area.removeFromRight(26));   area.removeFromRight(3);
    downButton  .setBounds(area.removeFromRight(26));   area.removeFromRight(2);
    upButton    .setBounds(area.removeFromRight(26));   area.removeFromRight(6);
    bypassButton.setBounds(area.removeFromRight(26));
}

void SectionHeaderComponent::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f, 1.0f);

    g.setColour(selected ? tf::colour::accent.withAlpha(0.18f) : tf::colour::surface3);
    g.fillRoundedRectangle(r, 5.0f);

    if (selected)
    {
        g.setColour(tf::colour::accent.withAlpha(0.6f));
        g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);
    }

    // Left accent bar.
    g.setColour(sectionType == PluginChain::SectionDef::Type::preset
                ? tf::colour::warn : tf::colour::accentDim);
    g.fillRoundedRectangle(r.withWidth(4.0f), 2.0f);

    // Type badge pill.
    const juce::String badge = (sectionType == PluginChain::SectionDef::Type::preset) ? "PRESET" : "STOMP";
    const juce::Colour badgeColour = (sectionType == PluginChain::SectionDef::Type::preset)
                                     ? tf::colour::warn.withAlpha(0.22f)
                                     : tf::colour::accentDim.withAlpha(0.35f);
    const juce::Colour badgeText   = (sectionType == PluginChain::SectionDef::Type::preset)
                                     ? tf::colour::warn : tf::colour::accent;

    juce::Font badgeFont(juce::FontOptions(10.5f, juce::Font::bold));
    const float badgeW = (sectionType == PluginChain::SectionDef::Type::preset ? 54.0f : 48.0f);
    auto badgeRect = juce::Rectangle<float>(10.0f, r.getCentreY() - 9.0f, badgeW, 18.0f);
    g.setColour(badgeColour);
    g.fillRoundedRectangle(badgeRect, 4.0f);
    g.setColour(badgeText);
    g.setFont(badgeFont);
    g.drawText(badge, badgeRect, juce::Justification::centred);

    // Section name.
    auto nameArea = getLocalBounds().reduced(4 + (int) badgeW + 16, 0);
    nameArea.removeFromRight(26 * 4 + 3 + 2 + 6 + 6);   // reserve space for 4 buttons
    g.setColour(tf::colour::text);
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(sectionName, nameArea, juce::Justification::centredLeft);
}

void SectionHeaderComponent::mouseDown(const juce::MouseEvent& e)
{
    if (model.onSelect)
        model.onSelect(rowIndex);   // select this row so new plugins go to this section

    if (e.mods.isRightButtonDown())
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Rename Section");
        menu.addItem(2, "Remove Section");

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [this](int result)
            {
                if (result == 1 && model.onSectionRename)
                    model.onSectionRename(sectionId);
                else if (result == 2 && model.onSectionRemove)
                    model.onSectionRemove(sectionId);
            });
    }
}

// ── ChainSlotComponent ────────────────────────────────────────────────────────

ChainSlotComponent::ChainSlotComponent(ChainListModel& ownerModel) : model(ownerModel)
{
    auto styleIcon = [this](juce::TextButton& b, const juce::String& text, juce::Colour c)
    {
        b.setButtonText(text);
        b.setColour(juce::TextButton::textColourOffId, c);
        b.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible(b);
    };

    styleIcon(bypassButton,  kBypass, tf::colour::text);
    styleIcon(editorButton,  kEditor, tf::colour::accent);
    styleIcon(removeButton,  kRemove, tf::colour::danger);

    bypassButton.onClick = [this] { if (model.onBypass) model.onBypass(slotIndex); };
    editorButton.onClick = [this] { if (model.onEditor) model.onEditor(slotIndex); };
    removeButton.onClick = [this] { if (model.onRemove) model.onRemove(slotIndex); };
}

void ChainSlotComponent::update(int rowIdx, const ChainRow& row, bool sel)
{
    rowIndex  = rowIdx;
    slotIndex = row.slotIndex;
    selected  = sel;
    isFirstInSection = row.isFirstInSection;
    isLastInSection  = row.isLastInSection;
    controlHint = row.controlHint;

    const auto& info = row.slotInfo;
    name          = info.name;
    originalName  = info.originalName;
    format        = info.format;
    bypassed      = info.bypassed;
    hasCustomName = info.hasCustomName;
    isPreset      = info.isPreset;

    if (isPreset)
    {
        bypassButton.setButtonText(bypassed ? kPresetInactive : kPresetActive);
        bypassButton.setColour(juce::TextButton::buttonColourId,
                               bypassed ? juce::Colour{} : tf::colour::accent.withAlpha(0.28f));
        bypassButton.setColour(juce::TextButton::textColourOffId,
                               bypassed ? tf::colour::textDim : tf::colour::accent);
    }
    else
    {
        bypassButton.setButtonText(bypassed ? kBypass : kSlotActive);
        bypassButton.setColour(juce::TextButton::buttonColourId,
                               bypassed ? tf::colour::warn.withAlpha(0.28f) : juce::Colour{});
        bypassButton.setColour(juce::TextButton::textColourOffId,
                               bypassed ? tf::colour::warn : tf::colour::text);
    }

    repaint();
}

void ChainSlotComponent::resized()
{
    auto area = getLocalBounds().reduced(6, 5);

    const int gap = 4;
    removeButton.setBounds(area.removeFromRight(30));  area.removeFromRight(gap);
    editorButton.setBounds(area.removeFromRight(34));  area.removeFromRight(gap);
    bypassButton.setBounds(area.removeFromRight(34));  area.removeFromRight(gap);

    gripArea = area.removeFromLeft(18);
    textArea = area;
}

void ChainSlotComponent::mouseDown(const juce::MouseEvent& e)
{
    if (model.onSelect)
        model.onSelect(rowIndex);   // rowIndex for visual ListBox selection

    if (e.mods.isRightButtonDown())
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Learn Control");
        menu.addSeparator();
        menu.addItem(2, "Rename");
        if (hasCustomName)
            menu.addItem(3, "Reset Name");

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [this](int result)
            {
                if (result == 1 && model.onLearnControl)
                    model.onLearnControl(slotIndex);
                else if (result == 2 && model.onRename)
                    model.onRename(slotIndex);
                else if (result == 3 && model.onResetName)
                    model.onResetName(slotIndex);
            });
    }
}

void ChainSlotComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < kDragThreshold)
        return;

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
    {
        dragStarted = true;
        container->startDragging("chain-slot:" + juce::String(slotIndex),
                                 this,
                                 juce::ScaledImage{},
                                 false,
                                 nullptr,
                                 &e.source);
    }
}

void ChainSlotComponent::mouseUp(const juce::MouseEvent&)
{
    dragStarted = false;
}

void ChainSlotComponent::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);

    g.setColour(selected ? tf::colour::accent.withAlpha(0.16f) : tf::colour::surface2);
    g.fillRoundedRectangle(r, 6.0f);

    if (selected)
    {
        g.setColour(tf::colour::accent.withAlpha(0.7f));
        g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
    }

    // Drag grip — three short horizontal bars.
    {
        g.setColour(tf::colour::textDim.withAlpha(0.55f));
        constexpr int lineW = 10, lineH = 2, lineGap = 3;
        constexpr int totalH = 3 * lineH + 2 * lineGap;
        int ly = gripArea.getCentreY() - totalH / 2;
        const int lx = gripArea.getCentreX() - lineW / 2;
        for (int i = 0; i < 3; ++i)
        {
            g.fillRoundedRectangle((float) lx, (float) ly, (float) lineW, (float) lineH, 1.0f);
            ly += lineH + lineGap;
        }
    }

    auto a = textArea;

    // Index badge.
    auto badge = a.removeFromLeft(26).toFloat().reduced(1.0f, 7.0f);
    g.setColour(bypassed ? tf::colour::outline : tf::colour::accentDim);
    g.fillRoundedRectangle(badge, 4.0f);
    g.setColour(bypassed ? tf::colour::textDim : tf::colour::text);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(juce::String(slotIndex + 1), badge, juce::Justification::centred);

    a.removeFromLeft(8);

    auto nameArea = a.removeFromTop(a.getHeight() / 2 + 2);

    g.setColour(bypassed ? tf::colour::textDim : tf::colour::text);
    g.setFont(juce::FontOptions(14.5f, bypassed ? juce::Font::plain : juce::Font::bold));
    g.drawText(name, nameArea, juce::Justification::bottomLeft);

    g.setFont(juce::FontOptions(11.5f));
    juce::String sub = "[" + format + "]";
    if (hasCustomName)
        sub += "  " + originalName;
    if (controlHint.isNotEmpty())
    {
        g.setColour(tf::colour::accent.withAlpha(0.75f));
        // Draw hint right-aligned within the subtitle area before the bypassed tag
        g.drawText(controlHint, a, juce::Justification::centredRight);
        sub += "   ";
    }
    if (bypassed)
        sub += "bypassed";
    g.setColour(tf::colour::textDim);
    g.drawText(sub, a, juce::Justification::topLeft);
}

// ── ChainListBoxView ───────────────────────────────────────────────────────────

ChainListBoxView::ChainListBoxView(ChainListModel& model)
    : juce::ListBox("ChainListBoxView", &model), chainModel(model)
{}

bool ChainListBoxView::isInterestedInDragSource(const SourceDetails& details)
{
    return details.description.toString().startsWith("chain-slot:");
}

int ChainListBoxView::slotIndexFromDescription(const juce::var& description)
{
    const juce::String s = description.toString();
    if (s.startsWith("chain-slot:"))
        return s.fromFirstOccurrenceOf(":", false, false).getIntValue();
    return -1;
}

int ChainListBoxView::resolveTargetSlotIndex(juce::Point<int> posLocal,
                                              int& outRow, bool& outBefore,
                                              int& outSectionIdOverride) const
{
    const auto& rows = chainModel.getRows();
    outSectionIdOverride = -1;

    int row = getRowContainingPosition(posLocal.x, posLocal.y);

    // Dropped below all rows — find the last slot row.
    if (row < 0)
    {
        for (int i = rows.size() - 1; i >= 0; --i)
        {
            if (rows.getReference(i).kind == ChainRow::Kind::slot)
            {
                outRow    = i;
                outBefore = false;
                return rows.getReference(i).slotIndex;
            }
        }
        return -1;
    }

    // Section header row: check if its section is empty.
    if (rows.getReference(row).kind == ChainRow::Kind::sectionHeader)
    {
        outSectionIdOverride = rows.getReference(row).sectionId;

        // Walk forward past any consecutive headers to the first slot row.
        int nextRow = row + 1;
        while (nextRow < rows.size()
               && rows.getReference(nextRow).kind == ChainRow::Kind::sectionHeader)
            ++nextRow;

        if (nextRow >= rows.size())
        {
            // Empty section at the very end — indicator below the last slot row.
            for (int i = rows.size() - 1; i >= 0; --i)
            {
                if (rows.getReference(i).kind == ChainRow::Kind::slot)
                {
                    outRow    = i;
                    outBefore = false;
                    // Return "one past last slot" so itemDropped knows to append.
                    return chainModel.getSlotCount();
                }
            }
            return -1;   // no slots at all
        }

        // The empty section's natural insertion point is just before the first slot
        // of the next non-empty section.
        outRow    = nextRow;
        outBefore = true;
        return rows.getReference(nextRow).slotIndex;
    }

    // Regular slot row.
    const auto rowBounds = getRowPosition(row, true);
    outBefore = (posLocal.y < rowBounds.getCentreY());
    outRow    = row;
    return rows.getReference(row).slotIndex;
}

void ChainListBoxView::itemDragMove(const SourceDetails& details)
{
    int  row, sectionOverride;
    bool before;
    resolveTargetSlotIndex(details.localPosition, row, before, sectionOverride);

    if (row != dropIndicatorRow || before != dropBefore)
    {
        dropIndicatorRow = row;
        dropBefore       = before;
        repaint();
    }
}

void ChainListBoxView::itemDragExit(const SourceDetails&)
{
    dropIndicatorRow = -1;
    repaint();
}

void ChainListBoxView::itemDropped(const SourceDetails& details)
{
    dropIndicatorRow = -1;
    repaint();

    const int fromSlot = slotIndexFromDescription(details.description);
    if (fromSlot < 0)
        return;

    int row, sectionIdOverride;
    bool before;
    const int toSlot = resolveTargetSlotIndex(details.localPosition, row, before, sectionIdOverride);

    if (toSlot < 0)
        return;

    int finalTarget;

    if (sectionIdOverride >= 0)
    {
        // Dropping on an empty section: toSlot is the section's flat insertion position P.
        // movePlugin(F, T) = erase F, insert at T in the post-removal array.
        // F < P  → after erase, P shifts to P-1 → use T = P-1
        // F == P → no position change, just reassign section  → T = F
        // F > P  → erase doesn't affect P → use T = P
        if (fromSlot < toSlot)
            finalTarget = toSlot - 1;
        else if (fromSlot == toSlot)
            finalTarget = fromSlot;
        else
            finalTarget = toSlot;
    }
    else
    {
        // Dropping on a slot row: use before/after semantics.
        // movePlugin(F, T) = erase F, insert at T in the post-removal array.
        // "before toSlot": F > T → T (push T forward), F < T → T-1 (T shifted down after erase)
        // "after  toSlot": F < T → T (lands after T),  F > T → T+1 (T unaffected)
        if (before)
            finalTarget = (fromSlot > toSlot) ? toSlot : toSlot - 1;
        else
            finalTarget = (fromSlot < toSlot) ? toSlot : toSlot + 1;
    }

    if (finalTarget < 0 || (finalTarget == fromSlot && sectionIdOverride < 0))
        return;

    if (onMovePlugin)
        onMovePlugin(fromSlot, finalTarget, sectionIdOverride);
}

void ChainListBoxView::paintOverChildren(juce::Graphics& g)
{
    juce::ListBox::paintOverChildren(g);

    if (dropIndicatorRow < 0)
        return;

    const auto rowBounds = getRowPosition(dropIndicatorRow, true);
    const int y = dropBefore ? rowBounds.getY() : rowBounds.getBottom();

    g.setColour(tf::colour::accent);
    g.fillRect(4, y - 1, getWidth() - 8, 2);

    // Small arrowhead at the left for visual clarity.
    juce::Path arrow;
    arrow.addTriangle(4.0f, (float) (y - 4), 4.0f, (float) (y + 4), 10.0f, (float) y);
    g.fillPath(arrow);
}
