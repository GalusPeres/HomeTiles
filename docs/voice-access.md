# Voice Access per Display { data-toc-label="Voice access" }

Home Assistant gives every voice device the same reach: one list of entities **exposed to Assist**, shared by all of them. A display in a child's room can switch off the kitchen lights as easily as the one in the kitchen.

The HomeTiles Bridge can give each display its own reach instead. You choose per display which devices its voice assistant may **control** and which it may only **read**. Everything else in the home stays off limits for that display.

## How It Works { data-toc-label="How it works" }

The Bridge adds an LLM API called **HomeTiles room control**. An LLM conversation agent (Ollama, OpenAI, Anthropic, Google Gemini, ...) uses it instead of Home Assistant's own **Assist** API:

1. The display sends your voice to Home Assistant, which turns it into text as usual.
2. Home Assistant asks the LLM. The Bridge gives the LLM only the devices the speaking display may use, with their current states, and a fixed set of tools.
3. When the LLM wants to act, it calls a tool. The Bridge checks the call again: is the device on this display's lists, do your own words name that device, and can the device do it? Only then does the Bridge call the Home Assistant action. Otherwise the LLM gets a refusal and says so.

The LLM decides what you meant, but it cannot act on anything by itself: every action passes the Bridge's check. A model that ignores its instructions still cannot reach a device outside the lists.

## Requirements

- A HomeTiles Bridge version with voice access and a paired display with [Assist](device-ui.md).
- A conversation agent integration that offers **Control Home Assistant** (LLM APIs), for example [Ollama](https://www.home-assistant.io/integrations/ollama/), OpenAI, Anthropic or Google Gemini. Use a model that supports tool calls (for Ollama, for example `qwen3:8b` or `llama3.1`).

## Set It Up { data-toc-label="Setup" }

### 1. Choose the Display's Devices { data-toc-label="1. Devices" }

Open **Settings → Devices & services → HomeTiles Bridge**, select **Configure** on the display's entry and choose **Voice access (this display)**.

| Field | Meaning |
| --- | --- |
| **Can control: areas** | Everything in these areas the voice may switch, dim, move or set. |
| **Can control: labels** | Everything with one of these labels. |
| **Can control: entities** | Single entities, also from other areas. |
| **Can read: areas / labels / entities** | The voice may tell their state, but not change them. |

- Nothing selected means **no devices**.
- Controlled devices can be read as well.
- Areas and labels leave out hidden, configuration and diagnostic entities (for example *Factory reset* buttons or status LEDs). To use one of those, pick it as a single entity.
- **Locks and alarm panels are only read**, never controlled by voice, even in a control list.
- A control list can hold lights, switches, input booleans, fans, covers, valves, thermostats, media players, humidifiers, sirens, remotes, automations (turned on or off), scenes, scripts and buttons. Other entities in a control list (sensors, for example) are read.
- A **script** runs all of its actions when the voice starts it. Add only scripts that are fine for that room.
- Keep the lists short. A small model works better with a dozen devices than with a whole area of 200 entities.

A label is the easiest way to give several rooms' displays the same extra devices: put a label such as *Kids voice* on the hallway light and the porch light, and pick that label on each display.

### 2. Add a Conversation Agent { data-toc-label="2. Agent" }

For Ollama: open **Settings → Devices & services → Ollama** and choose **Add conversation agent**. Other LLM integrations have the same steps.

- **Name**: for example *Room limited*.
- **Model** and **Instructions**: as for your other agents.
- **Control Home Assistant**: tick **only HomeTiles room control**.

!!! danger "Do not tick Assist as well"
    **Assist** (Device control) reaches every exposed entity. With it ticked as well, the agent can use Home Assistant's own tools and the display's limits no longer hold.

One agent serves every display: the Bridge looks at which display speaks and uses that display's lists.

### 3. Add a Voice Assistant { data-toc-label="3. Voice assistant" }

Open **Settings → Voice assistants → Add assistant**:

- **Conversation agent**: the agent from step 2.
- **Prefer handling commands locally**: **off**. Home Assistant's own command handling reaches every exposed entity and would bypass the display's lists.
- Speech-to-text and text-to-speech: as for your other assistants.

### 4. Pick It on the Display { data-toc-label="4. Display" }

Open the display's device in Home Assistant (**Settings → Devices & services → HomeTiles Bridge → the display**) and set its **Assistant** select to the voice assistant from step 3. Also put the device in its room's area: the voice then prefers that room's devices when you do not name one.

Displays that should keep full access stay on your normal voice assistant.

## Try It { data-toc-label="Try it" }

Say to the display:

1. A device on its control list: "Turn off the desk lights." It switches.
2. A device on its read list: "What is the temperature outside?" It answers.
3. A device on no list: "Turn off the kitchen light." It says it cannot do that, and the light stays as it is.
4. "Set a timer for two minutes." The timer shows and rings on this display.

## Timers

Timers keep working and show on the display that started them. A timer cannot carry a command to run when it ends ("in ten minutes, turn off the lights"): that command would run through Home Assistant's own command handling and its exposure list.

## What Is and Is Not Protected { data-toc-label="Limits" }

- Every action of the voice is checked against the display's lists at the moment it runs, so a change to the lists applies at once.
- A request that does not come from a display, such as text typed into Assist in the Home Assistant app, gets no devices through this API.
- If the LLM maps a device you named to a similar one it may use ("worktable light" to the *Desk Lights*), the Bridge refuses: your own words must fit the chosen device at least as well as any device outside the lists.
- The limit belongs to the voice assistant the display uses. A Home Assistant administrator who switches the display's **Assistant** select back to an unlimited assistant lifts it.
- The display's tiles are not affected: what a tile can control is set by the [Bridge entity lists](bridge.md#entity-configuration) and the [Web Admin password](web-admin.md#web-admin-password).

## Troubleshooting

| What happens | What to check |
| --- | --- |
| "I cannot control any devices" for everything | The display's lists are empty, or the display's **Assistant** select is not on the limited assistant. |
| It answers but never acts | The model does not call tools. Use a model with tool support; for Ollama, raise the context window (`num_ctx`) if the lists are long. |
| A simple command acts on a device outside the lists | **Prefer handling commands locally** is on, or **Assist** is ticked under **Control Home Assistant**. |
| Commands are slow | Every command goes through the LLM. A smaller or faster model, or a shorter list, helps. |
| It refuses a device that is on the list | Name it as it is called in Home Assistant, or add an alias to the entity. |
