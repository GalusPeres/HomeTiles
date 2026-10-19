#pragma once

#include "src/ui/popups/popup_layout.h"

// The Media popup's geometry (user 2026-10-19, build/design-mockups/media-player
// and the simulator build): every distance around the controls is the card's
// left edge L (the head disc's left): from the bottom, the volume row L above
// the card's bottom, play L over the volume knob, the time knob L over play;
// with the cover left also the head disc to the cover, the cover to the title
// and the cover to the time knob. The cover takes what is left. Shared with
// the Bridge picture subscription (bridge_images.cpp coverEdge), so the cover
// arrives in exactly the size the popup shows.
namespace media_popup_layout {

// Card coordinates (the card's own top left, not its padded content box).
constexpr int kEdge = popup_layout::kCardPad + popup_layout::kHeaderIconX;
constexpr int kDisc = popup_layout::kHeaderIconDiscSize;
constexpr int kHeadTop = popup_layout::kHeaderCenterY - kDisc / 2;
constexpr int kButton = popup_layout::scale(78);
constexpr int kStep = popup_layout::scale(116);
constexpr int kSeekHeight = popup_layout::scale(10);
constexpr int kSeekKnob = popup_layout::scale(26);
constexpr int kVolumeHeight = popup_layout::scale(16);
constexpr int kVolumeKnob = popup_layout::scale(36);
constexpr int kTimeGap = popup_layout::scale(14);
constexpr int kHeadGap = popup_layout::scale(24);
constexpr int kTextGap = popup_layout::scale(8);
constexpr int kBarsHeight = popup_layout::scale(48);
constexpr int kBarWidth = popup_layout::scale(10);
constexpr int kBarGap = popup_layout::scale(7);
constexpr int kRowHeight = popup_layout::scale(96);
constexpr int kLead = popup_layout::scale(64);
constexpr int kSwitchWidth = popup_layout::scale(72);
constexpr int kSwitchHeight = popup_layout::scale(40);
constexpr int kHeadStep = popup_layout::scale(86);

constexpr int kVolumeTop = popup_layout::kCardHeight - kEdge - kButton;
constexpr int kVolumeKnobTop = kVolumeTop + kButton / 2 - kVolumeKnob / 2;
constexpr int kTransportTop = kVolumeKnobTop - kEdge - kButton;
constexpr int kSeekMid = kTransportTop - kEdge - kSeekKnob / 2;
constexpr int kSeekKnobTop = kSeekMid - kSeekKnob / 2;
constexpr int kLeftCoverTop = kHeadTop + kDisc + kEdge;
// The cover of "Cover links", the larger of the two layouts (V2 399, S3 218).
constexpr int kLeftCoverSide = kSeekKnobTop - kEdge - kLeftCoverTop;
static_assert(kLeftCoverSide > 0, "the Media popup's cover must have room");

}  // namespace media_popup_layout
