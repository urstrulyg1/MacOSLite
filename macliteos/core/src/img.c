#include "ml/img.h"
#include "ml/util.h"
#include "ml/log.h"

/* ---------------- crc32 / adler32 (self-contained) ---------------- */
static uint32_t crc_tab[256];
static bool crc_ready;
static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_tab[i] = c;
    }
    crc_ready = true;
}
static uint32_t crc32_buf(uint32_t crc, const void *buf, size_t len)
{
    if (!crc_ready) crc_init();
    const uint8_t *p = buf;
    crc = ~crc;
    for (size_t i = 0; i < len; i++) crc = crc_tab[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
static uint32_t adler32_buf(const uint8_t *d, size_t n)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; i++) {
        a = (a + d[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

static bool put_be32(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    return fwrite(b, 1, 4, f) == 4;
}
static bool chunk(FILE *f, const char *type, const void *data, size_t len)
{
    if (!put_be32(f, (uint32_t)len)) return false;
    if (fwrite(type, 1, 4, f) != 4) return false;
    if (len && fwrite(data, 1, len, f) != len) return false;
    uint32_t crc = crc32_buf(0, type, 4);
    if (len) crc = crc32_buf(crc, data, len);
    return put_be32(f, crc);
}

bool ml_img_write_png(const char *path, const ml_surface *s)
{
    if (!path || !s) return false;
    FILE *f = fopen(path, "wb");
    if (!f) { ML_WARN("png: cannot open %s", path); return false; }
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);

    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(s->w >> 24); ihdr[1] = (uint8_t)(s->w >> 16);
    ihdr[2] = (uint8_t)(s->w >> 8);  ihdr[3] = (uint8_t)s->w;
    ihdr[4] = (uint8_t)(s->h >> 24); ihdr[5] = (uint8_t)(s->h >> 16);
    ihdr[6] = (uint8_t)(s->h >> 8);  ihdr[7] = (uint8_t)s->h;
    ihdr[8] = 8;    /* bit depth */
    ihdr[9] = 6;    /* colour type: RGBA */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, sizeof ihdr);

    /* raw scanlines, each prefixed with filter type 0 */
    size_t rowbytes = (size_t)s->w * 4;
    size_t rawlen = (rowbytes + 1) * (size_t)s->h;
    uint8_t *raw = ml_alloc(rawlen);
    for (int y = 0; y < s->h; y++) {
        uint8_t *out = raw + (size_t)y * (rowbytes + 1);
        out[0] = 0;
        const uint32_t *src = ml_surface_row((ml_surface *)s, y);
        for (int x = 0; x < s->w; x++) {
            uint32_t v = src[x];
            out[1 + x * 4 + 0] = (uint8_t)(v >> 16);   /* R */
            out[1 + x * 4 + 1] = (uint8_t)(v >> 8);    /* G */
            out[1 + x * 4 + 2] = (uint8_t)v;           /* B */
            out[1 + x * 4 + 3] = (uint8_t)(v >> 24);   /* A */
        }
    }

    /* IDAT = zlib stream made only of stored blocks (max 65535 bytes each) */
    size_t maxblocks = (rawlen + 65534) / 65535 + 1;
    size_t idatcap = rawlen + maxblocks * 5 + 8;
    uint8_t *idat = ml_alloc(idatcap);
    size_t n = 0;
    idat[n++] = 0x78; idat[n++] = 0x01;      /* zlib header, no compression */
    size_t off = 0;
    while (off < rawlen) {
        size_t blk = ML_MIN((size_t)65535, rawlen - off);
        bool last = (off + blk >= rawlen);
        idat[n++] = last ? 1 : 0;
        idat[n++] = (uint8_t)(blk & 0xFF);
        idat[n++] = (uint8_t)((blk >> 8) & 0xFF);
        idat[n++] = (uint8_t)(~blk & 0xFF);
        idat[n++] = (uint8_t)((~blk >> 8) & 0xFF);
        memcpy(idat + n, raw + off, blk);
        n += blk;
        off += blk;
    }
    if (rawlen == 0) { idat[n++] = 1; idat[n++] = 0; idat[n++] = 0; idat[n++] = 0xff; idat[n++] = 0xff; }
    uint32_t ad = adler32_buf(raw, rawlen);
    idat[n++] = (uint8_t)(ad >> 24); idat[n++] = (uint8_t)(ad >> 16);
    idat[n++] = (uint8_t)(ad >> 8);  idat[n++] = (uint8_t)ad;
    chunk(f, "IDAT", idat, n);
    chunk(f, "IEND", NULL, 0);

    ml_free(raw);
    ml_free(idat);
    bool ok = fclose(f) == 0;
    return ok;
}

bool ml_img_write_ppm(const char *path, const ml_surface *s)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
    for (int y = 0; y < s->h; y++) {
        const uint32_t *row = ml_surface_row((ml_surface *)s, y);
        for (int x = 0; x < s->w; x++) {
            uint8_t b[3] = { (uint8_t)(row[x] >> 16), (uint8_t)(row[x] >> 8), (uint8_t)row[x] };
            fwrite(b, 1, 3, f);
        }
    }
    fclose(f);
    return true;
}

bool ml_img_write_bmp(const char *path, const ml_surface *s)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint32_t stride = (uint32_t)s->w * 3;
    stride = (stride + 3) & ~3u;
    uint32_t pixoff = 54;
    uint32_t size = pixoff + stride * (uint32_t)s->h;
    uint8_t hdr[54] = { 0 };
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = size & 0xFF; hdr[3] = (size >> 8) & 0xFF; hdr[4] = (size >> 16) & 0xFF; hdr[5] = (size >> 24) & 0xFF;
    hdr[10] = pixoff & 0xFF;
    hdr[14] = 40;                       /* DIB header size */
    hdr[18] = s->w & 0xFF; hdr[19] = (s->w >> 8) & 0xFF; hdr[20] = (s->w >> 16) & 0xFF; hdr[21] = (s->w >> 24) & 0xFF;
    uint32_t hh = (uint32_t)s->h;
    hdr[22] = hh & 0xFF; hdr[23] = (hh >> 8) & 0xFF; hdr[24] = (hh >> 16) & 0xFF; hdr[25] = (hh >> 24) & 0xFF;
    hdr[26] = 1; hdr[28] = 24;
    uint32_t dsz = stride * hh;
    hdr[34] = dsz & 0xFF; hdr[35] = (dsz >> 8) & 0xFF; hdr[36] = (dsz >> 16) & 0xFF; hdr[37] = (dsz >> 24) & 0xFF;
    fwrite(hdr, 1, 54, f);
    uint8_t *line = ml_zalloc(stride);
    for (int y = s->h - 1; y >= 0; y--) {
        const uint32_t *row = ml_surface_row((ml_surface *)s, y);
        for (int x = 0; x < s->w; x++) {
            line[x * 3 + 0] = (uint8_t)row[x];
            line[x * 3 + 1] = (uint8_t)(row[x] >> 8);
            line[x * 3 + 2] = (uint8_t)(row[x] >> 16);
        }
        fwrite(line, 1, stride, f);
    }
    ml_free(line);
    fclose(f);
    return true;
}

