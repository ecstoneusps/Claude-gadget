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

head = rbox(-head_x, head_x, head_y0, head_y1, head_z0, head_z1, 12)
cavity = rbox(-CORE_W / 2, CORE_W / 2, 0, CORE_D + CHANNEL, head_z0 + BOTTOM,
              head_z0 + BOTTOM + CORE_H + CHAMBER, 3)
screen = Manifold.cube((50.0, 10, 51.2)).translate((-25.0, head_y0 - 5, head_z0 + BOTTOM + 1.8))
head = head - cavity - screen

# --- Floppy ears: flat leaves from the upper back of the head, hanging down --
def ear(side):
    leaf = blob([(0, 0, 0, 8), (side * 6, 0, -20, 11), (side * 7, 0, -34, 9)]).scale((1, 0.32, 1))
    # tilt the bottom of the ear outward a little
    leaf = leaf.rotate((0, -side * 12, 0))
    return leaf.translate((side * (head_x - 3), 24, head_z1 - 8))

ears = ear(-1) + ear(1)

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

puppy = head + ears + neck + body + chest + tail
for p in paws + hinds:
    puppy = puppy + p
# flat bottom so it sits on the desk
puppy = puppy - Manifold.cube((300, 300, 50)).translate((-150, -150, -50))

mesh = puppy.to_mesh()
tm = trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts))
out = sys.argv[1] if len(sys.argv) > 1 else "puppy_concept.stl"
tm.export(out)
lo, hi = tm.bounds
print(f"concept: {hi[0]-lo[0]:.0f} x {hi[1]-lo[1]:.0f} x {hi[2]-lo[2]:.0f} mm, {len(tm.faces)} faces -> {out}")
