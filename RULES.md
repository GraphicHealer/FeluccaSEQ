# Felucca: hardware and build rules (as documented)

Everything below comes from Felucca's own README, BUILDING.md, wiki and release notes, as of **v1.6** (13 Oct 2026).

Source code was not read for this file. A line missing here means "not documented", not "false".

## Chip and toolchain

- JieLi **AC79** family. The SDK used is the JieLi AC79 SDK, tag `AC79NN_SDK_V1.2.1_2023-12-13` (Apache-2.0). Three of its files go into every package: `cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`.
- Compiler: JieLi's **clang 4.0.1 for pi32v2**. The toolchain is **Linux x86-64 only**.
  - macOS: each tool runs in a `linux/amd64` `debian:bookworm-slim` Docker container (Rosetta on Apple silicon). Keep the source tree somewhere Docker can share, e.g. under `/Users`.
  - Linux x86-64: runs natively, no Docker.
- Install the toolchain with `tools/get_toolchain.sh` (default `~/.jieli/toolchain`). Override locations with `JIELI_TOOLCHAIN` and `AC79_SDK` (default SDK path `~/fw-AC79_AIoT_SDK`).
- The browser emulator (`web/emu/`) is the same firmware compiled to WebAssembly.

## Build

`./build.sh` produces, in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

`./build.sh --release <tag>` changes the package identity to `FM-1_9xx` and sets the version string (v1.6 is identity `FM-1_916`).

Build flags (environment, `0` or `1`; defaults in `firmware/src/felucca.c`):

| Flag | Default | Meaning |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console |
| `FELUCCA_UART` | 0 | TRS MIDI IN (documented as **not tested on hardware**) |

Repository layout: `firmware/src/` app, `firmware/hal/` hardware layer, `firmware/loader/` update loader; `tools/` build and install scripts; `tests/` host tests; `assets/` font, icons, CC0 samples; `web/` installer, emulator.

### Image size history (app only)

| Version | `felucca-*-app.bin` (bytes) |
| --- | --- |
| 1.0.3.1 | 417,476 |
| 1.0.4 | 425,696 |
| 1.0.5 | 428,808 |
| 1.6 | 508,108 |

The v1.6 package `felucca-1.6.fwsc` is 610,086 bytes. The app has grown about 22% between 1.0.4 and 1.6.

## Tests and budgets

- `tests/run_tests.sh` runs host tests (flash storage, user presets, MIDI parser, update entry, update loader, a DSP render, the 4-track mix, project formats, the SLICER, the regression suite, the command-line installer) and, with Node.js, the web page tests. Run it after `./build.sh`; it needs `AC79_SDK` set.
- The regression suite (`tests/regress.c`) renders every engine and preset and compares a hash of each render with `tests/golden.txt`. It also checks levels, voices and CPU cost against `tests/cpu_baseline.txt` and `tests/target_budget.txt`.
- After an intended sound change: `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1` does the same for the cost files.

## Arithmetic

- **Division by zero does not crash.** Since 1.0.3.1 the divide-by-zero trap is off: a divide by zero that slips through gives a wrong value instead of a crash.
- Why it was changed: a 0.9 project with a DIGITAL track whose INDEX + FLT ENV was 0 made the 0.9-to-FM6 conversion divide by zero at start-up. The FM-1 crashed before the screen came on, and after a few starts fell into `WL80UBOOT`. The conversion now has no divide that can be 0. **Still test your divisors:** a wrong value is not a safe value.

## Audio hardware

- USB audio: stereo, **44.1 or 48 kHz** (the computer picks), records the master output.
- The firmware **cannot turn the speaker off**; headphones in the jack do.

## Flash and memory behaviour

- Settings, presets and projects live in flash. **Saving to flash stops the LED scan for about 17 ms**; the LEDs go dark for that moment (since 1.6; earlier versions flashed the panel).
- The autosave (RESTORE LAST) is written only when the music changed, the transport is stopped and nothing has been touched for **10 s**, at most **once a minute**.
- PROJECT ERASE works only while stopped, and is written so a power cut leaves the old project or an empty slot, never half of one.
- Memory is tight. The maintainer, on humanize: per-note offsets "would need memory the FM-1 doesn't have left".

## Install, boot and recovery

- Install paths: the web installer (Chrome or Edge, USB), `python3 tools/fm1_install.py build/felucca.fwsc` (needs `mido` and `python-rtmidi`), or a local copy of the installer site via `web/make_site.py`, served from `localhost` (Web MIDI needs a secure context).
- The web installer's **Return to official V15** and M-VAVE's own updater take you back to the stock firmware.
- Installing is at your own risk. Back up first.
- **If the FM-1 stays black:** a computer may see a USB device named **`WL80UBOOT`** (a mass-storage device, ID `4C4A:8057`). That is the chip's built-in boot mode.
  1. Try another USB data cable and close every other app that uses MIDI, then run the web installer again.
  2. If it stays in boot mode, use [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter): it reads and writes the FM-1's flash from a Mac through a Seeed XIAO RP2040, with three wires to the FM-1's USB lines. Back up the flash first, then write the official firmware (M-VAVE's `FM-1.fwsc`).
- USB serial (`MENU > SYSTEM > USB SERIAL`) can be OFF so the FM-1 is a plain audio and MIDI device; this is needed for the audio input to appear on macOS 13 to 15.

## Not documented (verify in the source before relying on it)

- Whether the code uses `float` or `double`, and whether the link rejects them. (Felucca ports DaisySP and Dexed, which use float in places.)
- Whether 64-bit division is unavailable.
- Any build-time check for ROM addresses, or for hardware registers touched outside `hal/`.
- The audio callback's name, block size, sample rate and time budget.
- The watchdog timeout and the screen update cost.
- Total flash and RAM, and the partition layout.
- A crash screen, an automatic USB rescue after repeated crashes, or a hold-a-button-at-power-on rescue. None is mentioned; the documented recovery is the one above.
- Whether CONTRIBUTING.md or `docs/` contain further coding rules (not read).

## Sources

- https://github.com/hugelton/Felucca (README as of v1.5.1)
- https://github.com/hugelton/Felucca/blob/main/BUILDING.md
- https://github.com/hugelton/Felucca/wiki and its Layout page
- https://github.com/hugelton/Felucca/releases/tag/v1.6
- Release notes for 1.0.3.1, 1.0.4, 1.0.5; Discussion #213