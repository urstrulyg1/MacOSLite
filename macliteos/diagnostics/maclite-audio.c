/* maclite-audio — audio device, volume, mute and test tone (spec §7).
 *
 *   maclite-audio list            cards, PCM streams, mixer controls
 *   maclite-audio volume          current volume
 *   maclite-audio volume 50       set (verified by reading the control back)
 *   maclite-audio mute|unmute     switch state (verified the same way)
 *   maclite-audio test [--ms N] [--hz N] [--wav FILE]
 *                                 play a generated tone, or write it to a WAV
 *
 * There is no sound server and no libasound: the ALSA kernel UAPI is used
 * directly (hardware/audio.c). When no card exists every check reports
 * UNSUPPORTED/NOT TESTED and the exit code is non-zero.
 */
#include "diag_common.h"

static void usage(const char *p)
{
    fprintf(stderr,
        "usage: %s list | volume [N] | mute | unmute | test [--ms N] [--hz N] [--wav FILE]\n"
        "exit: 0 pass, 1 fail, 2 not tested, 3 unsupported, 4 usage\n", p);
}

int main(int argc, char **argv)
{
    if (argc < 2 || diag_flag(argc, argv, "--help")) { usage(argv[0]); return HW_EXIT_USAGE; }
    const char *cmd = argv[1];

    ml_aud_state as;
    ml_audio_probe(&as);
    const ml_aud_card *card = ml_audio_default(&as);
    int cnum = card ? card->card : 0;

    if (!strcmp(cmd, "list")) {
        diag_title("MacLiteOS Audio");
        hw_report rep;
        hw_report_init(&rep);
        {
            char cards_txt[192];
            if (as.any) snprintf(cards_txt, sizeof cards_txt, "%d card(s) reported by procfs", as.n);
            else snprintf(cards_txt, sizeof cards_txt, "%s", as.note);
            hw_report_add(&rep, "Audio", "ALSA present", true,
                          as.any ? HW_PASS : HW_UNSUPPORTED, "%s", cards_txt);
        }
        diag_section("Cards");
        if (!as.n) printf("  (none)\n");
        for (int i = 0; i < as.n; i++) {
            const ml_aud_card *c = &as.cards[i];
            printf("  card%d  %-10s %-14s %s\n", c->card, c->id, c->driver, c->name);
            printf("         codec: %-20s playback: %-3s capture: %-3s HDMI/DP: %s\n",
                   c->codec[0] ? c->codec : "unknown", diag_yn(c->can_play), diag_yn(c->can_record),
                   diag_yn(c->has_hdmi));
            printf("         nodes: %s %s %s\n", c->pcm_play, c->can_record ? c->pcm_cap : "-", c->ctl);
            char names[40][48];
            int nn = ml_audio_list_controls(c->card, names, 40);
            if (nn) {
                printf("         mixer:");
                for (int k = 0; k < nn; k++) printf(" %s", names[k]);
                putchar('\n');
            } else {
                printf("         mixer: cannot read controls (%s)\n",
                       ml_file_exists(c->ctl) ? "permission denied?" : "control node missing");
            }
        }
        if (card) {
            /* the rule (PASS / FAIL / NOT TESTED for mixer reads) lives in
             * hardware/audio.c so maclite-hardware cannot drift from this tool */
            ml_audio_status a = ml_audio_status_get(&as);
            char ev[128];
            hw_report_add(&rep, "Audio", "Output device", true,
                          card->can_play ? HW_PASS : HW_FAIL, "card%d (%s)", cnum, card->name);
            if (a.have_volume) snprintf(ev, sizeof ev, "%d%%", a.volume_pct);
            else snprintf(ev, sizeof ev, "%s", a.why);
            hw_report_add(&rep, "Audio", "Volume control", true,
                          a.have_volume ? HW_PASS : (a.readable ? HW_FAIL : HW_NOT_TESTED), "%s", ev);
            if (a.have_mute) snprintf(ev, sizeof ev, "%s", a.muted ? "muted" : "unmuted");
            else snprintf(ev, sizeof ev, "%s", a.why);
            hw_report_add(&rep, "Audio", "Mute control", true,
                          a.have_mute ? HW_PASS : (a.readable ? HW_FAIL : HW_NOT_TESTED), "%s", ev);
            hw_report_add(&rep, "Audio", "Input device", false,
                          card->can_record ? HW_PASS : HW_UNSUPPORTED, "capture on card%d", cnum);
        }
        printf("\n");
        for (int i = 0; i < rep.n; i++)
            printf("  %-24s %-10s %s\n", rep.v[i].name, hw_result_str(rep.v[i].result), rep.v[i].evidence);
        printf("\nOverall: %s\n", hw_result_str(hw_report_overall(&rep)));
        return hw_report_exit(&rep);
    }

    if (!strcmp(cmd, "volume")) {
        if (!card) { fprintf(stderr, "no audio card: %s\n", as.note); return HW_EXIT_UNSUPPORTED; }
        const char *v = argc > 2 && argv[2][0] != '-' ? argv[2] : NULL;
        if (!v) {
            int pct = ml_audio_volume_percent(cnum);
            if (pct < 0) {
                if (ml_audio_status_get(&as).readable) {
                    fprintf(stderr, "cannot read volume on card%d\n", cnum);
                    return HW_EXIT_FAIL;
                }
                fprintf(stderr, "mixer ioctl not performed here (fixture roots): volume is NOT TESTED\n");
                return HW_EXIT_NOT_TESTED;
            }
            printf("%d\n", pct);
            return HW_EXIT_PASS;
        }
        int applied = -1;
        if (!ml_audio_set_volume_percent(cnum, atoi(v), &applied)) {
            fprintf(stderr, "could not set volume to %s%% on card%d\n", v, cnum);
            return HW_EXIT_FAIL;
        }
        printf("%d%% (read back from the ALSA control)\n", applied);
        return HW_EXIT_PASS;
    }

    if (!strcmp(cmd, "mute") || !strcmp(cmd, "unmute")) {
        if (!card) { fprintf(stderr, "no audio card: %s\n", as.note); return HW_EXIT_UNSUPPORTED; }
        bool want = !strcmp(cmd, "mute");
        bool applied = false;
        if (!ml_audio_set_mute(cnum, want, &applied)) {
            if (!ml_audio_status_get(&as).readable) {
                fprintf(stderr, "mixer ioctl not performed here (fixture roots): mute is NOT TESTED\n");
                return HW_EXIT_NOT_TESTED;
            }
            fprintf(stderr, "no mute switch on card%d\n", cnum);
            return HW_EXIT_FAIL;
        }
        printf("%s (verified)\n", applied ? "muted" : "unmuted");
        return HW_EXIT_PASS;
    }

    if (!strcmp(cmd, "test")) {
        const char *ms_s = diag_opt(argc, argv, "--ms");
        const char *hz_s = diag_opt(argc, argv, "--hz");
        const char *wav = diag_opt(argc, argv, "--wav");
        int ms = ms_s ? atoi(ms_s) : 1000;
        int hz = hz_s ? atoi(hz_s) : 440;
        char detail[256];
        int rc = ml_audio_test_tone(cnum, 0, ms, hz, wav, detail, sizeof detail);
        printf("MacLiteOS audio test tone\n\n");
        printf("  %-14s %s\n", "Card:", card ? card->name : "(none)");
        printf("  %-14s %d Hz sine, %d ms, 48 kHz stereo 16-bit\n", "Signal:", hz, ms);
        printf("  %-14s %s\n", "Result:", ml_audio_errstr(rc));
        printf("  %-14s %s\n", "Detail:", detail);
        if (rc == ML_AUDIO_OK) {
            printf("\nOverall: %s\n", wav ? "PASS (file written; playback not exercised)" : "PASS");
            return HW_EXIT_PASS;
        }
        if (rc == ML_AUDIO_NO_DEVICE) {
            printf("\nOverall: UNSUPPORTED (no ALSA PCM node; nothing was played)\n");
            return HW_EXIT_UNSUPPORTED;
        }
        printf("\nOverall: FAIL\n");
        return HW_EXIT_FAIL;
    }

    usage(argv[0]);
    return HW_EXIT_USAGE;
}
