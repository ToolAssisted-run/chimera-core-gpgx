# chimera-core-gpgx: Genesis Plus GX as a Chimera waterbox core

Sega Genesis / Mega Drive (and Sega CD; later SMS, Game Gear, SG-1000)
emulation for Chimera, built from ekeeke's Genesis Plus GX compiled into
miniBox's deterministic sandbox and packaged as `core.wbx` + `waterbox.config`,
the same shape as chimera-core-quickernes, chimera-core-neshawk,
chimera-core-ppsspp and chimera-core-dosbox-x.

## Sources and their roles

- **BizHawk gpgx64 + waterbox/gpgx** (the author's own integration): the
  reference we imitate almost verbatim - the cinterface surface (init
  settings, memory domains, SRAM composition, input model, FPS math), the
  sync-settings catalogue, the firmware names, the controller naming.
- **Upstream ekeeke/Genesis-Plus-GX @ 27426f0** (47 commits past BizHawk's
  956fdba pin): the emulation core, vendored as submodule
  `extern/Genesis-Plus-GX`. THE RULE: keep upstream as clean as possible.
  The BizHawk-era savestate rework (quickerGPGX) was a mistake we do not
  repeat - the waterbox host savestates guest memory, so upstream's own
  savestate code is simply unused, not modified.
- **quickerGPGX**: only its test base - 17 games' .sol movies + .test
  descriptors (5 homebrew roms are public; commercial roms stay local,
  gated like dosbox's run-roms.sh).

## What we take from the BizHawk upstream diff (956fdba..051d430) - and what we drop

Kept as `patches/0001-chimera-hooks.patch` (applied in the submodule tree,
re-applied by build scripts if missing):

1. `eeprom_i2c_get_size()` / `sms_cart_is_codies()` / `sms_cart_bootrom_size()`
   peek helpers (SRAM sizing, memory domains).
2. `sram.c`: default-SRAM-for-<2MB-carts gated behind `cinterface_force_sram`.
   BizHawk fed this from its game DB; chimera has no game DB, so it becomes
   the `forceSram` setting (default false = BizHawk's no-DB-entry behavior).
3. `io_ctrl.c`: `real_input_callback()` on controller port reads - feeds the
   guest's `InputWasRead` lag export (chimera's `lag` config group), no host
   callback involved.
4. `m68kconf.h`: `M68K_CHECK_PC_ADDRESS_ERROR OPT_ON` (accuracy; BizHawk had it).
5. `s68kcpu.c`: exec hook for the Sega CD sub-68K (trace tooling, later).
6. `vdp_render.c` `DRAW_SPRITE_TILE`: the `sprites_always_on_top` bit (a kept
   sync setting; the field lives in OUR config struct, see osd.h).

Dropped (they served BizHawk features chimera deliberately does not have):

- The layer-toggle / custom-backdrop rendering patches (~100 lines of
  vdp_render.c): BizHawk NON-SYNC display knobs. Chimera has one kind of
  setting and no runtime display toggles. With them go the `backdropColor`
  sync setting (its only consumer was the dropped custom-backdrop path)
  and the `border`/`bg_pattern_cache`/`pixel` static-removals (only the VDP
  viewer needed those; revisit in the tooling milestone).
- The cpuhook signature change (returning values from read/write hooks):
  only BizHawk's value-modifying memory callbacks needed it; upstream's
  void-return hook is enough for tracing.
- `USE_RAM_DEEPFREEZE` (RamWatch freeze); not a chimera feature.
- The entire host-callback CD plumbing (`cdStreamImpl.c`, `gpgx_set_cdd_callback`,
  TOC marshalling): chimera passes RAW files (.cue/.bin/.iso) into the guest
  FS and upstream's own `cdStream` = stdio default (core/macros.h) parses and
  reads them in-guest. Zero CD patches.

## Settings (all of BizHawk's sync settings, renamed to chimera style)

From GPGXSyncSettings, 1:1: `useSixButton`, `port1`/`port2` (controller type;
BizHawk ControlTypeLeft/Right), `region`, `forceVDP`, `loadBios`, `overscan`,
`ggExtra`, `smsFmSoundChip`, `genesisFmSoundChip`, `filter`, `lowPassRange`,
`lowFreq`, `highFreq`, `lowGain`, `midGain`, `highGain`, `spritesAlwaysOnTop`.
Added: `forceSram` (was game-DB "sram" flag). Dropped: `backdropColor` (see
above). Hardcoded exactly as BizHawk's cinterface: psg/fm preamps, hq_fm,
hq_psg, cdda/pcm volumes, mono=0, cd_latency=1, enhanced_vscroll=0,
no lock-on carts (config.lock_on=0).

## Milestones

- **M1 - skeleton + native reference**: repo layout, submodule pin, patches,
  waterbox/osd.h (config struct + firmware name externs, modeled on BizHawk
  util/osd.h minus the CD callback machinery), shared adapter
  `waterbox/cinterface.c` (chimera guest ABI: Init/FrameAdvance/GetVideoBgra/
  GetAudio/memory domains/InputWasRead/SetButton), `run-native` driver builds
  and runs a homebrew rom, digests video+audio+domains. DONE when a
  quickerGPGX homebrew .sol replays with a stable digest.
- **M2 - core.wbx + equivalence gate**: meson cross build via miniBox
  toolchain (C only, no libstdc++), `run-wbx` host driver, `run-gate.sh`:
  native == sandbox == savestate-rerecord == on all digests, over the 5
  public homebrew roms (Genesis .bin x3, SMS, SG-1000 - the last two also
  prove the core's non-MD paths early).
- **M3 - settings + package**: full settings catalogue in waterbox.config
  (wired in Init exactly like BizHawk's InitSettings), file_slots.json
  (cart XOR cd via exposedWhen), default_keybinds.json, firmware
  declarations (CD_BIOS_US/EU/JP requiredWhen cd slot + region;
  MD_BIOS requiredWhen loadBios), build-package.sh (deterministic zip),
  gate legs proving a setting reaches the guest (e.g. region flips FPS,
  forceVDP flips vsync).
- **M4 - frontend gate**: tests/run-frontend.sh boots the package inside
  Chimera headless, lua digests a RAM domain vs native, keybinds adopted.
- **M5 - Sega CD**: cue/bin + iso mounted raw into the guest, upstream cdd.c
  loads them via stdio; CD BIOS via firmware channel; disc swap via
  Previous/Next Disk buttons + gpgx_swap_disc; gate with a homebrew/mini
  CD image. RISKS: miniBox VFS one-open-per-file vs cdd.c holding track FILE*
  open (and reopening the same bin per track); FILE* handles inside
  savestated guest memory. The gate decides what needs a shim.
- **M6 - the other systems**: SMS/GG/SG-1000 packaging (one core.wbx, one
  package per systemId with per-system button lists), exotic input devices
  (mouse, lightgun, XE-1AP, activator, paddle - the axes channel), lock-on
  carts if ever wanted.
- **M7 - tooling**: registers (M68K/Z80/S68K via m68k_get_reg), trace
  (HOOK_CPU exec ring buffer), VDP surfaces (nametables/patterns/sprites -
  this is where the static-removal patches return if needed).

## Input (M1-M4 scope)

The Genesis package's wire format, superset style like quickerNES/DOSBox:
`Power`, `Reset`, then P1..P8 x {Up,Down,Left,Right,A,B,C,Start,X,Y,Z,Mode}
(98 buttons > 64, so the wide SetButton channel). Ports carry gamepads only
until M6; `port1`/`port2` = none/gamepad/teamplayer/wayplay decide how many
players exist and which t_input.pad index each P# maps to (the guest mirrors
GPGXControlConverter's device walk). `useSixButton` picks 3B/6B. Power/Reset
edges call gpgx_reset(hard/soft).

## Build

- native: `meson setup build/meson-native && ninja -C build/meson-native`
- guest: `waterbox/setup-guest.sh` (cross file over miniBox's musl toolchain,
  C only) then `ninja -C build/meson-guest core.wbx`
- package: `waterbox/build-package.sh` -> `<chimera>/build/Cores/gpgx.zip`

## Milestone log

- 2026-08-26: repo created, upstream pinned at 27426f0.
- 2026-08-26: M1+M2 DONE in one stroke. Native reference and core.wbx build
  from the same cinterface.c (meson native + miniBox cross); the ~40-line
  patch set applied; run-gate.sh 13/13: all five quickerGPGX homebrew movies
  (3 Genesis, Master System, SG-1000) native == sandbox == per-frame
  savestate round-trip on every digest, and the pad-exercise leg proves
  input shapes the machine. Guest needed -std=c99 (musl's BSD uint typedef
  vs m68k.h's '#define uint').
