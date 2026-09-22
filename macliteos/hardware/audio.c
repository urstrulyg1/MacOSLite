#include "audio.h"
#include "hwcap.h"
#include "ml/util.h"
#include "ml/log.h"
#include <sound/asound.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>
#include <sys/ioctl.h>

/* ----------------------------------------------------------- enumeration -- */
static void card_codec(ml_aud_card *c)
{
    char rel[96];
    snprintf(rel, sizeof rel, "asound/card%d/codec#0", c->card);
    char *t = ml_read_file(hw_proc(rel), NULL);
    if (!t) { snprintf(rel, sizeof rel, "asound/card%d/codec#2", c->card); t = ml_read_file(hw_proc(rel), NULL); }
    if (!t) return;
    char *l = strtok(t, "\n");
    while (l) {
        char *q = strstr(l, "Codec:");
        if (q) {
            snprintf(c->codec, sizeof c->codec, "%s", ml_str_trim(q + 6));
            break;
        }
        l = strtok(NULL, "\n");
    }
    ml_free(t);
}

bool ml_audio_probe(ml_aud_state *st)
{
    memset(st, 0, sizeof *st);
    st->dev_nodes = ml_is_dir(hw_dev("snd"));
    char *cards = ml_read_file(hw_proc("asound/cards"), NULL);
    if (!cards) {
        snprintf(st->note, sizeof st->note, "%s/asound/cards absent — no ALSA on this host", hw_proc_root());
        return false;
    }
    char *l = strtok(cards, "\n");
    while (l && st->n < (int)ML_ARRAY_SIZE(st->cards)) {
        int num = -1;
        char id[32] = "", drv[32] = "";
        /* " 0 [PCH            ]: HDA-Intel - HDA Intel PCH" */
        if (sscanf(l, " %d [%31[^]]]: %31s", &num, id, drv) >= 1 && num >= 0) {
            ml_aud_card *c = &st->cards[st->n];
            memset(c, 0, sizeof *c);
            c->card = num;
            c->present = true;
            snprintf(c->id, sizeof c->id, "%s", ml_str_trim(id));
            snprintf(c->driver, sizeof c->driver, "%s", ml_str_trim(drv));
            char rel[96], *nm;
            snprintf(rel, sizeof rel, "asound/card%d/id", num);
            nm = ml_sysfs_str(hw_proc(rel), "");
            if (nm[0]) snprintf(c->id, sizeof c->id, "%s", nm);
            ml_free(nm);
            /* "> 0 [PCH ]: HDA-Intel - HDA Intel PCH": the driver is the first
             * token after the colon, the card name follows the " - " separator
             * (splitting on the first '-' would cut "HDA-Intel" in half). */
            char *colon = strchr(l, ':');
            if (colon) {
                char *sep = strstr(colon, " - ");
                if (sep) snprintf(c->name, sizeof c->name, "%s", ml_str_trim(sep + 3));
                else snprintf(c->name, sizeof c->name, "%s", ml_str_trim(colon + 1));
            }
            const char *devroot = hw_dev_root()[0] ? hw_dev_root() : "/dev";
            snprintf(c->pcm_play, sizeof c->pcm_play, "%s/snd/pcmC%dD0p", devroot, num);
            snprintf(c->pcm_cap, sizeof c->pcm_cap, "%s/snd/pcmC%dD0c", devroot, num);
            snprintf(c->ctl, sizeof c->ctl, "%s/snd/controlC%d", devroot, num);
            /* playback/capture presence from the procfs stream files */
            snprintf(rel, sizeof rel, "asound/card%d/pcm0p/info", num);
            c->can_play = ml_file_exists(hw_proc(rel));
            snprintf(rel, sizeof rel, "asound/card%d/pcm0c/info", num);
            c->can_record = ml_file_exists(hw_proc(rel));
            /* HDMI/DP audio usually shows up as pcm3p with an ELD file */
            snprintf(rel, sizeof rel, "asound/card%d/pcm3p/info", num);
            c->has_hdmi = ml_file_exists(hw_proc(rel));
            card_codec(c);
            st->n++;
        }
        l = strtok(NULL, "\n");
    }
    ml_free(cards);
    st->any = st->n > 0;
    if (!st->any) snprintf(st->note, sizeof st->note, "%s/asound/cards is empty", hw_proc_root());
    else if (!st->dev_nodes)
        snprintf(st->note, sizeof st->note,
                 "%d card(s) in procfs but %s/snd is missing — driver not loaded or no permissions",
                 st->n, hw_dev_root());
    return st->any;
}

