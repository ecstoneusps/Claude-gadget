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
from manifold3d import CrossSection, Manifold

SEG = 64          # smoothness of round shapes
CLEAR = 0.25      # clearance between parts that fit together
RIM_CLEAR = 0.35  # the head's rim in the back cap's groove (0.25 needed trimming on the test print)

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
    """Tight rounding around the screen, a soft dome on top; `shrink` offsets inward.

    The head prints standing on its bottom, so the bottom edges are a 45-degree
    chamfer (small spheres on the bed, inset so the slope up to the rounded
    corners is 45 degrees) instead of a rounding that would overhang the bed.
    """
    lo, hi = 8.0, 24.0
    foot = 0.6 * lo + 0.5   # bottom-edge inset that gives a 45-degree slope up to the lo spheres
    pts = []
    for sx in (-1, 1):
        for y, sy in ((head_y0, 1), (head_y1, -1)):
            pts.append((sx * (head_x - foot), y + sy * foot, HEAD_Z0 + 0.5, 0.5 - shrink))
            pts.append((sx * (head_x - lo), y + sy * lo, HEAD_Z0 + lo, lo - shrink))
            pts.append((sx * (head_x - lo), y + sy * lo, P0 + CORE, lo - shrink))
            pts.append((sx * (head_x - hi), y + sy * hi, head_z1 - hi, hi - shrink))
        # The back cap prints back face down, so the back edges are a 45-degree
        # chamfer too: small spheres on the back face, 0.414 r beyond each
        # rounded corner's centre (where a 45-degree line meets the rounding).
        yb = head_y1 - 0.5
        for cx, cz, r, ux, uz in ((head_x - lo, HEAD_Z0 + lo, lo, 1, -1), (head_x - lo, P0 + CORE, lo, 1, 0),
                                  (head_x - hi, head_z1 - hi, hi, 1, 1)):
            reach = 0.414 * r
            pts.append((sx * (cx + ux * reach), yb, cz, 0.5 - shrink))
            if uz:
                pts.append((sx * cx, yb, cz + uz * reach, 0.5 - shrink))
                pts.append((sx * (cx + ux * reach * 0.707), yb, cz + uz * reach * 0.707, 0.5 - shrink))
    return pts


head_outer = blob(head_spheres(0))


ROOF_WALL = 6.0   # straight walls above the CoreS3 before the chamber roof slopes in


def roof(erode=0.0):
    """The chamber's ceiling: 45-degree slopes from the left, right and front
    walls (the back is open to the cap), so the head prints upright with no
    supports inside. Leaves ~11 mm above Ports B and C, ~23 mm at the middle."""
    z0 = P0 + CORE + ROOF_WALL
    half = CORE / 2 - erode + 0.6   # a little wider than the pocket, so no faces just touch
    depth = SPLIT_Y + 40
    # Across (x-z): a gable, extruded front to back.
    gable = CrossSection([[(-half, P0 - 10), (half, P0 - 10), (half, z0), (0, z0 + half), (-half, z0)]])
    sides = Manifold.extrude(gable, depth).rotate((90, 0, 0)).translate((0, depth - 10, 0))
    # Front to back (y-z): rising at 45 degrees from the front wall.
    slope = CrossSection([[(erode - 0.6, P0 - 10), (depth, P0 - 10), (depth, z0 + depth + 0.6), (erode - 0.6, z0)]])
    front = Manifold.extrude(slope, 200).rotate((90, 0, 90)).translate((-100, 0, 0))
    return sides ^ front


def head_cavity(erode=0.0):
    """Pocket for the CoreS3 (open to the back) plus the chamber above it."""
    pocket = rbox(-CORE / 2 + erode, CORE / 2 - erode, 0 + erode, SPLIT_Y + 20, P0 + erode, P0 + CORE - erode, 3)
    dome = blob(head_spheres(SHELL + erode)) ^ above(P0 + CORE - 1) ^ roof(erode)
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
# The rim (on the head) and groove (in the cap) run around the CoreS3 pocket
# only, a U on the left, bottom and right. Above that, in the dome, the
# chamber narrows toward the back, so a rim there would be thin and step in
# and out; the cap and head just meet face to face along the dome.
slab = lambda y0, y1: box(-500, 500, y0, y1, -500, 500)
RIM_TOP = P0 + CORE - 0.5
below = lambda z: box(-500, 500, -500, 500, -500, z)
rim = ((blob(head_spheres(1.2)) ^ slab(SPLIT_Y, SPLIT_Y + LIP - 1.0))
       # lead-in: the last millimetre is 0.5 mm slimmer on the outside, so it finds the groove
       + (blob(head_spheres(1.7)) ^ slab(SPLIT_Y + LIP - 1.0, SPLIT_Y + LIP))) - cavity
rim = rim ^ below(RIM_TOP)

# Ear tabs go through slots in the sides of the dome.
EAR_Y, EAR_TOP = 24.0, head_z1 - 16


def ear_tab(side, grow=0.0):
    return box(min(side * (head_x - 12), side * (head_x + 2)) - grow, max(side * (head_x - 12), side * (head_x + 2)) + grow,
               EAR_Y - 2.0 - grow, EAR_Y + 0.95 + grow, EAR_TOP - 7 - grow, EAR_TOP + 2 + grow)


