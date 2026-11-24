# Plan: Buddy on a cheap Android phone (on hold)

Idea (Black Friday 2026): a prepaid Android phone, never activated as a phone,
used on Wi-Fi only as a Buddy body. Example: Samsung Galaxy A16 5G (Straight
Talk, $39.88 at Walmart): 6.7" 1080x2340 AMOLED 90 Hz, Exynos 1330 (8 cores,
2.4 GHz), 50 MP camera + 13 MP front camera, 5,000 mAh, IP54. RAM about
4-6 GB depending on the version.

Status: on hold. Check phones in the store first; a 6.7" phone may be too
large for the desk (the Badware head would become a monitor-shaped phone
holder).

## Why
- Cheaper than a CoreS3 (about $60) and much more capable: bigger screen,
  real cameras, better microphones and speaker, a battery to ride out power
  blips.
- The pieces exist already: the Gemini brain (main/gemini.cpp), the face
  (components/face, and the web version in buddy-pc buddy_pc/webapp), and
  Android apps built by GitHub Actions (buddy-pc tv/).

## Brain
- Main brain: Gemini Live over Wi-Fi, the same as brain = gemini on the
  CoreS3 (voice in and out, look tool with the camera, sunglasses tool,
  remember/forget, the clock, follow-ups that ignore videos with "skip").
- A local model on the phone is possible (Gemma 3 1B / Llama 3.2 1B class,
  about 5-15 words a second) but much weaker than Gemini: only as an
  offline fallback when the internet is down.

## Cautions
- AMOLED burn-in from a face shown all day: move the whole face slowly,
  dim or sleep when nobody's around.
- Always on the charger: turn on Samsung's "Protect battery" (stops at 80%)
  so the battery doesn't swell.
- Carrier-locked phones may push an activation screen in setup; usually it
  can be skipped, not guaranteed.
- Hands-free "Hey Buddy" needs a wake-word engine running on the phone
  (more work than the CoreS3, which has one built in).

## Build order (when it's picked up)
1. Android app: the face full screen (monitor or puppy style) + the Gemini
   voice connection and its tools, settings screen (Wi-Fi is the phone's own;
   Gemini key, names, personality, timezone).
2. "Hey Buddy" wake word; burn-in protection; sleep when nobody's around.
3. Optional: offline fallback model.
APK built by GitHub Actions, installed like the TV app (adb or a file).
