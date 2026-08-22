#include "PedalboardComponent.h"
#include "../../DSP/PedalboardOrder.h"

namespace
{
    // Pinned ids never appear as strip tiles and are never offered in the
    // "+ Add Pedal" menu -- their chain position is fixed by
    // fullOrderForProcessor() instead (Input first, Gate right after it,
    // Output last), and they're rendered in the editor's bottom bar.
    bool isPinned (const juce::String& id)
    {
        return id == "inputGain" || id == "outputGain" || id == "noiseGate";
    }

    enum class PedalMenuCategory { compressor, driveFuzz, ampCab, modulation, reverb, other };

    PedalMenuCategory categoryFor (const juce::String& id)
    {
        if (id == "compressor" || id == "lowDynamic")
            return PedalMenuCategory::compressor;
        if (id == "klon" || id == "ts9" || id == "fangs" || id == "bison" || id == "growl")
            return PedalMenuCategory::driveFuzz;
        if (id == "amp" || id == "cab")
            return PedalMenuCategory::ampCab;
        if (id == "tremolo" || id == "chorus" || id == "dimChorus"
            || id == "dimBbd" || id == "jcChorus")
            return PedalMenuCategory::modulation;
        if (id == "delay" || id == "spaceEcho" || id == "reverb" || id == "spring")
            return PedalMenuCategory::reverb;
        // Tape, Desk, Redface, EQ and the Parallel utility intentionally
        // live here rather than being mixed into unrelated effect types.
        return PedalMenuCategory::other;
    }

    void addCategorisedPedals (juce::PopupMenu& root,
                               const juce::StringArray& availableIds,
                               std::vector<juce::String>& menuIds)
    {
        struct Category { const char* name; PedalMenuCategory type; };
        static constexpr Category categories[] {
            { "Compressor",  PedalMenuCategory::compressor },
            { "OD / Fuzz",   PedalMenuCategory::driveFuzz },
            { "Amp / Cab",   PedalMenuCategory::ampCab },
            { "Modulation",  PedalMenuCategory::modulation },
            { "Reverb",      PedalMenuCategory::reverb },
            { "Other",       PedalMenuCategory::other }
        };

        int itemId = 1;
        for (const auto& category : categories)
        {
            juce::PopupMenu submenu;
            for (const auto& id : availableIds)
            {
                if (categoryFor (id) != category.type)
                    continue;
                submenu.addItem (itemId++, PedalTileFactory::displayNameFor (id));
                menuIds.push_back (id);
            }
            if (submenu.getNumItems() > 0)
                root.addSubMenu (category.name, submenu);
        }
    }
}

PedalboardComponent::PedalboardComponent (ThreadlineAudioProcessor& processorIn)
    : processor (processorIn)
{
    viewport.setViewedComponent (&boardContent, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    addButton.setColour (juce::TextButton::buttonColourId, ThreadlineColours::panelDark);
    addButton.setColour (juce::TextButton::textColourOffId, ThreadlineColours::textCream);
    addButton.onClick = [this] { showAddMenu (addButton, middleOrder.size()); };
    boardContent.addAndMakeVisible (addButton);

    leadingInsertButton.onClick = [this] { showAddMenu (leadingInsertButton, 0); };
    boardContent.addAndMakeVisible (leadingInsertButton);

    middleOrder = processor.getActivePedalOrder();
    middleOrder.removeString ("inputGain");
    middleOrder.removeString ("outputGain");
    middleOrder.removeString ("noiseGate");
    rebuildTiles();

    // Re-syncs against the processor's actual order periodically -- that
    // order can change from outside this component entirely (preset load,
    // host automation restoring state), and the strip needs to follow it
    // rather than silently drifting out of sync with what's really
    // processing audio.
    startTimerHz (4);
}

PedalboardComponent::~PedalboardComponent() = default;

void PedalboardComponent::paint (juce::Graphics& g)
{
    paintThreadlineBackground (g, getLocalBounds());
}

void PedalboardComponent::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutTiles();
}

