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
    // gap between two neighboring tiles (or after the last one) so a pedal
    // can be inserted at a specific position, not just appended at the
    // end. `Button::paintButton`'s own `highlighted`/`down` flags already
    // track hover/press state, so no separate mouseEnter/mouseExit
    // bookkeeping is needed here.
    class InsertPedalButton : public juce::Button
    {
    public:
        InsertPedalButton() : juce::Button ("Insert pedal here") { setTooltip ("Insert a pedal here"); }

        // Guarantees this sits above whichever neighbouring tile it
        // overlaps while hovered -- without this, a tile raised to front
        // during its own drag (see PedalTileComponent::mouseDown) could sit
        // above an insert button occupying the same gap, silently eating
        // hover/click even though the button is still fully visible-ish
        // underneath.
        void mouseEnter (const juce::MouseEvent&) override { toFront (false); }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            if (! highlighted && ! down)
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

    // Wide enough to fully contain an insertButtonSize (40px) button with
    // room to spare either side, so it never overlaps a neighboring tile's
    // own bounds -- the previous 14px gap forced the button to spill ~13px
    // into each neighbor, which made whichever tile's own mouseDown/drag
    // handling happened to be on top intercept the hover before the button
    // itself ever saw it. Removing the overlap at its root, rather than
    // only patching z-order after the fact (still done below too, as a
    // second line of defence), is what makes the hover-to-reveal actually
    // reliable.
    static constexpr int tileGap = 48;
    static constexpr int tileMargin = 16;
    // Fixed portrait stompbox proportions -- see layoutTiles().
    static constexpr int tileHeight = 400;
    static constexpr int tileTopMargin = 24;
    static constexpr int insertButtonSize = 40;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalboardComponent)
};
