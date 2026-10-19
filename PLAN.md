# FeluccaSEQ

A sequencer-focused fork of [Felucca](https://github.com/hugelton/Felucca), the custom firmware for the M-VAVE FM-1.

## The idea

Felucca is a multi-engine synth firmware with a sequencer bolted on top. FeluccaSEQ flips that priority: the sequencer is the product, and everything else exists to support it.

Two moves:

1. **Strip** the least-important parts of Felucca to free flash, RAM and CPU.
2. **Spend** what is freed on a much bigger, more capable sequencer, within reason for a small portable device with 4 knobs and a short keyboard.

## Baseline

- Forked at **`v1.6.1`** — `main` as of 16 Oct 2026, which is v1.6 plus bugfixes, the licensing cleanup, and the maintainer's "stepping back from active development" notice. Release identity for v1.6 was `FM-1_916`.
- **Upstream is now dormant** (occasional maintenance only, no guaranteed reviews). Drift risk is low; conversely the Felucca-Engines PR path is effectively closed — if shared engine code (`firmware/engines/`, a separate MIT repo) ever needs changes, fork it and repoint `.gitmodules`. Compile-gating an engine like WHEEL can be done in `engines.c` without touching the submodule.
- v1.6 already includes: conditional steps (`1:2` ... `4:4`, FILL, !FILL), EUCLID, DOUBLE and HALF in SEQ TOOLS, a piano roll that follows the playhead, QNT SEQ+T, a MIDI channel per track, and humanize (VEL, TIME) in the MOD matrix. These are the starting point, not things to build.
- Sizes: `felucca-1.6-app.bin` was 508,108 bytes; the measured v1.6.1 build is **511,452 B** against an XIP budget of 581,564 B (`firmware/app.ld`) — **~68.5 KB headroom**. RAM at v1.6.1: `.data`+`.bss` 60,984 / 73,728 B; `POOL` 328,896 / 368,640 B. The bundled samples alone are 126,244 B of ADPCM. (The app was about 425 KB at 1.0.4, so it has been growing.)
- **After the FeluccaSEQ cuts** (CC0 data, SLICE, PHYS, VOICE, SAMPLE, GRAIN — all `FELUCCA_*=1` restorable, indices reserved): app **353,876 B** (−157.6 KB, XIP ~243 KB free), `.data`+`.bss` 55,544 / 73,728 B (**~18.2 KB free**), `POOL` 284,640 / 368,640 B (**~84 KB free**; `eng_mem` shrank 51.6 → 7.3 KB once PHYS and GRAIN stopped sizing `ENG_MEM_PART`).
- **Measured memory map** (`tools/size_report.py`, v1.6.1): `.data`/`.bss` only **8.8 KB free** — it already holds the sequencer state (`trk` 7,296 B = 4×1,824 B track_t, `chain` 13,096 B = 4 pattern snapshots for SONG). `POOL` 39.8 KB free, but its elephant is `dly_buf` **131 KB** (the tempo delay line), then `cv_px` 59.5 KB, `eng_mem` 51.6 KB, `sl_buf` 32.8 KB. `NOINIT` is 99% full (`proj_slot` 15,360 B).
- **RAM, not flash, is the wall.** Step data costs in two places: `trk` steps (704 B/track today) and `chain.source` snapshots (~2.8 KB/track). NSTEP 128 costs ~+8.4 KB in `.data`/`.bss` — it now fits the ~18.2 KB free (was 8.8 KB), and `POOL` has ~84 KB free if bigger growth needs relocation. `dly_buf` (131 KB, kept — the delay is wanted) remains the single largest RAM object if more is ever needed.
- Licence: GPL-3.0-only (the engines submodule is MIT). Keep copyright notices and `LICENSES/`, add my own copyright line, carry the GPL-3.0-only SPDX header on new files, and release source if I ever distribute binaries.
- Build host: **Linux x86-64**, where the JieLi pi32v2 toolchain runs natively (no Docker; Docker is only needed on macOS). `tools/get_toolchain.sh` installs the toolchain (default `~/.jieli/toolchain`); `JIELI_TOOLCHAIN` and `AC79_SDK` override locations (default SDK `~/fw-AC79_AIoT_SDK`, a gitee clone, tag `AC79NN_SDK_V1.2.1_2023-12-13`). `AC79_SDK` must also be set for `tests/run_tests.sh`. Build needs Python Pillow + fontTools; `git submodule update --init` is required first.
- **Known baseline artifact (host gcc 15.2.1):** `tests/v16_test`'s "CUSTOM stored (not in use): COLOR has 11 values" check FAILs under gcc `-O1` — the compiler CSEs the pure `menu_n` call across `cpal_store`, which writes the flag byte it reads (ipa-modref misses it). Firmware logic is correct; `-fno-ipa-modref` or `-fno-ipa-pure-const` makes it pass. Not a real bug; don't re-investigate. Host sanitizers (`libubsan`, `libasan`, `libtsan` packages) are needed for the UBSan/ASan/TSan test binaries.

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

Measure first, then cut. Cuts come in three severities — **data, code, feature** — and they don't have to move together:

| Area | Why it is a candidate |
| --- | --- |
| The bundled CC0 sample sets (data only) | **Done** (`FELUCCA_CC0=0` default, 4fa184c): the 126,244 B of ADPCM gone, `FELUCCA_CC0=1` restores it. |
| SAMPLE, GRAIN, SLICE engine code | **Done** (`FELUCCA_SLICE`/`FELUCCA_SAMPLE`/`FELUCCA_GRAIN=0`, abf9d2e / 75b7f0b): reserved indices, silent stubs. The USR1-3 user slots keep uploading — the slot layer (scan, backup) outlives any one engine build. |
| PHYS, VOICE | **Done** (`FELUCCA_PHYS`/`FELUCCA_VOICE=0`, 17e7c3d / e08de4e): reserved indices 9 / 5, silent stubs. |
| USB serial console (`FELUCCA_CDC=0`) | Build flag, cheap — but the console is the diagnostics channel; keep it during bring-up, cut it for release builds only if the bytes are needed |
| `FELUCCA_CPU2=0` | Diagnostic lever: gives back 1.6's one-core code and RAM, at the cost of dual-core render headroom a busier sequencer may want |

Cut only if needed (check the size report first; revisit if the cuts above do not free enough):

| Area | Notes |
| --- | --- |
| WHEEL (tonewheel organ) | Probably pure synthesis code with no bundled data, so likely little flash. Lives in the MIT engines submodule — gate it in `engines.c` (no submodule fork needed). The rotary speaker may hold delay or modulation buffers; check its RAM before deciding. If its RAM is large, consider shrinking or sharing those buffers instead of cutting the engine. |

Keep (decided):

- **FM6**, the Dexed-based 6-operator FM engine, with its voice-bank support
- **The reverb and delay effects** (HALL, ROOM, SPRING, and the tempo-synced delay): useful for sequenced material
- **The cosmetic and ease-of-use features**: palettes (including CUSTOM), STYLE, LARGE, animations and LED modes, HELP, HOME TRACKS / LEVELS, the extra ARP modes. Individually small, and they make the device easier to use.
- DRUM (fully synthesized, no samples), ANALOG, the flash save code, the OTA / update entry, the loader, USB MIDI

With FM6, the reverbs and the cosmetics staying, the savings came mainly from the sample data + DSP, PHYS, VOICE and GRAIN — done; the serial console and WHEEL remain unless a later feature needs them. The delay and reverb RAM is not freed (`dly_buf` 131 KB stays by choice), so any bigger patterns have to be budgeted against the ~84 KB of `POOL` and ~18.2 KB of `.bss` that remain.

Removal strategy: **do not delete engine IDs.** Keep the enum and the stored sound format, compile out the DSP, and map a removed engine to a fallback (for example ANALOG). Old presets and projects still load. Decide later whether to drop the fallback.

### 2. Expand the sequencer

Ideas, roughly by payoff against UI cost — **capacity first** (decided), the rest as resources allow:

- **Capacity (first):** the 128-record automation cap (3 B/record — cheap), 64-step patterns (paged to 128 or 256 — expensive in `.bss`, see Baseline: trk + chain.source both grow), the 16-section song limit. Also consider shrinking `dly_buf` (131 KB in POOL) — the largest single RAM lever if bigger patterns need it
- **Pattern variation:** scenes or fills per pattern, chaining inside a project, more pattern slots per project
- **Tools:** more SEQ TOOLS actions (per-step pitch transforms, probability groups) on the hold-SEQ layer
- **MIDI out from the sequencer (verified, promote):** the plumbing already exists — `midi_out_event` feeds a 64-deep queue from the audio ISR, live keyboard notes already go out per-track channel, and mod wheel sends CC1. Sequenced steps just never call it. Small change, high payoff: it makes the FM-1 a sequencer for other gear and sidesteps the 8-voices-shared limit. Watch the queue depth under 4 tracks of ratcheted chords. TRS MIDI IN exists (`FELUCCA_UART`, default **on** at 1.6.1) — confirm it works on hardware, since sequencing other gear wants clock in as well as notes out
- **More tracks:** only if cutting engines frees enough CPU, since it touches mixer, MIDI channels, UI and voice allocation. Leave until last.

### Non-goals

- New synth engines
- Changing the boot loader or partition layout early on (bricking risk)
- More than the 4-knob / keyboard control surface can drive sensibly

## Constraints (from the hardware rules, now partly verified in source)

- **Silent bad math.** Divide-by-zero does not trap on this chip; it produces a wrong value. Guard every divisor in new sequencer code (EUCLID, pitch transforms, anything dividing by LEN or a count). A past div-by-zero in the 0.9-to-FM6 project conversion crashed at start-up and put the device into WL80UBOOT.
- **No `float` or `double` anywhere** — the whole firmware is fixed point (Dexed was ported to integer; `gen_tables.py` precomputes curves). All new sequencer/DSP math in fixed point too.
- **Flash writes stall the LED scan for ~17 ms**, and autosave only runs while stopped, after 10 s idle, at most once a minute. Bigger project data means longer stalls — keep the format compact and saves rare.
- **RAM is the binding limit.** The maintainer rejected per-note humanize offsets because the memory "doesn't exist". Numbers now known (`app.ld`): 72 KiB `.data`/`.bss` + 360 KiB `POOL` (FX lines, canvases, per-part engine memory) + ~16 KiB `NOINIT`. Measure before assuming room for bigger patterns.
- **Flash headroom is ~73 KB** (581,564 B XIP budget − 508,108 B app). New code has to fit in what's freed plus that.
- **Audio budget:** 128 stereo frames per half buffer ≈ 2.9 ms at 44,117.6 Hz, shed at 85%. Sequencer work runs in the audio ISR — keep step logic out of the render path's way.
- **MIDI out queue is 64 events deep**, filled from ISR context — bursts (chords × ratchets × 4 tracks) can overrun it.
- **Watchdog ~8 s** (`fm1_wdt_arm(0x0D)`), fed in the main loop: long stall-free stretches in new code are fine, genuinely hung code resets the box.
- Still undocumented: 64-bit division cost, screen update cost, whether freed app flash can become data flash (partition question), the audio callback's exact per-part time split.

## Decisions

- **Project format: free to break.** FeluccaSEQ versions the project format; stock firmware will show FeluccaSEQ projects as empty slots and vice versa. **Clean break decided (FSQ1):** the FUN1–FUN10 readers are deleted, not ported — migration moves to the web editor, which will accept old-format dumps and convert them. (The engine-ID fallback still stands — that is about *sounds* loading safely, not project compatibility.)
- **Pattern library over bigger records (direction B).** Patterns become independent flash objects referenced by ID, instead of growing the single project record. Opens up pattern count, chain length and step length independently, enables a pattern selector in the song editor and a free-space indicator in SAVE, and removes the 4-slot/`chain.source` snapshot model.
- **User sample slots removed entirely.** All three 80 KiB slots and the editor upload path go; the 240 KiB region `0xA0000..0xDBFFF` becomes the pattern library. SAMPLE/GRAIN compat builds keep their engine code but have no flash slots to play from.

## Data model (FSQ1)

The stock project record is a 3,840 B fixed struct at 99.7% capacity (13 B spare): 4 tracks × (111 params + 64 packed steps) + chain + motion + 512 B of always-written FM6 patches. NSTEP=128 alone needs +2,304 B — impossible. FSQ1 splits it:

**Flash map** (replaces `storage.c`'s map comment):

| Region | Contents |
|---|---|
| `0x97000..0x9EFFF` | song slots: 4 × A/B × 4 KB (unchanged location; record shrinks, slot count may grow into it) |
| `0xA0000..0xDBFFF` | **pattern library**: 30 A/B sector-pair "pages" (was 3 × 80 KiB user samples) |
| `0xDC000..0xDFFFF` | user preset banks (unchanged) |
| `0x9F000 / 0xFE000`, `0xE5000/0xE6000`, `0xE7000`, `0xFC000/0xFD000` | UPFM6, autosave, FM6 voice bank, settings (unchanged) |

**Pattern page** — the `up_bank` model scaled up: one A/B sector-pair object; page body = directory `{id, off, len}` at the tail + variable-length records packed from the front. A page loads whole into `POOL` RAM, is modified, erased and rewritten as one unit — same torn-write safety as today, no new machinery. Pattern ID = 16 bits: high byte page index (0–29), low byte a page-local handle that survives repacking (never reused).

**Pattern record** — one track's phrase, self-contained:

- `name[8]`, flags/version
- timing: `slen` (≤128), `sdiv`, `sswing`, `sgate`, `sqnt`
- step bitmap `ceil(NSTEP/8)` + packed steps (today's 9 B encoding) for used steps only — ~210 B for a typical 16-step phrase, ~600 B dense 64-step, ~1.2 KB for a dense 128-step
- its own automation + parameter locks: `{step, param, value}` 3 B each (step ≤127, 7 bits; `MOTION_LOCK` stays on `param` bit 7). Per-pattern cap ~32 events

Capacity: ~150–500 patterns at typical densities; `patlib_space()` reports bytes/patterns free for the SAVE indicator.

**Song record** — sounds + structure only:

- globals `g[G_COUNT]`, per-track sound (`engine`, `preset`, `p[P_COUNT]` packed, `mch`), FM6 patch **only for FM6 tracks** (flag bit — recovers the 512 B unconditional block)
- chain rows `{pat_id[4], repeat}` 9 B/row — room for 32+ rows; `0xFFFF`-style sentinels for silent lanes
- name + CRC. ≈1.3 KB typical.

**RAM:** `chain.source[4]` (13 KB `.bss` of 4-track snapshots) goes away; `chain_apply` resolves pattern IDs instead — play straight from XIP where the access pattern allows, else a small per-track play buffer. The live `trk[].step` buffer stays the edit/record surface; the single live `motion` store stays for it, with per-track stores loaded at section boundaries during song playback.

**Migration:** none in firmware. An old project reads invalid → shows EMPTY; the editor's backup/dump path gets an FSQ1 counterpart and a FUN10→FSQ1 converter on the web side.

**Implementation order:** (1) remove user-sample slots + editor upload path, (2) `patlib.c` storage + host test, (3) chain on pattern IDs + drop `chain.source`, (4) FSQ1 song format + delete FUN readers, (5) motion into patterns + widen step field for NSTEP 128, (6) UI: pattern selector + SAVE space indicator, (7) NSTEP 128, (8) editor protocol + web converter.

## Plan

1. Fork `hugelton/Felucca` — done (`GraphicHealer/FeluccaSEQ`). Work happens on `main` directly (the `seq` branch was folded in; upstream is dormant, so merge hygiene matters less). Upstream tags/merges still possible if a maintenance release lands.
2. Build stock on the Linux box: `git submodule update --init`, `pip3 install Pillow fonttools`, `tools/get_toolchain.sh`, clone the AC79 SDK (gitee, tag `AC79NN_SDK_V1.2.1_2023-12-13`), then `./build.sh` and `tests/run_tests.sh` for a green baseline.
3. Build a repeatable size report (flash and static RAM per symbol — `size_fns.py` is an optimizer, not a reporter; use linker `-Map` + `nm`) and a host test that prints `sizeof` for the step, pattern, track and project structs.
4. Rebrand visible strings only (ABOUT, INFO, version). Do not rename internals, since that makes every upstream merge a conflict. Stamp builds with `./build.sh --release 1.0-seq` (numeric `X.Y` required; gives `FM-1_910` / `v1.0-seq`).
5. Prove the install and recover loop on the Linux box with a harmless build first (`tools/fm1_install.py`, needs `mido` + `python-rtmidi`). Have the XIAO RP2040 for FM-1-transporter recovery on hand before the first risky flash — on order.
6. Cut, one feature or engine per commit, regenerating `tests/golden.txt` (`GOLDEN_UPDATE=1`) and the budget files (`BUDGET_UPDATE=1`) after each so a hash change traces to one cut.
7. Build the FSQ1 data model (see Data model): pattern library first, then chain-on-pattern-IDs, then the new song record — landing the format before the UI that depends on it.
8. Add sequencer features one at a time, each checked in the browser emulator (`web/emu/`) and covered by a host test.

## Open questions

- ~~Real flash and RAM limits~~ — answered from `app.ld`: ~73 KB flash headroom; 72 + 360 + 16 KiB RAM. Still open: where the bytes go (needs the size report)
- Whether freed app flash can become data flash, or whether the loader fixes the partitions
- ~~Whether cutting only the sample engines, PHYS, VOICE and the serial console (before touching WHEEL) frees enough flash and RAM for the sequencer goals~~ — answered: yes. The five cuts above (data + SLICE + PHYS + VOICE + SAMPLE/GRAIN, without touching WHEEL or the console) free ~243 KB flash, ~18.2 KB `.bss`, ~84 KB `POOL`; NSTEP 128 fits. The serial console and WHEEL stay for now; revisit only if a later feature needs their bytes.
- ~~Whether the sequencer already sends MIDI out~~ — answered: no (plumbing yes, step playback no; see Goals)
- Whether `FELUCCA_UART` (TRS MIDI IN) actually works on hardware
- ~~`float`/`double`~~ — answered: banned, all fixed point. ~~Watchdog~~ — ~8 s. ~~Audio budget~~ — 2.9 ms halves, shed at 85%. Still open: 64-bit division cost, screen update cost
- ~~Release tag format~~ — answered: `--release 1.0-seq` → `FM-1_910` / `v1.0-seq`
- ~~Compatibility policy~~ — decided: free to break (see Decisions)

## Risks

- **Bricking.** Back up first (the full backup omits the FM6 voice bank; keep the `.syx`). Recovery is the WL80UBOOT boot mode plus [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter), which needs a Seeed XIAO RP2040. There is no documented crash screen or power-on rescue — WL80UBOOT is the only way back.
- **Saved-data format.** Changing step or pattern structs changes the project format. Stock firmware cannot open projects it does not understand: they show as an empty slot.
- **Golden hashes.** Sequencer-only changes should not alter render hashes. If they do, something leaked into the audio path.
- **Upstream is dormant** (announced 16 Oct 2026). Little drift to merge — but no upstream fixes either, and no review path for Felucca-Engines PRs; fork that repo if shared engine code ever needs changes. FeluccaSEQ may become the de facto continuation, which raises the bar for not breaking users' devices.

## Reference

- Repo: https://github.com/hugelton/Felucca
- v1.6 release notes: https://github.com/hugelton/Felucca/releases/tag/v1.6
- Build: `BUILDING.md` in the repo
- Install a custom build: `python3 tools/fm1_install.py build/felucca.fwsc`, or a local copy of the web installer from `web/make_site.py`
- Browser emulator: https://hugelton.github.io/Felucca/webapp/try/