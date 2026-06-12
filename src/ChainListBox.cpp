#include "ChainListBox.h"
#include "ToneForgeLookAndFeel.h"

// ── VolumeSliderCallout ────────────────────────────────────────────────────────

/** Content component shown inside a juce::CallOutBox when the VolumeControl
    button is clicked. Contains a horizontal slider + a reset-to-0-dB button. */
class VolumeSliderCallout : public juce::Component
{
public:
    std::function<void(double)> onValueChanged;   // dB

    explicit VolumeSliderCallout(double initialDb)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 62, 24);
        slider.setRange(-30.0, 6.0, 0.1);
        slider.setSkewFactorFromMidPoint(0.0);
        slider.setValue(initialDb, juce::dontSendNotification);
        slider.setNumDecimalPlacesToDisplay(1);
        slider.setTextValueSuffix(" dB");
        addAndMakeVisible(slider);

        resetBtn.setButtonText(juce::String::fromUTF8("\xE2\x86\xBA") + " 0 dB");  // ↺ 0 dB
        resetBtn.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible(resetBtn);

        slider.onValueChange = [this] { if (onValueChanged) onValueChanged(slider.getValue()); };
        resetBtn.onClick      = [this] { slider.setValue(0.0, juce::sendNotification); };

        setSize(270, 46);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(6, 8);
        resetBtn.setBounds(area.removeFromRight(62));
        area.removeFromRight(4);
        slider.setBounds(area);
    }

private:
    juce::Slider     slider;
    juce::TextButton resetBtn;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VolumeSliderCallout)
};

// ── VolumeControl ─────────────────────────────────────────────────────────────

float VolumeControl::toLinear(double dB)
{
    return dB <= -30.0 ? 0.0f : (float) juce::Decibels::decibelsToGain((float) dB);
}

double VolumeControl::fromLinear(float gain)
{
    return gain < 0.001f ? -30.0 : (double) juce::Decibels::gainToDecibels(gain);
}

VolumeControl::VolumeControl()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTooltip("Output volume. Click to adjust.");
}

void VolumeControl::setValue(double dB, juce::NotificationType)
{
    currentdB = dB;
    repaint();
}

void VolumeControl::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    const bool isNeutral = std::abs(currentdB) < 0.05;

    // Button background.
    g.setColour(hovered ? tf::colour::surface3.brighter(0.08f) : tf::colour::surface3);
    g.fillRoundedRectangle(r, 3.0f);

    if (!isNeutral)
    {
        g.setColour(tf::colour::accent.withAlpha(0.18f));
        g.fillRoundedRectangle(r, 3.0f);
    }

    // Thin border.
    g.setColour(tf::colour::outline.withAlpha(hovered ? 0.6f : 0.35f));
    g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 0.8f);

    // Split: icon top half, label bottom half.
    const float cx  = r.getCentreX();
    const float mid = r.getY() + r.getHeight() * 0.52f;

    // Speaker icon (body + horn + optional wave arc).
    const juce::Colour iconColour = isNeutral ? tf::colour::textDim : tf::colour::accent;
    g.setColour(iconColour);

    const float iy  = r.getY() + 3.5f;          // icon top
    const float ih  = mid - iy - 2.0f;           // icon height
    const float ibx = cx - 5.5f;                 // body left x
    const float ibw = 3.5f;                       // body width

    // Speaker body rect.
    juce::Path icon;
    icon.addRectangle(ibx, iy + ih * 0.2f, ibw, ih * 0.6f);

    // Horn (trapezoid).
    const float hx = ibx + ibw;
    icon.startNewSubPath(hx, iy + ih * 0.15f);
    icon.lineTo(hx + 4.5f, iy);
    icon.lineTo(hx + 4.5f, iy + ih);
    icon.lineTo(hx, iy + ih * 0.85f);
    icon.closeSubPath();

    g.fillPath(icon);

    // Sound wave arcs (only when not muted).
    if (currentdB > -30.0)
    {
        const float midY = iy + ih * 0.5f;
        // Inner arc — small.
        {
            juce::Path arc;
            const float wr = ih * 0.38f;
            arc.addArc(hx + 5.5f - wr, midY - wr, wr * 2.0f, wr * 2.0f,
                       juce::MathConstants<float>::pi * 1.25f,
                       juce::MathConstants<float>::pi * 1.75f, true);
            g.strokePath(arc, juce::PathStrokeType(1.0f));
        }
        if (currentdB > -12.0)
        {
            juce::Path arc;
            const float wr = ih * 0.62f;
            arc.addArc(hx + 5.5f - wr, midY - wr, wr * 2.0f, wr * 2.0f,
                       juce::MathConstants<float>::pi * 1.3f,
                       juce::MathConstants<float>::pi * 1.7f, true);
            g.strokePath(arc, juce::PathStrokeType(1.0f));
        }
    }

    // dB label.
    juce::String label;
    if (currentdB <= -30.0)  label = "-inf";
    else if (std::abs(currentdB) < 0.05) label = "0 dB";
    else label = (currentdB > 0 ? "+" : "") + juce::String(currentdB, 1);

    g.setColour(isNeutral ? tf::colour::textDim : tf::colour::text);
    g.setFont(juce::FontOptions(8.0f));
    g.drawText(label, r.withTop(mid), juce::Justification::centred);
}