const ml_aud_card *ml_audio_default(const ml_aud_state *st)
{
    for (int i = 0; i < st->n; i++) if (st->cards[i].can_play) return &st->cards[i];
    return st->n ? &st->cards[0] : NULL;
}

/* --------------------------------------------------------------- mixer ---- */
int ml_audio_ctl_open(int card)
{
    char p[128];
    snprintf(p, sizeof p, "%s/snd/controlC%d", hw_dev_root()[0] ? hw_dev_root() : "/dev", card);
    int fd = open(p, O_RDWR);
    if (fd < 0) fd = open(p, O_RDONLY);
    return fd;
}
void ml_audio_ctl_close(int fd) { if (fd >= 0) close(fd); }

static bool ctl_by_name(int fd, const char *name, struct snd_ctl_elem_id *id)
{
    struct snd_ctl_elem_list list;
    memset(&list, 0, sizeof list);
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) < 0) return false;
    if (!list.count) return false;
    struct snd_ctl_elem_id *ids = calloc(list.count, sizeof *ids);
    if (!ids) return false;
    list.pids = ids;
    list.space = list.count;
    bool ok = ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) >= 0;
    if (ok) {
        ok = false;
        for (unsigned i = 0; i < list.used; i++) {
            if (ids[i].iface != SNDRV_CTL_ELEM_IFACE_MIXER) continue;
            if (!strcasecmp((const char *)ids[i].name, name)) { *id = ids[i]; ok = true; break; }
        }
    }
    free(ids);
    return ok;
}

bool ml_audio_ctl_read(int fd, const char *name, ml_aud_ctl *out)
{
    memset(out, 0, sizeof *out);
    snprintf(out->name, sizeof out->name, "%s", name);
    struct snd_ctl_elem_id id;
    if (fd < 0) { snprintf(out->err, sizeof out->err, "control device not open"); return false; }
    if (!ctl_by_name(fd, name, &id)) { snprintf(out->err, sizeof out->err, "no such control"); return false; }
    struct snd_ctl_elem_info info;
    memset(&info, 0, sizeof info);
    info.id = id;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) < 0) {
        snprintf(out->err, sizeof out->err, "ELEM_INFO: %s", strerror(errno));
        return false;
    }
    if (info.type == SNDRV_CTL_ELEM_TYPE_INTEGER) {
        out->min = info.value.integer.min;
        out->max = info.value.integer.max;
    } else if (info.type == SNDRV_CTL_ELEM_TYPE_BOOLEAN) {
        out->min = 0;
        out->max = 1;
    } else {
        snprintf(out->err, sizeof out->err, "unsupported control type %d", (int)info.type);
        return false;
    }
    struct snd_ctl_elem_value v;
    memset(&v, 0, sizeof v);
    v.id = id;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &v) < 0) {
        snprintf(out->err, sizeof out->err, "ELEM_READ: %s", strerror(errno));
        return false;
    }
    out->val = v.value.integer.value[0];
    out->found = true;
    return true;
}

bool ml_audio_ctl_write(int fd, const char *name, long value, long *applied)
{
    struct snd_ctl_elem_id id;
    if (applied) *applied = 0;
    if (fd < 0 || !ctl_by_name(fd, name, &id)) return false;
    struct snd_ctl_elem_value v;
    memset(&v, 0, sizeof v);
    v.id = id;
    v.value.integer.value[0] = value;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &v) < 0) return false;
    ml_aud_ctl back;
    if (!ml_audio_ctl_read(fd, name, &back)) return false;   /* verify, never assume */
    if (applied) *applied = back.val;
    return back.val == value;
}

static const char *vol_bases[] = { "Master", "PCM", "Headphone", "Speaker", "Front" };

bool ml_audio_ctl_find_volume(int fd, ml_aud_ctl *out)
{
    for (size_t i = 0; i < ML_ARRAY_SIZE(vol_bases); i++) {
        char name[64];
        snprintf(name, sizeof name, "%s Playback Volume", vol_bases[i]);
        if (ml_audio_ctl_read(fd, name, out)) {
            char sw[64];
            snprintf(sw, sizeof sw, "%s Playback Switch", vol_bases[i]);
            bool on = true;
            out->has_switch = ml_audio_switch_read(fd, sw, &on);
            out->muted = out->has_switch ? !on : false;
            return true;
        }
    }
    if (out) { memset(out, 0, sizeof *out); snprintf(out->err, sizeof out->err, "no * Playback Volume control"); }
    return false;
}

