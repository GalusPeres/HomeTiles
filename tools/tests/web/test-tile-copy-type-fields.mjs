// Copy and paste and drafts keep a tile's own settings (user 2026-10-11: a
// copied Camera tile lost its picture options). Copies and drafts carry the
// editor's field names (collectTypeFieldValues -> save<Type>Fields); the
// type's load must read them as well as the stored fields of GET /api/tiles.
// Runs the real save and load handlers of the Camera and the Pixel
// animation tile on a small fake form.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

function form(values) {
  const fields = new Map(Object.entries(values).map(([id, value]) => [id, {id, value: String(value), textContent: '',
    classList: {toggle() {}, add() {}, remove() {}}}]));
  return {
    getElementById: id => fields.get(id) || null,
    value: id => fields.get(id)?.value,
  };
}

// --- Camera ---------------------------------------------------------------------------------------
const camera = readRepoFile('src/types/camera/admin.js');
const constants = camera.slice(camera.indexOf('const CAMERA_FITS'), camera.indexOf('function cameraEvery'));
const cameraFunctions = ['cameraEvery', 'cameraTransparency', 'cameraPictureOptionsOf', 'cameraPictureOptions',
  'saveCameraFields', 'loadCameraFields', 'cameraEditor', 'cameraText', 'cameraSourceOfKind', 'cameraFnv1a', 'cameraHex', 'cameraCoordinates',
  'cameraHundredths', 'cameraHttpsAddress', 'cameraEntryEntity', 'cameraEntryField', 'cameraParseRecord', 'cameraValidRows',
  'cameraRecordOf', 'cameraRowTitle', 'cameraTileIcon', 'cameraSyncTile', 'renderCameraEntries'].map(extractDeliveredFunction).join('\n');
const copied = form({t_camera_entity: 'camera.door', t_camera_display: '1', t_camera_fit: 'original',
  t_camera_every: '25', t_camera_transparency: '40', t_camera_head: 'dark', t_camera_tap: '1'});
const sent = new Map();
const cameraContext = vm.createContext({
  document: copied, Number, String, Math, Array, TextEncoder,
  CAMERA_MAX_ENTRIES: 8, CAMERA_PLACE_KINDS: ['r', 'c'], cameraEditors: new Map(), sensorMetaCache: {names: {}},
  titleFromEntity: entity => entity, normalizeTileTitle: title => title,
  loadIconColorFields() {}, saveIconColorFields() {}, syncCameraFields() {},
});
vm.runInContext(constants + cameraFunctions, cameraContext);
cameraContext.saveCameraFields('t', {append: (key, value) => sent.set(key, value)});
const clipboard = Object.fromEntries(sent);
// Paste into a fresh form: every picture option comes back.
const pasted = form({t_camera_entity: '', t_camera_display: '0', t_camera_fit: 'fill', t_camera_every: '10',
  t_camera_transparency: '0', t_camera_head: 'auto', t_camera_tap: '0'});
cameraContext.document = pasted;
cameraContext.loadCameraFields('t', clipboard);
assert.deepEqual(['camera_entity', 'camera_display', 'camera_fit', 'camera_every', 'camera_transparency', 'camera_head', 'camera_tap']
  .map(field => pasted.value('t_' + field)), ['camera.door', '1', 'original', '25', '40', 'dark', '1'],
  'a pasted Camera tile keeps its picture options');
// The stored fields still load (GET /api/tiles).
const stored = {sensor_entity: 'camera.door', sensor_display_mode: 1, sensor_decimals: 1, image_slideshow_sec: 20,
  key_code: 30, key_modifier: 1 | 2};
assert.deepEqual({...cameraContext.cameraPictureOptionsOf(stored)},
  {picture: true, fit: 'fit', every: 20, transparency: 30, head: 'light', tap: '1', shows: ''});

// --- Pixel animation ------------------------------------------------------------------------------
const animation = readRepoFile('src/types/pixelanim/admin.js');
const animationContext = vm.createContext({Number, String, Math, parseInt});
vm.runInContext(['loadAnimationFields'].map(extractDeliveredFunction).join('\n'), animationContext);
const animationForm = form({t_animation_file: 'cat.gif', t_animation_fps: '10', t_animation_fit: '0', t_animation_zoom: '100'});
animationContext.document = animationForm;
animationContext.loadAnimationFields('t', {animation_file: 'cat.gif', animation_fps: '24', animation_fit: '2', animation_zoom: '150'});
assert.equal(String(animationForm.value('t_animation_fps')), '24', 'a pasted animation keeps its speed');
animationContext.loadAnimationFields('t', {scene_alias: 'cat.gif', image_slideshow_sec: 12});
assert.equal(String(animationForm.value('t_animation_fps')), '12', 'a stored animation loads its speed');
assert.ok(animation.includes("formData.append('animation_fps',"), 'the form sends the speed as animation_fps');

console.log('Copy and paste keep the Camera and Pixel animation settings');