void VolumeControl::mouseEnter(const juce::MouseEvent&) { hovered = true;  repaint(); }
void VolumeControl::mouseExit (const juce::MouseEvent&) { hovered = false; repaint(); }

void VolumeControl::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown())
        showCallout();
}

void VolumeControl::showCallout()
{
    auto* content = new VolumeSliderCallout(currentdB);

    juce::Component::SafePointer<VolumeControl> safeThis(this);
    content->onValueChanged = [safeThis](double dB)
    {
        if (safeThis == nullptr) return;
        safeThis->currentdB = dB;
        safeThis->repaint();
        if (safeThis->onGainChanged)
            safeThis->onGainChanged(VolumeControl::toLinear(dB));
    };

    juce::CallOutBox::launchAsynchronously(
        std::unique_ptr<juce::Component>(content),
        getScreenBounds(),
        nullptr);
}

// ── LevelMeterBar ─────────────────────────────────────────────────────────────

float LevelMeterBar::toDB(float linear) noexcept
{
    return linear < 1e-7f ? kMinDb : juce::Decibels::gainToDecibels(linear);
}

float LevelMeterBar::toNorm(float dB) noexcept
{
    return juce::jlimit(0.0f, 1.0f, (dB - kMinDb) / (kMaxDb - kMinDb));
}

LevelMeterBar::LevelMeterBar()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTooltip("Click to reset peak hold");
    startTimerHz(24);
}

void LevelMeterBar::resetPeak()
{
    peakHold = 0.0f;
    peakHoldCounter = 0;
    repaint();
}

void LevelMeterBar::mouseDown(const juce::MouseEvent&)
{
    resetPeak();
}

void LevelMeterBar::timerCallback()
{
    if (! getLevel) return;
    const float raw = getLevel();
    displayLevel = raw > displayLevel ? raw : displayLevel * 0.88f;
    if (raw >= peakHold)
    {
        peakHold = raw;
        peakHoldCounter = 60;   // ~2.5 s at 24 fps
    }
    else if (peakHoldCounter > 0)
        --peakHoldCounter;
    else
        peakHold *= 0.94f;

    // Snap negligible values to zero so an idle meter stops repainting entirely.
    if (displayLevel < 1.0e-4f) displayLevel = 0.0f;
    if (peakHold     < 1.0e-4f) peakHold     = 0.0f;

    if (displayLevel != paintedLevel || peakHold != paintedPeak)
    {
        paintedLevel = displayLevel;
        paintedPeak  = peakHold;
        repaint();
    }
}

