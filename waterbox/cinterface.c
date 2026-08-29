/* cinterface.c - Genesis Plus GX behind the chimera guest ABI.
 *
 * Descended from the author's BizHawk waterbox/gpgx/cinterface/cinterface.c,
 * reshaped for chimera's generic waterbox adapter: settings arrive through the
 * mounted "settings" JSON (waterbox_settings.h) instead of an InitSettings
 * struct, the rom and disc files arrive as real files in the guest filesystem
 * (the project's slot map names them), and there are no host callbacks at all -
 * lag detection is the InputWasRead export over a flag the patched io_ctrl.c
 * raises.
 *
 * This file compiles IDENTICALLY for the guest (miniBox emulibc) and for the
 * native reference build (native-shim/emulibc.h), which is what makes the
 * equivalence gate a real proof.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include <shared.h>
#include <genesis.h>
#include <md_ntsc.h>
#include <sms_ntsc.h>
#include <eeprom_i2c.h>
#include <vdp_render.h>

/* peek helpers our upstream patch adds (see patches/0001-chimera-hooks.patch) */
extern int eeprom_i2c_get_size(void);
extern int sms_cart_is_codies(void);
extern int sms_cart_bootrom_size(void);

struct config_t config;
int cinterface_force_sram;

/* Turbo. The VDP goes on doing everything the 68000 can see - sprite overflow
 * and collision, the pattern cache, the sprite parse - and stops only at
 * remap_line, which is the step that turns the finished line buffer into
 * pixels. ECL_INVISIBLE because it is the frontend's policy for the moment,
 * not part of the machine: a state saved while fast-forwarding must not put the
 * machine back into it when it is loaded to be looked at. */
ECL_INVISIBLE int cinterface_render_enabled = 1;

static char g_loadError[512];
static int g_inited;

/* The hardware this package IS. gpgx picks its machine from the loaded
 * file's extension, and the author's BizHawk port passed that extension
 * explicitly ("GEN"/"SMS"/".GG"/".SG") rather than trusting a file name -
 * chimera must do the same, because the frontend mounts a plain rom under
 * the fixed name "rom" with no extension at all. Each system's package
 * pins this; "auto" keeps the old sniff-the-name behaviour for the test
 * runners, which mount real names. */
static char g_forceExt[8];
static int g_inputRead;      /* raised by real_input_callback (patched io_ctrl.c) */

/* ---- video: GPGX renders into bitmap.data (1024px pitch); the ABI hands out
 * a packed copy. 1024x512 covers every mode incl. interlace-doubled PAL. ---- */
#define VID_MAX_W 1024
#define VID_MAX_H 512
static uint32_t g_videoOut[VID_MAX_W * VID_MAX_H];
static int g_vwidth = 320, g_vheight = 224;

static int16_t g_soundbuffer[4096];
static int g_nsamples;

static uint8_t *g_tempsram;

/* ---- the wire format: waterbox.config "input.buttons" order ----
 * ONE core.wbx serves four systems, and each system's PACKAGE declares the
 * controller that system actually has (the author's BizHawk controller
 * definitions, shape for shape). The guest picks the matching wire from the
 * machine it booted - system_hw, not a setting that could disagree:
 *
 *   genesis (MD/MCD): 0 Power, 1 Reset, 2 Pause (inert), 3 Previous Disk,
 *     4 Next Disk, then P1..P8 x {Up,Down,Left,Right,A,B,C,Start,X,Y,Z,Mode}
 *     (the quickerGPGX .sol column order; masks are input.h bit values)
 *   sms/sg: 0 Power, 1 Reset, 2 Pause (the CONSOLE button - an INPUT_START
 *     bit on pad 0, which is what raises the Z80's NMI), then P1..P2 x
 *     {Up,Down,Left,Right,Button 1,Button 2}
 *   gg: 0 Power, 1 Reset, then P1 x {Up,Down,Left,Right,Button 1,Button 2,
 *     Start} - the Game Gear's Start sits on the unit but reads as a pad bit
 */
#define WIRE_GENESIS 0
#define WIRE_8BIT 1
#define WIRE_GG 2
static int g_wire = WIRE_GENESIS;

#define BTN_COUNT 101 /* the widest wire (genesis); narrower ones use a prefix */
static uint8_t g_buttons[BTN_COUNT];

static const uint16_t kPadBits[12] = {
	INPUT_UP, INPUT_DOWN, INPUT_LEFT, INPUT_RIGHT,
	INPUT_A, INPUT_B, INPUT_C, INPUT_START,
	INPUT_X, INPUT_Y, INPUT_Z, INPUT_MODE,
};

