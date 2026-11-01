# Calendar tile contract

Tile type 27 (`TILE_CALENDAR`, `src/types/calendar/`), popup in
`src/ui/popups/calendar/`. Mockup: `build/design-mockups/calendar` (2026-10-24).

## Persistence (`PackedTileV7`, unchanged)

| Field | Use |
| --- | --- |
| `sensor_entity` | Up to four `calendar.*` ids, comma separated, in color order (long lists use the entity sidecar) |
| `sensor_display_mode` | View: 0 agenda (adapts to the size), 1 week, 2 month, 3 today (event count at half height, the agenda otherwise) |
| `sensor_decimals` | First day of the week: `0xFF` automatic (`LocaleProfile::first_weekday`), 0 Sunday, 1 Monday, 6 Saturday |
| popup open mode | Short or long press, like the other popup tiles |

Layouts (`layout_of`, mirrored by the Web Admin's `calendarPreviewLayout`):

| Size | Layout |
| --- | --- |
| half height | next event, "Next · 10:00" / title (`chip_lines`); 2+ wide "Next · in 1 h 30 min" / "title · 10:00"; view 3: "Today" / event count (`remaining_today`) |
| below 2 wide, 1 row | countdown: event title as header, large day, countdown (`countdown_lines`) |
| 2+ wide, 1 row | Up next |
| 2+ rows | agenda |
| view 1, 1.5+ wide | week strip, today's column tinted; one row starts today, taller the first weekday (`strip_first_day`) |
| view 2, 2x2+ | month |

The popup shows a legend (dot and name per calendar, one drawn object) on top
of the body when the tile has two or more calendars. The mockup puts it under
the title, but the popup header is the shared shell (`popup_shell.cpp`), which
copies plain text labels only.

The month view needs a panel larger than 480x480 (`DEVICE_LAYOUT_480X480`
hides it in the Web Admin and the tile falls back to the agenda) and a tile of
at least 2x2. The popup's month grid is available on every panel.

## Bridge

The Bridge announces `"entity_search": 5` and the lists `calendars` and
`calendar_meta` in its configuration. The panel then offers the picker list
`calendars`, declares its calendar tiles' ids under `calendars`, and subscribes
`{ha_prefix}/calendar/<object>/events` for every offered calendar (like the
`detail` topics). Older Bridges announce no calendars; the tile then shows
"no data".

Retained payload (Bridge `calendar_events.py`, panel `calendar_events.h`):

```json
{"v":1,"state":"on","from":"2026-09-24","to":"2027-01-07",
 "e":[{"s":"2026-10-24","e":"2026-10-25","t":"Bio waste"},
      {"s":"2026-10-24T10:00","e":"2026-10-24T11:30","t":"Long ride","l":"Pool"}]}
```

- Times are Home Assistant's local time without an offset. A start without a
  time is all-day; its end date is exclusive. A timed event ending at midnight
  belongs to the days before.
- `from`/`to` (inclusive) are the days the list is complete for. The popup's
  arrows stop at them, and days outside show no events rather than "free".
- `state` is the entity state; `"unavailable"` marks a calendar Home
  Assistant does not have.
- Bounds: at most 96 events, title 63 bytes, location 47 bytes (UTF-8, cut on
  a character), payload 16384 bytes, window at most 400 days. When the Bridge
  must drop events it drops the latest and moves `to` back. The panel ignores
  a payload over the limit and keeps the previous one.
- Window: from 7 days before the 1st of the current month to 7 days after the
  end of the second month after it.
- The Bridge publishes after a snapshot, when the calendar's state changes
  (an event starts or ends), and every 15 minutes (edits, the new day),
  debounced by 2 s and skipped when unchanged. A failing `calendar.get_events`
  keeps the retained payload. One entry per `ha_prefix` publishes
  (`_owns_state_publish`).

## Panel

- MQTT callbacks only queue the payload (`calendar_store::queue`, coalesced,
  at most 24 calendars); the loop parses it (`calendar_store::process`, two
  per pass) and refreshes the tiles and an open popup.
- The payloads, the store's map and the parsed events live in PSRAM
  (`PsString`, `PsVector`, a PSRAM JSON pool): the default malloc puts
  blocks below 16 KB into the internal heap.
- Every rendered tile owns a small view found by calendar; a 15 s timer
  redraws them when the minute or the day changes.
- "Today" needs the panel clock (NTP): until it is set, the tile shows `--`
  and the month and week views mark no day.

## Verification

- Host: `tools/tests/tiles/test-calendar-model.mjs` (parser, model, week
  start, month grid), `test-calendar-tiles.mjs` (wiring, Web Admin preview).
- Bridge: `tests/test_calendar_events.py` (payload bounds, window, publisher).
- Hardware: pending on JC8012P4A1 V3 (10.1", month view) and
  ESP32-4848S040C_I (480x480, agenda/week only).

## Status (2026-10-24)

Done and on the branch: tile, popup, Web Admin, i18n, docs, host tests
(firmware `calendar-tile`, Bridge `calendar-events`, both pushed to the
ph15ch forks; draft PRs against GalusPeres `main`). Host syntax checks of
the calendar sources pass for the JC8012P4A1 and ESP32-4848S040 profiles.

2026-10-24: the mockup's remaining views (half-height "Next" line and
Today count, 1x1 countdown, rolling week strip with today tinted, popup
legend) are built; the user tested the agenda and week on
ESP32-4848S040C_I and does no further hardware tests.

Open, in this order:

1. Hardware (target compile done: `firmware.yml` run 40096721461 on
   ph15ch/HomeTiles `calendar-tile` at `d0f507e5` built all 21 profiles;
   its `firmware-<key>` artifacts, e.g. `firmware-guition_jc8012p4a1_v3`
   and `firmware-guition_esp32_4848s040`, are the test images):
   JC8012P4A1 V3 (tile sizes, Week, Month, popup month and list, light
   theme) and ESP32-4848S040C_I (agenda and week only, popup on
   480x480). Watch heap after a calendar with many events
   (`calendar_store` keeps the parsed events per calendar).
2. Live Home Assistant: a local and a Google/CalDAV calendar, all-day and
   multi-day events, an event over midnight, a renamed or removed calendar
   (retained topic stays; the panel drops calendars it is no longer offered).
3. Web Admin preview screenshots for `docs/tiles.md` (the section has no
   images yet).
