#include "src/types/camera/picture.h"

#include <esp_heap_caps.h>
// lvgl.h no longer exports lv_image_cache_drop() (tile_renderer.cpp).
#include <misc/cache/instance/lv_image_cache.h>

#include <algorithm>
#include <vector>

#include "src/core/power/power_manager.h"
#include "src/network/bridge/bridge_images.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/network/network_manager.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/types/vacuum/vacuum_map.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/ui/screensaver/wallpaper_cover.h"
#include "src/ui/shared/title_label.h"
#include "src/ui/shared/ui_surface_style.h"

namespace camera_picture {
namespace {

struct PictureTile {
  lv_obj_t* card = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* title = nullptr;
  // Created with the first picture.
  lv_obj_t* image = nullptr;
  lv_obj_t* circle = nullptr;
  lv_obj_t* pill = nullptr;
  lv_image_dsc_t* dsc = nullptr;
  String entity;
  uint16_t width = 0;  // the card's size = the picture's
  uint16_t height = 0;
  uint8_t fit = bridge_images::kFill;
  uint8_t every = bridge_images::kEveryDefault;
  uint8_t transparency = 0;
  char key[bridge_images::kKeyLength + 1] = "";
  // The color the corners were rounded towards (native RGB565) and with
  // which radius: they follow the color behind the card (follow_corners).
  uint16_t under = 0;
  uint16_t radius = 0;
  // A picture may wait in bridge_images (attached, or one arrived).
  bool due = true;
};

std::vector<PictureTile*> g_tiles;
bool g_topics_due = true;
uint32_t g_topics_ms = 0;
constexpr uint32_t kTopicsEveryMs = 500;

// The pixels of a deleted picture tile, for a while: a Web Admin change
// deletes the card and renders it again, and the new card shows them at once
// instead of the icon look until its picture is decoded again (user
// 2026-10-11). The newest few; the rest and old ones are freed.
struct Parked {
  String entity;
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t fit = bridge_images::kFill;
  char key[bridge_images::kKeyLength + 1] = "";
  lv_image_dsc_t* dsc = nullptr;
  uint16_t under = 0;
  uint16_t radius = 0;
  uint32_t since_ms = 0;
};
std::vector<Parked> g_parked;
constexpr size_t kParkedMax = 2;
constexpr uint32_t kParkedMs = 10000;

void free_dsc(lv_image_dsc_t*& dsc) {
  if (!dsc) return;
  // LVGL caches decoded images by address (free_media_cover_dsc).
  lv_image_cache_drop(dsc);
  free(const_cast<uint8_t*>(dsc->data));
  free(dsc);
  dsc = nullptr;
}

void park(PictureTile& tile) {
  if (!tile.dsc) return;
  if (g_parked.size() >= kParkedMax) {
    free_dsc(g_parked.front().dsc);
    g_parked.erase(g_parked.begin());
  }
  Parked parked;
  parked.entity = tile.entity;
  parked.width = tile.width;
  parked.height = tile.height;
  parked.fit = tile.fit;
  memcpy(parked.key, tile.key, sizeof(parked.key));
  parked.dsc = tile.dsc;
  parked.under = tile.under;
  parked.radius = tile.radius;
  parked.since_ms = millis();
  tile.dsc = nullptr;
  g_parked.push_back(parked);
}

void delete_cb(lv_event_t* event) {
  PictureTile* tile = static_cast<PictureTile*>(lv_event_get_user_data(event));
  if (!tile) return;
  g_tiles.erase(std::remove(g_tiles.begin(), g_tiles.end(), tile), g_tiles.end());
  park(*tile);
  delete tile;
  g_topics_due = true;
}

// The color behind the card: its nearest opaque ancestor (the screen).
uint32_t color_behind(lv_obj_t* card) {
  lv_obj_t* host = card ? lv_obj_get_parent(card) : nullptr;
  while (host && lv_obj_get_style_bg_opa(host, LV_PART_MAIN) < LV_OPA_COVER) host = lv_obj_get_parent(host);
  if (!host) host = lv_screen_active();
  return host ? lv_color_to_u32(lv_obj_get_style_bg_color(host, LV_PART_MAIN)) & 0xFFFFFF : 0;
}

// On screen: the active screen, nothing hidden on the way up.
bool on_screen(lv_obj_t* card) {
  if (!card || lv_obj_get_screen(card) != lv_screen_active()) return false;
  for (lv_obj_t* obj = card; obj; obj = lv_obj_get_parent(obj)) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return false;
  }
  return true;
}

// A circle or pill over the picture: a resting surface (the circle's color,
// tile_icon_source::refresh_controls) at the tile's transparency, with the
// corner disc's shape and corner shrink.
lv_obj_t* make_surface(lv_obj_t* card, uint8_t transparency) {
  lv_obj_t* obj = lv_obj_create(card);
  if (!obj) return nullptr;
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
  ui_surface_style::apply_radius(obj, tile_icon_disc::radius_baseline(), 0);
  tile_icon_disc::shrink_corner_disc(obj);
  tile_icon_disc::mark_surface(obj);
  lv_obj_set_style_bg_opa(obj, static_cast<lv_opa_t>((kTransparencyMax - transparency) * 255 / kTransparencyMax), 0);
  return obj;
}

// Card coordinates -> the card's content box (its paddings and the frame
// line's border, ui_surface_style::draw_frame_over_children).
void place(lv_obj_t* card, lv_obj_t* obj, int x, int y) {
  lv_obj_align(obj, LV_ALIGN_TOP_LEFT, x - lv_obj_get_style_space_left(card, LV_PART_MAIN),
               y - lv_obj_get_style_space_top(card, LV_PART_MAIN));
}

// The first picture turns the icon look into the head over the picture.
bool show_head(PictureTile& tile) {
  lv_obj_t* card = tile.card;
  tile.image = lv_image_create(card);
  if (!tile.image) return false;
  // The frame line stays visible over the picture (user 2026-10-11: "the
  // camera tiles have no frame"); set before anything is placed.
  ui_surface_style::draw_frame_over_children(card);
  lv_obj_remove_flag(tile.image, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_add_flag(tile.image, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_IGNORE_LAYOUT));
  lv_obj_set_size(tile.image, tile.width, tile.height);
  place(card, tile.image, 0, 0);
  lv_obj_move_to_index(tile.image, 0);

  const int inset = tile_icon_disc::inset();
  const int icon_w =
      FONT_MDI_ICONS ? lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0) : 0;
  const int icon_h = FONT_MDI_ICONS ? lv_font_get_line_height(FONT_MDI_ICONS) : 0;
  const int disc = tile_icon_disc::header_diameter(icon_w);
  const int shrink = tile_icon_disc::corner_shrink();

