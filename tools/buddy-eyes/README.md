# Buddy's eyes

Lets Buddy watch the desk through your PC's webcam and make the odd remark
about what he sees ("That's your third donut today, you'll be bouncing off the
walls!"), share thoughts of his own now and then (a daydream, something he's
wondering about, a plan for the day), and ask "who's that?" when someone else
shows up.

It runs on the Windows PC that runs Ollama. About once a minute, **while
Buddy's radar says you're at the desk**, it takes a webcam picture (kept in
memory only, never saved), asks the vision model what's going on, and writes
one line in a diary of the day (`diary.json`, text only). Roughly every 30
minutes it asks the chat model whether anything in the diary is worth a remark;
most of the time the answer is no. When it's yes, Buddy says it in his own voice.
About 40 % of the time (`thoughts_percent`), and sometimes when there's nothing
to remark on, he shares a thought of his own instead. Remarks that repeat
earlier ones, or mention food, drinks or things the diary never saw, are
skipped.

It doesn't watch or talk while Buddy is asleep, muted ("be quiet"), during quiet
hours (11 pm to 7 am by default), or while you've paused it.

## If he keeps seeing things that aren't there

The vision model can mistake things on your desk for something else (a dark
shape for a mug, a keyboard for a laptop). In `eyes.ini`:

- `save_last_picture = on` keeps the latest webcam picture as `last_look.jpg`
  in this folder, so you can see what the camera sees (turn it back off after).
- `desk_notes` tells the vision model what's always on the desk, and what
  isn't, e.g. `desk_notes = a desktop PC with two monitors, a keyboard and
  mouse; no laptop, no mug or cup`.

Then delete `diary.json` so the mistaken lines are gone, and restart.

## In Home Assistant

On the **Desk Buddy** device:
- **Buddy's eyes**: a switch. Off pauses watching (for gaming, calls, privacy).
  It stays off until you turn it back on, even after a restart.
- **What Buddy sees**: the latest line of the diary; its `log` attribute holds
  today's diary. Add it to Buddy's AI instructions (Settings → Devices &
  services → Ollama → Buddy brain → Configure → Instructions) so he can talk
  about your day ("how much coffee have I had?"):

      What you've noticed at the desk today through your camera (time, what was going on):
      {{ state_attr('sensor.desk_buddy_what_buddy_sees', 'log') }}

## Setup (Windows)

1. Install Python: in PowerShell, `winget install Python.Python.3.12` (or from
   python.org, ticking **Add python.exe to PATH**). Close and reopen PowerShell.
2. Get this folder: download
   https://github.com/ecstoneusps/Claude-gadget/archive/refs/heads/face-engine-cores3.zip,
   open it, and copy `tools\buddy-eyes` somewhere like `C:\BuddyEyes`.
3. In PowerShell: `cd C:\BuddyEyes` then `pip install -r requirements.txt`.
4. Copy `eyes.example.ini` to `eyes.ini` and fill it in (Notepad): your name,
   and the same MQTT login Buddy uses.
5. Double-click **run-eyes.bat**. A window shows what it's doing:
   `found Buddy`, then `saw: ...` lines while you're at the desk, and `says: ...`
   when Buddy comments.
6. To start it with Windows: press Win+R, type `shell:startup`, and put a
   shortcut to `run-eyes.bat` in the folder that opens.

If it uses the wrong camera, set `camera_index = 1`. Settings in `eyes.ini`:
how often to look and to remark, quiet hours, and which models to use.
