# chimera-core-gpgx

**Genesis Plus GX as a Chimera waterbox core** - [ekeeke's Genesis Plus
GX](https://github.com/ekeeke/Genesis-Plus-GX) (Sega Mega Drive/Genesis, Sega
CD, Master System, Game Gear, SG-1000, Sega Pico), compiled into
[miniBox](https://github.com/ToolAssisted-run/chimera-common-minibox)'s
deterministic sandbox and packaged as a Chimera core (`core.wbx` +
`waterbox.config`), the same shape as
[chimera-core-quickernes](https://github.com/ToolAssisted-run/chimera-core-quickernes),
[chimera-core-neshawk](https://github.com/ToolAssisted-run/chimera-core-neshawk),
[chimera-core-ppsspp](https://github.com/ToolAssisted-run/chimera-core-ppsspp) and
[chimera-core-dosbox-x](https://github.com/ToolAssisted-run/chimera-core-dosbox-x).

The integration imitates the author's own BizHawk GPGX port
(`src/BizHawk.Emulation.Cores/.../gpgx64` + `waterbox/gpgx`) almost verbatim,
on a current upstream pin, with upstream kept as clean as possible: the whole
patch set is ~40 lines (see `patches/` and `docs/PLAN.md`). There is no
host-side CD plumbing at all - disc images (.cue/.bin/.iso) are mounted raw
into the guest filesystem and upstream's own stdio `cdStream` reads them.

**One core, five machines, one package.** `gpgx` is a Mega Drive / Genesis
(with Sega CD), a Master System, a Game Gear, an SG-1000 and a Sega Pico.
The package declares them as machines and the `systemHardware` setting picks one, so
a project pins the machine and a movie cites it; the controller, the picture,
the file extensions and the settings that make sense follow from the choice -
a Master System project is never offered a Genesis pad's X/Y/Z. The guest
picks its matching wire from the machine it actually booted.

The Sega Pico's controls are its D-pad, its red button, the pen's button
(the pen pressed on the drawing pad), Previous Page and Next Page (the
storybook's pages, from the cover, 0, to page 7), and the pen's place on the
drawing pad as two axes, 0 to 65535 from one edge to the other. Upstream
emulates the Pico without its speech chip (ADPCM), so a Pico game makes no
speech. The storybook itself is printed paper on the real console: the game
is told only which page is open.

Status and plan: `docs/PLAN.md`.

## Using it in Chimera

Chimera ships no cores and downloads none. Download
`gpgx-<version>.chimeraCore` from this repository's
[Releases](https://github.com/ToolAssisted-run/chimera-core-gpgx/releases)
page (`dev` follows main, `nightly-YYYY-MM-DD` builds are dated), or build it,
and put the file in the `cores` folder beside Chimera's program. Tools > Core Manager... lists the cores in that folder. The same file works on Linux and on
Windows. Game files and BIOS are not included: you provide them.

## Build and test

`<chimera>` is a Chimera checkout and `<minibox>` is its built
`extern/chimera-common-minibox` submodule.

```
# native reference + sandbox drivers
meson setup build/meson-native -Dminibox_dir=<minibox> && ninja -C build/meson-native

# the guest core
sh waterbox/setup-guest.sh -m <minibox> && ninja -C build/meson-guest

# the equivalence gate: native == sandbox == savestate-rerecord on video,
# audio, lag and every memory domain, over quickerGPGX's homebrew movie set
./waterbox/run-gate.sh

# the package, written to <chimera>/build/Cores/gpgx.chimeraCore
./waterbox/build-package.sh -m <minibox> -r <chimera>
```

The full instructions, from a fresh clone to the gates, are in
[docs/BUILDING.md](docs/BUILDING.md). An AI coding agent should start with
[AGENTS.md](AGENTS.md).
