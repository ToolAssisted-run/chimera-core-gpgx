#!/bin/sh
# Ensures patches/ are applied to the extern/Genesis-Plus-GX submodule tree.
# The submodule pin is PRISTINE upstream; the ~40-line chimera patch set lives
# in patches/ and is applied into the working tree here (idempotent - run at
# every meson configure).
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
sub="$root/extern/Genesis-Plus-GX"

marker="cinterface_force_sram"
if grep -q "$marker" "$sub/core/cart_hw/sram.c" 2>/dev/null; then
	exit 0
fi

for p in "$root"/patches/*.patch; do
	git -C "$sub" apply "$p"
	echo "applied $(basename "$p")"
done
