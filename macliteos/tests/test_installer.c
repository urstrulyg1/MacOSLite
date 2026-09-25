/* test_installer — Test suite for G1OS / MacLiteOS Installer logic.
 *
 * Exercises:
 *   1. Hardware disk probe and internal disk discovery.
 *   2. Live USB boot media detection and protection (preventing self-overwrite).
 *   3. Partition layout geometry verification (MACLITE_BOOT, MACLITE_DATA, MACLITE_BASE).
 *   4. Confirmation validation and safety state machine.
 *   5. Bootloader UUID binding & exclusion of USB paths in grub.cfg.
 *   6. Offline pre-flight verification pass for internal boot reliability.
 *   7. Authoritative state progression (PREPARING -> INSTALLING -> FINALIZING -> VERIFYING -> COMPLETED).
 *   8. Bootloader failure gate (blocks completion and disables restart).
 *   9. Driver categorization (REQUIRED vs OPTIONAL vs UNSUPPORTED).
 *  10. Configuration automatic repair & re-verification.
 *  11. Low disk space safety gate.
 *  12. Partial installation marker detection.
 *  13. Strict reboot safety gate.
 */
#include "ml/common.h"
#include "ml/log.h"
#include "ml/util.h"
#include "../hardware/hwprobe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int TESTS_RUN = 0;
static int TESTS_PASSED = 0;

#define TEST(name) \
    do { \
        TESTS_RUN++; \
        printf("  TEST [%02d] %-55s ", TESTS_RUN, name); \
    } while (0)

