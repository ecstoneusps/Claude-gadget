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
GAP_BEHIND = 5.0            # space behind the device for cables (the DIN base's back panel sits a further 2.5-5.5 mm in)
CHAMBER = 4.0               # a little room above the CoreS3 (turned around, nothing plugs in at the top)

# --- Head ---------------------------------------------------------------------------------
FRONT, BOTTOM, TOP, BACK = 2.4, 8.5, 6.0, 10.0   # thick floor and back: room to sink the screw heads
SIDE = 6.0                  # side walls
SHELL = 3.0                 # dome wall
HEAD_Z0 = 66.0              # underside of the head above the desk (just clear of the body)
head_x = CORE / 2 + SIDE
head_y0, head_y1 = -FRONT, CORE_D + GAP_BEHIND + BACK
P0 = HEAD_Z0 + BOTTOM                       # floor of the pocket
head_z1 = P0 + CORE + CHAMBER + TOP
SPLIT_Y = CORE_D + GAP_BEHIND               # head / back cap joint
LIP = 3.0                                   # how far the head's rim reaches into the cap

# --- Neck (part of the body; the head sits on it) --------------------------------------
NECK_W, NECK_D, NECK_R = 44.0, 28.0, 12.0   # a rounded oval
NECK_CY = 21.0              # wholly in front of the back cap (at 28 the cap covered the socket's back)
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
TOP_R = 16.0      # rounding of the head's top corners, seen from the front


def head_spheres(shrink):
    """A rounded box around the CoreS3: edges rounded to 8 mm, the top corners
    to TOP_R as seen from the front. `shrink` offsets inward.

    The head prints standing on its bottom, so the bottom edges are a 45-degree
    chamfer (small spheres on the bed, inset so the slope up to the rounded
    corners is 45 degrees) instead of a rounding that would overhang the bed.
    """
    lo = 8.0
    foot = 0.6 * lo + 0.5   # bottom-edge inset that gives a 45-degree slope up to the lo spheres
    # Outline seen from the front (x, z) of the edge spheres' centres, with the
    # outward direction at each: the bottom corner, then the top corner's arc.
    outline = [(head_x - lo, HEAD_Z0 + lo, 1.0, 0.0), (head_x - lo, HEAD_Z0 + lo, 0.707, -0.707),
               (head_x - lo, HEAD_Z0 + lo, 0.0, -1.0)]
    for deg in range(0, 91, 15):
        a = np.radians(deg)
        outline.append((head_x - TOP_R + (TOP_R - lo) * np.cos(a), head_z1 - TOP_R + (TOP_R - lo) * np.sin(a),
                        np.cos(a), np.sin(a)))
    pts = []
    for sx in (-1, 1):
        for y, sy in ((head_y0, 1), (head_y1, -1)):
            pts.append((sx * (head_x - foot), y + sy * foot, HEAD_Z0 + 0.5, 0.5 - shrink))
            for cx, cz, _, _ in outline:
                pts.append((sx * cx, y + sy * lo, cz, lo - shrink))
        # The back cap prints back face down, so the back edges are a 45-degree
        # chamfer too: small spheres on the back face, 0.414 r beyond each
        # rounded edge's centre (where a 45-degree line meets the rounding).
        yb = head_y1 - 0.5
        for cx, cz, ux, uz in outline:
            pts.append((sx * (cx + ux * 0.414 * lo), yb, cz + uz * 0.414 * lo, 0.5 - shrink))
    return pts


head_outer = blob(head_spheres(0))


ROOF_WALL = 1.0   # straight walls above the CoreS3 before the chamber roof slopes in
ROOF_TOP = 6.0    # the chamber's flat top, this far above the CoreS3 (a short hidden bridge)
ROOF_FRONT = 4.0  # the front slope starts this far behind the front wall


