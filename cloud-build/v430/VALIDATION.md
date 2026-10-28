# Clean cloud build validation

PASS on 2026-10-28 (Asia/Taipei). Exit code 0. Tested configuration commit: `fc71bc7fde5397f80fb1f49ec71c7feb75684371`.

A new empty environment downloaded and installed all archives again using the committed lock and index snapshots. No packages, downloads, build objects or compiled core from the original passing environment were reused. Configuration files were re-fetched from the tested GitHub commit and matched byte-for-byte. Compilation used the firmware re-fetched from that same commit. Installed external libraries and CLI builtin tools exactly matched the lock.

Commands (Linux x86_64, Python 3.12.14):

```bash
python3 cloud-build/v430/build.py install --env-dir /workspace/scratch/v430-clean-repro
python3 cloud-build/v430/build.py compile --env-dir /workspace/scratch/v430-clean-repro
```

| Measurement | Result |
|---|---|
| FQBN | esp32:esp32:esp32s3 |
| CLI / Core | 1.5.1 / 2.0.3 |
| External libraries | 21, all pinned; see versions.lock.json |
| Flash | 1,125,993 / 1,310,720 bytes (85%) |
| Global/static RAM | 47,780 / 327,680 bytes (14%) |
| Remaining RAM before runtime allocations | 279,900 bytes |
| Source hashes before/after | Match sanitized V4.30 baseline |
| Compilation/link errors | None |
| Nonfatal warnings | 6 |

Warnings: three deprecated Arduino Cloud property API calls, one C++14 lambda extension in ArduinoIoTCloud, and two discarded const qualifiers in Arduino_CloudUtils tinycbor. No firmware fixes were made.

Flash usage is 48 bytes above the first cloud test. The binaries contain eight absolute installation-path strings, and this environment path is six characters longer. This validates repeatable PASS with pinned dependencies, not byte-identical binaries across different paths or hosts. No board runtime or dynamic heap/stack test was performed.

Full verbose and installation logs are retained in the test environment under `logs/`. `validation.json` records the full compile-log SHA-256, result, warnings and timestamps. No secrets, local proxy configuration, firmware binaries or build directories are committed. V4.30 firmware and main remain unchanged; no upload or firmware execution occurred.
