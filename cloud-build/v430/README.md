# V4.30 pinned cloud compile

Linux x86_64, Python **3.12.14** with `venv`/pip, CA certificates and HTTPS access are prerequisites. No Arduino IDE is required. `versions.lock.json` fixes Arduino CLI **1.5.1**, ESP32 Core **2.0.3**, pyserial **3.5**, and all **21** external libraries from the first passing cloud build. The committed reduced index snapshots fix package URLs, SHA-256 checksums, Core toolchain dependencies and CLI builtin tools. Library installation uses `--no-deps` with every version explicitly specified; do not run index updates or unversioned library installation.

From the repository root, choose a new empty directory outside the checkout:

```bash
python3 cloud-build/v430/build.py install --env-dir /tmp/v430-clean
python3 cloud-build/v430/build.py compile --env-dir /tmp/v430-clean
```

The install stage requires network access and honors the existing HTTPS proxy. Proxy configuration stays in the temporary environment, never in Git. Each command writes full logs under `/tmp/v430-clean/logs/`; the compile command and result are saved as JSON. To repeat, choose another empty environment directory. There is no dependency on the original Codex installation or download/build caches.

The compile stage uses `esp32:esp32:esp32s3` defaults: 4 MB flash, PSRAM disabled, default dual-OTA partition layout, USB CDC on boot disabled. It runs only `arduino-cli compile`, with warnings and verbose logging, two build jobs and isolated build/cache/output directories. It validates the original firmware SHA-256 before and after compilation and refuses changed sources. Generated binaries, ELF and map files stay in the environment's `artifacts/` directory.

This reproduces compilation of sanitized baseline commit `27b9f8a2d5c4de53b790d2aeae362c52a161c83b`; credentials remain placeholders. Do not supply secrets. No upload or firmware execution is performed. Compilation success does not verify runtime memory use or actual board/flash/PSRAM compatibility. Core-bundled libraries identify themselves as 2.0.0 even though the installed platform is 2.0.3.

The 21 installed libraries include transitive dependencies for other boards; only the subset selected by Arduino's ESP32 resolver is linked. The full installed list is in `versions.lock.json`, and the actual selected libraries appear in the compile log. Upstream archives must remain available; hashes prevent silently accepting changed downloads. Python and the host OS are prerequisites rather than a fully pinned OS image.
