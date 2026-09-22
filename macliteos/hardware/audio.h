#ifndef MICA_AUDIO_H
#define MICA_AUDIO_H
#include "ml/common.h"
#include <stdint.h>

/* MacLiteOS audio backend (spec §7).
 *
 * Smallest reliable architecture: no sound server, no libasound, no daemon.
 * PCM and mixer are driven with the ALSA kernel UAPI directly (<sound/asound.h>
 * is part of the C library headers, so this adds no dependency), the tone is
 * generated in-process, and when there is no sound device the tools say so and
 * exit non-zero instead of pretending a tone was heard.
 *
 * Everything here is exercised by tests/test_hardware.c for the parts that do
 * not need a sound card: card/PCM enumeration from a /proc/asound fixture, the
 * control-name search, tone sample values and the WAV container. */

typedef struct {
    int card;                     /* 0 */
    char id[32];                  /* "PCH", "HDMI" */
    char driver[32];              /* "HDA-Intel" */
    char name[64];                /* "HDA Intel PCH" */
    char codec[48];               /* "Realtek ALC889" from codec#0 */
    char pcm_play[64];            /* /dev/snd/pcmC0D0p */
    char pcm_cap[64];
    char ctl[64];                 /* /dev/snd/controlC0 */
    bool can_play, can_record, has_hdmi;
    bool present;
} ml_aud_card;

typedef struct {
    ml_aud_card cards[8];
    int n;
    bool any;
    bool dev_nodes;               /* /dev/snd exists at all */
    char note[192];
} ml_aud_state;

bool ml_audio_probe(ml_aud_state *st);
const ml_aud_card *ml_audio_default(const ml_aud_state *st);

/* ---- mixer --------------------------------------------------------- */
typedef struct {
    char name[48];
    long min, max, val;
    bool found, muted, has_switch;
    char err[96];
} ml_aud_ctl;

int ml_audio_ctl_open(int card);                     /* fd or -1 */
void ml_audio_ctl_close(int fd);
/* look up "<base> Playback Volume" / "<base> Playback Switch"; bases tried are
 * Master, PCM, Headphone, Speaker, Front. Returns false when none exists. */
bool ml_audio_ctl_find_volume(int fd, ml_aud_ctl *out);
bool ml_audio_ctl_read(int fd, const char *name, ml_aud_ctl *out);
bool ml_audio_ctl_write(int fd, const char *name, long value, long *applied);
bool ml_audio_switch_read(int fd, const char *name, bool *on);
bool ml_audio_switch_write(int fd, const char *name, bool on, bool *applied);

/* One honest answer to "can the audio path be controlled here?", shared by
 * maclite-audio and maclite-hardware so the PASS/FAIL/NOT TESTED rule cannot
 * drift between the two tools. `readable` is false when no mixer ioctl was
 * performed at all (no card, or fixture roots — a fixture cannot answer an
 * ioctl), which makes the caller's verdict NOT TESTED rather than FAIL. */
typedef struct {
    bool readable;               /* a mixer ioctl really ran */
    bool have_volume;
    int volume_pct;              /* -1 unknown */
    bool have_mute;
    bool muted;
    char why[128];
} ml_audio_status;
ml_audio_status ml_audio_status_get(const ml_aud_state *st);

int ml_audio_volume_percent(int card);                       /* -1 unknown */
bool ml_audio_set_volume_percent(int card, int pct, int *applied);
bool ml_audio_get_mute(int card, bool *muted);
bool ml_audio_set_mute(int card, bool mute, bool *applied);
/* enumerate every MIXER control name into out (for maclite-audio list) */
int ml_audio_list_controls(int card, char out[][48], int max);

/* ---- PCM ----------------------------------------------------------- */
enum { ML_AUDIO_OK = 0, ML_AUDIO_NO_DEVICE = -1, ML_AUDIO_OPEN_FAILED = -2,
       ML_AUDIO_PARAMS_FAILED = -3, ML_AUDIO_WRITE_FAILED = -4 };
const char *ml_audio_errstr(int rc);

typedef enum { ML_TONE_SINE = 0, ML_TONE_SWEEP } ml_tone_kind;
/* fill buf with `ms` milliseconds of interleaved samples at `rate` Hz;
 * cap_samples is the buffer capacity in samples (frames * channels).
 * Returns the number of frames written. */
size_t ml_tone_generate(int16_t *buf, size_t cap_samples, int rate, int hz_start,
                        int hz_end, int ms, int channels, ml_tone_kind kind);
size_t ml_wav_header(uint8_t hdr[44], uint32_t data_bytes, int rate, int channels, int bits);
/* write a standalone WAV file (used by `maclite-audio test --wav`, and by the
 * unit test, which checks the container byte for byte) */
bool ml_audio_write_wav(const char *path, const int16_t *pcm, size_t frames, int rate, int channels);
/* real playback through /dev/snd/pcmC<card>D<device>p */
int ml_audio_play(int card, int device, const int16_t *pcm, size_t frames, int rate, int channels);
/* generate + play (or, when path != NULL, write) a test tone */
int ml_audio_test_tone(int card, int device, int ms, int hz, const char *wav_path,
                       char *detail, size_t detail_len);

#endif /* MICA_AUDIO_H */