void PedalboardComponent::timerCallback()
{
    if (draggedTile != nullptr)
        return; // never yank the strip out from under an in-progress drag

    // The moment a pedal is picked into the Parallel box's Slot A/Slot B
    // (see ParallelTile), pull it out of the main strip automatically if
    // it's still sitting there as its own tile -- the user shouldn't have
    // to manually remove it first for it to become pickable.
    bool removedForParallel = false;
    for (auto* paramId : { "parallelSlotA", "parallelSlotB" })
    {
        const auto choiceIndex = (int) processor.apvts.getRawParameterValue (paramId)->load();
        const auto& ids = PedalboardOrder::parallelSlotChoiceIds();
        const auto idx = choiceIndex - 1;
        if (choiceIndex > 0 && idx >= 0 && idx < ids.size() && middleOrder.contains (ids[idx]))
        {
            middleOrder.removeString (ids[idx]);
            removedForParallel = true;
        }
    }
    if (removedForParallel)
    {
        publishOrder();
        rebuildTiles();
        return;
    }

    auto liveMiddle = processor.getActivePedalOrder();
    liveMiddle.removeString ("inputGain");
    liveMiddle.removeString ("outputGain");
    liveMiddle.removeString ("noiseGate");
    if (liveMiddle != middleOrder)
    {
        middleOrder = liveMiddle;
        rebuildTiles();
    }
}

juce::StringArray PedalboardComponent::fullOrderForProcessor() const
{
    juce::StringArray full;
    full.add ("inputGain");
    full.add ("noiseGate");
    full.addArray (middleOrder);
    full.add ("outputGain");
    return full;
}

void PedalboardComponent::publishOrder()
{
    processor.setActivePedalOrder (fullOrderForProcessor());
}

void PedalboardComponent::rebuildTiles()
{
    tiles.clear();
    for (auto& id : middleOrder)
    {
        auto tile = PedalTileFactory::createTile (id, processor);
        if (tile == nullptr)
            continue;
        auto* tilePtr = tile.get();
        tile->onRemoveClicked = [this] (const juce::String& removedId) { removePedal (removedId); };
        tile->onNameClicked = [this, tilePtr, id] { showSwapMenu (tilePtr->getTitleMenuAnchor(), id); };
        tile->onDragStart = [this] (PedalTileComponent& t) { handleDragStart (t); };
        tile->onDragTo = [this] (PedalTileComponent& t, int x) { handleDragTo (t, x); };
        tile->onDragEnd = [this] (PedalTileComponent& t) { handleDragEnd (t); };
        boardContent.addAndMakeVisible (*tile);
        tiles.push_back (std::move (tile));
    }

    insertButtons.clear();
    for (int i = 0; i + 1 < (int) tiles.size(); ++i)
    {
        auto insertButton = std::make_unique<InsertPedalButton>();
        auto* buttonPtr = insertButton.get();
        const auto insertIndex = i + 1;
        insertButton->onClick = [this, buttonPtr, insertIndex] { showAddMenu (*buttonPtr, insertIndex); };
        boardContent.addAndMakeVisible (*insertButton);
        insertButtons.push_back (std::move (insertButton));
    }

    layoutTiles();
}