  // The icon's own circle gives way to the surfaces; it keeps computing the
  // circle color and a readable icon.
  if (tile.icon) {
    if (lv_obj_t* own = tile_icon_disc::disc_of(tile.icon)) lv_obj_set_style_opa(own, LV_OPA_TRANSP, 0);
    place(card, tile.icon, inset + (disc - icon_w) / 2, inset + (disc - icon_h) / 2);
  }

  int text_w = 0;
  hometiles_title::State* title_state = tile.title ? hometiles_title::state_for(tile.title) : nullptr;
  if (tile.title) {
    const char* text = title_state ? title_state->text.c_str() : lv_label_get_text(tile.title);
    text_w = title_metric(lv_obj_get_style_text_font(tile.title, LV_PART_MAIN), text);
  }
  const camera_picture_layout::Head head = camera_picture_layout::head(
      tile.width, inset, disc, shrink, tile_layout::scale_480(kTitlePad), GRID_GAP, text_w);

  tile.circle = head.shared ? nullptr : make_surface(card, tile.transparency);
  if (tile.circle) {
    lv_obj_set_size(tile.circle, disc, disc);
    place(card, tile.circle, inset, inset);
    lv_obj_move_to_index(tile.circle, 1);
  }
  tile.pill = tile.title || head.shared ? make_surface(card, tile.transparency) : nullptr;
  if (tile.pill) {
    lv_obj_set_size(tile.pill, head.pill_w, disc);
    place(card, tile.pill, head.pill_x, inset);
    lv_obj_move_to_index(tile.pill, tile.circle ? 2 : 1);
  }
  if (tile.title) {
    if (title_state) title_state->single_line = true;
    // Split: centered across the whole visible pill, so kerning never cuts
    // the title its metric made room for. Shared: right-aligned where it was.
    const int text_x = head.shared ? head.text_x : head.pill_x + shrink;
    const int text_w = head.shared ? head.text_w : head.pill_w - 2 * shrink;
    lv_obj_set_style_text_align(tile.title, head.shared ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(tile.title, std::max(1, text_w));
    const int line_h = lv_font_get_line_height(lv_obj_get_style_text_font(tile.title, LV_PART_MAIN));
    place(card, tile.title, text_x, inset + disc / 2 - line_h / 2);
    if (title_state) hometiles_title::render(tile.title, title_state);
  }
  // The surfaces take the circle's color now; later colors reach them
  // through the icon color hook.
  tile_icon_source::refresh_controls(card);
  Serial.printf("[CameraTile] %s: picture %ux%u, %s head\n", tile.entity.c_str(),
                static_cast<unsigned>(tile.width), static_cast<unsigned>(tile.height),
                head.shared ? "shared" : "split");
  return true;
}

// Decodes `picture` for `tile` and shows it.
void show(PictureTile& tile, const bridge_images::Picture& picture) {
  const uint32_t started_ms = millis();
  uint16_t w = 0, h = 0;
  uint16_t* pixels =
      image_screensaver_decode_picture(picture.jpeg, picture.length, tile.width, tile.height, w, h);
  if (!pixels || w != tile.width || h != tile.height) {
    free(pixels);
    Serial.printf("[CameraTile] %s: picture could not be decoded\n", tile.entity.c_str());
    return;
  }
  const uint16_t radius = static_cast<uint16_t>(std::max(0, static_cast<int>(lv_obj_get_style_radius(tile.card, LV_PART_MAIN))));
  const uint16_t under = camera_picture_layout::rgb565(color_behind(tile.card));
  camera_picture_layout::round_corners(pixels, w, h, radius, under, wallpaper_cover::rounded_pixel_coverage);
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
  if (!tile.image && !show_head(tile)) {
    free(pixels);
    free(dsc);
    return;
  }
  lv_image_set_src(tile.image, dsc);
  free_dsc(tile.dsc);
  tile.dsc = dsc;
  tile.under = under;
  tile.radius = radius;
  memcpy(tile.key, picture.key, sizeof(tile.key));
  Serial.printf("[CameraTile] %s %s shown in %lu ms\n", tile.entity.c_str(), tile.key,
                static_cast<unsigned long>(millis() - started_ms));
}

// The picture topics the tiles want now: an image entity's while its tile
// exists, a camera's only while its tile is on screen, the display awake and
// no camera stream runs (the Bridge loads a camera still at every interval).
void update_topics() {
  std::vector<String> entities, suffixes;
  if (networkManager.linkConfigured()) {
    const bool awake = !powerManager.isInSleep() && !image_screensaver_covers_ui() && !camera_popup_is_busy();
    char suffix[40];
    for (const PictureTile* tile : g_tiles) {
      const bool camera = is_camera(tile->entity);
      if (camera && !(awake && on_screen(tile->card))) continue;
      if (!bridge_images::pictureSuffix(suffix, sizeof(suffix), tile->width, tile->height, tile->fit,
                                        camera ? tile->every : 0)) {
        continue;
      }
      entities.push_back(tile->entity);
      suffixes.push_back(suffix);
    }
  }
  // The Vacuum tiles' and popup's maps share the tile picture topics.
  vacuum_map::collect_topics(entities, suffixes);
  mqttSetTilePictures(entities.data(), suffixes.data(), entities.size());
}

// The corners follow the color behind the card at once (user 2026-10-11:
// black corners stayed after the theme turned light until a new picture
// came, which for an unchanged camera still can take long): turned in
// place, the image drawn again.
void follow_corners(PictureTile& tile) {
  if (!tile.dsc || !tile.image) return;
  const uint16_t under = camera_picture_layout::rgb565(color_behind(tile.card));
  if (under == tile.under) return;
  camera_picture_layout::reround_corners(reinterpret_cast<uint16_t*>(const_cast<uint8_t*>(tile.dsc->data)),
                                         tile.width, tile.height, tile.radius, tile.under, under,
                                         wallpaper_cover::rounded_pixel_coverage);
  tile.under = under;
  lv_image_cache_drop(tile.dsc);
  lv_obj_invalidate(tile.image);
}

// A rebuilt tile takes the pixels its card had (Parked): the same picture,
// or for the moment one of the other placement until its own arrives.
bool adopt_parked(PictureTile& tile) {
  for (auto it = g_parked.begin(); it != g_parked.end(); ++it) {
    if (!it->entity.equalsIgnoreCase(tile.entity) || it->width != tile.width || it->height != tile.height) {
      continue;
    }
    if (!tile.image && !show_head(tile)) return false;
    lv_image_set_src(tile.image, it->dsc);
    tile.dsc = it->dsc;
    tile.under = it->under;
    tile.radius = it->radius;
    if (it->fit == tile.fit) memcpy(tile.key, it->key, sizeof(tile.key));
    g_parked.erase(it);
    follow_corners(tile);
    return true;
  }
  return false;
}

}  // namespace

