"""Buddy's eyes: lets Buddy watch the desk through the PC's webcam and make
remarks about what he sees ("That's your third donut today!").

Runs on the Windows PC that runs Ollama (see README.md). About once a minute,
while Buddy's radar says you're at the desk, it takes a webcam picture (kept in
memory only, never saved), asks the vision model what's going on, and writes
one line in a diary of the day. Every ~30 minutes, or right away when someone
else shows up, it asks the chat model whether there's something funny or kind
to say, and if so Buddy says it (through his MQTT "Say" control).

In Home Assistant (on the Desk Buddy device): a "Buddy's eyes" switch to pause
and resume watching, and a "What Buddy sees" sensor with today's diary, which
his AI instructions can include so he can talk about your day.
"""
import base64
import configparser
import datetime as dt
import json
import os
import queue
import random
import re
import sys
import threading
import time

import cv2
import paho.mqtt.client as mqtt
import requests

VERSION = "2026-11-19c"  # shown at startup, to check an update took
HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE = os.path.join(HERE, "eyes.ini")
DIARY_FILE = os.path.join(HERE, "diary.json")
STATE_FILE = os.path.join(HERE, "state.json")
PICTURE_FILE = os.path.join(HERE, "last_look.jpg")  # only with save_last_picture = on
AVAIL_TOPIC = "buddy_eyes/status"


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


# ---------------------------------------------------------------------------
# Settings (eyes.ini)

def load_config():
    cfg = configparser.ConfigParser()
    # Windows hides file extensions, so "eyes.ini" typed in Explorer often
    # becomes eyes.ini.ini or eyes.ini.txt; accept those too.
    names = [CONFIG_FILE, CONFIG_FILE + ".ini", CONFIG_FILE + ".txt"]
    found = next((n for n in names if os.path.isfile(n)), None)
    if not found:
        sys.exit(f"Missing {CONFIG_FILE}: copy eyes.example.ini to eyes.ini and fill it in.")
    cfg.read(found, encoding="utf-8-sig")
    if "eyes" not in cfg:
        sys.exit(f"{found} has no [eyes] line at the top: copy eyes.example.ini again.")
    c = cfg["eyes"]
    return {
        "owner": c.get("owner_name", "my owner").strip(),
        "camera": c.getint("camera_index", 0),
        "ollama": c.get("ollama_url", "http://localhost:11434").rstrip("/"),
        "vision_model": c.get("vision_model", "qwen2.5vl:7b").strip(),
        "chat_model": c.get("chat_model", "qwen3:4b-instruct").strip(),
        "context_size": c.getint("context_size", 8192),
        "mqtt_host": c.get("mqtt_host").strip(),
        "mqtt_port": c.getint("mqtt_port", 1883),
        "mqtt_user": c.get("mqtt_user", "").strip(),
        "mqtt_password": c.get("mqtt_password", "").strip(),
        "look_every": c.getint("look_every_seconds", 60),
        "remark_every": c.getint("remark_every_minutes", 30),
        "thoughts": max(0, min(100, c.getint("thoughts_percent", 40))),
        "desk_notes": c.get("desk_notes", "").strip(),
        "about_owner": c.get("about_owner", "").strip(),
        "not_there": [w.strip().lower() for w in c.get("not_there", "").split(",") if w.strip()],
        "save_picture": c.getboolean("save_last_picture", False),
        "quiet_from": c.get("quiet_hours_from", "23:00").strip(),
        "quiet_to": c.get("quiet_hours_to", "07:00").strip(),
    }


# ---------------------------------------------------------------------------
# The diary of the day: one line per change in what's going on.

