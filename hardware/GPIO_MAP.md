# ESP32-S3 GPIO map — V4.30 development reference

Source: V4.30 firmware inspection and prior physical test notes. **Code assignment is not the same as electrical verification.** Reconfirm physical board labels before wiring.

| Peripheral | Signal | GPIO / interface | Status |
|---|---|---|---|
| SHT30 | SDA | GPIO 8 / I2C | PASS: sensor communication previously tested |
| SHT30 | SCL | GPIO 9 / I2C | PASS: sensor communication previously tested |
| DS3231 RTC | SDA/SCL | shared I2C, address 0x68 | PASS: V4.30 RTC/NTP test reported; confirm wiring from physical reference |
| PAR RS-485 | UART1 RX/TX, enable | GPIO 16/17/18 in V4.30 code | Code reference; confirm individual signal-to-pin mapping against source and physical wiring |
| MicroSD | CS | GPIO 10 | Code reference; physical test status not established here |
| MicroSD | SCK | GPIO 12 | Code reference; physical test status not established here |
| MicroSD | MOSI | GPIO 11 | Code reference; physical test status not established here |
| MicroSD | MISO | GPIO 13 | Code reference; physical test status not established here |
| BOOT | BOOT_PIN | GPIO 0 | Previously tested setup trigger |

**Rule:** Before changing any GPIO, check the current firmware declarations, the board's front/back photos, and existing test records. If sources conflict, flag the discrepancy and request confirmation; do not guess.
