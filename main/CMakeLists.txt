# No REQUIRES list: `main` then depends on every component, including the
# managed ones from idf_component.yml (M5Unified, ESP-SR).
idf_component_register(
    SRCS "main.cpp" "voice.cpp" "sound.cpp"
    INCLUDE_DIRS "."
)
