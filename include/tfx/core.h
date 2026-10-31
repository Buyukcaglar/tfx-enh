/* TFX's core, the public API (EX19 plan step 4): the game (the original's
 * program translated to C, its state the core's) run headless and stepped
 * by a host that owns the window, the sound and the files.
 *
 *   tfx_create      the core over the host's services, configured by the
 *                   command line the original took ("TFX.EXE cd_demo")
 *   tfx_key / tfx_mouse   input: the keyboard's bytes as port 0x60 gave
 *                   them (scan codes, 0x80 set on a release, 0xe0 prefixes)
 *                   and the mouse's state (tfx_mouse_motion: its motion)
 *   tfx_stick       input: the game port's joystick (M04): present or
 *                   not, its four axes as the one-shots' counts (the read
 *                   loop's iterations an axis stays high; about 400 at
 *                   the centre of a typical stick, 0xffff an axis that
 *                   never times out) and its buttons (bits 0 .. 3: A1,
 *                   A2, B1, B2); none until it is given
 *   tfx_step        the game run until it shows a frame, waits, or ends
 *   tfx_frame       the frame shown last (320 x 200 indexed) and its DAC
 *   tfx_enhanced    the enhanced 3D mode: the world at the picture's size
 *   tfx_ended       how the run ended, the fatal report's message
 *   tfx_quit        the run asked to end (through the game's exit path:
 *                   the files it has open written, the sound stopped)
 *   tfx_destroy     the core freed (a run under way ended first)
 *
 * Time is the host's monotonic clock (now_us), which the core reads when
 * the game reads its clock; the core decides what runs when. One thread
 * calls the API; the game runs only inside tfx_step (on a fiber of its
 * own, handed control in turn), and the host's callbacks are made from
 * there. */
#ifndef TFX_CORE_API_H
#define TFX_CORE_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tfx tfx;

/* The host's services; ctx is passed back. NULL entries: none (the core
 * reports what it needed through diag). */
typedef struct tfx_host {
    void *ctx;
    /* the monotonic clock, in microseconds (required) */
    uint64_t (*now_us)(void *ctx);
    /* the game folder's content (DID.DAT, START.EXE, TFX.DAT, TFX.CFG and
     * the loose files, by the DOS path the game asks for): a file's size
     * (0: missing) and its bytes (1 read, 0 failed) */
    uint32_t (*size)(void *ctx, const char *path);
    int (*read)(void *ctx, const char *path, uint8_t *buf, uint32_t cap);
    /* the user store: a file written whole (1 done), and the directory's
     * k-th entry matching a DOS pattern with DOS's attributes (0: none) */
    int (*write)(void *ctx, const char *path, const uint8_t *buf, uint32_t n);
    int (*find)(void *ctx, const char *pattern, uint16_t attr, uint32_t k, char name[13]);
    /* the DOS environment's variable (DID, BLASTER ...), NULL unset */
    const char *(*getenv)(void *ctx, const char *name);
    /* sound, stamped on now_us's clock (play them a fixed delay late): the
     * music devices' port writes (OPL2 0x388 / 0x389, MPU-401 0x330 /
     * 0x331; without it no music device answers) and the samples (8-bit
     * unsigned mono at rate Hz on a voice, n 0 stops it; the bytes valid
     * during the call only) */
    void (*audio_port)(void *ctx, uint64_t at, uint16_t port, uint8_t value);
    void (*pcm)(void *ctx, uint64_t at, int voice, const uint8_t *data, uint32_t n, uint32_t rate);
    /* the CD's audio: a track's first sector (-1: none), n sectors played
     * from a sector, stopped, the track heard now; without cd_play no
     * CD-ROM */
    int32_t (*cd_track)(void *ctx, int track);
    void (*cd_play)(void *ctx, int32_t from, int32_t n);
    void (*cd_stop)(void *ctx);
    int (*cd_heard)(void *ctx);
    /* the messages the original printed, and the core's own reports */
    void (*print)(void *ctx, const char *message);
    void (*diag)(void *ctx, const char *message);
} tfx_host;

typedef struct tfx_config {
    int argc;           /* the command line: argv[0] the program, then the */
    const char **argv;  /* script's name ("cd_demo") and the like */
} tfx_config;

typedef enum tfx_step_result {
    TFX_STEP_FRAME = 0, /* a frame shown (tfx_frame) */
    TFX_STEP_WAIT = 1,  /* the game waits for time or input: step again later */
    TFX_STEP_ENDED = 2  /* the run has ended (tfx_ended) */
} tfx_step_result;

typedef enum tfx_end {
    TFX_RUNNING = -1,
    TFX_END_QUIT = 0,     /* the player quit (the game's main returned) */
    TFX_END_FATAL = 1,    /* a fatal report */
    TFX_END_REQUEST = 2   /* tfx_quit */
} tfx_end;

/* NULL: no memory, or no clock */
tfx *tfx_create(const tfx_host *host, const tfx_config *config);
void tfx_key(tfx *t, uint8_t byte);
void tfx_mouse(tfx *t, uint16_t x, uint16_t y, uint16_t buttons); /* x 0 .. 639, y 0 .. 199, buttons bits 0 .. 2 */
/* the mouse's motion in mickeys (the driver's counters, which do not stop
 * at the screen's edges: the ARCADE and MILITARY SPEC flight steers by
 * them); without it the core counts tfx_mouse's moves (M09) */
void tfx_mouse_motion(tfx *t, int32_t dx, int32_t dy);
void tfx_stick(tfx *t, int present, const uint16_t count[4], uint8_t buttons);
tfx_step_result tfx_step(tfx *t);
const uint8_t *tfx_frame(const tfx *t, const uint8_t **dac); /* 64000 bytes; dac: 768 6-bit values */
/* the enhanced 3D mode (not the original's): the 3D world drawn at w x h,
 * the picture's own size (0 x 0: off, the original's 320 x 200 only); 0
 * when there is no memory for it. tfx_frame_enhanced: the frame shown
 * last at that size (w x h indexed, tfx_frame's DAC), NULL when off */
int tfx_enhanced(tfx *t, int w, int h);
const uint8_t *tfx_frame_enhanced(tfx *t, int *w, int *h);
tfx_end tfx_ended(const tfx *t, const char **message);
void tfx_quit(tfx *t);
void tfx_destroy(tfx *t);

#ifdef __cplusplus
}
#endif

#endif
