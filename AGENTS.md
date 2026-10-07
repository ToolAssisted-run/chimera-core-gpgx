# AGENTS.md - Genesis Plus GX core for Chimera

This repository builds ekeeke's Genesis Plus GX as a sandboxed guest for
Chimera, a frontend for tool-assisted speedruns. It produces one file,
`gpgx.chimeraCore`: a zip holding `core.wbx` (the emulator built for the
miniBox sandbox), `waterbox.config`, `default_keybinds.json`,
`file_slots.json`, `build.json` and the licence texts. The one package is five
machines, picked by the `systemHardware` setting: Mega Drive / Genesis, Sega
CD, Master System, Game Gear and SG-1000. `docs/BUILDING.md` has the detailed
build instructions; this file is the short operating guide.

## Layout

- `extern/Genesis-Plus-GX/` - upstream, a git submodule pinned to unmodified upstream.
- `patches/0001-chimera-hooks.patch` - every change made to upstream.
- `meson.build`, `meson_options.txt` - one build description, two flavors: native (`run-native`, `run-wbx`) and guest (`core.wbx`).
- `waterbox/cinterface.c` - the adapter: Genesis Plus GX behind Chimera's guest ABI. Compiled unchanged into both flavors.
- `waterbox/osd.h`, `waterbox/util/` - the port header upstream expects, and crc32.
- `waterbox/native-shim/emulibc.h` - replaces miniBox's emulibc in the native build.
- `waterbox/run-native.c`, `waterbox/run-wbx.c`, `waterbox/gate-harness.h` - the reference driver, the sandbox driver, and the replay-and-digest loop they share.
- `waterbox/waterbox.config` - what Chimera is told: machines, controllers, settings, firmware.
- `waterbox/file_slots.json` - the files the project wizard asks for, per machine.
- `waterbox/default_keybinds.json`, `waterbox/package-licenses.json` - shipped in the package.
- `waterbox/apply-patches.sh`, `waterbox/setup-guest.sh`, `waterbox/build-package.sh` - the build scripts.
- `waterbox/run-gate.sh` - the core gate.
- `waterbox/tests/` - `run-roms.sh` (manifest replays), `run-frontend.sh` (frontend gate) and their helpers.
- `tests/roms/`, `tests/movies/` - homebrew ROMs, `.sol` movies and `manifest.json`.
- `docs/PLAN.md` - design decisions and the milestone log.
- `.github/workflows/chimera.yml` - CI: core gate, frontend gate, publish. The authoritative build recipe.
- `build/` - all build output (gitignored).

## Set up the build environment

Linux on x86_64. Shell variables do not survive between separate shell
sessions: set `CHIMERA` and `MB` again in each one.

```
sudo apt-get update
sudo apt-get install -y --no-install-recommends meson ninja-build build-essential python3

git submodule update --init

CHIMERA="$HOME/chimera"      # a Chimera checkout; any absolute path will do
[ -d "$CHIMERA" ] || git clone https://github.com/ToolAssisted-run/chimera.git "$CHIMERA"
git -C "$CHIMERA" submodule update --init extern/chimera-common-minibox
MB="$CHIMERA/extern/chimera-common-minibox"

[ -f "$MB/build/meson-linux/build.ninja" ] || meson setup "$MB/build/meson-linux" "$MB"
meson compile -C "$MB/build/meson-linux"
```

The last two lines build miniBox: the sandbox host library and the C guest
toolchain. Nothing is downloaded. This core needs no C++ guest toolchain.

## Build

```
# the package: configures the guest if needed, builds core.wbx, checks and zips it
./waterbox/build-package.sh -m "$MB" -r "$CHIMERA"

# the native reference and the sandbox driver, which the gates need
meson setup build/meson-native -Dminibox_dir="$MB"
ninja -C build/meson-native
```

- The package lands at `$CHIMERA/build/Cores/gpgx.chimeraCore`.
- `meson setup` is needed once per build directory. After a source change run
  `ninja -C build/meson-native` and `ninja -C build/meson-guest`.