void LevelMeterBar::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    const int w = bounds.getWidth();
    const int h = bounds.getHeight();

    // Layout: optional label region on top, bar below.
    const int kLabelH = compact ? 0 : 13;
    const int barTop  = bounds.getY() + kLabelH + (compact ? 0 : 1);
    const int barH    = h - kLabelH - (compact ? 0 : 1);

    if (barH < 2) return;

    // dB → pixel X helper.
    auto dBtoX = [&](float dB) -> int {
        return bounds.getX() + juce::roundToInt(toNorm(dB) * (float)w);
    };

    constexpr float ticksDb[]      = { -60.0f, -40.0f, -20.0f, -12.0f, -6.0f, 0.0f, 6.0f };
    constexpr const char* tickLabels[] = { "-60",  "-40",  "-20",  "-12",  "-6",  "0", "+6" };

    // ── dB scale labels (full mode only) ─────────────────────────────────────
    if (!compact)
    {
        g.setFont(juce::FontOptions(9.0f));
        for (int i = 0; i < 7; ++i)
        {
            const int x = dBtoX(ticksDb[i]);
            g.setColour(tf::colour::textDim.withAlpha(0.65f));
            g.drawText(juce::String(tickLabels[i]),
                       x - 12, bounds.getY(), 24, kLabelH,
                       juce::Justification::centred, false);
        }
    }

    // ── Bar background ────────────────────────────────────────────────────────
    const juce::Rectangle<int> bar(bounds.getX(), barTop, w, barH);
    g.setColour(tf::colour::background.withAlpha(0.9f));
    g.fillRoundedRectangle(bar.toFloat(), 2.0f);

    // ── Three-zone fill ───────────────────────────────────────────────────────
    if (displayLevel > 1e-7f)
    {
        const int levelX    = dBtoX(toDB(displayLevel));
        const int greenEnd  = dBtoX(-12.0f);
        const int yellowEnd = dBtoX(-6.0f);

        auto fillZone = [&](juce::Colour col, int x1, int x2)
        {
            x1 = juce::jlimit(bar.getX(), bar.getRight(), x1);
            x2 = juce::jlimit(bar.getX(), bar.getRight(), x2);
            if (x2 > x1)
            {
                g.setColour(col);
                g.fillRect(x1, barTop, x2 - x1, barH);
            }
        };

        fillZone(juce::Colour(0xff22c55e), bar.getX(),  juce::jmin(levelX, greenEnd));
        fillZone(juce::Colour(0xfff59e0b), greenEnd,    juce::jmin(levelX, yellowEnd));
        fillZone(juce::Colour(0xffef4444), yellowEnd,   levelX);
    }

    // ── Tick-mark hairlines (full mode only) ─────────────────────────────────
    if (!compact)
    {
        for (int i = 0; i < 7; ++i)
        {
            const int x = dBtoX(ticksDb[i]);
            g.setColour(juce::Colours::black.withAlpha(0.22f));
            g.drawVerticalLine(x, (float) barTop, (float) (barTop + barH));
        }
    }

    // ── Bar border ────────────────────────────────────────────────────────────
    g.setColour(tf::colour::outline.withAlpha(0.4f));
    g.drawRoundedRectangle(bar.toFloat().reduced(0.5f), 2.0f, 0.7f);

    // ── Peak hold tick + dB label ─────────────────────────────────────────────
    if (peakHold > 1e-7f)
    {
        const float peakDb = toDB(peakHold);
        const int   peakX  = dBtoX(peakDb);
        const bool  isClip = peakHold >= 1.0f;

        g.setColour(isClip ? tf::colour::danger : juce::Colours::white.withAlpha(0.9f));
        g.fillRect(peakX - 1, barTop, 2, barH);

        // Peak dB label (right edge of the label row)
        juce::String peakStr;
        if (isClip)
            peakStr = "CLIP";
        else
            peakStr = (peakDb >= 0.0f ? "+" : "") + juce::String(peakDb, 1) + " dB";

        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.setColour(isClip ? tf::colour::danger : tf::colour::textDim);
        g.drawText(peakStr, bounds.getRight() - 44, bounds.getY(), 42, kLabelH,
                   juce::Justification::centredRight, false);
    }
}

namespace
{
    const juce::String kUp             = juce::String::fromUTF8("\xE2\x96\xB2");   // ▲
    const juce::String kDown           = juce::String::fromUTF8("\xE2\x96\xBC");   // ▼
    const juce::String kRemove         = juce::String::fromUTF8("\xE2\x9C\x95");   // ✕
    const juce::String kBypass         = juce::String::fromUTF8("\xE2\x8A\x98");   // ⊘
    const juce::String kEditor         = juce::String::fromUTF8("\xE2\x9A\x99");   // ⚙
    const juce::String kLeft           = juce::String::fromUTF8("\xE2\x97\x80");   // ◀
    const juce::String kRight          = juce::String::fromUTF8("\xE2\x96\xB6");   // ▶ (same glyph, section-move context)

    // Shared layout constants — keeps section-header and slot-row buttons the same size and aligned.
    constexpr int kBtnW   = 28;
    constexpr int kVolW   = 30;
    constexpr int kBtnGap =  4;
    constexpr int kRightMargin = 4;   // right-edge inset used by both row types
}

