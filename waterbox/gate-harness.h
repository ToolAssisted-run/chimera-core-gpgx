/* gate-harness.h - the shared half of run-native.c and run-wbx.c.
 *
 * One replay-and-digest loop over an abstract core interface, so the native
 * reference and the sandboxed core run EXACTLY the same schedule: the same
 * .sol movie (quickerGPGX's format) or the same pseudo-random pad exercise,
 * digesting video, audio, lag and every memory domain per frame. The two
 * drivers differ only in how the exports are reached.
 *
 * Wire format (waterbox.config button order): 0 Power, 1 Reset, 2 Pause,
 * then P1..P8 x {U,D,L,R,A,B,C,S,X,Y,Z,M}.
 */
#ifndef GATE_HARNESS_H
#define GATE_HARNESS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GATE_BTN_COUNT (3 + 8 * 12)

struct gate_core
{
	int (*init)(void);
	const char *(*load_error)(void);
	void (*set_button)(int32_t index, int32_t state);
	void (*frame)(void);
	const uint32_t *(*video)(int *w, int *h);
	const int16_t *(*audio)(int *n);
	int (*input_was_read)(void);
	int (*domain_count)(void);
	const char *(*domain_name)(int i);
	const uint8_t *(*domain_ptr)(int i);
	int64_t (*domain_size)(int i);
	/* optional per-frame hook (the rerecord leg); may be NULL */
	void (*pre_frame)(void);
};

struct gate_opts
{
	long frames;          /* run length; a movie may end earlier padded with idle */
	const char *solPath;  /* NULL = pad-exercise schedule */
	const char *sys;      /* genesis | sms | sg1000 | segacd */
	const char *ctl1;     /* none | gamepad3b | gamepad6b | gamepad2b | gamegear2b */
	const char *ctl2;
	const char *screenshotPath; /* optional final-frame .tga */
	int exercise;         /* nonzero: drive P1 with a deterministic pattern */
};

static uint64_t gate_fnv(uint64_t h, const void *p, size_t n)
{
	const uint8_t *b = (const uint8_t *)p;
	if (!h) h = 1469598103934665603ULL;
	for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
	return h;
}

/* deterministic P1 pattern, identical in both drivers, so the input path is
 * part of the comparison; Power/Reset/Pause stay untouched */
static void gate_exercise_pad(long frame, uint8_t *buttons)
{
	uint64_t x = (uint64_t)frame * 6364136223846793005ULL + 1442695040888963407ULL;
	x ^= x >> 33;
	for (int k = 0; k < 12; k++)
		buttons[3 + k] = (x >> k) & 1;
	/* holding LEFT+RIGHT or UP+DOWN is not a pad state a real controller
	 * produces and some games misbehave; drop the contradictions */
	if (buttons[3] && buttons[4]) buttons[4] = 0;
	if (buttons[5] && buttons[6]) buttons[6] = 0;
}

/* ---- .sol parsing (quickerGPGX's movie format) ---- */

struct gate_sol
{
	char **lines;
	long count;
};

static int gate_sol_load(const char *path, struct gate_sol *sol)
{
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	sol->lines = NULL;
	sol->count = 0;
	long cap = 0;
	char line[256];
	while (fgets(line, sizeof line, f))
	{
		size_t n = strlen(line);
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
		if (n == 0) continue;
		if (sol->count == cap)
		{
			cap = cap ? cap * 2 : 1024;
			sol->lines = (char **)realloc(sol->lines, (size_t)cap * sizeof(char *));
		}
		sol->lines[sol->count++] = strdup(line);
	}
	fclose(f);
	return 1;
}

/* one controller field -> the wire's pad block at base; returns chars consumed
 * or -1 on a malformed field */