bool ml_audio_switch_read(int fd, const char *name, bool *on)
{
    ml_aud_ctl c;
    if (!ml_audio_ctl_read(fd, name, &c)) return false;
    *on = c.val != 0;
    return true;
}
bool ml_audio_switch_write(int fd, const char *name, bool on, bool *applied)
{
    long back = -1;
    bool ok = ml_audio_ctl_write(fd, name, on ? 1 : 0, &back);
    if (applied) *applied = (back > 0);
    return ok;
}

int ml_audio_list_controls(int card, char out[][48], int max)
{
    int fd = ml_audio_ctl_open(card);
    if (fd < 0) return 0;
    struct snd_ctl_elem_list list;
    memset(&list, 0, sizeof list);
    int n = 0;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) >= 0 && list.count) {
        struct snd_ctl_elem_id *ids = calloc(list.count, sizeof *ids);
        if (ids) {
            list.pids = ids;
            list.space = list.count;
            if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) >= 0)
                for (unsigned i = 0; i < list.used && n < max; i++)
                    if (ids[i].iface == SNDRV_CTL_ELEM_IFACE_MIXER)
                        snprintf(out[n++], 48, "%s", (const char *)ids[i].name);
            free(ids);
        }
    }
    close(fd);
    return n;
}

ml_audio_status ml_audio_status_get(const ml_aud_state *st)
{
    ml_audio_status s;
    memset(&s, 0, sizeof s);
    s.volume_pct = -1;
    const ml_aud_card *c = ml_audio_default(st);
    if (!c) {
        snprintf(s.why, sizeof s.why, "no sound card: nothing to control");
        return s;
    }
    if (hw_using_fixture()) {
        /* A fixture tree cannot answer a mixer ioctl, and writing to a fixture
         * file is not a hardware change: say NOT TESTED, never PASS. */
        snprintf(s.why, sizeof s.why,
                 "fixture roots: mixer ioctl not performed, volume state unknowable here");
        return s;
    }
    s.readable = true;
    s.volume_pct = ml_audio_volume_percent(c->card);
    s.have_volume = s.volume_pct >= 0;
    s.have_mute = ml_audio_get_mute(c->card, &s.muted);
    if (s.have_volume && s.have_mute)
        snprintf(s.why, sizeof s.why, "%d%%%s", s.volume_pct, s.muted ? ", muted" : "");
    else if (s.have_volume)
        snprintf(s.why, sizeof s.why, "%d%% (no playback switch exposed)", s.volume_pct);
    else
        snprintf(s.why, sizeof s.why, "no usable * Playback Volume control on card%d", c->card);
    return s;
}

int ml_audio_volume_percent(int card)
{
    int fd = ml_audio_ctl_open(card);
    if (fd < 0) return -1;
    ml_aud_ctl c;
    int pct = -1;
    if (ml_audio_ctl_find_volume(fd, &c) && c.max > c.min)
        pct = (int)(((c.val - c.min) * 100) / (c.max - c.min));
    close(fd);
    return pct;
}

bool ml_audio_set_volume_percent(int card, int pct, int *applied)
{
    int fd = ml_audio_ctl_open(card);
    if (fd < 0) { if (applied) *applied = -1; return false; }
    ml_aud_ctl c;
    bool ok = false;
    if (ml_audio_ctl_find_volume(fd, &c) && c.max >= c.min) {
        pct = ML_CLAMP(pct, 0, 100);
        long raw = c.min + (long)(((c.max - c.min) * (long)pct + 50) / 100);
        long back = 0;
        ok = ml_audio_ctl_write(fd, c.name, raw, &back);
        if (applied) *applied = (c.max > c.min) ? (int)(((back - c.min) * 100) / (c.max - c.min)) : (int)back;
    } else if (applied) *applied = -1;
    close(fd);
    return ok;
}

bool ml_audio_get_mute(int card, bool *muted)
{
    int fd = ml_audio_ctl_open(card);
    if (fd < 0) return false;
    ml_aud_ctl c;
    bool ok = false;
    if (ml_audio_ctl_find_volume(fd, &c) && c.has_switch) { *muted = c.muted; ok = true; }
    close(fd);
    return ok;
}

bool ml_audio_set_mute(int card, bool mute, bool *applied)
{
    int fd = ml_audio_ctl_open(card);
    if (fd < 0) return false;
    ml_aud_ctl c;
    bool ok = false;
    if (ml_audio_ctl_find_volume(fd, &c) && c.has_switch) {
        char sw[64];
        char base[48];
        snprintf(base, sizeof base, "%s", c.name);
        char *sp = strstr(base, " Playback Volume");
        if (sp) *sp = 0;
        snprintf(sw, sizeof sw, "%s Playback Switch", base);
        ok = ml_audio_switch_write(fd, sw, !mute, applied);
    }
    close(fd);
    return ok;
}

