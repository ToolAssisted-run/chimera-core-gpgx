#!/bin/bash
# The core-level equivalence gate: for every test the sandboxed core must
# produce byte-identical video, audio, lag and memory-domain digests to the
# native reference build (the same cinterface.c compiled natively), and must
# survive a whole-machine savestate round-trip around every frame.
#
# The test base is quickerGPGX's: real playaround movies over the five
# homebrew roms that are free to distribute (three Genesis, one Master
# System, one SG-1000 - the 8-bit paths are proven from day one).
#
# Usage: ./run-gate.sh [-n <native build dir>] [-g <guest build dir>]
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
nat="$root/build/meson-native"
gst="$root/build/meson-guest"
while getopts "n:g:" opt; do
	case "$opt" in
		n) nat="$OPTARG" ;;
		g) gst="$OPTARG" ;;
		*) exit 2 ;;
	esac
done

[ -x "$nat/run-native" ] && [ -x "$nat/run-wbx" ] || {
	echo "native build missing: meson setup build/meson-native && ninja -C build/meson-native" >&2; exit 1; }
[ -f "$gst/core.wbx" ] || {
	echo "guest build missing: sh waterbox/setup-guest.sh && ninja -C build/meson-guest" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
digests() { grep -E '^(frames|vsync|videoHash|audioHash|lagFrames|domain\[)'; }

ok=0
failed=0
report() { printf "%-30s %-6s %s\n" "$1" "$2" "$3"; case "$2" in PASS) ok=$((ok+1)) ;; *) failed=$((failed+1)) ;; esac; }
printf "%-30s %-6s %s\n" "Check" "Result" "Detail"
printf "%-30s %-6s %s\n" "-----" "------" "------"

# name rom sys ctl1 ctl2 movie(- = pad exercise)
tests=(
	"dinoRunner Dino-Runner.bin genesis gamepad3b none dinoRunner.playaround.sol"
	"avuado avuado-1st-version.bin genesis gamepad3b none avuado.playaround.sol"
	"drunkasilike drunkasilike.bin genesis gamepad3b gamepad3b drunkasilike.playaround.sol"
	"maiNurse mai_nurse_v1.00.sms sms gamepad2b none maiNurse.playaround.sol"
	"pitman Pitman.sg sg1000 gamegear2b none pitman.playaround.sol"
	"exercise Dino-Runner.bin genesis gamepad3b none -"
)

for t in "${tests[@]}"; do
	read -r name rom sys ctl1 ctl2 movie <<< "$t"

	wd="$work/$name"
	mkdir -p "$wd"
	cp "$root/tests/roms/$rom" "$wd/"
	printf '{"cart":["%s"]}' "$rom" > "$wd/slots"

	args=(--sys "$sys" --ctl1 "$ctl1" --ctl2 "$ctl2")
	if [ "$movie" = "-" ]; then
		args+=(--frames 600 --exercise)
	else
		args+=(--sol "$root/tests/movies/$movie")
	fi

	if ! "$nat/run-native" "$wd" "${args[@]}" 2>"$work/nat.err" | digests > "$work/nat.txt"; then
		report "$name:equivalence" FAIL "native runner error: $(head -1 "$work/nat.err")"; continue
	fi
	if ! "$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" 2>"$work/box.err" | digests > "$work/box.txt"; then
		report "$name:equivalence" FAIL "waterbox runner error: $(head -1 "$work/box.err")"; continue
	fi
	frames="$(sed -n 's/^frames=//p' "$work/box.txt")"
	if cmp -s "$work/nat.txt" "$work/box.txt"; then
		report "$name:equivalence" PASS "$frames frames, native == waterboxed"
	else
		report "$name:equivalence" FAIL "$(diff "$work/nat.txt" "$work/box.txt" | tr '\n' ' ' | head -c 120)"
		continue
	fi

	# a hollow pass cannot sneak through: the input schedule must have shaped
	# the machine - an idle run of the same length must differ
	if [ "$movie" = "-" ]; then
		"$nat/run-wbx" "$gst/core.wbx" "$wd" --sys "$sys" --ctl1 "$ctl1" --ctl2 "$ctl2" \
			--frames "$frames" 2>/dev/null | digests > "$work/idle.txt"
		if cmp -s "$work/box.txt" "$work/idle.txt"; then
			report "$name:input-shaped" FAIL "the pad exercise changed nothing"
		else
			report "$name:input-shaped" PASS "input visibly shaped the machine"
		fi
	fi

	if ! "$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" --rerecord 2>/dev/null | digests > "$work/rr.txt"; then
		report "$name:savestate" FAIL "rerecord runner error"; continue
	fi
	if cmp -s "$work/box.txt" "$work/rr.txt"; then
		report "$name:savestate" PASS "per-frame round-trip is lossless"
	else
		report "$name:savestate" FAIL "$(diff "$work/box.txt" "$work/rr.txt" | tr '\n' ' ' | head -c 120)"
	fi
done

# ---- settings leg: a declared setting must reach the guest and shape the
# machine identically in both flavors. forceVDP=pal flips the vertical rate
# (the vsync= line) and the whole timing of the run.
wd="$work/palset"
mkdir -p "$wd"
cp "$root/tests/roms/Dino-Runner.bin" "$wd/"
printf '{"cart":["Dino-Runner.bin"]}' > "$wd/slots"
printf '{"forceVDP":"pal"}' > "$wd/settings"
args=(--frames 300 --exercise)
"$nat/run-native" "$wd" "${args[@]}" 2>/dev/null | digests > "$work/palnat.txt"
"$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" 2>/dev/null | digests > "$work/palbox.txt"
if ! cmp -s "$work/palnat.txt" "$work/palbox.txt"; then
	report "settings:forceVDP" FAIL "$(diff "$work/palnat.txt" "$work/palbox.txt" | tr '\n' ' ' | head -c 120)"
elif ! grep -q '^vsync=53203424/1070460$' "$work/palbox.txt"; then
	report "settings:forceVDP" FAIL "vsync did not flip to PAL: $(grep '^vsync=' "$work/palbox.txt")"
else
	rm "$wd/settings"
	"$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" 2>/dev/null | digests > "$work/ntsc.txt"
	if cmp -s "$work/palbox.txt" "$work/ntsc.txt"; then
		report "settings:forceVDP" FAIL "pal and ntsc runs are identical"
	else
		report "settings:forceVDP" PASS "forceVDP=pal reached the guest, vsync + digests flipped"
	fi
fi

echo ""
echo "$ok ok, $failed failed"
[ "$failed" -gt 0 ] && exit 1
exit 0