bool ml_img_write(const char *path, const ml_surface *s)
{
    if (!path) return false;
    if (ml_str_endswith(path, ".ppm")) return ml_img_write_ppm(path, s);
    if (ml_str_endswith(path, ".bmp")) return ml_img_write_bmp(path, s);
    return ml_img_write_png(path, s);
}

/* ---------------- tiny PNG reader (stored / fixed-Huffman) ---------------- */
typedef struct { const uint8_t *p; size_t n, pos; int bitpos; } bitr;
static int bit_get(bitr *b)
{
    if (b->pos >= b->n) return -1;
    int v = (b->p[b->pos] >> b->bitpos) & 1;
    if (++b->bitpos == 8) { b->bitpos = 0; b->pos++; }
    return v;
}
static int bit_getn(bitr *b, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        int x = bit_get(b);
        if (x < 0) return -1;
        v |= x << i;
    }
    return v;
}

static const uint8_t cl_order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

static bool build_huff(uint16_t *counts, uint16_t *syms, const uint8_t *lens, int n)
{
    memset(counts, 0, 16 * 2);
    for (int i = 0; i < n; i++) counts[lens[i]]++;
    counts[0] = 0;
    uint16_t offs[16];
    offs[0] = 0;
    for (int i = 1; i < 16; i++) offs[i] = offs[i - 1] + counts[i - 1];
    for (int i = 0; i < n; i++)
        if (lens[i]) syms[offs[lens[i]]++] = (uint16_t)i;
    return true;
}
static int huff_decode(bitr *b, const uint16_t *counts, const uint16_t *syms)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        int bit = bit_get(b);
        if (bit < 0) return -1;
        code |= bit;
        int cnt = counts[len];
        if (code - first < cnt) return syms[index + (code - first)];
        index += cnt;
        first = (first + cnt) << 1;
        code <<= 1;
    }
    return -1;
}

