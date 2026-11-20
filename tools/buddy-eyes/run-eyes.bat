@echo off
cd /d "%~dp0"
rem Install the needed parts the first time (or if they are missing).
python -c "import cv2, paho.mqtt, requests, pystray, PIL" 2>nul || python -m pip install -r requirements.txt
python eyes.py
pause
