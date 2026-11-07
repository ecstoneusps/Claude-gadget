"""Concept model of Buddy's chibi puppy body (proportions only, not yet printable).

Coordinates: x to Buddy's left/right, y front (-) to back (+), z up. Millimetres.
Remix of "Puppy Stand - M5Stack Core S3" by John White (Printables, CC BY 4.0).
"""
import sys
import numpy as np
import trimesh
from manifold3d import Manifold

SEG = 48  # sphere/cylinder smoothness


def rbox(x0, x1, y0, y1, z0, z1, r):
    """Box with all edges rounded to radius r (hull of 8 spheres)."""
    pts = []
    for x in (x0 + r, x1 - r):
        for y in (y0 + r, y1 - r):
            for z in (z0 + r, z1 - r):
                pts.append(Manifold.sphere(r, SEG).translate((x, y, z)))
    return Manifold.batch_hull(pts)


def blob(points_radii):
    """Smooth rounded shape: hull of spheres at (x, y, z, r)."""
    return Manifold.batch_hull([Manifold.sphere(r, SEG).translate((x, y, z)) for x, y, z, r in points_radii])


# --- CoreS3 + DIN base inside the head (screen faces -y) --------------------
CORE_W, CORE_H, CORE_D = 54.8, 54.8, 34.0  # with 0.4 mm clearance; 33.5 mm deep measured
CHAMBER = 18.0      # space above the ports for Grove plugs to bend
CHANNEL = 8.0       # cable channel behind the CoreS3, down to the neck
WALL = 3.0
HEAD_Z0 = 62.0      # height of the head's underside above the desk

head_x = CORE_W / 2 + 7             # thicker sides: rounder look, room for the rounding
FRONT, TOP, BOTTOM, BACK = 3.0, 6.0, 6.0, 6.0
head_y0, head_y1 = -FRONT, CORE_D + CHANNEL + BACK
head_z0 = HEAD_Z0
head_z1 = HEAD_Z0 + BOTTOM + CORE_H + CHAMBER + TOP

# Head = hull of spheres: tighter rounding around the screen (lower part),
# a big soft dome on top. The inside is the same hull with every sphere
# shrunk by the wall thickness, so the walls stay even all round.
def head_spheres(shrink):
    lo_r, hi_r = 10.0, 24.0
    pts = []
    for x in (-1, 1):
        for y, yr in ((head_y0, 1), (head_y1, -1)):
            # lower corners (around the screen and the bottom)
            pts.append((x * (head_x - lo_r), y + yr * lo_r, head_z0 + lo_r, lo_r - shrink))
            # where the dome starts, level with the top of the screen
            pts.append((x * (head_x - lo_r), y + yr * lo_r, head_z0 + BOTTOM + CORE_H, lo_r - shrink))
            # the dome
            pts.append((x * (head_x - hi_r), y + yr * hi_r, head_z1 - hi_r, hi_r - shrink))
    return [p for p in pts if p[3] > 0]

WALL_SHELL = 3.0
head = blob(head_spheres(0))
dome_inside = blob(head_spheres(WALL_SHELL)) - Manifold.cube((300, 300, 300)).translate(
    (-150, -150, head_z0 + BOTTOM + CORE_H - 300))   # only above the CoreS3
pocket = rbox(-CORE_W / 2, CORE_W / 2, 0, CORE_D + CHANNEL, head_z0 + BOTTOM, head_z0 + BOTTOM + CORE_H + 1, 4)
screen = Manifold.cube((50.0, 10, 51.2)).translate((-25.0, head_y0 - 5, head_z0 + BOTTOM + 1.8))
head = head - pocket - dome_inside - screen

# --- Floppy ears: flat leaves from the upper back of the head, hanging down --
def ear(side):
    """A flat leaf hanging from the upper back of the head, and its pink
    inner-ear inlay: a smaller leaf shape set 1.2 mm into the front face, so
    the ear stays smooth and the slicer can give the inlay its own colour."""
    shape = [(0, 0, 0, 8), (side * 6, 0, -22, 11), (side * 8, 0, -40, 9.5)]
    leaf = blob(shape).scale((1, 0.32, 1))
    # The front 1.2 mm of the ear: the ear minus itself pushed back.
    front_layer = leaf - leaf.translate((0, 1.2, 0))
    # A smaller leaf (inset ~3.5 mm from the edge), as a deep prism.
    inner = blob([(x, y, z, r - 3.5) for x, y, z, r in shape]).scale((1, 3, 1))
    inlay = front_layer ^ inner
    place = lambda m: m.rotate((0, -side * 12, 0)).translate((side * (head_x - 4), 24, head_z1 - 16))
    return place(leaf - inlay), place(inlay)


(ear_l, inner_l), (ear_r, inner_r) = ear(-1), ear(1)
ears = ear_l + ear_r
inner_ears = inner_l + inner_r

# --- Neck: short hollow column (cables run inside) ------------------------------
neck = Manifold.cylinder(14, 13, 15, SEG).translate((0, 18, HEAD_Z0 - 12))

# --- Sitting chibi body ----------------------------------------------------------
body = blob([(0, 22, 46, 20), (0, 30, 22, 24), (0, 20, 18, 22), (0, 36, 18, 20)]).scale((1.15, 1, 1))
# chest bump (where the radar sits, facing forward)
chest = blob([(0, 6, 34, 13), (0, 4, 22, 13)])
# front paws: chunky rounded columns with round feet
paws = []
for side in (-1, 1):
    paws.append(blob([(side * 13, 4, 26, 8), (side * 13, -2, 7, 9)]))
    paws.append(blob([(side * 13, -5, 5, 9.5)]))  # paw pad, a bit forward
# hind legs tucked at the sides
hinds = []
for side in (-1, 1):
    hinds.append(blob([(side * 26, 30, 16, 13), (side * 27, 20, 7, 9.5), (side * 26, 42, 10, 9)]))
# tail: a little curl at the back
tail = blob([(0, 58, 22, 5), (0, 64, 32, 4.5), (0, 62, 40, 4)])

puppy = head + ears + inner_ears + neck + body + chest + tail
for p in paws + hinds:
    puppy = puppy + p
# flat bottom so it sits on the desk
puppy = puppy - Manifold.cube((300, 300, 50)).translate((-150, -150, -50))

# Paw pads: a separate piece so it can be printed in its own colour.
pads = None
for side in (-1, 1):
    big = Manifold.sphere(5.2, SEG).scale((1, 0.35, 0.8)).translate((side * 13, -14.3, 6))
    toes = [Manifold.sphere(2.1, SEG).scale((1, 0.4, 1)).translate((side * 13 + dx, -13.4, 12.2 + dz))
            for dx, dz in ((-4.2, -0.6), (0, 0.8), (4.2, -0.6))]
    piece = big + toes[0] + toes[1] + toes[2]
    pads = piece if pads is None else pads + piece


def save(m, out):
    mesh = m.to_mesh()
    tm = trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts))
    tm.export(out)
    lo, hi = tm.bounds
    print(f"{out}: {hi[0]-lo[0]:.0f} x {hi[1]-lo[1]:.0f} x {hi[2]-lo[2]:.0f} mm, {len(tm.faces)} faces")


pink = pads + inner_ears  # paw pads and inner ears: one colour
out = sys.argv[1] if len(sys.argv) > 1 else "puppy_concept.stl"
save(puppy - pink, out)
save(pink, out.replace(".stl", "_pink.stl"))
