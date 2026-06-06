#include "ChainListBox.h"
#include "ToneForgeLookAndFeel.h"

namespace
{
    const juce::String kUp     = juce::String::fromUTF8("\xE2\x96\xB2");   // ▲
    const juce::String kDown   = juce::String::fromUTF8("\xE2\x96\xBC");   // ▼
    const juce::String kRemove = juce::String::fromUTF8("\xE2\x9C\x95");   // ✕
}

// ── Model ──────────────────────────────────────────────────────────────────────
juce::Component* ChainListModel::refreshComponentForRow(int row, bool isRowSelected,
                                                        juce::Component* existing)
{
    if (! juce::isPositiveAndBelow(row, infos.size()))
    {
        delete existing;
        return nullptr;
    }

    auto* comp = dynamic_cast<ChainSlotComponent*>(existing);

    if (comp == nullptr)
    {
        delete existing;
        comp = new ChainSlotComponent(*this);
    }

    comp->update(row, isRowSelected);
    return comp;
}

// ── Row component ────────────────────────────────────────────────────────────────
ChainSlotComponent::ChainSlotComponent(ChainListModel& ownerModel) : model(ownerModel)
{
    auto styleIcon = [this](juce::TextButton& b, const juce::String& text, juce::Colour c)
    {
        b.setButtonText(text);
        b.setColour(juce::TextButton::textColourOffId, c);
        b.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible(b);
    };

    styleIcon(bypassButton, "BYP",  tf::colour::text);
    styleIcon(upButton,     kUp,    tf::colour::textDim);
    styleIcon(downButton,   kDown,  tf::colour::textDim);
    styleIcon(editorButton, "EDIT", tf::colour::accent);
    styleIcon(removeButton, kRemove, tf::colour::danger);

    bypassButton.onClick = [this] { if (model.onBypass)   model.onBypass(row);   };
    upButton.onClick     = [this] { if (model.onMoveUp)   model.onMoveUp(row);   };
    downButton.onClick   = [this] { if (model.onMoveDown) model.onMoveDown(row); };
    editorButton.onClick = [this] { if (model.onEditor)   model.onEditor(row);   };
    removeButton.onClick = [this] { if (model.onRemove)   model.onRemove(row);   };
}

void ChainSlotComponent::update(int newRow, bool isSelected)
{
    row = newRow;
    selected = isSelected;

    const auto& infos = model.getInfos();

    if (juce::isPositiveAndBelow(row, infos.size()))
    {
        const auto& info = infos.getReference(row);
        name         = info.name;
        originalName = info.originalName;
        format       = info.format;
        bypassed     = info.bypassed;
        hasCustomName = info.hasCustomName;

        // Bypass toggle reads as an "active/engaged" pill: accent when ON, dim when bypassed.
        bypassButton.setToggleState(! bypassed, juce::dontSendNotification);
        bypassButton.setButtonText(bypassed ? "BYP" : "ON");
        bypassButton.setColour(juce::TextButton::textColourOffId, tf::colour::textDim);

        upButton.setEnabled(row > 0);
        downButton.setEnabled(row < infos.size() - 1);
    }

    repaint();
}

void ChainSlotComponent::resized()
{
    auto area = getLocalBounds().reduced(6, 5);

    const int gap = 4;
    removeButton.setBounds(area.removeFromRight(30));        area.removeFromRight(gap);
    editorButton.setBounds(area.removeFromRight(48));        area.removeFromRight(gap);
    downButton.setBounds(area.removeFromRight(30));          area.removeFromRight(2);
    upButton.setBounds(area.removeFromRight(30));            area.removeFromRight(gap);
    bypassButton.setBounds(area.removeFromRight(44));        area.removeFromRight(gap);

    textArea = area;   // remaining left area is painted (index badge + name/format)
}

void ChainSlotComponent::mouseDown(const juce::MouseEvent& e)
{
    if (model.onSelect)
        model.onSelect(row);

    if (e.mods.isRightButtonDown())
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Rename");

        if (hasCustomName)
            menu.addItem(2, "Reset Name");

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [this](int result)
            {
                if (result == 1 && model.onRename)
                    model.onRename(row);
                else if (result == 2 && model.onResetName)
                    model.onResetName(row);
            });
    }
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

    auto a = textArea;

    // Index badge.
    auto badge = a.removeFromLeft(26).toFloat().reduced(1.0f, 7.0f);
    g.setColour(bypassed ? tf::colour::outline : tf::colour::accentDim);
    g.fillRoundedRectangle(badge, 4.0f);
    g.setColour(bypassed ? tf::colour::textDim : tf::colour::text);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(juce::String(row + 1), badge, juce::Justification::centred);

    a.removeFromLeft(8);

    // Name (bold) + sub line (dim, below).
    // Sub line: when renamed → "[VST3] original plugin name" [+ " bypassed"]
    //           otherwise    → "[VST3]" [+ " bypassed"]
    auto nameArea = a.removeFromTop(a.getHeight() / 2 + 2);

    g.setColour(bypassed ? tf::colour::textDim : tf::colour::text);
    g.setFont(juce::FontOptions(14.5f, bypassed ? juce::Font::plain : juce::Font::bold));
    g.drawText(name, nameArea, juce::Justification::bottomLeft);

    g.setColour(tf::colour::textDim);
    g.setFont(juce::FontOptions(11.5f));
    juce::String sub = "[" + format + "]";
    if (hasCustomName)
        sub += "  " + originalName;
    if (bypassed)
        sub += "   bypassed";
    g.drawText(sub, a, juce::Justification::topLeft);
}
