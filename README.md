# chimera-core-gpgx

**Genesis Plus GX as a Chimera waterbox core** - [ekeeke's Genesis Plus
GX](https://github.com/ekeeke/Genesis-Plus-GX) (Sega Mega Drive/Genesis, Sega
CD, Master System, Game Gear, SG-1000), compiled into
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

Status and plan: `docs/PLAN.md`.

## Build and test

```
# native reference + sandbox drivers
meson setup build/meson-native && ninja -C build/meson-native

# the guest core (needs a built miniBox checkout, e.g. chimera/extern/miniBox)
sh waterbox/setup-guest.sh && ninja -C build/meson-guest

# the equivalence gate: native == sandbox == savestate-rerecord on video,
# audio, lag and every memory domain, over quickerGPGX's homebrew movie set
./waterbox/run-gate.sh
```
