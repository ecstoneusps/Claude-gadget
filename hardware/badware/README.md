# Badware head for Buddy (CoreS3)

![head](images/head.png)

A monitor head that holds an M5Stack CoreS3 with its DIN base, for the
Badware figure. The rest of the body is the original model printed at 135 %.

Remixed from **"Die of Death badware articulated figure" by GigaBagel
(@GigaBagel777)** on Printables (https://www.printables.com/model/1410750),
licensed **CC BY 4.0** (https://creativecommons.org/licenses/by/4.0/). The
original file is in `original/`. The CoreS3 pocket, its side windows and the
screwed back cap come from the puppy head in `../puppy` (remix of "Puppy
Stand - M5Stack Core S3" by John White, CC BY 4.0). This remix is shared under
the same license.

Rebuild the STLs: `pip install manifold3d trimesh numpy`, then
`python3 badware.py stl/`.

## Parts

| File | Size (mm) | Print |
|---|---|---|
| `stl/badware_head.stl` + `badware_head_green.stl` + `badware_head_red.stl` | 80 x 73 x 47 | **Face down** (the front on the bed). Load the three as **one object with three parts** and give the green and red parts their own filament (the block on his side). No supports: the screen's sloped frame and the block sit on the bed, the CoreS3 pocket opens upward. The side windows' far edges are 44 mm bridges |
| `stl/badware_back_cap.stl` | 80 x 73 x 20 | Back face down, posts up. No supports |
| `stl/badware_neck.stl` | 16 x 14 x 25 | Peg down, ball up (like the original's head). Print it in the **same material as the body** (PETG) so the ball holds in its socket |
| The original's torso, arms, legs and chest plate (`original/Just_Badware.stl`) | | **Scaled to 135 %** in the slicer, as the designer says in **PETG** (the ball joints wear out in PLA). Leave out the original head |

Suggested for the head and cap: PLA, 0.2 mm layers, 3 walls, 15 % infill.

## Assembly

1. **Neck:** push the plug's square peg into the socket under the head (it's
   snug: 0.25 mm clearance) with a drop of super glue, ball pointing down.
2. **CoreS3:** turned around (camera at the top of the screen, Ports B and C
   facing down), slide it in from the back, screen first, against the front
   frame. `screen_flip = on` in settings.txt (the default).
3. **Back cap:** fit its groove over the head's rim. 2 x M3 x 16 socket-head
   screws through the posts into the DIN base's brass nuts (they hold the
   CoreS3 against the front), 2 x M3 x 16 into the head's floor at the bottom
   corners (printed 2.5 mm pilot holes: stop at snug).
4. Snap the head's ball into the torso's neck socket.

## Things to know

- **Weight:** the CoreS3 with its DIN base is much heavier than the
  original's plastic head. The scaled-up ball joint may not hold the head up,
  and the figure may tip forward. If so: a drop of glue in the neck joint (it
  won't turn, but holds), and pose the legs apart or glue the feet to a base.
- **Size:** the head is 80 mm wide and 73 mm tall; with the 135 % body he
  stands about 20 cm tall.
- **Print a test first:** the head's pocket is the puppy's (fitted on test
  prints), but the new parts (the neck peg, the ball in the scaled torso's
  socket) haven't been printed yet.