/* ----------------------------------------------------------------- tone --- */
size_t ml_tone_generate(int16_t *buf, size_t cap_samples, int rate, int hz_start, int hz_end,
                        int ms, int channels, ml_tone_kind kind)
{
    if (rate <= 0 || ms <= 0 || !buf || !cap_samples || channels <= 0) return 0;
    size_t want = (size_t)((uint64_t)rate * (uint64_t)ms / 1000ull);
    if (want > cap_samples / (size_t)channels) want = cap_samples / (size_t)channels;
    size_t ramp = (size_t)(rate * 5 / 1000);           /* 5 ms attack/decay: no clicks */
    double phase = 0.0;
    for (size_t i = 0; i < want; i++) {
        double f = (kind == ML_TONE_SWEEP && want > 1)
                       ? hz_start + (hz_end - hz_start) * ((double)i / (double)(want - 1))
                       : hz_start;
        phase += 2.0 * M_PI * f / (double)rate;
        if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
        double env = 1.0;
        if (ramp && i < ramp) env = (double)i / (double)ramp;
        else if (ramp && i + ramp >= want) env = (double)(want - i) / (double)ramp;
        int16_t s = (int16_t)(sin(phase) * 0.5 * env * 32767.0);   /* -6 dBFS */
        for (int c = 0; c < channels; c++) buf[i * (size_t)channels + (size_t)c] = s;
    }
    return want;
}

size_t ml_wav_header(uint8_t h[44], uint32_t data_bytes, int rate, int channels, int bits)
{
    uint32_t byterate = (uint32_t)rate * (uint32_t)channels * (uint32_t)bits / 8u;
    uint16_t blockalign = (uint16_t)(channels * bits / 8);
    memcpy(h, "RIFF", 4);
    uint32_t riff = data_bytes + 36;
    memcpy(h + 4, &riff, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    uint32_t fmt_len = 16;
    memcpy(h + 16, &fmt_len, 4);
    uint16_t pcm = 1;
    memcpy(h + 20, &pcm, 2);
    uint16_t ch = (uint16_t)channels;
    memcpy(h + 22, &ch, 2);
    uint32_t r = (uint32_t)rate;
    memcpy(h + 24, &r, 4);
    memcpy(h + 28, &byterate, 4);
    memcpy(h + 32, &blockalign, 2);
    uint16_t b = (uint16_t)bits;
    memcpy(h + 34, &b, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data_bytes, 4);
    return 44;
}

bool ml_audio_write_wav(const char *path, const int16_t *pcm, size_t frames, int rate, int channels)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint8_t hdr[44];
    uint32_t data = (uint32_t)(frames * (size_t)channels * 2u);
    ml_wav_header(hdr, data, rate, channels, 16);
    if (fwrite(hdr, 1, 44, f) != 44) { fclose(f); return false; }
    if (data && fwrite(pcm, 1, data, f) != data) { fclose(f); return false; }
    fclose(f);
    return true;
}

