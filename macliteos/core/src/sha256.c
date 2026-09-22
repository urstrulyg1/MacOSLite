/* SHA-256 (FIPS 180-4). Straightforward, unrolled only where it helps; this is
 * never on a render path — it runs when a driver package is verified. */
#include "ml/sha256.h"

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void block(ml_sha256 *s, const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROTR(w[i - 15], 7) ^ ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROTR(w[i - 2], 17) ^ ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

void ml_sha256_init(ml_sha256 *s)
{
    s->h[0] = 0x6a09e667u; s->h[1] = 0xbb67ae85u; s->h[2] = 0x3c6ef372u;
    s->h[3] = 0xa54ff53au; s->h[4] = 0x510e527fu; s->h[5] = 0x9b05688cu;
    s->h[6] = 0x1f83d9abu; s->h[7] = 0x5be0cd19u;
    s->len = 0;
    s->buflen = 0;
}

void ml_sha256_update(ml_sha256 *s, const void *data, size_t len)
{
    const uint8_t *p = data;
    s->len += len;
    if (s->buflen) {
        size_t need = 64 - s->buflen;
        size_t take = len < need ? len : need;
        memcpy(s->buf + s->buflen, p, take);
        s->buflen += take;
        p += take;
        len -= take;
        if (s->buflen == 64) { block(s, s->buf); s->buflen = 0; }
    }
    while (len >= 64) { block(s, p); p += 64; len -= 64; }
    if (len) { memcpy(s->buf, p, len); s->buflen = len; }
}

void ml_sha256_final(ml_sha256 *s, uint8_t out[32])
{
    uint64_t bits = s->len * 8;
    uint8_t pad = 0x80;
    ml_sha256_update(s, &pad, 1);
    uint8_t zero = 0;
    while (s->buflen != 56) ml_sha256_update(s, &zero, 1);
    uint8_t lenb[8];
    for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - i * 8));
    ml_sha256_update(s, lenb, 8);           /* s->buflen is 0 again here */
    for (int i = 0; i < 8; i++) {
        out[i * 4]     = (uint8_t)(s->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(s->h[i]);
    }
}

void ml_sha256_hex(const uint8_t digest[32], char out[65])
{
    static const char hexd[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2] = hexd[digest[i] >> 4];
        out[i * 2 + 1] = hexd[digest[i] & 15];
    }
    out[64] = 0;
}

void ml_sha256_buf(const void *data, size_t len, char out_hex[65])
{
    ml_sha256 s;
    uint8_t d[32];
    ml_sha256_init(&s);
    ml_sha256_update(&s, data, len);
    ml_sha256_final(&s, d);
    ml_sha256_hex(d, out_hex);
}

bool ml_sha256_file(const char *path, char out_hex[65], uint64_t *bytes_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    ml_sha256 s;
    uint8_t d[32];
    ml_sha256_init(&s);
    uint8_t buf[64 * 1024];
    uint64_t total = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        ml_sha256_update(&s, buf, n);
        total += n;
    }
    bool ok = !ferror(f);
    fclose(f);
    if (!ok) return false;
    ml_sha256_final(&s, d);
    ml_sha256_hex(d, out_hex);
    if (bytes_out) *bytes_out = total;
    return true;
}

bool ml_sha256_hex_valid(const char *hex)
{
    if (!hex) return false;
    for (int i = 0; i < 64; i++) {
        char c = hex[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!ok) return false;
    }
    return hex[64] == 0;
}

bool ml_sha256_hex_eq(const char *a, const char *b)
{
    if (!ml_sha256_hex_valid(a) || !ml_sha256_hex_valid(b)) return false;
    for (int i = 0; i < 64; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'F') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'F') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}