class Diary:
    def __init__(self, path):
        self.path = path
        self.day = dt.date.today().isoformat()
        self.lines = []  # [{"time": "14:05", "what": "...", "people": 1}]
        self.remarks = []  # what Buddy already said today
        try:
            with open(path, encoding="utf-8") as f:
                d = json.load(f)
            if d.get("day") == self.day:
                self.lines, self.remarks = d.get("lines", []), d.get("remarks", [])
        except (OSError, ValueError):
            pass

    def _roll(self):
        today = dt.date.today().isoformat()
        if today != self.day:  # a new day: start a new diary
            self.day, self.lines, self.remarks = today, [], []

    def add(self, what, people, now=None):
        """Adds a line unless it says the same as the last one. True if added."""
        self._roll()
        now = now or dt.datetime.now()
        if self.lines and self.lines[-1]["what"].lower() == what.lower():
            return False
        self.lines.append({"time": now.strftime("%H:%M"), "what": what, "people": people})
        self.lines = self.lines[-200:]
        self.save()
        return True

    def said(self, remark):
        self._roll()
        self.remarks.append(remark)
        self.remarks = self.remarks[-30:]
        self.save()

    def text(self, last=None):
        lines = self.lines[-last:] if last else self.lines
        return "\n".join(f"{l['time']} {l['what']}" for l in lines)

    def split(self, minutes=30, now=None):
        """(earlier lines, lines from the last `minutes`) as text."""
        now = now or dt.datetime.now()
        cut = (now - dt.timedelta(minutes=minutes)).strftime("%H:%M")
        old = [l for l in self.lines if l["time"] < cut]
        new = [l for l in self.lines if l["time"] >= cut]
        fmt = lambda ls: "\n".join(f"{l['time']} {l['what']}" for l in ls)
        return fmt(old), fmt(new)

    def save(self):
        try:
            with open(self.path, "w", encoding="utf-8") as f:
                json.dump({"day": self.day, "lines": self.lines, "remarks": self.remarks}, f, indent=1)
        except OSError as e:
            log(f"couldn't save the diary: {e}")


# ---------------------------------------------------------------------------
# Webcam and models

def snapshot(index):
    """One JPEG from the webcam (opened just for this, so it isn't held on to)."""
    cam = cv2.VideoCapture(index, cv2.CAP_DSHOW) if os.name == "nt" else cv2.VideoCapture(index)
    try:
        if not cam.isOpened():
            return None
        frame = None
        for _ in range(8):  # let the exposure settle
            ok, f = cam.read()
            if ok:
                frame = f
        if frame is None:
            return None
        h, w = frame.shape[:2]
        if w > 768:
            frame = cv2.resize(frame, (768, int(h * 768 / w)))
        ok, jpg = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 80])
        return jpg.tobytes() if ok else None
    finally:
        cam.release()


def ollama_chat(cfg, model, messages, json_format=False, timeout=120, temperature=None, steady=False):
    body = {"model": model, "messages": messages, "stream": False, "keep_alive": "10m", "options": {}}
    if temperature is not None:
        body["options"]["temperature"] = temperature
    if steady:  # the vision model: short, and no getting stuck repeating itself
        body["options"].update({"repeat_penalty": 1.15, "num_predict": 200})
    if model == cfg["chat_model"]:
        # The same context size as Home Assistant and Buddy on the PC: then all
        # three share one loaded copy of the chat model.
        body["options"]["num_ctx"] = cfg["context_size"]
        if "instruct" not in model:
            body["think"] = False  # qwen3:8b would think it over silently first
    if json_format:
        body["format"] = "json"
    r = requests.post(f"{cfg['ollama']}/api/chat", json=body, timeout=timeout)
    r.raise_for_status()
    text = r.json().get("message", {}).get("content", "")
    return re.sub(r"<think>.*?</think>", "", text, flags=re.S).strip()


def look(cfg, jpg):
    """What's going on at the desk: (activity, number of people, something notable)."""
    prompt = (
        f"This is a webcam picture of {cfg['owner']}'s desk. The person sitting at the desk is "
        f"{cfg['owner']}, and a presence sensor confirms {cfg['owner']} is there right now (maybe "
        "partly out of frame, leaning back or in poor light). "
        + (f"About the desk: {cfg['desk_notes']}. Don't mention what's always on the desk unless "
           f"{cfg['owner']} is using it right now. " if cfg["desk_notes"] else "")
        + "Reply with JSON only: "
        '{"people": how many real people are in the room (don\'t count people on screens, photos or '
        'posters), '
        '"activity": one short sentence about what ' + cfg["owner"] + " is doing, eating, drinking or holding, "
        '"notable": something clearly visible and out of the ordinary, or "" (usually "")}. '
        f"The activity is only about {cfg['owner']} and what's in their hands or right in front of them: "
        "things in the background behind them (counters, shelves, another room) aren't theirs. "
        "Describe only what you can clearly see in this picture; if you aren't sure what an object is, "
        "leave it out rather than guess (a dark shape is not a mug, a keyboard is not a laptop, a blurry "
        "moving hand isn't holding anything). Don't describe clothing: it isn't news."
    )
    reply = ollama_chat(cfg, cfg["vision_model"],
                        [{"role": "user", "content": prompt, "images": [base64.b64encode(jpg).decode()]}],
                        json_format=True, temperature=0.2, steady=True)
    LAST_REPLY[0] = reply
    return parse_look(reply)


