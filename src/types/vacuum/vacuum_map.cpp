#include "src/types/vacuum/vacuum_map.h"

// lvgl.h no longer exports lv_image_cache_drop() (tile_renderer.cpp).
#include <misc/cache/instance/lv_image_cache.h>

#include <algorithm>
#include <cstring>

#include "src/core/power/power_manager.h"
#include "src/network/bridge/bridge_images.h"
#include "src/network/network_manager.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/types/camera/picture_layout.h"
#include "src/types/media/artwork_scale.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/ui/screensaver/wallpaper_cover.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/shared/ui_theme.h"

namespace vacuum_map {
namespace {


// The side the Bridge sends: the P4's hardware JPEG decoder takes sides in
// steps of 16 only (like the Media popup's cover, bridge_images.cpp
// coverEdge); any other side went to the slow software decoder in the UI
// loop (user 2026-10-30: the popup's map took long). The square shows an
// averaged copy in its own size.
uint16_t fetch_edge(uint16_t edge) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  return static_cast<uint16_t>((edge + 15) / 16 * 16);
#else
  return edge;
#endif
}

struct View {
  lv_obj_t* box = nullptr;    // the square: black like the map's background until a picture
  lv_obj_t* icon = nullptr;   // the placeholder's map icon
  lv_obj_t* image = nullptr;  // created with the first picture
  lv_image_dsc_t* dsc = nullptr;
  String entity;
  uint16_t edge = 0;
  uint16_t radius = 0;
  uint16_t under = 0;  // RGB565 the corners were rounded towards
  char key[bridge_images::kKeyLength + 1] = "";
  bool due = true;
  uint8_t every = 5;  // a camera's interval
  int32_t under_rgb = -1;  // set_under; -1: the nearest opaque parent's
};

std::vector<View*> g_views;

// Pictures fetched ahead of their square (want).
struct Want {
  const void* owner = nullptr;
  lv_obj_t* card = nullptr;
  String entity;
  uint16_t edge = 0;
  uint8_t every = 5;
};
std::vector<Want> g_wants;

// The popup's picture decoded ahead while its tile is on screen, as the
// Media tile keeps its popup cover decoded (MediaCoverRef::popup_dsc): the
// popup opens with a copy in its size instead of decoding the JPEG while it
// opens, and the tile's square takes its own copy from it, one picture and
// one decode for both (user 2026-10-30: the map still came after the popup).
// Kept a while after its last wish, so a rebuilt tile finds it.
struct Source {
  String entity;
  uint16_t fetch = 0;
  char key[bridge_images::kKeyLength + 1] = "";
  lv_image_dsc_t* dsc = nullptr;  // fetch x fetch RGB565, corners not rounded
  uint32_t wanted_ms = 0;
};
std::vector<Source> g_sources;

// The pixels of a deleted map, for a while: a Web Admin change or a new
// layout builds the tile again, and the new square shows them at once
// instead of the placeholder until the next picture (user 2026-10-30: "das
// Bild ist weg"), like the Camera tile's parked pictures.
struct Parked {
  String entity;
  uint16_t edge = 0;
  uint16_t radius = 0;
  uint16_t under = 0;
  char key[bridge_images::kKeyLength + 1] = "";
  lv_image_dsc_t* dsc = nullptr;
  uint32_t since_ms = 0;
};
std::vector<Parked> g_parked;
constexpr size_t kParkedMax = 3;
constexpr uint32_t kParkedMs = 15000;

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

void park(View& view) {
  if (!view.dsc) return;
  if (g_parked.size() >= kParkedMax) {
    free_dsc(g_parked.front().dsc);
    g_parked.erase(g_parked.begin());
  }
  Parked parked;
  parked.entity = view.entity;
  parked.edge = view.edge;
  parked.radius = view.radius;
  parked.under = view.under;
  memcpy(parked.key, view.key, sizeof(parked.key));
  parked.dsc = view.dsc;
  parked.since_ms = millis();
  view.dsc = nullptr;
  g_parked.push_back(parked);
}

void delete_cb(lv_event_t* event) {
  View* view = static_cast<View*>(lv_event_get_user_data(event));
  if (!view) return;
  g_views.erase(std::remove(g_views.begin(), g_views.end(), view), g_views.end());
  park(*view);
  delete view;
}

// The color behind the square: its nearest opaque ancestor (the card).
uint32_t color_behind(lv_obj_t* box) {
  lv_obj_t* host = box ? lv_obj_get_parent(box) : nullptr;
  while (host && lv_obj_get_style_bg_opa(host, LV_PART_MAIN) < LV_OPA_COVER) host = lv_obj_get_parent(host);
  if (!host) host = lv_screen_active();
  return host ? lv_color_to_u32(lv_obj_get_style_bg_color(host, LV_PART_MAIN)) & 0xFFFFFF : 0;
}

uint32_t corner_color(const View& view) {
  return view.under_rgb >= 0 ? static_cast<uint32_t>(view.under_rgb) : color_behind(view.box);
}

bool on_screen(lv_obj_t* box) {
  if (!box || lv_obj_get_screen(box) != lv_screen_active()) return false;
  for (lv_obj_t* obj = box; obj; obj = lv_obj_get_parent(obj)) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return false;
  }
  return true;
}