static bool inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap)
{
    bitr b = { in, inlen, 0, 0 };
    size_t op = 0;
    int final = 0;
    static const uint16_t lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint8_t lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint8_t dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    do {
        final = bit_getn(&b, 1);
        int type = bit_getn(&b, 2);
        if (type == 0) {
            b.pos += (b.bitpos ? 1 : 0); b.bitpos = 0;
            if (b.pos + 4 > b.n) return false;
            uint16_t len = in ? (uint16_t)(b.p[b.pos] | (b.p[b.pos + 1] << 8)) : 0;
            b.pos += 4;
            if (b.pos + len > b.n || op + len > outcap) return false;
            memcpy(out + op, b.p + b.pos, len);
            op += len; b.pos += len;
        } else if (type == 1 || type == 2) {
            uint16_t lcounts[16], lsyms[320], dcounts[16], dsyms[32];
            if (type == 1) {
                uint8_t lens[320];
                for (int i = 0; i < 144; i++) lens[i] = 8;
                for (int i = 144; i < 256; i++) lens[i] = 9;
                for (int i = 256; i < 280; i++) lens[i] = 7;
                for (int i = 280; i < 320; i++) lens[i] = 8;
                build_huff(lcounts, lsyms, lens, 320);
                uint8_t dl[30];
                for (int i = 0; i < 30; i++) dl[i] = 5;
                build_huff(dcounts, dsyms, dl, 30);
            } else {
                int hlit = bit_getn(&b, 5) + 257;
                int hdist = bit_getn(&b, 5) + 1;
                int hclen = bit_getn(&b, 4) + 4;
                uint8_t cl[19] = { 0 };
                for (int i = 0; i < hclen; i++) cl[cl_order[i]] = (uint8_t)bit_getn(&b, 3);
                uint16_t ccounts[16], csyms[19];
                build_huff(ccounts, csyms, cl, 19);
                uint8_t lens[320];
                int i = 0;
                while (i < hlit + hdist) {
                    int sym = huff_decode(&b, ccounts, csyms);
                    if (sym < 0) return false;
                    if (sym < 16) { lens[i++] = (uint8_t)sym; }
                    else if (sym == 16) {
                        int rep = bit_getn(&b, 2) + 3;
                        uint8_t prev = i ? lens[i - 1] : 0;
                        while (rep-- && i < hlit + hdist) lens[i++] = prev;
                    } else if (sym == 17) {
                        int rep = bit_getn(&b, 3) + 3;
                        while (rep-- && i < hlit + hdist) lens[i++] = 0;
                    } else {
                        int rep = bit_getn(&b, 7) + 11;
                        while (rep-- && i < hlit + hdist) lens[i++] = 0;
                    }
                }
                build_huff(lcounts, lsyms, lens, hlit);
                build_huff(dcounts, dsyms, lens + hlit, hdist);
            }
            for (;;) {
                int sym = huff_decode(&b, lcounts, lsyms);
                if (sym < 0) return false;
                if (sym < 256) {
                    if (op >= outcap) return false;
                    out[op++] = (uint8_t)sym;
                } else if (sym == 256) break;
                else {
                    sym -= 257;
                    if (sym >= 29) return false;
                    int len = lbase[sym] + bit_getn(&b, lext[sym]);
                    int dsym = huff_decode(&b, dcounts, dsyms);
                    if (dsym < 0 || dsym >= 30) return false;
                    int dist = dbase[dsym] + bit_getn(&b, dext[dsym]);
                    if ((size_t)dist > op || op + (size_t)len > outcap) return false;
                    for (int k = 0; k < len; k++) { out[op] = out[op - (size_t)dist]; op++; }
                }
            }
        } else return false;
    } while (!final);
    return op == outcap || op > 0;
}

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

