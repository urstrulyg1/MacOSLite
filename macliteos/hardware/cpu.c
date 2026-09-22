/* Deep CPU + microcode + frequency/thermal layer (spec §1, §6, §7).
 *
 * Everything here comes from this machine: /proc/cpuinfo, /sys/devices/system/
 * cpu/{cpufreq,cpuidle}, /sys/class/{hwmon,thermal}, /proc/cmdline, DMI. The
 * known-part table is a *cross-check*, and a mismatch is reported as a mismatch
 * rather than papered over: on a machine where someone has swapped the CPU, the
 * report must say so instead of repeating the datasheet.
 *
 * No daemon, no polling: governor/turbo changes are one-shot writes with
 * read-back verification, and the thermal value is read on demand.
 */
#include "cpu.h"
#include "hwcap.h"
#include "ml/util.h"
#include "ml/log.h"
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>

/* ------------------------------------------------------------- small reads -- */
static void rd_str(const char *dir, const char *file, char *out, size_t outlen)
{
    char p[512];
    snprintf(p, sizeof p, "%.400s/%.60s", dir, file);
    char *s = ml_sysfs_str(p, "");
    snprintf(out, outlen, "%s", s);
    ml_free(s);
}
static long rd_long(const char *dir, const char *file, long def)
{
    char p[512];
    snprintf(p, sizeof p, "%.400s/%.60s", dir, file);
    return ml_sysfs_long(p, def);
}
static bool has(const char *dir, const char *file)
{
    char p[512];
    snprintf(p, sizeof p, "%.400s/%.60s", dir, file);
    return access(p, F_OK) == 0;
}

/* One line of /proc/cpuinfo: "model name\t: Intel(R) Core(TM) i5 ..." -> the
 * value, clipped at the newline. Clip-and-copy rather than "return a pointer
 * into the buffer" is deliberate: /proc/cpuinfo is many lines and a value that
 * runs on into the next line (which is what a naive pointer return does) would
 * put "stepping\t: 5" inside the CPU's brand string. */
