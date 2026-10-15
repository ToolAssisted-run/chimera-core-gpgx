#!/bin/sh
# Ensures patches/ are applied to the extern/Genesis-Plus-GX submodule tree.
# The submodule pin is PRISTINE upstream; the chimera patch set lives in
# patches/ and is applied into the working tree here (idempotent - run at
# every meson configure).
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
sub="$root/extern/Genesis-Plus-GX"

# each patch on its own: applied when it is not already (a reverse check
# that succeeds means it is in the tree)
for p in "$root"/patches/*.patch; do
	if git -C "$sub" apply --reverse --check "$p" 2> /dev/null; then
		continue
	fi
	git -C "$sub" apply "$p"
	echo "applied $(basename "$p")"
done
