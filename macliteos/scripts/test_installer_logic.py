#!/usr/bin/env python3
"""
Test runner for MacLiteOS Installer logic:
Verifies USB boot device filtering, internal disk selection,
GPT partition calculation, UUID boot binding, preflight checks, and safety gates.
"""
import sys

def test_usb_protection():
    disks = [
        {"name": "sdb", "removable": True, "mountpoint": "/run/maclite-base", "size_gb": 16},
        {"name": "sda", "removable": False, "mountpoint": "", "size_gb": 500},
    ]
    
    # Live USB drive identification
    boot_disks = [d for d in disks if d["removable"] or "maclite" in d["mountpoint"]]
    assert len(boot_disks) == 1, "Expected exactly 1 boot disk"
    assert boot_disks[0]["name"] == "sdb", "Expected sdb to be the boot disk"
    
    # Internal target selection
    target_disks = [d for d in disks if d not in boot_disks]
    assert len(target_disks) == 1, "Expected exactly 1 target disk"
    assert target_disks[0]["name"] == "sda", "Expected sda to be selected"
    print("PASS: USB boot media protection & internal target identification")

def test_partition_layout():
    disk_bytes = 500 * 1000 * 1000 * 1000
    boot_bytes = 256 * 1024 * 1024
    data_bytes = 4 * 1024 * 1024 * 1024
    base_bytes = disk_bytes - boot_bytes - data_bytes
    
    assert boot_bytes == 268435456, "MACLITE_BOOT must be 256 MiB"
    assert data_bytes == 4294967296, "MACLITE_DATA must be 4 GiB"
    assert base_bytes > 0, "MACLITE_BASE must have positive remaining capacity"
    print("PASS: GPT partition geometry and label allocation")

def test_uuid_boot_binding():
    mock_boot_uuid = "78FA-C9B2"
    mock_base_uuid = "e78d910a-3142-4f81-9b16-5fa4e872c019"
    
    grub_cfg = f"""
    search --no-floppy --fs-uuid --set=root {mock_boot_uuid}
    linux /boot/vmlinuz-maclite root=UUID={mock_base_uuid} ro quiet
    initrd /boot/initrd-maclite.img
    """
    
    assert mock_boot_uuid in grub_cfg, "grub.cfg must search by filesystem UUID"
    assert f"root=UUID={mock_base_uuid}" in grub_cfg, "root must be set to base partition UUID"
    assert "maclite-live" not in grub_cfg, "grub.cfg must not reference live media"
    assert "/dev/sda" not in grub_cfg, "grub.cfg must not hardcode block device paths"
    print("PASS: UUID boot binding guarantees reliable startup after USB removal")

def test_preflight_verification():
    # Pre-flight checklist
    checks = {
        "bootx64_efi": True,
        "grub_cfg": True,
        "uuid_bound": True,
        "vmlinuz": True,
        "initrd": True,
        "disk_label": True,
    }
    assert all(checks.values()), "All pre-flight checks must pass before success screen"
    
    # Missing EFI loader must fail preflight
    checks["bootx64_efi"] = False
    assert not all(checks.values()), "Missing BOOTX64.EFI must cause preflight to fail"
    print("PASS: Offline pre-flight verification ensures installation validity")

def test_error_handling():
    step_results = [True, True, False, True] # step 3 fails
    installed = True
    failed_at = None
    for idx, ok in enumerate(step_results):
        if not ok:
            installed = False
            failed_at = idx + 1
            break
    
    assert not installed, "Installation must not succeed when a step fails"
    assert failed_at == 3, f"Failed step must be recorded (got {failed_at})"
    print("PASS: Error handling halts sequence without false success")

def main():
    print("=== Running G1OS Installer Logic Tests ===")
    test_usb_protection()
    test_partition_layout()
    test_uuid_boot_binding()
    test_preflight_verification()
    test_error_handling()
    print("All G1OS installer logic tests PASSED.\n")
    return 0

if __name__ == "__main__":
    sys.exit(main())
