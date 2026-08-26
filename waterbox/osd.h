/* osd.h - the port header Genesis Plus GX expects each frontend to provide.
 *
 * Chimera flavor, descended from the author's BizHawk waterbox/gpgx/util/osd.h
 * with the host-callback CD machinery removed: chimera mounts the raw disc
 * files (.cue/.bin/.iso) into the guest filesystem, so upstream's default
 * stdio cdStream (core/macros.h) reads them directly and this header has
 * nothing to say about CDs.
 *
 * The firmware names are the file names chimera's firmware channel mounts
 * (waterbox.config "firmware" declaration ids). The lock-on cart roms keep
 * their BizHawk-era names but never resolve: config.lock_on stays 0.
 */
#ifndef _OSD_H_
#define _OSD_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scrc32.h"

#define MAX_INPUTS 8
#define MAX_KEYS 8
#define MAXPATHLEN 1024

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef M_PI
#define M_PI 3.1415926535897932385
#endif

typedef struct
{
  uint8_t padtype; /* the only field the core reads (config.input[i].padtype) */
} t_input_config;

struct config_t
{
  uint8_t hq_fm;
  uint8_t filter;
  uint8_t hq_psg;
  uint8_t ym2612;
  uint8_t ym2413;
  uint8_t ym3438;
  uint8_t opll;
  uint8_t cd_latency;
  int16_t psg_preamp;
  int16_t fm_preamp;
  int16_t cdda_volume;
  int16_t pcm_volume;
  uint32_t lp_range;
  int16_t low_freq;
  int16_t high_freq;
  int16_t lg;
  int16_t mg;
  int16_t hg;
  uint8_t mono;
  uint8_t system;
  uint8_t region_detect;
  uint8_t vdp_mode;
  uint8_t master_clock;
  uint8_t force_dtack;
  uint8_t addr_error;
  uint8_t bios;
  uint8_t lock_on;
  uint8_t add_on;
  uint8_t hot_swap;
  uint8_t invert_mouse;
  uint8_t gun_cursor[2];
  uint8_t overscan;
  uint8_t gg_extra;
  uint8_t ntsc;
  uint8_t lcd;
  uint8_t render;
  uint8_t enhanced_vscroll;
  uint8_t enhanced_vscroll_limit;
  t_input_config input[MAX_INPUTS];
  uint8_t sprites_always_on_top; /* referenced by our vdp_render.c patch */
};

extern struct config_t config;

/* firmware channel mount names (waterbox.config "firmware" ids) */
#define MD_BIOS     "mdBios"
#define CD_BIOS_US  "cdBiosUS"
#define CD_BIOS_EU  "cdBiosEU"
#define CD_BIOS_JP  "cdBiosJP"
#define MS_BIOS_US  "msBiosUS"
#define MS_BIOS_EU  "msBiosEU"
#define MS_BIOS_JP  "msBiosJP"
#define GG_BIOS     "ggBios"

/* lock-on cart roms: never requested while config.lock_on == 0 */
#define GG_ROM      "lockonGameGenie"
#define AR_ROM      "lockonActionReplay"
#define SK_ROM      "lockonSonicKnuckles"
#define SK_UPMEM    "lockonSonicKnucklesUpmem"

extern void osd_input_update(void);
extern int load_archive(const char *filename, unsigned char *buffer, int maxsize, char *extension);
extern void real_input_callback(void);

#endif /* _OSD_H_ */
