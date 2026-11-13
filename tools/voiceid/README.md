# Buddy voice ID

Tells Buddy whether the person talking to him is his owner, so he uses your
name with you and not with your guests. Runs on the PC (CPU only, no GPU),
like Kokoro and SearXNG.

    docker build -t buddy-voiceid https://github.com/ecstoneusps/Claude-gadget.git#face-engine-cores3:tools/voiceid
    docker run -d --name buddy-voiceid --restart unless-stopped -p 8095:8095 -v buddy-voiceid:/data buddy-voiceid

The first start downloads the voice model (about 80 MB). Then set
`voice_id_url = http://<the PC's address>:8095` in Buddy's settings and say
"Hey Buddy, learn my voice".

Tuning: `-e OWNER_THRESHOLD=0.40 -e OTHER_THRESHOLD=0.25` on `docker run`
(the log, `docker logs buddy-voiceid`, shows each score). Your voiceprint is
kept in the `buddy-voiceid` Docker volume: numbers describing your voice, not
a recording. "Hey Buddy, forget my voice" deletes it.