bool is_camera(const String& entity) { return entity.startsWith("camera."); }

void present(View& view, lv_image_dsc_t* dsc) {
  if (!view.image) {
    view.image = lv_image_create(view.box);
    lv_obj_remove_flag(view.image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(view.image, 0, 0);
    lv_obj_set_size(view.image, view.edge, view.edge);
  }
  lv_image_set_src(view.image, dsc);
  if (view.dsc != dsc) free_dsc(view.dsc);
  view.dsc = dsc;
  if (view.icon) lv_obj_add_flag(view.icon, LV_OBJ_FLAG_HIDDEN);
}

// The picture as RGB565 pixels in its own size (`fetch` x `fetch`).
lv_image_dsc_t* decode(const bridge_images::Picture& picture, uint16_t fetch, const String& entity) {
  uint16_t w = 0, h = 0;
  uint16_t* pixels = image_screensaver_decode_picture(picture.jpeg, picture.length, fetch, fetch, w, h);
  if (!pixels || w != fetch || h != fetch) {
    free(pixels);
    Serial.printf("[VacuumMap] %s: picture could not be decoded\n", entity.c_str());
    return nullptr;
  }
  lv_image_dsc_t* dsc = static_cast<lv_image_dsc_t*>(malloc(sizeof(lv_image_dsc_t)));
  if (!dsc) {
    free(pixels);
    return nullptr;
  }
  memset(dsc, 0, sizeof(*dsc));
  dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc->header.cf = LV_COLOR_FORMAT_RGB565;
  dsc->header.w = w;
  dsc->header.h = h;
  dsc->header.stride = static_cast<uint32_t>(w) * 2;
  dsc->data_size = static_cast<uint32_t>(w) * h * 2;
  dsc->data = reinterpret_cast<const uint8_t*>(pixels);
  return dsc;
}

Source* source_of(const String& entity) {
  for (Source& source : g_sources) {
    if (source.dsc && source.entity.equalsIgnoreCase(entity)) return &source;
  }
  return nullptr;
}

bool wanted(const String& entity) {
  for (const Want& wish : g_wants) {
    if (wish.entity.equalsIgnoreCase(entity)) return true;
  }
  return false;
}

// From the decoded popup picture, the square's own copy (averaged to its
// size, corners rounded): no JPEG decode. False without one.
bool show_from_source(View& view) {
  const Source* source = source_of(view.entity);
  if (!source) return false;
  if (strcmp(source->key, view.key) == 0) return true;
  const uint32_t started_ms = millis();
  lv_image_dsc_t* dsc = make_media_popup_cover_dsc(source->dsc, view.edge);
  if (!dsc) {
    Serial.printf("[VacuumMap] %s: no memory for the %u px copy\n", view.entity.c_str(),
                  static_cast<unsigned>(view.edge));
    return true;
  }
  const uint16_t under = camera_picture_layout::rgb565(corner_color(view));
  camera_picture_layout::round_corners(reinterpret_cast<uint16_t*>(const_cast<uint8_t*>(dsc->data)), view.edge,
                                       view.edge, view.radius, under, wallpaper_cover::rounded_pixel_coverage);
  view.under = under;
  present(view, dsc);
  memcpy(view.key, source->key, sizeof(view.key));
  Serial.printf("[VacuumMap] %s %s copied in %lu ms (%u px from %u px)\n", view.entity.c_str(), view.key,
                static_cast<unsigned long>(millis() - started_ms), static_cast<unsigned>(view.edge),
                static_cast<unsigned>(source->fetch));
  return true;
}

void show(View& view, const bridge_images::Picture& picture) {
  const uint32_t started_ms = millis();
  const uint16_t fetch = fetch_edge(view.edge);
  lv_image_dsc_t* dsc = decode(picture, fetch, view.entity);
  if (!dsc) return;
  if (fetch != view.edge) {
    // The Media popup's averaged copy in the square's size.
    lv_image_dsc_t* copy = make_media_popup_cover_dsc(dsc, view.edge);
    free_dsc(dsc);
    if (!copy) {
      Serial.printf("[VacuumMap] %s: no memory for the %u px copy\n", view.entity.c_str(),
                    static_cast<unsigned>(view.edge));
      return;
    }
    dsc = copy;
  }
  const uint16_t under = camera_picture_layout::rgb565(corner_color(view));
  camera_picture_layout::round_corners(reinterpret_cast<uint16_t*>(const_cast<uint8_t*>(dsc->data)), view.edge,
                                       view.edge, view.radius, under, wallpaper_cover::rounded_pixel_coverage);
  view.under = under;
  present(view, dsc);
  memcpy(view.key, picture.key, sizeof(view.key));
  Serial.printf("[VacuumMap] %s %s shown in %lu ms (%u px from %u px)\n", view.entity.c_str(), view.key,
                static_cast<unsigned long>(millis() - started_ms), static_cast<unsigned>(view.edge),
                static_cast<unsigned>(fetch));
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

// A parked picture of the same entity in the same size shows at once.
bool adopt_parked(View& view) {
  for (auto it = g_parked.begin(); it != g_parked.end(); ++it) {
    if (it->edge != view.edge || it->radius != view.radius || !it->entity.equalsIgnoreCase(view.entity)) continue;
    view.under = it->under;
    memcpy(view.key, it->key, sizeof(view.key));
    lv_image_dsc_t* dsc = it->dsc;
    g_parked.erase(it);
    present(view, dsc);
    return true;
  }
  return false;
}

// The corners follow the color behind the square at once (a design change,
// the popup taking its tile's color).
void follow_corners(View& view) {
  if (!view.dsc || !view.image) return;
  const uint16_t under = camera_picture_layout::rgb565(corner_color(view));
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
  // No theme style (LVGL's default object is white, which showed until the
  // picture came). Black like the map's own background: the Bridge puts a
  // transparent map on black, so the square looks the same before and after.
  lv_obj_remove_style_all(box);
  lv_obj_set_style_bg_color(box, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  // The radius like every surface (the design's corner setting); the
  // picture's corners are rounded with what it resolves to.
  ui_surface_style::apply_radius(box, radius, 0);
  lv_obj_remove_flag(box, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_pos(box, x, y);
  lv_obj_set_size(box, edge, edge);
  View* view = new View();
  view->box = box;
  view->edge = static_cast<uint16_t>(edge);
  view->radius = static_cast<uint16_t>(std::max<int32_t>(0, lv_obj_get_style_radius(box, LV_PART_MAIN)));
  if (FONT_MDI_ICONS) {
    view->icon = lv_label_create(box);
    lv_obj_set_style_text_font(view->icon, FONT_MDI_ICONS, 0);
    lv_obj_set_style_text_color(view->icon, lv_color_white(), 0);
    lv_obj_set_style_text_opa(view->icon, LV_OPA_40, 0);
    lv_label_set_text(view->icon, getMdiChar("map-outline").c_str());
    lv_obj_center(view->icon);
  }
  g_views.push_back(view);
  lv_obj_add_event_cb(box, delete_cb, LV_EVENT_DELETE, view);
  return box;
}

void set_under(lv_obj_t* box, uint32_t rgb) {
  if (View* view = view_of(box)) view->under_rgb = static_cast<int32_t>(rgb & 0xFFFFFF);
}

uint8_t every_of(uint16_t seconds) {
  if (seconds < bridge_images::kEveryMin) return bridge_images::kEveryMin;
  if (seconds > bridge_images::kEveryMax) return bridge_images::kEveryMax;
  return static_cast<uint8_t>(seconds);
}

void set_entity(lv_obj_t* box, const char* entity, uint8_t every) {
  View* view = view_of(box);
  if (!view) return;
  view->every = every_of(every);
  const String next = entity ? String(entity) : String();
  if (next.equalsIgnoreCase(view->entity)) return;
  view->entity = next;
  show_placeholder(*view);
  if (next.length()) adopt_parked(*view);
  view->due = next.length() > 0;
}

void want(const void* owner, lv_obj_t* card, const char* entity, uint16_t edge, uint8_t every) {
  if (!owner) return;
  auto it = std::find_if(g_wants.begin(), g_wants.end(), [owner](const Want& w) { return w.owner == owner; });
  if (!entity || !*entity || edge < 16) {
    if (it != g_wants.end()) g_wants.erase(it);
    return;
  }
  if (it == g_wants.end()) {
    g_wants.push_back(Want{});
    it = g_wants.end() - 1;
  }
  it->owner = owner;
  it->card = card;
  it->entity = entity;
  it->edge = edge;
  it->every = every_of(every);
}

void unwant(const void* owner) {
  g_wants.erase(std::remove_if(g_wants.begin(), g_wants.end(), [owner](const Want& w) { return w.owner == owner; }),
                g_wants.end());
}

void show_now(lv_obj_t* box) {
  View* view = view_of(box);
  if (!view || !view->entity.length()) return;
  if (show_from_source(*view)) {
    view->due = false;
    return;
  }
  const uint16_t fetch = fetch_edge(view->edge);
  const bridge_images::Picture* picture =
      bridge_images::find(view->entity.c_str(), fetch, fetch, bridge_images::kContain);
  if (!picture || strcmp(picture->key, view->key) == 0) return;
  view->due = false;
  show(*view, *picture);
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
  // A camera only while its square (or the tile that wants it) is on screen
  // and the display awake; each topic once.
  auto add = [&](const String& entity, uint16_t edge, uint8_t every, lv_obj_t* shown) {
    if (!entity.length()) return;
    const bool camera = is_camera(entity);
    if (camera && !(awake && on_screen(shown))) return;
    const uint16_t fetch = fetch_edge(edge);
    if (!bridge_images::pictureSuffix(suffix, sizeof(suffix), fetch, fetch, bridge_images::kContain,
                                      camera ? every : 0)) {
      return;
    }
    for (size_t i = 0; i < entities.size(); ++i) {
      if (entities[i] == entity && suffixes[i] == suffix) return;
    }
    entities.push_back(entity);
    suffixes.push_back(suffix);
  };
  // A square whose entity a tile wants takes its copy from that picture.
  for (const View* view : g_views) {
    if (!wanted(view->entity)) add(view->entity, view->edge, view->every, view->box);
  }
  for (const Want& wish : g_wants) add(wish.entity, wish.edge, wish.every, wish.card);
}

void service() {
  const uint32_t now = millis();
  while (!g_parked.empty() && now - g_parked.front().since_ms >= kParkedMs) {
    free_dsc(g_parked.front().dsc);
    g_parked.erase(g_parked.begin());
  }
  for (View* view : g_views) follow_corners(*view);
  // The wished pictures: decoded once when a new one is here and its tile
  // (or a square of its entity) is on screen; one decode per pass.
  for (Source& source : g_sources) {
    if (wanted(source.entity)) source.wanted_ms = now;
  }
  for (auto it = g_sources.begin(); it != g_sources.end();) {
    if (now - it->wanted_ms < kParkedMs) {
      ++it;
      continue;
    }
    free_dsc(it->dsc);
    it = g_sources.erase(it);
  }
  for (const Want& wish : g_wants) {
    const uint16_t fetch = fetch_edge(wish.edge);
    const bridge_images::Picture* picture =
        bridge_images::find(wish.entity.c_str(), fetch, fetch, bridge_images::kContain);
    if (!picture) continue;
    auto it = std::find_if(g_sources.begin(), g_sources.end(), [&](const Source& source) {
      return source.fetch == fetch && source.entity.equalsIgnoreCase(wish.entity);
    });
    if (it != g_sources.end() && strcmp(it->key, picture->key) == 0) continue;
    bool shown = on_screen(wish.card);
    for (const View* view : g_views) {
      shown = shown || (view->entity.equalsIgnoreCase(wish.entity) && on_screen(view->box));
    }
    if (!shown) continue;
    const uint32_t started_ms = millis();
    lv_image_dsc_t* dsc = decode(*picture, fetch, wish.entity);
    if (!dsc) break;
    if (it == g_sources.end()) {
      g_sources.push_back(Source{});
      it = g_sources.end() - 1;
      it->entity = wish.entity;
      it->fetch = fetch;
    }
    free_dsc(it->dsc);
    it->dsc = dsc;
    it->wanted_ms = now;
    memcpy(it->key, picture->key, sizeof(it->key));
    Serial.printf("[VacuumMap] %s %s decoded ahead in %lu ms (%u px)\n", it->entity.c_str(), it->key,
                  static_cast<unsigned long>(millis() - started_ms), static_cast<unsigned>(fetch));
    for (View* view : g_views) {
      if (view->entity.equalsIgnoreCase(wish.entity)) view->due = true;
    }
    return;
  }
  for (View* view : g_views) {
    if (!view->due || !view->entity.length()) continue;
    // Off screen nothing decodes; the picture waits for the next pass.
    if (!on_screen(view->box)) continue;
    view->due = false;
    // A copy of the wished picture (no decode), the next square in the same
    // pass; a square without a wish decodes its own picture.
    if (show_from_source(*view)) continue;
    if (wanted(view->entity)) continue;
    const uint16_t fetch = fetch_edge(view->edge);
    const bridge_images::Picture* picture =
        bridge_images::find(view->entity.c_str(), fetch, fetch, bridge_images::kContain);
    if (!picture || strcmp(picture->key, view->key) == 0) continue;
    show(*view, *picture);
    break;
  }
}

}  // namespace vacuum_map