def roof(erode=0.0):
    """The chamber's ceiling: 45-degree slopes from the left, right, front and
    back, and a short flat bridge on top, all inside the dome's shell, so the
    head prints upright with no curved overhangs inside (the dome's own inner
    surface would droop)."""
    z0 = P0 + CORE + ROOF_WALL
    zt = P0 + CORE + ROOF_TOP - erode
    half = CORE / 2 - erode + 0.6   # a little wider than the pocket, so no faces just touch
    depth = SPLIT_Y + 40
    # Across (x-z): a gable, extruded front to back.
    gable = CrossSection([[(-half, P0 - 10), (half, P0 - 10), (half, z0), (0, z0 + half), (-half, z0)]])
    sides = Manifold.extrude(gable, depth).rotate((90, 0, 0)).translate((0, depth - 10, 0))
    # Front to back (y-z): rising at 45 degrees from the front and the back.
    yf, yb = ROOF_FRONT + erode, SPLIT_Y + 0.6
    slope = CrossSection([[(yf - 0.6, P0 - 10), (yb + 10, P0 - 10), (yb + 10, z0 - 10), (yb, z0),
                           (yb - 15, z0 + 15), (yf - 0.6 + 15, z0 + 15), (yf - 0.6, z0)]])
    ends = Manifold.extrude(slope, 200).rotate((90, 0, 90)).translate((-100, 0, 0))
    return sides ^ ends ^ box(-100, 100, -100, 100, -100, zt)


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
# (just the middle: the floor beside it, over the neck's side walls, holds the neck screws)
bottom_windows = box(-14, 14, 11, 31, HEAD_Z0 - 1, P0 + 1)
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
EAR_Y, EAR_TOP = 24.0, head_z1 - TOP_R + 1   # tabs in the straight side wall, just below the top corner


def ear_tab(side, grow=0.0):
    return box(min(side * (head_x - 4), side * (head_x + 2)) - grow, max(side * (head_x - 4), side * (head_x + 2)) + grow,
               EAR_Y - 2.0 - grow, EAR_Y + 0.95 + grow, EAR_TOP - 7 - grow, EAR_TOP + 2 + grow)


head = ((head_outer - behind_split) + rim) - cavity - screen_window - side_windows - bottom_windows - neck_socket
head = head - ear_tab(-1, CLEAR) - ear_tab(1, CLEAR)

groove = ((blob(head_spheres(1.2 - RIM_CLEAR)) ^ slab(SPLIT_Y - 1, SPLIT_Y + LIP + RIM_CLEAR))
          - head_cavity(RIM_CLEAR)) ^ below(RIM_TOP + RIM_CLEAR)

# PANEL_D is the depth from the screen to the DIN base's flat back panel
# (measured 28 mm; its raised edges stick out further).
PANEL_D = 28.0
RIB_PLAY = 1.0                  # how far the posts stop short of the back panel (0.5 strained the cap on the test print)

# Every joint is held by M3 screws whose heads sit flush or below the surface:
#   cap -> CoreS3: 2 x M3 x 16 socket head, through posts into the DIN base's brass nuts
#   cap -> head:   2 x M3 x 16 socket head, into the head's floor at the bottom corners (printed pilot holes)
#   head -> neck:  2 x M3 x 12 socket head, up from under the body through the neck into the head's floor

# Cap -> CoreS3: the screws clamp the CoreS3 against the front lip so a tap on
# the screen can't push it back.
SCREW_X = 11.75                 # the nuts are this far left and right of centre (measured 23-24 mm apart)
SLOT = 0.6                      # the holes are slots, +/- this much across, to forgive that measurement
SCREW_Z = P0 + CORE / 2         # and at the panel's mid-height
SCREW_L = 16.0                  # M3 x 16 socket-head screws
SCREW_BITE = 4.0                # how far the screw goes into the brass nut
POST_R = 5.0
posts, screw_holes = Manifold(), Manifold()
head_at = PANEL_D - SCREW_BITE + SCREW_L      # where the screw head sits (y), deep inside the post
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

# Cap -> head: those screws only tie the cap to the CoreS3, so two more go
# through the cap's bottom corners into the head's floor, in the 5 mm of solid
# PLA between the neck socket and the CoreS3 (the 6 mm side walls are too thin
# for them). The head has printed 2.5 mm pilot holes, the cap 3.4 mm clearance
# holes, so the screws pull the cap tight. The heads sit in counterbores just
# above the groove, below the cap's back surface.
FIX_X, FIX_Z = 24.5, P0 - 3.0         # beside the neck socket, mid-floor
FIX_L = 16.0                           # M3 x 16
FIX_SEAT = SPLIT_Y + LIP + RIM_CLEAR + 1.2    # the screw head sits here, flush with the back
fix_holes, fix_pilots = Manifold(), Manifold()
for sx in (-1, 1):
    along_y = lambda m: m.rotate((-90, 0, 0)).translate((sx * FIX_X, 0, FIX_Z))
    fix_holes += along_y(Manifold.cylinder(20, 1.7, 1.7, SEG).translate((0, 0, SPLIT_Y - 1)))
    fix_holes += along_y(Manifold.cylinder(20, 3.2, 3.2, SEG).translate((0, 0, FIX_SEAT)))
    fix_holes += along_y(Manifold.cylinder(1.5, 1.7, 3.2, SEG).translate((0, 0, FIX_SEAT - 1.5)))   # 45-degree step
    fix_pilots += along_y(Manifold.cylinder(FIX_L + 1, 1.25, 1.25, SEG).translate((0, 0, FIX_SEAT - FIX_L - 1)))
