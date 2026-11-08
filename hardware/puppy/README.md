# Puppy body for Buddy

![assembled](images/assembled.png)

A chibi, sitting puppy body for the M5Stack CoreS3 with its DIN base
(33.5 mm deep), with floppy ears, pink inner ears and paw pads, room for Grove
cables, and an LD2410 presence radar in the chest.

Remixed from **"Puppy Stand - M5Stack Core S3" by John White**
(Printables), licensed **CC BY 4.0**
(https://creativecommons.org/licenses/by/4.0/). The openings for the CoreS3's
buttons and ports are placed where the original frame has them. This remix is
shared under the same license.

## Parts (`stl/`, already turned to their printing position)

| File | Size (mm) | Notes |
|---|---|---|
| `buddy_head.stl` | 71 x 44 x 78 | **Standing upright** on its bottom, as it sits on Buddy. The dome prints as a smooth top surface and the bottom edges are a 45-degree chamfer, so the outside needs no supports. The top of the screen window is a 50 mm bridge and the side windows 13.5 mm bridges: fine with normal bridging settings (add a support enforcer there if yours sag). Inside, the chamber's ceiling is 45-degree slopes and a short flat bridge, all hidden. Tip: variable layer height smooths the top of the dome |
| `buddy_back_cap.stl` | 71 x 73 x 20 | **Back side down**, posts and groove facing up. The back edges are a 45-degree chamfer: no supports |
| `buddy_ear_left.stl` + `buddy_ear_left_pink.stl` | 32 x 55 x 4 | Flat side down, no supports. Load both as **one object with two parts**; pink part on the top |
| `buddy_ear_right.stl` + `buddy_ear_right_pink.stl` | 32 x 55 x 4 | Same |
| `buddy_body.stl` + `buddy_body_pink.stl` | 78 x 77 x 70 | Standing on its open bottom. Load both as **one object with two parts** (pink = paw pads). **No supports**: every underside, inside and out, slopes at 45 degrees or less down to the bed, and the tail lies on the floor |

Suggested: PLA, 0.2 mm layers, 3 walls, 15 % infill. In Anycubic Slicer Next
(or any Orca-based slicer), select a part and its `_pink` file together and
answer **Yes** to "load as a single object with multiple parts", then give the
pink part its own filament slot on the ACE Pro.

### Screws

All M3, with heads that sit flush with or below the surface:

| Qty | Screw | Joins |
|---|---|---|
| 2 | M3 x 10 countersunk (flat head, hex socket) | head to neck, down through the floor of the CoreS3 pocket |
| 2 | M3 x 16 socket head | back cap to the CoreS3 (into the DIN base's brass nuts) |
| 2 | M3 x 16 socket head | back cap to the head's side walls |

The holes into plastic are printed 2.5 mm pilots: the screws cut their own
thread the first time. Stop at snug, since PLA threads strip if forced.

## Inside

![cut-away](images/cutaway.png)

- **Head:** the CoreS3 slides in from the back, screen first, against the front
  lip. Above it, a chamber gives a Grove plug about 10 mm of room; behind it a
  5 mm gap (more where the DIN base's back panel is recessed) carries cables
  down to the bottom opening, which leads into the neck. Side windows (both
  sides) for the power button, USB-C, Port A and the speaker, as on the
  original. Two countersunk holes in the pocket floor take the neck screws.
- **Back cap:** a U-shaped groove fits over a rim around the left, bottom and
  right of the head's back opening. Two screws through posts in the middle go
  into the brass nuts in the DIN base's back panel and clamp the CoreS3 against
  the front lip, so it can't slide back when you touch the screen; two near the
  side edges hold the cap to the head. Their heads sit in counterbores.

  ![back](images/back.png)
- **Body:** hollow, open at the bottom. The neck on top is a hollow oval tube
  that fits into the socket under the head, with two bosses for the head
  screws. In the middle of the body, two grooved cheeks hold the radar board
  upright (22 x 16 mm), antenna side forward, about 20 mm behind the chest,
  which the 24 GHz radar sees through.

  ![body from below](images/body_below.png)

## Assembly

1. Push each ear's tab into the slot on the side of the head (left ear on the
   left), with a drop of super glue.
2. Plug the Grove-to-jumper cable onto the radar (red VCC, black GND, yellow
   TX, white RX). Drop the board **down through the neck** into the grooves,
   antenna side (the side with the chip and gold patches) facing forward, pins
   up, until it rests on the grooves' bottom ends. A dab of hot glue keeps it
   from rattling. Leave the cable sticking up out of the neck.
3. Set the head on the neck (cable up through the bottom opening) and, from
   the back with a hex key, drive the two M3 x 10 countersunk screws down
   through the pocket floor into the neck. Their heads must end flush with the
   floor so the CoreS3 sits flat.
4. Plug the cable into Port C (blue), then slide the CoreS3 in, screen first,
   letting the cable fold up into the space above it. The USB-C power cable
   goes in through the left side window.
5. Put the back cap on and screw it to the CoreS3 with two M3 x 16 mm screws
   through the middle posts into the DIN base's brass nuts.
6. Drive an M3 x 16 mm screw into each of the two holes near the cap's side
   edges, into the head. Snug is enough.

To take the CoreS3 out (for the SD card, for example), remove the four back
screws. Note: the DIN base's DC jack and power switch face down into the neck,
as on the original, so they can't be reached while assembled.

## Changing it

The model is code: `puppy.py` (needs `pip install manifold3d trimesh numpy`).

    python3 puppy.py stl/

Sizes worth checking against your parts are near the top of the file: the
CoreS3 pocket (`CORE`, `CORE_D`), the room above it (`CHAMBER`, `ROOF_TOP`),
the fit clearances (`CLEAR` 0.25 mm, `RIM_CLEAR` 0.35 mm), the radar board
(`RADAR_W`, `RADAR_T`), and, by the back cap, `PANEL_D` (28 mm, screen to the
recessed back panel), `RIB_PLAY` (1.0 mm: how far the posts stop short of the
panel), and the screw posts: `SCREW_X` (the brass nuts are 23-24 mm apart,
11.75 mm each side of centre; the holes are slotted 0.6 mm either way),
`SCREW_L` (16 mm screws) and `SCREW_BITE` (4 mm into the nut). The cap's side
screws are at `FIX_X`/`FIX_Z` and the neck screws at `NECK_SCREW_X`.
`concept.py` is the earlier proportions model.
