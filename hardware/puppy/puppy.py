"""Buddy's puppy body: printable parts for the M5Stack CoreS3 + DIN base.

Remix of "Puppy Stand - M5Stack Core S3" by John White (Printables,
CC BY 4.0, https://creativecommons.org/licenses/by/4.0/). The openings for
the CoreS3's buttons and ports are placed where the original frame has them.

    pip install manifold3d trimesh numpy
    python3 puppy.py out/          # writes the STLs (print-oriented) + an assembly preview

Coordinates (assembled): x across Buddy, y front (-) to back (+), z up, mm.
The screen faces -y.
"""
import os
import sys

import numpy as np
import trimesh
from manifold3d import Manifold

SEG = 64          # smoothness of round shapes
CLEAR = 0.25      # clearance between parts that fit together

# --- The CoreS3 with its DIN base (measured: 54 x 54 x 33.5 mm) -------------------
CORE = 54.8                 # pocket width/height (0.4 mm clearance per side)
CORE_D = 34.0               # pocket depth for the 33.5 mm device
GAP_BEHIND = 8.0            # space behind the device for cables (and to slide it out)
CHAMBER = 18.0              # room above Ports B and C for Grove plugs to bend

# --- Head ---------------------------------------------------------------------------------
FRONT, BOTTOM, TOP, BACK = 2.4, 6.0, 6.0, 6.0
SIDE = 8.0                  # side walls (thick for the rounded look)
SHELL = 3.0                 # dome wall
HEAD_Z0 = 66.0              # underside of the head above the desk (just clear of the body)
head_x = CORE / 2 + SIDE
head_y0, head_y1 = -FRONT, CORE_D + GAP_BEHIND + BACK
P0 = HEAD_Z0 + BOTTOM                       # floor of the pocket
head_z1 = P0 + CORE + CHAMBER + TOP
SPLIT_Y = CORE_D + GAP_BEHIND               # head / back cap joint
LIP = 3.0                                   # how far the head's rim reaches into the cap

# --- Neck (part of the body; the head sits on it) --------------------------------------
NECK_W, NECK_D, NECK_R = 44.0, 32.0, 15.0   # a rounded oval
NECK_CY = 28.0
NECK_WALL = 4.0
SOCKET = 3.5                                # depth of the neck socket in the head

# --- LD2410C radar board (22 x 16 mm, 1.6 mm thick) in the chest ------------------
RADAR_W, RADAR_T = 22.0, 1.6


def sphere(x, y, z, r):
    return Manifold.sphere(r, SEG).translate((x, y, z))


def blob(points):
    """Smooth rounded solid: the hull of spheres (x, y, z, r)."""
    return Manifold.batch_hull([sphere(*p) for p in points if p[3] > 0])


def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def rbox(x0, x1, y0, y1, z0, z1, r):
    """Box with every edge rounded to radius r."""
    return blob([(x, y, z, r) for x in (x0 + r, x1 - r) for y in (y0 + r, y1 - r) for z in (z0 + r, z1 - r)])


def rrect_prism(cx, cy, w, d, r, z0, z1):
    """Rounded rectangle (w x d, corner radius r) extruded from z0 to z1."""
    posts = [Manifold.cylinder(z1 - z0, r, r, SEG).translate((cx + sx * (w / 2 - r), cy + sy * (d / 2 - r), z0))
             for sx in (-1, 1) for sy in (-1, 1)]
    return Manifold.batch_hull(posts)


def above(z):
    return box(-500, 500, -500, 500, z, 500)


# =========================================================================================
# Head
# =========================================================================================
def head_spheres(shrink):
    """Tight rounding around the screen, a soft dome on top; `shrink` offsets inward."""
    lo, hi = 8.0, 24.0
    pts = []
    for sx in (-1, 1):
        for y, sy in ((head_y0, 1), (head_y1, -1)):
            pts.append((sx * (head_x - lo), y + sy * lo, HEAD_Z0 + lo, lo - shrink))
            pts.append((sx * (head_x - lo), y + sy * lo, P0 + CORE, lo - shrink))
            pts.append((sx * (head_x - hi), y + sy * hi, head_z1 - hi, hi - shrink))
    return pts


head_outer = blob(head_spheres(0))


def head_cavity(erode=0.0):
    """Pocket for the CoreS3 (open to the back) plus the dome chamber above it."""
    pocket = rbox(-CORE / 2 + erode, CORE / 2 - erode, 0 + erode, SPLIT_Y + 20, P0 + erode, P0 + CORE - erode, 3)
    dome = blob(head_spheres(SHELL + erode)) ^ above(P0 + CORE - 1)
    return pocket + dome


cavity = head_cavity()

screen_window = box(-25.0, 25.0, head_y0 - 1, 1, P0 + CORE / 2 - 25.6, P0 + CORE / 2 + 25.6)

