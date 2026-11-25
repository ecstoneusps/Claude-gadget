"""Badware's body with fixed joints, and a weighted base.

The original figure (GigaBagel, Printables 1410750, CC BY 4.0) snaps
together with ball joints. Under the CoreS3 head that's too floppy: he tips
over. Here every ball becomes a diamond peg (a square turned 45 degrees, so it
prints without supports lying or standing) and every socket in the torso is
filled in, with a matching diamond hole where it was: each part goes in at
one of four quarter-turns and is glued, a fixed pose. All parts come out
already at 135 %, the size that fits the CoreS3 head. The base takes his
feet in pockets and has a pocket underneath for weight (coins, steel nuts).

    python3 static_body.py stl/
"""
import os
import sys

import numpy as np
import trimesh
from manifold3d import CrossSection, Manifold, Mesh

HERE = os.path.dirname(os.path.abspath(__file__))
SCALE = 1.35
SEG = 48

# In the original's own units (scaled at the end):
BALL_R = 4.72        # every ball
SOCKET_R = 4.6       # every socket (they snap)
PEG = 6.0            # the diamond's side (8.1 mm printed)
PEG_CLEAR = 0.15     # each side of the hole (0.2 mm printed): room for glue
PEG_TIP = 4.0        # the peg reaches this far past where the ball's centre was
PEG_BACK = 4.8       # and this far back into the part it's on

# The joints, found in the original file (fitted spheres): ball centres on
# the parts, socket centres in the torso, and each joint's axis (the way out
# of the torso).
TORSO_SOCKETS = {
    "neck": ((1.0, 9.5, 8.0), (0, 1, 0)),
    "hip_r": ((10.0, -26.5, 8.0), (0, -1, 0)),
    "hip_l": ((-10.0, -26.5, 8.0), (0, -1, 0)),
    "shoulder_r": ((16.5, -1.0, 8.0), (1, 0, 0)),
    "shoulder_l": ((-17.5, -1.0, 8.0), (-1, 0, 0)),
}
# Part index (by size in the file) -> (ball centre, direction from the ball into its part)
LIMBS = {
    "arm_a": (2, (42.71, 3.17, 8.82), (1, 0, 0)),
    "arm_b": (3, (-44.99, 0.76, 8.03), (-1, 0, 0)),
    "leg_a": (4, (20.25, -70.27, 44.94), (0, 0, -1)),
    "leg_b": (5, (-24.77, -70.28, 44.95), (0, 0, -1)),
}


def from_trimesh(tm):
    return Manifold(Mesh(vert_properties=np.asarray(tm.vertices, np.float32), tri_verts=np.asarray(tm.faces, np.uint32)))


