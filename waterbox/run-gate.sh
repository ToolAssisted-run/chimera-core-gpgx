#!/bin/bash
# The core-level equivalence gate: for every test the sandboxed core must
# produce byte-identical video, audio, lag and memory-domain digests to the
# native reference build (the same cinterface.c compiled natively), and must
# survive a whole-machine savestate round-trip around every frame.
#
# The test base is quickerGPGX's: real playaround movies over the five
# homebrew roms that are free to distribute (three Genesis, one Master
# System, one SG-1000 - the 8-bit paths are proven from day one). The Sega
# Pico runs a cartridge the test writes (tests/make-pico.py).
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
# What a turbo run can be held to: everything except the whole-run video hash,
# which a run that skipped the first half cannot possibly match - the second
# half it did draw is compared instead.
turboDigests() { grep -E '^(frames|vsync|tailVideoHash|audioHash|lagFrames|domain\[)'; }

ok=0
failed=0
report() { printf "%-30s %-6s %s\n" "$1" "$2" "$3"; case "$2" in PASS) ok=$((ok+1)) ;; *) failed=$((failed+1)) ;; esac; }
printf "%-30s %-6s %s\n" "Check" "Result" "Detail"
printf "%-30s %-6s %s\n" "-----" "------" "------"

# name rom sys ctl1 ctl2 wire movie(- = pad exercise)
# the wire is the PACKAGE's controller (one core.wbx, four packages)
tests=(
	"dinoRunner Dino-Runner.bin genesis gamepad3b none genesis dinoRunner.playaround.sol"
	"avuado avuado-1st-version.bin genesis gamepad3b none genesis avuado.playaround.sol"
	"drunkasilike drunkasilike.bin genesis gamepad3b gamepad3b genesis drunkasilike.playaround.sol"
	"maiNurse mai_nurse_v1.00.sms sms gamepad2b none 8bit maiNurse.playaround.sol"
	"pitman Pitman.sg sg1000 gamegear2b none 8bit pitman.playaround.sol"
	"exercise Dino-Runner.bin genesis gamepad3b none genesis -"
)

for t in "${tests[@]}"; do
	read -r name rom sys ctl1 ctl2 wire movie <<< "$t"

	wd="$work/$name"
	mkdir -p "$wd"
	cp "$root/tests/roms/$rom" "$wd/"
	printf '{"cart":["%s"]}' "$rom" > "$wd/slots"

	args=(--sys "$sys" --ctl1 "$ctl1" --ctl2 "$ctl2" --wire "$wire")
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
			--wire "$wire" --frames "$frames" 2>/dev/null | digests > "$work/idle.txt"
		if cmp -s "$work/box.txt" "$work/idle.txt"; then
			report "$name:input-shaped" FAIL "the pad exercise changed nothing"
		else
			report "$name:input-shaped" PASS "input visibly shaped the machine"
		fi
	fi

	# Turbo: the VDP's output step switched off for the first half of the run and
	# back on for the second. Everything the 68000 can see - including the sprite
	# overflow and collision bits, which are decided before that step - must be
	# what it would have been, and so must every picture of that second half.
	"$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" 2>/dev/null | turboDigests > "$work/tnorm.txt"
	if "$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" --turbo 2>/dev/null | turboDigests > "$work/turbo.txt"; then
		if cmp -s "$work/tnorm.txt" "$work/turbo.txt"; then
			report "$name:turbo" PASS "$frames frames, half of them undrawn, same machine and same pictures"
		else
			report "$name:turbo" FAIL "$(diff "$work/tnorm.txt" "$work/turbo.txt" | tr '\n' ' ' | head -c 120)"
		fi
	else
		report "$name:turbo" FAIL "turbo runner error"
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

# ---- Sega CD smoke legs: a synthetic cue/bin disc set plus a deterministic
# dummy BIOS (load_rom refuses a CD without one) drive the whole in-guest
# disc pipeline - upstream's cue parser, the track FILE handles the guest
# keeps open, their survival across savestates, and hot swapping. Real-game
# CD legs need the user's own BIOS and images (see tests/run-roms.sh).
wd="$work/cdsmoke"
mkdir -p "$wd"
python3 "$here/tests/gen-fakecd.py" "$wd" fake 12345 >/dev/null
python3 "$here/tests/gen-fakecd.py" "$wd" fake2 99999 >/dev/null
python3 "$here/tests/gen-fakecd.py" --bios "$wd/cdBiosUS" >/dev/null
printf '{"cd":["fake.cue","fake2.cue"]}' > "$wd/slots"
# a movie that opens with nothing and presses Next Disk at frame 100
python3 - "$wd/swap.sol" <<'PYSOL'
import sys
# |PR<>|UDLRABCS| - Next Disk held on frames 100-101 (one rising edge)
lines = ["|.." + (".>" if f in (100, 101) else "..") + "|........|" for f in range(300)]
open(sys.argv[1], "w").write("\n".join(lines) + "\n")
PYSOL
cdargs=(--sys segacd --ctl1 gamepad3b --sol "$wd/swap.sol")
if ! "$nat/run-native" "$wd" "${cdargs[@]}" 2>"$work/cd.err" | digests > "$work/cdnat.txt"; then
	report "cdsmoke:equivalence" FAIL "native runner error: $(head -1 "$work/cd.err")"
