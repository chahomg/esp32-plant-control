# V4.30-RTC-NTP — GitHub development baseline

This is a credential-sanitized development copy of the verified V4.30 firmware. The complete unmodified original, including device-specific configuration, remains in Google Drive as `V430_RTC_NTP_FINAL_20261027.zip`. No Google Drive or Mac source files were changed.

**Do not flash this GitHub copy as-is.** Restore device-specific credentials locally, never commit them. Placeholder fields include `CLOUD_DEVICE_ID`, `CLOUD_DEVICE_KEY`, `FIREBASE_API_KEY`, `FIREBASE_DEVICE_EMAIL`, `FIREBASE_DEVICE_PASSWORD`, `FIREBASE_DB_HOST`, `FIREBASE_EXPECTED_UID`, `PLANT_FACTORY_SSID`, and `PLANT_FACTORY_PASSWORD`.

Validated local environment: Arduino IDE 1.8.19 and ESP32 Arduino Core 2.0.3. This sanitized GitHub copy has not been compiled or hardware-tested. Confirm ArduinoIoTCloud and Arduino_ConnectionHandler dependencies before cloud builds. Keep `main` unchanged; use development branches for modifications.
