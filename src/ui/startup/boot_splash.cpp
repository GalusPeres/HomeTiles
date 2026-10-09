#include "src/ui/startup/boot_splash.h"

#include <lvgl.h>

#include "src/core/firmware/firmware_version.h"
#include "src/devices/device.h"
#include "src/fonts/ui_fonts.h"
#include "src/ui/startup/hometiles_logo.h"
#include "src/ui/popups/popup_layout.h"

namespace BootSplash {
namespace {

lv_obj_t* g_overlay = nullptr;

// Use the same logo-image setup as create_hometiles_logo_mark() in
// tab_settings.cpp. That helper is file-local; this only repeats
// lv_image_create() and scaling, without additional logic.
lv_obj_t* create_logo(lv_obj_t* parent, int32_t size) {
  lv_obj_t* img = lv_image_create(parent);
  lv_obj_set_style_pad_all(img, 0, 0);
  lv_obj_set_style_border_width(img, 0, 0);
  lv_obj_set_style_bg_opa(img, LV_OPA_TRANSP, 0);
  lv_image_set_src(img, &hometiles_logo_dsc);
  lv_image_set_antialias(img, true);
  const uint32_t zoom = static_cast<uint32_t>(
      (static_cast<int64_t>(size) * 256) / hometiles_logo_dsc.header.w);
  lv_image_set_scale(img, zoom);
  // See create_hometiles_logo_mark() in tab_settings.cpp: LV_SIZE_CONTENT
  // uses the unscaled source dimensions for lv_image, not its scaled
  // appearance. Without this explicit size, the icon reserves more layout
  // space than it visibly occupies, leaving an invisible margin.
  lv_obj_set_size(img, size, size);
  lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
  return img;
}

}  // namespace

void show() {
  if (g_overlay) return;

  // lv_screen_active() initially has LVGL's light default background;
  // uiManager.buildUI() changes it to black. Since hide() removes the
  // splash before buildUI() runs, set the same background color now to
  // prevent the light default from briefly showing through.
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // The whole screen black with the brand in its middle: no frame and no
  // card (user 2026-10-09; a card at the screen edge looked like a border).
  g_overlay = lv_obj_create(scr);
  lv_obj_set_size(g_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(g_overlay, 0, 0);
  lv_obj_set_style_bg_color(g_overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_opa(g_overlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_overlay, 0, 0);
  lv_obj_set_style_radius(g_overlay, 0, 0);
  lv_obj_set_style_pad_all(g_overlay, 0, 0);
  lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* card = lv_obj_create(g_overlay);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_center(card);
  lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_radius(card, 0, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, popup_layout::scale(24), 0);

  // Use the same branding as the system popup: icon on the left,
  // title and version beside it.
  lv_obj_t* brand = lv_obj_create(card);
  lv_obj_set_style_bg_opa(brand, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(brand, 0, 0);
  lv_obj_set_style_pad_all(brand, 0, 0);
  lv_obj_clear_flag(brand, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(brand, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(brand, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(brand, popup_layout::scale(18), 0);
  create_logo(brand, popup_layout::scale(100));

  lv_obj_t* brand_text = lv_obj_create(brand);
  lv_obj_set_style_bg_opa(brand_text, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(brand_text, 0, 0);
  lv_obj_set_style_pad_all(brand_text, 0, 0);
  lv_obj_clear_flag(brand_text, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(brand_text, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(brand_text, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(brand_text, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(brand_text, popup_layout::scale(2), 0);

  lv_obj_t* title = lv_label_create(brand_text);
  lv_label_set_text(title, "HomeTiles");
  lv_obj_set_style_text_font(title, popup_layout::font40(), 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);

  lv_obj_t* version_caption = lv_label_create(brand_text);
  lv_label_set_text(version_caption, FW_VERSION);
  lv_obj_set_style_text_font(version_caption, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(version_caption, lv_color_hex(0xA8A8A8), 0);

  lv_obj_t* device_label = lv_label_create(card);
  lv_label_set_text(device_label, Device::displayName());
  lv_obj_set_style_text_font(device_label, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(device_label, lv_color_hex(0xA8A8A8), 0);
}

void hide() {
  if (!g_overlay) return;
  lv_obj_del(g_overlay);
  g_overlay = nullptr;
}

}  // namespace BootSplash
