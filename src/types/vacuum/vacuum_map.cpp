#include "src/types/vacuum/vacuum_map.h"

// lvgl.h no longer exports lv_image_cache_drop() (tile_renderer.cpp).
#include <misc/cache/instance/lv_image_cache.h>

#include <algorithm>
#include <cstring>

#include "src/core/power/power_manager.h"
#include "src/network/bridge/bridge_images.h"
#include "src/network/network_manager.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/types/camera/picture_layout.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/ui/screensaver/wallpaper_cover.h"
#include "src/ui/shared/ui_theme.h"

namespace vacuum_map {
namespace {

// A camera map is a still the Bridge loads again at this interval.
constexpr uint8_t kCameraEvery = 10;

struct View {
  lv_obj_t* box = nullptr;    // the square: control surface until a picture
  lv_obj_t* icon = nullptr;   // the placeholder's map icon
  lv_obj_t* image = nullptr;  // created with the first picture
  lv_image_dsc_t* dsc = nullptr;
  String entity;
  uint16_t edge = 0;
  uint16_t radius = 0;
  uint16_t under = 0;  // RGB565 the corners were rounded towards
  char key[bridge_images::kKeyLength + 1] = "";
  bool due = true;
};

std::vector<View*> g_views;

void free_dsc(lv_image_dsc_t*& dsc) {
  if (!dsc) return;
  // LVGL caches decoded images by address (free_media_cover_dsc).
  lv_image_cache_drop(dsc);
  free(const_cast<uint8_t*>(dsc->data));
  free(dsc);
  dsc = nullptr;
}

View* view_of(lv_obj_t* box) {
  for (View* view : g_views) {
    if (view->box == box) return view;
  }
  return nullptr;
}

void delete_cb(lv_event_t* event) {
  View* view = static_cast<View*>(lv_event_get_user_data(event));
  if (!view) return;
  g_views.erase(std::remove(g_views.begin(), g_views.end(), view), g_views.end());
  free_dsc(view->dsc);
  delete view;
}

// The color behind the square: its nearest opaque ancestor (the card).
uint32_t color_behind(lv_obj_t* box) {
  lv_obj_t* host = box ? lv_obj_get_parent(box) : nullptr;
  while (host && lv_obj_get_style_bg_opa(host, LV_PART_MAIN) < LV_OPA_COVER) host = lv_obj_get_parent(host);
  if (!host) host = lv_screen_active();
  return host ? lv_color_to_u32(lv_obj_get_style_bg_color(host, LV_PART_MAIN)) & 0xFFFFFF : 0;
}

bool on_screen(lv_obj_t* box) {
  if (!box || lv_obj_get_screen(box) != lv_screen_active()) return false;
  for (lv_obj_t* obj = box; obj; obj = lv_obj_get_parent(obj)) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return false;
  }
  return true;
}

bool is_camera(const String& entity) { return entity.startsWith("camera."); }

void show(View& view, const bridge_images::Picture& picture) {
  uint16_t w = 0, h = 0;
  uint16_t* pixels =
      image_screensaver_decode_picture(picture.jpeg, picture.length, view.edge, view.edge, w, h);
  if (!pixels || w != view.edge || h != view.edge) {
    free(pixels);
    Serial.printf("[VacuumMap] %s: picture could not be decoded\n", view.entity.c_str());
    return;
  }
  const uint16_t under = camera_picture_layout::rgb565(color_behind(view.box));
  camera_picture_layout::round_corners(pixels, w, h, view.radius, under, wallpaper_cover::rounded_pixel_coverage);
  lv_image_dsc_t* dsc = static_cast<lv_image_dsc_t*>(malloc(sizeof(lv_image_dsc_t)));
  if (!dsc) {
    free(pixels);
    return;
  }
  memset(dsc, 0, sizeof(*dsc));
  dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc->header.cf = LV_COLOR_FORMAT_RGB565;
  dsc->header.w = w;
  dsc->header.h = h;
  dsc->header.stride = static_cast<uint32_t>(w) * 2;
  dsc->data_size = static_cast<uint32_t>(w) * h * 2;
  dsc->data = reinterpret_cast<const uint8_t*>(pixels);
  if (!view.image) {
    view.image = lv_image_create(view.box);
    lv_obj_remove_flag(view.image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(view.image, 0, 0);
    lv_obj_set_size(view.image, view.edge, view.edge);
  }
  lv_image_set_src(view.image, dsc);
  free_dsc(view.dsc);
  view.dsc = dsc;
  view.under = under;
  memcpy(view.key, picture.key, sizeof(view.key));
  if (view.icon) lv_obj_add_flag(view.icon, LV_OBJ_FLAG_HIDDEN);
}

void show_placeholder(View& view) {
  free_dsc(view.dsc);
  view.key[0] = '\0';
  if (view.image) {
    lv_obj_delete(view.image);
    view.image = nullptr;
  }
  if (view.icon) lv_obj_remove_flag(view.icon, LV_OBJ_FLAG_HIDDEN);
}

// The corners follow the color behind the square at once (a design change).
void follow_corners(View& view) {
  if (!view.dsc || !view.image) return;
  const uint16_t under = camera_picture_layout::rgb565(color_behind(view.box));
  if (under == view.under) return;
  camera_picture_layout::reround_corners(reinterpret_cast<uint16_t*>(const_cast<uint8_t*>(view.dsc->data)),
                                         view.edge, view.edge, view.radius, view.under, under,
                                         wallpaper_cover::rounded_pixel_coverage);
  view.under = under;
  lv_obj_invalidate(view.image);
}

}  // namespace

lv_obj_t* create(lv_obj_t* parent, int x, int y, int edge, int radius) {
  if (!parent || edge < 16) return nullptr;
  lv_obj_t* box = lv_obj_create(parent);
  // The placeholder sits on the card's control surface (the disc's fill,
  // tile_icon_source::refresh_controls), like the level bar's track.
  tile_icon_disc::mark_surface(box);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_shadow_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_set_style_radius(box, radius, 0);
  lv_obj_set_style_clip_corner(box, false, 0);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(box, x, y);
  lv_obj_set_size(box, edge, edge);
  View* view = new View();
  view->box = box;
  view->edge = static_cast<uint16_t>(edge);
  view->radius = static_cast<uint16_t>(std::max(0, radius));
  if (FONT_MDI_ICONS) {
    view->icon = lv_label_create(box);
    lv_obj_set_style_text_font(view->icon, FONT_MDI_ICONS, 0);
    lv_obj_set_style_text_color(view->icon, lv_color_hex(ui_theme::text()), 0);
    lv_obj_set_style_text_opa(view->icon, LV_OPA_60, 0);
    lv_label_set_text(view->icon, getMdiChar("map-outline").c_str());
    lv_obj_center(view->icon);
  }
  g_views.push_back(view);
  lv_obj_add_event_cb(box, delete_cb, LV_EVENT_DELETE, view);
  return box;
}

void set_entity(lv_obj_t* box, const char* entity) {
  View* view = view_of(box);
  if (!view) return;
  const String next = entity ? String(entity) : String();
  if (next.equalsIgnoreCase(view->entity)) return;
  view->entity = next;
  show_placeholder(*view);
  view->due = next.length() > 0;
}

void arrived(const char* entity_id) {
  if (!entity_id) return;
  for (View* view : g_views) {
    if (view->entity.equalsIgnoreCase(entity_id)) view->due = true;
  }
}

void collect_topics(std::vector<String>& entities, std::vector<String>& suffixes) {
  if (!networkManager.linkConfigured()) return;
  const bool awake = !powerManager.isInSleep() && !image_screensaver_covers_ui() && !camera_popup_is_busy();
  char suffix[40];
  for (const View* view : g_views) {
    if (!view->entity.length()) continue;
    const bool camera = is_camera(view->entity);
    if (camera && !(awake && on_screen(view->box))) continue;
    if (!bridge_images::pictureSuffix(suffix, sizeof(suffix), view->edge, view->edge, bridge_images::kContain,
                                      camera ? kCameraEvery : 0)) {
      continue;
    }
    entities.push_back(view->entity);
    suffixes.push_back(suffix);
  }
}

void service() {
  for (View* view : g_views) follow_corners(*view);
  for (View* view : g_views) {
    if (!view->due || !view->entity.length()) continue;
    // Off screen nothing decodes; the picture waits for the next pass.
    if (!on_screen(view->box)) continue;
    view->due = false;
    const bridge_images::Picture* picture =
        bridge_images::find(view->entity.c_str(), view->edge, view->edge, bridge_images::kContain);
    if (!picture || strcmp(picture->key, view->key) == 0) continue;
    show(*view, *picture);
    break;
  }
}

}  // namespace vacuum_map
