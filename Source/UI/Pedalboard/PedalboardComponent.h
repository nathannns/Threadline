#pragma once

#include "PedalTileFactory.h"

// The flat, horizontally-scrolling pedalboard strip -- replaces the old
// 4-tab UI. Owns one PedalTileComponent per active *reorderable* pedal id
// (built via PedalTileFactory), a trailing "+ Add Pedal" button, and
// hand-rolled drag-to-reorder (no DragAndDropContainer -- reordering only
// ever happens within this one strip, so a simpler self-contained
// swap-on-cross interaction is enough).
//
// "inputGain"/"noiseGate"/"outputGain" are NOT part of this strip --
// they're pinned permanently (Input first, Gate right after it, Output
// last) and rendered separately, in the editor's bottom bar (see
// PluginEditor). `middleOrder` holds only the other (up to 10) reorderable
// ids; every publish to the processor wraps it as {"inputGain",
// "noiseGate", ...middleOrder, "outputGain"} so the DSP's actual active
// order (see PedalChainRunner.h) always has them fixed regardless of what
// the user drags around in between.
//
// A periodic timer re-syncs `middleOrder`/the tile strip against
// processor.getActivePedalOrder() -- the processor's order can change out
// from under this component (preset load, host automation), and the
// strip needs to reflect that rather than silently going stale.
class PedalboardComponent : public juce::Component, private juce::Timer
{
public:
    explicit PedalboardComponent (ThreadlineAudioProcessor& processor);
    ~PedalboardComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // A "+" that's invisible until the mouse hovers over it -- sits in the
    // gap between two neighboring tiles (or before the first / after the
    // last one) so a pedal can be inserted at a specific position, not
    // just appended at the end.
    //
    // Hover is tracked explicitly (mouseEnter/mouseExit -> a plain bool),
    // not via Button's own shouldDrawButtonAsHighlighted() -- and the
    // button is marked setAlwaysOnTop(true), which makes JUCE hit-test and
    // paint it above every sibling unconditionally, regardless of z-order.
    // The gap is narrower than this button (see tileGap) since a neighbor
    // can be much wider now (ParallelTile), so this button always overlaps
    // a tile's own bounds a little either way; alwaysOnTop is what makes
    // that overlap harmless instead of a race over which sibling's own
    // mouseDown/toFront() happened to win most recently.
    class InsertPedalButton : public juce::Button
    {
    public:
        InsertPedalButton() : juce::Button ("Insert pedal here")
        {
            setTooltip ("Insert a pedal here");
            setAlwaysOnTop (true);
        }

        void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }

        void paintButton (juce::Graphics& g, bool /*highlighted*/, bool down) override
        {
            if (! hovering && ! down)
                return;
            auto bounds = getLocalBounds().withSizeKeepingCentre (
                juce::jmin (getWidth(), 34), juce::jmin (getWidth(), 34)).toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (bounds.translated (0.0f, 2.0f));
            g.setColour (down ? ThreadlineColours::accent : ThreadlineColours::accentBright);
            g.fillEllipse (bounds);
            g.setColour (juce::Colour (0xff18120f));
            const auto c = bounds.getCentre();
            const auto r = bounds.getWidth() * 0.28f;
            g.drawLine (c.x - r, c.y, c.x + r, c.y, 2.6f);
            g.drawLine (c.x, c.y - r, c.x, c.y + r, 2.6f);
        }

    private:
        bool hovering = false;
    };

    void timerCallback() override;
    void rebuildTiles();
    void layoutTiles();
    void showAddMenu (juce::Component& anchor, int insertIndex);
    void removePedal (const juce::String& id);
    void publishOrder();
    juce::StringArray fullOrderForProcessor() const;

    void handleDragStart (PedalTileComponent&);
    void handleDragTo (PedalTileComponent&, int newXInParent);
    void handleDragEnd (PedalTileComponent&);

    ThreadlineAudioProcessor& processor;
    juce::Viewport viewport;
    juce::Component boardContent;
    std::vector<std::unique_ptr<PedalTileComponent>> tiles;
    // One fewer than `tiles` -- insertButtons[i] sits in the gap between
    // tiles[i] and tiles[i+1].
    std::vector<std::unique_ptr<InsertPedalButton>> insertButtons;
    // Mirrors an insertButtons[i] but sits before the very first tile
    // (insertIndex 0) -- built once in the constructor rather than in
    // rebuildTiles() since, unlike the others, its position doesn't depend
    // on how many tiles currently exist.
    InsertPedalButton leadingInsertButton;
    juce::StringArray middleOrder; // excludes "inputGain"/"outputGain"
    juce::TextButton addButton { "+" };

    PedalTileComponent* draggedTile = nullptr;

    // Halved again from 24px -- InsertPedalButton (40px) already overlaps
    // its neighboring tiles' own bounds regardless of gap size, so it
    // relies entirely on setAlwaysOnTop(true) rather than gap width to
    // stay reliably hoverable/clickable (see InsertPedalButton).
    static constexpr int tileGap = 12;
    static constexpr int tileMargin = 16;
    // Fixed portrait stompbox proportions -- see layoutTiles().
    static constexpr int tileHeight = 375;
    static constexpr int tileTopMargin = 24;
    static constexpr int insertButtonSize = 40;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalboardComponent)
};
