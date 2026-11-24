"""Badware's head for the M5Stack CoreS3 + DIN base.

Remix of "Die of Death badware articulated figure" by GigaBagel
(@GigaBagel777, Printables, https://www.printables.com/model/1410750,
CC BY 4.0, https://creativecommons.org/licenses/by/4.0/): a new monitor head
that holds a CoreS3, with the original neck ball (scaled to fit the body
printed at 135 %). The CoreS3 pocket, its windows and the screwed back cap
come from ../puppy/puppy.py (fitted on test prints).

    pip install manifold3d trimesh numpy
    python3 badware.py stl/        # writes the STLs (print-oriented) + a preview

Coordinates (assembled): x across (+x = his left, the viewer's right), y front
(-) to back (+), z up, mm. The screen faces -y.
"""
import os
import sys

import numpy as np
import trimesh
from manifold3d import Manifold, Mesh

SEG = 64
CLEAR = 0.25
RIM_CLEAR = 0.35
HERE = os.path.dirname(os.path.abspath(__file__))
BODY_SCALE = 1.35        # print the original body parts at 135 %

# --- The CoreS3 with its DIN base (as in the puppy) -------------------------------
CORE = 54.8              # pocket width/height (0.4 mm clearance per side)
CORE_D = 34.0            # pocket depth for the 33.5 mm device
GAP_BEHIND = 5.0         # room behind it for cables

# --- The monitor ------------------------------------------------------------------
FRONT = 5.0              # the frame in front of the screen (the screen sits back, like his)
BOTTOM = 12.0            # floor: room for the neck plug's socket
TOP = 6.0
HEAD_X = 40.0            # half width: 80 mm wide, a bit wider than tall, like his monitor
EDGE_R = 2.5             # rounding of the box's edges
P0 = BOTTOM              # floor of the pocket
HEAD_Z1 = P0 + CORE + TOP
SPLIT_Y = CORE_D + GAP_BEHIND
BACK = 10.0
LIP = 3.0
Y0, Y1 = -FRONT, SPLIT_Y + BACK

# --- Neck plug: the original ball on a square peg glued into the floor -----------
PEG = 14.0               # square peg
PEG_H = 9.0              # how deep it goes into the floor
NECK_Y = 17.0            # under the CoreS3's middle (its weight)


def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def sphere(x, y, z, r):
    return Manifold.sphere(r, SEG).translate((x, y, z))


def rbox(x0, x1, y0, y1, z0, z1, r):
    return Manifold.batch_hull([sphere(x, y, z, r) for x in (x0 + r, x1 - r) for y in (y0 + r, y1 - r)
                                for z in (z0 + r, z1 - r)])


def from_trimesh(tm):
    return Manifold(Mesh(vert_properties=np.asarray(tm.vertices, np.float32),
                         tri_verts=np.asarray(tm.faces, np.uint32)))


