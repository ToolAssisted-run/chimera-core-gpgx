#!/usr/bin/env python3
"""Writes a Sega Pico cartridge for the core's tests. It needs no file of
anyone's: the Pico has no BIOS.

usage: make-pico.py <out.bin> [nopad]

"SEGA PICO" in the header at &100 is what makes Genesis Plus GX a Pico. The
program, hand-assembled below (68000 machine code), turns the display on
and, for ever, once a frame (it waits for the vertical blank to end):
  - counts the frames in the word at &FF0000;
  - copies the Pico's registers to RAM: IO (&800003: the D-pad, the red
    button, the pen button, 0 while held) to &FF0002, the pen's X (&800005,
    &800007) to &FF0004, its Y (&800009, &80000B) to &FF0006, the page
    (&80000D) to &FF0008;
  - gives the background colour (CRAM entry 0) the frame count.
The long at &FF0030 holds "PICO" once the program runs. Made with "nopad" it
reads none of the registers, so every frame is a frame that did not read its
controls. (In the core's memory the 68000's RAM is held a word at a time
with the bytes of each word swapped.)
"""
import struct
import sys


def assemble(nopad):
    rom = bytearray(0x10000)
    # vectors: the stack, the start, and every exception to an RTE
    struct.pack_into(">II", rom, 0, 0x00FFFE00, 0x200)
    for v in range(2, 64):
        struct.pack_into(">I", rom, v * 4, 0x3F0)   # the RTE below
    rom[0x100:0x110] = b"SEGA PICO       "
    rom[0x110:0x120] = b"(C)CHIM 2026.OCT"
    rom[0x120:0x150] = b"CHIMERA PICO TEST".ljust(48)
    rom[0x150:0x180] = b"CHIMERA PICO TEST".ljust(48)
    rom[0x180:0x18E] = b"GM 00000000-00"
    rom[0x1F0:0x1F3] = b"JUE"

    code = bytearray()
    labels = {}

    def w(*words):
        for x in words:
            code.extend(struct.pack(">H", x & 0xFFFF))

    def l(x):
        code.extend(struct.pack(">I", x))

    def label(n):
        labels[n] = 0x200 + len(code)

    def branch(op, target):
        d = labels[target] - (0x200 + len(code) + 2)
        assert -128 <= d < 0, d
        w((op << 8) | (d & 0xFF))

    w(0x46FC, 0x2700)                     # move.w #$2700,sr
    w(0x41F9); l(0x00FF0000)              # lea $ff0000,a0
    w(0x33FC, 0x8004); l(0x00C00004)      # VDP register 0 = $04
    w(0x33FC, 0x8144); l(0x00C00004)      # VDP register 1 = $44: display on, mode 5
    w(0x217C); l(0x5049434F); w(0x0030)   # move.l #'PICO',$30(a0)
    label("loop")
    # the end of the vertical blank: wait while it is on, then until it starts
    # and ends again (the VB flag is bit 3 of the VDP status)
    label("w1")
    w(0x3039); l(0x00C00004); w(0x0800, 0x0003)   # move.w $c00004,d0 / btst #3,d0
    branch(0x67, "w1")                            # beq.s w1: until it is on
    label("w2")
    w(0x3039); l(0x00C00004); w(0x0800, 0x0003)
    branch(0x66, "w2")                            # bne.s w2: until it is off
    w(0x5250)                                     # addq.w #1,(a0)
    if not nopad:
        for reg, at in ((0x03, 0x02), (0x05, 0x04), (0x07, 0x05), (0x09, 0x06), (0x0B, 0x07), (0x0D, 0x08)):
            w(0x1179); l(0x00800000 | reg); w(at)  # move.b $8000xx,at(a0)
    w(0x23FC); l(0xC0000000); l(0x00C00004)       # move.l #$c0000000,$c00004: CRAM 0
    w(0x33D0); l(0x00C00000)                      # move.w (a0),$c00000: the count
    w(0x6000); w((labels["loop"] - (0x200 + len(code))) & 0xFFFF)   # bra.w loop

    assert 0x200 + len(code) <= 0x3F0
    rom[0x200:0x200 + len(code)] = code
    rom[0x3F0:0x3F2] = b"\x4e\x73"   # rte, for every exception
    return bytes(rom)


def main():
    nopad = len(sys.argv) > 2 and sys.argv[2] == "nopad"
    data = assemble(nopad)
    open(sys.argv[1], "wb").write(data)
    print("%s: %d bytes%s" % (sys.argv[1], len(data), " (no controls read)" if nopad else ""))


if __name__ == "__main__":
    main()
