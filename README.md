# Claude Gadget: desk companion

[![Build](https://github.com/ecstoneusps/Claude-gadget/actions/workflows/build.yml/badge.svg)](https://github.com/ecstoneusps/Claude-gadget/actions/workflows/build.yml)

A small desk buddy built on the **M5Stack CoreS3** (ESP32-S3). It shows an
animated face that blinks, glances around, and changes expression. It chirps,
giggles, purrs and yawns through its speaker, reacts to touch, motion and sound,
and understands spoken commands offline after you say **"Hey Buddy"**.

![expressions](preview/face_sheet.png)

## Talking with it (AI conversation)

With Wi-Fi and a Home Assistant server set up, you can have a real
conversation: say **"Hey Buddy"**, then ask anything ("what's a good name for
a cat?", "tell me about Saturn"). It looks thoughtful while it works out an
answer, then replies **out loud** with its mouth moving along and its face
showing the mood of the answer. After it answers it keeps listening for about
5 seconds, so you can carry on talking without saying "Hey Buddy" again.

How it works: what you say goes to Home Assistant's voice assistant over its
websocket API. Whisper turns it into text, and the conversation agent (a
language model on your own PC, through Ollama) writes the answer, starting with
an emotion tag such as `[laughing]` that picks the face. Piper turns the answer
into speech. All of it runs on your own machines. The setup guide covers the
Home Assistant side step by step.

- The face and mood commands below (look left, wink at me, be cool, good night,
  be quiet...) still run on the buddy itself when you say just that. Everything
  else, including "tell me a joke" and "how are you", goes to the AI.
- It remembers the conversation for 5 minutes, so "what about Jupiter?" works
  after a question about Saturn.
- Without Wi-Fi, or if Home Assistant can't be reached, it falls back to the
  built-in commands.
- "Be quiet" turns off its sound effects, but it still speaks its answers.

Fill in `wifi_name`, `wifi_password`, `ha_url` and `ha_token` in `settings.txt`
(see [Settings](#settings-microsd-card)). Keep the card private: the token works
like a password for the Home Assistant user it belongs to.

## Voice commands

Say **"Hey Buddy"**. The face switches to its listening look, and you then have
about 6 seconds to say one of these. With the AI connected, these work the same
way for the face and mood commands; the chatty ones (hello, how are you, jokes,
compliments) are answered by the AI instead:

| Say | It does |
|---|---|
| hello / good morning / hi there | Happy |
| how are you | Happy (sleepy if the battery is low) |
| i love you / you are cute | Love eyes and blush |
| thank you | Love eyes |
| good job / well done / you are awesome | Happy |
| bad robot / stop it | Sad |
| tell me a joke / make me laugh | Laughs |
| look left / look right | Looks that way |
| be happy / be sad / be angry / be surprised | Shows that expression |
| be cool / make a silly face (or "act goofy") | Heavy-lidded smirk / lopsided eyes with its tongue out |
| wink at me / give me a wink / do a wink | Winks |
| good night / go to sleep | Yawns, then sleeps and dims until "Hey Buddy", a tap, or a shake |
| wake up | Startled awake |
| be quiet / quiet please | Turns its sounds off (remembered after a restart) |
| louder / speak up / turn it up / volume up / talk louder | Volume up 10 (saved to `volume` in settings.txt) |
| quieter / turn it down / volume down / talk quieter | Volume down 10 (saved too; never below 10) |
| sound on / you can talk | Turns its sounds back on |

If it doesn't catch a command, it looks confused for a moment.

All recognition runs on the device with Espressif's
[ESP-SR](https://github.com/espressif/esp-sr) models: the WakeNet "Hey Buddy"
wake word and the MultiNet7 English command recognizer. Nothing is sent to the
internet (with the AI set up, what you say after "Hey Buddy" goes to your own
Home Assistant server). To change the phrases, edit `kCommandPhrases` in
`components/companion/Companion.cpp`. Phrases must be lowercase letters and spaces only.

By default it listens with **one microphone**. `two_microphones = on` in
`settings.txt` uses both: ESP-SR's blind source separation then pulls your voice
apart from other sounds, and the wake word detector checks both channels. On the
CoreS3 that costs much more CPU than Espressif's benchmarks suggest. In testing
it slowed the face animation to 5 fps, so try it if it struggles to hear you in a
noisy room. The serial log prints the active audio chain at boot.

## Expressions and emotions

The face has 18 expressions (above). Each is a set of numbers, such as eye size,
lid angles, mouth curve, tears, tongue and color, so any two blend smoothly into
each other.

It also understands the **21 emotion names** that AI voice servers like
[xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)'s attach to their replies:
neutral, happy, laughing, funny, sad, angry, crying, loving, embarrassed, surprised,
shocked, thinking, winking, cool, relaxed, delicious, kissy, confident, sleepy,
silly and confused. `Companion::onEmotion("laughing")` shows the closest expression
with a matching sound. That's the hook a future "talk with AI" feature will use.
The mapping is `kEmotionMap` in `components/face/Face.cpp`.

## Sound effects

Every sound is synthesized in code (`components/sfx/Sfx.cpp`), so there are no
audio files to manage, and the mouth moves along with each sound. Hear them on a
PC in `preview/sounds/`:

| Sound | When |
|---|---|
| chirp | Heard "Hey Buddy", or sound turned back on |
| boop | Tapped; looking left or right |
| happy | Greeted, praised, or told to be happy |
| giggle | Asked for a joke |
| love | "I love you", "thank you" |
| purr | Being petted (repeats while you hold it) |
| sad / angry / surprised | Scolded or told to show that feeling; a clap surprises it |
| confused | Shaken, or didn't catch a command |
| yawn | Dozing off or told good night |
| wake | Startled awake |

Audio is **full duplex**: the speaker and both microphones run at the same
time on one I2S port, so it keeps listening while it chirps. That covers "Hey
Buddy" and commands. Its own sounds are ignored by the clap and "someone is
talking" reactions. The approach follows
[xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)'s CoreS3 driver (MIT),
using Espressif's `esp_codec_dev`. Volume is `volume` in `settings.txt`.

## Other reactions

| You do | It does |
|---|---|
| Sit down in front of it (or wave near the top of the screen) | Wakes up, looks up at you and says hello |
| Walk away | Dozes off after about 20 seconds instead of a minute |
| Move around in front of it | Its eyes follow your face (camera) |
| Come back into view while it's dozing | Wakes up and says hello (camera) |
| Tap the screen | Happy |
| Hold a finger on it ("petting") | Love eyes and blush |
| Shake it | Confused / dizzy |
| Tilt it | Eyes look toward the tilt |
| Clap | Surprised |
| Talk near it (without "Hey Buddy") | Listens, then a happy "I heard you" |
| Leave it alone for 1 minute | Gets sleepy |
| Leave it alone for 3 minutes | Dims the screen |
| Let the battery drop below 10% | Stays sleepy until charged |

## Camera

The CoreS3's camera, a GC0308 above the screen, runs Espressif's
[face detection model](https://github.com/espressif/esp-dl/tree/master/models/human_face_detect)
on the device: 5 frames a second while awake, 1 while dozing, and none after
"good night". The eyes turn toward the biggest face it sees. **Images never leave
the device and are never stored.** Each frame is analyzed in memory and
discarded. The serial log prints the face position every few seconds.

## Project layout

```
components/face/       Face engine: expressions, tweening, blinking, gaze (portable C++)
components/companion/  The "brain": events -> expressions + sounds, voice command list (portable C++)
components/sfx/        Sound effect synthesizer (portable C++)
main/main.cpp          CoreS3 glue: display, touch, IMU, battery, main loop
main/voice.cpp         ESP-SR pipeline: mic -> audio front-end -> wake word -> commands
main/audio_io.cpp      Full-duplex audio: ES7210 mics + AW88298 speaker amp on one I2S port
main/sound.cpp         Sound effect player task (streams to the speaker while the mics listen)
main/i2c_bus.cpp       The shared internal I2C bus (M5Unified, camera and audio chips)
main/proximity.cpp     LTR-553 proximity sensor driver (presence logic is in components/companion/Presence.*)
main/camera.cpp        Camera setup and face detection task
main/settings.cpp      Settings kept across restarts in flash (NVS): currently mute
main/sdcard.cpp        microSD card (shares the display's SPI bus)
main/wifi.cpp          Wi-Fi station (reconnects on its own)
main/assistant.cpp     Home Assistant voice assistant client: streams speech, gets the answer, plays it
components/assist/     Reply parsing (emotion tags, text cleanup), command matching, WAV decoding (portable C++)
components/config/     settings.txt parser and the default file (portable C++)
sdcard/                Files for the SD card: settings.txt as written to a fresh card
components/espdl_all_ops/  Build helper: makes esp-dl compile every neural-network op the face model may need
tests/                 Native unit tests for the brain (run on a PC)
tools/preview/         Renders the face to PNG/GIF on a PC
partitions.csv         Flash layout (7 MB app + 8 MB speech models; ~4.1 MB and ~3.1 MB used)
sdkconfig.defaults     Board, PSRAM and speech model settings
```

`components/face`, `components/companion`, `components/sfx`, `components/config`
and `components/assist` contain no ESP32 code, so they build and run on a PC for testing and previews.

## Build and flash

This project uses **ESP-IDF** (tested with v5.4.1), Espressif's official framework.
ESP-SR's speech models install automatically with it.

1. Install ESP-IDF. The easiest way is VS Code with the **ESP-IDF** extension
   (Espressif's own); its setup wizard installs everything. Alternatively,
   follow the [ESP-IDF getting started guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/).
2. Plug the CoreS3 in over USB-C. Use a data cable, not a charge-only one.
3. In an ESP-IDF terminal, from this folder:
   ```
   idf.py set-target esp32s3      # first time only
   idf.py build
   idf.py flash monitor
   ```
   The first build downloads M5Unified and ESP-SR through the component
   manager. `idf.py flash` also writes the speech models to the `model` partition.
   Later, `idf.py app-flash` skips rewriting the models and is faster.

If flashing can't find the board, hold the reset button on the side for about
3 seconds until the green LED lights. That puts it in download mode.

The serial monitor logs every wake word and command, with a confidence value,
which helps with tuning.

### Flash a ready-made build (no ESP-IDF needed)

GitHub Actions builds the firmware on every push (`.github/workflows/build.yml`).
To flash it without installing ESP-IDF:

1. Open the repository's **Actions** tab, pick the latest green **Build** run, and
   download the **desk-companion-firmware** artifact. Unzip it.
2. Install esptool with `pip install esptool`.
3. With the CoreS3 plugged in:
   ```
   cd desk-companion-firmware/build
   python -m esptool --chip esp32s3 write-flash '@flash_args'
   ```
   Keep the quotes around `'@flash_args'`. Windows PowerShell treats a bare `@`
   as special and silently drops the argument, so esptool reports a missing
   address and filename.

The artifact also contains `dependencies.lock`, the exact library versions that
build used. Commit it to the repo root to pin those versions for future builds.

## Tests and preview (on a PC)

```
tests/run_tests.sh                      # unit tests for the brain and settings parser (needs g++)
python3 tools/preview/make_preview.py   # face images + sound WAVs (needs g++ and Pillow)
```

## Settings (microSD card)

Put a FAT32-formatted microSD card in the slot on the side. On its first start
with the card, the buddy writes a commented **`settings.txt`** to it (the same
file as [`sdcard/settings.txt`](sdcard/settings.txt) in this repo, which you can
also copy onto the card yourself). Edit it on your computer, put the card back,
and restart the buddy.

| Setting | What it changes |
|---|---|
| `volume`, `brightness`, `dim_brightness` | Speaker and screen levels, 0-100 |
| `sleep_after_seconds`, `dim_after_seconds`, `sleep_after_leaving_seconds` | How quickly it gets sleepy and dims |
| `command_confidence` | Raise to around 0.3 if it reacts to things you didn't say. The serial log shows each command's confidence |
| `say.<command> = phrase, phrase` | Extra ways to say any command, e.g. `say.hello = howdy, whats up buddy` |
| `face_tracking`, `camera_mirrored`, `face_gaze` | Camera on/off, fix eyes that look away from you, how far they turn |
| `face_model` | `fast` (MSR+MNP, default) checks about 14 times a second; `accurate` (ESPDet-Pico) finds you more often per check but takes ~0.5 s each and slows the animation |
| `camera_preview` | Show what the camera sees, with a green box around a detected face (for troubleshooting) |
| `proximity`, `proximity_sensitivity` | Proximity sensor on/off; lower sensitivity numbers notice you from further away |
| `flip_tilt_x`, `flip_tilt_y` | Fix tilt reactions that go the wrong way |
| `wifi_name`, `wifi_password` | Your Wi-Fi (2.4 GHz). Put the password in "quotes" if it contains a `#` |
| `ha_url` | Home Assistant's address with its IP and port, e.g. `http://192.168.1.20:8123` (`.local` names don't work) |
| `ha_token` | A long-lived access token for the Home Assistant user the buddy logs in as |
| `ha_pipeline` | The voice assistant to use, by its name in Home Assistant (default `Buddy`) |
| `conversation` | `off` uses only the built-in commands, even with the above filled in |
| `follow_up` | `off` makes it go back to waiting for "Hey Buddy" after each answer |

When an update adds new settings, they're added to the end of your
`settings.txt` (at their default values, with their explanations) the next
time it starts, so the file always lists everything you can change. A setting
you commented out with `#` is left alone.

Mistakes, such as a typo or an out-of-range number, are listed in the serial log,
and that line is ignored. Without a card, it uses the built-in defaults. The
mute setting ("be quiet" / "sound on") is kept in the board's own flash and
works without a card.

## Tuning in code

- **Expressions:** `paramsFor()` in `components/face/Face.cpp`.
- **Reactions:** `Companion::onCommand()` and the tunables in `components/companion/Companion.h`.
- **Built-in phrases:** `kCommandPhrases` in `components/companion/Companion.cpp`.
- **Default settings:** `components/config/BuddyConfig.h`. After changing one, run
  `tests/run_tests.sh --write-settings` to refresh `sdcard/settings.txt`.
- **Different wake word:** run `idf.py menuconfig` → *ESP Speech Recognition* →
  *Load Multiple Wake Words*. Choose one, such as "Hi M Five", "Jarvis" or "Computer",
  and turn off "Hey Buddy".
- **Doesn't notice you sitting down:** the proximity sensor (just above the screen)
  is infrared and short-range, roughly a hand's length to an arm's length depending on
  what you're wearing. The serial log prints `prox <reading>, baseline <empty desk>`
  every 10 seconds. Compare the numbers with you seated and away, then lower
  `proximity_sensitivity` in settings.txt, or angle the buddy toward you.

## Roadmap

1. ~~Face engine and desktop preview~~
2. ~~Touch, motion and sound reactions~~
3. ~~Offline wake word and voice commands~~
4. ~~Test on hardware and tune thresholds~~
5. ~~Use both microphones for better pickup across a room~~
6. ~~Sound effects through the speaker (chirps, giggles, yawns)~~
7. ~~Proximity sensor: wake up when someone sits down~~
8. ~~Camera: look toward your face~~
9. ~~Remember the mute setting across restarts~~
10. ~~Settings file on the SD card~~
11. Recognize you (face recognition, enrolled face stored on the card)
12. ~~AI conversation: speech-to-text → LLM → emotion tag drives the face, with spoken replies~~
13. Home Assistant sensors and controls over MQTT (desk occupied, light level, battery, mood)