// ── Model: refreshComponentForRow ─────────────────────────────────────────────
juce::Component* ChainListModel::refreshComponentForRow(int row, bool /*isRowSelected*/,
                                                        juce::Component* existing)
{
    if (! juce::isPositiveAndBelow(row, rows.size()))
    {
        delete existing;
        return nullptr;
    }

    const auto& rowData = rows.getReference(row);
    const bool isSelected = (row == highlightedRow);

    if (rowData.kind == ChainRow::Kind::sectionHeader)
    {
        auto* comp = dynamic_cast<SectionHeaderComponent*>(existing);
        if (comp == nullptr) { delete existing; comp = new SectionHeaderComponent(*this); }
        comp->update(row, rowData, isSelected);
        return comp;
    }
    else
    {
        auto* comp = dynamic_cast<ChainSlotComponent*>(existing);
        if (comp == nullptr) { delete existing; comp = new ChainSlotComponent(*this); }
        comp->update(row, rowData, isSelected);
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
    styleIcon(bypassButton, kBypass, tf::colour::textDim);

    upButton.onClick   = [this] { if (model.onSectionMoveUp)   model.onSectionMoveUp(sectionId);   };
    downButton.onClick = [this] { if (model.onSectionMoveDown) model.onSectionMoveDown(sectionId); };
    bypassButton.onClick = [this]
    {
        sectionBypassed = ! sectionBypassed;
        bypassButton.setColour(juce::TextButton::buttonColourId,
                               sectionBypassed ? tf::colour::warn.withAlpha(0.28f) : juce::Colour{});
        if (model.onSectionBypass)
            model.onSectionBypass(sectionId, sectionBypassed);
    };

    volumeControl.onGainChanged = [this](float gain)
    {
        if (model.onSectionGainChanged)
            model.onSectionGainChanged(sectionId, gain);
    };
    addAndMakeVisible(volumeControl);

    startTimerHz(24);   // refresh level meter ~24 fps
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

    volumeControl.setValue(VolumeControl::fromLinear(row.sectionGain));

    upButton.setEnabled(! isFirst);
    downButton.setEnabled(! isLast);
    repaint();
}

void SectionHeaderComponent::resized()
{
    auto area = getLocalBounds().reduced(kRightMargin, 3);
    downButton  .setBounds(area.removeFromRight(kBtnW)); area.removeFromRight(kBtnGap);
    upButton    .setBounds(area.removeFromRight(kBtnW)); area.removeFromRight(kBtnGap);
    bypassButton.setBounds(area.removeFromRight(kBtnW)); area.removeFromRight(kBtnGap);
    volumeControl.setBounds(area.removeFromRight(kVolW).reduced(0, 3));
}

void SectionHeaderComponent::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f, 1.0f);
    const bool isSelected = (model.highlightedRow == rowIndex);

    g.setColour(isSelected ? tf::colour::accent.withAlpha(0.13f) : tf::colour::surface3);
    g.fillRoundedRectangle(r, 5.0f);

    if (isSelected)
    {
        g.setColour(tf::colour::accent.withAlpha(0.75f));
        g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.5f);
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

    // Section name + level meter area.
    auto nameArea = getLocalBounds().reduced(4 + (int) badgeW + 16, 0);
    // Reserve space for volume control + 4 buttons on the right.
    nameArea.removeFromRight(30 + 3 + 26 * 4 + 3 + 2 + 6 + 6);

    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));

    if (!horizontalMode)
    {
        // ── Level meter strip (dBFS scale) ────────────────────────────────────
        auto meterArea = nameArea.removeFromBottom(10).reduced(0, 1);

        g.setColour(tf::colour::background.withAlpha(0.8f));
        g.fillRoundedRectangle(meterArea.toFloat(), 2.0f);

        if (displayLevel > 1e-7f)
        {
            const float levelNorm   = LevelMeterBar::toNorm(LevelMeterBar::toDB(displayLevel));
            const float greenThresh = LevelMeterBar::toNorm(-12.0f);
            const float yellowThresh= LevelMeterBar::toNorm(-6.0f);
            const int mW = meterArea.getWidth();
            const int levelX    = meterArea.getX() + juce::roundToInt(levelNorm    * mW);
            const int greenEndX = meterArea.getX() + juce::roundToInt(greenThresh  * mW);
            const int yellowEndX= meterArea.getX() + juce::roundToInt(yellowThresh * mW);

            auto fillZone = [&](juce::Colour col, int x1, int x2)
            {
                x1 = juce::jlimit(meterArea.getX(), meterArea.getRight(), x1);
                x2 = juce::jlimit(meterArea.getX(), meterArea.getRight(), x2);
                if (x2 > x1) { g.setColour(col); g.fillRect(x1, meterArea.getY(), x2 - x1, meterArea.getHeight()); }
            };
            fillZone(juce::Colour(0xff22c55e), meterArea.getX(), juce::jmin(levelX, greenEndX));
            fillZone(juce::Colour(0xfff59e0b), greenEndX,        juce::jmin(levelX, yellowEndX));
            fillZone(juce::Colour(0xffef4444), yellowEndX,       levelX);
        }

        if (peakHold > 1e-7f)
        {
            const float holdNorm = LevelMeterBar::toNorm(LevelMeterBar::toDB(peakHold));
            const int holdX = meterArea.getX() + juce::roundToInt(holdNorm * meterArea.getWidth());
            const bool isClip = peakHold >= 1.0f;
            g.setColour(isClip ? tf::colour::danger : juce::Colours::white.withAlpha(0.75f));
            g.fillRect(holdX - 1, meterArea.getY(), 2, meterArea.getHeight());
        }

        g.setColour(tf::colour::outline.withAlpha(0.35f));
        g.drawRoundedRectangle(meterArea.toFloat().reduced(0.5f), 2.0f, 0.6f);

        // Peak dB label.
        if (peakHold > 1e-7f)
        {
            const float peakDb = LevelMeterBar::toDB(peakHold);
            const bool  isClip = peakHold >= 1.0f;
            const juce::Colour pkCol = isClip ? tf::colour::danger
                                     : (peakDb > -6.0f) ? tf::colour::warn
                                     : tf::colour::textDim;
            const juce::String pkStr = isClip ? "CLIP"
                                     : (peakDb >= 0.0f ? "+" : "") + juce::String(peakDb, 1) + " dB";
            auto peakLabel = nameArea.removeFromRight(50);
            g.setColour(pkCol);
            g.setFont(juce::FontOptions(10.0f));
            g.drawText(pkStr, peakLabel, juce::Justification::centredRight);
            g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        }
    }

    g.setColour(tf::colour::text);
    g.drawText(sectionName, nameArea, juce::Justification::centredLeft);
}

