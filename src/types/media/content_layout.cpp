#include "src/types/media/content_layout.h"
#include "src/types/media/cover_geometry.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"

void set_media_cover_text_layout(MediaTileWidgets& widgets, bool cover_visible) {
  lv_obj_t* parent = widgets.media_title_label
                         ? lv_obj_get_parent(widgets.media_title_label) : nullptr;
  if (!parent) return;
  lv_obj_update_layout(parent);
  const int width = lv_obj_get_content_width(parent);
  const int height = lv_obj_get_content_height(parent);
  const bool has_subtitle = widgets.media_subtitle_label &&
      !lv_obj_has_flag(widgets.media_subtitle_label, LV_OBJ_FLAG_HIDDEN);
  // Alignment offsets are relative to the padded content area. get_y() also
  // includes the parent's padding and would subtract that space twice.
  const int top = widgets.cover_clip ? lv_obj_get_style_y(widgets.cover_clip, LV_PART_MAIN) : tile_layout::scale(42);
  lv_obj_t* control = widgets.play_pause_label ? lv_obj_get_parent(widgets.play_pause_label) : nullptr;
  const int footer = control ? lv_obj_get_height(control) -
      lv_obj_get_style_y(control, LV_PART_MAIN) + tile_layout::scale(12) : tile_layout::scale(80);
  const int side = media_layout::cover_side(width, height, top, footer);
  if (widgets.cover_clip && widgets.cover_image) {
    lv_obj_set_size(widgets.cover_clip, side, side);
    lv_obj_set_size(widgets.cover_image, side, side);
  }
  const int text_x = cover_visible ? side + tile_layout::scale(16) : tile_layout::scale(20);
  const int text_width = std::max<int>(1, width - text_x - tile_layout::scale(8));
  int title_height = 0;
  int subtitle_height = 0;
  if (widgets.media_title_label) {
    lv_obj_set_width(widgets.media_title_label, text_width);
    title_height = lv_font_get_line_height(lv_obj_get_style_text_font(widgets.media_title_label, LV_PART_MAIN));
  }
  if (widgets.media_subtitle_label) {
    lv_obj_set_width(widgets.media_subtitle_label, text_width);
    if (has_subtitle) subtitle_height = lv_font_get_line_height(
        lv_obj_get_style_text_font(widgets.media_subtitle_label, LV_PART_MAIN));
  }
  const int gap = has_subtitle ? tile_layout::scale(6) : 0;
  const int block_height = title_height + gap + subtitle_height;
  const int center = cover_visible ? top + side / 2 : (top + height - footer) / 2;
  const int title_y = std::max(top, std::min(center - block_height / 2,
                                          height - footer - block_height));
  if (widgets.media_title_label) {
    lv_obj_align(widgets.media_title_label, LV_ALIGN_TOP_LEFT, text_x, title_y);
  }
  if (widgets.media_subtitle_label) {
    lv_obj_align(widgets.media_subtitle_label, LV_ALIGN_TOP_LEFT,
                 text_x, title_y + title_height + gap);
  }
}