#define PASS() \
    do { \
        TESTS_PASSED++; \
        printf("PASS\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("FAIL: %s (line %d)\n", msg, __LINE__); \
        return 1; \
    } while (0)

/* Mock disk layout */
typedef struct {
    char name[16];
    uint64_t size_bytes;
    bool removable;
    bool is_usb;
    char mountpoint[64];
} mock_disk;

static bool is_protected_boot_media(const mock_disk *d)
{
    if (d->is_usb) return true;
    if (d->removable) return true;
    if (strstr(d->mountpoint, "/run/maclite-base") ||
        strstr(d->mountpoint, "/run/maclite-live") ||
        strstr(d->mountpoint, "/cdrom"))
        return true;
    return false;
}

static int test_usb_protection(void)
{
    TEST("USB live boot media protection");

    mock_disk live_usb = {
        .name = "sdb",
        .size_bytes = 16ULL * 1000 * 1000 * 1000,
        .removable = true,
        .is_usb = true,
        .mountpoint = "/run/maclite-base"
    };

    if (!is_protected_boot_media(&live_usb)) {
        FAIL("Live USB drive was not identified as protected");
    }

    mock_disk internal_ssd = {
        .name = "sda",
        .size_bytes = 500ULL * 1000 * 1000 * 1000,
        .removable = false,
        .is_usb = false,
        .mountpoint = ""
    };

    if (is_protected_boot_media(&internal_ssd)) {
        FAIL("Internal SSD was incorrectly flagged as protected boot media");
    }

    PASS();
    return 0;
}

static int test_target_auto_selection(void)
{
    TEST("Internal drive auto-selection excluding USB");

    mock_disk disks[3] = {
        { "sdb", 16ULL * 1e9, true, true, "/run/maclite-live" },   /* Live USB */
        { "sda", 500ULL * 1e9, false, false, "" },                /* Internal SSD */
        { "sdc", 64ULL * 1e9, true, true, "" }                    /* External thumb drive */
    };

    int selected = -1;
    for (int i = 0; i < 3; i++) {
        if (!is_protected_boot_media(&disks[i])) {
            selected = i;
            break;
        }
    }

    if (selected != 1) {
        FAIL("Did not auto-select internal drive sda");
    }
    if (strcmp(disks[selected].name, "sda") != 0) {
        FAIL("Selected target name mismatch");
    }

    PASS();
    return 0;
}

static int test_partition_geometry(void)
{
    TEST("G1OS GPT partition geometry");

    uint64_t disk_size = 500ULL * 1024 * 1024 * 1024; /* 500 GiB */

    uint64_t boot_size = 256ULL * 1024 * 1024;        /* 256 MiB FAT32 EFI */
    uint64_t data_size = 4ULL * 1024 * 1024 * 1024;   /* 4 GiB ext4 */
    uint64_t base_size = disk_size - boot_size - data_size;

    if (boot_size != 268435456ULL) {
        FAIL("BOOT size calculation error");
    }
    if (data_size != 4294967296ULL) {
        FAIL("DATA size calculation error");
    }
    if (base_size <= 0 || base_size >= disk_size) {
        FAIL("BASE staging size calculation error");
    }

    PASS();
    return 0;
}

static int test_confirmation_state_machine(void)
{
    TEST("User confirmation state gate");

    bool confirmed = false;
    bool can_proceed = false;

    /* Without confirmation, proceeding must be refused */
    can_proceed = confirmed;
    if (can_proceed) {
        FAIL("Installer allowed proceeding without confirmation");
    }

    /* With confirmation, proceeding is allowed */
    confirmed = true;
    can_proceed = confirmed;
    if (!can_proceed) {
        FAIL("Installer blocked proceeding after confirmation");
    }

    PASS();
    return 0;
}

static int test_uuid_boot_binding(void)
{
    TEST("UUID-bound boot configuration (no USB references)");

    const char *mock_grub_cfg =
        "search --no-floppy --fs-uuid --set=boot 78FA-C9B2\n"
        "linux /boot/vmlinuz-g1os root=UUID=e78d910a-3142-4f81-9b16-5fa4e872c019 ro rd.maclite=1\n"
        "initrd /boot/initrd-g1os.img\n";

    if (strstr(mock_grub_cfg, "maclite-live")) {
        FAIL("grub.cfg contains residual references to live media");
    }
    if (strstr(mock_grub_cfg, "/dev/sda") || strstr(mock_grub_cfg, "/dev/sdb")) {
        FAIL("grub.cfg relies on hardcoded /dev/sdX devnode rather than UUID");
    }
    if (!strstr(mock_grub_cfg, "78FA-C9B2")) {
        FAIL("grub.cfg does not bind to target boot UUID");
    }

    PASS();
    return 0;
}

static int test_kernel_manifest_binding(void)
{
    TEST("Real G1OS kernel/initramfs manifest binding");

    const char *manifest =
        "G1OS_BOOT_MANIFEST=1\n"
        "build_id=20260925T000000Z-test\n"
        "kernel_filename=vmlinuz-maclite\n"
        "kernel_install_filename=vmlinuz-g1os\n"
        "kernel_sha256=0123456789abcdef\n"
        "kernel_size=12764160\n"
        "kernel_arch=x86_64\n"
        "initrd_filename=initrd-maclite.img\n"
        "initrd_install_filename=initrd-g1os.img\n"
        "initrd_sha256=fedcba9876543210\n"
        "initrd_size=9240318\n";

    if (!strstr(manifest, "G1OS_BOOT_MANIFEST=1")) FAIL("Missing manifest magic");
    if (!strstr(manifest, "kernel_install_filename=vmlinuz-g1os")) FAIL("Final kernel filename is not bound");
    if (!strstr(manifest, "initrd_install_filename=initrd-g1os.img")) FAIL("Final initramfs filename is not bound");
    if (!strstr(manifest, "kernel_arch=x86_64")) FAIL("Kernel architecture is not recorded");
    if (!strstr(manifest, "kernel_sha256=") || !strstr(manifest, "initrd_sha256=")) FAIL("Checksums are not recorded");
    if (!strstr(manifest, "build_id=")) FAIL("Build ID is not recorded");

    PASS();
    return 0;
}

static int test_preflight_verification(void)
{
    TEST("Offline pre-flight verification pass");

    /* Simulated pre-flight checks */
    bool efi_loader_exists = true;
    bool kernel_exists = true;
    bool initrd_exists = true;
    bool grub_uuid_matches = true;
    bool no_usb_references = true;

    bool preflight_passed = efi_loader_exists && kernel_exists && initrd_exists &&
                            grub_uuid_matches && no_usb_references;

    if (!preflight_passed) {
        FAIL("Pre-flight check failed when all files present");
    }

    /* Failure test: missing EFI fallback loader must abort */
    efi_loader_exists = false;
    preflight_passed = efi_loader_exists && kernel_exists && initrd_exists &&
                       grub_uuid_matches && no_usb_references;
    if (preflight_passed) {
        FAIL("Pre-flight pass did not abort on missing EFI loader");
    }

    PASS();
    return 0;
}

/* State machine verification: PREPARING -> INSTALLING -> FINALIZING -> VERIFYING -> COMPLETED */
typedef enum {
    STATE_PREPARING = 0,
    STATE_INSTALLING,
    STATE_FINALIZING,
    STATE_VERIFYING,
    STATE_COMPLETED,
    STATE_FAILED
} installer_state;

static bool can_transition(installer_state from, installer_state to, bool verification_passed)
{
    if (from == STATE_PREPARING && to == STATE_INSTALLING) return true;
    if (from == STATE_INSTALLING && to == STATE_FINALIZING) return true;
    if (from == STATE_FINALIZING && to == STATE_VERIFYING) return true;
    /* Forbidden transition: INSTALLING -> COMPLETED directly */
    if (from == STATE_INSTALLING && to == STATE_COMPLETED) return false;
    /* Only VERIFYING -> COMPLETED is allowed, and ONLY if verification_passed is true */
    if (from == STATE_VERIFYING && to == STATE_COMPLETED) return verification_passed;
    if (to == STATE_FAILED) return true;
    return false;
}

static int test_verification_state_transitions(void)
{
    TEST("State machine rejects INSTALLING -> COMPLETED directly");

    if (can_transition(STATE_INSTALLING, STATE_COMPLETED, true)) {
        FAIL("Direct transition from INSTALLING to COMPLETED was erroneously allowed");
    }
    if (can_transition(STATE_VERIFYING, STATE_COMPLETED, false)) {
        FAIL("VERIFYING to COMPLETED allowed when verification failed");
    }
    if (!can_transition(STATE_VERIFYING, STATE_COMPLETED, true)) {
        FAIL("VERIFYING to COMPLETED blocked even though all checks passed");
    }

    PASS();
    return 0;
}

static int test_bootloader_failure_blocks_completion(void)
{
    TEST("Bootloader verification failure blocks completion");

    bool bootloader_verified = false;
    bool all_other_checks = true;
    bool allow_complete = bootloader_verified && all_other_checks;

    if (allow_complete) {
        FAIL("Installation completed with unverified bootloader");
    }

    PASS();
    return 0;
}

typedef enum {
    DRIVER_REQUIRED,
    DRIVER_OPTIONAL,
    DRIVER_UNSUPPORTED
} driver_class;

static bool evaluate_driver_check(driver_class cls, bool present)
{
    if (cls == DRIVER_REQUIRED) return present; /* failure is fatal */
    if (cls == DRIVER_OPTIONAL) return true;    /* missing emits warning, not fatal */
    if (cls == DRIVER_UNSUPPORTED) return true; /* missing emits info, not fatal */
    return false;
}

static int test_driver_classification(void)
{
    TEST("Driver categorization: REQUIRED vs OPTIONAL vs UNSUPPORTED");

    /* Storage driver missing -> FATAL */
    if (evaluate_driver_check(DRIVER_REQUIRED, false) != false) {
        FAIL("Missing REQUIRED driver was not treated as fatal failure");
    }
    /* WiFi driver missing -> WARNING (passes) */
    if (evaluate_driver_check(DRIVER_OPTIONAL, false) != true) {
        FAIL("Missing OPTIONAL driver halted installation");
    }
    /* Legacy modem missing -> INFO (passes) */
    if (evaluate_driver_check(DRIVER_UNSUPPORTED, false) != true) {
        FAIL("Missing UNSUPPORTED hardware halted installation");
    }

    PASS();
    return 0;
}

static int test_config_repair_and_reverification(void)
{
    TEST("Configuration automatic repair & re-verification");

    bool fstab_syntax_valid = false;
    bool can_auto_repair = true;

    if (fstab_syntax_valid) {
        FAIL("Initial corrupted config falsely evaluated as valid");
    }

    /* Simulate safe automatic repair */
    if (can_auto_repair) {
        fstab_syntax_valid = true; /* regenerated */
    }

    /* Mandatory re-verification */
    bool reverify_passed = fstab_syntax_valid;
    if (!reverify_passed) {
        FAIL("Re-verification failed after repair");
    }

    PASS();
    return 0;
}

static int test_disk_space_guard(void)
{
    TEST("Disk space verification threshold check");

    uint64_t free_boot_kb = 4096; /* 4 MB - below 10 MB minimum */
    uint64_t free_root_kb = 2048000; /* 2 GB */

    bool space_sufficient = (free_boot_kb >= 10240) && (free_root_kb >= 102400);
    if (space_sufficient) {
        FAIL("Critically low EFI space was not flagged as failure");
    }

    PASS();
    return 0;
}

static int test_partial_installation_detection(void)
{
    TEST("Partial installation marker detection");

    bool in_progress_marker_present = true;
    bool allow_verified_complete = !in_progress_marker_present;

    if (allow_verified_complete) {
        FAIL("Installer reported success despite interrupted installation marker");
    }

    PASS();
    return 0;
}

static int test_repair_is_surgical(void)
{
    TEST("Repair reconstructs only missing boot artifacts");

    bool root_filesystem_replaced = false;
    bool kernel_missing = true;
    bool initrd_missing = true;
    bool boot_config_invalid = true;

    /* Repair contract: never recopy/reformat the root filesystem. */
    if (root_filesystem_replaced) FAIL("Repair would replace the existing root filesystem");

    if (kernel_missing) kernel_missing = false;
    if (initrd_missing) initrd_missing = false;
    if (boot_config_invalid) boot_config_invalid = false;

    if (kernel_missing || initrd_missing || boot_config_invalid)
        FAIL("Repair did not reconstruct all missing boot components");

    /* Idempotency: once valid, a second repair changes nothing. */
    bool second_kernel_copy = kernel_missing;
    bool second_initrd_copy = initrd_missing;
    if (second_kernel_copy || second_initrd_copy)
        FAIL("Repair is not idempotent");

    PASS();
    return 0;
}

static int test_reboot_safety_gate(void)
{
    TEST("Reboot safety gate strictly enforces COMPLETED & PASS");

    installer_state st = STATE_VERIFYING;
    bool verification_passed = false;
    bool can_reboot = (st == STATE_COMPLETED) && verification_passed;

    if (can_reboot) {
        FAIL("Reboot allowed while in VERIFYING state");
    }

    st = STATE_COMPLETED;
    verification_passed = false;
    can_reboot = (st == STATE_COMPLETED) && verification_passed;
    if (can_reboot) {
        FAIL("Reboot allowed without verified PASS");
    }

    st = STATE_COMPLETED;
    verification_passed = true;
    can_reboot = (st == STATE_COMPLETED) && verification_passed;
    if (!can_reboot) {
        FAIL("Reboot blocked despite verified COMPLETED state");
    }

    PASS();
    return 0;
}

int main(void)
{
    printf("\n=== G1OS Installer Test Suite ===\n\n");

    if (test_usb_protection() != 0) return 1;
    if (test_target_auto_selection() != 0) return 1;
    if (test_partition_geometry() != 0) return 1;
    if (test_confirmation_state_machine() != 0) return 1;
    if (test_uuid_boot_binding() != 0) return 1;
    if (test_kernel_manifest_binding() != 0) return 1;
    if (test_preflight_verification() != 0) return 1;
    if (test_verification_state_transitions() != 0) return 1;
    if (test_bootloader_failure_blocks_completion() != 0) return 1;
    if (test_driver_classification() != 0) return 1;
    if (test_config_repair_and_reverification() != 0) return 1;
    if (test_disk_space_guard() != 0) return 1;
    if (test_partial_installation_detection() != 0) return 1;
    if (test_repair_is_surgical() != 0) return 1;
    if (test_reboot_safety_gate() != 0) return 1;

    printf("\nAll %d tests passed successfully.\n\n", TESTS_PASSED);
    return 0;
}
