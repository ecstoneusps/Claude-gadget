# No REQUIRES list: `main` then depends on every component, including the
# managed ones from idf_component.yml (M5Unified, ESP-SR).
idf_component_register(
    SRCS "main.cpp" "voice.cpp" "sound.cpp" "proximity.cpp" "camera.cpp" "settings.cpp" "sdcard.cpp" "audio_io.cpp" "i2c_bus.cpp" "wifi.cpp" "assistant.cpp" "cpustats.cpp"
    INCLUDE_DIRS "."
)