def to_trimesh(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(vertices=mesh.vert_properties[:, :3], faces=mesh.tri_verts)


def outer(shrink=0.0):
    """The monitor: a rounded box in front of the split, and behind it a back
    that narrows like an old CRT's (more at the top and sides than the bottom,
    so the bottom screws stay inside)."""
    s = shrink
    front = rbox(-HEAD_X + s, HEAD_X - s, Y0 + s, SPLIT_Y + 1, 0 + s, HEAD_Z1 - s, max(EDGE_R - s, 0.3))
    taper = Manifold.batch_hull([
        box(-HEAD_X + s, HEAD_X - s, SPLIT_Y - 1, SPLIT_Y, s, HEAD_Z1 - s),
        box(-HEAD_X + 8 + s, HEAD_X - 8 - s, Y1 - 0.01, Y1, 2 + s, HEAD_Z1 - 6 - s),
    ])
    return front + taper


# The CoreS3 pocket (open to the back), its rounded long edges like the device's.
def pocket(erode=0.0):
    rr = 3.0
    posts = [Manifold.cylinder(SPLIT_Y + 20 - erode, rr, rr, SEG).rotate((-90, 0, 0))
             .translate((sx * (CORE / 2 - erode - rr), erode, z))
             for sx in (-1, 1) for z in (P0 + erode + rr, P0 + CORE - erode - rr)]
    return Manifold.batch_hull(posts)


cavity = pocket()
CZ = P0 + CORE / 2       # the screen's middle (height)

# The screen sits back in a sloped frame, like his monitor's: 50 x 51.2 at the
# screen (the puppy's window), opening 45 degrees to the front.
screen_window = Manifold.batch_hull([box(-25.0, 25.0, -1.0, 1.0, CZ - 25.6, CZ + 25.6),
                                     box(-25.0 - FRONT + 1, 25.0 + FRONT - 1, Y0 - 0.01, Y0, CZ - 25.6 - FRONT + 1,
                                         CZ + 25.6 + FRONT - 1)])

# Side windows (power button, USB-C and Port A on one side, the speaker on the
# other), as in the puppy, through the thicker side walls.
side_windows = Manifold()
for sx in (-1, 1):
    side_windows += rbox(CORE / 2 - 3, HEAD_X + 1, 1.0, 14.5, CZ - 22, CZ + 22, 2.5).scale((sx, 1, 1)) \
        if sx > 0 else rbox(-HEAD_X - 1, -CORE / 2 + 3, 1.0, 14.5, CZ - 22, CZ + 22, 2.5)

# The neck plug's socket, under the CoreS3.
neck_socket = box(-PEG / 2 - CLEAR, PEG / 2 + CLEAR, NECK_Y - PEG / 2 - CLEAR, NECK_Y + PEG / 2 + CLEAR, -1, PEG_H + CLEAR)

behind_split = box(-500, 500, SPLIT_Y, 500, -500, 500)
slab = lambda y0, y1: box(-500, 500, y0, y1, -500, 500)
below = lambda z: box(-500, 500, -500, 500, -500, z)
RIM_TOP = P0 + CORE - 0.5
rim = ((outer(1.2) ^ slab(SPLIT_Y, SPLIT_Y + LIP - 1.0)) + (outer(1.7) ^ slab(SPLIT_Y + LIP - 1.0, SPLIT_Y + LIP))) - cavity
rim = rim ^ below(RIM_TOP)
groove = ((outer(1.2 - RIM_CLEAR) ^ slab(SPLIT_Y - 1, SPLIT_Y + LIP + RIM_CLEAR)) - pocket(RIM_CLEAR)) ^ below(RIM_TOP + RIM_CLEAR)

# The red and green block on his side (the viewer's right, his left), near the
# top and flush with the front: it prints on the bed, and it only overlaps the
# top 4 mm of the side window, so the speaker can still be heard.
BLOCK_OUT, BLOCK_D, BLOCK_Z0 = 9.0, 14.0, HEAD_Z1 - 16.0
BLOCK_MID = BLOCK_Z0 + 8.0
block_green = box(HEAD_X - 0.5, HEAD_X + BLOCK_OUT, Y0, Y0 + BLOCK_D, BLOCK_MID, HEAD_Z1)
block_red = box(HEAD_X - 0.5, HEAD_X + BLOCK_OUT, Y0, Y0 + BLOCK_D, BLOCK_Z0, BLOCK_MID)

head = ((outer() - behind_split) + rim) - cavity - screen_window - side_windows - neck_socket
head = head - block_green - block_red   # (the block parts fill their own space)

# --- Back cap: screws into the DIN base's brass nuts clamp the CoreS3 forward,
# two more tie it to the head (all as in the puppy).
PANEL_D, RIB_PLAY = 28.0, 1.0
SCREW_X, SLOT, SCREW_L, SCREW_BITE, POST_R = 11.75, 0.6, 16.0, 4.0, 5.0
posts, screw_holes = Manifold(), Manifold()
head_at = PANEL_D - SCREW_BITE + SCREW_L
for sx in (-1, 1):
    x = sx * SCREW_X
    along_y = lambda m: m.rotate((-90, 0, 0)).translate((x, 0, CZ))
    posts += along_y(Manifold.cylinder(SPLIT_Y + 2 - PANEL_D - RIB_PLAY, POST_R, POST_R, SEG).translate((0, 0, PANEL_D + RIB_PLAY)))
    slot = lambda m: Manifold.batch_hull([along_y(m).translate((dx, 0, 0)) for dx in (-SLOT, SLOT)])
    screw_holes += slot(Manifold.cylinder(60, 1.75, 1.75, SEG).translate((0, 0, PANEL_D - 10)))
    screw_holes += slot(Manifold.cylinder(30, 3.2, 3.2, SEG).translate((0, 0, head_at)))
    screw_holes += slot(Manifold.cylinder(1.45, 1.75, 3.2, SEG).translate((0, 0, head_at - 1.45)))

FIX_X, FIX_Z, FIX_L = 26.0, P0 - 5.0, 16.0
FIX_SEAT = SPLIT_Y + LIP + RIM_CLEAR + 1.2
fix_holes, fix_pilots = Manifold(), Manifold()
for sx in (-1, 1):
    along_y = lambda m: m.rotate((-90, 0, 0)).translate((sx * FIX_X, 0, FIX_Z))
    fix_holes += along_y(Manifold.cylinder(20, 1.7, 1.7, SEG).translate((0, 0, SPLIT_Y - 1)))
    fix_holes += along_y(Manifold.cylinder(20, 3.2, 3.2, SEG).translate((0, 0, FIX_SEAT)))
    fix_holes += along_y(Manifold.cylinder(1.5, 1.7, 3.2, SEG).translate((0, 0, FIX_SEAT - 1.5)))
    fix_pilots += along_y(Manifold.cylinder(FIX_L + 1, 1.25, 1.25, SEG).translate((0, 0, FIX_SEAT - FIX_L - 1)))

head = head - fix_pilots
back_cap = ((outer() ^ behind_split) - groove) + posts - screw_holes - fix_holes


# --- Neck plug: the original model's neck (its ridges and ball), scaled for the
# 135 % body, on a square peg. Prints peg down, ball up, like the original.
def neck_plug():
    src = trimesh.load(os.path.join(HERE, "original", "Just_Badware.stl"))
    head0 = sorted(src.split(only_watertight=False), key=lambda p: -p.bounding_box.volume)[0]
    top = head0.bounds[1, 2]
    base_z = 44.0                                   # where the neck leaves the original head
    neck = head0.slice_plane([0, 0, base_z - 0.01], [0, 0, 1], cap=True)
    cx, cy = neck.bounds[:, :2].mean(0)
    neck.apply_translation((-cx, -cy, -base_z))
    neck.apply_scale(BODY_SCALE)
    ball = from_trimesh(neck).translate((0, 0, PEG_H))
    peg = box(-PEG / 2, PEG / 2, -PEG / 2, PEG / 2, 0, PEG_H + 0.5)
    print(f"neck: {(top - base_z) * BODY_SCALE:.1f} mm of the original's neck and ball, scaled {BODY_SCALE}", file=sys.stderr)
    return peg + ball


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    # Print orientation: the head and its block face down (front on the bed);
    # the back cap back down; the neck plug peg down.
    face_down = lambda m: m.rotate((90, 0, 0))
    back_down = lambda m: m.rotate((-90, 0, 0))
    plug = neck_plug()
    parts = {
        "badware_head": face_down(head),
        "badware_head_green": face_down(block_green),
        "badware_head_red": face_down(block_red),
        "badware_back_cap": back_down(back_cap),
        "badware_neck": plug,
    }
    # The head and its block parts share one placement (load them together as one object).
    zmin = min(to_trimesh(parts[k]).bounds[0, 2] for k in ("badware_head", "badware_head_green", "badware_head_red"))
    for name, m in parts.items():
        tm = to_trimesh(m)
        dz = -zmin if name.startswith("badware_head") else -tm.bounds[0, 2]
        tm.apply_translation((0, 0, dz))
        if name in ("badware_back_cap", "badware_neck"):
            c = tm.bounds.mean(0)
            tm.apply_translation((-c[0], -c[1], 0))
        tm.export(os.path.join(outdir, name + ".stl"))
        ext = tm.extents
        print(f"{name}: {ext[0]:.1f} x {ext[1]:.1f} x {ext[2]:.1f} mm, watertight {tm.is_watertight}", file=sys.stderr)
    # Assembled, for a look.
    asm = [to_trimesh(head), to_trimesh(block_green), to_trimesh(block_red), to_trimesh(back_cap),
           to_trimesh(plug.rotate((180, 0, 0)).translate((0, NECK_Y, PEG_H)))]
    return asm


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "stl"))