/* the 8-bit pads: Button 1/Button 2 are the same hardware bits as B/C */
static const uint16_t kPadBits8[7] = {
	INPUT_UP, INPUT_DOWN, INPUT_LEFT, INPUT_RIGHT,
	INPUT_BUTTON1, INPUT_BUTTON2, INPUT_START,
};

/* where the pads start, and how wide they are, on each wire */
static int wire_pad_base(void)
{
	return g_wire == WIRE_GENESIS ? 5 : (g_wire == WIRE_8BIT ? 3 : 2);
}
static int wire_pad_width(void)
{
	return g_wire == WIRE_GENESIS ? 12 : (g_wire == WIRE_8BIT ? 6 : 7);
}

/* dev index -> assigned player (1-based), 0 = no player; built after init by
 * walking input.dev[] exactly like the author's GPGXControlConverter */
static int g_devPlayer[MAX_DEVICES];

/* the cd slot's swap list; the current index and the swap buttons' previous
 * levels are machine state (savestates and movies carry them) */
#define MAX_DISCS 32
static char g_discs[MAX_DISCS][256];
static int g_discCount;
static int g_discIndex;
static uint8_t g_prevDiscBtn[2];

static void update_viewport(void)
{
	g_vwidth = bitmap.viewport.w + (bitmap.viewport.x * 2);
	g_vheight = bitmap.viewport.h + (bitmap.viewport.y * 2);

	if (config.ntsc)
	{
		if (reg[12] & 1)
			g_vwidth = MD_NTSC_OUT_WIDTH(g_vwidth);
		else
			g_vwidth = SMS_NTSC_OUT_WIDTH(g_vwidth);
	}

	if (config.render && interlaced)
	{
		g_vheight = g_vheight * 2;
	}
}

static void refresh_video(void)
{
	if (g_vwidth > VID_MAX_W) g_vwidth = VID_MAX_W;
	if (g_vheight > VID_MAX_H) g_vheight = VID_MAX_H;
	/* Turbo: remap_line produced nothing, so there is nothing to copy and the
	 * buffer keeps the last frame that was drawn. The size is still reported,
	 * because a mode change is the machine's news whether or not anyone looks. */
	if (!cinterface_render_enabled) return;
	const uint32_t *src = (const uint32_t *)bitmap.data;
	int spitch = bitmap.pitch / 4;
	for (int y = 0; y < g_vheight; y++)
	{
		const uint32_t *s = src + (size_t)y * spitch;
		uint32_t *d = g_videoOut + (size_t)y * g_vwidth;
		for (int x = 0; x < g_vwidth; x++)
			d[x] = s[x] | 0xff000000u; /* opaque; GPGX leaves alpha 0 */
	}
}

void osd_input_update(void)
{
}

void real_input_callback(void)
{
	g_inputRead = 1;
}

/* GPGX pulls files through this (rom, bioses, lock-on carts). Files are real
 * in the guest filesystem: the mounted rom/slot files and the firmware
 * channel's mounts. The extension out-param is the file name's last three
 * characters, which is exactly what load_rom's auto-detection reads ("BIN",
 * "SMS", ".GG", ".SG"...). */
int load_archive(const char *filename, unsigned char *buffer, int maxsize, char *extension)
{
	if (extension)
	{
		if (g_forceExt[0] != 0)
		{
			/* the package's hardware wins over any file name */
			memcpy(extension, g_forceExt, 4);
		}
		else
		{
			size_t len = strlen(filename);
			const char *tail = len >= 3 ? filename + len - 3 : filename;
			memcpy(extension, tail, strlen(tail));
			extension[3] = 0;
		}
	}

	FILE *f = fopen(filename, "rb");
	if (!f)
		return 0;
	int size = (int)fread(buffer, 1, (size_t)maxsize, f);
	/* refuse a truncated read: a file larger than maxsize is an error upstream
	 * would have caught by its own size checks */
	int extra = fgetc(f) != EOF;
	fclose(f);
	if (extra)
	{
		fprintf(stderr, "chimera-gpgx: '%s' exceeds the %d byte limit\n", filename, maxsize);
		return 0;
	}
	return size;
}

/* Sega CD backup ram formatting block (BizHawk's) */
static uint8_t brm_format[0x40] =
{
	0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x00,0x00,0x00,0x00,0x40,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x53,0x45,0x47,0x41,0x5f,0x43,0x44,0x5f,0x52,0x4f,0x4d,0x00,0x01,0x00,0x00,0x00,
	0x52,0x41,0x4d,0x5f,0x43,0x41,0x52,0x54,0x52,0x49,0x44,0x47,0x45,0x5f,0x5f,0x5f
};

