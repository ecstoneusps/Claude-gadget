#!/usr/bin/env python3
"""Build the face preview: compiles preview.cpp, then writes
preview/face_sheet.png (every expression) and preview/face_anim.gif."""
import pathlib
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[2]
W, H = 320, 240


def frames(path):
    data = path.read_bytes()
    n = len(data) // (W * H * 3)
    return [Image.frombytes("RGB", (W, H), data[i * W * H * 3:(i + 1) * W * H * 3]) for i in range(n)]


def main():
    out_dir = ROOT / "preview"
    out_dir.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "preview"
        subprocess.run(["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-I", str(ROOT / "lib/Face"),
                        str(ROOT / "tools/preview/preview.cpp"), str(ROOT / "lib/Face/Face.cpp"),
                        "-o", str(exe)], check=True)

        names = subprocess.run([str(exe), "sheet", str(tmp / "sheet.rgb")], check=True,
                               capture_output=True, text=True).stdout.split()
        imgs = frames(tmp / "sheet.rgb")
        cols, label_h, pad = 3, 22, 6
        rows = (len(imgs) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * (W + pad) + pad, rows * (H + label_h + pad) + pad), (40, 40, 48))
        d = ImageDraw.Draw(sheet)
        for i, (img, name) in enumerate(zip(imgs, names)):
            x = pad + (i % cols) * (W + pad)
            y = pad + (i // cols) * (H + label_h + pad)
            sheet.paste(img, (x, y))
            d.text((x + 6, y + H + 4), name, fill=(230, 230, 230))
        sheet.save(out_dir / "face_sheet.png")

        subprocess.run([str(exe), "anim", str(tmp / "anim.rgb")], check=True, capture_output=True)
        anim = frames(tmp / "anim.rgb")
        anim[0].save(out_dir / "face_anim.gif", save_all=True, append_images=anim[1:], duration=40, loop=0)
    print("wrote", out_dir / "face_sheet.png", "and", out_dir / "face_anim.gif")


if __name__ == "__main__":
    sys.exit(main())
