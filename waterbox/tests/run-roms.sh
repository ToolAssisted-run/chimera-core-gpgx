#!/bin/bash
# Replays the full quickerGPGX movie set (tests/movies/manifest.json) over
# whatever licensed images sit in tests/roms-local/ - commercial roms are
# never in this repo, so every leg SKIPs cleanly until you drop the files in.
# Sega CD entries also need the matching BIOS dumps in tests/firmware-local/
# (cdBiosUS / cdBiosEU / cdBiosJP - the id is the file name).
#
# Every present game must be native == sandbox == per-frame savestate
# round-trip on all digests, over its whole movie. The savestart entries are
# skipped: their .state files are quickerGPGX's own format.
#
# Usage: ./run-roms.sh [-f <extra frames past the movie>]
set -u

here="$(cd "$(dirname "$0")" && pwd)"
wb="$(cd "$here/.." && pwd)"
root="$(cd "$wb/.." && pwd)"
nat="$root/build/meson-native"
gst="$root/build/meson-guest"

[ -x "$nat/run-native" ] && [ -x "$nat/run-wbx" ] && [ -f "$gst/core.wbx" ] || {
	echo "build both flavors first (see README)" >&2; exit 1; }

romdir="$root/tests/roms-local"
fwdir="$root/tests/firmware-local"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
digests() { grep -E '^(frames|vsync|videoHash|audioHash|lagFrames|domain\[)'; }

ok=0
failed=0
skipped=0
report() {
	printf "%-34s %-6s %s\n" "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok+1)) ;; SKIP) skipped=$((skipped+1)) ;; *) failed=$((failed+1)) ;; esac
}
printf "%-34s %-6s %s\n" "Check" "Result" "Detail"
printf "%-34s %-6s %s\n" "-----" "------" "------"

count="$(python3 -c "import json;print(len(json.load(open('$root/tests/movies/manifest.json'))))")"
for i in $(seq 0 $((count - 1))); do
	eval "$(python3 - "$root/tests/movies/manifest.json" "$i" <<'PYENT'
import json, shlex, sys
m = json.load(open(sys.argv[1]))[int(sys.argv[2])]
for k, v in m.items():
    print(f"{k}={shlex.quote(str(v))}")
PYENT
)"
	[ -n "$initialState" ] && { report "$name" SKIP "savestart states are quickerGPGX-format"; continue; }

	src="$(find "$root/tests/roms" "$romdir" -name "$rom" -type f 2>/dev/null | head -1)"
	[ -n "$src" ] || { report "$name" SKIP "drop '$rom' into tests/roms-local/"; continue; }

	wd="$work/$name"
	mkdir -p "$wd"
	if [ "${rom##*.}" = "cue" ]; then
		# a cue's track files are its siblings; bring the whole directory
		cp "$(dirname "$src")"/* "$wd/" 2>/dev/null
	else
		cp "$src" "$wd/"
	fi

	slot="cart"
	case "$sys" in segacd) slot="cd" ;; esac
	printf '{"%s":["%s"]}' "$slot" "$rom" > "$wd/slots"

	# the machine the movie was made for: the declared port devices
	python3 - "$wd/settings" "$ctl1" "$ctl2" <<'PYSET'
import json, sys
def port(c):
    return "none" if c == "none" else "gamepad"
s = {"port1": port(sys.argv[2]), "port2": port(sys.argv[3])}
if "6b" in sys.argv[2] or "6b" in sys.argv[3]:
    s["useSixButton"] = True
json.dump(s, open(sys.argv[1], "w"))
PYSET

	if [ "$slot" = "cd" ]; then
		found=0
		for b in cdBiosUS cdBiosEU cdBiosJP; do
			[ -f "$fwdir/$b" ] && { cp "$fwdir/$b" "$wd/"; found=1; }
		done
		[ "$found" = 1 ] || { report "$name" SKIP "needs a CD BIOS in tests/firmware-local/"; continue; }
	fi

	shaGot="$(sha1sum "$src" | awk '{print toupper($1)}')"
	shaNote=""
	[ -n "$sha1" ] && [ "$shaGot" != "$sha1" ] && shaNote=" (rom sha1 differs from the movie's!)"

	args=(--sys "$sys" --ctl1 "$ctl1" --ctl2 "$ctl2" --sol "$root/tests/movies/$sol")
	if ! "$nat/run-native" "$wd" "${args[@]}" 2>"$work/err" | digests > "$work/nat.txt"; then
		report "$name" FAIL "native runner error: $(head -1 "$work/err")"; continue
	fi
	if ! "$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" 2>"$work/err" | digests > "$work/box.txt"; then
		report "$name" FAIL "waterbox runner error: $(head -1 "$work/err")"; continue
	fi
	frames="$(sed -n 's/^frames=//p' "$work/box.txt")"
	if ! cmp -s "$work/nat.txt" "$work/box.txt"; then
		report "$name" FAIL "$(diff "$work/nat.txt" "$work/box.txt" | tr '\n' ' ' | head -c 110)"
		continue
	fi
	if ! "$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" --rerecord 2>/dev/null | digests > "$work/rr.txt"; then
		report "$name" FAIL "rerecord runner error"
	elif cmp -s "$work/box.txt" "$work/rr.txt"; then
		report "$name" PASS "$frames frames, native == sandbox == rerecord$shaNote"
	else
		report "$name" FAIL "rerecord differs$shaNote"
	fi
done

echo ""
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -gt 0 ] && exit 1
exit 0