void PedalboardComponent::layoutTiles()
{
    // Fixed, portrait, real-stompbox-like proportions -- tiles sit at a
    // constant height regardless of the window's height (unlike a tile
    // stretched to fill the whole window), with some breathing room above
    // and below so the strip reads as pedals sitting on a board rather
    // than a full-bleed panel. A tile can opt into a taller-than-standard
    // height via getPreferredHeight() (only ParallelTile does, since it
    // embeds two whole nested pedal tiles) -- every tile still top-aligns
    // at the same `y`, like differently-sized units sitting on a real
    // pedalboard; only a taller tile's own bottom edge extends further
    // down, and the strip's scroll content grows to fit whichever tile is
    // tallest.
    const auto y = tileTopMargin;
    // Leaves room for leadingInsertButton before the first tile, mirroring
    // the gap every insertButtons[i] gets between tiles.
    int x = tileMargin + tileGap;
    leadingInsertButton.setBounds (tileMargin + tileGap / 2 - insertButtonSize / 2,
                                    y + tileHeight / 2 - insertButtonSize / 2, insertButtonSize, insertButtonSize);
    int maxBottom = y + tileHeight;
    for (size_t i = 0; i < tiles.size(); ++i)
    {
        auto& tile = tiles[i];
        const auto height = tile->getPreferredHeight() > 0 ? tile->getPreferredHeight() : tileHeight;
        if (draggedTile == tile.get())
        {
            // The dragged tile positions itself (see PedalTileComponent::
            // mouseDrag) -- just reserve its width in the flow so the rest
            // of the strip lays out around it.
            x += tile->getWidth() + tileGap;
        }
        else
        {
            tile->setBounds (x, y, tile->getPreferredWidth(), height);
            x += tile->getPreferredWidth() + tileGap;
        }
        maxBottom = juce::jmax (maxBottom, y + height);

        // The insert button after this tile (if any) sits centred on the
        // strip's standard row height, not this tile's own (possibly
        // taller) height, so it stays in a consistent visual row
        // regardless of a tall neighbor -- small and only as tall as
        // itself, not the full tile height, so it can never sit on top of
        // (and steal clicks from) a neighboring tile's remove button or
        // controls near its edge.
        if (i < insertButtons.size())
        {
            const auto gapCentre = x - tileGap / 2;
            insertButtons[i]->setBounds (gapCentre - insertButtonSize / 2, y + tileHeight / 2 - insertButtonSize / 2,
                                         insertButtonSize, insertButtonSize);
        }
    }
    addButton.setBounds (x, y + tileHeight / 2 - insertButtonSize / 2, insertButtonSize, insertButtonSize);
    x += insertButtonSize + trailingMargin;
    boardContent.setSize (juce::jmax (x, viewport.getWidth()), juce::jmax (maxBottom + tileTopMargin, viewport.getHeight()));

    // Always keep insert buttons above every tile, not just the one being
    // dragged (PedalTileComponent::mouseDown raises the dragged tile to
    // front, which -- without this -- could permanently sit above an
    // insert button in the same gap; that button would then never even
    // receive its own mouseEnter to raise itself in response, since the
    // tile on top would intercept the hover first). Re-asserted on every
    // layout rather than relying purely on hover, so a "+" button shows up
    // reliably at rest, not only after some earlier lucky hover order.
    for (auto& button : insertButtons)
        button->toFront (false);
    leadingInsertButton.toFront (false);
}

void PedalboardComponent::showAddMenu (juce::Component& anchor, int insertIndex)
{
    // Pedals currently parked in the Parallel box's Slot A/Slot B (see
    // ParallelTile) are excluded here too -- a pedal id must never be live
    // in two places on the board at once (see ParallelNode.h).
    auto parkedInParallel = [this] (const char* paramId)
    {
        const auto choiceIndex = (int) processor.apvts.getRawParameterValue (paramId)->load();
        const auto& ids = PedalboardOrder::parallelSlotChoiceIds();
        const auto idx = choiceIndex - 1;
        return choiceIndex > 0 && idx >= 0 && idx < ids.size() ? ids[idx] : juce::String();
    };
    const auto slotA = parkedInParallel ("parallelSlotA");
    const auto slotB = parkedInParallel ("parallelSlotB");

    const auto& allIds = processor.getAllPedalIds();
    juce::PopupMenu menu;
    std::vector<juce::String> menuIds;
    juce::StringArray availableIds;
    for (auto& id : allIds)
    {
        if (isPinned (id) || middleOrder.contains (id) || id == slotA || id == slotB)
            continue;
        availableIds.add (id);
    }
    addCategorisedPedals (menu, availableIds, menuIds);
    if (menuIds.empty())
    {
        menu.addItem (1, "All pedals already on board", false, false);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor)
            .withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::downwards));
        return;
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor)
        .withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::downwards),
        [safe = juce::Component::SafePointer<PedalboardComponent> (this), menuIds, insertIndex] (int result)
        {
            if (safe == nullptr || result <= 0 || result > (int) menuIds.size())
                return;
            const auto clampedIndex = juce::jlimit (0, safe->middleOrder.size(), insertIndex);
            safe->middleOrder.insert (clampedIndex, menuIds[(size_t) result - 1]);
            safe->publishOrder();
            safe->rebuildTiles();
        });
}

