# chimera-core-gpgx: Genesis Plus GX in miniBox's sandbox, as a Chimera core.
#
# Native reference build (run-native + run-wbx under the build dir):
#     meson setup build/meson-native && ninja -C build/meson-native
# Guest (core.wbx) build via the cross file waterbox/setup-guest.sh writes:
#     sh waterbox/setup-guest.sh && ninja -C build/meson-guest core.wbx
#
# The source list is the author's BizHawk waterbox/gpgx/Makefile list, plus
# what upstream added since its 956fdba pin (input_hw/smash.c). Both flavors
# compile the SAME cinterface.c; only emulibc.h differs (native-shim/).
project('chimera-core-gpgx', 'c',
  default_options: ['c_std=gnu99', 'optimization=2', 'debug=false', 'warning_level=0'])

gpgx = 'extern/Genesis-Plus-GX'

# miniBox provides the guest kit headers (waterbox_settings/slots + jsmn) for
# BOTH flavors, and the host library for run-wbx.
mb = get_option('minibox_dir')
if mb == ''
  mb_candidate = meson.current_source_dir() / '../chimera/extern/miniBox'
  if import('fs').is_dir(mb_candidate)
    mb = mb_candidate
  else
    error('pass -Dminibox_dir=<miniBox checkout> (or keep one at ../chimera/extern/miniBox)')
  endif
endif

defs = [
  '-DLSB_FIRST',
  '-DUSE_32BPP_RENDERING',
  '-DHOOK_CPU',
  '-DCHIMERA_HOOKS',
  '-DHAVE_YM3438_CORE',
  '-DHAVE_OPLL_CORE',
  '-DINLINE=static __inline__',
  '-DMAXROMSIZE=33554432',
]
quiet = [
  '-fcommon',
  '-fno-strict-aliasing',
  '-Wall',
  '-Werror=pointer-to-int-cast', '-Werror=int-to-pointer-cast',
  '-Werror=implicit-function-declaration',
  '-Wfatal-errors',
  '-fomit-frame-pointer',
]
add_project_arguments(defs + quiet, language: 'c')

inc = include_directories(
  'waterbox',
  'waterbox/util',
  gpgx / 'core',
  gpgx / 'core/cart_hw',
  gpgx / 'core/cart_hw/svp',
  gpgx / 'core/cd_hw',
  gpgx / 'core/debug',
  gpgx / 'core/input_hw',
  gpgx / 'core/m68k',
  gpgx / 'core/ntsc',
  gpgx / 'core/sound',
  gpgx / 'core/sound/minimp3',
  gpgx / 'core/z80',
)

core_sources = files(
  gpgx / 'core/cart_hw/areplay.c',
  gpgx / 'core/cart_hw/eeprom_93c.c',
  gpgx / 'core/cart_hw/eeprom_i2c.c',
  gpgx / 'core/cart_hw/eeprom_spi.c',
  gpgx / 'core/cart_hw/flash_cfi.c',
  gpgx / 'core/cart_hw/ggenie.c',
  gpgx / 'core/cart_hw/md_cart.c',
  gpgx / 'core/cart_hw/megasd.c',
  gpgx / 'core/cart_hw/sms_cart.c',
  gpgx / 'core/cart_hw/sram.c',
  gpgx / 'core/cart_hw/svp/ssp16.c',
  gpgx / 'core/cart_hw/svp/svp.c',
  gpgx / 'core/cart_hw/yx5200.c',
  gpgx / 'core/cd_hw/cd_cart.c',
  gpgx / 'core/cd_hw/cdc.c',
  gpgx / 'core/cd_hw/cdd.c',
  gpgx / 'core/cd_hw/gfx.c',
  gpgx / 'core/cd_hw/pcm.c',
  gpgx / 'core/cd_hw/scd.c',
  gpgx / 'core/debug/cpuhook.c',
  gpgx / 'core/genesis.c',
  gpgx / 'core/input_hw/activator.c',
  gpgx / 'core/input_hw/gamepad.c',
  gpgx / 'core/input_hw/graphic_board.c',
  gpgx / 'core/input_hw/input.c',
  gpgx / 'core/input_hw/lightgun.c',
  gpgx / 'core/input_hw/mouse.c',
  gpgx / 'core/input_hw/paddle.c',
  gpgx / 'core/input_hw/smash.c',
  gpgx / 'core/input_hw/sportspad.c',
  gpgx / 'core/input_hw/terebi_oekaki.c',
  gpgx / 'core/input_hw/teamplayer.c',
  gpgx / 'core/input_hw/xe_1ap.c',
  gpgx / 'core/io_ctrl.c',
  gpgx / 'core/loadrom.c',
  gpgx / 'core/m68k/m68kcpu.c',
  gpgx / 'core/m68k/s68kcpu.c',
  gpgx / 'core/mem68k.c',
  gpgx / 'core/membnk.c',
  gpgx / 'core/memz80.c',
  gpgx / 'core/ntsc/md_ntsc.c',
  gpgx / 'core/ntsc/sms_ntsc.c',
  gpgx / 'core/sound/blip_buf.c',
  gpgx / 'core/sound/eq.c',
  gpgx / 'core/sound/opll.c',
  gpgx / 'core/sound/psg.c',
  gpgx / 'core/sound/sound.c',
  gpgx / 'core/sound/ym2413.c',
  gpgx / 'core/sound/ym2612.c',
  gpgx / 'core/sound/ym3438.c',
  gpgx / 'core/system.c',
  gpgx / 'core/vdp_ctrl.c',
  gpgx / 'core/vdp_render.c',
  gpgx / 'core/z80/z80.c',
  'waterbox/util/scrc32.c',
  'waterbox/cinterface.c',
)

if meson.is_cross_build()
  # ---- the guest: core.wbx --------------------------------------------------
  # miniBox's musl toolchain arrives through the cross file (setup-guest.sh);
  # the link recipe is the guest kit's - --no-relax for the large code model,
  # emulibc from the miniBox build, plain C (no unwinder, no libstdc++).
  mbdir = meson.get_external_property('minibox_dir')
  mbuild = meson.get_external_property('minibox_build')
  sr = meson.get_external_property('guest_sysroot')

  executable('core', core_sources,
    name_suffix: 'wbx',
    include_directories: inc,
    link_args: [
      '-static', '-no-pie',
      '-Wl,--eh-frame-hdr', '-Wl,-O2', '-Wl,--no-relax',
      '-T', mbdir / 'source/guest/linkscript.T',
      mbuild / 'source/guest/emulibc.c.o',
      '-L' + sr / 'lib', '-lgcc', '-lc'])
else
  # ---- the native reference -------------------------------------------------
  # the same cinterface over shimmed emulibc; the guest kit headers
  # (waterbox_settings/slots, jsmn) are plain stdio C and run as-is.
  # miniBox lives outside this tree, so its dirs ride plain -I flags.
  # -lm: glibc's. If the gate ever shows float-table divergence against the
  # musl guest (the PPSSPP lesson), switch the native link to musl's libm.
  m = meson.get_compiler('c').find_library('m', required: false)
  executable('run-native', core_sources + files('waterbox/run-native.c'),
    include_directories: [include_directories('waterbox/native-shim'), inc],
    c_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    dependencies: [m])

  mbhost = mb / 'build/meson-linux/source/host'
  executable('run-wbx', files('waterbox/run-wbx.c'),
    c_args: ['-I' + mb / 'source/host'],
    link_args: [mbhost / 'libminiboxhost.so', '-Wl,-rpath,' + mbhost])
endif
