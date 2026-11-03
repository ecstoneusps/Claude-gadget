# Claude Gadget: desk companion

A small desk buddy built on the **M5Stack CoreS3** (ESP32-S3). It shows an
animated face that blinks, glances around, and changes expression. It reacts to
touch, motion and sound, and understands spoken commands offline after you say
**"Hey Buddy"**.

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
| good night / go to sleep | Sleeps and dims until "Hey Buddy", a tap, or a shake |
| wake up | Startled awake |

If it doesn't catch a command, it looks confused for a moment.

All recognition runs on the device with Espressif's
[ESP-SR](https://github.com/espressif/esp-sr) models: the WakeNet "Hey Buddy"
wake word and the MultiNet7 English command recognizer. Nothing is sent to the
internet. To change the phrases, edit `kCommandPhrases` in
`components/companion/Companion.cpp`. Phrases must be lowercase letters and spaces only.

## Other reactions

| You do | It does |
|---|---|
| Tap the screen | Happy |
| Hold a finger on it ("petting") | Love eyes and blush |
| Shake it | Confused / dizzy |
| Tilt it | Eyes look toward the tilt |
| Clap | Surprised |
| Talk near it (without "Hey Buddy") | Listens, then a happy "I heard you" |
| Leave it alone for 1 minute | Gets sleepy |
| Leave it alone for 3 minutes | Dims the screen |
| Let the battery drop below 10% | Stays sleepy until charged |

## Project layout

```
components/face/       Face engine: expressions, tweening, blinking, gaze (portable C++)
components/companion/  The "brain": events -> expressions, voice command list (portable C++)
main/main.cpp          CoreS3 glue: display, touch, IMU, battery, main loop
main/voice.cpp         ESP-SR pipeline: mic -> audio front-end -> wake word -> commands
tests/                 Native unit tests for the brain (run on a PC)
tools/preview/         Renders the face to PNG/GIF on a PC
partitions.csv         Flash layout (4 MB app + 8 MB speech models)
sdkconfig.defaults     Board, PSRAM and speech model settings
```

`components/face` and `components/companion` contain no ESP32 code, so they
build and run on a PC for testing and previews.

## Build and flash

This project uses **ESP-IDF** (v5.1 or newer), Espressif's official framework.
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

## Tests and preview (on a PC)

```
tests/run_tests.sh                      # unit tests for the brain (needs g++)
python3 tools/preview/make_preview.py   # face images (needs g++ and Pillow)
```

## Tuning

- **Expressions:** `paramsFor()` in `components/face/Face.cpp`.
- **Reactions:** `Companion::onCommand()` and the tunables in `components/companion/Companion.h`.
- **Wrong commands get triggered:** raise `kMinCommandProb` in `main/main.cpp`. The
  serial log shows the confidence of each command it hears.
- **Different wake word:** run `idf.py menuconfig` → *ESP Speech Recognition* →
  *Load Multiple Wake Words*. Choose one, such as "Hi M Five", "Jarvis" or "Computer",
  and turn off "Hey Buddy".
- **Tilt goes the wrong way:** flip the signs of `tx` / `ty` in `handleImu()` in `main/main.cpp`.

## Roadmap

1. ~~Face engine and desktop preview~~
2. ~~Touch, motion and sound reactions~~
3. ~~Offline wake word and voice commands~~
4. Test on hardware and tune thresholds
5. Use both microphones (beamforming) for better pickup across a room
6. Sound effects through the speaker (chirps, giggles, yawns)
7. Proximity sensor and camera: wake up when someone sits down
8. Cloud conversation: speech-to-text → LLM → emotion tag drives the face, with spoken replies