void PedalboardComponent::showSwapMenu (juce::Component& anchor, const juce::String& pedalId)
{
    // Same "not already live elsewhere on the board" exclusion as
    // showAddMenu(), except pedalId itself stays in the list -- it's the
    // pedal being replaced, not a duplicate.
    auto parkedInParallel = [this] (const char* paramId)
    {
        const auto choiceIndex = (int) processor.apvts.getRawParameterValue (paramId)->load();
        const auto& ids = PedalboardOrder::parallelSlotChoiceIds();
        const auto idx = choiceIndex - 1;
        return choiceIndex > 0 && idx >= 0 && idx < ids.size() ? ids[idx] : juce::String();
    };
    const auto slotA = parkedInParallel ("parallelSlotA");
    const auto slotB = parkedInParallel ("parallelSlotB");

    const auto& allIds = processor.getAllPedalIds();
    juce::PopupMenu menu;
    std::vector<juce::String> menuIds;
    juce::StringArray availableIds;
    for (auto& id : allIds)
    {
        if (id != pedalId && (isPinned (id) || middleOrder.contains (id) || id == slotA || id == slotB))
            continue;
        availableIds.add (id);
    }
    addCategorisedPedals (menu, availableIds, menuIds);
    if (menuIds.empty())
    {
        menu.addItem (1, "No other pedals available", false, false);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor)
            .withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::downwards));
        return;
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor)
        .withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::downwards),
        [safe = juce::Component::SafePointer<PedalboardComponent> (this), menuIds, pedalId] (int result)
        {
            if (safe == nullptr || result <= 0 || result > (int) menuIds.size())
                return;
            const auto chosenId = menuIds[(size_t) result - 1];
            if (chosenId == pedalId)
                return;
            const auto index = safe->middleOrder.indexOf (pedalId);
            if (index < 0)
                return;
            safe->middleOrder.set (index, chosenId);
            safe->publishOrder();
            safe->rebuildTiles();
        });
}

void PedalboardComponent::removePedal (const juce::String& id)
{
    middleOrder.removeString (id);
    if (id == "parallel")
    {
        // Free whatever pedals were parked in the box's two slots back into
        // the normal pool -- otherwise they'd stay invisible forever
        // (excluded from the strip because a slot still names them, but
        // never processed because the box itself is gone).
        for (auto* paramId : { "parallelSlotA", "parallelSlotB" })
            if (auto* parameter = processor.apvts.getParameter (paramId))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (0.0f));
    }
    publishOrder();
    // Deferred: this runs from inside the removed tile's own remove-button
    // onClick callback, which JUCE is still executing on that (about-to-be-
    // destroyed) button. rebuildTiles() -> tiles.clear() would destroy it
    // synchronously out from under that in-progress callback, so the actual
    // rebuild has to happen after the current callback has fully unwound.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PedalboardComponent> (this)]
    {
        if (safe != nullptr)
            safe->rebuildTiles();
    });
}

void PedalboardComponent::handleDragStart (PedalTileComponent& tile)
{
    draggedTile = &tile;
}

void PedalboardComponent::handleDragTo (PedalTileComponent& tile, int newX)
{
    juce::ignoreUnused (newX);
    const auto myIt = std::find_if (tiles.begin(), tiles.end(),
        [&] (const std::unique_ptr<PedalTileComponent>& t) { return t.get() == &tile; });
    if (myIt == tiles.end())
        return;
    const auto myIndex = (int) std::distance (tiles.begin(), myIt);
    const auto myCentre = tile.getX() + tile.getWidth() / 2;

    if (myIndex > 0)
    {
        auto& left = tiles[(size_t) myIndex - 1];
        if (myCentre < left->getX() + left->getWidth() / 2)
        {
            std::swap (tiles[(size_t) myIndex - 1], tiles[(size_t) myIndex]);
            const auto tmp = middleOrder[myIndex - 1];
            middleOrder.set (myIndex - 1, middleOrder[myIndex]);
            middleOrder.set (myIndex, tmp);
            layoutTiles();
            return;
        }
    }
    if (myIndex < (int) tiles.size() - 1)
    {
        auto& right = tiles[(size_t) myIndex + 1];
        if (myCentre > right->getX() + right->getWidth() / 2)
        {
            std::swap (tiles[(size_t) myIndex], tiles[(size_t) myIndex + 1]);
            const auto tmp = middleOrder[myIndex];
            middleOrder.set (myIndex, middleOrder[myIndex + 1]);
            middleOrder.set (myIndex + 1, tmp);
            layoutTiles();
            return;
        }
    }
}

void PedalboardComponent::handleDragEnd (PedalTileComponent&)
{
    draggedTile = nullptr;
    layoutTiles();
    publishOrder();
}
