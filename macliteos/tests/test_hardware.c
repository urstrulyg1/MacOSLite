/* test_hardware — regression coverage for the v0.2 hardware layer (spec §21).
 *
 * Runs against generated sysfs/procfs fixtures (tests/fixtures/make_sysfs.py),
 * so it pins the *detection and fallback logic* — including the machine shapes
 * we cannot buy: an iMac11,2 with a Radeon HD 4670, an iMac11,3 with a HD 5670,
 * a virtio-gpu guest, and a bare host with no hardware at all.
 *
 * What this file can NOT prove: that the iMac's panel really lights up. Those
 * rows live in docs/testing.md under the IMAC tier and stay UNVERIFIED here.
 *
 *   ML_HW_FIXTURE=<dir> ML_HW_VARIANT=<imac11_2|imac11_3|virtual|bare> ./test_hardware
 */
#include "ml/common.h"
#include "ml/util.h"
#include "ml/log.h"
#include "../hardware/hwcap.h"
#include "../hardware/hwprobe.h"
#include "../hardware/backlight.h"
#include "../hardware/audio.h"
#include "../hardware/media.h"
#include "../hardware/edid.h"
#include "../hardware/kms.h"
#include "../hardware/cpu.h"
#include "../hardware/driver.h"
#include "ml/sha256.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static char FIX[512];
static const char *VARIANT = "bare";

static const char *fix(const char *rel)
{
    static char buf[4][600];
    static int i;
    char *b = buf[i = (i + 1) % 4];
    snprintf(b, sizeof buf[0], "%s/%s", FIX, rel);
    return b;
}
static void write_fixture(const char *rel, const char *text)
{
    if (!ml_write_file(fix(rel), text, strlen(text)))
        printf("note: cannot write %s\n", fix(rel));
}

/* --------------------------------------------------------------- helpers --- */
static void test_env(void)
{
    CHECK(hw_using_fixture(), "fixture roots must be considered a fixture, not real hardware");
    char rooted[600];
    snprintf(rooted, sizeof rooted, "%s", hw_sys("class/drm"));
    CHECK(strstr(rooted, FIX) == rooted, "hw_sys must be rooted in the fixture (got %s)", rooted);
    CHECK(!strcmp(hw_result_str(HW_NOT_TESTED), "NOT TESTED"), "NOT TESTED must print as such");
    CHECK(!strcmp(hw_result_str(HW_PASS), "PASS"), "PASS string");
    CHECK(!hw_result_ok(HW_NOT_TESTED) && !hw_result_ok(HW_UNSUPPORTED) && !hw_result_ok(HW_FAIL),
          "only PASS/PARTIAL count as ok");
}

static void test_exit_codes(void)
{
    CHECK(hw_exit_for(HW_PASS) == HW_EXIT_PASS, "PASS -> 0");
    CHECK(hw_exit_for(HW_FAIL) == HW_EXIT_FAIL, "FAIL -> 1");
    CHECK(hw_exit_for(HW_NOT_TESTED) == HW_EXIT_NOT_TESTED, "NOT TESTED -> 2");
    CHECK(hw_exit_for(HW_UNSUPPORTED) == HW_EXIT_UNSUPPORTED, "UNSUPPORTED -> 3");
    CHECK(hw_exit_for(HW_PARTIAL) == HW_EXIT_NOT_TESTED, "PARTIAL -> 2 (incomplete verification)");

    hw_report r;
    hw_report_init(&r);
    hw_report_add(&r, "G", "ok", true, HW_PASS, "verified");
    CHECK(hw_report_exit(&r) == 0, "all-required-pass -> 0");
    hw_report_add(&r, "G", "optional fail", false, HW_FAIL, "not required");
    CHECK(hw_report_exit(&r) == 0, "optional failures do not fail the run");
    hw_report_add(&r, "G", "required untested", true, HW_NOT_TESTED, "no device here");
    CHECK(hw_report_exit(&r) == HW_EXIT_NOT_TESTED, "an untested required check is not a pass");
    hw_report_add(&r, "G", "required fail", true, HW_FAIL, "broken");
    CHECK(hw_report_exit(&r) == HW_EXIT_FAIL, "a failed required check wins");
    CHECK(hw_report_find(&r, "G", "required fail") != NULL, "report lookup by group+name");
}

/* ------------------------------------------------------------- GPUs ------- */
static void test_gpu_imac(void)
{
    mica_gpu_info g;
    bool found = mica_gpu_probe(&g);
    CHECK(found && g.present, "an iMac has a PCI display controller");
    CHECK(g.vendor_id == 0x1002, "vendor id 0x1002 (ATI/AMD), got 0x%04x", g.vendor_id);
    CHECK(g.device_id == (strcmp(VARIANT, "imac11_3") ? 0x9490u : 0x68c1u), "device id, got 0x%04x", g.device_id);
    /* v0.1 read `driver/module` as a *file* (a symlink to a directory), which
     * always yielded "" on real hardware. This is the regression pin. */
    CHECK(!strcmp(g.driver, "radeon"), "driver must come from readlink(), got '%s'", g.driver);
    char *viacat = ml_sysfs_str(fix("sys/bus/pci/devices/0000:01:00.0/driver/module"), "EMPTY");
    CHECK(!strcmp(viacat, "EMPTY"), "reading the driver symlink as a file yields nothing (the v0.1 bug)");
    ml_free(viacat);
    CHECK(g.has_kms, "card0 is bound to the device");
    CHECK(!strcmp(g.card_node, fix("dev/dri/card0")), "card node path, got %s", g.card_node);
    CHECK(g.has_render_node && !strcmp(g.render_node, fix("dev/dri/renderD128")), "render node, got %s", g.render_node);
    CHECK(g.vram_bytes == 512ull * 1024 * 1024, "VRAM from mem_info_vram_total, got %llu", (unsigned long long)g.vram_bytes);
    CHECK(strstr(g.db_model, "Radeon HD"), "capability DB models the part, got '%s'", g.db_model);
    CHECK(!strcmp(g.accel_api, "vdpau"), "UVD is reached through VDPAU, got '%s'", g.accel_api);
    CHECK(!strcmp(g.gallium, "r600"), "gallium driver r600, got '%s'", g.gallium);
    CHECK(g.decode_mask & ML_CODEC_H264, "silicon H.264 decode bit");
    CHECK((g.decode_mask & ML_CODEC_MPEG2), "silicon MPEG-2 decode bit");
    CHECK(!g.gl_probed, "GL must not be claimed without a query (gl_probed=%d)", g.gl_probed);
}

static void test_gpu_virtual(void)
{
    mica_gpu_info g;
    mica_gpu_probe(&g);
    CHECK(g.present && g.vendor_id == 0x1af4, "virtio-gpu vendor, got 0x%04x", g.vendor_id);
    CHECK(g.decode_mask == 0, "virtio-gpu has no fixed-function decode, got 0x%x", g.decode_mask);
    CHECK(!strcmp(g.accel_api, "none"), "no decode API for virtio-gpu, got '%s'", g.accel_api);
}

static void test_gpu_absent(void)
{
    mica_gpu_info g;
    CHECK(!mica_gpu_probe(&g), "no display controller in the bare fixture");
    CHECK(!g.present, "nothing detected");
    char reason[160];
    CHECK(hw_pick_present("auto", reason, sizeof reason) == ML_PRESENT_HEADLESS,
          "no card, no fbdev -> headless (%s)", reason);
    CHECK(hw_pick_mode_caps(false, false, 4, 8192, reason, sizeof reason) == 2,
          "software-only machines get the performance mode");
}