ml_surface *ml_img_read_png(const char *path)
{
    size_t len = 0;
    uint8_t *buf = (uint8_t *)ml_read_file(path, &len);
    if (!buf) return NULL;
    if (len < 8 || memcmp(buf, "\x89PNG\r\n\x1a\n", 8) != 0) { ml_free(buf); return NULL; }
    size_t pos = 8;
    int w = 0, h = 0, depth = 0, ctype = 0;
    ml_str idat;
    ml_str_init(&idat);
    ml_surface *out = NULL;
    while (pos + 8 <= len) {
        uint32_t clen = be32(buf + pos);
        char type[5] = { 0 };
        memcpy(type, buf + pos + 4, 4);
        const uint8_t *data = buf + pos + 8;
        if (pos + 8 + clen > len) break;
        if (!strcmp(type, "IHDR") && clen >= 13) {
            w = (int)be32(data); h = (int)be32(data + 4);
            depth = data[8]; ctype = data[9];
        } else if (!strcmp(type, "IDAT")) {
            ml_str_reserve(&idat, clen);
            memcpy(idat.p + idat.n, data, clen);
            idat.n += clen;
            idat.p[idat.n] = 0;
        } else if (!strcmp(type, "IEND")) break;
        pos += 12 + clen;
    }
    if (!w || !h || depth != 8 || (ctype != 6 && ctype != 2) || idat.n < 6) { ml_str_free(&idat); ml_free(buf); return NULL; }
    int ch = ctype == 6 ? 4 : 3;
    size_t rawlen = ((size_t)w * ch + 1) * (size_t)h;
    uint8_t *raw = ml_zalloc(rawlen);
    /* skip zlib header + trailing adler */
    if (!inflate((const uint8_t *)idat.p + 2, idat.n - 6, raw, rawlen)) { ml_free(raw); ml_str_free(&idat); ml_free(buf); return NULL; }
    out = ml_surface_new(w, h);
    uint8_t *prev = ml_zalloc((size_t)w * ch + 1);
    for (int y = 0; y < h; y++) {
        uint8_t *line = raw + (size_t)y * ((size_t)w * ch + 1);
        uint8_t ft = line[0];
        uint8_t *cur = line + 1;
        for (size_t x = 0; x < (size_t)w * ch; x++) {
            uint8_t a = x >= (size_t)ch ? cur[x - ch] : 0;
            uint8_t bb = prev[x];
            uint8_t cc = x >= (size_t)ch ? prev[x - ch] : 0;
            switch (ft) {
            case 1: cur[x] = (uint8_t)(cur[x] + a); break;
            case 2: cur[x] = (uint8_t)(cur[x] + bb); break;
            case 3: cur[x] = (uint8_t)(cur[x] + ((a + bb) >> 1)); break;
            case 4: {
                int p = a + bb - cc;
                int pa = abs(p - a), pb = abs(p - bb), pc = abs(p - cc);
                uint8_t pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? bb : cc);
                cur[x] = (uint8_t)(cur[x] + pr);
                break;
            }
            default: break;
            }
        }
        uint32_t *drow = ml_surface_row(out, y);
        for (int x = 0; x < w; x++) {
            uint8_t r = cur[x * ch + 0], g = cur[x * ch + 1], b = cur[x * ch + 2];
            uint8_t al = ch == 4 ? cur[x * ch + 3] : 255;
            drow[x] = ((uint32_t)al << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
        memcpy(prev, cur, (size_t)w * ch);
    }
    ml_free(prev);
    ml_free(raw);
    ml_str_free(&idat);
    ml_free(buf);
    ml_surface_damage_all(out);
    return out;
}
