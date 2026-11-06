# Contributing

Thanks for helping preserve the Leaf Valeo 11. This is a community effort — contributions of all
sizes are welcome, from a one-line fix to a major feature.

> **License note:** this project is **CC BY-NC-SA 4.0**. By contributing you agree your contribution
> is released under the same license, and that your own findings/derivatives stay **open** and
> **non-commercial** (see `LICENSE`). Please don't submit anything containing Leaf firmware,
> software, ICC presets or proprietary data — this project deliberately contains none.

## Where help is most valuable
1. **Hardware reports** from other Valeo 11 backs, Macs, macOS versions and FireWire adapters —
   especially Intel macOS 10.15–14, Apple Silicon macOS 11–15 and Intel macOS 26.
2. **Calibration research:** a reproducible, owner-run way to derive per-back calibration, and
   comparisons of the dark-frame correction against factory gain/defect correction.
3. **Robustness testing:** long sessions, disconnect/reset/sleep recovery and repeatable timing
   measurements on real hardware.

## How to contribute
1. Fork the repo and create a branch.
2. Make your change; keep it dependency-light. Camera commands must stay inside the reviewed
   allowlist — never add a command that has not been observed and reviewed, and never add firmware
   update or persistent calibration writes.
3. Run the tests (they need no hardware and no private data):
   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```
   For memory/undefined-behaviour checks, also configure with `-DVALEO_SANITIZERS=ON`.
   Add a test when you change or add behaviour.
4. Keep docs in step with behaviour changes.
5. Open a pull request describing **what** you changed, **why**, and **how you validated it**. In
   keeping with the license, please share the findings behind your change openly, not just the code.
   Say clearly which results come from real hardware and which from tests or simulation.

## Reporting issues
Open an issue with your Mac model and architecture, macOS version/build, FireWire route (macOS's
own stack or ASFW, with its version), adapter chain, the exact steps, and the full message or log.
Describe the **shape** of your inputs only — never attach private per-device data (serial numbers,
calibration, raw captures, dumps) or any Leaf material.

## Code of conduct
Be kind and constructive. This is a small preservation project for owners of long-discontinued
hardware; assume good faith.

## Support
If this helped you, support ongoing digital-back work at **[ko-fi.com/oakman](https://ko-fi.com/oakman)**
— it funds hardware, test units, media, and time for further research.