/* internal: computes sram size (no brams) - BizHawk's saveramsize() */
static int saveramsize(void)
{
	if (!sram.on)
		return 0;

	switch (sram.type)
	{
		case DEFAULT_SRAM: break;            /* plain bus access saveram */
		case EEPROM_I2C: return eeprom_i2c_get_size();
		case EEPROM_SPI: return sizeof(sram.sram);
		case EEPROM_MICROWIRE: return 128;   /* 93c (SMS only) */
		default: return sizeof(sram.sram);   /* incl. FLASH_MEMORY */
	}

	{
		int startaddr = sram.start / 8192;
		int endaddr = sram.end / 8192 + 1;
		return (endaddr - startaddr) * 8192;
	}
}

/* SaveRAM starts EMPTY every boot (chimera's rule: no ambient persistent
 * state; continuing from a save is a project decision, not a side effect) */
static void clear_sram(void)
{
	if (sram.on)
		memset(sram.sram, 0xff, 0x10000);

	if (system_hw == SYSTEM_MCD)
	{
		/* clear and format bram */
		memset(scd.bram, 0, 0x2000);
		brm_format[0x10] = brm_format[0x12] = brm_format[0x14] = brm_format[0x16] = 0x00;
		brm_format[0x11] = brm_format[0x13] = brm_format[0x15] = brm_format[0x17] = (0x2000 / 64) - 3;
		memcpy(scd.bram + 0x2000 - 0x40, brm_format, 0x40);

		if (scd.cartridge.id)
		{
			/* clear and format ebram */
			memset(scd.cartridge.area, 0x00, scd.cartridge.mask + 1);
			brm_format[0x10] = brm_format[0x12] = brm_format[0x14] = brm_format[0x16] = (((scd.cartridge.mask + 1) / 64) - 3) >> 8;
			brm_format[0x11] = brm_format[0x13] = brm_format[0x15] = brm_format[0x17] = (((scd.cartridge.mask + 1) / 64) - 3) & 0xff;
			memcpy(scd.cartridge.area + scd.cartridge.mask + 1 - 0x40, brm_format, 0x40);
		}
	}
}

/* ---- settings: option-string -> core value maps (same catalogue as the
 * author's GPGXSyncSettings, chimera-cased; defaults match BizHawk's) ---- */

static int opt(const char *name, const char *fallback, const char *const *names,
	const int *values, int count)
{
	char buf[64];
	strncpy(buf, fallback, sizeof buf - 1);
	buf[sizeof buf - 1] = 0;
	wbx_setting_str(name, buf, sizeof buf);
	for (int i = 0; i < count; i++)
		if (strcmp(buf, names[i]) == 0) return values[i];
	fprintf(stderr, "chimera-gpgx: unknown %s '%s', using default '%s'\n", name, buf, fallback);
	for (int i = 0; i < count; i++)
		if (strcmp(fallback, names[i]) == 0) return values[i];
	return values[0];
}

static int port_system(const char *name)
{
	static const char *const names[] =
		{ "gamepad", "none", "mouse", "xea1p", "activator", "teamplayer", "wayplay", "paddle" };
	static const int values[] =
		{ SYSTEM_GAMEPAD, NO_SYSTEM, SYSTEM_MOUSE, SYSTEM_XE_1AP, SYSTEM_ACTIVATOR,
		  SYSTEM_TEAMPLAYER, SYSTEM_WAYPLAY, SYSTEM_PADDLE };
	return opt(name, "gamepad", names, values, 8);
}

/* Picks the file to boot: the project slot map's cart or cd entry when
 * present (its real name carries the extension load_rom auto-detects from),
 * else the plain "rom" mount, which detects as a Genesis cartridge - the
 * guest filesystem is read-only, so an anonymous mount cannot be re-dressed
 * with its extension. Test drivers and the frontend both mount real names
 * plus the slot map. Returns NULL when nothing is mounted. */
