# Buddy's voice recognition service. Build and run (see README.md):
#   docker build -t buddy-voiceid https://github.com/ecstoneusps/Claude-gadget.git#face-engine-cores3:tools/voiceid
#   docker run -d --name buddy-voiceid --restart unless-stopped -p 8095:8095 -v buddy-voiceid:/data buddy-voiceid
FROM python:3.11-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY server.py .
ENV VOICEID_DATA=/data
VOLUME /data
EXPOSE 8095
CMD ["uvicorn", "server:app", "--host", "0.0.0.0", "--port", "8095"]
