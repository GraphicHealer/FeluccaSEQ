# FeluccaSEQ

A sequencer-focused fork of [Felucca](https://github.com/hugelton/Felucca), the custom firmware for the M-VAVE FM-1.

## The idea

Felucca is a multi-engine synth firmware with a sequencer bolted on top. FeluccaSEQ flips that priority: the sequencer is the product, and everything else exists to support it.

Two moves:

1. **Strip** the least-important parts of Felucca to free flash, RAM and CPU.
2. **Spend** what is freed on a much bigger, more capable sequencer, within reason for a small portable device with 4 knobs and a short keyboard.

## Baseline

- Fork from the **`v1.6` tag** (released 13 Oct 2026), not 1.5.1 or `main` at some arbitrary point. Release identity is `FM-1_916`.
- v1.6 already includes: conditional steps (`1:2` ... `4:4`, FILL, !FILL), EUCLID, DOUBLE and HALF in SEQ TOOLS, a piano roll that follows the playhead, QNT SEQ+T, a MIDI channel per track, and humanize (VEL, TIME) in the MOD matrix. These are the starting point, not things to build.
- Sizes at v1.6: `felucca-1.6-app.bin` is 508,108 bytes; `felucca-1.6.fwsc` is 610,086 bytes. (The app was about 425 KB at 1.0.4, so it has been growing.)
- Licence: GPL-3.0-only. Keep copyright notices and `LICENSES/`, add my own copyright line, and release source if I ever distribute binaries.
- Build host: **Linux x86-64**, where the JieLi pi32v2 toolchain runs natively (no Docker; Docker is only needed on macOS). `tools/get_toolchain.sh` installs the toolchain (default `~/.jieli/toolchain`); `JIELI_TOOLCHAIN` and `AC79_SDK` override locations (default SDK `~/fw-AC79_AIoT_SDK`). `AC79_SDK` must also be set for `tests/run_tests.sh`.

## What the sequencer already has (v1.6)

- 4 tracks, 64 steps each, per-track LEN / DIV / SWING / GATE
- Chords, ties, accent, slide, per-step chance and conditions, ratchets (x1 to x4), nudge, velocity, note length
- Parameter locks and automation (128 records shared by all 4 tracks)
- Piano roll, drum grid, SEQ > DETAIL, SEQ > AUTOMATION
- Live loop recording with overdub, step recording
- SEQ TOOLS: CLEAR, REVERSE, SHIFT, RANDOM, COOK, EUCLID, DOUBLE, HALF
- SONG: up to 16 sections, a lane per track
- 4 project slots (A to D), PHRASES pattern loader

## Goals

### 1. Free resources

Measure first, then cut. Candidates (each one must be confirmed against a real size report before it goes):

| Area | Why it is a candidate |
| --- | --- |
| Sample-based engines: SAMPLE, GRAIN, SLICE | Bundled sample data is probably the largest flash consumer |
| PHYS, VOICE | DSP code and state |
| USB serial console (`FELUCCA_CDC=0`) | Build flag, cheap |

Cut only if needed (check the size report first; revisit if the cuts above do not free enough):

| Area | Notes |
| --- | --- |
| WHEEL (tonewheel organ) | Probably pure synthesis code with no bundled data, so likely little flash. The rotary speaker may hold delay or modulation buffers; check its RAM before deciding. If its RAM is large, consider shrinking or sharing those buffers instead of cutting the engine. |

Keep (decided):

- **FM6**, the Dexed-based 6-operator FM engine, with its voice-bank support
- **The reverb and delay effects** (HALL, ROOM, SPRING, and the tempo-synced delay): useful for sequenced material
- **The cosmetic and ease-of-use features**: palettes (including CUSTOM), STYLE, LARGE, animations and LED modes, HELP, HOME TRACKS / LEVELS, the extra ARP modes. Individually small, and they make the device easier to use.
- DRUM (fully synthesized, no samples), ANALOG, the flash save code, the OTA / update entry, the loader, USB MIDI

With FM6, the reverbs and the cosmetics staying, the savings come mainly from the sample data, PHYS, VOICE and the serial console (WHEEL only if needed). The delay and reverb RAM is not freed, so any bigger patterns have to be budgeted against the RAM that remains. Measure before assuming there is room.

Removal strategy: **do not delete engine IDs.** Keep the enum and the stored sound format, compile out the DSP, and map a removed engine to a fallback (for example ANALOG). Old presets and projects still load. Decide later whether to drop the fallback.

### 2. Expand the sequencer

Ideas, roughly by payoff against UI cost — **capacity first** (decided), the rest as resources allow:

- **Capacity (first):** the 128-record automation cap, 64-step patterns (paged to 128 or 256), the 16-section song limit
- **Pattern variation:** scenes or fills per pattern, chaining inside a project, more pattern slots per project
- **Tools:** more SEQ TOOLS actions (per-step pitch transforms, probability groups) on the hold-SEQ layer
- **MIDI out from the sequencer:** check whether it exists today; if not, it makes the FM-1 a sequencer for other gear. TRS MIDI IN already exists behind a build flag (`FELUCCA_UART`, default off) but is documented as untested on hardware — worth checking too, since sequencing other gear wants clock in as well as notes out
- **More tracks:** only if cutting engines frees enough CPU, since it touches mixer, MIDI channels, UI and voice allocation. Leave until last.

### Non-goals

- New synth engines
- Changing the boot loader or partition layout early on (bricking risk)
- More than the 4-knob / keyboard control surface can drive sensibly

## Constraints (from the hardware rules)

- **Silent bad math.** Divide-by-zero does not trap on this chip; it produces a wrong value. Guard every divisor in new sequencer code (EUCLID, pitch transforms, anything dividing by LEN or a count). A past div-by-zero in the 0.9-to-FM6 project conversion crashed at start-up and put the device into WL80UBOOT.
- **Flash writes stall the LED scan for ~17 ms**, and autosave only runs while stopped, after 10 s idle, at most once a minute. Bigger project data means longer stalls — keep the format compact and saves rare.
- **RAM is the binding limit.** The maintainer rejected per-note humanize offsets because the memory "doesn't exist". Measure before assuming room for bigger patterns.
- Undocumented, verify in source before relying on: `float`/`double` use and 64-bit division, the audio callback's block size and time budget, watchdog timeout, screen update cost.

## Decisions

- **Project format: free to break.** FeluccaSEQ versions the project format; stock firmware will show FeluccaSEQ projects as empty slots and vice versa. No backward-compat conversion code. (The engine-ID fallback still stands — that is about *sounds* loading safely, not project compatibility.)

## Plan

1. Fork `hugelton/Felucca`, name it FeluccaSEQ, branch from `v1.6`. Keep `main` close to upstream and work on a `seq` branch so upstream fixes can be merged.
2. Build stock on the Linux box (`tools/get_toolchain.sh`, then `./build.sh` with `AC79_SDK` set) and run `tests/run_tests.sh` for a green baseline.
3. Build a repeatable size report (flash and static RAM per symbol) and a host test that prints `sizeof` for the step, pattern, track and project structs.
4. Rebrand visible strings only (ABOUT, INFO, version). Do not rename internals, since that makes every upstream merge a conflict. Stamp builds with `./build.sh --release seq-<version>`.
5. Prove the install and recover loop on the Linux box with a harmless build first (`tools/fm1_install.py`, needs `mido` + `python-rtmidi`). Have the XIAO RP2040 for FM-1-transporter recovery on hand before the first risky flash — on order.
6. Cut, one feature or engine per commit, regenerating `tests/golden.txt` (`GOLDEN_UPDATE=1`) and the budget files (`BUDGET_UPDATE=1`) after each so a hash change traces to one cut.
7. Design the new project data model (versioned format, no stock compatibility — see Decisions) before writing UI.
8. Add sequencer features one at a time, each checked in the browser emulator (`web/emu/`) and covered by a host test.

## Open questions

- Real flash and RAM limits, and where the bytes go (needs the size report)
- Whether freed app flash can become data flash, or whether the loader fixes the partitions
- Whether cutting only the sample engines, PHYS, VOICE and the serial console (before touching WHEEL) frees enough flash and RAM for the sequencer goals; if not, the next candidates are decided after the size report, not before
- Whether the sequencer already sends MIDI out
- Whether `FELUCCA_UART` (TRS MIDI IN) actually works on hardware
- `float`/`double` and 64-bit division support, the audio callback's time budget, watchdog and screen cost — all undocumented, check the source
- Whether `./build.sh --release seq-<version>` produces a valid `FM-1_9xx` package identity, or the tag must stay numeric
- ~~Compatibility policy~~ — decided: free to break (see Decisions)

## Risks

- **Bricking.** Back up first (the full backup omits the FM6 voice bank; keep the `.syx`). Recovery is the WL80UBOOT boot mode plus [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter), which needs a Seeed XIAO RP2040. There is no documented crash screen or power-on rescue — WL80UBOOT is the only way back.
- **Saved-data format.** Changing step or pattern structs changes the project format. Stock firmware cannot open projects it does not understand: they show as an empty slot.
- **Golden hashes.** Sequencer-only changes should not alter render hashes. If they do, something leaked into the audio path.
- **Upstream drift.** Felucca is actively developed. Merge upstream regularly, or the fork gets expensive to maintain.

## Reference

- Repo: https://github.com/hugelton/Felucca
- v1.6 release notes: https://github.com/hugelton/Felucca/releases/tag/v1.6
- Build: `BUILDING.md` in the repo
- Install a custom build: `python3 tools/fm1_install.py build/felucca.fwsc`, or a local copy of the web installer from `web/make_site.py`
- Browser emulator: https://hugelton.github.io/Felucca/webapp/try/