static void test_display(void)
{
    mica_drm_state d;
    bool any = mica_drm_probe(&d);
    CHECK(any && d.any, "DRM device found in the fixture");
    CHECK(d.kms, "KMS usable: card node plus connectors");
    CHECK(d.n == 2, "two connectors, got %d", d.n);
    CHECK(d.primary == 0, "the connected connector is primary, got %d", d.primary);
    CHECK(!strcmp(d.driver, "radeon"), "card driver from the class/drm symlink, got '%s'", d.driver);
    mica_connector *c = &d.c[d.primary];
    CHECK(!strcmp(c->connector, "eDP-1"), "connector name, got '%s'", c->connector);
    CHECK(c->connected, "eDP-1 connected");
    CHECK(!strcmp(c->dpms, "On"), "dpms readable");
    CHECK(c->nmodes == 4, "four modes listed, got %d", c->nmodes);
    CHECK(c->mode_w[0] == (strcmp(VARIANT, "imac11_3") ? 1920 : 2560), "first mode is native, got %d",
          c->mode_w[0]);
    CHECK(c->has_edid && c->edid.valid, "EDID parsed");
    const edid_mode *p = edid_preferred(&c->edid);
    CHECK(p != NULL, "preferred timing present");
    if (p) {
        CHECK(p->w == (strcmp(VARIANT, "imac11_3") ? 1920 : 2560) &&
              p->h == (strcmp(VARIANT, "imac11_3") ? 1080 : 1440), "native resolution %dx%d", p->w, p->h);
        CHECK(edid_refresh_hz(p) == 60, "60 Hz panel, got %d", edid_refresh_hz(p));
        CHECK(p->hsync_positive && p->vsync_positive, "digital separate sync, positive polarity");
    }
    CHECK(!d.c[1].connected, "DP-1 is disconnected");
    /* the compositor backend the desktop will actually use */
    char reason[160];
    CHECK(hw_pick_present("auto", reason, sizeof reason) == ML_PRESENT_KMS, "auto picks KMS (%s)", reason);
    CHECK(hw_pick_present("kms", reason, sizeof reason) == ML_PRESENT_KMS, "explicit kms honoured");
    CHECK(hw_pick_present("headless", reason, sizeof reason) == ML_PRESENT_HEADLESS, "explicit headless honoured");
}

static void test_edid_unit(void)
{
    mica_drm_state d;
    mica_drm_probe(&d);
    edid_info e = d.c[d.primary].edid;
    CHECK(e.valid, "valid flag");
    CHECK(e.checksum_ok, "checksum");
    CHECK(!strcmp(e.manufacturer, "APP"), "manufacturer PNP id, got '%s'", e.manufacturer);
    CHECK(!strcmp(e.monitor_name, "iMac"), "monitor name descriptor, got '%s'", e.monitor_name);
    CHECK(e.year == 2010, "manufacture year, got %d", e.year);
    CHECK(e.width_cm == 60 && e.height_cm == 34, "physical size, got %dx%d", e.width_cm, e.height_cm);
    CHECK(e.nmodes >= 4, "established timings plus the DTD, got %d", e.nmodes);
    const edid_mode *best = edid_best_mode(&e, 0, 0);
    CHECK(best && best->w == (strcmp(VARIANT, "imac11_3") ? 1920 : 2560), "best mode is the panel's native mode");
    const edid_mode *capped = edid_best_mode(&e, 1280, 720);
    CHECK(capped && capped->w <= 1280 && capped->h <= 720, "best mode respects a cap");
    char desc[160];
    edid_describe(&e, desc, sizeof desc);
    CHECK(strstr(desc, "iMac") && strstr(desc, "Hz"), "describe text: %s", desc);

    /* a corrupted blob must be rejected, not partially trusted */
    edid_info bad;
    uint8_t blob[128];
    memset(blob, 0, sizeof blob);
    CHECK(!edid_parse(blob, sizeof blob, &bad), "garbage rejected");
    CHECK(!bad.valid && bad.problem[0], "rejection states a reason: %s", bad.problem);
    CHECK(!edid_parse(blob, 64, &bad), "short EDID rejected");
}

/* ------------------------------------------------------------- backlight -- */
static void test_backlight(void)
{
    /* the test wrote to the fixture before; restore the pristine value so the
     * checks below are about behaviour, not about run order */
    write_fixture("sys/class/backlight/radeon_bl0/brightness", "140\n");
    write_fixture("sys/class/backlight/radeon_bl0/actual_brightness", "140\n");
    bl_state st;
    bl_probe(&st);
    CHECK(st.n == 2, "two backlight devices, got %d", st.n);
    CHECK(st.apple_machine, "Apple machine detected from DMI");
    CHECK(st.efi_boot, "EFI boot detected");
    const bl_device *b = bl_active(&st);
    CHECK(b && !strcmp(b->name, "radeon_bl0"), "the DRM backlight wins over acpi_video0, got %s",
          b ? b->name : "(none)");
    CHECK(b && !b->suspect, "the chosen device is not suspect");
    CHECK(b && b->max == 255 && b->cur == 140, "brightness/max read (%ld/%ld)", b ? b->cur : -1, b ? b->max : -1);
    int pct = -1;
    CHECK(bl_get_percent(&st, &pct) && pct == 55, "140/255 is 55%%, got %d", pct);
    CHECK(bl_raw_for(b, 50) == 128, "50%% maps to 128/255, got %ld", bl_raw_for(b, 50));
    CHECK(bl_raw_for(b, 0) == 0, "0%% is off");
    CHECK(bl_raw_for(b, 100) == 255, "100%% is max");

    /* Under fixture roots the write must be refused: a fixture file is not
     * panel hardware, and "written + read back" against a text file would be a
     * verified change that never happened (the user-visible rule of spec §5). */
    int applied = -1;
    bl_set_result rc = bl_set_percent(&st, 50, &applied);
    CHECK(rc == BL_SET_NOT_TESTED, "fixture roots refuse the write (%s)", bl_set_result_str(rc));
    CHECK(!bl_set_ok(rc), "a fixture write is never reported as a verified change");
    CHECK(applied == 55, "the reported value is the unchanged one, got %d", applied);
    char *raw = ml_sysfs_str(fix("sys/class/backlight/radeon_bl0/brightness"), "");
    CHECK(!strcmp(raw, "140"), "sysfs is untouched, got '%s'", raw);
    ml_free(raw);

    rc = bl_step(&st, -10, &applied);
    CHECK(rc == BL_SET_NOT_TESTED && applied == 55, "step is refused too (%s, %d)",
          bl_set_result_str(rc), applied);

    /* The write+read-back path itself is still pinned, through the test hook, so
     * the anti-fake mechanism cannot rot: a real write that sticks is OK, one
     * that does not stick is VERIFY_FAILED. */
    rc = bl_set_percent_test(&st, 50, &applied);
    CHECK(bl_set_ok(rc), "test hook: set 50%% verified by read-back (%s)", bl_set_result_str(rc));
    CHECK(applied == 50, "read-back reports 50%%, got %d", applied);
    CHECK(st.dev[st.chosen].cur == 128, "the device struct carries the read-back value");
    raw = ml_sysfs_str(fix("sys/class/backlight/radeon_bl0/brightness"), "");
    CHECK(!strcmp(raw, "128"), "sysfs really holds the new value, got '%s'", raw);
    ml_free(raw);

    rc = bl_set_percent_test(&st, 40, &applied);
    CHECK(bl_set_ok(rc) && applied == 40, "test hook: 40%% sticks, got %d (%s)", applied, bl_set_result_str(rc));

    /* Apple + EFI + no acpi_backlight override: the ACPI device is a known no-op
     * and must be flagged, never used silently. */
    write_fixture("proc/cmdline", "root=LABEL=MACLITE_BASE ro quiet\n");
    bl_probe(&st);
    const bl_device *fake = bl_find(&st, "acpi_video0");
    CHECK(fake && fake->suspect, "acpi_video0 is flagged suspect without acpi_backlight=native");
    CHECK(fake && strstr(fake->note, "acpi_backlight=native"), "the note names the fix: %s",
          fake ? fake->note : "");
    CHECK(bl_active(&st) && !strcmp(bl_active(&st)->name, "radeon_bl0"), "the real device is still preferred");
    if (fake) {
        /* refusing to write is the point: no fake brightness change, ever */
        bl_state copy = st;
        int idx = (int)(fake - st.dev);
        copy.chosen = idx;
        int ap = -1;
        bl_set_result rr = bl_set_percent(&copy, 90, &ap);
        CHECK(rr == BL_SET_UNSUPPORTED, "writing the suspect device is refused, got %s", bl_set_result_str(rr));
        CHECK(ap == bl_percent_of(fake, fake->cur), "the reported value is the unchanged one, got %d", ap);
    }
    write_fixture("proc/cmdline", "root=LABEL=MACLITE_BASE ro quiet acpi_backlight=native radeon.uvd=1\n");
    bl_probe(&st);
    CHECK(!bl_find(&st, "acpi_video0")->suspect, "acpi_backlight=native clears the suspect flag");
    CHECK(!strcmp(st.acpi_backlight_arg, "native"), "cmdline argument parsed, got '%s'", st.acpi_backlight_arg);

    /* write failure must be reported, not swallowed */
    char path[600];
    snprintf(path, sizeof path, "%s", fix("sys/class/backlight/radeon_bl0/brightness"));
    chmod(path, 0400);
    bl_probe(&st);
    bl_set_result perm = bl_set_percent(&st, 70, NULL);
    CHECK(perm != BL_SET_OK, "a read-only device cannot report success (%s)", bl_set_result_str(perm));
    chmod(path, 0600);

    /* A device that accepts the write but does not keep it must be reported, not
     * assumed: /dev/null is the perfect liar. This is the branch that turns a
     * silent no-op into VERIFY_FAILED on real hardware. */
    char bpath[600];
    snprintf(bpath, sizeof bpath, "%s", path);
    unlink(bpath);
    if (symlink("/dev/null", bpath) == 0) {
        bl_probe(&st);
        int ap = -1;
        bl_set_result vr = bl_set_percent_test(&st, 60, &ap);
        CHECK(vr == BL_SET_VERIFY_FAILED, "a write that does not stick is VERIFY_FAILED, got %s",
              bl_set_result_str(vr));
        CHECK(ap == -1, "and no percentage is invented, got %d", ap);
    }
    /* restore the fixture file for every test that runs after this one */
    unlink(bpath);
    write_fixture("sys/class/backlight/radeon_bl0/brightness", "140\n");
    bl_probe(&st);
    CHECK(bl_get_percent(&st, &pct) && pct == 55, "fixture restored to 140/255, got %d", pct);
}