# Side windows (power button, USB-C and Port A on the left, speaker on the right),
# as in the original frame: 44 mm tall, over the front 1..15 mm of the device.
side_windows = Manifold()
for sx in (-1, 1):
    w = rbox(-6, 6, 1.0, 14.5, P0 + CORE / 2 - 22, P0 + CORE / 2 + 22, 2.5)
    side_windows += w.translate((sx * (CORE / 2 + 3), 0, 0))

# Bottom: DC jack and power switch (as in the original), and the way into the neck.
bottom_windows = (box(-19, 19, 11, 30, HEAD_Z0 - 1, P0 + 1)
                  + rrect_prism(0, NECK_CY, NECK_W - 2 * NECK_WALL, NECK_D - 2 * NECK_WALL, 3, HEAD_Z0 - 1, P0 + 1))
neck_socket = rrect_prism(0, NECK_CY, NECK_W + 2 * CLEAR, NECK_D + 2 * CLEAR, NECK_R + CLEAR, HEAD_Z0 - 1, HEAD_Z0 + SOCKET)

# Back cap joint: the head keeps a rim reaching LIP mm into the cap.
behind_split = box(-500, 500, SPLIT_Y, 500, -500, 500)
rim = (blob(head_spheres(1.2)) - cavity) ^ box(-500, 500, SPLIT_Y, SPLIT_Y + LIP, -500, 500)

# Ear tabs go through slots in the sides of the dome.
EAR_Y, EAR_TOP = 24.0, head_z1 - 16


def ear_tab(side, grow=0.0):
    return box(min(side * (head_x - 12), side * (head_x + 2)) - grow, max(side * (head_x - 12), side * (head_x + 2)) + grow,
               EAR_Y - 2.0 - grow, EAR_Y + 0.95 + grow, EAR_TOP - 7 - grow, EAR_TOP + 2 + grow)


head = ((head_outer - behind_split) + rim) - cavity - screen_window - side_windows - bottom_windows - neck_socket
head = head - ear_tab(-1, CLEAR) - ear_tab(1, CLEAR)

groove = (blob(head_spheres(1.2 - CLEAR)) - head_cavity(CLEAR)) ^ box(-500, 500, SPLIT_Y - 1, SPLIT_Y + LIP + CLEAR, -500, 500)
back_cap = (head_outer ^ behind_split) - groove


# =========================================================================================
# Ears: flat leaves hanging from the upper back of the head, with a pink inner ear.
# =========================================================================================
def ear(side):
    shape = [(0, 0, 0, 8), (side * 6, 0, -22, 11), (side * 8, 0, -40, 9.5)]
    leaf = blob(shape).scale((1, 0.32, 1)) ^ box(-100, 100, -100, 1.0, -100, 100)  # flat back for printing
    front_layer = leaf - leaf.translate((0, 1.2, 0))
    inlay = front_layer ^ blob([(x, y, z, r - 4.0) for x, y, z, r in shape]).scale((1, 3, 1))
    place = lambda m: m.rotate((0, -side * 12, 0)).translate((side * (head_x - 4), EAR_Y, EAR_TOP))
    leaf, inlay = place(leaf), place(inlay)
    # Where the ear meets the head it follows the head's surface, plus the tab.
    piece = (leaf - inlay - blob(head_spheres(-CLEAR))) + ear_tab(side)
    return piece, inlay - blob(head_spheres(-CLEAR))


# =========================================================================================
# Body: sitting chibi puppy with the neck on top and the radar in the chest.
# =========================================================================================
WALL = 2.4
body_parts = [(0, 22, 46, 20), (0, 30, 22, 24), (0, 20, 18, 22), (0, 36, 18, 20)]
chest_parts = [(0, 6, 34, 13), (0, 4, 22, 13)]
torso = blob(body_parts).scale((1.15, 1, 1))
chest = blob(chest_parts)
inside = blob([(x, y, z, r - WALL / 1.15) for x, y, z, r in body_parts]).scale((1.15, 1, 1)) + \
    blob([(x, y, z, r - WALL) for x, y, z, r in chest_parts])
# The hollow's ceiling rises at 45 degrees to the neck, so the body prints
# upright with no supports inside it.
inside = inside ^ Manifold.cylinder(80, 80, 0, SEG).translate((0, NECK_CY, -6))

paws = Manifold()
for sx in (-1, 1):
    paws += blob([(sx * 13, 4, 26, 8), (sx * 13, -2, 7, 9)]) + sphere(sx * 13, -5, 5, 9.5)
hinds = Manifold()
for sx in (-1, 1):
    hinds += blob([(sx * 26, 30, 16, 13), (sx * 27, 20, 7, 9.5), (sx * 26, 42, 10, 9)])
tail = blob([(0, 58, 22, 5), (0, 64, 32, 4.5), (0, 62, 40, 4)])