static const char *boot_file(char *buf, size_t bufsize)
{
	/* one cartridge slot per machine, because each machine takes its own kind
	 * of file and the wizard offers only that kind; exactly one of these is
	 * ever filled, since exactly one machine is chosen */
	static const char *const cart_slots[] = { "cart_md", "cart_sms", "cart_gg", "cart_sg", "cart" };
	for (size_t i = 0; i < sizeof cart_slots / sizeof *cart_slots; i++)
	{
		if (wbx_slot_count(cart_slots[i]) > 0
			&& wbx_slot_name(cart_slots[i], 0, buf, bufsize) != NULL)
			return buf;
	}

	g_discCount = wbx_slot_count("cd");
	if (g_discCount > MAX_DISCS) g_discCount = MAX_DISCS;
	for (int i = 0; i < g_discCount; i++)
		wbx_slot_name("cd", i, g_discs[i], sizeof g_discs[i]);
	if (g_discCount > 0)
	{
		snprintf(buf, bufsize, "%s", g_discs[0]);
		return buf;
	}

	FILE *f = fopen("rom", "rb");
	if (f == NULL)
		return NULL;
	fclose(f);
	snprintf(buf, bufsize, "rom");
	return buf;
}

ECL_EXPORT const char *GetLoadError(void)
{
	return g_loadError;
}