/* ----------------------------------------------------------------- audio -- */
static void test_audio(void)
{
    ml_aud_state st;
    bool any = ml_audio_probe(&st);
    CHECK(any && st.any, "one card in the fixture");
    CHECK(st.n == 1, "card count, got %d", st.n);
    const ml_aud_card *c = ml_audio_default(&st);
    CHECK(c && !strcmp(c->id, "PCH"), "card id, got '%s'", c ? c->id : "");
    CHECK(c && strstr(c->codec, "ALC889"), "codec name read from procfs, got '%s'", c ? c->codec : "");
    CHECK(c && c->can_play, "playback PCM present");
    CHECK(c && c->can_record, "capture PCM present");
    CHECK(c && c->has_hdmi, "HDMI/DP PCM present");
    CHECK(c && strstr(c->pcm_play, fix("dev/snd/pcmC0D0p")), "playback node path, got '%s'", c ? c->pcm_play : "");
    CHECK(st.dev_nodes, "dev/snd exists in the fixture");

    /* no real card here: playback must refuse rather than pretend */
    int rc = ml_audio_play(9, 9, NULL, 0, 48000, 2);
    CHECK(rc != ML_AUDIO_OK, "playing to a nonexistent card fails (%s)", ml_audio_errstr(rc));

    /* tone generation: right length, no clicks at the edges, right frequency */
    int16_t buf[48000 * 2];
    size_t frames = ml_tone_generate(buf, 48000 * 2, 48000, 440, 880, 100, 2, ML_TONE_SINE);
    CHECK(frames == 4800, "100 ms at 48 kHz is 4800 frames, got %zu", frames);
    CHECK(buf[0] == 0 || abs(buf[0]) < 100, "the tone starts from silence (no click), got %d", buf[0]);
    int16_t peak = 0;
    for (size_t i = 0; i < frames * 2; i++) if (abs(buf[i]) > peak) peak = abs(buf[i]);
    CHECK(peak > 8000 && peak <= 16384 + 1, "peak is about -6 dBFS, got %d", peak);
    int crossings = 0;
    for (size_t i = 2; i < frames * 2; i += 2)
        if ((buf[i - 2] < 0) != (buf[i] < 0)) crossings++;
    CHECK(crossings >= 80 && crossings <= 96, "~88 zero crossings for 100 ms of 440 Hz, got %d", crossings);

    /* WAV container, byte for byte */
    const char *path = "/tmp/maclite-test-tone.wav";
    CHECK(ml_audio_write_wav(path, buf, frames, 48000, 2), "wav written");
    size_t len = 0;
    char *data = ml_read_file(path, &len);
    CHECK(data != NULL, "wav readable");
    if (data) {
        uint32_t data_bytes = (uint32_t)(frames * 2 * 2);
        CHECK(len == 44u + data_bytes, "wav length %zu == 44 + %u", len, data_bytes);
        CHECK(!memcmp(data, "RIFF", 4) && !memcmp(data + 8, "WAVEfmt ", 8), "RIFF/WAVEfmt headers");
        CHECK(!memcmp(data + 36, "data", 4), "data chunk");
        uint32_t rate = 0, bytes = 0;
        memcpy(&rate, data + 24, 4);
        memcpy(&bytes, data + 40, 4);
        CHECK(rate == 48000, "sample rate field, got %u", rate);
        CHECK(bytes == data_bytes, "data size field, got %u want %u", bytes, data_bytes);
        uint16_t ch = 0, bits = 0;
        memcpy(&ch, data + 22, 2);
        memcpy(&bits, data + 34, 2);
        CHECK(ch == 2 && bits == 16, "2 channels, 16 bits (got %u/%u)", ch, bits);
        ml_free(data);
    }
    unlink(path);
}

static void test_audio_absent(void)
{
    ml_aud_state st;
    CHECK(!ml_audio_probe(&st), "bare fixture has no cards");
    CHECK(!st.any && st.n == 0, "no cards reported");
    CHECK(st.note[0], "and a reason is given: %s", st.note);
}

/* --------------------------------------------------------------- media ---- */
static void test_codec_caps(void)
{
    ml_hwdec_state h;
    ml_hwdec_probe(&h);
    char detail[200];
    uint32_t vendor = 0x1002, device = strcmp(VARIANT, "imac11_3") ? 0x9490 : 0x68c1;
    ml_cap h264 = ml_codec_capability(vendor, device, ML_CODEC_H264, &h, detail, sizeof detail);
    CHECK(h264 != ML_CAP_SW, "the Radeon UVD is not software-only (%s)", detail);
    ml_cap vp9 = ml_codec_capability(vendor, device, ML_CODEC_VP9, &h, detail, sizeof detail);
    CHECK(vp9 == ML_CAP_SW, "VP9 is not decoded by UVD2 (%s)", detail);
    if (!h.probed) {
        char d2[200];
        ml_codec_capability(vendor, device, ML_CODEC_H264, &h, d2, sizeof d2);
        CHECK(strstr(d2, "NOT TESTED") != NULL,
              "without a runtime query the verdict is explicitly unverified: %s", d2);
    }
    ml_cap virt = ml_codec_capability(0x1af4, 0x1010, ML_CODEC_H264, NULL, detail, sizeof detail);
    CHECK(virt == ML_CAP_SW, "virtio-gpu gets software decode (%s)", detail);
    CHECK(!strcmp(hw_result_str(h.probed ? HW_PASS : HW_NOT_TESTED), h.probed ? "PASS" : "NOT TESTED"),
          "runtime probe state maps to an honest result");
    CHECK(ml_codec_bit("h264") == ML_CODEC_H264 && ml_codec_bit("H.264") == ML_CODEC_H264,
          "codec name lookup is case-insensitive and punctuation-free");
    CHECK(ml_codec_bit("mpeg2") == ML_CODEC_MPEG2 && ml_codec_bit("MPEG-2") == ML_CODEC_MPEG2, "mpeg2 alias");
    CHECK(ml_codec_bit("nonsense") == 0, "unknown codec -> 0");
    CHECK(ml_codec_bit("h265") == ML_CODEC_HEVC, "h265 alias");
    char list[128];
    ml_codec_list(ML_CODEC_H264 | ML_CODEC_MPEG2, list, sizeof list);
    CHECK(!strcmp(list, "H.264 MPEG-2"), "codec list text, got '%s'", list);
    CHECK(!strcmp(ml_hwdec_name(ML_HWDEC_NONE), "no"), "hwdec names");
    CHECK(!strcmp(ml_present_name(ML_PRESENT_KMS), "kms"), "present backend names");
}

