# Power and electrical constraints

- **ESP32-S3 main supply (confirmed project design):** standard regulated USB 5 V phone charger during independent operation; Mac USB used for development/debugging.
- **SHT30 external probe (tested):** supplied from ESP32 3.3 V rail.
- **PAR sensor (planned design):** intended to draw power from ESP32 5 V rail; verify the sensor's actual voltage range, current consumption, and board 5 V power budget **before connecting**. A label mentioning DC 30 V does not by itself establish the operating voltage.
- **RS-485 interface:** MAX3485 planned/used; confirm 3.3 V logic and pin-level compatibility with exact module.
- **LED, fans, relays and mains control:** require separate load/current and electrical isolation review; never infer that GPIO can power a load directly.

No real credentials or device-specific private configuration belongs in this repository.
