# Sensors and peripheral interfaces

| Component | Known interface | Parameters | Verification |
|---|---|---|---|
| SHT30 external probe | I2C | 0x44 or 0x45; 100 kHz; SDA 8, SCL 9 | Communication PASS; exact detected address should be read from V4.30 logs |
| DS3231 RTC | I2C | 0x68; UTC stored, NTP updates RTC, local display UTC+8 | V4.30 functional test PASS reported |
| PAR quantum sensor | RS-485 | UART1; 4800 baud; Modbus CRC implementation in firmware | Firmware implementation present; sensor model/register details require datasheet |
| MicroSD | SPI | CS 10, SCK 12, MOSI 11, MISO 13 | Pin definitions known; physical verification status pending |
| CO2 sensor | Not selected | Interface, supply, model, calibration all pending | PLANNED |

Before introducing new libraries or sensor support, compare the exact model/datasheet, existing firmware, library compatibility with ESP32 Core 2.0.3, and free flash/RAM.
