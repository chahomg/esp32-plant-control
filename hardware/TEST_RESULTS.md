# Hardware test status

Definitions: **PASS** = previously tested on actual hardware; **CODE** = implemented in firmware but not independently verified in this reference; **PLANNED** = intended, not verified; **UNKNOWN** = insufficient evidence.

| Item | Status | Notes |
|---|---|---|
| ESP32-S3 + Wi-Fi setup + OTA | PASS | Previously validated in project iterations |
| SHT30 external probe | PASS | I2C reading verified |
| DS3231 RTC / NTP V4.30 | PASS | Reported physical validation of V4.30 |
| PAR RS-485 reading | CODE | Source implements protocol; physical success not established by this reference |
| MicroSD data logging | CODE | Source implements SPI/SD; verify physical write/read test logs |
| CO2 sensor | PLANNED | No selected hardware yet |
| Cloud build V4.30 | PASS (compile only) | Arduino CLI 1.5.1; ESP32 Core 2.0.3; no physical upload |

A compile PASS does not establish sensor, power, or full-system runtime PASS.
