@echo off
cd /d "%~dp0"
rem Buddy's eyes with no console window: the log goes to eyes.log, and its
rem icon in the tray by the clock pauses it, opens the log or quits it.
python -c "import cv2, paho.mqtt, requests, pystray, PIL" 2>nul || python -m pip install -r requirements.txt
python -c "import os, subprocess, sys; subprocess.Popen([os.path.join(os.path.dirname(sys.executable), 'pythonw.exe'), 'eyes.py'])"
