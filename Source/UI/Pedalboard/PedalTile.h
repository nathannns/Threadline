#pragma once

#include <JuceHeader.h>
#include "../SectionBuilder.h"

// One knob + caption + APVTS wiring, plain-slider version of SectionBuilder's
// KnobUI (no PhotoKnob image) -- the reusable unit every tile body is built
// from.
struct TileKnob
{
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

struct TileCombo
{
    juce::ComboBox box;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

struct TileToggle
{
    juce::ToggleButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

inline std::unique_ptr<TileKnob> makeTileKnob (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
                                                const juce::String& paramId, const juce::String& labelText)
{
    auto knob = std::make_unique<TileKnob>();
    // The board scrolls horizontally with a two-finger trackpad swipe --
    // without this, that same gesture would get eaten by whichever knob
    // happens to be under the cursor and nudge its value instead.
    knob->slider.setScrollWheelEnabled (false);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 15);
    knob->slider.setColour (juce::Slider::textBoxTextColourId, ThreadlineColours::textCream);
    knob->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    knob->label.setText (labelText, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setFont (juce::FontOptions (11.0f));
    knob->label.setColour (juce::Label::textColourId, ThreadlineColours::textDim);
    parent.addAndMakeVisible (knob->slider);
    parent.addAndMakeVisible (knob->label);
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, knob->slider);
    return knob;
}

inline std::unique_ptr<TileCombo> makeTileCombo (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
                                                  const juce::String& paramId, const juce::String& labelText,
                                                  const juce::StringArray& choices)
{
    auto combo = std::make_unique<TileCombo>();
    for (int i = 0; i < choices.size(); ++i)
        combo->box.addItem (choices[i], i + 1);
    combo->box.setColour (juce::ComboBox::backgroundColourId, ThreadlineColours::panelDark);
    combo->box.setColour (juce::ComboBox::textColourId, ThreadlineColours::textCream);
    combo->box.setColour (juce::ComboBox::outlineColourId, ThreadlineColours::cardBorder);
    combo->label.setText (labelText, juce::dontSendNotification);
    combo->label.setJustificationType (juce::Justification::centred);
    combo->label.setFont (juce::FontOptions (11.0f));
    combo->label.setColour (juce::Label::textColourId, ThreadlineColours::textDim);
    parent.addAndMakeVisible (combo->box);
    parent.addAndMakeVisible (combo->label);
    combo->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, paramId, combo->box);
    return combo;
}

inline std::unique_ptr<TileToggle> makeTileToggle (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
                                                    const juce::String& paramId, const juce::String& text)
{
    auto toggle = std::make_unique<TileToggle>();
    toggle->button.setButtonText (text);
    parent.addAndMakeVisible (toggle->button);
    toggle->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramId, toggle->button);
    return toggle;
}

// Lays out a row (optionally wrapped into `columns` columns) of knobs across
// `area`, each getting a caption above its slider.
inline void layoutTileKnobRow (std::vector<std::unique_ptr<TileKnob>>& knobs, juce::Rectangle<int> area, int columns = -1)
{
    if (knobs.empty())
        return;
    const auto count = (int) knobs.size();
    const auto cols = columns > 0 ? columns : count;
    const auto rows = (count + cols - 1) / cols;
    const auto cellW = area.getWidth() / juce::jmax (1, cols);
    const auto cellH = area.getHeight() / juce::jmax (1, rows);
    for (int i = 0; i < count; ++i)
    {
        juce::Rectangle<int> cell (area.getX() + (i % cols) * cellW, area.getY() + (i / cols) * cellH, cellW, cellH);
        auto inner = cell.reduced (4, 2);
        knobs[(size_t) i]->label.setBounds (inner.removeFromTop (14));
        knobs[(size_t) i]->slider.setBounds (inner);
    }
}

// Shared chrome every pedal tile gets: a header (name + drag handle + bypass
// toggle + remove button) and a body area subclasses fill via resizedBody().
// Reordering/removal are handled by the owning PedalboardComponent, which
// this component only reports raw mouse events / a click up to via the
// on... callbacks below -- it holds no opinion on ordering itself.
class PedalTileComponent : public juce::Component
{
public:
    PedalTileComponent (juce::AudioProcessorValueTreeState& apvtsIn, juce::String idIn,
                         const juce::String& displayName, const juce::String& toggleParamId)
        : apvts (apvtsIn), pedalId (std::move (idIn))
    {
        nameLabel.setText (displayName, juce::dontSendNotification);
        nameLabel.setJustificationType (juce::Justification::centred);
        nameLabel.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        nameLabel.setColour (juce::Label::textColourId, ThreadlineColours::textCream);
        nameLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (nameLabel);

        removeButton.setButtonText ("X");
        removeButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a1f19));
        removeButton.setColour (juce::TextButton::textColourOffId, ThreadlineColours::textDim);
        removeButton.onClick = [this] { if (onRemoveClicked) onRemoveClicked (pedalId); };
        addAndMakeVisible (removeButton);

        hasToggle = toggleParamId.isNotEmpty();
        if (hasToggle)
        {
            bypassToggle.setButtonText ("On");
            addAndMakeVisible (bypassToggle);
            bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts, toggleParamId, bypassToggle);
        }
    }

    ~PedalTileComponent() override = default;

    const juce::String& getPedalId() const noexcept { return pedalId; }
    virtual int getPreferredWidth() const { return 190; }

    std::function<void (const juce::String&)> onRemoveClicked;
    std::function<void (PedalTileComponent&)> onDragStart;
    std::function<void (PedalTileComponent&, int newXInParent)> onDragTo;
    std::function<void (PedalTileComponent&)> onDragEnd;

    void paint (juce::Graphics& g) override
    {
        paintCard (g, getLocalBounds(), 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRect (juce::Rectangle<int> (0, 0, getWidth(), headerHeight).reduced (1));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (headerHeight);
        removeButton.setBounds (header.removeFromRight (22).reduced (3));
        if (hasToggle)
            bypassToggle.setBounds (header.removeFromRight (46).reduced (3, 4));
        nameLabel.setBounds (header);
        resizedBody (area.reduced (6, 4));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Anywhere on the tile's own surface is a drag handle -- every
        // actual control (knob, combo, toggle, button) is a child
        // component that already captures its own clicks first, so this
        // only ever fires on genuinely empty space, not on top of a knob.
        dragging = true;
        dragMouseDownScreenX = e.getScreenX();
        dragStartComponentX = getX();
        toFront (false);
        if (onDragStart) onDragStart (*this);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        const auto newX = dragStartComponentX + (e.getScreenX() - dragMouseDownScreenX);
        setTopLeftPosition (newX, getY());
        if (onDragTo) onDragTo (*this, newX);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging)
            return;
        dragging = false;
        if (onDragEnd) onDragEnd (*this);
    }

protected:
    virtual void resizedBody (juce::Rectangle<int> body) = 0;

    juce::AudioProcessorValueTreeState& apvts;
    static constexpr int headerHeight = 24;

private:
    juce::String pedalId;
    juce::Label nameLabel;
    juce::TextButton removeButton;
    juce::ToggleButton bypassToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    bool hasToggle = false;

    bool dragging = false;
    int dragMouseDownScreenX = 0, dragStartComponentX = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalTileComponent)
};