- meson applies `patches/` to the submodule at every configure. You do not run
  `waterbox/apply-patches.sh` by hand.
- A hand-built package is stamped `<commit>+local`, or `<commit>-dirty+local`
  when the tree has changes. The applied patches count as a change, so expect
  `-dirty`. Only CI, which sets `CORE_VERSION`, builds a publishable package.

## Install the core into Chimera

Chimera ships no cores and downloads nothing. A core is a file that somebody
puts in the cores folder.

- Source checkout: the cores folder is `$CHIMERA/build/Cores/`, and
  `build-package.sh -r "$CHIMERA"` has already written the package there.
- Release bundle: copy `gpgx.chimeraCore` into the `Cores` folder beside
  `Chimera.exe`, or into the folder chosen in File > Core Manager >
  Change folder...
- File > Core Manager lists the cores in that folder. Refresh List rescans it.
- The same file works on Linux and on Windows.

## Test before you commit

Rebuild both flavors first. The gates build nothing and will compare stale
binaries without complaint.

```
ninja -C build/meson-native && ninja -C build/meson-guest
./waterbox/run-gate.sh             # must end "<n> ok, 0 failed" (23 checks)
./waterbox/tests/run-roms.sh       # homebrew entries PASS, 0 failed
```

`run-roms.sh` reports SKIP for every entry whose ROM or BIOS is not in
`tests/roms-local/` or `tests/firmware-local/`. A SKIP is not a pass.

When the change touches `waterbox.config`, the keybinds, `file_slots.json` or
packaging, also run the frontend gate. It needs a built Chimera
(`docs/BUILDING.md`, "Frontend gate"):

```
./waterbox/build-package.sh -m "$MB" -r "$CHIMERA"
./waterbox/tests/run-frontend.sh --chimera-root "$CHIMERA"   # 5 checks, 0 failed
```

CI runs all three on every push to main and every pull request.

## Rules of this repository

- Upstream is a submodule pinned to unmodified upstream. Never commit inside
  `extern/Genesis-Plus-GX`. A change to upstream goes into a numbered patch in
  `patches/`; keep the patch set small. After a build `git status` shows the
  submodule as modified: that is the applied patch set, leave it.
- `apply-patches.sh` decides by one marker string
  (`cinterface_force_sram` in `core/cart_hw/sram.c`). It does not notice a
  patch that was edited or added. After changing `patches/`, return the
  submodule's working tree to its pin before the next configure.
- Determinism is the product. The guest must not read host time, host
  randomness or anything else that differs between runs, and a savestate must
  round-trip. The gate checks it. A change that breaks it is a bug.
- Run the gate before committing. A new check needs a negative control: break
  the thing it checks, watch it fail, and say so in the commit.
- Never commit game files, BIOS or firmware. `tests/roms/` holds only homebrew
  that is free to distribute. Local content goes in `tests/roms-local/` and
  `tests/firmware-local/`, which are gitignored. Never add network access.
- Do not write `version` or `versionDate` into `waterbox.config`: `build-package.sh` stamps them.
- Shell scripts stay executable (git mode 100755). CI runs them by path.
- Documentation prose is plain ASCII.
- Commit messages: the subject is a plain sentence saying what is now true,
  usually with a type and scope in front, and no final period. Example:
  `fix(ci): the nightly no longer cancels a push's gate`. Types in the log:
  `feat`, `fix`, `docs`, `ci`. The body is prose: what was wrong, what
  changed, how it was proved (the gate count, the negative control). Issues
  for this core are filed in the chimera repository and cited as
  `ToolAssisted-run/chimera#N`.
- Do not edit `.github/workflows` unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md` - the full build, every gate, the files a user provides.
- `docs/PLAN.md` - why the integration is shaped the way it is.
- `.github/workflows/chimera.yml` - what CI does, step by step.
- In the Chimera repository (https://github.com/ToolAssisted-run/chimera):
  `docs/porting-a-core.md`, `docs/gates.md` (read it before writing a check)
  and `docs/core-manager.md`.
