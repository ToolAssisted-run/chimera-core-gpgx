#!/usr/bin/env python3
# Generates a deterministic synthetic Sega CD image (cue + bin) for the gate:
# a MODE1/2352 data track (proper sync headers, "SEGADISCSYSTEM" signature,
# seeded pseudo-random payload) plus an audio track in the same bin. It is not
# a runnable game - the machine boots BIOS-less into deterministic garbage -
# but it drives the whole in-guest disc pipeline: upstream's cue parser, the
# track FILE handles the guest keeps open, and their survival across
# savestates.
#
# Usage: gen-fakecd.py <outdir> <basename> <seed>
#        gen-fakecd.py --bios <outfile>
#
# --bios writes a deterministic dummy 128KB CD boot rom (valid reset vectors
# into a NOP sled ending in a tight loop): load_rom refuses a CD without a
# BIOS, and a dummy one boots a boring machine that is exactly as
# deterministic in both flavors - which is all the smoke legs need. Real-game
# legs use the user's own BIOS files.
import struct
import sys

if sys.argv[1] == "--bios":
    rom = bytearray(131072)
    rom[0:4] = (0x8000).to_bytes(4, "big")        # initial SP
    rom[4:8] = (0x400).to_bytes(4, "big")         # initial PC
    for off in range(0x400, 0x800, 2):
        rom[off:off + 2] = b"\x4e\x71"            # NOP sled...
    rom[0x7fe:0x800] = b"\x60\xfe"                # ...into BRA.S *
    open(sys.argv[2], "wb").write(rom)
    print("dummy bios written")
    sys.exit(0)

outdir, base, seed = sys.argv[1], sys.argv[2], int(sys.argv[3])

SECTOR = 2352
DATA_SECTORS = 150   # the 2s minimum the BIOS would demand
AUDIO_SECTORS = 180

def rng_bytes(n, state):
    out = bytearray()
    x = state
    while len(out) < n:
        x = (x * 6364136223846793005 + 1442695040888963407) & (2**64 - 1)
        out += struct.pack("<Q", x)
    return bytes(out[:n]), x

def msf(lba):
    lba += 150
    return lba // 4500, (lba // 75) % 60, lba % 75

def bcd(v):
    return ((v // 10) << 4) | (v % 10)

state = seed
with open(f"{outdir}/{base}.bin", "wb") as f:
    for s in range(DATA_SECTORS):
        sync = b"\x00" + b"\xff" * 10 + b"\x00"
        m, sec, frac = msf(s)
        hdr = bytes([bcd(m), bcd(sec), bcd(frac), 1])
        payload, state = rng_bytes(2048, state)
        if s == 0:
            payload = b"SEGADISCSYSTEM  " + payload[16:]
        tail, state = rng_bytes(SECTOR - 16 - 2048, state)
        f.write(sync + hdr + payload + tail)
    audio, state = rng_bytes(AUDIO_SECTORS * SECTOR, state)
    f.write(audio)

mm, ss, ff = msf(DATA_SECTORS - 150)  # audio INDEX is file-relative time
start = DATA_SECTORS
with open(f"{outdir}/{base}.cue", "w") as f:
    f.write(f'FILE "{base}.bin" BINARY\n')
    f.write("  TRACK 01 MODE1/2352\n")
    f.write("    INDEX 01 00:00:00\n")
    f.write("  TRACK 02 AUDIO\n")
    f.write(f"    INDEX 01 {start // 4500:02d}:{(start // 75) % 60:02d}:{start % 75:02d}\n")

print(f"{base}: {DATA_SECTORS} data + {AUDIO_SECTORS} audio sectors")