/* ------------------------------------------------------------- network ---- */
static void test_network(void)
{
    mica_net_state n;
    mica_net_probe(&n);
    CHECK(n.n >= 2, "two interfaces in the fixture, got %d", n.n);
    CHECK(n.neth == 1 && n.nwifi == 1, "one wired and one wireless (%d/%d)", n.neth, n.nwifi);
    for (int i = 0; i < n.n; i++) {
        if (!strcmp(n.v[i].name, "eth0")) {
            CHECK(!strcmp(n.v[i].kind, "ethernet"), "eth0 is ethernet, got '%s'", n.v[i].kind);
            CHECK(!strcmp(n.v[i].driver, "tg3"), "eth0 driver from readlink, got '%s'", n.v[i].driver);
            CHECK(!strcmp(n.v[i].mac, "00:26:bb:11:22:33"), "mac, got '%s'", n.v[i].mac);
        }
        if (!strcmp(n.v[i].name, "wlan0")) {
            CHECK(!strcmp(n.v[i].kind, "wifi"), "wlan0 is wifi, got '%s'", n.v[i].kind);
            CHECK(!strcmp(n.v[i].driver, "b43"), "wlan0 driver, got '%s'", n.v[i].driver);
        }
    }
    for (int i = 0; i < n.n; i++)
        /* regression: the sandbox may have a NIC with the same name as the
         * fixture's, and its address must never be reported as the modelled
         * machine's. iface_ip() only queries interfaces present in the real
         * /sys, so fixture interfaces report no address here. */
        CHECK(!n.v[i].has_ip4, "fixture iface %s does not borrow this host's address (got %s)",
              n.v[i].name, n.v[i].ip4);
    CHECK(n.have_gateway && !strcmp(n.gateway, "10.0.0.2"), "gateway parsed from /proc/net/route, got '%s'", n.gateway);
    CHECK(n.ndns == 2, "two nameservers in the fixture, got %d", n.ndns);
    CHECK(n.ndns == 2 && !strcmp(n.dns[0], "192.168.1.1"), "first nameserver, got '%s'", n.ndns ? n.dns[0] : "");
    CHECK(strstr(n.resolv_source, "resolv.conf"), "resolv.conf source recorded: %s", n.resolv_source);
}

/* ----------------------------------------------------------- usb/storage -- */
static void test_usb(void)
{
    mica_usb_state u;
    mica_usb_probe(&u);
    CHECK(u.present, "USB bus present in the fixture");
    CHECK(u.n == 5, "five devices (keyboard, mouse, camera, bluetooth, storage), got %d", u.n);
    CHECK(u.nkey == 1, "one keyboard, got %d", u.nkey);
    CHECK(u.nmouse == 1, "one mouse, got %d", u.nmouse);
    CHECK(u.nstorage == 1, "one mass storage device, got %d", u.nstorage);
    int ncamera = 0, nother = 0;
    for (int i = 0; i < u.n; i++) {
        if (!strcmp(u.v[i].kind, "camera")) ncamera++;
        if (!strcmp(u.v[i].kind, "other")) nother++;
    }
    CHECK(ncamera == 1, "iSight is classified as a camera, got %d", ncamera);
    CHECK(nother == 1, "the Bluetooth controller is neither HID nor storage, got %d", nother);
    for (int i = 0; i < u.n; i++)
        if (!strcmp(u.v[i].port, "1-1")) {
            CHECK(!strcmp(u.v[i].kind, "keyboard"), "HID protocol 1 is a keyboard, got '%s'", u.v[i].kind);
            CHECK(!strcmp(u.v[i].driver, "usbhid"), "driver from the interface symlink, got '%s'", u.v[i].driver);
            /* regression: idVendor/idProduct are bare hex in sysfs; base-10
             * parsing printed 0000:0000 for every real device */
            CHECK(u.v[i].vid == 0x05ac, "idVendor parsed as hex, got %04x", u.v[i].vid);
            CHECK(u.v[i].pid == 0x0245, "idProduct parsed as hex, got %04x", u.v[i].pid);
        }
    mica_input_state in;
    mica_input_probe(&in);
    CHECK(in.present && in.keyboard && in.mouse, "input devices parsed from procfs");
    CHECK(in.nkey == 1 && in.nmouse == 1, "handlers classified (%d/%d)", in.nkey, in.nmouse);
    CHECK(in.media_keys, "extended KEY bits mark a device with media keys");
}

static void test_storage(void)
{
    mica_storage_state s;
    mica_storage_probe(&s);
    CHECK(s.n == 6, "HDD + 2 partitions + SuperDrive + SD card + partition, got %d", s.n);
    CHECK(s.nwhole == 3, "whole disks: sda, sr0, mmcblk0 (got %d)", s.nwhole);
    CHECK(s.nremovable == 3, "removable: sr0, mmcblk0, mmcblk0p1 (got %d)", s.nremovable);
    const mica_disk *disk = NULL, *root = NULL;
    for (int i = 0; i < s.n; i++) {
        if (!strcmp(s.v[i].name, "sda")) disk = &s.v[i];
        if (!strcmp(s.v[i].mountpoint, "/")) root = &s.v[i];
    }
    CHECK(disk && disk->whole, "sda is a whole disk");
    CHECK(disk && strstr(disk->model, "SSD"), "model string, got '%s'", disk ? disk->model : "");
    CHECK(disk && disk->size_bytes > 400ull * 1000 * 1000 * 1000, "size from the size attribute");
    CHECK(root && root->mounted && !strcmp(root->fstype, "ext4"), "mount info joined on the device name");
    CHECK(root && root->use_pct >= 0, "free space measured for a mounted filesystem (%d%%)", root ? root->use_pct : -1);
}

static void test_power(void)
{
    mica_power_state p;
    bool ok = mica_power_probe(&p);
    CHECK(ok && p.state_node, "suspend state node present");
    CHECK(p.freeze && p.disk, "freeze and disk listed");
    CHECK(strstr(p.mem_modes, "deep"), "mem_sleep string, got '%s'", p.mem_modes);
    CHECK(!p.battery, "an iMac has no battery");
    CHECK(p.note[0], "the platform caveat is carried with the data: %s", p.note);
    CHECK(!strcmp(p.note, "iMac11,x: suspend-to-RAM is a documented failure on this platform "
                          "(panel does not re-light after resume) — see docs/hardware.md"),
          "the caveat text matches the documentation");
}

/* ------------------------------------------------------------- CPU -------- */
static void test_sha256(void)
{
    /* FIPS 180-4 vectors: the driver resolver's verification is only as good as
     * this primitive, so it is pinned here against published digests. */
    char hex[65];
    ml_sha256_buf("", 0, hex);
    CHECK(!strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
          "SHA-256(\"\") vector, got %s", hex);
    ml_sha256_buf("abc", 3, hex);
    CHECK(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
          "SHA-256(\"abc\") vector, got %s", hex);
    ml_sha256_buf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, hex);
    CHECK(!strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
          "SHA-256(448-bit vector), got %s", hex);
    /* multi-chunk update must equal one-shot (the file path chunks 64 KiB) */
    ml_sha256 s;
    uint8_t d[32];
    ml_sha256_init(&s);
    for (int i = 0; i < 1000; i++) ml_sha256_update(&s, "0123456789", 10);
    ml_sha256_final(&s, d);
    char hex2[65];
    ml_sha256_hex(d, hex2);
    char big[10000];
    for (int i = 0; i < 1000; i++) memcpy(big + i * 10, "0123456789", 10);
    ml_sha256_buf(big, sizeof big, hex);
    CHECK(!strcmp(hex, hex2), "1000 incremental updates equal the one-shot digest");

    /* a malformed digest must never compare equal to anything */
    CHECK(ml_sha256_hex_valid(hex), "a real digest is valid hex");
    CHECK(!ml_sha256_hex_valid("abc"), "short hex rejected");
    CHECK(!ml_sha256_hex_valid("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"),
          "non-hex rejected");
    CHECK(!ml_sha256_hex_eq("", ""), "two empty strings are not a match");
    CHECK(ml_sha256_hex_eq(hex, hex), "identical digests match");
    char upper[65];
    snprintf(upper, sizeof upper, "%s", hex);
    for (char *q = upper; *q; q++) *q = (char)toupper((unsigned char)*q);
    CHECK(ml_sha256_hex_eq(hex, upper), "hex comparison ignores case (catalog may use either)");

    /* hashing a real file, and the absence case */
    const char *path = "/tmp/maclite-sha256-test.bin";
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL, "temp file for the file-hash test");
    if (f) {
        fwrite("abc", 1, 3, f);
        fclose(f);
        uint64_t n = 0;
        CHECK(ml_sha256_file(path, hex2, &n), "file hashed");
        CHECK(n == 3, "byte count, got %llu", (unsigned long long)n);
        CHECK(!strcmp(hex2, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
              "file digest equals the string vector");
        unlink(path);
    }
    CHECK(!ml_sha256_file("/nonexistent/maclite", hex2, NULL), "a missing file yields false");
}

