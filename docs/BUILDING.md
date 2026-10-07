# Building the Genesis Plus GX core

This repository builds Genesis Plus GX as a sandboxed guest (`core.wbx`) and
packages it as one file, `gpgx.chimeraCore`, which Chimera loads. The one
package is five machines: Mega Drive / Genesis, Sega CD / Mega CD, Master
System, Game Gear and SG-1000. The steps below are the ones
`.github/workflows/chimera.yml` runs from a fresh clone on a public Ubuntu
runner, where they pass.

Two placeholders are used throughout:

- `<chimera>` is the absolute path of a Chimera checkout
  (https://github.com/ToolAssisted-run/chimera).
- `<minibox>` is `<chimera>/extern/chimera-common-minibox`, the miniBox
  submodule of that checkout. miniBox is the sandbox host and the guest
  toolchain.

## Requirements

- Linux on x86_64. CI uses GitHub's `ubuntu-latest` runner. Cores are built on
  Linux; the package that comes out also runs on Windows.
- git.
- For the core, the package and the core gate:

  ```
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends meson ninja-build build-essential python3
  ```

  The compiler is the distribution's gcc from `build-essential`. The workflow
  pins no compiler version. The guest is compiled by the same gcc through
  miniBox's musl specs file, so there is no separate cross compiler to
  install.
- For the frontend gate only, a built Chimera, which needs more packages:

  ```
  sudo apt-get install -y --no-install-recommends \
    meson ninja-build build-essential cmake pkg-config python3 \
    mono-complete xvfb \
    libgl1-mesa-dev libx11-dev libxext-dev libasound2-dev
  ```

  and the .NET SDK 8.0. The workflow installs it with
  `actions/setup-dotnet@v4` and `dotnet-version: '8.0'`. By hand, Chimera's
  README gives this command:

  ```
  curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0
  ```

- The core build downloads nothing. miniBox builds the guest C library (musl)
  from sources it carries. Genesis Plus GX is plain C, so miniBox's C++ guest
  toolchain is not needed.

## Get the sources

This repository, with its submodule. The workflow uses `actions/checkout@v6`
with `submodules: true`, which is:

```
git clone https://github.com/ToolAssisted-run/chimera-core-gpgx.git
cd chimera-core-gpgx
git submodule update --init
```

The submodule is `extern/Genesis-Plus-GX`, pinned to an unmodified upstream
commit.

A Chimera checkout, for miniBox. The workflow checks out Chimera's `main`
branch and then only the miniBox submodule:

```
git clone https://github.com/ToolAssisted-run/chimera.git <chimera>
git -C <chimera> submodule update --init extern/chimera-common-minibox
```

The frontend gate builds Chimera itself and needs all of its submodules
(`submodules: recursive` in the workflow):

```
git -C <chimera> submodule update --init --recursive
```

Where the scripts look when no path is given:

- `waterbox/build-package.sh` and `waterbox/tests/run-frontend.sh` look for
  Chimera at `../chimera` beside this repository, then at `$HOME/chimera`.
- `meson.build` (the native build) looks for miniBox at
  `../chimera/extern/chimera-common-minibox`.
- `waterbox/setup-guest.sh` (the guest build) looks for miniBox at
  `$HOME/chimera/extern/chimera-common-minibox`.

These defaults differ from each other. Pass the paths explicitly, as CI does
and as every command below does.

## Build miniBox

```
meson setup <minibox>/build/meson-linux <minibox>
meson compile -C <minibox>/build/meson-linux
```

This builds, under `<minibox>/build/meson-linux`:

- the sandbox host library, `source/host/libminiboxhost.so`, which the
  `run-wbx` test driver links;
- the guest toolchain: `guest-sysroot/` (musl) and the emulibc object the
  guest links.

The workflow keeps this directory in a cache (`actions/cache@v4`) and skips
`meson setup` when `build.ninja` is already there. By hand, keep the directory
and run only `meson compile` the next time.

## Build the core

### Patches

Every change to Genesis Plus GX is in `patches/0001-chimera-hooks.patch`.
`waterbox/apply-patches.sh` applies the patches to the working tree of
`extern/Genesis-Plus-GX`. `meson.build` runs that script at every configure,
so there is no need to run it by hand.

How the script behaves:

- It looks for the string `cinterface_force_sram` in
  `extern/Genesis-Plus-GX/core/cart_hw/sram.c`. If the string is there, the
  tree counts as patched and the script does nothing.
- Otherwise it runs `git apply` for each file in `patches/`, in order, and
  prints `applied <name>` for each.

After the first configure `git status` shows the submodule as modified. That
is the applied patch set, and it is expected.

### The native reference and the sandbox driver

```
meson setup build/meson-native -Dminibox_dir=<minibox>
ninja -C build/meson-native
```

This builds two programs in `build/meson-native`:

- `run-native` is the reference: the same `waterbox/cinterface.c` and the same
  Genesis Plus GX sources, compiled for the host with no sandbox. The gates
  compare the sandboxed core against it.
- `run-wbx` runs `core.wbx` through the miniBox host library and prints the
  same digests as `run-native`.

`ninja -C build/meson-native run-native` builds the reference alone. That is
enough for the frontend gate.

### The guest core

```
MINIBOX_DIR=<minibox> sh waterbox/setup-guest.sh -- -Dminibox_dir=<minibox>
ninja -C build/meson-guest
```

`setup-guest.sh` writes the meson cross file `build/guest-cross.ini` (paths of
this machine; do not commit it) and configures `build/meson-guest`. It takes
the miniBox path from `-m <miniBox dir>` or from `MINIBOX_DIR`. Arguments
after `--` go to `meson setup`. The result is `build/meson-guest/core.wbx`.

## Build the package

```
./waterbox/build-package.sh -m <minibox> -r <chimera>
```

Options:

- `-m <miniBox dir>`: the miniBox checkout. Default: `MINIBOX_DIR`, then
  `<chimera root>/extern/chimera-common-minibox`.
- `-r <chimera root>`: the Chimera checkout the package is written into.
  Default: `../chimera`, then `$HOME/chimera`.

There is no option for another output directory.

What the script does:

1. Runs `waterbox/setup-guest.sh` if `build/meson-guest` is not configured,
   then builds `core.wbx`.
2. Runs miniBox's `source/guest/check-wbx.sh` on `core.wbx`.
3. Stages `core.wbx`, `waterbox.config`, `default_keybinds.json`,
   `file_slots.json`, the licence texts and `build.json` (what built the
   package) under `build/package-staging/gpgx`.
4. Stamps `version` and `versionDate` into the staged `waterbox.config`.
5. Writes the package twice, compares the SHA-1 of both and prints it.
6. Removes `<chimera>/build/CoreCache/gpgx*`.

The file lands at `<chimera>/build/Cores/gpgx.chimeraCore`.

The version is the commit the package was built from:

- CI sets `CORE_VERSION` to the full commit SHA, and the package carries that.
- Without `CORE_VERSION` the script stamps the first 12 digits of `HEAD` and
  `+local`, with `-dirty` before it when `git diff --quiet HEAD` reports
  changes: `0123456789ab+local` or `0123456789ab-dirty+local`.
- `versionDate` is the date of that commit in UTC, never the build's date.

A package built by hand is for testing. Chimera's publishing script refuses a
version that carries `+local` or `-dirty`.

In CI the frontend gate job builds the package with `CORE_VERSION` set, runs
the frontend gate on it and uploads it as the artifact `gpgx-<commit SHA>`
(`actions/upload-artifact@v7`). The `publish` job hands that artifact to
Chimera's reusable workflow `publish-core.yml`. Nothing is published from a
pull request. A run started by hand takes an input `kind`, `dev` or `nightly`;
blank means `dev`.

## Install it into Chimera

Chimera ships no cores and downloads nothing: it has no network code. A core
gets into Chimera because somebody puts its file in the cores folder.

- In a Chimera source checkout the cores folder is `<chimera>/build/Cores/`.
  `build-package.sh -r <chimera>` has already written the package there.
- In a release bundle the cores folder is `Cores` beside `Chimera.exe`, or
  another folder chosen in File > Core Manager > Change folder... Copy
  `gpgx.chimeraCore` into it.

File > Core Manager lists what is in that folder. Refresh List rescans it, so
a package copied in while Chimera runs is found without a restart.

The same package file works on Linux and on Windows: Chimera's sandbox,
miniBox, runs the guest inside it on either.

You do not have to build it. CI publishes the package on this repository's
Releases page (https://github.com/ToolAssisted-run/chimera-core-gpgx/releases)
as `gpgx-<version>.chimeraCore`:

- `dev`: replaced on every push to main that passes the gates.
- `nightly-YYYY-MM-DD`: dated, from the scheduled run (04:00 UTC), and only
  when main moved since the last one.

To use the core, start Chimera (`build/ChimeraMono.sh` on Linux,
`build\Chimera.exe` on Windows in a source checkout), choose
File > New Project... and pick the core. To play a ROM with no project, pass
`--core=<package> <rom>` on the command line.

## Run the gates

CI runs all three and all three must pass before anything is published.

### Core gate

```
./waterbox/run-gate.sh
```

It needs `build/meson-native/run-native`, `build/meson-native/run-wbx`,
`build/meson-guest/core.wbx` and python3. It needs no Chimera build, no .NET,
no Mono and no X display. `-n <native build dir>` and `-g <guest build dir>`
point it at other build directories. It does not rebuild anything: build both
flavors first.

It runs over the homebrew ROMs and movies in `tests/` and a disc pair it
synthesizes, so no check is skipped for lack of content. It prints one line
per check:

- `<name>:equivalence`: the sandboxed core and the native reference give the
  same frame count, vsync rate, video hash, audio hash, lag count and memory
  domain digests. Run over five homebrew movies (three Genesis, one Master
  System, one SG-1000) and a 600-frame pad exercise.
- `exercise:input-shaped`: the pad exercise differs from an idle run of the
  same length, so the input reached the machine.
- `<name>:turbo`: with drawing switched off for the first half of the run,
  the machine, the audio, the lag count and the second half's pictures are
  unchanged.
- `<name>:savestate`: saving and loading the whole machine around every frame
  changes nothing.
- `cdsmoke:equivalence` and `cdsmoke:savestate`: the same two proofs for a
  Sega CD, over a synthetic cue/bin disc pair and a dummy BIOS made by
  `waterbox/tests/gen-fakecd.py`, with a disc swap at frame 100.
- `savedata:export`: the exported save files (cartridge SRAM, Sega CD backup
  RAM) are identical from both flavors.
- `settings:forceVDP`: the setting `forceVDP=pal` reaches the guest and
  changes the vsync rate and the digests.

The last line is `<n> ok, <m> failed`. A complete run has 23 checks. The exit
status is non-zero when any check fails.

### Manifest replays

```
./waterbox/tests/run-roms.sh
```

It needs the same three build outputs as the core gate. It replays every
entry of `tests/movies/manifest.json` whose ROM it can find and requires
native == sandbox == per-frame savestate round-trip over the whole movie.

- The five homebrew entries run from `tests/roms/`.
- Every other entry reports SKIP until its ROM is in `tests/roms-local/`
  (gitignored). The SKIP line names the file.
- Sega CD entries also need a BIOS in `tests/firmware-local/` (gitignored), in
  a file named `cdBiosUS`, `cdBiosEU` or `cdBiosJP`.
- Entries that start from a savestate are always skipped.

A SKIP is not a pass: it means the check did not run. In CI only the homebrew
half runs.

### Frontend gate

This gate runs the package inside Chimera, headless, under Mono. Build Chimera
first, as the workflow does:

```
cd <chimera>
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-linux
meson install -C build/meson-linux
dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false
```

Then, in this repository, with the package built and `run-native` built:

```
./waterbox/tests/run-frontend.sh --chimera-root <chimera>
```

It needs `<chimera>/build/Chimera.exe`,
`<chimera>/build/Cores/gpgx.chimeraCore`, `build/meson-native/run-native`,
mono and python3. With `DISPLAY` unset it starts its own Xvfb and stops it on
exit; with `DISPLAY` set it uses that display. `--frames N` changes the run
length (default 300). Its checks:

- `cart:frontend`: after 300 idle frames of a homebrew cartridge, the 68K RAM
  inside Chimera is byte-identical to the native reference.
- `settings:forceVDP`: `forceVDP=pal`, set through Chimera's config, builds a
  different machine that matches its own native reference.
- `keybinds`: the package's `default_keybinds.json` becomes Chimera's default
  bindings for the Genesis controller.
- `sms:frontend` and `sms:keybinds`: the same package opens a `.sms` ROM as a
  Master System, with matching RAM and the Master System controller's
  bindings.

Its logs and dumps stay in `waterbox/tests/work/` (gitignored). CI uploads
that directory when the gate fails.

## Files the core needs at run time

Game files, BIOS and firmware are never in this repository or in the package.
The user provides them. The project's `systemHardware` setting picks the
machine, and the machine decides the files:

| `systemHardware` | Machine | Files |
| --- | --- | --- |
| `genesis` | Mega Drive / Genesis | one cartridge ROM: `.md`, `.gen`, `.bin`, `.smd`, `.mdx` |
| `segacd` | Sega CD / Mega CD | one or more disc images: `.cue` with its track files, or a raw single-track `.iso` / `.bin` |
| `sms` | Master System | one cartridge ROM: `.sms` |
| `gg` | Game Gear | one cartridge ROM: `.gg` |
| `sg` | SG-1000 | one cartridge ROM: `.sg` |

For a Sega CD the first disc is in the drive at boot, and the order of the
list is the swap order of the Previous Disk and Next Disk inputs. A cartridge
for another console is refused at load.

Firmware declared in `waterbox/waterbox.config`:

| Id | What | Needed |
| --- | --- | --- |
| `cdBiosUS` | Sega CD BIOS (USA), 131072 bytes, SHA-1 `F4F315ADCEF9B8FEB0364C21AB7F0EAF5457F3ED` | Sega CD discs, by region |
| `cdBiosEU` | Mega CD BIOS (Europe), 131072 bytes, SHA-1 `F891E0EA651E2232AF0C5C4CB46A0CAE2EE8F356` | Sega CD discs, by region |
| `cdBiosJP` | Mega CD BIOS (Japan), 131072 bytes, SHA-1 `4846F448160059A7DA0215A5DF12CA160F26DD69` | Sega CD discs, by region |
| `mdBios` | Genesis TMSS boot ROM, 2048 bytes | only when `loadBios` is on |
| `msBiosUS`, `msBiosEU`, `msBiosJP` | Master System BIOS | only when `loadBios` is on |
| `ggBios` | Game Gear BIOS | only when `loadBios` is on |

A Sega CD does not load without a CD BIOS. Which one is asked for follows the
`region` setting; `autodetect` keeps all three regions possible. `loadBios` is
off by default, so a cartridge machine needs no firmware unless it is turned
on.

## Troubleshooting

- `pass -Dminibox_dir=<miniBox checkout>` from `meson setup`: the native build
  did not find miniBox at `../chimera/extern/chimera-common-minibox`. Pass
  `-Dminibox_dir=<minibox>`.
- `miniBox guest toolchain missing under <minibox>/build.` from
  `setup-guest.sh`: miniBox is not built. Run the two commands of "Build
  miniBox".
- `chimera checkout not found; pass -r <path>` from `build-package.sh`: pass
  `-r <chimera>`.
- `native build missing` or `guest build missing` from `run-gate.sh`: the gate
  builds nothing. Build both flavors first.
- `Chimera not built`, `package not installed` or `native reference not built`
  from `run-frontend.sh`: build Chimera, run `build-package.sh`, and build
  `run-native`, in that order.
- `Xvfb not found (apt install xvfb)` from `run-frontend.sh`: `DISPLAY` is
  unset and Xvfb is not installed.
- `config bootstrap failed` from `run-frontend.sh`: Chimera did not start.
  Read `waterbox/tests/work/bootstrap.log`.
- A hand-built package is stamped `-dirty+local` even with nothing edited. The
  applied patches make the submodule's working tree differ from its pin, and
  `git diff --quiet HEAD` counts that as a change.
- `setup-guest.sh` runs `meson setup` with its error output hidden and, when
  that fails, runs `meson setup --reconfigure`. The error you see comes from
  the second command.
- `packaging is not deterministic` from `build-package.sh`: the package was
  written twice and the two files differ. The package's SHA-1 is the core's
  identity, so the script stops.