ECL_EXPORT int Init(void)
{
	g_loadError[0] = 0;

	memset(&bitmap, 0, sizeof(bitmap));
	bitmap.width = 1024;
	bitmap.height = 512;
	bitmap.pitch = 1024 * 4;
	bitmap.data = alloc_invisible(2 * 1024 * 1024);
	g_tempsram = alloc_invisible(0x100000 + 0x2000);

	/* ---- the settings channel; every default is BizHawk's ---- */
	cinterface_force_sram = wbx_setting_bool("forceSram", 0);

	/* sound options (fixed exactly as the author's BizHawk cinterface) */
	config.psg_preamp = 150;
	config.fm_preamp = 100;
	config.cdda_volume = 100;
	config.pcm_volume = 100;
	config.hq_fm = 1;
	config.hq_psg = 1;
	config.mono = 0;
	config.ym3438 = 0;

	{
		static const char *const names[] = { "none", "lowPass", "threeBand" };
		static const int values[] = { 0, 1, 2 };
		config.filter = opt("filter", "lowPass", names, values, 3);
	}
	config.lp_range = (uint32_t)wbx_setting_double("lowPassRange", 0x6666);
	config.low_freq = (int16_t)wbx_setting_double("lowFreq", 880);
	config.high_freq = (int16_t)wbx_setting_double("highFreq", 5000);
	config.lg = (int16_t)wbx_setting_double("lowGain", 100);
	config.mg = (int16_t)wbx_setting_double("midGain", 100);
	config.hg = (int16_t)wbx_setting_double("highGain", 100);

	{
		static const char *const names[] = { "disabled", "mame", "nuked" };
		static const int smsChip[] = { 0, 1, 2 };
		switch (opt("smsFmSoundChip", "mame", names, smsChip, 3))
		{
			case 0: config.opll = 0; config.ym2413 = 0; break;
			case 1: config.opll = 0; config.ym2413 = 1; break;
			case 2: config.opll = 1; config.ym2413 = 1; break;
		}
	}
	{
		static const char *const names[] =
			{ "mameYM2612", "mameASICYM3438", "mameEnhancedYM3438", "nukedYM2612", "nukedYM3438" };
		static const int values[] = { 0, 1, 2, 3, 4 };
		switch (opt("genesisFmSoundChip", "mameYM2612", names, values, 5))
		{
			case 0: config.ym2612 = YM2612_DISCRETE; YM2612Config(YM2612_DISCRETE); break;
			case 1: config.ym2612 = YM2612_INTEGRATED; YM2612Config(YM2612_INTEGRATED); break;
			case 2: config.ym2612 = YM2612_ENHANCED; YM2612Config(YM2612_ENHANCED); break;
			case 3: OPN2_SetChipType(ym3438_mode_ym2612); config.ym3438 = 1; break;
			case 4: OPN2_SetChipType(ym3438_mode_readmode); config.ym3438 = 2; break;
		}
	}

	/* system options */
	config.system = 0; /* AUTO */
	{
		static const char *const names[] = { "autodetect", "usa", "europe", "japanNtsc", "japanPal" };
		static const int values[] = { 0, 1, 2, 3, 4 };
		config.region_detect = opt("region", "autodetect", names, values, 5);
	}
	{
		static const char *const names[] = { "disabled", "ntsc", "pal" };
		static const int values[] = { 0, 1, 2 };
		config.vdp_mode = opt("forceVDP", "disabled", names, values, 3);
	}
	config.master_clock = 0; /* AUTO */
	config.force_dtack = 0;
	config.addr_error = 1;
	config.bios = wbx_setting_bool("loadBios", 0) ? 3 : 0;
	config.lock_on = 0;
	config.add_on = 0; /* HW_ADDON_AUTO */
	config.cd_latency = 1;

	/* display options */
	{
		static const char *const names[] = { "none", "vertical", "horizontal", "all" };
		static const int values[] = { 0, 1, 2, 3 };
		config.overscan = opt("overscan", "none", names, values, 4);
	}
	config.gg_extra = wbx_setting_bool("ggExtra", 0);
	config.render = 1; /* double resolution output for interlaced mode 2 */
	config.ntsc = 0;
	config.lcd = 0;
	config.enhanced_vscroll = 0;
	config.enhanced_vscroll_limit = 8;
	config.sprites_always_on_top = wbx_setting_bool("spritesAlwaysOnTop", 0);

	input.system[0] = port_system("port1");
	input.system[1] = port_system("port2");

	int sixButton = wbx_setting_bool("useSixButton", 0);

	{
		/* the extension strings are load_rom's own (loadrom.c compares
		 * "SMS", "GG", "SG" after upper-casing; anything else is a Mega
		 * Drive cart) */
		static const char *const names[] = { "auto", "genesis", "segacd", "sms", "gg", "sg" };
		static const int values[] = { 0, 1, 2, 3, 4, 5 };
		switch (opt("systemHardware", "auto", names, values, 6))
		{
			case 1: memcpy(g_forceExt, "GEN", 4); break;
			/* a Sega CD boots from the disc, and load_rom reads the machine off
			 * the disc's own extension - forcing a cartridge extension here
			 * would make it a Mega Drive holding a cue sheet */
			case 2: g_forceExt[0] = 0; break;
			case 3: memcpy(g_forceExt, "SMS", 4); break;
			case 4: memcpy(g_forceExt, ".GG", 4); break;
			case 5: memcpy(g_forceExt, ".SG", 4); break;
			default: g_forceExt[0] = 0; break;
		}
	}

	char name[512];
	const char *file = boot_file(name, sizeof name);
	if (file == NULL)
	{
		snprintf(g_loadError, sizeof g_loadError, "no cart or cd file is mounted");
		return 0;
	}

	/* pad type follows the loaded system, exactly like BizHawk keyed it off
	 * the rom extension: Genesis carts get 3B/6B pads, 8-bit systems 2B */
	int is8bit = 0;
	{
		char up[4] = { 0, 0, 0, 0 };
		if (g_forceExt[0] != 0)
			memcpy(up, g_forceExt, 4);
		else
		{
			size_t flen = strlen(file);
			const char *fext = flen >= 3 ? file + flen - 3 : file;
			for (int i = 0; i < 3 && fext[i]; i++)
				up[i] = fext[i] >= 'a' && fext[i] <= 'z' ? fext[i] - 32 : fext[i];
		}
		is8bit = memcmp("SMS", up, 3) == 0 || memcmp("GG", up + 1, 2) == 0
			|| memcmp("SG", up + 1, 2) == 0;
	}

	/* A cartridge from the wrong console does not just look wrong - a Mega
	 * Drive machine fed 8-bit code can spin inside a frame forever. Refuse
	 * it here, where the message can name the problem. The Mega Drive
	 * header's "SEGA" at 0x100 is the marker (an 8-bit cart has code
	 * there). Discs and BIOS-only boots have no cart to check. */
	if (g_forceExt[0] != 0 && g_discCount == 0)
	{
		FILE *cf = fopen(file, "rb");
		if (cf != NULL)
		{
			char head[4] = { 0, 0, 0, 0 };
			if (fseek(cf, 0x100, SEEK_SET) == 0)
			{
				if (fread(head, 1, 4, cf) != 4) head[0] = 0;
			}
			fclose(cf);
			int looksMD = memcmp(head, "SEGA", 4) == 0;
			if (looksMD && is8bit)
			{
				snprintf(g_loadError, sizeof g_loadError,
					"'%s' is a Mega Drive cartridge; open it with the Mega Drive core package", file);
				return 0;
			}
			if (!looksMD && !is8bit)
			{
				snprintf(g_loadError, sizeof g_loadError,
					"'%s' is not a Mega Drive cartridge; open it with the package for its console", file);
				return 0;
			}
		}
	}
	for (int i = 0; i < MAX_INPUTS; i++)
		config.input[i].padtype = is8bit ? DEVICE_PAD2B : (sixButton ? DEVICE_PAD6B : DEVICE_PAD3B);

	if (!is8bit && (config.bios & 1))
	{
		/* gpgx won't load the genesis bootrom itself (BizHawk's manual load) */
		if (load_archive(MD_BIOS, boot_rom, sizeof(boot_rom), NULL) != 0)
		{
#ifdef LSB_FIRST
			for (size_t i = 0; i < sizeof(boot_rom); i += 2)
			{
				uint8 temp = boot_rom[i];
				boot_rom[i] = boot_rom[i + 1];
				boot_rom[i + 1] = temp;
			}
#endif
			system_bios |= SYSTEM_MD;
		}
	}

	if (!load_rom((char *)file))
	{
		snprintf(g_loadError, sizeof g_loadError, "GPGX could not load '%s'", file);
		return 0;
	}

	audio_init(44100, 0);
	system_init();
	system_reset();

	update_viewport();
	clear_sram();

	/* the wire this machine speaks (see the block above) */
	if ((system_hw & SYSTEM_PBC) == SYSTEM_MD || system_hw == SYSTEM_MCD)
		g_wire = WIRE_GENESIS;
	else if (system_hw == SYSTEM_GG)
		g_wire = WIRE_GG;
	else
		g_wire = WIRE_8BIT;

	/* the player map: walk the devices GPGX chose, number them like the
	 * author's GPGXControlConverter (sequential player per active device) */
	{
		int player = 1;
		for (int i = 0; i < MAX_DEVICES; i++)
		{
			g_devPlayer[i] = 0;
			switch (input.dev[i])
			{
				case DEVICE_PAD3B:
				case DEVICE_PAD6B:
				case DEVICE_PAD2B:
					g_devPlayer[i] = player++;
					break;
				case NO_DEVICE:
					break;
				default:
					/* mouse/lightgun/paddle/XE-1AP/activator: a later
					 * milestone wires their buttons and axes */
					player++;
					break;
			}
		}
	}

	g_inited = 1;
	return 1;
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
	if (index >= 0 && index < BTN_COUNT)
		g_buttons[index] = state != 0;
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	(void)packed; /* > 64 buttons: input rides the SetButton wide channel */
	if (!g_inited)
		return;

	g_inputRead = 0;

	/* Power = hard reset, Reset = the console's reset button (held levels,
	 * exactly as the author's BizHawk IEmulator did) */
	if (g_buttons[0])
	{
		system_init();
		system_reset();
	}
	else if (g_buttons[1])
	{
		gen_reset(0);
	}

	{
		const int base = wire_pad_base();
		const int width = wire_pad_width();
		const uint16_t *bits = g_wire == WIRE_GENESIS ? kPadBits : kPadBits8;
		const int maxPlayer = g_wire == WIRE_GENESIS ? 8 : (g_wire == WIRE_GG ? 1 : 2);
		for (int i = 0; i < MAX_DEVICES; i++)
		{
			int player = g_devPlayer[i];
			if (player < 1 || player > maxPlayer)
				continue;
			uint16_t pad = 0;
			const uint8_t *b = &g_buttons[base + (player - 1) * width];
			for (int k = 0; k < width; k++)
				if (b[k]) pad |= bits[k];
			input.pad[i] = pad;
		}
	}

	/* the SMS/SG pause button is a start-bit NMI on pad 0 (BizHawk's rule);
	 * on the Genesis wire index 2 is inert, and the GG wire has no such
	 * console button (its Start is a pad bit) */
	if (g_wire == WIRE_8BIT && g_buttons[2])
		input.pad[0] |= INPUT_START;

	/* disc swapping, edge-triggered exactly like the author's BizHawk
	 * frontend: index -1 is an open tray */
	if (g_wire == WIRE_GENESIS && system_hw == SYSTEM_MCD && g_discCount > 0)
	{
		int newDisk = g_discIndex;
		if (g_buttons[3] && !g_prevDiscBtn[0]) newDisk--;
		if (g_buttons[4] && !g_prevDiscBtn[1]) newDisk++;
		g_prevDiscBtn[0] = g_buttons[3];
		g_prevDiscBtn[1] = g_buttons[4];
		if (newDisk < -1) newDisk = -1;
		if (newDisk >= g_discCount) newDisk = g_discCount - 1;
		if (newDisk != g_discIndex)
		{
			g_discIndex = newDisk;
			cdd_unload();
			if (g_discIndex >= 0)
			{
				char header[0x210];
				cdd_load(g_discs[g_discIndex], header);
			}
			cdd_reset();
		}
	}

	if (system_hw == SYSTEM_MCD)
		system_frame_scd(0);
	else if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
		system_frame_gen(0);
	else
		system_frame_sms(0);

	if (bitmap.viewport.changed & 1)
	{
		bitmap.viewport.changed &= ~1;
		update_viewport();
	}

	g_nsamples = audio_update(g_soundbuffer);
	refresh_video();
}