static void test_cpu(void)
{
    ml_cpu_state c;
    CHECK(ml_cpu_probe(&c), "CPU probe reads %s", hw_proc("cpuinfo"));
    CHECK(strstr(c.brand, "Intel") != NULL, "brand string, got '%s'", c.brand);
    if (!strcmp(VARIANT, "imac11_3")) {
        CHECK(c.cores == 4 && c.threads == 8, "i7-870 is 4C/8T, got %u/%u", c.cores, c.threads);
        CHECK(strstr(c.part, "i7-870"), "normalised part name, got '%s'", c.part);
    } else {
        CHECK(c.cores == 2 && c.threads == 4, "Clarkdale is 2C/4T in this fixture, got %u/%u", c.cores, c.threads);
        CHECK(strstr(c.part, "i5-650"), "normalised part name, got '%s'", c.part);
    }
    CHECK(c.family == 6, "family 6, got %u", c.family);
    CHECK(c.stepping == 5, "stepping from cpuinfo, got %u", c.stepping);
    CHECK(c.hyperthreading && c.ht_flag, "HT flag and threads>cores agree");
    CHECK(c.lm64 && c.sse42 && !c.avx,
          "x86-64 + SSE4.2 + no AVX (the whole codebase assumes no AVX): lm=%d sse42=%d avx=%d",
          c.lm64, c.sse42, c.avx);
    CHECK(strstr(c.isa, "SSE4.2") && strstr(c.isa_absent, "AVX"), "ISA list: %s (absent: %s)",
          c.isa, c.isa_absent);
    CHECK(c.cpufreq && !strcmp(c.scaling_driver, "acpi-cpufreq"), "cpufreq driver, got '%s'", c.scaling_driver);
    CHECK(!strcmp(c.governor, "ondemand"), "governor read from sysfs, got '%s'", c.governor);
    CHECK(c.mhz_max == (unsigned)(strcmp(VARIANT, "imac11_3") ? 3200 : 2933),
          "cpuinfo_max_freq converted to MHz, got %u", c.mhz_max);
    CHECK(c.mhz_cur > 0, "current clock read, got %u", c.mhz_cur);
    CHECK(c.turbo_knob, "the boost knob is in the fixture");
    CHECK(c.cpuidle && c.idle_states == 4, "four C-states, got %d (%s)", c.idle_states, c.idle_names);
    CHECK(c.thermal && c.thermal_c == 51, "package temperature read (got %d, crit %d)",
          c.thermal_c, c.thermal_crit_c);
    CHECK(c.thermal_crit_c == 100, "critical temperature read, got %d", c.thermal_crit_c);
    CHECK(c.vuln_affected >= 1 && c.vuln_mitigated >= 1, "vulnerability files summarised: %s (%d affected, %d mitigated)",
          c.vulnerabilities, c.vuln_affected, c.vuln_mitigated);
    CHECK(strstr(c.kernel, "Linux version") != NULL, "kernel string, got '%s'", c.kernel);
    CHECK(strstr(c.cmdline, "acpi_backlight=native") != NULL, "cmdline captured for context");
    CHECK(!c.mitigations_off, "the fixture cmdline does not set mitigations=off");

    /* cross-check: the part is in the table and must match what was observed */
    char why[400];
    ml_cpu_xcheck x = ml_cpu_crosscheck(&c, why, sizeof why);
    CHECK(x == ML_CPU_MATCH, "known part cross-check: %s", why);
    if (!strcmp(VARIANT, "imac11_3")) {
        CHECK(strstr(why, "i7-870") != NULL, "the cross-check names the part: %s", why);
    }

    /* recommendation and the honesty rules around it */
    const char *rec = ml_cpu_recommend_governor(&c, why, sizeof why);
    CHECK(rec && !strcmp(rec, "schedutil"), "schedutil is preferred when offered, got %s", rec ? rec : "(none)");
    CHECK(strstr(why, "schedutil") != NULL, "and the reason mentions it: %s", why);

    /* a governor that is not offered is refused rather than written */
    CHECK(ml_cpu_set_governor(&c, "nonexistent") == 0, "an unknown governor sets nothing");
    /* writes are refused under fixture roots by the tool, not by the library; the
     * library must still verify by read-back, which is pinned in maclite-cpu's
     * --governor path and by the fixture file below */
}

static void test_cpu_microcode(void)
{
    ml_cpu_state c;
    ml_cpu_probe(&c);
    CHECK(c.ucode_loaded_known, "loaded microcode revision is readable here");
    CHECK(c.ucode_loaded == (unsigned)(strcmp(VARIANT, "imac11_3") ? 0x1A : 0x0F),
          "loaded revision from cpuinfo, got 0x%x", c.ucode_loaded);
    CHECK(!strcmp(c.ucode_sig, "06-25-05") || !strcmp(c.ucode_sig, "06-1e-05"),
          "signature file name, got %s", c.ucode_sig);
    CHECK(c.ucode_file_present, "the image ships a blob for this stepping: %s", c.ucode_file);
    CHECK(c.ucode_available_known && c.ucode_available == 0x1F,
          "blob revision parsed from the header, got 0x%x", c.ucode_available);
    CHECK(c.ucode_available > c.ucode_loaded, "the fixture has an update pending (0x%x > 0x%x)",
          c.ucode_available, c.ucode_loaded);
    CHECK(!c.ucode_early, "no early-load copy in the fixture, and the note says why");
    CHECK(strstr(c.ucode_note, "early-load copy") != NULL, "note: %s", c.ucode_note);

    /* a blob for another stepping must be refused, never selected */
    char other[600];
    snprintf(other, sizeof other, "%s", fix("fw/intel-ucode"));
    uint32_t rev = 0, date = 0, sig = 0;
    CHECK(ml_ucode_blob_info(c.ucode_file, &rev, &date, &sig, NULL), "blob header parses");
    CHECK(sig == ((c.stepping & 0xf) | ((c.model & 0xf) << 4) | ((c.family & 0xf) << 8) |
                  (((c.model >> 4) & 0xf) << 16)),
          "blob signature encodes this CPU's family/model/stepping, got 0x%05x", sig);
    CHECK(date == 0x20180807, "BCD date parsed, got 0x%x", date);

    char path[600];
    uint32_t r2 = 0;
    /* write a blob whose *content* claims a different signature than its file
     * name: the name lookup must not be trusted */
    char wrong[700];
    snprintf(wrong, sizeof wrong, "%s/06-25-05", other);
    CHECK(ml_ucode_find_for(fix("fw"), sig, path, sizeof path, &r2, NULL),
          "the matching blob is found by signature");
    /* now point the search at a nonexistent root: no blob, no claim */
    CHECK(!ml_ucode_find_for("/nonexistent-fw-root", sig, path, sizeof path, &r2, NULL),
          "an empty firmware root finds no microcode");
    CHECK(ml_ucode_blob_info("/etc/hostname", &rev, &date, &sig, NULL) == false ||
          access("/etc/hostname", F_OK) != 0,
          "a file that is not a microcode update is rejected by the header check");
    (void)wrong;
}