static bool cpuinfo_value(const char *text, const char *key, char *out, size_t outlen)
{
    size_t klen = strlen(key);
    if (outlen) out[0] = 0;
    for (const char *p = text; p && *p; ) {
        if (!strncmp(p, key, klen) && (p[klen] == ' ' || p[klen] == '\t' || p[klen] == ':')) {
            const char *colon = strchr(p, ':');
            if (!colon) return false;
            const char *v = colon + 1;
            while (*v == ' ' || *v == '\t') v++;
            const char *e = v;
            while (*e && *e != '\n') e++;
            while (e > v && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
            size_t n = (size_t)(e - v);
            if (n >= outlen) n = outlen ? outlen - 1 : 0;
            memcpy(out, v, n);
            if (outlen) out[n] = 0;
            return true;
        }
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return false;
}
/* value of a field as an integer (decimal or 0x-prefixed) */
static long cpuinfo_long(const char *text, const char *key, long def)
{
    char v[64];
    if (!cpuinfo_value(text, key, v, sizeof v) || !v[0]) return def;
    char *end = NULL;
    long x = strtol(v, &end, 0);
    return end == v ? def : x;
}
static void cpuinfo_str(const char *text, const char *key, char *out, size_t outlen)
{
    cpuinfo_value(text, key, out, outlen);
}
static bool cpuinfo_has_word(const char *text, const char *word)
{
    /* flags is a space separated list; match whole words only so "avx" does not
     * match "avx512f" and "ht" does not match "htt" */
    char f[1024];
    if (!cpuinfo_value(text, "flags", f, sizeof f)) return false;
    size_t wl = strlen(word);
    for (const char *p = f; *p; ) {
        while (*p == ' ' || *p == '\t') p++;
        const char *e = p;
        while (*e && *e != ' ' && *e != '\t') e++;
        if ((size_t)(e - p) == wl && !strncmp(p, word, wl)) return true;
        p = e;
    }
    return false;
}

/* ------------------------------------------------------------- brand names -- */
void ml_cpu_normalise_brand(const char *brand, char out[40])
{
    out[0] = 0;
    if (!brand || !*brand) return;
    /* Take the brand string apart far enough to build the marketing name the
     * owner of the machine expects to see: "Intel(R) Core(TM) i3 CPU 540 @
     * 3.07GHz" -> "Intel Core i3-540". Anything we do not recognise is passed
     * through unchanged, trimmed — a guess is worse than the raw string. */
    const char *p = brand;
    while (*p == ' ') p++;
    const char *intel = strstr(p, "Intel");
    const char *amd = strstr(p, "AMD");
    const char *fam = NULL;
    if (intel) fam = "Intel";
    else if (amd) fam = "AMD";
    else if (!strncmp(p, "QEMU", 4) || !strncmp(p, "Virtual", 7)) fam = NULL;

    /* the tier token: Core(TM) i3 / Core(TM) i5 / Core(TM) i7 / Xeon */
    const char *tier = NULL, *tier_name = NULL;
    if ((p = strstr(brand, "Core(TM)")) || (p = strstr(brand, "Core"))) {
        const char *q = p + 8;
        while (*q == ' ') q++;
        if (!strncmp(q, "i3", 2) || !strncmp(q, "i5", 2) || !strncmp(q, "i7", 2) ||
            !strncmp(q, "i9", 2)) {
            tier = q;
            tier_name = NULL;
        }
    }
    if ((p = strstr(brand, "Xeon"))) { tier = p; tier_name = "Xeon"; }
    if ((p = strstr(brand, "Pentium"))) { tier = p; tier_name = "Pentium"; }

    /* the model number: the first standalone 3-4 digit run (540, 680, 860) */
    char num[8] = "";
    if (tier) {
        const char *q = tier + ((tier_name) ? strlen(tier_name) : 2);
        while (*q && !isdigit((unsigned char)*q) && *q != '(') q++;
        int n = 0;
        while (isdigit((unsigned char)*q) && n < 7) num[n++] = *q++;
        /* "CPU 540" -> after digits there may be nothing; the number may also be
         * preceded by a model suffix such as "i5-680" already in the string */
        num[n] = 0;
        if (!n && (q = strchr(tier, '-'))) {
            q++;
            n = 0;
            while (isdigit((unsigned char)*q) && n < 7) num[n++] = *q++;
            num[n] = 0;
        }
    }
    if (!fam || !tier) {
        snprintf(out, 40, "%.39s", brand);
        return;
    }
    if (tier_name) {
        if (num[0]) snprintf(out, 40, "%s %s %s", fam, tier_name, num);
        else snprintf(out, 40, "%s %s", fam, tier_name);
    } else {
        char tierbuf[8] = { tier[0], tier[1], 0 };
        if (num[0]) snprintf(out, 40, "%s Core %s-%s", fam, tierbuf, num);
        else snprintf(out, 40, "%s Core %s", fam, tierbuf);
    }
}

/* ----------------------------------------------------------- known parts --- */
typedef struct {
    const char *alias;      /* substring to match in brand or normalised name */
    uint32_t cores, threads;
    bool ht, turbo;
    uint32_t base_mhz;
    const char *note;
} cpu_part;

static const cpu_part parts[] = {
    { "i3-540",  2, 4, true,  false, 3066, "Clarkdale, 4 MB L3, 73 W, SSE4.2, no AVX, no Turbo" },
    { "i3-550",  2, 4, true,  false, 3200, "Clarkdale, 4 MB L3, 73 W, SSE4.2, no AVX, no Turbo" },
    { "i3-560",  2, 4, true,  false, 3333, "Clarkdale" },
    { "i5-650",  2, 4, true,  true,  3200, "Clarkdale, Turbo 3.46 GHz" },
    { "i5-660",  2, 4, true,  true,  3333, "Clarkdale, Turbo 3.60 GHz" },
    { "i5-670",  2, 4, true,  true,  3466, "Clarkdale" },
    { "i5-680",  2, 4, true,  true,  3600, "Clarkdale, Turbo 3.86 GHz, the 3.6 GHz iMac11,2 option" },
    { "i5-750",  4, 4, false, true,  2666, "Lynnfield, no HT" },
    { "i7-870",  4, 8, true,  true,  2933, "Lynnfield (iMac11,3 27-inch)" },
    { "i3-530",  2, 4, true,  false, 2933, "Clarkdale" },
};

static const cpu_part *part_lookup(const char *brand, const char *part)
{
    for (size_t i = 0; i < ML_ARRAY_SIZE(parts); i++)
        if ((brand && strstr(brand, parts[i].alias)) || (part && strstr(part, parts[i].alias)))
            return &parts[i];
    return NULL;
}

/* -------------------------------------------------------------- microcode --- */
/* Intel microcode file names and blobs are keyed by the CPUID signature:
 * family, model (extended model folded in) and stepping, formatted as
 * "%02x-%02x-%02x" — Clarkdale stepping K0 is "06-25-05". */
void ml_cpu_ucode_filename(uint32_t family, uint32_t model, uint32_t stepping, char out[16])
{
    snprintf(out, 16, "%02x-%02x-%02x", family & 0xff, model & 0xff, stepping & 0xff);
}

/* CPUID-signature encoding used in a microcode blob's processor_signature field */
static uint32_t sig_encode(uint32_t family, uint32_t model, uint32_t stepping)
{
    return (stepping & 0xf) | ((model & 0xf) << 4) | ((family & 0xf) << 8) |
           (((model >> 4) & 0xf) << 16) | (((family >> 4) & 0xf) << 20);
}

bool ml_ucode_blob_info(const char *path, uint32_t *revision, uint32_t *date,
                        uint32_t *sig_out, uint32_t *expected_sig)
{
    uint8_t hdr[48];
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(hdr, 1, sizeof hdr, f);
    fclose(f);
    if (n < 48) return false;
    uint32_t header_version = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                              ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    if (header_version != 1) return false;      /* not an Intel ucode update */
    uint32_t rev = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                   ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
    uint32_t dt = (uint32_t)hdr[8] | ((uint32_t)hdr[9] << 8) |
                  ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
    uint32_t sig = (uint32_t)hdr[12] | ((uint32_t)hdr[13] << 8) |
                   ((uint32_t)hdr[14] << 16) | ((uint32_t)hdr[15] << 24);
    uint32_t total = (uint32_t)hdr[32] | ((uint32_t)hdr[33] << 8) |
                     ((uint32_t)hdr[34] << 16) | ((uint32_t)hdr[35] << 24);
    if (total == 0 || total > (32u << 20)) return false;  /* implausible size */
    if (revision) *revision = rev;
    if (date) *date = dt;
    if (sig_out) *sig_out = sig;
    if (expected_sig) *expected_sig = sig;
    return true;
}

bool ml_ucode_find_for(const char *fw_root, uint32_t signature, char *out, size_t outlen,
                       uint32_t *revision, uint32_t *date)
{
    /* Search order matters: the early-load copy the initrd ships first, then the
     * vendor directory, then the caveats directory (which linux-firmware keeps
     * separate because those blobs are only safe for specific microarchitectures
     * — we take one only when it matches this CPU's exact signature). */
    static const char *subs[] = { "intel-ucode", "intel-ucode-with-caveats", "maclite-ucode" };
    char name[16];
    /* the caller passes the *encoded* signature; derive the file name from it */
    uint32_t stepping = signature & 0xf;
    uint32_t model = ((signature >> 16) & 0xf) << 4 | ((signature >> 4) & 0xf);
    uint32_t family = ((signature >> 20) & 0xf) << 4 | ((signature >> 8) & 0xf);
    ml_cpu_ucode_filename(family, model, stepping, name);
    for (size_t i = 0; i < ML_ARRAY_SIZE(subs); i++) {
        char dir[400];
        snprintf(dir, sizeof dir, "%.300s/%.40s", fw_root, subs[i]);
        char path[480];
        snprintf(path, sizeof path, "%.420s/%.20s", dir, name);
        if (access(path, F_OK) != 0) continue;
        uint32_t rev = 0, dt = 0, blob_sig = 0;
        if (!ml_ucode_blob_info(path, &rev, &dt, &blob_sig, NULL)) continue;
        /* The file name is not proof: check the blob really targets this CPU.
         * A blob whose signature differs is skipped (and would be reported as
         * INCOMPATIBLE by the caller) — never loaded. */
        if (blob_sig != signature) continue;
        snprintf(out, outlen, "%s", path);
        if (revision) *revision = rev;
        if (date) *date = dt;
        return true;
    }
    out[0] = 0;
    return false;
}

void ml_cpu_microcode_probe(ml_cpu_state *c, const char *fw_root)
{
    c->ucode_file[0] = 0;
    c->ucode_file_present = false;
    c->ucode_available = 0;
    c->ucode_available_known = false;
    c->ucode_early = false;
    c->ucode_date = 0;
    snprintf(c->ucode_dir, sizeof c->ucode_dir, "%s", fw_root ? fw_root : "/lib/firmware");
    snprintf(c->ucode_sig, sizeof c->ucode_sig, "%02x-%02x-%02x", c->family & 0xff,
             c->model & 0xff, c->stepping & 0xff);

    if (!c->vendor[0] || strncmp(c->vendor, "GenuineIntel", 12)) {
        /* AMD blobs are a single archive keyed differently; MacLiteOS's target
         * is Intel, so anything else is reported as not-applicable rather than
         * guessed at. */
        snprintf(c->ucode_note, sizeof c->ucode_note,
                 "vendor %s: no signature-matched microcode path implemented (target is Intel Clarkdale/Lynnfield)",
                 c->vendor[0] ? c->vendor : "unknown");
        return;
    }
    uint32_t sig = sig_encode(c->family, c->model, c->stepping);
    char path[192] = "";
    uint32_t rev = 0, dt = 0;
    if (ml_ucode_find_for(fw_root ? fw_root : "/lib/firmware", sig, path, sizeof path, &rev, &dt)) {
        snprintf(c->ucode_file, sizeof c->ucode_file, "%s", path);
        c->ucode_file_present = true;
        c->ucode_available = rev;
        c->ucode_available_known = true;
        c->ucode_date = dt;
    }
    /* the initrd copy: /boot/maclite-ucode/<sig> is what the bootloader feeds the
     * kernel, and it is the only copy that can be applied as an *early* update
     * (late reload cannot add the microcode's new CPUID bits) */
    char early[256];
    snprintf(early, sizeof early, "/boot/maclite-ucode/%.15s", c->ucode_sig);
    c->ucode_early = access(early, F_OK) == 0;

    if (!c->ucode_loaded_known) {
        snprintf(c->ucode_note, sizeof c->ucode_note,
                 "no 'microcode' line in /proc/cpuinfo; the loaded revision cannot be read here");
        return;
    }
    if (!c->ucode_file_present) {
        snprintf(c->ucode_note, sizeof c->ucode_note,
                 "loaded 0x%x; no blob for signature %.15s in %.80s — microcode updates for this stepping are not in the image",
                 c->ucode_loaded, c->ucode_sig, c->ucode_dir);
        return;
    }
    if (c->ucode_available > c->ucode_loaded) {
        snprintf(c->ucode_note, sizeof c->ucode_note,
                 "loaded 0x%x, image ships 0x%x for %.15s: %s",
                 c->ucode_loaded, c->ucode_available, c->ucode_sig,
                 c->ucode_early ? "early-load copy present — apply at boot"
                                : "an update is available but has no early-load copy in "
                                  "/boot/maclite-ucode — late reload cannot add CPUID bits");
    } else if (c->ucode_available == c->ucode_loaded) {
        snprintf(c->ucode_note, sizeof c->ucode_note,
                 "loaded 0x%x is the revision the image ships for %s", c->ucode_loaded, c->ucode_sig);
    } else {
        snprintf(c->ucode_note, sizeof c->ucode_note,
                 "loaded 0x%x is newer than the image's 0x%x for %s (BIOS or a different image applied it)",
                 c->ucode_loaded, c->ucode_available, c->ucode_sig);
    }
}

/* ---------------------------------------------------------------- thermal --- */
static void probe_thermal(ml_cpu_state *c)
{
    c->thermal = false;
    c->thermal_c = -1;
    c->thermal_crit_c = -1;
    c->thermal_trip_max_c = -1;
    c->thermal_source[0] = 0;
    c->thermal_label[0] = 0;

    /* hwmon: prefer a CPU package sensor by driver name, then any core sensor */
    const char *hwmon_dir = hw_sys("class/hwmon");
    DIR *d = opendir(hwmon_dir);
    if (d) {
        struct dirent *e;
        char best_name[32] = "";
        int best_temp = -1, best_crit = -1;
        char best_dir[400] = "", best_label[48] = "";
        bool have_pkg = false;
        while ((e = readdir(d))) {
            if (strncmp(e->d_name, "hwmon", 5)) continue;
            char dir[400];
            snprintf(dir, sizeof dir, "%s/%s", hwmon_dir, e->d_name);
            char name[32];
            rd_str(dir, "name", name, sizeof name);
            bool cpu_sensor = !strcmp(name, "coretemp") || !strcmp(name, "k10temp") ||
                              !strcmp(name, "zenpower") || !strcmp(name, "cpu_thermal") ||
                              !strcmp(name, "acpitz");
            if (!cpu_sensor) continue;
            for (int i = 1; i <= 8; i++) {
                char f[24], label[48] = "";
                snprintf(f, sizeof f, "temp%d_input", i);
                if (!has(dir, f)) continue;
                long mv = rd_long(dir, f, -1);
                if (mv < 0) continue;
                snprintf(f, sizeof f, "temp%d_label", i);
                rd_str(dir, f, label, sizeof label);
                snprintf(f, sizeof f, "temp%d_crit", i);
                long crit = rd_long(dir, f, -1);
                bool is_pkg = strcasestr(label, "package") != NULL;
                /* a package sensor beats a core sensor; the first reading wins
                 * otherwise, so the value is always one this machine reported */
                if (best_temp < 0 || (is_pkg && !have_pkg)) {
                    best_temp = (int)(mv / 1000);
                    best_crit = crit > 0 ? (int)(crit / 1000) : -1;
                    snprintf(best_name, sizeof best_name, "%s", name);
                    snprintf(best_dir, sizeof best_dir, "%s", dir);
                    snprintf(best_label, sizeof best_label, "%s", label[0] ? label : name);
                    have_pkg = is_pkg;
                }
                if (have_pkg) break;
            }
            if (have_pkg) break;
        }
        closedir(d);
        if (best_temp >= 0) {
            c->thermal = true;
            c->thermal_c = best_temp;
            c->thermal_crit_c = best_crit;
            snprintf(c->thermal_label, sizeof c->thermal_label, "%.31s", best_label);
            snprintf(c->thermal_source, sizeof c->thermal_source, "hwmon %.23s (%.40s)", best_name, best_dir);
        }
    }
    /* thermal zones as a fallback: acpitz or the package zone */
    if (!c->thermal) {
        const char *tz_root = hw_sys("class/thermal");
        DIR *z = opendir(tz_root);
        if (z) {
            struct dirent *e;
            while ((e = readdir(z))) {
                if (strncmp(e->d_name, "thermal_zone", 12)) continue;
                char dir[400];
                snprintf(dir, sizeof dir, "%s/%s", tz_root, e->d_name);
                char type[48];
                rd_str(dir, "type", type, sizeof type);
                if (strcmp(type, "x86_pkg_temp") && strcmp(type, "acpitz") && strcmp(type, "cpu-thermal"))
                    continue;
                long mv = rd_long(dir, "temp", -1);
                if (mv <= 0) continue;
                c->thermal = true;
                c->thermal_c = (int)(mv / 1000);
                long trip = rd_long(dir, "trip_point_0_temp", -1);
                c->thermal_trip_max_c = trip > 0 ? (int)(trip / 1000) : -1;
                snprintf(c->thermal_label, sizeof c->thermal_label, "%.31s", type);
                snprintf(c->thermal_source, sizeof c->thermal_source, "thermal zone %.31s (%.60s)",
                         e->d_name, dir);
                break;
            }
            closedir(z);
        }
    }
}

/* ------------------------------------------------------------- cpuidle ------ */
static void probe_idle(ml_cpu_state *c)
{
    const char *base = hw_sys("devices/system/cpu/cpu0/cpuidle");
    DIR *d = opendir(base);
    if (!d) { c->cpuidle = false; return; }
    c->cpuidle = true;
    char drv[24] = "";
    rd_str(base, "current_driver", drv, sizeof drv);
    snprintf(c->idle_driver, sizeof c->idle_driver, "%s", drv);
    c->idle_states = 0;
    c->idle_names[0] = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "state", 5)) continue;
        char dir[400];
        snprintf(dir, sizeof dir, "%s/%s", base, e->d_name);
        char name[32];
        rd_str(dir, "name", name, sizeof name);
        if (!name[0]) continue;
        c->idle_states++;
        size_t n = strlen(c->idle_names);
        snprintf(c->idle_names + n, sizeof c->idle_names - n, "%s%s", n ? " " : "", name);
    }
    closedir(d);
}