def _drop_echo(text):
    """Removes sentences or brackets that repeat the prompt's note about the
    presence sensor (the vision model sometimes copies it back) or talk about
    the picture itself ("the image is blurry") instead of the desk."""
    parts = re.split(r"(?<=[.!?])\s+|\s*[()]\s*", text)
    keep = [p for p in parts if p and not re.search(r"sensor|confirm|out of frame|poor light|behind|counter|background|not on (?:his|her|their|the) desk|image|picture|photo|webcam|camera|blurr|difficult to|hard to tell|wearing|shirt|hoodie|sweater|jacket|clothing|outfit", p, re.I)]
    return " ".join(keep).strip().rstrip(" .,;")


# Things the vision model keeps "seeing" that aren't there (not_there in
# eyes.ini): taken out of every diary line.
NOT_THERE = []
LAST_REPLY = [""]  # the vision model's last answer, for the log
# The desk notes: a small vision model repeats them back as if it saw them
# ("Eric's red cup holding chocolate milk"), so anything copied from them goes.
DESK_NOTES = [""]


_SMALL = set("the a an and or of to in on at is are his her their its with from for by it this that "
             "eric always never so none those".split())


def copies_notes(text, notes=None):
    """True if `text` is mostly words from one of the desk notes ("Eric's red
    cup holding chocolate milk" from "his red cup always holds chocolate milk")."""
    notes = DESK_NOTES[0] if notes is None else notes
    words = lambda s: {w for w in re.findall(r"[a-z0-9]+", s.lower().replace("'s", "")) if w not in _SMALL}
    t = words(text)
    if len(t) < 2:
        return False
    for clause in re.split(r"[.;:]", notes):
        shared = len(t & words(clause))
        if shared >= 3 and shared >= 0.6 * len(t):
            return True
    return False


def drop_copied(text):
    parts = re.split(r"(?<=[.!?,;])\s+|\s*[()]\s*", text)
    return " ".join(p for p in parts if p and not copies_notes(p)).strip().rstrip(" .,;")


def remove_absent(text, words=None):
    """'sitting at a desk with a laptop, typing' -> 'sitting at a desk, typing'."""
    original = text
    for w in (NOT_THERE if words is None else words):
        pat = re.escape(w) + r"s?"
        # "with a laptop", "using his laptop", "on the white laptop"
        text = re.sub(r"\s*,?\s*\b(?:with|using|on|at|holding|near|beside|and|or|while using)\s+"
                      r"(?:(?:a|an|the|his|her|their)\s+)?(?:\w+\s+)?" + pat + r"\b", "", text, flags=re.I)
        # Anything else still mentioning it goes, sentence or bracket at a time.
        parts = re.split(r"(?<=[.!?,;])\s+|\s*[()]\s*", text)
        text = " ".join(p for p in parts if p and not re.search(r"\b" + pat + r"\b", p, re.I))
    text = text.strip().rstrip(" .,;")
    if text == original.strip().rstrip(" .,;"):
        return text
    # Nothing sensible left ("Eric is"): drop it (that look is skipped).
    if re.search(r"\b(is|are|was|were|be|a|an|the|his|her|their|with|and)$", text, re.I) or len(text.split()) < 2:
        return ""
    return text


