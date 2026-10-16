# Felucca: hardware and build rules (as documented)

Everything below comes from Felucca's own README, BUILDING.md, CONTRIBUTING.md, wiki and release notes, re-read at **v1.6.1** (16 Oct 2026 — the day the maintainer announced he is stepping back; the docs may not be updated further).

A line missing here means "not documented", not "false". Facts checked in the source itself are in **Verified in the source** at the bottom, kept separate on purpose.

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

`./build.sh --release X.Y` changes the package identity to `FM-1_9XY` and sets the version string (v1.6 is identity `FM-1_916`). The tag must start with two single digits (`X.Y`, optionally `.Z.W` and/or `-suffix`): `seq-1.0` is rejected, `1.0-seq` gives `FM-1_910` / `v1.0-seq`.

Build flags (environment, `0` or `1`; defaults in `firmware/src/felucca.c`, `core.h`, `icons.c`):

| Flag | Default | Meaning |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console |
| `FELUCCA_CDC_DEFAULT` | 1 | `0`: console built in but left out of USB from boot (as USB SERIAL OFF) |
| `FELUCCA_UAC` | 1 | USB audio input (the master output, stereo 44.1/48 kHz) |
| `FELUCCA_UAC_48K` | 1 | `0`: USB audio at 44.1 kHz only |
| `FELUCCA_UAC_AS_FIRST` | 1 | audio interface listed before MIDI (for macOS up to 15) |
| `FELUCCA_CPU2` | 1 | second core built in (MENU > SYSTEM > 2ND CORE, OFF by default); `0` gives 1.6's one-core code and RAM |
| `FELUCCA_UART` | 1 | TRS MIDI IN (older docs said default off / untested on hardware; BUILDING.md at 1.6.1 lists it as a normal flag) |
| `FELUCCA_SLICE` | 1 | `0` builds without the SLICE engine — a supported engine cut already |
| `FELUCCA_ICONS` | 1 | parameter icons on the knob cards |
| `FELUCCA_FM4` | 0 | the retired DIGITAL engine instead of its FM6 conversion |

Repository layout: `firmware/src/` app, `firmware/hal/` hardware layer, `firmware/loader/` update loader; `tools/` build and install scripts; `tests/` host tests; `assets/` font, icons, CC0 samples; `web/` installer, emulator.

- **`firmware/engines/` is a git submodule** ([Felucca-Engines](https://github.com/hugelton/Felucca-Engines), **MIT**, not GPL): the ANALOG, LOFI, TRIO, WHEEL and NOISE engines plus the shared `dsp.c`, `felucca_engine.h` and tables. `engines.c` `#include`s its sources directly, so an engine can be compile-gated in the parent repo without touching the submodule. `git submodule update --init` is required before the first build; GitHub ZIPs leave it empty.
- CONTRIBUTING.md: new files carry the **GPL-3.0-only SPDX header**; keep changes small and focused; test on real hardware; run `tests/run_tests.sh`. (PRs upstream are no longer guaranteed review.)
- Without `assets/samples-cc0/` the build still works: SAMPLE has no instrument sets and SLICE has no PIANO — they play a sine and the screen says NO SAMPLE. The bundled data is separable from the engine code.

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

## Verified in the source (v1.6.1 checkout)

These were on the undocumented list; the source answers them:

- **No `float` or `double` anywhere.** The Dexed port (`fm6_core.c`) is "integer only, no float"; `phys_dsp.c` documents "no float anywhere"; `tools/gen_tables.py` exists to precompute curves "so the firmware needs no float". Every `float`/`double` match in `firmware/` is a comment (double-tap, double-sampled SVF). Write new DSP and sequencer math in fixed point.
- **Memory map (`firmware/app.ld`).** App executes in place from XIP: `0x8DFBC` = **581,564 B**. RAM: `.data`/`.bss` 72 KiB, `POOL` 360 KiB (FX lines, canvases, per-part engine memory, work buffers), `NOINIT` ~15.7 KiB (survives resets/UBOOT), `.ram_text`/`.c1_text` a 24 KiB region (code that runs while flash is busy + the idle second core).
- **Measured at v1.6.1** (a real `./build.sh`): app image 511,452 B → **70,112 B flash headroom**; `.data`+`.bss` 60,984 / 73,728 B (~12.7 KB free); `POOL` 328,896 / 368,640 B (~39.7 KB free). The bundled CC0 samples are **126,244 B of ADPCM** — ~24% of the app image; SLICE adds a 44,100-sample BREAK and shares the PIANO zone. The build itself verifies `.ram_text`/`.c1_text` call discipline and that registers are touched only in `hal/`.
- **Audio path.** 44,117.6 Hz (24 MHz / 544 ticks per I2S frame). `HALF_FRAMES` = 128 stereo frames ≈ **2.9 ms per half buffer**; rendering happens in CTL-sample sub-blocks (32 per `tools/gen_tables.py`). CPU shed at **85% of a half** (`SHED_TICKS` in `audio.c`); a per-half load meter exists. The audio ISR is `fm1_alnk0_irq`; key/LED scan + USB poll is `fm1_timer5_irq`.
- **MIDI out exists.** `midi_out_event` feeds a **64-deep queue** from the audio ISR (`usb.c`). Live keyboard notes go out on the track's channel (`trk_midi_ch`), and mod wheel sends CC1. **Sequencer step playback does not send MIDI out** — the plumbing is there, the call is not.
- **Watchdog.** `fm1_wdt_arm(0x0D)` in `main.c` — reset-mode watchdog, scale 0xA = 1 s .. 0xF = 32 s, so ~8 s, fed in the main loop.
- **Build-flag defaults** in `felucca.c`/`core.h` match the table above, including `FELUCCA_UART 1`.

## Still not documented (verify in the source before relying on it)

- Whether 64-bit division is safe/cheap (the code uses 64-bit types; division cost on pi32v2 unverified).
- Any build-time check for ROM addresses. (Register access outside `hal/` IS checked by the build — see above.)
- The screen update cost.
- The flash partition layout beyond the app region: whether freed app flash can become data flash is a loader/partition question.
- A crash screen, an automatic USB rescue after repeated crashes, or a hold-a-button-at-power-on rescue. None is mentioned; the documented recovery is the one above.

## Sources

- https://github.com/hugelton/Felucca (README as of v1.5.1)
- https://github.com/hugelton/Felucca/blob/main/BUILDING.md and CONTRIBUTING.md (v1.6.1)
- `firmware/app.ld`, `firmware/src/{felucca,core,audio,usb,main,seq}.c`, `tools/build.py` (this checkout)
- https://github.com/hugelton/Felucca/wiki and its Layout page
- https://github.com/hugelton/Felucca/releases/tag/v1.6
- Release notes for 1.0.3.1, 1.0.4, 1.0.5; Discussion #213