elif ! "$nat/run-wbx" "$gst/core.wbx" "$wd" "${cdargs[@]}" 2>"$work/cd.err" | digests > "$work/cdbox.txt"; then
	report "cdsmoke:equivalence" FAIL "waterbox runner error: $(head -1 "$work/cd.err")"
elif ! cmp -s "$work/cdnat.txt" "$work/cdbox.txt"; then
	report "cdsmoke:equivalence" FAIL "$(diff "$work/cdnat.txt" "$work/cdbox.txt" | tr '\n' ' ' | head -c 120)"
elif ! grep -q '^domain\[CD PRG RAM\]' "$work/cdbox.txt"; then
	report "cdsmoke:equivalence" FAIL "the machine is not a Sega CD"
else
	report "cdsmoke:equivalence" PASS "cue+bin discs, disc swap at frame 100, native == waterboxed"
	if ! "$nat/run-wbx" "$gst/core.wbx" "$wd" "${cdargs[@]}" --rerecord 2>/dev/null | digests > "$work/cdrr.txt"; then
		report "cdsmoke:savestate" FAIL "rerecord runner error"
	elif cmp -s "$work/cdbox.txt" "$work/cdrr.txt"; then
		report "cdsmoke:savestate" PASS "open disc files survive per-frame round-trips"
	else
		report "cdsmoke:savestate" FAIL "$(diff "$work/cdbox.txt" "$work/cdrr.txt" | tr '\n' ' ' | head -c 120)"
	fi
fi

# ---- savedata export: the persistent-data channel's files must be identical
# from both flavors, for the cart's battery SRAM and for the Sega CD's
# backup rams (the cdsmoke work dir is still around)
wd="$work/dinoRunner"
mkdir -p "$work/sd.nat" "$work/sd.box" "$work/sd.cd.nat" "$work/sd.cd.box"
"$nat/run-native" "$wd" --frames 120 --exercise --savedata-out "$work/sd.nat" >/dev/null 2>&1
"$nat/run-wbx" "$gst/core.wbx" "$wd" --frames 120 --exercise --savedata-out "$work/sd.box" >/dev/null 2>&1
"$nat/run-native" "$work/cdsmoke" --sys segacd --frames 120 --savedata-out "$work/sd.cd.nat" >/dev/null 2>&1
"$nat/run-wbx" "$gst/core.wbx" "$work/cdsmoke" --sys segacd --frames 120 --savedata-out "$work/sd.cd.box" >/dev/null 2>&1
nc="$(find "$work/sd.nat" "$work/sd.cd.nat" -type f | wc -l)"
if [ "$nc" -eq 0 ]; then
	report "savedata:export" FAIL "the native runs exported no files"
elif diff -r "$work/sd.nat" "$work/sd.box" >/dev/null 2>&1 \
	&& diff -r "$work/sd.cd.nat" "$work/sd.cd.box" >/dev/null 2>&1; then
	report "savedata:export" PASS "$nc files (cart SRAM + CD backup rams), native == sandbox"
else
	report "savedata:export" FAIL "export trees differ"
fi

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

