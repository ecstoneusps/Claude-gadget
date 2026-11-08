#!/usr/bin/env python3
"""Build the previews: preview/face_sheet.png (every expression),
preview/face_anim.gif (animation tour), preview/sounds/*.wav (puppy sound effects)
and preview/sounds_robot/*.wav (the robot voice)."""
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
        subprocess.run(["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-I", str(ROOT / "components/face"),
                        str(ROOT / "tools/preview/preview.cpp"), str(ROOT / "components/face/Face.cpp"),
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
        sounds = out_dir / "sounds"
        sounds.mkdir(exist_ok=True)
        sfx_exe = tmp / "sfx_wav"
        subprocess.run(["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-I", str(ROOT / "components/sfx"),
                        str(ROOT / "tools/preview/sfx_wav.cpp"), str(ROOT / "components/sfx/Sfx.cpp"),
                        "-o", str(sfx_exe)], check=True)
        subprocess.run([str(sfx_exe), str(sounds), "puppy"], check=True)
        robot = out_dir / "sounds_robot"
        robot.mkdir(exist_ok=True)
        subprocess.run([str(sfx_exe), str(robot), "robot"], check=True)
    print("wrote", out_dir / "face_sheet.png", out_dir / "face_anim.gif", "and", sounds)


if __name__ == "__main__":
    sys.exit(main())
