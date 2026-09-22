#ifndef ML_IMG_H
#define ML_IMG_H
#include "ml/common.h"
#include "ml/surface.h"

/* Image output with no external dependency.
 * PNG is written with *stored* (uncompressed) deflate blocks, so we do not need
 * zlib on the build host and the target rootfs stays smaller. Screenshots and
 * wallpapers are not bandwidth critical; correctness and zero deps are. */
bool ml_img_write_png(const char *path, const ml_surface *s);
bool ml_img_write_ppm(const char *path, const ml_surface *s);
bool ml_img_write_bmp(const char *path, const ml_surface *s);
bool ml_img_write(const char *path, const ml_surface *s);   /* by extension */
/* Minimal PNG decoder for wallpapers/icons shipped as PNG (stored or fixed
 * Huffman blocks only — enough for assets we generate ourselves). Returns NULL
 * for anything more complex, and the caller falls back to a solid colour. */
ml_surface *ml_img_read_png(const char *path);
#endif