/* Turbo (optional guest ABI group): while off the core must produce no picture
 * and must otherwise be exactly the machine it would have been. run-gate.sh's
 * turbo leg is the proof - N undrawn frames plus one drawn one come out byte for
 * byte the same machine, and the same picture, as N+1 drawn ones. */
ECL_EXPORT void SetRenderingEnabled(int on) { cinterface_render_enabled = on != 0; }

ECL_EXPORT uint32_t *GetVideoBgra(void) { return g_videoOut; }
ECL_EXPORT int GetVideoWidth(void) { return g_vwidth; }
ECL_EXPORT int GetVideoHeight(void) { return g_vheight; }

ECL_EXPORT int16_t *GetAudio(void) { return g_soundbuffer; }
ECL_EXPORT int GetAudioSampleCount(void) { return g_nsamples; }

/* the machine's own vertical rate (BizHawk's gpgx_get_fps) */
ECL_EXPORT int GetVsyncNumerator(void)
{
	return vdp_pal ? 53203424 : 53693175;
}
ECL_EXPORT int GetVsyncDenominator(void)
{
	return vdp_pal ? 3420 * 313 : 3420 * 262;
}

ECL_EXPORT int InputWasRead(void)
{
	return g_inputRead;
}

/* ---- memory domains: the author's gpgx_get_memdom table, verbatim, plus a
 * writability column. VRAM/CRAM/VSRAM stay read-only through this channel:
 * a raw write would bypass the pattern-cache / color LUT updates. ---- */
