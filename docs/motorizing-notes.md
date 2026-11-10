# Notes for motorizing Buddy someday (M5Stack StackChan)

Saved for later, in case Buddy gets a moving head. Nothing here is built yet.

## What StackChan is

M5Stack's open-source AI desktop robot (Kickstarter early 2026, then sold as
SKU K151, about $99, or $125 with the remote; roughly $120 shipped). It uses a
**stock CoreS3** as the head, the same as Buddy. Code:
https://github.com/m5stack/StackChan (firmware, app, remote, server: all MIT).
Board support: https://github.com/m5stack/StackChan-BSP.
Docs: https://docs.m5stack.com/en/StackChan

## The robot body (replaces the DIN base, on the CoreS3's bottom M-Bus)

- **Two feedback servos**: pan (360 degrees, continuous) and tilt (90 degrees).
  Feetech **SCS-series serial bus servos** (SCSCL protocol), on **UART1 at
  1 Mbaud, GPIO 6 and 7** (`_scs_bus.begin(UART_NUM_1, 1000000, 6, 7)` in
  `firmware/main/hal/hal_servo.cpp`). Does not clash with Port C (radar, GPIO
  17/18).
- 550 mAh battery, USB-C for power and data.
- 12 RGB LEDs (two rows), IR transmitter and receiver, three-zone touch panel
  (Si12T), NFC, PY32 IO expander (`PY32IOExpander_Class`).
- Size about 54.0 x 70.5 x 61.5 mm, about 187 g.

## Code worth borrowing (MIT)

- `firmware/main/hal/drivers/FTServo_Arduino/`: Feetech servo driver (MIT,
  ftservo): `WritePos(id, pos, time, speed)`, `ReadPos`, `ReadMove`,
  `EnableTorque`, `WritePWM` (continuous mode), `ReadCurrent`.
- `firmware/main/hal/hal_servo.cpp`: angle limits, zero-position calibration
  saved in NVS, stall protection, PWM (continuous) mode for the pan servo.
- `firmware/main/stackchan/` motion helpers (host-tested), `hal_head_touch.cpp`
  (touch gestures), Si12T, PY32 IO expander drivers, IR/NFC handling.

## Their firmware vs Buddy's

Their AI is xiaozhi-esp32 (v2.5.2) talking to the xiaozhi cloud by default
(`OTA_URL` = https://api.tenclass.net/xiaozhi/ota/), plus M5Stack's own server
for the app. Built with ESP-IDF 6.1. Not a replacement for Buddy (Home
Assistant + Ollama, memory, radar, puppy voice, settings page, away mode);
the plan would be to add servo support to Buddy's firmware instead.

## Ideas for a moving Buddy

- Turn toward you when the radar sees you arrive; look around when bored.
- Nod while you talk or when he agrees; tilt his head when confused.
- Wag (small fast pan wiggle) for happy; droop (tilt down) for sad or sleepy.
- A new puppy shell: the CoreS3 rides in the head shell on the pan/tilt, the
  StackChan body hides in the torso (and brings a battery).
