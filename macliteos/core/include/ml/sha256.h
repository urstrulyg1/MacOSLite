/* SHA-256 — the one cryptographic primitive MacLiteOS needs in-process.
 *
 * Why it lives in core: the driver resolver (maclite-drivers) verifies every
 * firmware, microcode and driver package against a digest from the trusted
 * catalog before it installs anything, and the verifier must work on a machine
 * with nothing installed — no openssl, no sha256sum binary, no python. A
 * self-contained 200-line implementation is smaller and safer than a shell-out
 * that may not exist, and it keeps the dependency list at libc.
 *
 * What this is NOT: a signature scheme. A digest proves the bytes match the
 * catalog entry; it does not prove who wrote the catalog. That check is done
 * separately (gpgv/openssl when present; see hardware/driver.c) and MacLiteOS
 * reports NOT TESTED rather than PASS when no verifier exists.
 */
#ifndef ML_SHA256_H
#define ML_SHA256_H
#include "ml/common.h"

typedef struct {
    uint32_t h[8];
    uint64_t len;           /* total bytes fed in */
    uint8_t buf[64];
    size_t buflen;
} ml_sha256;

void ml_sha256_init(ml_sha256 *s);
void ml_sha256_update(ml_sha256 *s, const void *data, size_t len);
void ml_sha256_final(ml_sha256 *s, uint8_t out[32]);

/* lowercase hex, NUL-terminated: out must hold 65 bytes */
void ml_sha256_hex(const uint8_t digest[32], char out[65]);
/* one-shot over a memory buffer */
void ml_sha256_buf(const void *data, size_t len, char out_hex[65]);
/* stream a file; returns false when the file cannot be read (out_hex untouched).
 * *bytes_out (may be NULL) receives the number of bytes hashed. */
bool ml_sha256_file(const char *path, char out_hex[65], uint64_t *bytes_out);
/* case-insensitive compare of two hex digests; false when either is not a
 * 64-character hex string (a malformed catalog entry must never "match") */
bool ml_sha256_hex_eq(const char *a, const char *b);
bool ml_sha256_hex_valid(const char *hex);

#endif /* ML_SHA256_H */
