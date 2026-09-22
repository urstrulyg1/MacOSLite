#include "edid.h"
#include "hwcap.h"
#include "ml/util.h"
#include "ml/log.h"
#include <ctype.h>

static void parse_descriptor(const uint8_t *d, edid_info *e)
{
    uint16_t flag = (uint16_t)((d[0] << 8) | d[1]);
    if (flag != 0) {
        /* detailed timing descriptor */
        if (e->nmodes >= (int)ML_ARRAY_SIZE(e->modes)) return;
        edid_mode *m = &e->modes[e->nmodes];
        memset(m, 0, sizeof *m);
        /* DTD layout (EDID 1.3 table 3.18): clock(0-1), hactive low(2),
         * hblank low(3), hactive/hblank high nibbles(4), vactive low(5),
         * vblank low(6), vactive/vblank high nibbles(7). Getting this off by a
         * byte yields a plausible-looking wrong mode, so it is test-pinned. */
        m->pixclk_khz = (int)((uint32_t)d[0] | ((uint32_t)d[1] << 8)) * 10;
        int hactive = (int)d[2] | (((int)d[4] >> 4) << 8);
        int hblank  = (int)d[3] | (((int)d[4] & 0x0f) << 8);
        int vactive = (int)d[5] | (((int)d[7] >> 4) << 8);
        int vblank  = (int)d[6] | (((int)d[7] & 0x0f) << 8);
        if (hactive <= 0 || vactive <= 0 || m->pixclk_khz <= 0) return;   /* reserved / invalid */
        m->w = hactive;
        m->h = vactive;
        m->hblank = hblank;
        m->vblank = vblank;
        m->interlaced = (d[17] & 0x80) != 0;
        m->hsync_positive = (d[17] & 0x02) != 0;
        m->vsync_positive = (d[17] & 0x04) != 0;
        int64_t ht = (int64_t)hactive + hblank;
        int64_t vt = (int64_t)vactive + vblank;
        if (m->interlaced) vt /= 2;
        if (ht > 0 && vt > 0)
            m->refresh_mhz = (int)(((int64_t)m->pixclk_khz * 1000 * 1000) / (ht * vt));
        /* EDID 1.3+: the FIRST detailed timing descriptor is preferred.
         * Established timings listed earlier in the block must not shadow it. */
        bool any_preferred = false;
        for (int k = 0; k < e->nmodes; k++) if (e->modes[k].preferred) any_preferred = true;
        m->preferred = !any_preferred;
        e->nmodes++;
        if (m->preferred) e->preferred = e->nmodes - 1;
        return;
    }
    /* monitor descriptor */
    char buf[14];
    memcpy(buf, d + 5, 13);
    buf[13] = 0;
    for (int i = 0; i < 13; i++) if (buf[i] == 0x0a) buf[i] = 0;
    switch (d[3]) {
    case 0xFC: snprintf(e->monitor_name, sizeof e->monitor_name, "%s", ml_str_trim(buf)); break;
    case 0xFF: snprintf(e->serial_string, sizeof e->serial_string, "%s", ml_str_trim(buf)); break;
    default: break;
    }
}

static void add_established(edid_info *e, int w, int h, int hz, bool bit)
{
    if (!bit || e->nmodes >= (int)ML_ARRAY_SIZE(e->modes)) return;
    edid_mode *m = &e->modes[e->nmodes++];
    memset(m, 0, sizeof *m);
    m->w = w; m->h = h; m->refresh_mhz = hz * 1000; m->from_established = true;
}