neck_top = HEAD_Z0 + SOCKET
neck = rrect_prism(0, NECK_CY, NECK_W, NECK_D, NECK_R, 38, neck_top)  # starts inside the body
neck_hole = rrect_prism(0, NECK_CY, NECK_W - 2 * NECK_WALL, NECK_D - 2 * NECK_WALL, 3, 30, neck_top + 1)

# Radar holder: two grooved rails and a top stop behind the chest wall. The board
# slides up from the open bottom, antenna side facing forward, ~1 mm behind the wall.
RADAR_Z0, RADAR_Z1 = 18.0, 41.0
holder = box(-RADAR_W / 2 - 3, RADAR_W / 2 + 3, -8.0, 0.5, RADAR_Z0, RADAR_Z1 + 3) ^ chest.translate((0, 2, 0))
holder = holder - box(-RADAR_W / 2 - 0.4, RADAR_W / 2 + 0.4, -5.0, -5.0 + RADAR_T + 0.2, RADAR_Z0 - 1, RADAR_Z1)
holder = holder - box(-RADAR_W / 2 + 1.0, RADAR_W / 2 - 1.0, -20, 5, RADAR_Z0 - 1, RADAR_Z1)

pads = Manifold()
for sx in (-1, 1):
    pads += Manifold.sphere(5.2, SEG).scale((1, 0.35, 0.8)).translate((sx * 13, -14.3, 6))
    for dx, dz in ((-4.2, -0.6), (0, 0.8), (4.2, -0.6)):
        pads += Manifold.sphere(2.1, SEG).scale((1, 0.4, 1)).translate((sx * 13 + dx, -13.4, 12.2 + dz))

floor_cut = box(-500, 500, -500, 500, -500, 0)
body = (torso + chest + paws + hinds + tail + neck) - inside - neck_hole
body = (body + (holder - neck_hole)) - floor_cut
body_pink = pads
body = body - pads


# =========================================================================================
# Output
# =========================================================================================
def clean(m, min_volume=0.5):
    """Drop stray fragments (slivers left by boolean operations)."""
    keep = [p for p in m.decompose() if p.volume() >= min_volume]
    return Manifold.compose(keep) if keep else m


def to_trimesh(m):
    mesh = clean(m).to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts))


def oriented(m, how):
    """Turn a part to its printing position: 'front_down' (head), 'joint_down' (back cap: the flat joint face,
    which is its front), 'flat_back' (ears), 'as_is' (body)."""
    if how in ("front_down", "joint_down"):
        m = m.rotate((90, 0, 0))     # the part's front-most face (-y) ends up on the bed
    elif how == "flat_back":
        m = m.rotate((-90, 0, 0))    # the part's back-most face (+y) ends up on the bed
    return m


def save(parts, outdir):
    os.makedirs(outdir, exist_ok=True)
    report = []
    for name, (m, how, group) in parts.items():
        tm = to_trimesh(oriented(m, how))
        # Shared offset per group so a part and its pink inlay stay aligned.
        report.append((name, tm, group))
    offsets = {}
    for name, tm, group in report:
        lo = tm.bounds[0]
        offsets.setdefault(group, (-(tm.bounds[0][0] + tm.bounds[1][0]) / 2, -(tm.bounds[0][1] + tm.bounds[1][1]) / 2, -lo[2]))
    for name, tm, group in report:
        tm.apply_translation(offsets[group])
        path = os.path.join(outdir, f"{name}.stl")
        tm.export(path)
        lo, hi = tm.bounds
        print(f"{name:22s} {hi[0]-lo[0]:6.1f} x {hi[1]-lo[1]:6.1f} x {hi[2]-lo[2]:6.1f} mm  "
              f"volume {tm.volume/1000:6.1f} cm3  watertight {tm.is_watertight}")


if __name__ == "__main__":
    outdir = sys.argv[1] if len(sys.argv) > 1 else "out"
    ear_l, ear_l_pink = ear(-1)
    ear_r, ear_r_pink = ear(1)
    save({
        "buddy_head": (head, "front_down", "head"),
        "buddy_back_cap": (back_cap, "joint_down", "cap"),
        "buddy_ear_left": (ear_l, "flat_back", "ear_l"),
        "buddy_ear_left_pink": (ear_l_pink, "flat_back", "ear_l"),
        "buddy_ear_right": (ear_r, "flat_back", "ear_r"),
        "buddy_ear_right_pink": (ear_r_pink, "flat_back", "ear_r"),
        "buddy_body": (body, "as_is", "body"),
        "buddy_body_pink": (body_pink, "as_is", "body"),
    }, outdir)
    # Everything in place, for previews.
    assembled = {"assembly_main": head + back_cap + ear_l + ear_r + body,
                 "assembly_pink": ear_l_pink + ear_r_pink + body_pink}
    for name, m in assembled.items():
        to_trimesh(m).export(os.path.join(outdir, f"{name}.stl"))
    print("assembly preview written")