back_cap = ((head_outer ^ behind_split) - groove) + posts - screw_holes - fix_holes

# Head -> neck: socket-head screws driven UP from underneath the body (a long
# 2.5 mm hex key through the open bottom), through bosses inside the neck,
# into blind pilot holes in the head's floor. That way the head goes on last:
# the CoreS3, with the radar cable plugged into its downward-facing Port C,
# is already in it. The pocket floor has no holes.
NECK_SCREW_X = 17.5                    # inside the neck wall, so the key tunnel stays 1.2 mm inside its outside
NECK_SCREW_L = 12.0                    # M3 x 12 socket head
NECK_BITE = P0 - 1.2 - (HEAD_Z0 + SOCKET)     # pilot depth in the head's floor (1.2 mm of floor left above)
NECK_SEAT = HEAD_Z0 + SOCKET + NECK_BITE - NECK_SCREW_L   # the screw head bears on this (facing down, in the boss)
neck_screw_holes, neck_pilots = Manifold(), Manifold()
for sx in (-1, 1):
    at = lambda m: m.translate((sx * NECK_SCREW_X, NECK_CY, 0))
    neck_screw_holes += at(Manifold.cylinder(NECK_BITE + SOCKET + 1, 1.25, 1.25, SEG).translate((0, 0, HEAD_Z0 - 1)))   # pilot in the head
    neck_pilots += at(Manifold.cylinder(40, 1.7, 1.7, SEG).translate((0, 0, NECK_SEAT - 1)))                    # clearance in the neck
    neck_pilots += at(Manifold.cylinder(NECK_SEAT - 20, 3.3, 3.3, SEG).translate((0, 0, 20)))                     # room for the head and the key
    neck_pilots += at(Manifold.cylinder(22, 2.6, 2.6, SEG).translate((0, 0, -1)))                               # and for the key through the base
head = head - fix_pilots - neck_screw_holes
# A 0.4 mm skin across the opening at the socket's ceiling, so the opening's
# edges above it have something to print on (without it they sagged into
# strings). Cut it out with a hobby knife after printing.
SKIN = 0.4
head = head + ((bottom_windows ^ box(-100, 100, -100, 100, HEAD_Z0 + SOCKET, HEAD_Z0 + SOCKET + SKIN)) - neck_screw_holes)


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
SKIRT = 40.0      # undersides slope at most this many degrees from vertical (45 after the x stretch)


def skirted(points, xscale=1.0):
    """Like blob(), but each sphere's underside continues as a cone (SKIRT
    degrees from vertical) down to the bed, so nothing overhangs more than
    that. The cones end in a point if they don't reach the bed."""
    t = np.radians(SKIRT)
    parts = []
    for x, y, z, r in points:
        if r <= 0:
            continue
        parts.append(sphere(x, y, z, r))
        zt = z - r * np.sin(t)                     # where the cone meets the sphere
        if zt <= 0:
            continue
        rt = r * np.cos(t)
        z0 = max(0.0, zt - rt / np.tan(t))         # the bed, or the cone's tip
        if z0 < 6.0:
            z0 = 0.0   # a tip just above the bed would start in mid-air: take it down to the bed
        r0 = max(0.01, rt - (zt - z0) * np.tan(t))
        parts.append(Manifold.cylinder(zt - z0, r0, rt, SEG).translate((x, y, z0)))
    return Manifold.batch_hull(parts).scale((xscale, 1, 1))


body_parts = [(0, 22, 46, 20), (0, 30, 22, 24), (0, 20, 18, 22), (0, 36, 18, 20)]
chest_parts = [(0, 6, 34, 13), (0, 4, 22, 13)]
torso = skirted(body_parts, 1.15)
chest = skirted(chest_parts)
inside = skirted([(x, y, z, r - WALL / 1.15) for x, y, z, r in body_parts], 1.15) + \
    skirted([(x, y, z, r - WALL) for x, y, z, r in chest_parts])
# The hollow's ceiling rises at 45 degrees to the neck, so the body prints
# upright with no supports inside it.
inside = inside ^ Manifold.cylinder(80, 80, 0, SEG).translate((0, NECK_CY, -19))   # peak at z 61, into the neck; clears the radar board

paws = Manifold()
for sx in (-1, 1):
    paws += skirted([(sx * 13, 4, 26, 8), (sx * 13, -2, 7, 9)]) + skirted([(sx * 13, -5, 5, 9.5)])