/* -------------------------------------------------------- vulnerabilities --- */
static void probe_vulns(ml_cpu_state *c)
{
    c->vuln_affected = -1;
    c->vuln_mitigated = -1;
    c->vulnerabilities[0] = 0;
    const char *dir = hw_sys("devices/system/cpu/vulnerabilities");
    DIR *d = opendir(dir);
    if (!d) return;
    int affected = 0, mitigated = 0, n = 0;
    size_t off = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[440];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        char *v = ml_read_file(path, NULL);
        if (!v) continue;
        char *t = ml_str_trim(v);
        bool not_aff = strstr(t, "Not affected") != NULL;
        bool vuln = strstr(t, "Vulnerable") != NULL;
        if (!not_aff) {
            affected++;
            if (!vuln) mitigated++;
        }
        n++;
        if (off < sizeof c->vulnerabilities - 40) {
            off += (size_t)snprintf(c->vulnerabilities + off, sizeof c->vulnerabilities - off,
                                    "%s%s=%s", off ? ", " : "", e->d_name,
                                    not_aff ? "not-affected" : vuln ? "vulnerable" : "mitigated");
        }
        ml_free(v);
    }
    closedir(d);
    if (n) { c->vuln_affected = affected; c->vuln_mitigated = mitigated; }
    else snprintf(c->vulnerabilities, sizeof c->vulnerabilities, "no vulnerability files in %s", dir);
}

