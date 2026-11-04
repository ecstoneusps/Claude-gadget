# Claude Gadget: desk companion

[![Build](https://github.com/ecstoneusps/Claude-gadget/actions/workflows/build.yml/badge.svg)](https://github.com/ecstoneusps/Claude-gadget/actions/workflows/build.yml)

A small desk buddy built on the **M5Stack CoreS3** (ESP32-S3). It shows an
animated face that blinks, glances around, and changes expression. It chirps,
giggles, purrs and yawns through its speaker, reacts to touch, motion and sound,
and understands spoken commands offline after you say **"Hey Buddy"**.

![expressions](preview/face_sheet.png)

## Voice commands

Say **"Hey Buddy"**. The face switches to its listening look, and you then have
about 6 seconds to say one of these:

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
| good night / go to sleep | Yawns, then sleeps and dims until "Hey Buddy", a tap, or a shake |
| wake up | Startled awake |
| be quiet / quiet please | Turns its sounds off |
| sound on / you can talk | Turns its sounds back on |

If it doesn't catch a command, it looks confused for a moment.

All recognition runs on the device with Espressif's
[ESP-SR](https://github.com/espressif/esp-sr) models: the WakeNet "Hey Buddy"
wake word and the MultiNet7 English command recognizer. Nothing is sent to the
internet. To change the phrases, edit `kCommandPhrases` in
`components/companion/Companion.cpp`. Phrases must be lowercase letters and spaces only.

It listens with **both of the CoreS3's microphones**. ESP-SR's blind source
separation uses the two channels to pull your voice apart from other sounds,
like a TV, fan or music, and the wake word detector checks both. To compare
against a single mic, set `kUseBothMics = false` in `main/voice.cpp`. The serial
log prints the active audio chain at boot, such as `|SE(BSS)| -> |VAD| -> |WakeNet|`.

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

On the CoreS3 the speaker and microphone can't run at the same time, so the
buddy stops listening while it plays a sound (under a second). It also can't
hear itself. Volume is `kVolume` in `main/sound.cpp`.

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
main/sound.cpp         Plays sound effects, switching the audio hardware mic <-> speaker
main/proximity.cpp     LTR-553 proximity sensor driver (presence logic is in components/companion/Presence.*)
main/camera.cpp        Camera setup and face detection task
components/espdl_all_ops/  Build helper: makes esp-dl compile every neural-network op the face model may need
tests/                 Native unit tests for the brain (run on a PC)
tools/preview/         Renders the face to PNG/GIF on a PC
partitions.csv         Flash layout (4 MB app + 8 MB speech models; ~2.8 MB and ~3.1 MB used)
sdkconfig.defaults     Board, PSRAM and speech model settings
```

`components/face`, `components/companion` and `components/sfx` contain no ESP32
code, so they build and run on a PC for testing and previews.

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
   python -m esptool --chip esp32s3 write_flash @flash_args
   ```

The artifact also contains `dependencies.lock`, the exact library versions that
build used. Commit it to the repo root to pin those versions for future builds.

## Tests and preview (on a PC)

```
tests/run_tests.sh                      # unit tests for the brain (needs g++)
python3 tools/preview/make_preview.py   # face images + sound WAVs (needs g++ and Pillow)
```

## Tuning

- **Expressions:** `paramsFor()` in `components/face/Face.cpp`.
- **Reactions:** `Companion::onCommand()` and the tunables in `components/companion/Companion.h`.
- **Wrong commands get triggered:** raise `kMinCommandProb` in `main/main.cpp`. The
  serial log shows the confidence of each command it hears.
- **Different wake word:** run `idf.py menuconfig` → *ESP Speech Recognition* →
  *Load Multiple Wake Words*. Choose one, such as "Hi M Five", "Jarvis" or "Computer",
  and turn off "Hey Buddy".
- **Doesn't notice you sitting down:** the proximity sensor (just above the screen)
  is infrared and short-range, roughly a hand's length to an arm's length depending on
  what you're wearing. The serial log prints `prox <reading>, baseline <empty desk>`
  every 10 seconds. Compare the numbers with you seated and away, then lower `minRise`
  / `riseFraction` in `components/companion/Presence.h`, or angle the buddy toward you.
- **Eyes look away from you instead of at you:** set `kCameraMirrored = true` in
  `main/main.cpp`. To make them turn further or less far toward you, change `kFaceGazeGain`.
- **Tilt goes the wrong way:** flip the signs of `tx` / `ty` in `handleImu()` in `main/main.cpp`.

## Roadmap

1. ~~Face engine and desktop preview~~
2. ~~Touch, motion and sound reactions~~
3. ~~Offline wake word and voice commands~~
4. Test on hardware and tune thresholds
5. ~~Use both microphones for better pickup across a room~~
6. ~~Sound effects through the speaker (chirps, giggles, yawns)~~
7. ~~Proximity sensor: wake up when someone sits down~~
8. ~~Camera: look toward your face~~
9. Remember the mute setting across restarts
10. Cloud conversation: speech-to-text → LLM → emotion tag drives the face, with spoken replies