bool head_over_picture(const Tile& tile) {
  // Not tied to the link: the tiles are drawn at boot before the network
  // knows its link (user 2026-10-11: the head turned dark only after an edit).
  return wanted(tile) && tile.span_h >= 1;
}

int title_metric(const lv_font_t* font, const char* text) {
  if (!font) return 0;
  return camera_picture_layout::title_metric(
      text, [font](uint32_t code) { return static_cast<int>(lv_font_get_glyph_width(font, code, 0)); });
}

void attach(lv_obj_t* card, lv_obj_t* icon, lv_obj_t* title, const Tile& tile) {
  if (!card || !wanted(tile)) return;
  const int width = tile_geometry::extent(tile.col, std::max(1.0f, tile.span_w), GRID_CELL_W, GRID_GAP);
  const int height = tile_geometry::extent(tile.row, std::max(1.0f, tile.span_h), GRID_CELL_H, GRID_GAP);
  if (width < bridge_images::kMinEdge || height < bridge_images::kMinEdge ||
      width > bridge_images::kMaxEdge || height > bridge_images::kMaxEdge) {
    return;
  }
  PictureTile* picture_tile = new PictureTile();
  picture_tile->card = card;
  picture_tile->icon = icon;
  picture_tile->title = title;
  picture_tile->entity = tile.sensor_entity;
  picture_tile->width = static_cast<uint16_t>(width);
  picture_tile->height = static_cast<uint16_t>(height);
  picture_tile->fit = fit(tile);
  picture_tile->every = every(tile);
  picture_tile->transparency = transparency(tile);
  lv_obj_add_event_cb(card, delete_cb, LV_EVENT_DELETE, picture_tile);
  g_tiles.push_back(picture_tile);
  g_topics_due = true;
  // No icon look in between when the picture is at hand.
  if (!adopt_parked(*picture_tile)) {
    const bridge_images::Picture* picture = bridge_images::find(
        picture_tile->entity.c_str(), picture_tile->width, picture_tile->height, picture_tile->fit);
    if (picture) show(*picture_tile, *picture);
  }
}