head = ((head_outer - behind_split) + rim) - cavity - screen_window - side_windows - bottom_windows - neck_socket
head = head - ear_tab(-1, CLEAR) - ear_tab(1, CLEAR)

groove = ((blob(head_spheres(1.2 - RIM_CLEAR)) ^ slab(SPLIT_Y - 1, SPLIT_Y + LIP + RIM_CLEAR))
          - head_cavity(RIM_CLEAR)) ^ below(RIM_TOP + RIM_CLEAR)

# Two ribs reach forward from the cap and press the CoreS3 against the front lip
# (it slid back and forth on the test print). They're at the left and right
# edges, clear of the cables down the middle and of Ports B/C at the top.
# PANEL_D is the depth from the screen to the DIN base's flat back panel,
# where they touch (measured 28 mm; the raised edges stick out further).
PANEL_D = 28.0
RIB_PLAY = 0.5                  # how far the ribs stop short of the device (0.2 needed trimming)
ribs = Manifold()
for sx in (-1, 1):
    ribs += rbox(min(sx * 20.5, sx * 25.5), max(sx * 20.5, sx * 25.5), PANEL_D + RIB_PLAY, SPLIT_Y + 2,
                 P0 + 14, P0 + 44, 1.2)

# Two screws hold the cap on (the friction fit alone didn't): M3 screws through
# posts in the cap into the two brass nuts in the DIN base's back panel, which
# also clamps the CoreS3 against the front lip. The posts stop RIB_PLAY short of
# the panel like the ribs, so the cap always seats on the head first.
SCREW_X = 11.75                 # the nuts are this far left and right of centre (measured 23-24 mm apart)
SLOT = 0.6                      # the holes are slots, +/- this much across, to forgive that measurement
SCREW_Z = P0 + CORE / 2         # and at the panel's mid-height
SCREW_L = 16.0                  # M3 x 16 socket-head screws
SCREW_BITE = 4.0                # how far the screw goes into the brass nut
POST_R = 5.0
posts, screw_holes = Manifold(), Manifold()
head_at = PANEL_D - SCREW_BITE + SCREW_L      # where the screw head sits (y)
for sx in (-1, 1):
    x = sx * SCREW_X
    along_y = lambda m: m.rotate((-90, 0, 0)).translate((x, 0, SCREW_Z))
    posts += along_y(Manifold.cylinder(SPLIT_Y + 2 - PANEL_D - RIB_PLAY, POST_R, POST_R, SEG)
                     .translate((0, 0, PANEL_D + RIB_PLAY)))
    slot = lambda m: Manifold.batch_hull([along_y(m).translate((dx, 0, 0)) for dx in (-SLOT, SLOT)])
    screw_holes += slot(Manifold.cylinder(60, 1.75, 1.75, SEG).translate((0, 0, PANEL_D - 10)))   # 3.5 mm
    # Counterbore for the head (5.5 mm, so 6.4 mm wide), opening at the back of
    # the cap; it narrows at 45 degrees so it prints without a bridge.
    screw_holes += slot(Manifold.cylinder(30, 3.2, 3.2, SEG).translate((0, 0, head_at)))
    screw_holes += slot(Manifold.cylinder(1.45, 1.75, 3.2, SEG).translate((0, 0, head_at - 1.45)))

# ...but those screws only tie the cap to the CoreS3; nothing tied the cap to
# the head, so a tap on the screen pushed both off the back (and no friction
# fit held on the test prints). Two more M3 x 16 screws go straight through the
# cap into the head's side walls beside the pocket, where the wall behind the
# rim is 8 mm of solid PLA. The head isn't reprinted: you drill a 2.5 mm pilot
# into the head through the cap's 3.4 mm clearance hole, then drive the screw
# in. It cuts its thread in the head only, so it pulls the cap tight. (A 2.5 mm
# hole in the cap printed too small for the screw, and threads in the cap
# would lock in any gap at the seam.) A small boss squares off the cap's
# rounded back corner there so the screw head has a flat seat.
FIX_X, FIX_Z = 30.5, P0 + CORE / 2    # hole centres: middle of the side wall, mid-height
DRILL_D = 2.5                          # pilot for M3 in the head
FIX_HOLE = 3.4                         # clearance in the cap
FIX_L = 16.0                           # M3 x 16
DRILL_DEPTH = FIX_L + 1.0              # from the cap's back face
fix_bosses, fix_holes = Manifold(), Manifold()
for sx in (-1, 1):
    along_y = lambda m: m.rotate((-90, 0, 0)).translate((sx * FIX_X, 0, FIX_Z))
    fix_bosses += along_y(Manifold.cylinder(head_y1 - SPLIT_Y, 3.3, 3.3, SEG).translate((0, 0, SPLIT_Y)))
    fix_holes += along_y(Manifold.cylinder(20, FIX_HOLE / 2, FIX_HOLE / 2, SEG).translate((0, 0, SPLIT_Y - 1)))
# The side ribs are left off: the screwed posts already hold the CoreS3, and
# the ribs' ends landed on the DIN base's raised edges, holding the cap ~1 mm
# off the head on the test print.
back_cap = (((head_outer ^ behind_split) + fix_bosses) - groove) + posts - screw_holes - fix_holes


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
        "buddy_head": (head, "as_is", "head"),
        "buddy_back_cap": (back_cap, "flat_back", "cap"),
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
