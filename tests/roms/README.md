# The test roms in this folder

Five homebrew games the tests run, so that the tests can run on a public CI
runner. Each is here as its author published it, for free download. None is
under this repository's licence, and for none of them was a licence found
that says the file may be passed on (looked 2026-10-13): if you are the
author of one and want it removed, say so in an issue and it goes.

| file | what it is | by | where it was published |
| --- | --- | --- | --- |
| `Dino-Runner.bin` | Dino Runner Genesis, a Mega Drive endless runner made with SGDK | Rafagars | itch.io (the page's address was not recorded) |
| `avuado-1st-version.bin` | avuado, a Mega Drive shoot-'em-up (first version) | nemezes | <https://nemezes.itch.io/avuado> |
| `drunkasilike.bin` | drunkasilike, a Mega Drive game | tboddy | <https://github.com/tboddy/drunkasilike> (no licence file) |
| `mai_nurse_v1.00.sms` | Mai Nurse 1.00, a Master System puzzle game | lunoka | <https://lunoka.itch.io/mai-nurse> |
| `Pitman.sg` | Pitman, an SG-1000 puzzle game (the rom says "UNDER4MHZ C 2021") | Under4Mhz | itch.io (the page's address was not recorded) |

Commercial games are never put here: they stay in `tests/roms-local/`, which
git ignores, and their tests are skipped where they are absent.
