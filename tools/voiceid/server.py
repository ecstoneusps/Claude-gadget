"""Buddy's voice recognition: is the person talking his owner?

A tiny web service for the PC (run it with Docker, see README.md). Buddy sends
the audio of each question (raw 16-bit, 16 kHz, mono); this answers whether it
sounds like the owner, using SpeechBrain's ECAPA speaker model. The owner's
"voiceprint" is the average of a few samples recorded when they say "learn my
voice": numbers describing the voice, not a recording. Kept in /data.

  POST /identify     audio -> {"decision": "owner" | "other" | "unsure", "score": ..., "enrolled": ...}
  POST /enroll       audio -> adds a sample of the owner's voice -> {"samples": n, "seconds": s}
  POST /reset        forgets the owner's voice
  GET  /status       {"enrolled": ..., "samples": n}
"""
import json
import os
import threading
import time

import numpy as np
import torch
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from speechbrain.inference.speaker import EncoderClassifier

DATA = os.environ.get("VOICEID_DATA", "/data")
OWNER_AT = float(os.environ.get("OWNER_THRESHOLD", "0.40"))  # this similar or more: the owner
OTHER_BELOW = float(os.environ.get("OTHER_THRESHOLD", "0.25"))  # less similar than this: someone else
MIN_SECONDS = 1.0  # shorter than this is too little to tell
RATE = 16000
PRINT_FILE = os.path.join(DATA, "owner.npy")

os.makedirs(DATA, exist_ok=True)
model = EncoderClassifier.from_hparams(
    source="speechbrain/spkrec-ecapa-voxceleb", savedir=os.path.join(DATA, "model"), run_opts={"device": "cpu"}
)
lock = threading.Lock()
samples = np.load(PRINT_FILE) if os.path.exists(PRINT_FILE) else np.zeros((0, 192), dtype=np.float32)
app = FastAPI(title="Buddy voice ID")


def log(msg: str) -> None:
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def embed(pcm: bytes) -> tuple[np.ndarray, float]:
    audio = np.frombuffer(pcm[: len(pcm) // 2 * 2], dtype="<i2").astype(np.float32) / 32768.0
    seconds = len(audio) / RATE
    with torch.no_grad():
        e = model.encode_batch(torch.from_numpy(audio).unsqueeze(0)).squeeze().numpy()
    return e / (np.linalg.norm(e) + 1e-9), seconds


def voiceprint() -> np.ndarray | None:
    if len(samples) == 0:
        return None
    m = samples.mean(axis=0)
    return m / (np.linalg.norm(m) + 1e-9)


@app.post("/identify")
async def identify(request: Request):
    pcm = await request.body()
    seconds = len(pcm) / 2 / RATE
    with lock:
        vp = voiceprint()
    if vp is None:
        return {"decision": "unsure", "score": 0.0, "enrolled": False, "seconds": seconds}
    if seconds < MIN_SECONDS:
        return {"decision": "unsure", "score": 0.0, "enrolled": True, "seconds": seconds}
    e, _ = embed(pcm)
    score = float(np.dot(e, vp))
    decision = "owner" if score >= OWNER_AT else "other" if score < OTHER_BELOW else "unsure"
    log(f"identify: {seconds:.1f} s, score {score:.2f} -> {decision}")
    return {"decision": decision, "score": round(score, 3), "enrolled": True, "seconds": round(seconds, 2)}


@app.post("/enroll")
async def enroll(request: Request):
    global samples
    pcm = await request.body()
    seconds = len(pcm) / 2 / RATE
    if seconds < 2.0:
        return JSONResponse({"error": "too short", "seconds": seconds}, status_code=400)
    e, _ = embed(pcm)
    with lock:
        samples = np.vstack([samples, e[None, :]]).astype(np.float32)
        np.save(PRINT_FILE, samples)
        n = len(samples)
    log(f"enroll: sample {n} ({seconds:.1f} s)")
    return {"samples": n, "seconds": round(seconds, 2)}


@app.post("/reset")
async def reset():
    global samples
    with lock:
        samples = np.zeros((0, 192), dtype=np.float32)
        if os.path.exists(PRINT_FILE):
            os.remove(PRINT_FILE)
    log("reset: owner's voice forgotten")
    return {"samples": 0}


@app.get("/status")
async def status():
    with lock:
        return {"enrolled": len(samples) > 0, "samples": int(len(samples))}
