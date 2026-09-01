# Top Stories — NYTimes for Pebble

A Pebble watchapp that lists the New York Times most prominent stories and lets
you read each one's headline and summary on the watch.

## What it does

- On launch, the phone (PebbleKit JS) calls the NYT **Top Stories** API and
  streams the results to the watch.
- A small "NYT Top Stories" banner stays fixed at the top of the screen. The
  headline list scrolls underneath it, not with it.
- The list shows headlines with their section. Every row is one fixed height
  and the title is one line. When a headline is too wide for its row, it
  marquee-scrolls while that row is selected (PebbleSuperProductivity's
  pattern). On round screens (chalk, gabbro) the title's start is inset by
  however much the circular bezel demands at that row's height, computed from
  the display geometry. On emery (Pebble Time 2), which renders third-party
  apps at the "Large" content size, the fonts and row height step up a notch.
- Selecting a headline opens a scrollable card with the title, the abstract
  (the 1–2 sentence summary the API provides), and the byline.
- Full article text is not offered by this API, so the card shows the abstract.
- On touchscreen hardware (Pebble Time 2 and newer) the app opts into the
  system's touch-navigation bridge, so taps and swipes drive the same
  select/up/down/back handling the buttons use.
- A **Backlight** setting (Settings → Display) can keep the screen lit longer
  than the watch's own default: 5/15/30/60 seconds after a button press, or
  always on while the app is open. Same scheme as PebbleSuperProductivity's
  backlight setting.

## Setup

1. Get a free key at <https://developer.nytimes.com>. Create an app and enable
   the **Top Stories API**.
2. Install the app, open its **Settings** in the Pebble phone app, paste the
   API key, and pick a section.
3. Re-open the watchapp (or close settings — it refetches automatically).

## Build

```
pebble build
pebble install --emulator basalt          # or --phone <ip>
```

Set the key in the emulator with:

```
pebble emu-app-config --emulator basalt
```

## Layout

| File | Role |
|------|------|
| `src/c/main.c` | Watch UI: headline menu + detail card, AppMessage receiver |
| `src/pkjs/index.js` | Fetches the NYT API, streams articles to the watch |
| `src/pkjs/config.js` | Clay settings page (API key, section) |
| `resources/images/menu_icon.png` | Launcher icon: the `{T}` mark from the NYT dev portal (25x25) |
| `tools/make_icon.py` | Rebuilds the icon from `tools/nyt-devportal-logo.jpg` (`python3 tools/make_icon.py`; args: `T` for the bare blackletter T, `white` for white ink) |

## Platforms

Targets aplite, basalt, chalk, diorite, emery, and gabbro (Pebble Time 2, the
only touchscreen platform here). `pebble-clay` hasn't shipped a release since
2016 and doesn't know about gabbro; `wscript` patches its vendored platform
folders in before every build (it ships no native code, just empty per-platform
placeholder files, so this is safe — see the comment in `wscript`).

## Message protocol

JS sends `AppKeyCount`, then one message per article with `AppKeyIndex`,
`AppKeyTitle`, `AppKeyAbstract`, `AppKeySection`, `AppKeyByline`. Errors come
back as `AppKeyError` with a short string the watch displays.

`BacklightMode` is sent as its own standalone message (on launch and after
settings close), independent of the article fetch, so it still applies if the
fetch fails.