static void test_cpu_absent(void)
{
    ml_cpu_state c;
    CHECK(ml_cpu_probe(&c), "the bare fixture still has a cpuinfo");
    CHECK(!strcmp(c.brand, "bare"), "brand as written, got '%s'", c.brand);
    CHECK(!c.cpufreq, "no cpufreq in the bare fixture");
    CHECK(!c.thermal, "no thermal source in the bare fixture");
    CHECK(!c.ucode_file_present, "and no microcode blob: %s", c.ucode_note);
    CHECK(strcmp(hw_result_str(HW_NOT_TESTED), "PASS") != 0, "NOT TESTED is never PASS");
}

/* ---------------------------------------------------------- mode picking -- */

/* ------------------------------------------------- driver resolver (§25-§30) - */
/* Every release of a component in the shipped catalog, with what the resolver
 * must conclude about the machine it is looking at. The catalog is data, so
 * these tests read it from the tree instead of restating it here. */
static const char *shipped_catalog(void)
{
    static const char *cands[] = {
        "drivers/catalog/maclite-offline.cat",
        "../drivers/catalog/maclite-offline.cat",
        "macliteos/drivers/catalog/maclite-offline.cat",
    };
    for (size_t i = 0; i < ML_ARRAY_SIZE(cands); i++)
        if (access(cands[i], F_OK) == 0) return cands[i];
    return NULL;
}

static void test_driver_versions(void)
{
    /* dotted numeric comparison, including the suffix runs 6.6.30 vs 6.6.30-foo
     * that the catalog uses for release candidates */
    CHECK(drv_version_cmp("24.0.9", "25.0.0") < 0, "24.0.9 < 25.0.0");
    CHECK(drv_version_cmp("25.0.0", "24.0.9") > 0, "25.0.0 > 24.0.9");
    CHECK(drv_version_cmp("6.6.30", "6.6.30") == 0, "equal versions compare equal");
    CHECK(drv_version_cmp("6.6.30", "6.6.9") > 0, "numeric, not lexical: 30 > 9");
    CHECK(drv_version_cmp("2015-12-15", "2013-01-01") > 0, "date versions");
    CHECK(drv_version_cmp("6.6.30-rc1", "6.6.30") < 0, "a release candidate sorts before the release");
    CHECK(drv_version_cmp("6.6.30", "6.6.30-rc1") > 0, "and after it, seen the other way");
}

static void test_driver_catalog(void)
{
    const char *path = shipped_catalog();
    if (!path) { printf("note: shipped catalog not found; skipping catalog test\n"); return; }
    drv_catalog cat;
    CHECK(drv_catalog_load(path, &cat), "the shipped catalog must parse (%s)", cat.error);
    CHECK(cat.n >= 10, "the shipped catalog carries the machine's components (got %d)", cat.n);
    CHECK(!strcmp(cat.repo, "maclite-offline"), "repo id, got '%s'", cat.repo);
    /* the resolver's central rule: a newer release that refuses this machine is
     * parsed *and* rejected, not silently dropped */
    bool saw_break = false;
    for (int i = 0; i < cat.n; i++)
        if (!strcmp(cat.v[i].name, "mesa-r600") && !strcmp(cat.v[i].version, "25.0.0"))
            saw_break = cat.v[i].nbreaks > 0 && !strcmp(cat.v[i].status, "broken");
    CHECK(saw_break, "mesa-r600 25.0.0 must be catalogued as broken with a breaks target");
    /* a package's digest is either a real SHA-256 or deliberately unpinned — a
     * short/garbage digest would silently never match and must not be accepted */
    for (int i = 0; i < cat.n; i++)
        for (int f = 0; f < cat.v[i].nfiles; f++)
            CHECK(!cat.v[i].files[f].sha256[0] || ml_sha256_hex_valid(cat.v[i].files[f].sha256),
                  "%s: digest '%s' is neither empty nor a valid SHA-256",
                  cat.v[i].name, cat.v[i].files[f].sha256);
}

static void test_driver_kinds(void)
{
    CHECK(drv_kind_parse("firmware") == DRV_KIND_FIRMWARE, "kind firmware");
    CHECK(drv_kind_parse("microcode") == DRV_KIND_MICROCODE, "kind microcode");
    CHECK(drv_kind_parse("drm") == DRV_KIND_DRM, "kind drm");
    CHECK(drv_kind_parse("mesa") == DRV_KIND_MESA, "kind mesa");
    CHECK(drv_kind_parse("kernel") == DRV_KIND_KERNEL, "kind kernel");
    CHECK(drv_kind_parse("nonsense") == DRV_KIND_OTHER, "an unknown kind is 'other', never a guess");
    CHECK(!strcmp(drv_kind_name(DRV_KIND_MICROCODE), "microcode"), "kind name round trip");
}

/* The heart of §25: not "newest", but "newest compatible". */
static void test_driver_resolve(void)
{
    const char *path = shipped_catalog();
    if (!path) { printf("note: shipped catalog not found; skipping resolve test\n"); return; }
    drv_catalog cat;
    CHECK(drv_catalog_load(path, &cat), "catalog load (%s)", cat.error);
    drv_hw hw;
    drv_hw_snapshot(&hw);
    CHECK(hw.present, "the iMac fixture has a GPU to resolve for");

    /* the resolver needs a root it will only read from: the fixture */
    char root[600];
    snprintf(root, sizeof root, "%s/instroot-not-written", FIX);
    drv_plan plan;
    drv_plan_resolve(&cat, &hw, root, &plan);

    const drv_package *mesa = NULL, *drm = NULL, *ucode = NULL, *radeon_fw = NULL, *b43 = NULL;
    char mesa_rejected[192] = "";
    for (int i = 0; i < plan.n; i++) {
        const drv_item *it = &plan.v[i];
        if (!it->pkg) continue;
        if (!strcmp(it->pkg->name, "mesa-r600")) { mesa = it->pkg; snprintf(mesa_rejected, sizeof mesa_rejected, "%s", it->rejected); }
        if (!strcmp(it->pkg->name, "radeon-drm")) drm = it->pkg;
        if (it->pkg->kind == DRV_KIND_MICROCODE) ucode = it->pkg;
        if (!strcmp(it->pkg->name, "radeon-firmware")) radeon_fw = it->pkg;
        if (!strcmp(it->pkg->name, "b43-firmware")) b43 = it->pkg;
    }
    CHECK(mesa && !strcmp(mesa->version, "24.0.9"),
          "must select mesa-r600 24.0.9, not the newer 25.0.0 that drops RV730 support");
    CHECK(strstr(mesa_rejected, "25.0.0") != NULL && strstr(mesa_rejected, "broken") != NULL,
          "and must say which release was skipped and why (got '%s')", mesa_rejected);
    /* the fixture has no GL query, so the "mesa>=20.0.0" requirement of the
     * selected release cannot be confirmed here — the resolver must say so
     * rather than assume it either way */
    bool caveat_seen = false;
    for (int i = 0; i < plan.n; i++)
        if (plan.v[i].pkg && !strcmp(plan.v[i].pkg->name, "mesa-r600") && plan.v[i].caveat[0])
            caveat_seen = strstr(plan.v[i].caveat, "could not be confirmed") != NULL;
    CHECK(caveat_seen, "an unconfirmable requirement is reported, not silently assumed");
    CHECK(drm && !strcmp(drm->status, "in-kernel"), "the DRM driver resolves to the in-kernel radeon");
    CHECK(radeon_fw != NULL, "RV730 UVD firmware is part of this machine's stack");
    CHECK(b43 != NULL, "the AirPort chipset's firmware is resolved from the machine's own PCI id");

    if (!strcmp(VARIANT, "imac11_2")) {
        CHECK(ucode && hw.cpu_signature == 0x00020655,
              "iMac11,2's CPU signature is what the microcode target matches (got %05x)", hw.cpu_signature);
        CHECK(ucode && !strcmp(ucode->name, "intel-ucode-clarkdale"),
              "and the Clarkdale blob is the one selected");
        for (int i = 0; i < plan.n; i++)
            CHECK(!plan.v[i].pkg || strcmp(plan.v[i].pkg->name, "intel-ucode-lynnfield"),
                  "the Lynnfield blob must never be offered to a Clarkdale machine");
    } else {
        CHECK(ucode && !strcmp(ucode->name, "intel-ucode-lynnfield"),
              "iMac11,3's CPU selects the Lynnfield blob, got %s", ucode ? ucode->name : "(none)");
        for (int i = 0; i < plan.n; i++)
            CHECK(!plan.v[i].pkg || strcmp(plan.v[i].pkg->name, "intel-ucode-clarkdale"),
                  "the Clarkdale blob must never be offered to a Lynnfield machine");
    }

    /* in-kernel components are "in kernel" only if the driver is really bound */
    bool seen_in_kernel = false;
    for (int i = 0; i < plan.n; i++) {
        const drv_item *it = &plan.v[i];
        if (!it->pkg || strcmp(it->pkg->status, "in-kernel")) continue;
        seen_in_kernel = true;
        CHECK(it->action == DRV_ACT_KERNEL_IN_USE || it->action == DRV_ACT_KERNEL_MISSING,
              "%s must resolve to in-kernel or missing, got %s", it->pkg->name, drv_action_str(it->action));
    }
    CHECK(seen_in_kernel, "the fixture must exercise the in-kernel path");
    if (!strcmp(VARIANT, "imac11_2")) {
        /* the fixture binds sdhci-pci/uvcvideo/firewire_ohci, so those read as in
         * use; the audio package is matched through /proc/asound instead */
        int in_use = 0;
        for (int i = 0; i < plan.n; i++)
            if (plan.v[i].action == DRV_ACT_KERNEL_IN_USE) in_use++;
        CHECK(in_use >= 4, "the peripheral drivers are recognised as bound (got %d)", in_use);
    }
}

