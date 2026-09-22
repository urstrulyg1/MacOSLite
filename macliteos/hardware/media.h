#ifndef MICA_MEDIA_H
#define MICA_MEDIA_H
#include "ml/common.h"
#include "hwcap.h"

/* Multimedia capability + decode verification (spec §8/§9).
 *
 * Two separate questions, kept separate on purpose:
 *   1. what the silicon can decode  -> hwcap.c capability DB
 *   2. what the driver exposes here -> a real vdpauinfo/vainfo query
 * A codec is only reported as hardware-decodable when BOTH agree, and the
 * playback test below only reports PASS when a decoder actually ran. */

typedef enum { ML_DEC_NONE = 0, ML_DEC_MPV, ML_DEC_FFMPEG, ML_DEC_GST } ml_decoder;
const char *ml_decoder_name(ml_decoder d);

typedef struct {
    ml_decoder kind;
    char path[128];
    char version[80];
    bool present;
    char note[128];
} ml_dec_state;
bool ml_decoder_probe(ml_dec_state *out);

typedef struct {
    bool vdpau_lib, vaapi_lib;
    bool has_render_node;
    char render_node[64];
    bool probed;                 /* a runtime tool actually answered */
    char source[32];             /* "vdpauinfo" | "vainfo" */
    uint32_t runtime_mask;       /* ML_CODEC_* the runtime advertises */
    char detail[256];
    char note[192];
} ml_hwdec_state;
bool ml_hwdec_probe(ml_hwdec_state *out);

typedef enum { ML_CAP_HW = 0, ML_CAP_SW, ML_CAP_UNKNOWN } ml_cap;
const char *ml_cap_str(ml_cap c);
/* intersection of silicon capability and runtime reality */
ml_cap ml_codec_capability(uint32_t vendor, uint32_t device, uint32_t codec,
                           const ml_hwdec_state *h, char *detail, size_t detail_len);

/* ---- actual playback/decode test ----------------------------------- */
typedef struct {
    char codec[16];
    int width, height, frames;
    bool ran;
    bool hw_requested, hw_used;
    double wall_ms, cpu_ms, fps;
    double cpu_pct;              /* child CPU time / wall time */
    int dropped;
    char decoder_line[160];
    char note[200];
    ml_cap cap;
} ml_vtest;

/* Generates a short stream with ffmpeg when one is present, decodes it with and
 * without the hardware path, and measures wall time, child CPU time (wait4
 * rusage) and fps. When no decoder exists it sets ran=false and explains why —
 * it never fabricates a result. */
bool ml_video_test_run(ml_vtest *out, const char *codec, int width, int height,
                       int frames, bool want_hw, int *exit_rc);

#endif /* MICA_MEDIA_H */