static int gate_parse_pad(const char *ctl, const char *s, uint8_t *buttons, int base)
{
	static const int map3b[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	static const int map6b[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
	static const int map2b[] = { 0, 1, 2, 3, 5, 6 };
	static const int mapgg[] = { 0, 1, 2, 3, 5, 6, 7 };
	static const struct { const char *name; const char *cols; const int *map; } kinds[] = {
		{ "gamepad3b", "UDLRABCS", map3b },
		{ "gamepad6b", "UDLRABCSXYZM", map6b },
		{ "gamepad2b", "UDLR12", map2b },
		{ "gamegear2b", "UDLR12S", mapgg },
	};
	if (strcmp(ctl, "none") == 0)
		return 0;
	for (size_t k = 0; k < sizeof kinds / sizeof kinds[0]; k++)
	{
		if (strcmp(ctl, kinds[k].name) != 0)
			continue;
		int n = (int)strlen(kinds[k].cols);
		for (int i = 0; i < n; i++)
		{
			if (s[i] != '.' && s[i] != kinds[k].cols[i])
				return -1;
			buttons[base + kinds[k].map[i]] = s[i] != '.';
		}
		return n;
	}
	return -1;
}

/* a full |console|ctl1|ctl2| line -> wire buttons; 0 on malformed input */
static int gate_parse_line(const char *line, const char *sys, const char *ctl1,
	const char *ctl2, uint8_t *buttons)
{
	memset(buttons, 0, GATE_BTN_COUNT);
	const char *s = line;
	if (*s++ != '|') return 0;

	if (*s != '.' && *s != 'P') return 0;
	buttons[0] = *s++ == 'P';
	if (*s != '.' && *s != 'r') return 0;
	buttons[1] = *s++ == 'r';
	if (strcmp(sys, "sms") == 0)
	{
		if (*s != '.' && *s != 'p') return 0;
		buttons[2] = *s++ == 'p';
	}
	if (strcmp(sys, "segacd") == 0)
	{
		/* previous/next disc: not wired yet (single-disc milestone) */
		if (*s != '.' && *s != '<') return 0;
		s++;
		if (*s != '.' && *s != '>') return 0;
		s++;
	}
	if (*s++ != '|') return 0;

	int n = gate_parse_pad(ctl1, s, buttons, 3);
	if (n < 0) return 0;
	s += n;
	if (n > 0 && *s++ != '|') return 0;

	n = gate_parse_pad(ctl2, s, buttons, 3 + 12);
	if (n < 0) return 0;
	s += n;
	if (n > 0 && *s++ != '|') return 0;

	return *s == 0;
}

static int gate_write_tga(const char *path, const uint32_t *bgra, int w, int h)
{
	FILE *f = fopen(path, "wb");
	if (!f) return 0;
	uint8_t hdr[18] = { 0 };
	hdr[2] = 2;
	hdr[12] = w & 0xff; hdr[13] = (w >> 8) & 0xff;
	hdr[14] = h & 0xff; hdr[15] = (h >> 8) & 0xff;
	hdr[16] = 32;
	hdr[17] = 0x20;
	fwrite(hdr, 1, 18, f);
	fwrite(bgra, 4, (size_t)w * h, f);
	fclose(f);
	return 1;
}

/* the loop both drivers share; prints the digest block to stdout */
static int gate_run(const struct gate_core *c, const struct gate_opts *o)
{
	struct gate_sol sol = { NULL, 0 };
	if (o->solPath && !gate_sol_load(o->solPath, &sol))
	{
		fprintf(stderr, "cannot read movie %s\n", o->solPath);
		return 1;
	}

	if (c->init() != 1)
	{
		fprintf(stderr, "Init failed: %s\n", c->load_error ? c->load_error() : "?");
		return 1;
	}

	long frames = o->frames;
	if (sol.count > 0 && frames < sol.count)
		frames = sol.count;

	uint64_t vh = 0, ah = 0;
	long lag = 0;
	uint8_t buttons[GATE_BTN_COUNT];
	uint8_t prev[GATE_BTN_COUNT];
	memset(prev, 0, sizeof prev);

	for (long f = 0; f < frames; f++)
	{
		memset(buttons, 0, sizeof buttons);
		if (f < sol.count)
		{
			if (!gate_parse_line(sol.lines[f], o->sys, o->ctl1, o->ctl2, buttons))
			{
				fprintf(stderr, "bad movie line %ld: '%s'\n", f, sol.lines[f]);
				return 1;
			}
		}
		else if (o->exercise)
			gate_exercise_pad(f, buttons);

		if (c->pre_frame)
			c->pre_frame();

		for (int i = 0; i < GATE_BTN_COUNT; i++)
		{
			if (buttons[i] != prev[i])
				c->set_button(i, buttons[i]);
			prev[i] = buttons[i];
		}

		c->frame();

		int w = 0, h = 0, n = 0;
		const uint32_t *video = c->video(&w, &h);
		const int16_t *audio = c->audio(&n);
		vh = gate_fnv(vh, &w, sizeof w);
		vh = gate_fnv(vh, &h, sizeof h);
		vh = gate_fnv(vh, video, (size_t)w * h * 4);
		ah = gate_fnv(ah, audio, (size_t)n * 2 * sizeof(int16_t));
		if (!c->input_was_read())
			lag++;

		if (o->screenshotPath && f == frames - 1)
			gate_write_tga(o->screenshotPath, video, w, h);
	}

	printf("frames=%ld\n", frames);
	printf("videoHash=%016llx\n", (unsigned long long)vh);
	printf("audioHash=%016llx\n", (unsigned long long)ah);
	printf("lagFrames=%ld\n", lag);
	int nd = c->domain_count();
	for (int i = 0; i < nd; i++)
	{
		uint64_t dh = gate_fnv(0, c->domain_ptr(i), (size_t)c->domain_size(i));
		printf("domain[%s]=%016llx\n", c->domain_name(i), (unsigned long long)dh);
	}
	return 0;
}

/* shared CLI parsing: returns the index of the first positional argument's
 * slot usage message on error */
static int gate_parse_opts(int argc, char **argv, int first, struct gate_opts *o)
{
	o->frames = 600;
	o->solPath = NULL;
	o->sys = "genesis";
	o->ctl1 = "gamepad3b";
	o->ctl2 = "none";
	o->screenshotPath = NULL;
	o->exercise = 0;
	for (int i = first; i < argc; i++)
	{
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) o->frames = strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--sol") && i + 1 < argc) o->solPath = argv[++i];
		else if (!strcmp(argv[i], "--sys") && i + 1 < argc) o->sys = argv[++i];
		else if (!strcmp(argv[i], "--ctl1") && i + 1 < argc) o->ctl1 = argv[++i];
		else if (!strcmp(argv[i], "--ctl2") && i + 1 < argc) o->ctl2 = argv[++i];
		else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) o->screenshotPath = argv[++i];
		else if (!strcmp(argv[i], "--exercise")) o->exercise = 1;
		else if (!strcmp(argv[i], "--rerecord")) ; /* run-wbx's; ignored here */
		else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 0; }
	}
	return 1;
}

#endif /* GATE_HARNESS_H */