/* ------------------------------------------------------------ PCM playback - */
static void params_init(struct snd_pcm_hw_params *p)
{
    memset(p, 0, sizeof *p);
    for (unsigned i = 0; i < ML_ARRAY_SIZE(p->masks); i++)
        for (unsigned b = 0; b < ML_ARRAY_SIZE(p->masks[i].bits); b++) p->masks[i].bits[b] = ~0u;
    for (unsigned i = 0; i < ML_ARRAY_SIZE(p->intervals); i++) {
        p->intervals[i].min = 0;
        p->intervals[i].max = ~0u;
    }
    p->rmask = ~0u;
    p->info = ~0u;
}
static void params_set_mask(struct snd_pcm_hw_params *p, int param, unsigned val)
{
    struct snd_mask *m = &p->masks[param - SNDRV_PCM_HW_PARAM_FIRST_MASK];
    memset(m, 0, sizeof *m);
    m->bits[val / 32] = 1u << (val % 32);
}
static void params_set_int(struct snd_pcm_hw_params *p, int param, unsigned val)
{
    struct snd_interval *iv = &p->intervals[param - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
    iv->min = val;
    iv->max = val;
    iv->integer = 1;
}

int ml_audio_play(int card, int device, const int16_t *pcm, size_t frames, int rate, int channels)
{
    char path[128];
    snprintf(path, sizeof path, "%s/snd/pcmC%dD%dp", hw_dev_root()[0] ? hw_dev_root() : "/dev", card, device);
    int fd = open(path, O_RDWR);
    if (fd < 0) fd = open(path, O_WRONLY);
    if (fd < 0) return ML_AUDIO_NO_DEVICE;

    struct snd_pcm_hw_params hp;
    params_init(&hp);
    params_set_mask(&hp, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    params_set_mask(&hp, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
    params_set_mask(&hp, SNDRV_PCM_HW_PARAM_SUBFORMAT, 0 /* SNDRV_PCM_SUBFORMAT_STD */);
    params_set_int(&hp, SNDRV_PCM_HW_PARAM_CHANNELS, (unsigned)channels);
    params_set_int(&hp, SNDRV_PCM_HW_PARAM_RATE, (unsigned)rate);
    params_set_int(&hp, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 1024);
    params_set_int(&hp, SNDRV_PCM_HW_PARAM_PERIODS, 4);
    if (ioctl(fd, SNDRV_PCM_IOCTL_HW_REFINE, &hp) < 0) { close(fd); return ML_AUDIO_PARAMS_FAILED; }
    if (ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hp) < 0) { close(fd); return ML_AUDIO_PARAMS_FAILED; }

    struct snd_pcm_sw_params sp;
    memset(&sp, 0, sizeof sp);
    sp.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
    sp.period_step = 1;
    sp.avail_min = 1024;
    sp.start_threshold = 1024;
    sp.stop_threshold = (snd_pcm_uframes_t)frames + 4096;
    if (ioctl(fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sp) < 0) { close(fd); return ML_AUDIO_PARAMS_FAILED; }
    if (ioctl(fd, SNDRV_PCM_IOCTL_PREPARE, NULL) < 0) { close(fd); return ML_AUDIO_PARAMS_FAILED; }

    struct snd_xferi x;
    x.buf = (void *)pcm;
    x.frames = (snd_pcm_uframes_t)frames;
    while (x.frames > 0) {
        if (ioctl(fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) < 0) {
            if (errno == EPIPE || errno == ESTRPIPE) {     /* xrun / resume: retry once */
                if (ioctl(fd, SNDRV_PCM_IOCTL_PREPARE, NULL) < 0) { close(fd); return ML_AUDIO_WRITE_FAILED; }
                continue;
            }
            close(fd);
            return ML_AUDIO_WRITE_FAILED;
        }
        size_t done = (size_t)x.result;
        x.buf = (char *)x.buf + done * (size_t)channels * 2;
        x.frames -= done;
    }
    ioctl(fd, SNDRV_PCM_IOCTL_DRAIN, NULL);
    close(fd);
    return ML_AUDIO_OK;
}

const char *ml_audio_errstr(int rc)
{
    switch (rc) {
    case ML_AUDIO_OK:            return "ok";
    case ML_AUDIO_NO_DEVICE:     return "no ALSA PCM device";
    case ML_AUDIO_OPEN_FAILED:   return "cannot open PCM device";
    case ML_AUDIO_PARAMS_FAILED: return "hardware params refused";
    case ML_AUDIO_WRITE_FAILED:  return "write failed";
    default:                     return "unknown error";
    }
}

int ml_audio_test_tone(int card, int device, int ms, int hz, const char *wav_path,
                       char *detail, size_t detail_len)
{
    int rate = 48000, channels = 2;
    size_t frames = (size_t)rate * (size_t)ms / 1000u;
    int16_t *buf = ml_alloc(frames * (size_t)channels * sizeof(int16_t));
    size_t got = ml_tone_generate(buf, frames * (size_t)channels, rate, hz, hz * 2, ms,
                                  channels, hz > 0 ? ML_TONE_SINE : ML_TONE_SWEEP);
    int rc;
    if (wav_path) {
        rc = ml_audio_write_wav(wav_path, buf, got, rate, channels) ? ML_AUDIO_OK : ML_AUDIO_WRITE_FAILED;
        if (detail) snprintf(detail, detail_len, "wrote %s: %zu frames, %d Hz tone, %d ms, %d Hz stereo 16-bit",
                             wav_path, got, hz, ms, rate);
    } else {
        rc = ml_audio_play(card, device, buf, got, rate, channels);
        if (detail) snprintf(detail, detail_len, "pcmC%dD%dp: %zu frames @ %d Hz (%s)",
                             card, device, got, rate, ml_audio_errstr(rc));
    }
    ml_free(buf);
    return rc;
}