def parse_look(reply):
    try:
        d = json.loads(reply)
    except ValueError:
        return None
    try:
        people = int(d.get("people", 0))
    except (TypeError, ValueError):
        people = 0
    activity = remove_absent(drop_copied(_drop_echo(str(d.get("activity", "")))))
    notable = remove_absent(drop_copied(_drop_echo(str(d.get("notable", "")))))
    # Buddy's eyes only looks while the radar says someone is at the desk, so
    # "nobody there" is the camera missing them (out of frame, dark), not news.
    if not activity or re.search(r"\b(nobody|no one|no-one|empty|no person|not visible)\b", activity, re.I):
        return None
    return activity, max(people, 1), notable


PERSONALITY = (
    "You are Buddy, a small robot puppy who lives on {owner}'s desk, with the quick wit of a clever "
    "stand-up comedian: playful teasing, absurd observations, never mean. You can see the desk through "
    "a camera, and you keep a diary of what you've seen today."
)


def remark(cfg, diary, reason):
    """Asks the chat model for one remark (or None)."""
    now = dt.datetime.now().strftime("%H:%M")
    said = "\n".join(diary.remarks[-8:]) or "(nothing yet)"
    earlier, lately = diary.split()
    task = (
        f"It's {now}. Your diary of what you've seen at the desk today (time, what was going on).\n"
        f"Earlier today (background only: counts and patterns, it's over now):\n{earlier or '(nothing)'}\n\n"
        f"In the last 30 minutes:\n{lately or '(nothing)'}\n\n{reason}\n\n"
        "Your remark must be about something from the last 30 minutes (earlier lines can add a count or a "
        "pattern, like how many times today). Don't talk about earlier things as if they're happening now.\n\n"
        + (f"Things you know about {cfg['owner']} (only use one when the diary shows something it's about, "
           f"to make the joke land): {cfg['about_owner']}\n\n" if cfg["about_owner"] else "")
        +
        "Say one short, natural remark out loud to " + cfg["owner"] + " about what you've noticed: funny, "
        "teasing or kind, one or two sentences, like a friend glancing over. Patterns over the day make the "
        "best remarks. Only mention things that are written in the diary: never invent food, drinks, "
        "counts or faces that aren't there. If the diary only shows ordinary sitting, working or looking "
        "at the screen, reply NONE: that's not worth a remark. "
        "Don't repeat, rephrase or bring up again anything you already said today:\n"
        f"{said}\n\nStart with one emotion tag in square brackets, like [laughing], [happy], "
        "[surprised], [thinking] or [silly]. No lists, no markdown, no emoji. If nothing is worth saying "
        "right now, reply with just NONE."
    )
    reply = ollama_chat(cfg, cfg["chat_model"], [
        {"role": "system", "content": PERSONALITY.format(owner=cfg["owner"])},
        {"role": "user", "content": task},
    ])
    return clean_remark(reply)


# Starting points for a thought of his own, so they don't all sound the same.
THOUGHT_SEEDS = (
    "something you've been wondering about lately (big or silly)",
    "what you'd love to do today if you weren't a small robot puppy on a desk",
    "a funny thought that just popped into your head",
    "a daydream you just had",
    "a tiny complaint about robot-puppy life, played for laughs",
    "a question you'd like to ask {owner}, just out of curiosity",
    "a plan or goal you've secretly been working on",
    "a strange or fun fact you were thinking about, and your take on it",
    "something you're looking forward to later today or this week",
    "an opinion you've formed about something ordinary (socks, Mondays, clouds, keyboards...)",
    "how you're feeling right now, and why, in a funny way",
    "a game or challenge you just invented",
)


CLAIMS_ABOUT_OWNER = re.compile(r"\b(every ?time you|whenever you|you always|you never|you (say|said|keep saying))\b", re.I)


def fresh_thought(cfg, diary, tries=3):
    """A thought that isn't like anything he said today and doesn't make
    claims about the owner (new starting point each try), or None."""
    for _ in range(tries):
        r = thought(cfg, diary)
        if not r:
            continue
        if too_similar(r, diary.remarks):
            log(f"(skipped a repeat: {r})")
        elif CLAIMS_ABOUT_OWNER.search(r):
            log(f"(skipped, guessed about you: {r})")
        else:
            return r
    return None