# ---- Sega Pico: a cartridge the test writes (tests/make-pico.py; the Pico
# has no BIOS). Its program copies the Pico's IO, pen and page registers to
# RAM once a frame, so the 68K RAM after a run says what the controls did.
wd="$work/pico"
mkdir -p "$wd"
python3 "$root/tests/make-pico.py" "$wd/pico.bin" > /dev/null
printf '{"cart_pico":["pico.bin"]}' > "$wd/slots"
printf '{"systemHardware":"pico"}' > "$wd/settings"
pargs=(--sys genesis --ctl1 none --ctl2 none --wire pico)
"$nat/run-native" "$wd" "${pargs[@]}" --frames 600 --exercise 2>"$work/pico.err" | digests > "$work/piconat.txt"
"$nat/run-wbx" "$gst/core.wbx" "$wd" "${pargs[@]}" --frames 600 --exercise 2>>"$work/pico.err" | digests > "$work/picobox.txt"
"$nat/run-wbx" "$gst/core.wbx" "$wd" "${pargs[@]}" --frames 600 2>/dev/null | digests > "$work/picoidle.txt"
"$nat/run-wbx" "$gst/core.wbx" "$wd" "${pargs[@]}" --frames 600 --exercise --rerecord 2>/dev/null | digests > "$work/picorr.txt"
if [ ! -s "$work/picobox.txt" ] || ! cmp -s "$work/piconat.txt" "$work/picobox.txt"; then
	report "pico:equivalence" FAIL "$(head -1 "$work/pico.err") $(diff "$work/piconat.txt" "$work/picobox.txt" | tr '\n' ' ' | head -c 100)"
else
	report "pico:equivalence" PASS "600 frames of the pad, the buttons, the pages and the pen, native == waterboxed"
fi
if [ -s "$work/picoidle.txt" ] && ! cmp -s "$work/picobox.txt" "$work/picoidle.txt"; then
	report "pico:input-shaped" PASS "the exercise changed the machine"
else
	report "pico:input-shaped" FAIL "the exercise changed nothing"
fi
if [ -s "$work/picorr.txt" ] && cmp -s "$work/picobox.txt" "$work/picorr.txt"; then
	report "pico:savestate" PASS "per-frame round-trip is lossless"
else
	report "pico:savestate" FAIL "$(diff "$work/picobox.txt" "$work/picorr.txt" | tr '\n' ' ' | head -c 120)"
fi
# what the program read: IO, pen X, pen Y, page - after a 120-frame run (the
# 68K RAM is held with the bytes of each word swapped)
pico_regs() {
	"$nat/run-wbx" "$gst/core.wbx" "$wd" "${pargs[@]}" --frames 120 --dump-domain "68K RAM" "$work/pico.ram" "$@" > /dev/null 2>&1
	python3 -c "
import sys
d = open(sys.argv[1], 'rb').read()
b = lambda a: d[a ^ 1]
print('%02x %d %d %02x' % (b(2), b(4) << 8 | b(5), b(6) << 8 | b(7), b(8)))" "$work/pico.ram" 2> /dev/null
}
# each control, held from frame 100 to the end, against the IO register's
# bits (0 while held): up 0, down 1, left 2, right 3, red 4, pen 7
got=""
for i in 2 3 4 5 6 7; do got="$got $(pico_regs --press "$i:100:40" | cut -d' ' -f1)"; done
idle="$(pico_regs)"
if [ "$got" = " fe fd fb f7 ef 7f" ] && [ "$idle" = "ff 220 633 00" ]; then
	report "pico:controls" PASS "each button its own bit of the IO register; the pen at rest mid-pad (220, 633)"
else
	report "pico:controls" FAIL "IO:$got, at rest: $idle"
fi
corner0="$(pico_regs --axis 0:0 --axis 1:0)"
corner1="$(pico_regs --axis 0:65535 --axis 1:65535)"
pages="$(pico_regs --press 9:10:2 --press 9:20:2 --press 9:30:2 --press 8:40:2 | cut -d' ' -f4)"
under="$(pico_regs --press 8:20:2 | cut -d' ' -f4)"
over="$(pico_regs $(for f in 10 20 30 40 50 60 70 80 90; do printf -- '--press 9:%s:2 ' "$f"; done) | cut -d' ' -f4)"
if [ "$corner0" = "ff 60 508 00" ] && [ "$corner1" = "ff 380 759 00" ] && [ "$pages" = 03 ] && [ "$under" = 00 ] && [ "$over" = 7f ]; then
	report "pico:pen-and-pages" PASS "the pen spans the pad (60..380, 508..759); three pages on and one back is page 2; the book stops at the cover and at page 7"
else
	report "pico:pen-and-pages" FAIL "corners '$corner0' '$corner1', pages '$pages' '$under' '$over'"
fi
python3 "$root/tests/make-pico.py" "$wd/nopad.bin" nopad > /dev/null
mkdir -p "$work/piconopad"; cp "$wd/nopad.bin" "$wd/settings" "$work/piconopad/"
printf '{"cart_pico":["nopad.bin"]}' > "$work/piconopad/slots"
lagpad="$(sed -n 's/^lagFrames=//p' "$work/picobox.txt")"
lagnone="$("$nat/run-wbx" "$gst/core.wbx" "$work/piconopad" "${pargs[@]}" --frames 600 2>/dev/null | sed -n 's/^lagFrames=//p')"
if [ "${lagpad:-x}" -le 2 ] 2> /dev/null && [ "${lagnone:-x}" = 600 ]; then
	report "pico:lag" PASS "$lagpad of 600 reading the controls, $lagnone of 600 reading none"