/* ------------------------------------------------------------- cross-check -- */
ml_cpu_xcheck ml_cpu_crosscheck(const ml_cpu_state *c, char *why, size_t why_len)
{
    const cpu_part *p = part_lookup(c->brand, c->part);
    if (!p) {
        snprintf(why, why_len, "%s is not in the known-part table: topology reported as observed, not as expected",
                 c->part[0] ? c->part : (c->brand[0] ? c->brand : "this CPU"));
        return ML_CPU_UNKNOWN_PART;
    }
    bool ok_topology = p->cores == c->cores && p->threads == c->threads;
    bool ok_base = p->base_mhz == 0 || abs((int)c->mhz_base - (int)p->base_mhz) <= 40;
    snprintf(why, why_len, "%s expects %uC/%uT base %u MHz, ht=%s turbo=%s — observed %uC/%uT base %u MHz (%s)",
             p->alias, p->cores, p->threads, p->base_mhz, p->ht ? "yes" : "no", p->turbo ? "yes" : "no",
             c->cores, c->threads, c->mhz_base,
             (ok_topology && ok_base) ? "match" : !ok_topology ? "TOPOLOGY MISMATCH" : "base clock differs");
    return (ok_topology && ok_base) ? ML_CPU_MATCH : ML_CPU_MISMATCH;
}

