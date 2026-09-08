import assert from 'node:assert/strict';
import fs from 'node:fs';

const weather = fs.readFileSync(
  new URL('../../../src/ui/popups/weather/weather_popup.cpp', import.meta.url), 'utf8');

assert.match(
  weather,
  /#if defined\(DEVICE_ESP32_S3_RGB_480\)\s+constexpr int kHourlyParseBatchSize = 12;\s+constexpr int kHourlyInputObjectLimit = kHourlyForecastMax;\s+#else\s+constexpr int kHourlyParseBatchSize = 0;\s+constexpr int kHourlyInputObjectLimit = 0;/,
  'Only the S3 RGB family should split hourly weather parsing');

const batchStart = weather.indexOf('static bool parse_weather_hourly_batch(');
const buildStart = weather.indexOf('// Phase 2: Build UI', batchStart);
assert.ok(batchStart >= 0 && buildStart > batchStart,
          'Bounded hourly parser was not found');
const batch = weather.slice(batchStart, buildStart);
assert.match(batch, /objects_processed < kHourlyParseBatchSize/,
             'Hourly parsing must enforce a per-cycle object budget');
assert.match(batch, /g_pending_weather\.hourly_count < kHourlyForecastMax/,
             'Hourly parsing must enforce the model size limit');
assert.match(batch,
             /g_pending_weather\.hourly_objects_seen < kHourlyInputObjectLimit/,
             'Malformed objects must not extend work beyond the input limit');