static const char *memdom(int which, void **area, int *size, int *writable)
{
	int w = 0;
	const char *name = NULL;
	switch (which)
	{
	case 0:
		if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
			{ *area = work_ram; *size = 0x10000; name = "68K RAM"; w = 1; }
		else if (system_hw == SYSTEM_SG)
			{ *area = work_ram; *size = 0x400; name = "Main RAM"; w = 1; }
		else if (system_hw == SYSTEM_SGII)
			{ *area = work_ram; *size = 0x800; name = "Main RAM"; w = 1; }
		else
			{ *area = work_ram; *size = 0x2000; name = "Main RAM"; w = 1; }
		break;
	case 1:
		if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
			{ *area = zram; *size = 0x2000; name = "Z80 RAM"; w = 1; }
		break;
	case 2:
		if (system_hw != SYSTEM_MCD)
		{
			*area = cart.rom;
			*size = cart.romsize;
			name = (system_hw & SYSTEM_PBC) == SYSTEM_MD ? "MD CART" : "ROM";
		}
		else if (scd.cartridge.id)
			{ *area = scd.cartridge.area; *size = scd.cartridge.mask + 1; name = "EBRAM"; w = 1; }
		break;
	case 3:
		if (system_hw == SYSTEM_MCD)
			{ *area = scd.bootrom; *size = 0x20000; name = "CD BOOT ROM"; }
		break;
	case 4:
		if (system_hw == SYSTEM_MCD)
			{ *area = scd.prg_ram; *size = 0x80000; name = "CD PRG RAM"; w = 1; }
		break;
	case 5:
		if (system_hw == SYSTEM_MCD)
			{ *area = scd.word_ram[0]; *size = 0x20000; name = "CD WORD RAM[0] (1M)"; w = 1; }
		break;
	case 6:
		if (system_hw == SYSTEM_MCD)
			{ *area = scd.word_ram[1]; *size = 0x20000; name = "CD WORD RAM[1] (1M)"; w = 1; }
		break;
	case 7:
		if (system_hw == SYSTEM_MCD)
			{ *area = scd.word_ram_2M; *size = 0x40000; name = "CD WORD RAM (2M)"; w = 1; }
		break;
	case 8:
		if (system_hw == SYSTEM_MCD)
			{ *area = scd.bram; *size = 0x2000; name = "CD BRAM"; w = 1; }
		break;
	case 9:
		if (system_bios & SYSTEM_MD)
			{ *area = boot_rom; *size = 0x800; name = "MD BOOT ROM"; }
		else if (system_bios & (SYSTEM_SMS | SYSTEM_GG))
			{ *area = &cart.rom[0x400000]; *size = sms_cart_bootrom_size(); name = "BOOT ROM"; }
		break;
	case 10:
		if (sram.on)
		{
			*area = sram.sram;
			*size = saveramsize();
			w = 1;
			/* the one Codemasters SRAM cart has no battery (BizHawk's rule) */
			name = sms_cart_is_codies() ? "Cart (Volatile) RAM" : "SRAM";
		}
		else if ((system_hw & SYSTEM_PBC) != SYSTEM_MD)
			{ *area = &work_ram[0x2000]; *size = sms_cart_ram_size(); name = "Cart (Volatile) RAM"; w = 1; }
		break;
	case 11:
		*area = cram;
		*size = (system_hw & SYSTEM_PBC) == SYSTEM_MD ? 0x80 : 0x40;
		name = "CRAM";
		break;
	case 12:
		*area = vsram;
		*size = 128;
		name = "VSRAM";
		break;
	case 13:
		*area = vram;
		*size = (system_hw & SYSTEM_PBC) == SYSTEM_MD ? 0x10000 : 0x4000;
		name = "VRAM";
		break;
	default:
		break;
	}
	if (writable)
		*writable = w;
	return name;
}