def thought(cfg, diary):
    """Asks the chat model for a thought of Buddy's own (or None)."""
    now = dt.datetime.now()
    seed = random.choice(THOUGHT_SEEDS).format(owner=cfg["owner"])
    task = (
        f"It's {now.strftime('%A')} at {now.strftime('%H:%M')}. You're sitting on {cfg['owner']}'s desk and "
        f"feel like saying something out of the blue: share {seed}. This is your own inner life talking, "
        f"not a comment on what {cfg['owner']} is doing, so don't describe or guess what {cfg['owner']} is "
        f"doing, eating, drinking or saying. You're talking to {cfg['owner']}: call them \"you\", not by "
        "name in the third person. One or two short sentences (under 35 words), spoken out loud, playful "
        f"and a little quirky, like a friend thinking aloud. Don't claim things about what {cfg['owner']} "
        "always says or does.\n\nStart with one emotion tag in square brackets, like [happy], [thinking], [silly], "
        "[surprised] or [sleepy]. No lists, no markdown, no emoji."
    )
    reply = ollama_chat(cfg, cfg["chat_model"], [
        {"role": "system", "content": PERSONALITY.format(owner=cfg["owner"])},
        {"role": "user", "content": task},
    ], temperature=1.0)
    return clean_remark(reply)


def clean_remark(reply):
    r = reply.strip().strip('"').strip()
    if not r or re.fullmatch(r"(\[[a-z ]+\]\s*)?NONE\.?", r, flags=re.I):
        return None
    r = re.sub(r"[*_`#]+", "", r)  # markdown emphasis would be read out or look odd
    r = re.sub(r"[\U0001F000-\U0001FAFF\u2600-\u27BF\uFE0F\u200D]", "", r).strip()  # emoji
    if len(r) > 300:  # too long for a passing remark
        return None
    return r


# Things the small chat model likes to make up. A remark mentioning one of
# them is only allowed if the diary mentions it too.
INVENTABLE = ("coffee", "tea", "drink", "drinking", "mug", "cup", "glass", "bottle", "soda", "beer", "water", "energy drink", "juice",
              "donut", "doughnut", "snack", "chips", "cookie", "pizza", "sandwich", "burger", "candy",
              "chocolate", "milk", "face", "yawn", "phone", "laptop", "cat", "dog", "hat", "glasses")


# For "did the next look see it too?", different words for the same kind
# of thing count as the same: "a red cup", "about to drink", "drinking".
KINDS = {
    "drink": ("coffee", "tea", "drink", "drinking", "mug", "cup", "glass", "bottle", "soda", "beer", "water",
              "energy drink", "juice", "milk"),
    "food": ("donut", "doughnut", "snack", "chips", "cookie", "pizza", "sandwich", "burger", "candy",
             "chocolate"),
}


def kinds_in(text):
    """The kinds of thing (drink, food, phone...) a text mentions."""
    out = set()
    for thing in things_in(text):
        out.add(next((k for k, words in KINDS.items() if thing in words), thing))
    if "drink" in out:
        out.discard("food")  # chocolate milk is a drink
    return out


def things_in(text):
    """Which of the easily imagined things (INVENTABLE) a text mentions."""
    t = text.lower()
    return {x for x in INVENTABLE if re.search(r"\b" + re.escape(x) + r"s?\b", t)}


def invented(remark, diary_text, about=""):
    """The first thing the remark mentions that the diary never does, or None.
    A fact from about_owner counts when the diary shows what it's about:
    "chocolate milk" is fine once the diary has "red cup", if about_owner
    says the red cup is for chocolate milk."""
    r, d = remark.lower(), diary_text.lower()
    seen = things_in(d)
    clauses = [c for c in re.split(r"[.;\n]", about.lower()) if c.strip()]
    for thing in INVENTABLE:
        pat = r"\b" + re.escape(thing) + r"s?\b"
        if not re.search(pat, r) or re.search(pat, d):
            continue
        if any(re.search(pat, c) and (things_in(c) - {thing}) & seen for c in clauses):
            continue
        return thing
    return None


STOPWORDS = set("""about after again also another back been before being could does doing done even
every from have here just know like make maybe more most much must only over really right same should
some still such than that their them then there these they thing things think this those through very
want were what when where which while will with would your you're yours today wonder wondering""".split())