/* ------------------------------------------------------------------ probe --- */
void ml_cpu_refresh_clocks(ml_cpu_state *c)
{
    const char *d = hw_sys("devices/system/cpu/cpu0/cpufreq");
    long cur = rd_long(d, "scaling_cur_freq", -1);
    if (cur <= 0) cur = rd_long(d, "cpuinfo_cur_freq", -1);
    if (cur > 0) { c->mhz_cur = (uint32_t)(cur / 1000); c->mhz_from_cpufreq = true; }
    long mn = rd_long(d, "cpuinfo_min_freq", -1);
    long mx = rd_long(d, "cpuinfo_max_freq", -1);
    long bm = rd_long(d, "base_frequency", -1);
    if (mn > 0) c->mhz_min = (uint32_t)(mn / 1000);
    if (mx > 0) c->mhz_max = (uint32_t)(mx / 1000);
    if (bm > 0) c->mhz_base = (uint32_t)(bm / 1000);
}

bool ml_cpu_probe(ml_cpu_state *out)
{
    memset(out, 0, sizeof *out);
    out->thermal_c = -1;
    out->thermal_crit_c = -1;
    out->thermal_trip_max_c = -1;
    out->vuln_affected = -1;
    out->vuln_mitigated = -1;

    char *info = ml_read_file(hw_proc("cpuinfo"), NULL);
    if (!info) return false;

    cpuinfo_str(info, "vendor_id", out->vendor, sizeof out->vendor);
    cpuinfo_str(info, "model name", out->brand, sizeof out->brand);
    /* AMD reports "cpu family" too; both vendors use the same field names */
    out->family   = (uint32_t)cpuinfo_long(info, "cpu family", 0);
    out->model    = (uint32_t)cpuinfo_long(info, "model", 0);
    out->stepping = (uint32_t)cpuinfo_long(info, "stepping", 0);
    if (!out->family) out->family = (uint32_t)cpuinfo_long(info, "vendor_id", 0) ? 6 : 0;
    out->signature = sig_encode(out->family, out->model, out->stepping);
    ml_cpu_ucode_filename(out->family, out->model, out->stepping, out->ucode_sig);
    ml_cpu_normalise_brand(out->brand, out->part);

    uint32_t threads = (uint32_t)cpuinfo_long(info, "siblings", 0);
    uint32_t cores = (uint32_t)cpuinfo_long(info, "cpu cores", 0);
    /* count logical CPUs: "processor\t: N" lines, which works even when
     * siblings/cpu cores are missing (some VMs and old kernels omit them) */
    uint32_t nproc_lines = 0;
    for (const char *p = info; (p = strstr(p, "processor")); p += 9) nproc_lines++;
    out->sockets = (uint32_t)cpuinfo_long(info, "physical id", 0) + 1;
    out->threads = threads ? threads : nproc_lines;
    out->cores = cores ? cores : ML_MAX(1u, out->threads);
    out->cores_per_socket = cores ? cores : out->cores;
    out->hyperthreading = out->threads > out->cores;

    out->ht_flag = cpuinfo_has_word(info, "ht");
    out->lm64 = cpuinfo_has_word(info, "lm");
    out->sse2 = cpuinfo_has_word(info, "sse2");
    out->ssse3 = cpuinfo_has_word(info, "ssse3");
    out->sse41 = cpuinfo_has_word(info, "sse4_1");
    out->sse42 = cpuinfo_has_word(info, "sse4_2");
    out->avx = cpuinfo_has_word(info, "avx");
    out->avx2 = cpuinfo_has_word(info, "avx2");
    out->vmx = cpuinfo_has_word(info, "vmx");
    out->aes = cpuinfo_has_word(info, "aes");
    out->pclmulqdq = cpuinfo_has_word(info, "pclmulqdq");
    out->monitor = cpuinfo_has_word(info, "monitor");
    out->constant_tsc = cpuinfo_has_word(info, "constant_tsc");
    out->nonstop_tsc = cpuinfo_has_word(info, "nonstop_tsc");
    out->tsc_deadline = cpuinfo_has_word(info, "tsc_deadline_timer");
    out->est = cpuinfo_has_word(info, "est");
    out->popcnt = cpuinfo_has_word(info, "popcnt");
    out->movbe = cpuinfo_has_word(info, "movbe");

    char *isa = out->isa;
    size_t cap = sizeof out->isa;
    size_t off = 0;
#define ADD_ISA(cond, txt) do { if (cond) off += (size_t)snprintf(isa + off, cap - off, "%s%s", off ? " " : "", txt); } while (0)
    ADD_ISA(out->lm64, "x86-64");
    ADD_ISA(out->sse2, "SSE2");
    ADD_ISA(out->ssse3, "SSSE3");
    ADD_ISA(out->sse41, "SSE4.1");
    ADD_ISA(out->sse42, "SSE4.2");
    ADD_ISA(out->popcnt, "POPCNT");
    ADD_ISA(out->aes, "AES-NI");
    ADD_ISA(out->pclmulqdq, "PCLMULQDQ");
    ADD_ISA(out->vmx, "VT-x");
    ADD_ISA(out->avx, "AVX");
    ADD_ISA(out->avx2, "AVX2");
#undef ADD_ISA
    if (!off) snprintf(isa, cap, "none reported");
    off = 0;
    if (!out->avx)
        off += (size_t)snprintf(out->isa_absent + off, sizeof out->isa_absent - off, "AVX");
    if (!out->avx2)
        off += (size_t)snprintf(out->isa_absent + off, sizeof out->isa_absent - off, "%sAVX2", off ? ", " : "");
    if (!off) snprintf(out->isa_absent, sizeof out->isa_absent, "none");

    /* live clock from /proc/cpuinfo's "cpu MHz" (already a per-CPU sample) */
    char mhz[32];
    if (cpuinfo_value(info, "cpu MHz", mhz, sizeof mhz)) out->mhz_cur = (uint32_t)atof(mhz);
    /* loaded microcode revision: Intel only, hex */
    char uc[32];
    if (cpuinfo_value(info, "microcode", uc, sizeof uc) && uc[0]) {
        out->ucode_loaded = (uint32_t)strtoul(uc, NULL, 0);
        out->ucode_loaded_known = true;
    }
    ml_free(info);

    /* ---- cpufreq ---- */
    const char *cf = hw_sys("devices/system/cpu/cpu0/cpufreq");
    out->cpufreq = has(cf, "scaling_governor") || has(cf, "scaling_driver");
    if (out->cpufreq) {
        rd_str(cf, "scaling_driver", out->scaling_driver, sizeof out->scaling_driver);
        rd_str(cf, "scaling_governor", out->governor, sizeof out->governor);
        rd_str(cf, "scaling_available_governors", out->governors, sizeof out->governors);
        out->governor_writable = access(hw_path(cf, "scaling_governor"), W_OK) == 0;
        ml_cpu_refresh_clocks(out);
        if (!out->mhz_base) out->mhz_base = out->mhz_max ? out->mhz_max : out->mhz_cur;
        long boost = rd_long(cf, "boost", -1);
        if (boost >= 0) {
            out->turbo_knob = true;
            out->turbo_enabled = boost != 0;
        } else {
            long nt = rd_long(cf, "no_turbo", -1);
            if (nt >= 0) { out->turbo_knob = true; out->turbo_enabled = nt == 0; }
        }
    } else {
        snprintf(out->scaling_driver, sizeof out->scaling_driver, "none");
        snprintf(out->governor, sizeof out->governor, "none");
        out->mhz_base = out->mhz_max ? out->mhz_max : out->mhz_cur;
    }
    const cpu_part *p = part_lookup(out->brand, out->part);
    if (p) out->turbo_capable = p->turbo;
    else if (out->turbo_knob) out->turbo_capable = out->turbo_enabled;

    probe_idle(out);
    probe_thermal(out);

    /* ---- platform context ---- */
    const char *dmi = hw_sys("class/dmi/id");
    rd_str(dmi, "sys_vendor", out->dmi_vendor, sizeof out->dmi_vendor);
    rd_str(dmi, "product_name", out->dmi_product, sizeof out->dmi_product);
    rd_str(dmi, "board_name", out->dmi_board, sizeof out->dmi_board);
    char *ver = ml_read_file(hw_proc("version"), NULL);
    if (ver) {
        char *nl = strchr(ver, '\n');
        if (nl) *nl = 0;
        snprintf(out->kernel, sizeof out->kernel, "%.47s", ver);
        ml_free(ver);
    }
    char *cmd = ml_read_file(hw_proc("cmdline"), NULL);
    if (cmd) {
        snprintf(out->cmdline, sizeof out->cmdline, "%s", ml_str_trim(cmd));
        out->mitigations_off = strstr(out->cmdline, "mitigations=off") != NULL;
        ml_free(cmd);
    }
    probe_vulns(out);
    ml_cpu_microcode_probe(out, hw_fw_root());
    return true;
}