/* the table has gaps (system-dependent entries); the ABI wants a dense list */
static int domain_slot(int i)
{
	int found = 0;
	for (int which = 0; which < 14; which++)
	{
		void *area = NULL;
		int size = 0;
		if (memdom(which, &area, &size, NULL) != NULL)
		{
			if (found == i)
				return which;
			found++;
		}
	}
	return -1;
}

ECL_EXPORT int GetMemoryDomainCount(void)
{
	int count = 0;
	for (int which = 0; which < 14; which++)
	{
		void *area = NULL;
		int size = 0;
		if (memdom(which, &area, &size, NULL) != NULL)
			count++;
	}
	return count;
}

ECL_EXPORT const char *GetMemoryDomainName(int i)
{
	int which = domain_slot(i);
	void *area = NULL;
	int size = 0;
	return which >= 0 ? memdom(which, &area, &size, NULL) : NULL;
}

ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i)
{
	int which = domain_slot(i);
	void *area = NULL;
	int size = 0;
	if (which < 0 || memdom(which, &area, &size, NULL) == NULL)
		return NULL;
	/* the composed MCD saveram block goes through tempsram like BizHawk's
	 * gpgx_get_sram - not needed for plain domains, everything is direct */
	return (uint8_t *)area;
}

ECL_EXPORT int64_t GetMemoryDomainSize(int i)
{
	int which = domain_slot(i);
	void *area = NULL;
	int size = 0;
	if (which < 0 || memdom(which, &area, &size, NULL) == NULL)
		return 0;
	return size;
}

ECL_EXPORT int GetMemoryDomainWritable(int i)
{
	int which = domain_slot(i);
	void *area = NULL;
	int size = 0, w = 0;
	if (which < 0 || memdom(which, &area, &size, &w) == NULL)
		return 0;
	return w;
}

/* ---- savedata export (chimera's persistent-data channel) ----
 * SaveRAM starts empty every boot (clear_sram); this group is the way OUT:
 * the cartridge's battery SRAM, or on a Sega CD the internal backup RAM and
 * the backup RAM cartridge. Chimera's Export Save Data menu writes these
 * files; a future continue-from-save project feeds them back in. */

static const char *savedata_entry(int32_t i, uint8_t **buf, int64_t *size)
{
	if (system_hw == SYSTEM_MCD)
	{
		if (i == 0)
		{
			*buf = scd.bram;
			*size = 0x2000;
			return "InternalBackupRAM.brm";
		}
		if (i == 1 && scd.cartridge.id)
		{
			*buf = scd.cartridge.area;
			*size = scd.cartridge.mask + 1;
			return "CartBackupRAM.brm";
		}
		return NULL;
	}
	/* the one Codemasters SRAM cart has no battery (BizHawk's rule) */
	if (i == 0 && sram.on && !sms_cart_is_codies())
	{
		*buf = sram.sram;
		*size = saveramsize();
		return "SRAM.sav";
	}
	return NULL;
}

ECL_EXPORT int32_t GetSaveDataFileCount(void)
{
	uint8_t *b;
	int64_t n;
	int32_t count = 0;
	while (savedata_entry(count, &b, &n) != NULL) count++;
	return count;
}
ECL_EXPORT const char *GetSaveDataFileName(int32_t i)
{
	uint8_t *b;
	int64_t n;
	return savedata_entry(i, &b, &n);
}
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i)
{
	uint8_t *b = NULL;
	int64_t n = 0;
	return savedata_entry(i, &b, &n) != NULL ? n : 0;
}
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i)
{
	uint8_t *b = NULL;
	int64_t n = 0;
	return savedata_entry(i, &b, &n) != NULL ? b : NULL;
}
