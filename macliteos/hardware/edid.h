#ifndef MICA_EDID_H
#define MICA_EDID_H
#include "ml/common.h"

/* EDID 1.3/1.4 parser (spec §6 display validation).
 *
 * The only trustworthy source of "what is this panel and what modes does it
 * really accept" is the EDID the display hands over DDC. We parse it ourselves
 * instead of shelling out to a display daemon: 128 bytes in, modes out, no
 * dependency. Verified by tests/test_hardware.c against generated panels
 * (iMac11,2 1920x1080 and iMac11,3 2560x1440 blobs with correct checksums). */

typedef struct {
    int w, h;                  /* active pixels */
    int hblank, vblank;
    int pixclk_khz;
    int refresh_mhz;           /* refresh rate * 1000 (59940 == 59.94 Hz) */
    bool interlaced;
    bool hsync_positive, vsync_positive;
    bool preferred;            /* first detailed timing descriptor */
    bool from_established;     /* came from the legacy established-timings bits */
} edid_mode;

typedef struct {
    bool valid;
    char manufacturer[4];      /* PNP id, e.g. "APP" */
    uint16_t product_code;
    uint32_t serial;
    int week, year;            /* year already offset by 1990; 0 = model year unknown */
    char monitor_name[16];
    char serial_string[16];
    int width_cm, height_cm;   /* 0 when the panel lies (common on iMacs) */
    edid_mode modes[10];
    int nmodes;
    int preferred;             /* index into modes, or -1 */
    int extensions;
    bool checksum_ok;
    char problem[96];          /* why !valid, when applicable */
} edid_info;

void edid_info_init(edid_info *e);
bool edid_parse(const uint8_t *data, size_t len, edid_info *out);
bool edid_read_file(const char *path, edid_info *out);
int edid_refresh_hz(const edid_mode *m);                 /* rounded */
const edid_mode *edid_preferred(const edid_info *e);     /* NULL when unknown */
/* best mode not exceeding max_w/max_h (0 = no limit); prefers the panel's
 * preferred mode, then the largest area at the highest refresh. */
const edid_mode *edid_best_mode(const edid_info *e, int max_w, int max_h);
void edid_describe(const edid_info *e, char *out, size_t outlen);

#endif /* MICA_EDID_H */
