/* maclite-hardware — one unified hardware report (spec §15).
 *
 * Every line is the result of a probe that ran here, in this process. Nothing is
 * carried over from a datasheet: capability-DB facts are labelled as such, and
 * anything that could not be exercised prints NOT TESTED and keeps the overall
 * verdict honest.
 *
 *   maclite-hardware            fast report (no decode round trip)
 *   maclite-hardware --full     also run the 1080p H.264 decode measurement
 *   maclite-hardware --tsv      machine readable
 * exit: 0 PASS, 1 FAIL, 2 PARTIAL (something unverified), 3 UNSUPPORTED
 */
#include "diag_common.h"
#include <sys/statvfs.h>
#include <netdb.h>

static hw_result storage_result(mica_storage_state *s, char *ev, size_t evlen)
{
    struct statvfs sv;
    if (statvfs("/", &sv) == 0) {
        uint64_t total = (uint64_t)sv.f_blocks * (sv.f_frsize / 1024);
        uint64_t free_kb = (uint64_t)sv.f_bavail * (sv.f_frsize / 1024);
        snprintf(ev, evlen, "/ holds %llu MB, %llu MB free", 
                 (unsigned long long)(total / 1024), (unsigned long long)(free_kb / 1024));
        /* real write test: 4 MB, then removed */
        char path[128];
        snprintf(path, sizeof path, "/tmp/maclite-hw.%d", (int)getpid());
        FILE *f = fopen(path, "wb");
        if (!f) return HW_FAIL;
        char *buf = ml_zalloc(4u * 1024 * 1024);
        uint64_t t0 = ml_now_ns();
        size_t w = fwrite(buf, 1, 4u * 1024 * 1024, f);
        fclose(f);
        double ms = ml_elapsed_ms(t0);
        unlink(path);
        ml_free(buf);
        if (w != 4u * 1024 * 1024) return HW_FAIL;
        size_t n = strlen(ev);
        snprintf(ev + n, evlen - n, "; wrote 4 MB in %.0f ms (%.0f MB/s)", ms,
                 ms > 0 ? 4.0 / (ms / 1000.0) : 0.0);
        (void)s;
        return HW_PASS;
    }
    snprintf(ev, evlen, "statvfs(/) failed");
    return HW_FAIL;
}

