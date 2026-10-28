# Hardware development reference

This directory is a **development copy** of non-secret hardware information. The originals remain unchanged in Google Drive.

## Evidence sources
- Google Drive folder: [硬體資訊](https://drive.google.com/drive/folders/1p_blcxh9E_v4GZZRSiafpPdKzEnw8EOU) (4 JPEG photos: ESP32-S3 front/back, DS3231 RTC front/back).
- Google Drive folder: [01_硬體](https://drive.google.com/drive/folders/1hsFJ2elNhB5Ciy3fwzueUjyml3enefDy) (empty at 2026-10-28 inspection).
- Firmware baseline: `firmware/V430_RTC_NTP_FINAL/V430_RTC_NTP_FINAL.ino`.

## Reference files
- [GPIO_MAP.md](GPIO_MAP.md): firmware pin assignments; **do not assume every peripheral is physically connected**.
- [POWER_SPEC.md](POWER_SPEC.md): confirmed and pending power requirements.
- [SENSOR_SPEC.md](SENSOR_SPEC.md): interfaces and known device parameters.
- [TEST_RESULTS.md](TEST_RESULTS.md): confirmed vs pending hardware validation.
- [photos/INDEX.md](photos/INDEX.md): source links for actual board photographs. Image binaries are **not yet copied into GitHub**.

**Security:** Do not copy credentials, device secrets, Wi-Fi passwords, OTA passwords, cloud tokens, or full unsanitized configuration files to GitHub. Never infer electrical safety from photographs alone.