hinds = Manifold()
for sx in (-1, 1):
    hinds += skirted([(sx * 26, 30, 16, 13), (sx * 27, 20, 7, 9.5), (sx * 26, 42, 10, 9)])
# The tail lies on the floor and curls around the right hip, as a sitting
# puppy's does (sticking out into the air it printed with sagging lines).
tail = skirted([(0, 54, 5, 5), (10, 58, 4.5, 4.5)]) + skirted([(10, 58, 4.5, 4.5), (20, 54, 4, 4)]) + \
    skirted([(20, 54, 4, 4), (26, 47, 3.5, 3.5)])

neck_top = HEAD_Z0 + SOCKET
neck = rrect_prism(0, NECK_CY, NECK_W, NECK_D, NECK_R, 38, neck_top)  # starts inside the body
neck_hole = rrect_prism(0, NECK_CY, NECK_W - 2 * NECK_WALL, NECK_D - 2 * NECK_WALL, 3, 30, neck_top + 1)
# Bosses inside the neck's side walls for the head screws, with 45-degree
# undersides so they print without support.
neck_bosses = Manifold()
for sx in (-1, 1):
    boss = Manifold.cylinder(14, 4.0, 4.0, SEG).translate((sx * NECK_SCREW_X, NECK_CY, neck_top - 14))
    boss += Manifold.cylinder(4.0, 0.01, 4.0, SEG).translate((sx * NECK_SCREW_X, NECK_CY, neck_top - 18))
    neck_bosses += boss
neck_bosses = neck_bosses ^ rrect_prism(0, NECK_CY, NECK_W, NECK_D, NECK_R, 0, neck_top)

# Radar holder: the LD2410C board (22 x 16 mm) stands upright in the middle of
# the body, antenna side forward, in two grooved cheeks that grow out of the
# side walls. The chest is too narrow inside to hold a 22 mm board near its
# front, and from the open bottom there's no straight way up, so the board
# drops in from the top, through the neck, before the head goes on, and rests
# on the grooves' closed bottom ends. (The radar sees through the air and the
# 2.4 mm wall in front of it.) Pins up, so the cable goes straight up the neck.
RADAR_H = 16.0
RADAR_Z0 = 20.0                       # bottom edge of the board
RADAR_Y = 11.6                        # front face of the board (just behind the neck's front wall, so it drops straight down)
GROOVE = 1.5                          # how far the board's edges sit in the grooves
rz1 = RADAR_Z0 + RADAR_H
gx = RADAR_W / 2 + 0.3                # groove bottom (0.3 mm side play)
cy0, cy1 = RADAR_Y - 2.5, RADAR_Y + RADAR_T + 2.5
cheeks = Manifold()
for sx in (-1, 1):
    x_in = RADAR_W / 2 - GROOVE
    lo_x, hi_x = (x_in, 45.0) if sx > 0 else (-45.0, -x_in)
    block = box(lo_x, hi_x, cy0, cy1, RADAR_Z0 - 2, rz1)
    # 45-degree underside, sloping down and out to the side wall
    edge = box(lo_x, hi_x, cy0, cy1, RADAR_Z0 - 2, RADAR_Z0 - 1.99)
    far = (45.0 - x_in)
    outer_x = sx * 45.0
    wedge = Manifold.batch_hull([edge, box(min(outer_x, outer_x + sx * 0.01), max(outer_x, outer_x + sx * 0.01),
                                           cy0, cy1, RADAR_Z0 - 2 - far, RADAR_Z0 - 1.99)])
    cheeks += block + wedge
board_slot = box(-gx, gx, RADAR_Y - 0.15, RADAR_Y + RADAR_T + 0.15, RADAR_Z0, neck_top + 5)   # open at the top
window = box(-RADAR_W / 2 + GROOVE, RADAR_W / 2 - GROOVE, -60, 60, -10, 100)
outer_body = torso + chest
holder = (cheeks ^ outer_body) - board_slot - window

pads = Manifold()
for sx in (-1, 1):
    pads += Manifold.sphere(5.2, SEG).scale((1, 0.35, 0.8)).translate((sx * 13, -14.3, 6))
    for dx, dz in ((-4.2, -0.6), (0, 0.8), (4.2, -0.6)):
        pads += Manifold.sphere(2.1, SEG).scale((1, 0.4, 1)).translate((sx * 13 + dx, -13.4, 12.2 + dz))

floor_cut = box(-500, 500, -500, 500, -500, 0)
body = (torso + chest + paws + hinds + tail + neck) - inside - neck_hole
body = (body + holder + neck_bosses) - board_slot - floor_cut
body = body - neck_pilots.translate((0, 0, 0))
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
