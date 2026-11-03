# Claude Gadget: desk companion

A small desk buddy built on the **M5Stack CoreS3** (ESP32-S3). It shows an
animated face that blinks, glances around, changes expressions, and reacts to
touch, motion, and sound.

![expressions](preview/face_sheet.png)

## What it does right now

| You do | It does |
|---|---|
| Tap the screen | Happy for a few seconds |
| Hold a finger on it ("petting") | Love eyes and blush |
| Shake it | Confused / dizzy |
| Tilt it | Eyes look toward the tilt |
| Clap or make a sudden loud noise | Surprised |
| Talk near it | Listening face while you talk, then a happy "I heard you" |
| Leave it alone for 1 minute | Gets sleepy (taps, sound, or motion wake it up) |
| Leave it alone for 3 minutes | Dims the screen |
| Let the battery drop below 10% | Stays sleepy until charged |

It also blinks (sometimes twice), glances around on its own, and "breathes"
with a slight bob.

The mic only measures loudness for now. Speech recognition is the next step.

## Project layout

```
lib/Face/          Portable face engine (no Arduino code, also builds on a PC)
  Canvas.h         Drawing interface the engine renders into
  Face.h/.cpp      Expressions, tweening, blinking, gaze, talking mouth
src/main.cpp       CoreS3 firmware: display, touch, IMU, mic, behavior
tools/preview/     Desktop preview that renders the face to PNG/GIF
platformio.ini     Build configuration
```

## Build and flash

1. Install [VS Code](https://code.visualstudio.com/) and the **PlatformIO IDE** extension,
   or the command-line tool with `pip install platformio`.
2. Plug the CoreS3 in over USB-C. Use a data cable, not a charge-only one.
3. From this folder:
   ```
   pio run -t upload
   pio device monitor      # optional: view the serial log
   ```
   The first build downloads the ESP32 toolchain and M5Unified, which takes a few minutes.

If the upload can't find the board, hold the reset button on the side for
about 3 seconds until the green LED lights. That puts it in download mode.
Then upload again.

## Preview on your computer

Requires `g++`, Python 3 and Pillow (`pip install pillow`):

```
python3 tools/preview/make_preview.py
```

This writes `preview/face_sheet.png` (every expression) and
`preview/face_anim.gif` (an animated tour with blinking, glancing, and talking).

## Tuning

- **Expressions:** `paramsFor()` in `lib/Face/Face.cpp`. Each expression is a set of
  numbers (eye size, lid angles, mouth curve, color...). Any two blend smoothly.
- **Reactions and timings:** constants at the top of `src/main.cpp`.
- **Tilt goes the wrong way:** flip the signs of `tx` / `ty` in `handleImu()`.
- **Too sensitive or deaf to sound:** adjust `kClapRatio` and `kVoiceRatio`.

## Roadmap

1. ~~Face engine and desktop preview~~
2. ~~Touch, motion, and sound reactions~~
3. Test on hardware and tune thresholds
4. Offline wake word and voice commands (ESP-SR), e.g. "good morning" → happy
5. Sound effects through the speaker (chirps, yawns)
6. Proximity sensor and camera: wake up when someone sits down
7. Cloud conversation: speech-to-text → LLM → emotion tag drives the face, with spoken replies