/* ---------------------------------------------------------------- control ---- */
int ml_cpu_set_governor(ml_cpu_state *c, const char *governor)
{
    if (!governor || !*governor) return 0;
    /* Refuse anything the kernel does not offer. The kernel would reject it too,
     * but on a fixture file (which accepts any text) the read-back would then
     * "verify" a write the machine would never have taken. */
    if (c && c->governors[0] && !strstr(c->governors, governor)) return 0;
    int ok = 0;
    for (int cpu = 0; cpu < 64; cpu++) {
        char rel[64];
        snprintf(rel, sizeof rel, "devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu);
        const char *path = hw_sys(rel);
        if (access(path, F_OK) != 0) {
            if (cpu == 0) return 0;              /* no cpufreq at all */
            break;
        }
        if (!ml_sysfs_write(path, governor)) continue;
        /* verify by read-back: a write that does not stick is not a success */
        char back[32];
        char dir[400];
        snprintf(dir, sizeof dir, "%.300s/devices/system/cpu/cpu%d/cpufreq", hw_sysfs_root(), cpu);
        rd_str(dir, "scaling_governor", back, sizeof back);
        if (!strcmp(back, governor)) ok++;
    }
    if (c && ok) {
        snprintf(c->governor, sizeof c->governor, "%.23s", governor);
        ml_cpu_refresh_clocks(c);
    }
    return ok;
}

bool ml_cpu_set_turbo(ml_cpu_state *c, bool enabled)
{
    const char *dir = hw_sys("devices/system/cpu/cpu0/cpufreq");
    const char *knob = NULL;
    if (access(hw_path(dir, "boost"), F_OK) == 0) knob = hw_path(dir, "boost");
    else if (access(hw_path(dir, "no_turbo"), F_OK) == 0) knob = hw_path(dir, "no_turbo");
    if (!knob) return false;
    bool is_no_turbo = strstr(knob, "no_turbo") != NULL;
    const char *val = (enabled != is_no_turbo) ? "1" : "0";
    if (!ml_sysfs_write(knob, val)) return false;
    long back = ml_sysfs_long(knob, -1);
    bool now_enabled = is_no_turbo ? (back == 0) : (back != 0);
    if (c) { c->turbo_knob = true; c->turbo_enabled = now_enabled; }
    return back >= 0 && now_enabled == enabled;
}

const char *ml_cpu_recommend_governor(const ml_cpu_state *c, char *why, size_t why_len)
{
    bool has = false;
    if (c->governors[0]) has = strstr(c->governors, "schedutil") != NULL;
    if (strstr(c->scaling_driver, "intel_pstate")) {
        snprintf(why, why_len,
                 "intel_pstate is active: 'powersave' there is the load-following mode (not a fixed "
                 "low clock), which is what keeps this class of machine responsive at idle");
        return "powersave";
    }
    if (has) {
        snprintf(why, why_len,
                 "schedutil scales from the scheduler's own utilisation signal — event driven, no "
                 "sampling timer, so it is both more responsive and cheaper at idle than ondemand "
                 "on a 2010 dual-core");
        return "schedutil";
    }
    if (c->governors[0] && strstr(c->governors, "ondemand")) {
        snprintf(why, why_len,
                 "schedutil is not available on this kernel; ondemand is the next best fit for a "
                 "Core i3/i5 (ramps on load, idles low). 'performance' is deliberately not chosen: "
                 "it pins the clock and wastes idle power");
        return "ondemand";
    }
    if (c->governors[0] && strstr(c->governors, "conservative")) {
        snprintf(why, why_len, "only conservative is offered besides powersave; conservative ramps more slowly");
        return "conservative";
    }
    snprintf(why, why_len, "no usable governor list in sysfs (%s)", c->governors[0] ? c->governors : "empty");
    return NULL;
}