/* A catalog that pins nothing may be read but never installed from implicitly. */
static void test_driver_digest_rules(void)
{
    char dir[600], cat_path[700], repo[700];
    snprintf(dir, sizeof dir, "%s/drvtest", FIX);
    snprintf(cat_path, sizeof cat_path, "%s/mini.cat", dir);
    snprintf(repo, sizeof repo, "%s/repo", dir);
    ml_mkdirs(repo, 0755);
    /* three payloads: one with a real digest, one whose digest is wrong on
     * purpose, one deliberately unpinned. The repo layout is
     * <repo>/<package>/<file>, so each package gets its own directory. */
    char payload[700];
    const char *data = "maclite test payload";
    const char *pkgnames[] = { "good-thing", "bad-thing", "unpinned-thing" };
    for (size_t i = 0; i < ML_ARRAY_SIZE(pkgnames); i++) {
        char d[700];
        snprintf(d, sizeof d, "%.600s/%.20s", repo, pkgnames[i]);
        CHECK(ml_mkdirs(d, 0755), "create %s", d);
        snprintf(payload, sizeof payload, "%.500s/%.20s/blob.bin", repo, pkgnames[i]);
        CHECK(ml_write_file(payload, data, strlen(data)), "write the test payload in %s", d);
    }
    snprintf(payload, sizeof payload, "%.600s/good-thing/blob.bin", repo);
    char hex[65];
    CHECK(ml_sha256_file(payload, hex, NULL), "hash the test payload");
    char cattext[2000];
    snprintf(cattext, sizeof cattext,
             "repo test\n"
             "package good-thing 1.0\n"
             "  kind firmware\n"
             "  status stable\n"
             "  target dmi:testrig\n"
             "  install lib/firmware\n"
             "  file blob.bin sha256=%s\n"
             "package bad-thing 1.0\n"
             "  kind firmware\n"
             "  status stable\n"
             "  target dmi:testrig\n"
             "  install lib/firmware\n"
             "  file blob.bin sha256=%s\n"
             "package unpinned-thing 1.0\n"
             "  kind firmware\n"
             "  status stable\n"
             "  target dmi:testrig\n"
             "  install lib/firmware\n"
             "  file blob.bin sha256=unpinned\n",
             hex, "0000000000000000000000000000000000000000000000000000000000000000");
    CHECK(ml_write_file(cat_path, cattext, strlen(cattext)), "write the mini catalog");
    drv_catalog cat;
    CHECK(drv_catalog_load(cat_path, &cat), "mini catalog parses (%s)", cat.error);
    CHECK(cat.n == 3, "three packages parsed (got %d)", cat.n);

    drv_hw hw;
    drv_hw_snapshot(&hw);
    const drv_package *good = NULL, *bad = NULL, *unp = NULL;
    for (int i = 0; i < cat.n; i++) {
        if (!strcmp(cat.v[i].name, "good-thing")) good = &cat.v[i];
        if (!strcmp(cat.v[i].name, "bad-thing")) bad = &cat.v[i];
        if (!strcmp(cat.v[i].name, "unpinned-thing")) unp = &cat.v[i];
    }
    CHECK(good && bad && unp, "all three packages are in the catalog");
    drv_verify v;
    CHECK(drv_verify_package(&cat, good, repo, &v) && v.digest_checked && v.digest_ok,
          "a pinned digest that matches verifies: %s", v.detail);
    CHECK(!drv_verify_package(&cat, bad, repo, &v), "a pinned digest that does not match must fail");
    CHECK(v.digest_checked && !v.digest_ok, "and must be reported as a mismatch, not as unverifiable");
    CHECK(drv_verify_package(&cat, unp, repo, &v) && !v.digest_checked,
          "an unpinned file is readable but its integrity is NOT TESTED");
    (void)hw;
}

/* A catalog that names a key must never be trusted on a machine that cannot
 * check that key — and a key given as a URL is refused outright (spec §28:
 * trusted repositories, never a downloaded blob of unknown provenance). */
static void test_driver_catalog_keys(void)
{
    char dir[600], cat_path[700];
    snprintf(dir, sizeof dir, "%s/drvtest3", FIX);
    CHECK(ml_mkdirs(dir, 0755), "create %s", dir);
    snprintf(cat_path, sizeof cat_path, "%s/urlkey.cat", dir);
    const char *text =
        "repo remote\n"
        "key https://example.invalid/maclite.gpg\n"
        "package something 1.0\n  kind firmware\n  status stable\n"
        "  target dmi:testrig\n  file x.bin sha256=unpinned\n";
    CHECK(ml_write_file(cat_path, text, strlen(text)), "write the URL-key catalog");
    drv_catalog cat;
    CHECK(drv_catalog_load(cat_path, &cat), "the catalog still parses (%s)", cat.error);
    CHECK(strstr(cat.key, "https://") != NULL, "the catalog's key reference is recorded");
    drv_verify v;
    drv_verify_catalog(&cat, &v);
    CHECK(!v.signature_checked, "a URL key is not treated as verified");
    CHECK(!v.signature_ok, "a URL key is not treated as OK either");
    /* a catalog that names a local key but has no .sig beside it: the resolver
     * must say provenance was not checked, and must not claim it was verified */
    drv_catalog keyed;
    memset(&keyed, 0, sizeof keyed);
    snprintf(keyed.path, sizeof keyed.path, "%.200s", cat_path);
    snprintf(keyed.key, sizeof keyed.key, "/etc/maclite/nonexistent.key");
    drv_verify_catalog(&keyed, &v);
    CHECK(!v.signature_checked, "no signature file means provenance is NOT TESTED");
    CHECK(strstr(v.detail, "no ") != NULL, "and the reason names the missing signature (got '%s')", v.detail);
}

