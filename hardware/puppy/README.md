# Puppy body for Buddy (work in progress)

A chibi, sitting puppy body for the CoreS3 + DIN base (33.5 mm deep),
remixed from **"Puppy Stand - M5Stack Core S3" by John White**
(Printables, licensed CC BY 4.0, https://creativecommons.org/licenses/by/4.0/).

Changes from the original:
- taller, rounder head with an 18 mm chamber above the CoreS3 so Grove plugs
  in Ports B and C (which face up) aren't crushed, and a cable channel behind
  the CoreS3 down into the neck
- floppy ears
- a small sitting body (more stable under the heavy head) with room for an
  LD2410 presence radar in the chest

`concept.py` builds the proportions model (needs `pip install manifold3d trimesh`):

    python3 concept.py puppy_concept.stl