def _words(text):
    text = re.sub(r"\[[a-z ]+\]", " ", text.lower())  # the emotion tag
    return {w for w in re.findall(r"[a-z0-9']+", text) if len(w) > 3 and w not in STOPWORDS}


def too_similar(remark, earlier):
    """True if a remark mostly repeats one Buddy already made today."""
    a = _words(remark)
    for e in earlier:
        b = _words(e)
        if a and b and len(a & b) / min(len(a), len(b)) >= 0.35:
            return True
    return False


# ---------------------------------------------------------------------------
# MQTT: Buddy's state, the Home Assistant switch and sensor, and Say.

class Buddy:
    def __init__(self, cfg):
        self.cfg = cfg
        self.node = None  # desk_buddy_xxxxxx, found from his status messages
        self.at_desk = False
        self.awake = True
        self.muted = False
        self.pc_online = False  # Buddy on the PC is running: he says the remarks
        self.desk_online = False
        self.pc_saw = queue.Queue()  # what the PC Buddy saw when asked to look
        self.enabled = True  # the switch in Home Assistant, kept across restarts
        try:
            with open(STATE_FILE, encoding="utf-8") as f:
                self.enabled = bool(json.load(f).get("enabled", True))
        except (OSError, ValueError):
            pass
        self.seen = threading.Event()  # found Buddy
        self.c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="buddy-eyes")
        if cfg["mqtt_user"]:
            self.c.username_pw_set(cfg["mqtt_user"], cfg["mqtt_password"])
        self.c.will_set(AVAIL_TOPIC, "offline", retain=True)  # shown as unavailable if this stops
        self.c.on_connect = self.on_connect
        self.c.on_message = self.on_message
        self.c.connect_async(cfg["mqtt_host"], cfg["mqtt_port"], 30)
        self.c.loop_start()

    def topic(self, leaf):
        return f"buddy/{self.node}/{leaf}"

    def on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            log(f"MQTT login refused ({reason_code}): check mqtt_user / mqtt_password")
            return
        log("connected to MQTT; looking for Buddy")
        client.subscribe("buddy/+/status")
        client.subscribe("homeassistant/status")
        client.subscribe("buddy_pc/status")
        client.subscribe("buddy_pc/saw")
        if self.node:
            self.subscribe_buddy()

    def subscribe_buddy(self):
        for leaf in ("at_desk", "awake", "mute", "eyes/set"):
            self.c.subscribe(self.topic(leaf))
        self.announce()

    def announce(self):
        """Home Assistant entities, on the Desk Buddy device."""
        dev = {"identifiers": [self.node]}
        avail = AVAIL_TOPIC
        self.c.publish(f"homeassistant/switch/{self.node}/eyes/config", json.dumps({
            "name": "Buddy's eyes", "unique_id": f"{self.node}_eyes", "icon": "mdi:eye",
            "state_topic": self.topic("eyes"), "command_topic": self.topic("eyes/set"),
            "availability_topic": avail, "device": dev}), retain=True)
        self.c.publish(f"homeassistant/sensor/{self.node}/seeing/config", json.dumps({
            "name": "What Buddy sees", "unique_id": f"{self.node}_seeing", "icon": "mdi:eye-outline",
            "state_topic": self.topic("seeing"), "json_attributes_topic": self.topic("seeing/attributes"),
            "availability_topic": avail, "device": dev}), retain=True)
        self.c.publish(avail, "online", retain=True)
        self.c.publish(self.topic("eyes"), "ON" if self.enabled else "OFF", retain=True)

    def on_message(self, client, userdata, msg):
        t, p = msg.topic, msg.payload.decode(errors="replace")
        if t == "buddy_pc/saw":
            if p.strip():
                self.pc_saw.put(p.strip())
            return
        if t == "buddy_pc/status":
            on = p == "online"
            if on != self.pc_online:
                log("Buddy on the PC is running: he says the remarks" if on else "Buddy on the PC is off: "
                    "the desk Buddy says the remarks")
            self.pc_online = on
            if on:
                self.seen.set()  # enough to start: the PC Buddy can say the remarks
            return
        if t == "homeassistant/status" and p == "online" and self.node:
            self.announce()
            return
        parts = t.split("/")
        if len(parts) == 3 and parts[0] == "buddy" and parts[2] == "status":
            # His status is kept on the broker even while he's off ("offline"),
            # so the desk Buddy is found either way.
            if not self.node:
                self.node = parts[1]
                log(f"found Buddy ({self.node}, {'on' if p == 'online' else 'off'})")
                self.subscribe_buddy()
                self.seen.set()
            if parts[1] == self.node:
                on = p == "online"
                if on != self.desk_online and self.node:
                    log("the desk Buddy is on" if on else "the desk Buddy is off: watching without his radar")
                self.desk_online = on
            return
        if not self.node or not t.startswith(f"buddy/{self.node}/"):
            return
        leaf = t[len(f"buddy/{self.node}/"):]
        if leaf == "at_desk":
            self.at_desk = p == "ON"
        elif leaf == "awake":
            self.awake = p == "ON"
        elif leaf == "mute":
            self.muted = p == "ON"
        elif leaf == "eyes/set":
            self.enabled = p == "ON"
            self.c.publish(self.topic("eyes"), p, retain=True)
            try:
                with open(STATE_FILE, "w", encoding="utf-8") as f:
                    json.dump({"enabled": self.enabled}, f)
            except OSError:
                pass
            log("watching" if self.enabled else "paused (switch in Home Assistant)")

    def watching(self):
        """Time to look? With the desk Buddy on: when his radar says you're at
        the desk and he's awake. With only the PC Buddy: always (a picture of
        an empty desk just isn't written down)."""
        if self.desk_online:
            return self.at_desk and self.awake
        return self.pc_online

    def quiet(self):
        """Muted ("be quiet" on the desk Buddy)."""
        return self.desk_online and self.muted

    def say(self, text):
        # Buddy on the PC answers first while he's running, so he says them.
        if self.pc_online:
            self.c.publish("buddy_pc/say", text, qos=1)
        elif self.node:
            self.c.publish(self.topic("say/set"), text)

    def report(self, diary):
        if not self.node:  # the desk Buddy was never seen: no sensor to report to
            return
        last = diary.lines[-1]["what"] if diary.lines else "nothing yet today"
        self.c.publish(self.topic("seeing"), last[:250], retain=True)
        self.c.publish(self.topic("seeing/attributes"), json.dumps({"log": diary.text(last=60)}), retain=True)