void arrived(const char* entity_id) {
  if (!entity_id) return;
  for (PictureTile* tile : g_tiles) {
    if (tile->entity.equalsIgnoreCase(entity_id)) tile->due = true;
  }
}

void service() {
  const uint32_t now = millis();
  while (!g_parked.empty() && now - g_parked.front().since_ms >= kParkedMs) {
    free_dsc(g_parked.front().dsc);
    g_parked.erase(g_parked.begin());
  }
  vacuum_map::service();
  if (g_topics_due || now - g_topics_ms >= kTopicsEveryMs) {
    g_topics_due = false;
    g_topics_ms = now;
    update_topics();
  }
  // Every pass: a design change turns the corners with the next frame.
  for (PictureTile* tile : g_tiles) follow_corners(*tile);
  // Under an open tile popup nothing of a picture shows: no decode, and no
  // redraw of the popup over it (user 2026-10-14: the Weather popup turned
  // slower with a camera tile on Home). The newest one shows when it closes.
  if (popup_shell_active()) return;
  // One decode per call keeps the loop responsive with many tiles.
  for (PictureTile* tile : g_tiles) {
    if (!tile->due) continue;
    tile->due = false;
    const bridge_images::Picture* picture =
        bridge_images::find(tile->entity.c_str(), tile->width, tile->height, tile->fit);
    // A missing picture keeps the last one shown (its JPEG may have made room
    // for others after decoding).
    if (!picture || strcmp(picture->key, tile->key) == 0) continue;
    show(*tile, *picture);
    break;
  }
}

}  // namespace camera_picture
