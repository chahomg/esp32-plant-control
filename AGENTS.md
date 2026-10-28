# Codex instructions — ESP32 plant control

Before editing firmware, inspect `hardware/README.md`, `hardware/GPIO_MAP.md`, `hardware/POWER_SPEC.md`, `hardware/SENSOR_SPEC.md`, and `hardware/TEST_RESULTS.md`. Consult `hardware/photos/INDEX.md` for original board photos; the image files are not yet mirrored into GitHub and may require Google Drive access.

Never assume a GPIO assignment, power rating, I2C address, sensor register map, or physical test PASS without explicit evidence. If information conflicts or is missing, report it before making hardware-related changes. Keep separate statuses PASS, CODE, PLANNED, UNKNOWN.

Use the reproducible V4.30 build procedure in `cloud-build/v430/` as the compatibility baseline (Arduino CLI 1.5.1, ESP32 Core 2.0.3). Check existing libraries before adding a dependency. Pin any new dependency versions; report compatibility and flash/RAM changes. Do not silently upgrade the baseline.

Never commit secrets, real credentials, passwords, keys, tokens, or unredacted factory configuration. Do not alter validated V4.30 firmware, the main branch, or original Google Drive files. Work on separate branches. Compile after changes; report warnings, memory use, and test limitations. Never perform USB/OTA uploads without explicit approval.

The long-term system objective is environmental recording and replay: continuous temperature, RH, PPFD, photoperiod, and 24-hour timelines over approximately 90 days, portable to another controller with sensor-feedback closed-loop control. Preserve backward compatibility with this design goal.