assert.match(batch,
             /next_json_object_in_array\(g_pending_weather\.payload,/,
             'Hourly batches must parse the retained payload without a second large copy');
assert.doesNotMatch(weather, /String hourly_payload;/,
                    'The long hourly array must not be duplicated in memory');

const baseParseStart = weather.indexOf('static void parse_weather_base_data(');
const hourlyObjectStart = weather.indexOf(
  'static bool parse_hourly_weather_object(', baseParseStart);
assert.ok(baseParseStart >= 0 && hourlyObjectStart > baseParseStart,
          'Weather base parser was not found');
const baseParse = weather.slice(baseParseStart, hourlyObjectStart);
assert.match(baseParse, /daily_objects_processed < kCols/,
             'Daily parsing must be capped to the seven displayed columns');
assert.match(baseParse,
             /find_json_array_start\(json, "forecast"\)[\s\S]*next_json_object_in_array\(json, cursor, obj\)/,
             'Daily parsing must avoid copying its JSON array');
assert.match(baseParse,
             /find_json_array_start\(json, "forecast_hourly"\)/,
             'Hourly parsing must start at the array inside the retained payload');

const queueStart = weather.indexOf('void queue_weather_popup_payload(');
const processStart = weather.indexOf('void process_weather_popup_queue(', queueStart);
const refreshStart = weather.indexOf('void weather_popup_refresh_language(', processStart);
assert.ok(queueStart >= 0 && processStart > queueStart && refreshStart > processStart,
          'Weather popup queue functions were not found');
const queue = weather.slice(queueStart, processStart);
const process = weather.slice(processStart, refreshStart);

assert.match(
  queue,
  /g_pending_weather\.parse_hourly_pending \|\|\s+g_pending_weather\.build_ui_pending/,
  'Identical in-flight payloads must not restart parsing');
assert.match(queue, /cancel_weather_refresh_work\(\);[\s\S]*g_pending_weather\.valid = true;/,
             'A newer payload must replace unfinished work');
assert.match(
  queue,
  /active_work_entity[\s\S]*!active_work_entity\.equalsIgnoreCase\(entity_id\)[\s\S]*previous_selected_date\.remove\(0\);[\s\S]*previous_view_captured = false;[\s\S]*pending_day_nav = -1;/,
  'Replacing another entity must discard its selected day and navigation state');

const queuedPhase = process.indexOf('if (g_pending_weather.valid)');
const hourlyPhase = process.indexOf('if (g_pending_weather.parse_hourly_pending)');
const buildPhase = process.indexOf('if (g_pending_weather.build_ui_pending)');
const navPhase = process.indexOf('if (g_pending_weather.pending_day_nav >= 0)');
assert.ok(queuedPhase >= 0 && hourlyPhase > queuedPhase &&
          buildPhase > hourlyPhase && navPhase > buildPhase,
          'Newest payload, bounded parse, build, and navigation phases are reordered');
assert.match(process, /parse_weather_hourly_batch\(g_weather_popup_ctx\)/,
             'The queue processor must call the bounded hourly parser');
assert.match(process, /pending_day_nav >= 0[\s\S]*WeatherPopupViewMode::Week/,
             'Deferred day navigation must avoid building the detail graph twice');
assert.match(
  process,
  /has_rendered_data &&[\s\S]*!g_weather_popup_ctx->rendered_entity_id\.equalsIgnoreCase\([\s\S]*g_weather_popup_ctx->has_rendered_data = false;[\s\S]*rendered_entity_id\.remove\(0\);/,
  'Parsing entity B must invalidate entity A model metadata before replacement');
const finalBuild = process.slice(buildPhase, navPhase);
const finalHeader = finalBuild.indexOf(
  'apply_weather_header(g_weather_popup_ctx, g_pending_weather.payload);');
const finalGraph = finalBuild.indexOf('build_weather_ui(');
const commitLanguage = finalBuild.indexOf('rendered_language =');
const releasePayload = finalBuild.indexOf('g_pending_weather.payload.remove(0);');
assert.ok(finalHeader >= 0 && finalGraph > finalHeader &&
          commitLanguage > finalGraph && releasePayload > commitLanguage,
          'Final build must translate the header before committing language and releasing payload');

const showStart = weather.indexOf('static void finish_weather_popup_open(');
const preloadStart = weather.indexOf('void preload_weather_popup(', showStart);
assert.ok(showStart >= 0 && preloadStart > showStart,
          'Weather popup show function was not found');
const show = weather.slice(showStart, preloadStart);
assert.match(
  show,
  /!same_rendered_entity && matching_refresh_pending[\s\S]*hide_weather_model_widgets\(g_weather_popup_ctx\);/,
  'Opening entity B during its refresh must hide entity A widgets');

const refresh = weather.slice(refreshStart);
const refreshGuard = refresh.indexOf(
  'if (weather_refresh_in_progress(g_weather_popup_ctx)) return;');
const refreshBuild = refresh.indexOf('build_weather_ui(');
assert.ok(refreshGuard >= 0 && refreshBuild > refreshGuard,
          'Language refresh must not rebuild a partially parsed model');

assert.match(weather, /on_overlay_delete[\s\S]*reset_pending_weather_update\(\);/,
             'Deleting the popup must release pending weather work');
assert.ok((weather.match(/weather_refresh_in_progress\(ctx\)/g) ?? []).length >= 5,
          'Callbacks must not consume a partially replaced weather model');

class WeatherWorkModel {
  constructor(batchSize = 12, maxHours = 168, inputLimit = 168) {
    this.batchSize = batchSize;
    this.maxHours = maxHours;
    this.inputLimit = inputLimit;
    this.queued = null;
    this.active = null;
    this.rendered = 'old';
    this.maxObjectsInCall = 0;
  }

  queue(entity, revision, objects) {
    if ((this.queued && this.queued.entity === entity &&
         this.queued.revision === revision) ||
        (this.active && this.active.entity === entity &&
         this.active.revision === revision)) return;
    this.active = null;
    this.queued = {entity, revision, objects};
  }

  process() {
    if (this.queued) {
      this.active = {...this.queued, cursor: 0, accepted: 0};
      this.queued = null;
    }
    if (!this.active) return false;

    let processed = 0;
    while ((!this.batchSize || processed < this.batchSize) &&
           this.active.cursor < this.active.objects.length &&
           (!this.inputLimit || this.active.cursor < this.inputLimit) &&
           this.active.accepted < this.maxHours) {
      if (this.active.objects[this.active.cursor]) this.active.accepted++;
      this.active.cursor++;
      processed++;
    }
    this.maxObjectsInCall = Math.max(this.maxObjectsInCall, processed);

    if (this.active.cursor >= this.active.objects.length ||
        (this.inputLimit && this.active.cursor >= this.inputLimit) ||
        this.active.accepted >= this.maxHours) {
      this.rendered = `${this.active.entity}:${this.active.revision}`;
      this.active = null;
      return true;
    }
    return false;
  }
}

const full = new WeatherWorkModel();
full.queue('weather.home', 'A', Array(168).fill(true));
let calls = 0;
while (!full.process()) calls++;
calls++;
assert.equal(calls, 14, '168 hourly entries should require 14 S3 work cycles');
assert.equal(full.maxObjectsInCall, 12, 'No S3 cycle may parse more than 12 entries');
assert.equal(full.rendered, 'weather.home:A');

const replaced = new WeatherWorkModel();
replaced.queue('weather.home', 'A', Array(168).fill(true));
replaced.process();
replaced.process();
assert.equal(replaced.rendered, 'old', 'Incomplete data must not become rendered');
replaced.queue('weather.home', 'A2', [true, false, true]);
while (!replaced.process()) {}
assert.equal(replaced.rendered, 'weather.home:A2',
             'A newer payload for the same entity must replace partial work');

const bounded = new WeatherWorkModel();
bounded.queue('weather.large', 'large', Array(240).fill(true));
while (!bounded.process()) {}
assert.equal(bounded.rendered, 'weather.large:large');
assert.equal(bounded.maxObjectsInCall, 12);

const malformed = new WeatherWorkModel();
malformed.queue('weather.partial', 'partial',
                [false, true, false, true, false]);
while (!malformed.process()) {}
assert.equal(malformed.rendered, 'weather.partial:partial',
             'Empty or malformed hourly objects must still make progress');
assert.equal(malformed.maxObjectsInCall, 5);

const malformedLarge = new WeatherWorkModel();
malformedLarge.queue('weather.invalid', 'invalid', Array(240).fill(false));
let malformedCalls = 0;
while (!malformedLarge.process()) malformedCalls++;
malformedCalls++;
assert.equal(malformedCalls, 14,
             'Malformed input must stop after 168 inspected objects');
assert.equal(malformedLarge.maxObjectsInCall, 12);

const p4 = new WeatherWorkModel(0, 168, 0);
p4.queue('weather.home', 'P4', Array(168).fill(true));
assert.equal(p4.process(), true,
             'P4 must retain one-pass parsing for a normal 168-hour payload');
assert.equal(p4.maxObjectsInCall, 168);

const p4Partial = new WeatherWorkModel(0, 168, 0);
const partialP4Hours = Array(180).fill(true);
partialP4Hours.splice(0, 12, ...Array(12).fill(false));
p4Partial.queue('weather.home', 'P4-partial', partialP4Hours);
assert.equal(p4Partial.process(), true,
             'P4 must retain one-pass parsing when invalid objects precede 168 valid hours');
assert.equal(p4Partial.maxObjectsInCall, 180);

// The popup owns one shared parsed model. Starting B after rendering A must
// invalidate A's model identity before B writes its first partial batch. If B
// is then cancelled and A is opened again, A must be reparsed from its cache.
const sharedModel = {
  hasRendered: true,
  renderedEntity: 'weather.a',
  parsedModel: 'weather.a',
};
const beginParse = (entity) => {
  if (sharedModel.hasRendered && sharedModel.renderedEntity !== entity) {
    sharedModel.hasRendered = false;
    sharedModel.renderedEntity = '';
  }
  sharedModel.parsedModel = `partial:${entity}`;
};
const openEntity = (entity) => {
  const canReuse = sharedModel.hasRendered &&
    sharedModel.renderedEntity === entity;
  if (!canReuse) sharedModel.parsedModel = '';
  return canReuse ? 'reuse' : 'reparse';
};
beginParse('weather.b');
assert.equal(openEntity('weather.a'), 'reparse',
             'A partial B model must never be reused as rendered A');
assert.equal(sharedModel.parsedModel, '',
             'Cancelling B must clear its partial shared model before A reloads');

console.log('ESP32-S3 weather work budget contract: PASS');