def in_quiet_hours(cfg, now=None):
    now = (now or dt.datetime.now()).strftime("%H:%M")
    a, b = cfg["quiet_from"], cfg["quiet_to"]
    return (a <= now or now < b) if a > b else (a <= now < b)


# ---------------------------------------------------------------------------

def main():
    cfg = load_config()
    NOT_THERE[:] = cfg["not_there"]
    DESK_NOTES[0] = cfg["desk_notes"]
    diary = Diary(DIARY_FILE)
    buddy = Buddy(cfg)
    log(f"Buddy's eyes ({VERSION}): camera {cfg['camera']}, vision {cfg['vision_model']}, chat {cfg['chat_model']}")
    if cfg["desk_notes"]:
        log(f"desk notes: {cfg['desk_notes']}")
    if cfg["save_picture"]:
        log(f"saving each picture as {PICTURE_FILE}")
    if not buddy.seen.wait(60):
        log("haven't heard from either Buddy yet (is MQTT set up, and is one of them on?); still waiting")
        buddy.seen.wait()
    next_remark = time.time() + cfg["remark_every"] * 60 * random.uniform(0.5, 1.0)
    last_people = 1
    greeted = False  # already asked about the current visitor
    last_greeting = 0.0
    last_line = ""  # what the previous look saw
    last_spoke = 0.0
    quick_look = False  # something to double-check: look again in 15 s
    news = None  # something new in the diary, worth a remark straight away
    while True:
        # Wait for the next look, writing down what the PC Buddy saw meanwhile.
        wait_until = time.time() + (15 if quick_look else cfg["look_every"])
        while time.time() < wait_until:
            try:
                got = buddy.pc_saw.get(timeout=1.0)
            except queue.Empty:
                continue
            log(f"(Buddy on the PC was asked to look and saw: {got})")
            line = remove_absent(drop_copied(_drop_echo(got)))
            if not line:
                log("(nothing left to write down after the filters)")
            elif not diary.add(line, 1):
                log("(the same as the last diary line)")
            else:
                log(f"saw (Buddy on the PC was asked to look): {line}")
                buddy.report(diary)
                last_line = line  # (no need to double-check it)
        quick_look = False
        if not (buddy.enabled and buddy.watching()) or in_quiet_hours(cfg):
            continue
        jpg = snapshot(cfg["camera"])
        if jpg is None:
            log("couldn't get a picture from the webcam (in use by another app?)")
            continue
        try:
            seen = look(cfg, jpg)
        except requests.RequestException as e:
            log(f"vision model: {e}")
            continue
        if cfg["save_picture"]:  # so you can see what the camera sees
            try:
                with open(PICTURE_FILE, "wb") as f:
                    f.write(jpg)
            except OSError:
                pass
        del jpg
        if not seen:
            log(f"(looked: nothing to write down; the vision model said {LAST_REPLY[0][:160]!r})")
            continue
        activity, people, notable = seen
        line = activity + (f" ({notable})" if notable else "")
        log(f"(looked: {line})")
        # The vision model sometimes imagines a mug or a snack in one picture:
        # those only go in the diary once a second look in a row sees them too.
        unsure = kinds_in(line) - kinds_in(last_line)
        last_line = line
        if unsure:
            log(f"(not sure yet, {', '.join(sorted(unsure))}: {line})")
            quick_look = True  # check again soon instead of in a minute
        else:
            before = kinds_in(diary.split()[1])  # what the last 30 minutes already had
            if diary.add(line, people):
                log(f"saw: {line} [{people} people]")
                buddy.report(diary)
                # Something new (a drink, a snack...) is worth a remark now,
                # not whenever the timer comes round (at most every 5 min).
                if kinds_in(line) - before and time.time() - last_spoke > 300:
                    news = line
        # Someone else showed up: say something now (one greeting per visitor,
        # and not more than every 10 min if they keep popping in and out).
        reason = None
        visitor = False
        fresh, news = news, None
        # Two looks in a row, so one miscount doesn't make him ask "who's that?".
        if people >= 2 and last_people >= 2 and not greeted and time.time() - last_greeting > 600:
            greeted = True
            last_greeting = time.time()
            visitor = True
            reason = ("Someone else just came into view. Playfully ask who that is, or greet them; don't call "
                      f"them {cfg['owner']}.")
        else:
            if people < 2 and last_people < 2:
                greeted = False  # they've gone: greet the next visitor
            if fresh:
                visitor = True  # (react to what he saw, not a thought of his own)
                reason = (f"Just now: {fresh}. React to that out loud, playfully (a fact you know about "
                          f"{cfg['owner']} may fit here).")
            elif time.time() >= next_remark:
                reason = "Nothing urgent: only speak up if something in the diary is worth a remark."
        last_people = people
        if reason and not buddy.quiet():
            # Now and then (and sometimes when there's nothing to remark on),
            # a thought of his own instead of a comment on what he sees.
            own = not visitor and random.uniform(0, 100) < cfg["thoughts"]
            r = None
            try:
                if not own:
                    r = remark(cfg, diary, reason)
                    if r and too_similar(r, diary.remarks):
                        log(f"(skipped a repeat: {r})")
                        r = None
                    if r and (thing := invented(r, diary.split()[1], cfg["about_owner"])):
                        log(f"(skipped, made up \"{thing}\": {r})")
                        r = None
                    if not r and not visitor and random.uniform(0, 100) < cfg["thoughts"]:
                        own = True
                if own:
                    r = fresh_thought(cfg, diary)
            except requests.RequestException as e:
                log(f"chat model: {e}")
                r = None
            if r:
                log(f"{'thinks' if own else 'says'}: {r}")
                buddy.say(r)
                diary.said(r)
                last_spoke = time.time()
            next_remark = time.time() + cfg["remark_every"] * 60 * random.uniform(0.8, 1.2)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