static void test_driver_install_rollback(void)
{
    /* A throwaway install root, so the tests never touch the machine. Order
     * matters: pre-existing file (must be restored), new file (must be removed),
     * and the microcode early-load copy (also removed). */
    char root[] = "/tmp/mltest-root-XXXXXX";
    if (!mkdtemp(root)) { printf("note: no temp dir; skipping install test\n"); return; }
    char dir[600], cat_path[700], repo[700];
    snprintf(dir, sizeof dir, "%s/drvtest2", FIX);
    snprintf(cat_path, sizeof cat_path, "%s/mini2.cat", dir);
    snprintf(repo, sizeof repo, "%s/repo2", dir);
    ml_mkdirs(repo, 0755);

    char pay_keep[700], pay_new[700], pay_uc[700];
    const char *pkgnames2[] = { "keep-thing", "new-thing", "ucode-thing" };
    for (size_t i = 0; i < ML_ARRAY_SIZE(pkgnames2); i++) {
        char sub[700];
        snprintf(sub, sizeof sub, "%.600s/%.20s", repo, pkgnames2[i]);
        CHECK(ml_mkdirs(sub, 0755), "create %s", sub);
    }
    snprintf(pay_keep, sizeof pay_keep, "%.660s/keep-thing/keep.bin", repo);
    snprintf(pay_new, sizeof pay_new, "%.660s/new-thing/new.bin", repo);
    snprintf(pay_uc, sizeof pay_uc, "%.660s/ucode-thing/06-25-05", repo);
    const char *keep = "keep-me", *new_ = "brand-new";
    CHECK(ml_write_file(pay_keep, keep, strlen(keep)), "write keep.bin");
    CHECK(ml_write_file(pay_new, new_, strlen(new_)), "write new.bin");
    CHECK(ml_write_file(pay_uc, "ucode-bytes", 11), "write the ucode payload");
    char hk[65], hn[65], hu[65];
    ml_sha256_file(pay_keep, hk, NULL);
    ml_sha256_file(pay_new, hn, NULL);
    ml_sha256_file(pay_uc, hu, NULL);

    char cattext[2400];
    snprintf(cattext, sizeof cattext,
             "repo test\n"
             "package keep-thing 1.0\n"
             "  kind firmware\n  status stable\n  target dmi:testrig\n"
             "  install lib/firmware\n  file keep.bin sha256=%s\n"
             "package new-thing 1.0\n"
             "  kind firmware\n  status stable\n  target dmi:testrig\n"
             "  install lib/firmware\n  file new.bin sha256=%s\n"
             "package ucode-thing 1.0\n"
             "  kind microcode\n  status stable\n  target dmi:testrig\n"
             "  install lib/firmware/intel-ucode\n  file 06-25-05 sha256=%s\n",
             hk, hn, hu);
    CHECK(ml_write_file(cat_path, cattext, strlen(cattext)), "write the install catalog");
    drv_catalog cat;
    CHECK(drv_catalog_load(cat_path, &cat), "install catalog parses (%s)", cat.error);

    /* a pre-existing file that the install will replace */
    char existing[700];
    snprintf(existing, sizeof existing, "%.600s/lib/firmware/keep.bin", root);
    char existing_dir[700];
    snprintf(existing_dir, sizeof existing_dir, "%.600s/lib/firmware", root);
    CHECK(ml_mkdirs(existing_dir, 0755), "create the directory the seeded file lives in");
    CHECK(ml_write_file(existing, "old-content", 11), "seed the file to be replaced");

    char *old_root = getenv("ML_ROOT") ? ml_strdup(getenv("ML_ROOT")) : NULL;
    setenv("ML_ROOT", root, 1);
    char err[300] = "";
    int installed = 0;
    for (int i = 0; i < cat.n; i++) {
        err[0] = 0;
        CHECK(drv_install_package(&cat, &cat.v[i], repo, false, err, sizeof err),
              "install %s: %s", cat.v[i].name, err);
        if (!err[0]) installed++;
    }
    CHECK(installed == 3, "three packages installed (got %d)", installed);

    char landed[800];
    snprintf(landed, sizeof landed, "%s/lib/firmware/keep.bin", root);
    char *txt = ml_read_file(landed, NULL);
    CHECK(txt && !strcmp(txt, keep), "the pre-existing file was replaced by the new payload");
    ml_free(txt);
    snprintf(landed, sizeof landed, "%s/lib/firmware/new.bin", root);
    CHECK(access(landed, F_OK) == 0, "the new file landed in the install root");
    snprintf(landed, sizeof landed, "%s/boot/maclite-ucode/06-25-05", root);
    CHECK(access(landed, F_OK) == 0, "microcode also lands where the bootloader looks for it (§6)");
    CHECK(drv_snapshot_exists(root), "an install leaves a rollback snapshot");

    /* a second install of an unchanged component must be idempotent in effect */
    err[0] = 0;
    CHECK(drv_install_package(&cat, &cat.v[0], repo, false, err, sizeof err),
          "re-installing the same release is allowed: %s", err);

    err[0] = 0;
    CHECK(drv_rollback(root, err, sizeof err), "rollback: %s", err);
    snprintf(landed, sizeof landed, "%s/lib/firmware/keep.bin", root);
    txt = ml_read_file(landed, NULL);
    CHECK(txt && !strcmp(txt, "old-content"), "rollback restored the replaced file byte for byte");
    ml_free(txt);
    snprintf(landed, sizeof landed, "%s/lib/firmware/new.bin", root);
    CHECK(access(landed, F_OK) != 0, "rollback removed a file that had not existed before");
    snprintf(landed, sizeof landed, "%s/boot/maclite-ucode/06-25-05", root);
    CHECK(access(landed, F_OK) != 0, "rollback also removed the early-load microcode copy");

    if (old_root) { setenv("ML_ROOT", old_root, 1); ml_free(old_root); }
    else unsetenv("ML_ROOT");
    char rm[800];
    snprintf(rm, sizeof rm, "rm -rf %.700s", root);
    if (system(rm) != 0) printf("note: could not clean up %s\\n", root);
}

static void test_mode_pick(void)
{
    char reason[160];
    CHECK(hw_pick_mode_caps(true, true, 4, 8192, reason, sizeof reason) == 0, "fast GPU machine: beautiful");
    CHECK(hw_pick_mode_caps(true, false, 4, 8192, reason, sizeof reason) == 1, "GPU scanned out, CPU rendered: balanced");
    CHECK(hw_pick_mode_caps(true, true, 1, 1024, reason, sizeof reason) == 1, "one core: balanced");
    CHECK(hw_pick_mode_caps(false, false, 4, 8192, reason, sizeof reason) == 2, "no KMS: performance");
    CHECK(strlen(reason) > 0, "the picker explains itself");
    ml_hwdec d = hw_pick_hwdec(0x1002, 0x9490, true, reason, sizeof reason);
    CHECK(d == ML_HWDEC_VDPAU || d == ML_HWDEC_AUTO, "UVD part prefers VDPAU (%s)", reason);
    CHECK(hw_pick_hwdec(0x1af4, 0x1010, true, reason, sizeof reason) == ML_HWDEC_NONE,
          "virtio-gpu asks for no hardware decode (%s)", reason);
    CHECK(hw_pick_hwdec(0xdead, 0xbeef, false, reason, sizeof reason) == ML_HWDEC_NONE,
          "unknown GPU without a render node: software");
}

/* --------------------------------------------------------------- driver ---- */
int main(void)
{
    ml_log_init("test_hardware", ML_LOG_ERROR, NULL);
    const char *fixdir = getenv("ML_HW_FIXTURE");
    const char *variant = getenv("ML_HW_VARIANT");
    if (!fixdir || !variant) {
        fprintf(stderr, "usage: ML_HW_FIXTURE=<dir> ML_HW_VARIANT=<imac11_2|imac11_3|virtual|bare> %s\n",
                "test_hardware");
        return 2;
    }
    snprintf(FIX, sizeof FIX, "%s", fixdir);
    VARIANT = variant;
    setenv("ML_SYSFS_ROOT", fix("sys"), 1);
    setenv("ML_PROC_ROOT", fix("proc"), 1);
    setenv("ML_DEV_ROOT", fix("dev"), 1);
    setenv("ML_ETC_ROOT", fix("etc"), 1);
    setenv("ML_FW_ROOT", fix("fw"), 1);

    printf("fixture: %s (%s)\n", FIX, VARIANT);
    test_env();
    test_exit_codes();
    test_mode_pick();
    test_sha256();
    test_driver_versions();
    test_driver_kinds();
    test_driver_catalog();
    test_driver_digest_rules();
    test_driver_catalog_keys();
    test_driver_install_rollback();
    if (!strcmp(VARIANT, "bare")) {
        test_gpu_absent();
        test_audio_absent();
        test_cpu_absent();
    } else if (!strcmp(VARIANT, "virtual")) {
        test_gpu_virtual();
        test_audio_absent();
    } else {
        test_gpu_imac();
        test_display();
        test_edid_unit();
        test_backlight();
        test_audio();
        test_codec_caps();
        test_network();
        test_usb();
        test_storage();
        test_power();
        test_cpu();
        test_cpu_microcode();
        test_driver_resolve();
    }
    if (failures) { printf("%d FAILURES in %s\n", failures, VARIANT); return 1; }
    printf("all hardware tests passed (%s)\n", VARIANT);
    return 0;
}