int main(int argc, char **argv)
{
    if (diag_flag(argc, argv, "--help")) {
        fprintf(stderr, "usage: %s [--full] [--tsv]\n"
                        "exit: 0 PASS, 1 FAIL, 2 PARTIAL, 3 UNSUPPORTED, 4 usage\n", argv[0]);
        return HW_EXIT_USAGE;
    }
    bool full = diag_flag(argc, argv, "--full");
    bool tsv = diag_flag(argc, argv, "--tsv");

    hw_report rep;
    hw_report_init(&rep);

    /* ---- System ---- */
    mica_cpu_info cpu;
    mica_mem_info mem;
    mica_cpu_probe(&cpu);
    mica_mem_probe(&mem);
    hw_report_add(&rep, "System", "CPU", true,
                  cpu.model[0] ? HW_PASS : HW_FAIL, "%s (%u cores/%u threads, %u MHz)",
                  cpu.model, cpu.cores, cpu.threads, cpu.mhz_max);
    hw_report_add(&rep, "System", "RAM", true,
                  mem.total_kb ? HW_PASS : HW_FAIL, "%llu MB total, %llu MB available",
                  (unsigned long long)(mem.total_kb / 1024), (unsigned long long)(mem.avail_kb / 1024));
    mica_storage_state sto;
    mica_storage_probe(&sto);
    char sev[192];
    hw_report_add(&rep, "System", "Storage", true, storage_result(&sto, sev, sizeof sev), "%s", sev);

    /* ---- GPU ---- */
    mica_gpu_info g;
    mica_gpu_probe(&g);
    mica_drm_state drm;
    mica_drm_probe(&drm);
    hw_report_add(&rep, "GPU", "Detection", true,
                  g.present ? HW_PASS : HW_UNSUPPORTED, "%s",
                  g.present ? g.device : "no PCI display controller here");
    hw_report_add(&rep, "GPU", "DRM/KMS", true,
                  (g.has_kms && drm.any) ? HW_PASS : (drm.any ? HW_FAIL : HW_UNSUPPORTED),
                  "%s", g.card_node[0] ? g.card_node : "no /dev/dri/card*");
    gl_probe_result glr = mica_gl_probe(&g);
    hw_report_add(&rep, "GPU", "Hardware Rendering", true,
                  glr == GL_HW ? HW_PASS : glr == GL_SW ? HW_FAIL
                  : glr == GL_NONE ? HW_UNSUPPORTED : HW_NOT_TESTED,
                  "%s", g.gl_probed ? g.gl_renderer : "no GL query possible on this host");

    /* ---- Display ---- */
    bool disp_ok = drm.primary >= 0;
    hw_report_add(&rep, "Display", "Detection", true,
                  disp_ok ? HW_PASS : (drm.any ? HW_FAIL : HW_UNSUPPORTED),
                  disp_ok ? "%s %s" : "no connected connector",
                  disp_ok ? drm.c[drm.primary].connector : "",
                  disp_ok ? drm.c[drm.primary].status : "");
    const edid_mode *pref = disp_ok ? edid_preferred(&drm.c[drm.primary].edid) : NULL;
    hw_report_add(&rep, "Display", "Native Resolution", true,
                  pref ? HW_PASS : (disp_ok && drm.c[drm.primary].nmodes ? HW_PARTIAL : HW_UNSUPPORTED),
                  pref ? "%dx%d@%d Hz" : "no EDID preferred mode",
                  pref ? pref->w : 0, pref ? pref->h : 0, pref ? edid_refresh_hz(pref) : 0);
    bl_state bl;
    bl_probe(&bl);
    const bl_device *b = bl_active(&bl);
    int pct = 0;
    bool got_pct = b && bl_get_percent(&bl, &pct);
    hw_report_add(&rep, "Display", "Brightness Control", true,
                  (b && b->writable && !b->suspect && got_pct) ? HW_PASS
                  : b ? HW_FAIL : HW_UNSUPPORTED,
                  "%s", b ? (b->suspect ? b->note : bl.mechanism) : "no backlight device");

    /* ---- Audio ---- */
    ml_aud_state as;
    ml_audio_probe(&as);
    const ml_aud_card *card = ml_audio_default(&as);
    /* shared rule (see hardware/audio.c): a mixer ioctl that could not be
     * performed here is NOT TESTED, never FAIL and never PASS */
    ml_audio_status aud = ml_audio_status_get(&as);
    hw_report_add(&rep, "Audio", "Output", true,
                  (card && card->can_play) ? HW_PASS : (as.any ? HW_FAIL : HW_UNSUPPORTED),
                  "%s", card ? card->name : (as.note[0] ? as.note : "no card"));
    hw_report_add(&rep, "Audio", "Volume", true,
                  aud.have_volume ? HW_PASS : (aud.readable ? HW_FAIL : HW_NOT_TESTED),
                  "%s", aud.have_volume ? aud.why : aud.why);
    hw_report_add(&rep, "Audio", "Mute", true,
                  aud.have_mute ? HW_PASS : (aud.readable ? HW_FAIL : HW_NOT_TESTED),
                  "%s", aud.why);

    /* ---- Network ---- */
    mica_net_state net;
    mica_net_probe(&net);
    /* shared rule (see hardware/hwprobe.c): link layer from sysfs, address and
     * live probes only when they were really performed on this machine */
    ml_link_status link = ml_net_link_status(&net);
    bool live_net = !hw_using_fixture();
    hw_report_add(&rep, "Network", "Ethernet", true,
                  link.have_iface && link.link_up ? HW_PASS : (net.neth ? HW_FAIL : HW_UNSUPPORTED),
                  "%s", link.have_iface ? link.evidence
                                        : (net.neth ? "no non-loopback interface is up" : "none"));
    hw_report_add(&rep, "Network", "Address", true,
                  !link.address_readable ? HW_NOT_TESTED : (link.has_ip4 ? HW_PASS : HW_FAIL),
                  "%s", !link.address_readable
                            ? "fixture mode: an address cannot be modelled, ioctl not performed"
                            : (link.has_ip4 ? link.ip4 : "no IPv4 address on the primary interface"));
    hw_report_add(&rep, "Network", "Wi-Fi", false,
                  net.nwifi > 0 ? HW_PASS : HW_UNSUPPORTED,
                  "%d wireless interface(s)%s", net.nwifi, live_net ? " on this host" : " in the fixture");
    bool dns_ok = false;
    if (!live_net) {
        hw_report_add(&rep, "Network", "DNS", true, HW_NOT_TESTED,
                      "%d nameserver(s) from %s; live lookup not run in fixture mode",
                      net.ndns, net.resolv_source);
    } else {
        dns_ok = net.ndns > 0;
        if (dns_ok) {
            struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM }, *ai = NULL;
            dns_ok = getaddrinfo("one.one.one.one", "80", &hints, &ai) == 0;
            if (ai) freeaddrinfo(ai);
        }
        hw_report_add(&rep, "Network", "DNS", true,
                      dns_ok ? HW_PASS : (net.ndns ? HW_FAIL : HW_UNSUPPORTED),
                      dns_ok ? "name resolution succeeded via %s" : "resolution failed",
                      net.ndns ? net.dns[0] : "-");
    }

    /* ---- USB / input ---- */
    mica_input_state in;
    mica_usb_state usb;
    mica_input_probe(&in);
    mica_usb_probe(&usb);
    hw_report_add(&rep, "USB", "Keyboard", true,
                  in.keyboard ? HW_PASS : (in.present ? HW_FAIL : HW_NOT_TESTED),
                  "%d keyboard device(s)", in.nkey);
    hw_report_add(&rep, "USB", "Mouse", true,
                  in.mouse ? HW_PASS : (in.present ? HW_FAIL : HW_NOT_TESTED),
                  "%d pointer device(s)", in.nmouse);
    hw_report_add(&rep, "USB", "Storage", false,
                  usb.nstorage ? HW_PASS : (usb.present ? HW_UNSUPPORTED : HW_NOT_TESTED),
                  "%d mass-storage device(s)", usb.nstorage);

    /* ---- Multimedia ---- */
    ml_hwdec_state hd;
    ml_hwdec_probe(&hd);
    char cap_detail[192];
    ml_cap h264 = ml_codec_capability(g.vendor_id, g.device_id, ML_CODEC_H264, &hd,
                                      cap_detail, sizeof cap_detail);
    hw_report_add(&rep, "Multimedia", "H.264 HW Decode", true,
                  h264 == ML_CAP_HW ? HW_PASS : h264 == ML_CAP_UNKNOWN ? HW_NOT_TESTED : HW_UNSUPPORTED,
                  "%s", cap_detail);
    if (full) {
        ml_vtest vt;
        int rc = 0;
        bool ran = ml_video_test_run(&vt, "h264", 1920, 1080, 300, true, &rc);
        hw_report_add(&rep, "Multimedia", "1080p Playback", true,
                      !ran ? HW_NOT_TESTED : (rc == 0 ? HW_PASS : HW_FAIL),
                      "%s", ran ? vt.note : vt.note);
    } else {
        hw_report_add(&rep, "Multimedia", "1080p Playback", true, HW_NOT_TESTED,
                      "re-run with --full (encodes and decodes 10 s of 1080p H.264)");
    }

    /* ---- Power ---- */
    mica_power_state pw;
    mica_power_probe(&pw);
    if (!pw.state_node) {
        hw_report_add(&rep, "Power", "Suspend", false, HW_NOT_TESTED, "no suspend interface here");
        hw_report_add(&rep, "Power", "Resume", false, HW_NOT_TESTED, "blocked with suspend");
    } else if (bl.apple_machine) {
        hw_report_add(&rep, "Power", "Suspend", false, HW_UNSUPPORTED, "disabled by policy: %s", pw.note);
        hw_report_add(&rep, "Power", "Resume", false, HW_UNSUPPORTED, "blocked with suspend");
    } else {
        hw_report_add(&rep, "Power", "Suspend", false, HW_NOT_TESTED, "needs a human to confirm the panel returns");
        hw_report_add(&rep, "Power", "Resume", false, HW_NOT_TESTED, "not exercised");
    }

    if (tsv) { hw_report_print_tsv(&rep); printf("Overall\t%s\n", hw_result_str(hw_report_overall(&rep))); }
    else {
        diag_title("MacLiteOS Hardware Report");
        const char *last = "";
        for (int i = 0; i < rep.n; i++) {
            if (strcmp(rep.v[i].group, last)) { printf("%s\n", rep.v[i].group); last = rep.v[i].group; }
            printf("  %-22s %s\n", rep.v[i].name, hw_result_str(rep.v[i].result));
            if (rep.v[i].evidence[0]) printf("      %s\n", rep.v[i].evidence);
        }
        hw_result o = hw_report_overall(&rep);
        printf("\nOverall:\n  %s\n", o == HW_PASS ? "PASS" : o == HW_FAIL ? "FAIL" : "PARTIAL");
        printf("\n(exit %d — 0 PASS, 1 FAIL, 2 PARTIAL, 3 UNSUPPORTED)\n", hw_report_exit(&rep));
    }
    return hw_report_exit(&rep);
}