else
	report "pico:lag" FAIL "$lagpad of 600 reading the controls, $lagnone of 600 reading none"
fi
# the machine and the cartridge must agree
mkdir -p "$work/picomix1" "$work/picomix2"
cp "$root/tests/roms/Dino-Runner.bin" "$work/picomix1/"; printf '{"cart_pico":["Dino-Runner.bin"]}' > "$work/picomix1/slots"; printf '{"systemHardware":"pico"}' > "$work/picomix1/settings"
cp "$wd/pico.bin" "$work/picomix2/"; printf '{"cart_md":["pico.bin"]}' > "$work/picomix2/slots"; printf '{"systemHardware":"genesis"}' > "$work/picomix2/settings"
e1="$("$nat/run-wbx" "$gst/core.wbx" "$work/picomix1" "${pargs[@]}" --frames 1 2>&1 >/dev/null)"
e2="$("$nat/run-wbx" "$gst/core.wbx" "$work/picomix2" --frames 1 2>&1 >/dev/null)"
if echo "$e1" | grep -q "is not a Sega Pico cartridge" && echo "$e2" | grep -q "is a Sega Pico cartridge"; then
	report "pico:cartridge-check" PASS "a Mega Drive cartridge on the Pico, and a Pico one on the Mega Drive, are refused"
else
	report "pico:cartridge-check" FAIL "'$(echo "$e1" | tail -1)' / '$(echo "$e2" | tail -1)'"
fi

# ------------------------------------------------- Chimera's engine
# Each read of the controls is reported to Chimera as it happens, so a Lua
# script's event.oninputpoll runs there (chimera#344): every frame the game
# read them has at least one, and counting them moves nothing. Needs
# CHIMERA_ROOT, a Chimera checkout with this core's package installed in it.
runner="${CHIMERA_ROOT:-}/build/dll/chimera-run"
package="${CHIMERA_ROOT:-}/build/Cores/gpgx.chimeraCore"
if [ -n "${CHIMERA_ROOT:-}" ] && [ -x "$runner" ] && [ -f "$package" ]; then
	printf '[Input]\nLogKey:#\n' > "$work/none.txt"
	"$runner" "$package" "$root/tests/roms/Dino-Runner.bin" "$work/none.txt" --frames 300 --record "$work/idle.txt" > "$work/idle.log" 2>&1
	"$runner" "$package" "$root/tests/roms/Dino-Runner.bin" "$work/idle.txt" \
		--dump "68K RAM=$work/plain.ram" > "$work/plain.log" 2>&1
	"$runner" "$package" "$root/tests/roms/Dino-Runner.bin" "$work/idle.txt" --count-input-polls \
		--dump "68K RAM=$work/polls.ram" --meta "$work/polls.meta" > "$work/polls.log" 2>&1
	polls=$(sed -n 's/^inputPolls=//p' "$work/polls.meta" 2> /dev/null)
	lags=$(sed -n 's/^lagFrames=//p' "$work/polls.meta" 2> /dev/null)
	frames=$(sed -n 's/^frames=//p' "$work/polls.meta" 2> /dev/null)
	if [ "${polls:-none}" != none ] && [ "${frames:-0}" = 300 ] && [ "${lags:-300}" -lt 300 ] \
		&& [ "${polls:-0}" -ge $(( 300 - ${lags:-300} )) ] && [ -s "$work/plain.ram" ] && cmp -s "$work/plain.ram" "$work/polls.ram"; then
		report "engine:input-polls" PASS "the game's reads of its pad reach Chimera as they happen: $polls reads in 300 frames, $lags of them lag frames; 68K RAM is the run's without counting"
	else
		report "engine:input-polls" FAIL "inputPolls=${polls:-missing} frames=${frames:-?} lagFrames=${lags:-?}, or counting moved 68K RAM; the runner said: $(tail -1 "$work/polls.log" 2> /dev/null)"
	fi
else
	printf "%-30s %-6s %s\n" "engine:input-polls" SKIP "needs CHIMERA_ROOT with build/dll/chimera-run and build/Cores/gpgx.chimeraCore"
fi

echo ""
echo "$ok ok, $failed failed"
[ "$failed" -gt 0 ] && exit 1
exit 0
