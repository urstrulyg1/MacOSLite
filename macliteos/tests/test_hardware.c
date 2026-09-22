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
#include <assert.h>
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
    CHECK(u.n == 3, "three devices, got %d", u.n);
    CHECK(u.nkey == 1, "one keyboard, got %d", u.nkey);
    CHECK(u.nmouse == 1, "one mouse, got %d", u.nmouse);
    CHECK(u.nstorage == 1, "one mass storage device, got %d", u.nstorage);
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
    CHECK(s.n == 3, "one disk plus two partitions, got %d", s.n);
    CHECK(s.nwhole == 1, "one whole disk, got %d", s.nwhole);
    CHECK(s.nremovable == 0, "nothing removable in the fixture");
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

/* ---------------------------------------------------------- mode picking -- */
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

    printf("fixture: %s (%s)\n", FIX, VARIANT);
    test_env();
    test_exit_codes();
    test_mode_pick();
    if (!strcmp(VARIANT, "bare")) {
        test_gpu_absent();
        test_audio_absent();
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
    }
    if (failures) { printf("%d FAILURES in %s\n", failures, VARIANT); return 1; }
    printf("all hardware tests passed (%s)\n", VARIANT);
    return 0;
}