def to_trimesh(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(vertices=mesh.vert_properties[:, :3], faces=mesh.tri_verts)


def diamond_prism(centre, direction, t0, t1, side):
    """A prism with a diamond cross-section (a square of `side` turned 45
    degrees), along an axis-aligned `direction` from centre + t0*d to
    centre + t1*d."""
    d = np.array(direction, float)
    axis = int(np.argmax(np.abs(d)))
    sign = np.sign(d[axis])
    a0, a1 = sorted((centre[axis] + sign * t0, centre[axis] + sign * t1))
    h = side / np.sqrt(2)  # half the diagonal
    cs = CrossSection([[(h, 0), (0, h), (-h, 0), (0, -h)]])
    m = Manifold.extrude(cs, a1 - a0)            # along +z, from 0
    if axis == 0:
        m = m.rotate((0, 90, 0))                 # z -> +x
    elif axis == 1:
        m = m.rotate((-90, 0, 0))                # z -> +y
    pos = list(centre)
    pos[axis] = a0
    return m.translate(tuple(pos))


def static_torso(torso):
    m = from_trimesh(torso)
    for name, (c, out) in TORSO_SOCKETS.items():
        m = m + Manifold.sphere(SOCKET_R + 0.05, SEG).translate(c)                 # fill the socket
        m = m - diamond_prism(np.array(c), out, -(PEG_TIP + 0.3), 30, PEG + 2 * PEG_CLEAR)  # the peg's hole
    return m


def static_limb(part, ball, into_part):
    m = from_trimesh(part)
    c = np.array(ball)
    m = m - Manifold.sphere(BALL_R + 0.35, SEG).translate(tuple(c))               # off with the ball
    out = -np.array(into_part, float)                                             # toward the torso
    return m + diamond_prism(c, out, -PEG_BACK, PEG_TIP, PEG)


# --- Base: a slab with pockets for his feet, chamfered on top, and a pocket
# underneath for weight. Prints upside down (top on the bed, weight pocket up).
BASE_W, BASE_D, BASE_H = 120.0, 90.0, 12.0
BASE_CHAMFER = 3.0
FOOT_POCKET = 3.5        # how deep his feet sit
FOOT_CLEAR = 0.3
WEIGHT_SKIN = 5.0        # top skin over the weight pocket
WEIGHT_WALL = 8.0


def base(foot_rects):
    """foot_rects: (x0, x1, y0, y1) for each foot, printed size, centred on the hips."""
    c = BASE_CHAMFER
    slab = Manifold.batch_hull([
        Manifold.cube((BASE_W, BASE_D, BASE_H - c)).translate((-BASE_W / 2, -BASE_D / 2, 0)),
        Manifold.cube((BASE_W - 2 * c, BASE_D - 2 * c, BASE_H)).translate((-BASE_W / 2 + c, -BASE_D / 2 + c, 0)),
    ])
    for x0, x1, y0, y1 in foot_rects:
        slab = slab - Manifold.cube((x1 - x0 + 2 * FOOT_CLEAR, y1 - y0 + 2 * FOOT_CLEAR, FOOT_POCKET + 1)).translate(
            (x0 - FOOT_CLEAR, y0 - FOOT_CLEAR, BASE_H - FOOT_POCKET))
    weight = Manifold.cube((BASE_W - 2 * WEIGHT_WALL, BASE_D - 2 * WEIGHT_WALL, BASE_H - WEIGHT_SKIN + 1)).translate(
        (-BASE_W / 2 + WEIGHT_WALL, -BASE_D / 2 + WEIGHT_WALL, -1))
    return slab - weight


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    src = trimesh.load(os.path.join(HERE, "original", "Just_Badware.stl"))
    parts = sorted(src.split(only_watertight=False), key=lambda p: -p.bounding_box.volume)
    out = {"badware_torso": static_torso(parts[1])}
    for name, (idx, ball, into) in LIMBS.items():
        out["badware_" + name] = static_limb(parts[idx], ball, into)
    out["badware_chest_plate"] = from_trimesh(parts[6])

    # The feet, from the legs (foot soles at the leg's z = 0), relative to
    # each leg's ball, printed size: the hips are 2 x 10 mm apart (x 1.35),
    # the toes toward the front (-y).
    leg = parts[4]
    v = leg.vertices
    sole = v[v[:, 2] < 1.0]
    bx, by = LIMBS["leg_a"][1][:2]
    fx0, fx1 = (sole[:, 0].min() - bx) * SCALE, (sole[:, 0].max() - bx) * SCALE
    fy0, fy1 = (sole[:, 1].min() - by) * SCALE, (sole[:, 1].max() - by) * SCALE
    hip = 10.0 * SCALE
    feet = [(sx * hip + fx0, sx * hip + fx1, fy0, fy1) for sx in (-1, 1)]
    out_base = base(feet).rotate((180, 0, 0))     # upside down to print

    for name, m in out.items():
        tm = to_trimesh(m)
        tm.apply_scale(SCALE)
        tm.apply_translation(-np.r_[tm.bounds.mean(0)[:2], tm.bounds[0, 2]])
        tm.export(os.path.join(outdir, name + ".stl"))
        e = tm.extents
        print(f"{name}: {e[0]:.1f} x {e[1]:.1f} x {e[2]:.1f} mm, watertight {tm.is_watertight}", file=sys.stderr)
    tm = to_trimesh(out_base)
    tm.apply_translation((0, 0, -tm.bounds[0, 2]))
    tm.export(os.path.join(outdir, "badware_base.stl"))
    print(f"badware_base: {tm.extents[0]:.1f} x {tm.extents[1]:.1f} x {tm.extents[2]:.1f} mm, feet pockets "
          f"{fx1 - fx0:.1f} x {fy1 - fy0:.1f} mm", file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "stl"))