bool edid_parse(const uint8_t *d, size_t len, edid_info *out)
{
    edid_info_init(out);
    if (len < 128) { snprintf(out->problem, sizeof out->problem, "only %zu bytes (need 128)", len); return false; }
    static const uint8_t hdr[8] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
    if (memcmp(d, hdr, 8) != 0) { snprintf(out->problem, sizeof out->problem, "bad EDID header"); return false; }
    unsigned sum = 0;
    for (int i = 0; i < 128; i++) sum += d[i];
    out->checksum_ok = (sum & 0xff) == 0;
    if (!out->checksum_ok)
        snprintf(out->problem, sizeof out->problem, "checksum mismatch (sum %% 256 = %u)", sum & 0xff);

    uint16_t mfg = (uint16_t)((d[8] << 8) | d[9]);
    out->manufacturer[0] = (char)('@' + ((mfg >> 10) & 0x1f));
    out->manufacturer[1] = (char)('@' + ((mfg >> 5) & 0x1f));
    out->manufacturer[2] = (char)('@' + (mfg & 0x1f));
    out->manufacturer[3] = 0;
    for (int i = 0; i < 3; i++)
        if (!isupper((unsigned char)out->manufacturer[i])) out->manufacturer[i] = '?';
    out->product_code = (uint16_t)(d[10] | (d[11] << 8));
    out->serial = (uint32_t)d[12] | ((uint32_t)d[13] << 8) | ((uint32_t)d[14] << 16) | ((uint32_t)d[15] << 24);
    out->week = d[16];
    out->year = d[17] == 0xff ? 0 : 1990 + d[17];
    out->width_cm = d[21];
    out->height_cm = d[22];
    out->extensions = d[126];

    /* Established timings (EDID 1.3 §3.9): byte 0x23 then byte 0x24, MSB first. */
    add_established(out, 720, 400, 70, d[35] & 0x80);
    add_established(out, 640, 480, 60, d[35] & 0x20);
    add_established(out, 800, 600, 60, d[35] & 0x01);
    add_established(out, 1024, 768, 60, d[36] & 0x08);
    add_established(out, 800, 600, 75, d[36] & 0x40);
    add_established(out, 1280, 1024, 75, d[36] & 0x01);

    for (int i = 0; i < 4; i++) parse_descriptor(d + 54 + i * 18, out);

    out->valid = out->checksum_ok && out->nmodes > 0;
    if (out->checksum_ok && out->nmodes == 0)
        snprintf(out->problem, sizeof out->problem, "no usable timing descriptors");
    return out->valid;
}

void edid_info_init(edid_info *e)
{
    memset(e, 0, sizeof *e);
    e->preferred = -1;
}

bool edid_read_file(const char *path, edid_info *out)
{
    size_t len = 0;
    char *raw = ml_read_file(path, &len);
    if (!raw) { edid_info_init(out); snprintf(out->problem, sizeof out->problem, "cannot read %s", path); return false; }
    bool ok = edid_parse((const uint8_t *)raw, len, out);
    ml_free(raw);
    return ok;
}

int edid_refresh_hz(const edid_mode *m) { return m ? (int)((m->refresh_mhz + 500) / 1000) : 0; }

const edid_mode *edid_preferred(const edid_info *e)
{
    return (e->preferred >= 0 && e->preferred < e->nmodes) ? &e->modes[e->preferred] : NULL;
}

const edid_mode *edid_best_mode(const edid_info *e, int max_w, int max_h)
{
    const edid_mode *pref = edid_preferred(e);
    if (pref && (!max_w || pref->w <= max_w) && (!max_h || pref->h <= max_h)) return pref;
    const edid_mode *best = NULL;
    int64_t best_score = -1;
    for (int i = 0; i < e->nmodes; i++) {
        const edid_mode *m = &e->modes[i];
        if (max_w && m->w > max_w) continue;
        if (max_h && m->h > max_h) continue;
        int64_t score = (int64_t)m->w * m->h * 1000 + m->refresh_mhz;
        if (score > best_score) { best_score = score; best = m; }
    }
    return best;
}

void edid_describe(const edid_info *e, char *out, size_t outlen)
{
    if (!e->valid) { snprintf(out, outlen, "invalid EDID: %s", e->problem[0] ? e->problem : "unknown"); return; }
    const edid_mode *p = edid_preferred(e);
    snprintf(out, outlen, "%s%s%s%dx%d%s%s@%d Hz (%d modes)",
             e->manufacturer, e->monitor_name[0] ? " " : "", e->monitor_name,
             p ? p->w : 0, p ? p->h : 0, p && p->interlaced ? "i" : "", "",
             p ? edid_refresh_hz(p) : 0, e->nmodes);
}
