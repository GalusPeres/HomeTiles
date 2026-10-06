// The session request (hello) waits until its answer topic is subscribed.
// A new connection queues every subscribe first (P4: one per 50 ms), the hello
// went out as a priority publish before {base}/stat/secure was subscribed, the
// Bridge's answer was lost and the session only came with the retry 10 s
// later (V2 b239 logs: "attempt 1" twice, then "attempt 2", 15-20 s).
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const channel = read('src/network/secure/command_channel.cpp');

const hello = channel.slice(channel.indexOf('void sendHello() {'), channel.indexOf('void flushHeld() {'));
assert.match(hello, /if \(!g_state->answer_subscribed \|\| !networkManager\.mqttControlIdle\(\)\) \{\s*g_state->hello_requested = true;\s*return;\s*\}/,
  'the hello waits for its subscription and is asked for again');
assert.ok(hello.indexOf('answer_subscribed') < hello.indexOf('mqttEnqueuePublishPriority'), 'checked before anything is sent');

const connected = channel.slice(channel.indexOf('void onMqttConnected() {'), channel.indexOf('void service() {'));
assert.match(connected, /mqttEnqueueSubscribe\(topicFor\(kBridgeLeaf\)\.c_str\(\)\);\s*g_state->answer_subscribed = true;\s*\/\/[^\n]*\n\s*resetSession\(\);/,
  'every connection subscribes the answer topic, then asks');
const pairing = channel.slice(channel.indexOf('void completePairing() {'));
assert.match(pairing, /mqttEnqueueSubscribe\(topicFor\(kBridgeLeaf\)\.c_str\(\)\);\s*g_state->answer_subscribed = true;/,
  'a pairing completed on a running MQTT connection subscribes too, or it would never ask');
assert.match(channel, /if \(!networkManager\.isMqttConnected\(\)\) \{\s*g_state->answer_subscribed = false;[^\n]*\n\s*return;\s*\}/,
  'a lost connection forgets the subscription');

const network = read('src/network/network_manager.cpp');
assert.match(network, /bool HomeTilesNetworkManager::mqttControlIdle\(\) const \{\s*return !g_mqtt_control_queue \|\| uxQueueMessagesWaiting\(g_mqtt_control_queue\) == 0;\s*\}/);
// The worker sends a dequeued subscribe in the same pass and returns before
// it drains publishes, so a hello queued after the control queue emptied
// follows every subscription.
const worker = network.slice(network.indexOf('const bool control_waiting ='));
assert.ok(worker.indexOf('mqtt_client.subscribe(cmd->topic)') < worker.indexOf('xQueueReceive(g_mqtt_publish_queue'),
  'subscriptions go out before publishes of a later pass');

console.log('Session request order: the hello waits for its answer subscription');