void SectionHeaderComponent::timerCallback()
{
    if (horizontalMode) return;

    if (model.onGetSectionLevel)
    {
        const float raw = model.onGetSectionLevel(sectionId);
        // Smooth: fast attack, slow decay.
        displayLevel = raw > displayLevel ? raw : displayLevel * 0.88f;

        // Peak hold: freeze for ~40 frames (~1.7 s at 24 fps), then decay.
        if (raw >= peakHold)
        {
            peakHold = raw;
            peakHoldCounter = 40;
        }
        else if (peakHoldCounter > 0)
        {
            --peakHoldCounter;
        }
        else
        {
            peakHold = peakHold * 0.92f;
        }

        // Snap negligible values to zero so an idle meter stops repainting entirely.
        if (displayLevel < 1.0e-4f) displayLevel = 0.0f;
        if (peakHold     < 1.0e-4f) peakHold     = 0.0f;

        if (displayLevel != paintedLevel || peakHold != paintedPeak)
        {
            paintedLevel = displayLevel;
            paintedPeak  = peakHold;
            repaint();
        }
    }
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

void SectionHeaderComponent::setHorizontalMode(bool horizontal)
{
    horizontalMode = horizontal;
    upButton  .setButtonText(horizontal ? kLeft  : kUp);
    downButton.setButtonText(horizontal ? kRight : kDown);
}

// ── ChainSlotComponent ────────────────────────────────────────────────────────

ChainSlotComponent::ChainSlotComponent(ChainListModel& ownerModel) : model(ownerModel)
{
    volumeControl.onGainChanged = [this](float gain)
    {
        if (model.onSlotGainChanged)
            model.onSlotGainChanged(slotIndex, gain);
    };
    addAndMakeVisible(volumeControl);
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

    volumeControl.setValue(VolumeControl::fromLinear(info.postGain));

    repaint();
}

void ChainSlotComponent::resized()
{
    auto area = getLocalBounds().reduced(kRightMargin, 5);

    volumeControl.setBounds(area.removeFromRight(kVolW)); area.removeFromRight(kBtnGap);

    gripArea = area.removeFromLeft(18);
    textArea = area;

    // Badge rect mirrors the paint layout so mouse hit-testing is accurate.
    const int bSide = textArea.getHeight() - 4;
    badgeRect = textArea.toFloat().removeFromLeft(float(bSide + 8))
                        .withSizeKeepingCentre(float(bSide), float(bSide));
}

void ChainSlotComponent::mouseDown(const juce::MouseEvent& e)
{
    if (!e.mods.isRightButtonDown() && badgeRect.contains(e.position.toFloat()))
    {
        if (model.onBypass) model.onBypass(slotIndex);
        return;
    }

    if (!e.mods.isRightButtonDown())
        if (model.onSelect) model.onSelect(rowIndex);

    if (e.mods.isRightButtonDown())
    {
        const bool hasControl = controlHint.isNotEmpty();

        juce::PopupMenu menu;
        menu.addItem(5, "Open Editor");
        menu.addSeparator();
        menu.addItem(4, "Duplicate");
        menu.addSeparator();
        menu.addItem(1, hasControl ? "Remove Control" : "Learn Control");
        menu.addSeparator();
        menu.addItem(2, "Rename");
        if (hasCustomName)
            menu.addItem(3, "Reset Name");
        menu.addSeparator();
        menu.addItem(6, "Remove");

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [this, hasControl](int result)
            {
                if (result == 1)
                {
                    if (hasControl) { if (model.onRemoveControl) model.onRemoveControl(slotIndex); }
                    else            { if (model.onLearnControl)  model.onLearnControl(slotIndex);  }
                }
                else if (result == 2 && model.onRename)
                    model.onRename(slotIndex);
                else if (result == 3 && model.onResetName)
                    model.onResetName(slotIndex);
                else if (result == 4 && model.onDuplicate)
                    model.onDuplicate(slotIndex);
                else if (result == 5 && model.onEditor)
                    model.onEditor(slotIndex);
                else if (result == 6 && model.onRemove)
                    model.onRemove(slotIndex);
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

void ChainSlotComponent::mouseMove(const juce::MouseEvent& e)
{
    const bool over = badgeRect.contains(e.position.toFloat());
    if (over != badgeHovered)
    {
        badgeHovered = over;
        setMouseCursor(over ? juce::MouseCursor::PointingHandCursor
                            : juce::MouseCursor::NormalCursor);
    }
}

void ChainSlotComponent::mouseExit(const juce::MouseEvent&)
{
    if (badgeHovered)
    {
        badgeHovered = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void ChainSlotComponent::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    const bool isSelected = (model.highlightedRow == rowIndex);

    g.setColour(tf::colour::surface2);
    g.fillRoundedRectangle(r, 6.0f);

    if (isSelected)
    {
        g.setColour(tf::colour::accent.withAlpha(0.13f));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(tf::colour::accent.withAlpha(0.75f));
        g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.5f);
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

    // Control-binding badge — square, side = row height minus a small margin.
    const int bSide = a.getHeight() - 4;   // e.g. 32 px at rowHeight=46
    auto badge = a.removeFromLeft(bSide + 8).toFloat()
                   .withSizeKeepingCentre((float)bSide, (float)bSide);

    if (controlHint.isNotEmpty())
    {
        // Strip "BYP: " / "ACT: " prefix, then MIDI channel suffix, then "Key " prefix.
        const int colonIdx = controlHint.indexOf(": ");
        const juce::String prefix = colonIdx >= 0 ? controlHint.substring(0, colonIdx) : "";
        juce::String label = colonIdx >= 0 ? controlHint.substring(colonIdx + 2) : controlHint;
        const int chanIdx = label.indexOf(" (ch ");
        if (chanIdx >= 0) label = label.substring(0, chanIdx);
        if (label.startsWith("Key ")) label = label.substring(4);

        if (!bypassed)
        {
            // Stomp active: light blue so it reads as "on", not as bypass (amber).
            // Preset active: keep teal accent.
            const juce::Colour activeCol = isPreset ? tf::colour::accent
                                                     : juce::Colour(0xff4a9eff);
            g.setColour(activeCol.withAlpha(0.22f));
            g.fillRoundedRectangle(badge, 4.0f);
            g.setColour(activeCol.withAlpha(0.55f));
            g.drawRoundedRectangle(badge.reduced(0.5f), 4.0f, 0.8f);
            g.setColour(activeCol);
        }
        else
        {
            g.setColour(tf::colour::textDim.withAlpha(0.55f));
            g.drawRoundedRectangle(badge.reduced(0.5f), 4.0f, 0.8f);
            g.setColour(tf::colour::textDim);
        }

        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(label, badge, juce::Justification::centred);
    }
    else
    {
        // No control assigned — badge still acts as bypass toggle.
        // Mirror fill+border style of the assigned case so active/bypass state is clearly visible.
        const juce::Colour activeCol = isPreset ? tf::colour::accent
                                                 : juce::Colour(0xff4a9eff);
        if (!bypassed)
        {
            g.setColour(activeCol.withAlpha(0.22f));
            g.fillRoundedRectangle(badge, 4.0f);
            g.setColour(activeCol.withAlpha(0.55f));
            g.drawRoundedRectangle(badge.reduced(0.5f), 4.0f, 0.8f);
        }
        else
        {
            g.setColour(tf::colour::textDim.withAlpha(0.10f));
            g.fillRoundedRectangle(badge, 4.0f);
            g.setColour(tf::colour::textDim.withAlpha(0.45f));
            g.drawRoundedRectangle(badge.reduced(0.5f), 4.0f, 0.8f);
        }
    }

    a.removeFromLeft(4);

    auto nameArea = a.removeFromTop(a.getHeight() / 2 + 2);

    g.setColour(bypassed ? tf::colour::textDim : tf::colour::text);
    g.setFont(juce::FontOptions(14.5f, bypassed ? juce::Font::plain : juce::Font::bold));
    g.drawText(name, nameArea, juce::Justification::bottomLeft);

    g.setFont(juce::FontOptions(11.5f));
    juce::String sub = "[" + format + "]";
    if (hasCustomName)
        sub += "  " + originalName;
    if (bypassed)
        sub += "  bypassed";
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

// ── SectionColumnComponent ────────────────────────────────────────────────────

SectionColumnComponent::SectionColumnComponent(ChainListModel& ownerModel)
    : model(ownerModel)
{}

void SectionColumnComponent::setRows(int flatRowOffset,
                                      const ChainRow& headerRow,
                                      const juce::Array<ChainRow>& slotRows)
{
    sectionId      = headerRow.sectionId;
    headerRowIndex = flatRowOffset;

    slotIndices.clearQuick();
    for (const auto& r : slotRows)
        slotIndices.add(r.slotIndex);

    if (headerComp == nullptr)
    {
        headerComp = std::make_unique<SectionHeaderComponent>(model);
        headerComp->setHorizontalMode(true);
        addAndMakeVisible(*headerComp);
    }
    headerComp->update(flatRowOffset, headerRow, false);

    const int n = slotRows.size();
    while (slotComps.size() > n)
        slotComps.removeLast();
    while (slotComps.size() < n)
        addAndMakeVisible(slotComps.add(new ChainSlotComponent(model)));

    for (int i = 0; i < n; ++i)
        slotComps[i]->update(flatRowOffset + 1 + i, slotRows.getReference(i), false);

    resized();
}

juce::Component* SectionColumnComponent::getComponentForRow(int row) const
{
    if (headerComp != nullptr && row == headerRowIndex)
        return headerComp.get();
    const int slotIdx = row - headerRowIndex - 1;
    if (slotIdx >= 0 && slotIdx < slotComps.size())
        return slotComps[slotIdx];
    return nullptr;
}

void SectionColumnComponent::resized()
{
    auto area = getLocalBounds();
    if (headerComp != nullptr)
        headerComp->setBounds(area.removeFromTop(kRowHeight));
    for (auto* slot : slotComps)
        slot->setBounds(area.removeFromTop(kRowHeight));
}

void SectionColumnComponent::paintOverChildren(juce::Graphics& g)
{
    if (dropSlotRow < 0)
        return;

    const int y = (dropSlotRow < slotComps.size())
                ? kRowHeight + dropSlotRow * kRowHeight + (dropBefore ? 0 : kRowHeight)
                : kRowHeight + slotComps.size() * kRowHeight;

    g.setColour(tf::colour::accent);
    g.fillRect(4, y - 1, kColumnWidth - 8, 2);

    juce::Path arrow;
    arrow.addTriangle(4.0f, (float) (y - 4), 4.0f, (float) (y + 4), 10.0f, (float) y);
    g.fillPath(arrow);
}

int SectionColumnComponent::slotIndexFromDescription(const juce::var& description)
{
    const juce::String s = description.toString();
    if (s.startsWith("chain-slot:"))
        return s.fromFirstOccurrenceOf(":", false, false).getIntValue();
    return -1;
}

int SectionColumnComponent::resolveDropTarget(juce::Point<int> pos, bool& outBefore,
                                               int& outSectionIdOverride) const
{
    outSectionIdOverride = -1;
    outBefore = true;

    // Dropping on the header area, or anywhere in an empty column
    if (pos.y < kRowHeight || slotIndices.isEmpty())
    {
        outSectionIdOverride = sectionId;

        if (! slotIndices.isEmpty())
            return slotIndices.getLast() + 1;   // insertion point = end of this section

        // Empty section: find the first slot that belongs to a later section
        const auto& rows = model.getRows();
        bool pastSection = false;
        for (const auto& r : rows)
        {
            if (r.kind == ChainRow::Kind::sectionHeader)
            {
                if (r.sectionId == sectionId) pastSection = true;
                continue;
            }
            if (pastSection && r.kind == ChainRow::Kind::slot)
                return r.slotIndex;
        }
        return model.getSlotCount();
    }

    const int slotRow = juce::jlimit(0, slotIndices.size() - 1,
                                      (pos.y - kRowHeight) / kRowHeight);
    const int midY = kRowHeight + slotRow * kRowHeight + kRowHeight / 2;
    outBefore = (pos.y < midY);
    return slotIndices[slotRow];
}

bool SectionColumnComponent::isInterestedInDragSource(const SourceDetails& details)
{
    return details.description.toString().startsWith("chain-slot:");
}

void SectionColumnComponent::itemDragMove(const SourceDetails& details)
{
    bool before;
    int sectionOverride;
    resolveDropTarget(details.localPosition, before, sectionOverride);

    int newRow;
    bool newBefore;

    if (sectionOverride >= 0)
    {
        newRow    = slotComps.size();   // draw indicator at end of column
        newBefore = false;
    }
    else
    {
        newRow    = juce::jlimit(0, slotIndices.size() - 1,
                                 (details.localPosition.y - kRowHeight) / kRowHeight);
        newBefore = before;
    }

    if (newRow != dropSlotRow || newBefore != dropBefore)
    {
        dropSlotRow = newRow;
        dropBefore  = newBefore;
        repaint();
    }
}

void SectionColumnComponent::itemDragExit(const SourceDetails&)
{
    dropSlotRow = -1;
    repaint();
}

void SectionColumnComponent::itemDropped(const SourceDetails& details)
{
    dropSlotRow = -1;
    repaint();

    const int fromSlot = slotIndexFromDescription(details.description);
    if (fromSlot < 0) return;

    bool before;
    int sectionIdOverride;
    const int toSlot = resolveDropTarget(details.localPosition, before, sectionIdOverride);
    if (toSlot < 0) return;

    int finalTarget;
    if (sectionIdOverride >= 0)
    {
        if      (fromSlot < toSlot) finalTarget = toSlot - 1;
        else if (fromSlot == toSlot) finalTarget = fromSlot;
        else                         finalTarget = toSlot;
    }
    else
    {
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

// ── ChainHorizontalView ───────────────────────────────────────────────────────

ChainHorizontalView::ChainHorizontalView(ChainListModel& ownerModel)
    : model(ownerModel)
{
    viewport.setScrollBarsShown(false, true);    // show horizontal scrollbar when content overflows
    viewport.setViewedComponent(&contentArea, false);
    addAndMakeVisible(viewport);
}

void ChainHorizontalView::resized()
{
    viewport.setBounds(getLocalBounds());
    layoutColumns();
}

void ChainHorizontalView::setRows(const juce::Array<ChainRow>& rows)
{
    // Partition flat rows into per-section groups
    struct SectionData
    {
        ChainRow header;
        juce::Array<ChainRow> slots;
        int flatHeaderIdx = 0;
    };

    juce::Array<SectionData> sections;
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto& r = rows.getReference(i);
        if (r.kind == ChainRow::Kind::sectionHeader)
        {
            SectionData sec;
            sec.header        = r;
            sec.flatHeaderIdx = i;
            sections.add(std::move(sec));
        }
        else if (! sections.isEmpty())
        {
            sections.getReference(sections.size() - 1).slots.add(r);
        }
    }

    const int n = sections.size();

    if (columns.size() != n)
    {
        columns.clear();
        for (int i = 0; i < n; ++i)
        {
            auto* col = columns.add(new SectionColumnComponent(model));
            col->onMovePlugin = [this](int f, int t, int s)
            {
                if (onMovePlugin) onMovePlugin(f, t, s);
            };
            contentArea.addAndMakeVisible(*col);
        }
    }

    for (int i = 0; i < n; ++i)
    {
        const auto& sec = sections.getReference(i);
        columns[i]->setRows(sec.flatHeaderIdx, sec.header, sec.slots);
    }

    layoutColumns();
}

juce::Component* ChainHorizontalView::getComponentForRowNumber(int row) const
{
    for (auto* col : columns)
        if (auto* comp = col->getComponentForRow(row))
            return comp;
    return nullptr;
}

void ChainHorizontalView::layoutColumns()
{
    constexpr int colW = SectionColumnComponent::kColumnWidth;
    constexpr int gap  = 8;

    const int n = columns.size();
    const int contentW = n > 0 ? n * colW + (n - 1) * gap : 0;

    // Content height: at least the viewport height; expand if any column needs more
    int contentH = getHeight();
    for (auto* col : columns)
        contentH = juce::jmax(contentH,
                              SectionColumnComponent::kRowHeight * (1 + col->getNumSlots()));

    contentArea.setSize(juce::jmax(contentW, getWidth()), juce::jmax(contentH, 1));

    for (int i = 0; i < n; ++i)
        columns[i]->setBounds(i * (colW + gap), 0, colW, contentH);
}
