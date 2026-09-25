import { useState, useEffect } from "react";
import {
  AlertTriangle, CheckCircle2, XCircle, RotateCw, ShieldCheck, ChevronRight,
  ChevronDown, Terminal, Lock, ArrowLeft, Check, Info, Sliders,
  Wifi, DownloadCloud, HardDrive,
} from "../icons/glyphs";
import { useOS } from "../os";
import { G1Icon } from "../icons/IconSystem";

interface DiskItem {
  id: string;
  name: string;
  node: string;
  size: string;
  bus: string;
  model: string;
  isUsbBoot: boolean;
  isInternal: boolean;
  partitions: string[];
}

const DETECTED_DISKS: DiskItem[] = [
  {
    id: "sda",
    name: "Internal SSD",
    node: "/dev/sda",
    size: "500.1 GB",
    bus: "Internal SATA (AHCI)",
    model: "Crucial CT500MX500SSD1",
    isUsbBoot: false,
    isInternal: true,
    partitions: [
      "sda1: 256.0 MB (FAT32 · EFI System Partition)",
      "sda2: 4.0 GB (ext4 · MACLITE_DATA)",
      "sda3: 495.8 GB (ext4 · MACLITE_BASE)",
    ],
  },
  {
    id: "sdb",
    name: "Live USB Boot Media",
    node: "/dev/sdb",
    size: "15.9 GB",
    bus: "USB 3.0 Mass Storage",
    model: "SanDisk Ultra Flash Drive",
    isUsbBoot: true,
    isInternal: false,
    partitions: ["sdb1: 612 MB (ISO9660 · G1OS Live USB)"],
  },
];

interface WifiNetwork {
  ssid: string;
  security: "WPA2" | "WPA3" | "Open" | "Offline";
  signal: number; // 0-100
  freq: string;
  isOffline?: boolean;
}

const AVAILABLE_NETWORKS: WifiNetwork[] = [
  { ssid: "Starlight 5G", security: "WPA2", signal: 96, freq: "5 GHz" },
  { ssid: "iMac-Studio-5G", security: "WPA2", signal: 82, freq: "5 GHz" },
  { ssid: "CoffeeHouse Guest", security: "Open", signal: 54, freq: "2.4 GHz" },
  { ssid: "Offline Mode (Self-Contained)", security: "Offline", signal: 100, freq: "Local USB", isOffline: true },
];

export type InstallerStage =
  | "welcome"
  | "preflight"
  | "wifi_select"
  | "wifi_auth"
  | "network_validating"
  | "downloading"
  | "select"
  | "confirm"
  | "installing"
  | "verifying"
  | "summary"
  | "complete"
  | "error";

export type TestScenario =
  | "normal"
  | "wifi-auth-fail"
  | "dhcp-lease-fail"
  | "dns-resolution-fail"
  | "cdn-tls-fail"
  | "download-sha256-corrupt"
  | "target-disk-readonly"
  | "partition-table-error"
  | "mkfs-format-fail"
  | "failed-copy"
  | "failed-bootloader"
  | "missing-driver"
  | "corrupt-config"
  | "low-disk-space"
  | "interrupted-install"
  | "permission-bits-broken";

export interface DiagnosticCardData {
  phase: string;
  subPhase: string;
  action: string;
  command: string;
  status: "FAILED" | "WARNING" | "FATAL";
  elapsed: string;
  exitCode: number | string;
  error: string;
  recovery: string;
  canAutoRepair?: boolean;
  canFallbackOffline?: boolean;
}

interface VerificationCheckItem {
  id: string;
  domain: string;
  description: string;
  required: boolean;
  status: "pending" | "running" | "pass" | "fail" | "warn";
  details?: string;
}

const VERIFICATION_DOMAINS: Omit<VerificationCheckItem, "status">[] = [
  { id: "files", domain: "System Files", description: "Root hierarchy, system binaries, libraries, desktop shell & apps", required: true },
  { id: "fs", domain: "Filesystem", description: "Target partitions readable, writable, fsync completed, healthy mounts", required: true },
  { id: "bootloader", domain: "Bootloader", description: "Apple EFI fallback (BOOTX64.EFI), grub.cfg UUID binding, .disk_label", required: true },
  { id: "kernel", domain: "Kernel", description: "vmlinuz-g1os and initrd-g1os.img readable and bound to root UUID", required: true },
  { id: "drivers", domain: "Drivers", description: "GPU, CPU, Storage, USB [REQUIRED: OK], WiFi/BT [OPTIONAL: OK]", required: true },
  { id: "graphics", domain: "Graphics & Compositor", description: "mica-comp display server, hardware acceleration & software fallback", required: true },
  { id: "shell", domain: "Desktop Shell", description: "Desktop, Dock, Menu bar, Window manager, Launcher, Control Center", required: true },
  { id: "apps", domain: "Applications", description: "Essential multimedia apps (Finder, Browser, Video player, Settings)", required: true },
  { id: "network", domain: "Networking", description: "Network interfaces detected & configured (offline operation supported)", required: true },
  { id: "audio", domain: "Audio", description: "Audio subsystem loaded (absence handled gracefully without failure)", required: true },
  { id: "config", domain: "Configuration", description: "/etc/fstab syntax, UUID mapping, and g1os-installed metadata", required: true },
  { id: "services", domain: "Required Services", description: "Compositor, shell, network, audio, and session services configured", required: true },
  { id: "permissions", domain: "Permissions", description: "System executables +x, configuration readability, and root ownership", required: true },
  { id: "diskspace", domain: "Disk Space", description: "Sufficient free space confirmed on EFI, root, and data partitions", required: true },
  { id: "integrity", domain: "Integrity", description: "SHA256 checksums verified against official release manifest", required: true },
  { id: "nopartial", domain: "Installation State", description: "Zero interrupted copies, temporary markers, or pending migrations", required: true },
];

const SUMMARY_ITEMS = [
  "System files",
  "Filesystem",
  "Bootloader",
  "Kernel",
  "Graphics",
  "Desktop",
  "Drivers",
  "Configuration",
  "Required services",
  "Installation integrity",
];

const STORAGE_STATE_KEY = "g1os_installer_state_v1";

interface PersistedState {
  status: "PENDING" | "RUNNING" | "SUCCESS" | "FAILED" | "INCOMPLETE";
  stage: InstallerStage;
  selectedDiskId: string;
  wifiSsid: string;
  progress: number;
  scenario: TestScenario;
  timestamp: number;
}

export default function InstallerApp() {
  const os = useOS();
  const [stage, setStage] = useState<InstallerStage>("welcome");
  const [selectedDiskId, setSelectedDiskId] = useState("sda");
  const [confirmed, setConfirmed] = useState(false);
  const [progress, setProgress] = useState(0);
  const [currentStepText, setCurrentStepText] = useState("Preparing installation...");
  const [logs, setLogs] = useState<string[]>([]);
  const [showLogs, setShowLogs] = useState(false);

  // Wi-Fi and Networking State
  const [selectedNetwork, setSelectedNetwork] = useState<WifiNetwork>(AVAILABLE_NETWORKS[0]);
  const [wifiPassword, setWifiPassword] = useState("AppleAirport2010!");
  const [showPassword, setShowPassword] = useState(false);
  const [networkValidationSteps, setNetworkValidationSteps] = useState([
    { label: "Wi-Fi link state (wlan0)", status: "pending", detail: "Broadcom BCM43224 802.11a/b/g/n" },
    { label: "DHCP IP configuration", status: "pending", detail: "Requesting lease via udhcpc" },
    { label: "Default gateway reachable", status: "pending", detail: "ICMP echo ping test" },
    { label: "DNS host resolution", status: "pending", detail: "Resolving dist.g1os.org" },
    { label: "HTTPS package CDN link", status: "pending", detail: "TLS 1.3 certificate check" },
  ]);

  // Package Download State
  const [downloadProgress, setDownloadProgress] = useState(0);
  const downloadSpeed = "24.8 MB/s";
  const downloadEta = "12s";
  const [downloadTransferred, setDownloadTransferred] = useState("0 MB / 420 MB");
  const [downloadPhase, setDownloadPhase] = useState("Fetching base-system-v1.0.squashfs...");

  // Scenario testing state (§25 Final Acceptance Test)
  const [scenario, setScenario] = useState<TestScenario>("normal");
  const [repaired, setRepaired] = useState(false);

  // Diagnostic Card for deliberate failure testing
  const [diagnosticCard, setDiagnosticCard] = useState<DiagnosticCardData | null>(null);

  // Resume dialog state
  const [interruptedState, setInterruptedState] = useState<PersistedState | null>(null);

  // Verification checks state
  const [checks, setChecks] = useState<VerificationCheckItem[]>(
    VERIFICATION_DOMAINS.map((d) => ({ ...d, status: "pending" }))
  );

  const targetDisk = DETECTED_DISKS.find((d) => d.id === selectedDiskId) || DETECTED_DISKS[0];

  // Load and check persisted state on mount
  useEffect(() => {
    try {
      const raw = localStorage.getItem(STORAGE_STATE_KEY);
      if (raw) {
        const parsed: PersistedState = JSON.parse(raw);
        if (parsed.status === "RUNNING" || parsed.status === "INCOMPLETE" || parsed.status === "FAILED") {
          setInterruptedState(parsed);
        }
      }
    } catch {
      // Ignore localStorage errors
    }
  }, []);

  // Helper to persist state
  const persistState = (status: PersistedState["status"], curStage: InstallerStage, pct: number) => {
    try {
      const stateObj: PersistedState = {
        status,
        stage: curStage,
        selectedDiskId,
        wifiSsid: selectedNetwork.ssid,
        progress: pct,
        scenario,
        timestamp: Date.now(),
      };
      localStorage.setItem(STORAGE_STATE_KEY, JSON.stringify(stateObj));
    } catch {
      // Ignore localStorage write errors
    }
  };

  // Reset checks
  const resetChecks = () => {
    setChecks(VERIFICATION_DOMAINS.map((d) => ({ ...d, status: "pending" })));
    setDiagnosticCard(null);
  };

  // Resume interrupted session
  const handleResumeSession = () => {
    if (!interruptedState) return;
    setStage(interruptedState.stage);
    setSelectedDiskId(interruptedState.selectedDiskId || "sda");
    setProgress(interruptedState.progress || 0);
    setScenario(interruptedState.scenario || "normal");
    setInterruptedState(null);
    setLogs((prev) => [
      ...prev,
      `=== RESUMED FROM CHECKPOINT ===`,
      `Restored state: stage=${interruptedState.stage}, progress=${interruptedState.progress}%, disk=${interruptedState.selectedDiskId}`,
    ]);
  };

  const handleDiscardResume = () => {
    localStorage.removeItem(STORAGE_STATE_KEY);
    setInterruptedState(null);
  };

  // ----------------------------------------------------
  // Network Validation Flow
  // ----------------------------------------------------
  useEffect(() => {
    if (stage !== "network_validating") return;

    persistState("RUNNING", "network_validating", 15);
    setLogs([
      "=== STATE: NETWORK_VALIDATING ===",
      `+ Selected interface: wlan0 (Broadcom BCM43224 PCI-ID 14e4:4353)`,
      `+ Target SSID: ${selectedNetwork.ssid} (${selectedNetwork.security})`,
    ]);

    // Step 0: Wi-Fi Auth check
    if (scenario === "wifi-auth-fail") {
      setTimeout(() => {
        setDiagnosticCard({
          phase: "Network Configuration",
          subPhase: "Wi-Fi Authentication",
          action: "Authenticating with 802.11i WPA2-PSK access point",
          command: `wpa_supplicant -i wlan0 -c /tmp/wpa_supplicant.conf -B`,
          status: "FAILED",
          elapsed: "4.2s",
          exitCode: 2,
          error: "WPA: 4-Way Handshake failed: MIC verification failure (Invalid Pre-Shared Key).",
          recovery: "Re-enter the correct network passphrase or choose Offline Installation Mode.",
          canFallbackOffline: true,
        });
        setStage("error");
        persistState("FAILED", "error", 15);
      }, 1000);
      return;
    }

    // Sequentially step through validation
    const timers: NodeJS.Timeout[] = [];

    // Step 1: Link up
    timers.push(
      setTimeout(() => {
        setNetworkValidationSteps((prev) =>
          prev.map((s, i) =>
            i === 0 ? { ...s, status: "pass", detail: "wlan0 connected (-45 dBm, 300 Mbps)" } : s
          )
        );
        setLogs((prev) => [...prev, "[NET 1/5] wlan0: Link state UP, signal -45 dBm"]);
      }, 400)
    );

    // Step 2: DHCP Lease
    timers.push(
      setTimeout(() => {
        if (scenario === "dhcp-lease-fail") {
          setNetworkValidationSteps((prev) =>
            prev.map((s, i) => (i === 1 ? { ...s, status: "fail", detail: "udhcpc timed out (no lease offered)" } : s))
          );
          setDiagnosticCard({
            phase: "Network Configuration",
            subPhase: "DHCP Lease Acquisition",
            action: "Obtaining IPv4 lease from gateway",
            command: "udhcpc -i wlan0 -q -n -T 5",
            status: "FAILED",
            elapsed: "5.1s",
            exitCode: 1,
            error: "No DHCP offer received from router. IP address allocation failed.",
            recovery: "Check router DHCP pool exhaustion, or continue using self-contained Offline Mode.",
            canFallbackOffline: true,
          });
          setStage("error");
          persistState("FAILED", "error", 15);
          return;
        }

        setNetworkValidationSteps((prev) =>
          prev.map((s, i) =>
            i === 1 ? { ...s, status: "pass", detail: "Bound 192.168.1.105 (mask 255.255.255.0)" } : s
          )
        );
        setLogs((prev) => [...prev, "[NET 2/5] udhcpc: Bound to 192.168.1.105, gateway 192.168.1.1"]);
      }, 900)
    );

    // Step 3: Gateway ping
    timers.push(
      setTimeout(() => {
        if (scenario === "dhcp-lease-fail") return;

        setNetworkValidationSteps((prev) =>
          prev.map((s, i) =>
            i === 2 ? { ...s, status: "pass", detail: "192.168.1.1 ping RTT: 1.4 ms (0% loss)" } : s
          )
        );
        setLogs((prev) => [...prev, "[NET 3/5] ICMP ping 192.168.1.1: 0% packet loss, RTT 1.4ms"]);
      }, 1400)
    );

    // Step 4: DNS
    timers.push(
      setTimeout(() => {
        if (scenario === "dhcp-lease-fail") return;

        if (scenario === "dns-resolution-fail") {
          setNetworkValidationSteps((prev) =>
            prev.map((s, i) => (i === 3 ? { ...s, status: "fail", detail: "SERVFAIL resolving dist.g1os.org" } : s))
          );
          setDiagnosticCard({
            phase: "Network Configuration",
            subPhase: "DNS Host Resolution",
            action: "Resolving official distribution server domain",
            command: "nslookup dist.g1os.org 192.168.1.1",
            status: "FAILED",
            elapsed: "3.8s",
            exitCode: 1,
            error: "Nameserver returned SERVFAIL for dist.g1os.org. Remote package repository unreachable.",
            recovery: "Verify upstream DNS servers or proceed using the self-contained Offline Installer.",
            canFallbackOffline: true,
          });
          setStage("error");
          persistState("FAILED", "error", 15);
          return;
        }

        setNetworkValidationSteps((prev) =>
          prev.map((s, i) =>
            i === 3 ? { ...s, status: "pass", detail: "dist.g1os.org -> 142.250.190.46" } : s
          )
        );
        setLogs((prev) => [...prev, "[NET 4/5] DNS gethostbyname: dist.g1os.org -> 142.250.190.46"]);
      }, 1900)
    );

    // Step 5: CDN TLS Handshake
    timers.push(
      setTimeout(() => {
        if (scenario === "dhcp-lease-fail" || scenario === "dns-resolution-fail") return;

        if (scenario === "cdn-tls-fail") {
          setNetworkValidationSteps((prev) =>
            prev.map((s, i) => (i === 4 ? { ...s, status: "fail", detail: "TLS 1.3 handshake reset by peer" } : s))
          );
          setDiagnosticCard({
            phase: "Network Pre-Flight",
            subPhase: "HTTPS Package CDN Connection",
            action: "Establishing secure TLS 1.3 channel to package CDN",
            command: "curl -I https://cdn.g1os.org/v1/health",
            status: "FAILED",
            elapsed: "2.5s",
            exitCode: 35,
            error: "curl: (35) error:0A000410:SSL routines::sslv3 alert handshake failure.",
            recovery: "Check system real-time clock (RTC) battery or switch to Offline Installation.",
            canFallbackOffline: true,
          });
          setStage("error");
          persistState("FAILED", "error", 15);
          return;
        }

        setNetworkValidationSteps((prev) =>
          prev.map((s, i) =>
            i === 4 ? { ...s, status: "pass", detail: "TLS 1.3 Handshake OK · HTTP/2 200" } : s
          )
        );
        setLogs((prev) => [
          ...prev,
          "[NET 5/5] TLS 1.3 Handshake OK. CDN connection verified.",
          "✓ All network validation stages passed. Proceeding to package download.",
        ]);

        setTimeout(() => {
          setStage("downloading");
        }, 600);
      }, 2500)
    );

    return () => timers.forEach(clearTimeout);
  }, [stage, scenario, selectedNetwork]);

  // ----------------------------------------------------
  // Package Downloading Flow
  // ----------------------------------------------------
  useEffect(() => {
    if (stage !== "downloading") return;

    persistState("RUNNING", "downloading", 20);
    setDownloadProgress(0);
    setLogs((prev) => [
      ...prev,
      "=== STATE: DOWNLOADING ===",
      "+ Package repository: https://cdn.g1os.org/v1/packages",
      "+ Fetching maclite-base-rootfs.sqfs (SHA-256 verified)...",
    ]);

    let step = 0;
    const interval = setInterval(() => {
      step++;
      const pct = Math.min(step * 15, 100);
      setDownloadProgress(pct);
      setDownloadTransferred(`${Math.round((pct / 100) * 420)} MB / 420 MB`);

      if (pct === 30) {
        setDownloadPhase("Downloading live system base image (maclite-base.sqfs)...");
      } else if (pct === 60) {
        setDownloadPhase("Downloading kernel modules & firmware (kernel-modules-6.6.21.tar.zst)...");
      } else if (pct === 90) {
        setDownloadPhase("Verifying package SHA-256 signatures against official release manifest...");
      }

      if (pct >= 100) {
        clearInterval(interval);

        // Check for download corruption scenario
        if (scenario === "download-sha256-corrupt") {
          setDiagnosticCard({
            phase: "Package Download",
            subPhase: "SHA-256 Integrity Verification",
            action: "Validating checksum of maclite-base-rootfs.sqfs",
            command: "sha256sum -c /tmp/packages/manifest.sha256",
            status: "FATAL",
            elapsed: "8.4s",
            exitCode: 1,
            error: "CHECKSUM MISMATCH: Expected 8f4c2e5b61... but computed e3b0c44298... Package payload is corrupted.",
            recovery: "Re-download the base packages, or switch to the bundled Offline Live Media packages.",
            canFallbackOffline: true,
          });
          setStage("error");
          persistState("FAILED", "error", 20);
          return;
        }

        setLogs((prev) => [
          ...prev,
          "✓ SHA-256 package verification: 8f4c2e5b61a38094... [OK]",
          "✓ Base system and kernel modules successfully cached to RAM.",
        ]);

        setTimeout(() => {
          setStage("select");
        }, 500);
      }
    }, 280);

    return () => clearInterval(interval);
  }, [stage, scenario]);

  // ----------------------------------------------------
  // Execution & Verification Flow
  // ----------------------------------------------------
  useEffect(() => {
    if (stage !== "installing" && stage !== "verifying") return;

    if (stage === "installing") {
      resetChecks();
      persistState("RUNNING", "installing", 30);
      setLogs([
        "=== STATE: PREPARING ===",
        "[STEP 1 (10%)]: Validated target disk, live media, base system, kernel and initramfs",
        `+ Target device: ${targetDisk.node} (${targetDisk.model})`,
        "+ Live USB media /dev/sdb excluded and write-protected",
        "[STEP 2 (20%)]: Unmounting target partitions cleanly",
      ]);
      setProgress(30);

      // Check for immediate disk errors
      if (scenario === "target-disk-readonly") {
        setTimeout(() => {
          setDiagnosticCard({
            phase: "Partitioning & Storage",
            subPhase: "Device Open",
            action: "Opening target drive for exclusive partition table rewrite",
            command: `blockdev --setrw ${targetDisk.node}`,
            status: "FATAL",
            elapsed: "0.8s",
            exitCode: 30, // EROFS
            error: `EROFS: Read-only file system on ${targetDisk.node}. Hardware controller write-protect active.`,
            recovery: "Check internal SATA connection cable or drive health via SMART diagnostic utility.",
          });
          setStage("error");
          persistState("FAILED", "error", 30);
        }, 800);
        return;
      }

      if (scenario === "partition-table-error") {
        setTimeout(() => {
          setDiagnosticCard({
            phase: "Partitioning",
            subPhase: "GPT Partition Table Creation",
            action: "Writing fresh GUID Partition Table via sgdisk",
            command: `sgdisk -n 1:0:+256M -t 1:ef00 ${targetDisk.node}`,
            status: "FATAL",
            elapsed: "1.2s",
            exitCode: 2,
            error: "sgdisk returned exit code 2: Error allocating partition overlapping secondary GPT header.",
            recovery: "Execute 'sgdisk --zap-all' to clean invalid leftover partition tables.",
          });
          setStage("error");
          persistState("FAILED", "error", 30);
        }, 1100);
        return;
      }

      const t1 = setTimeout(() => {
        if (scenario === "mkfs-format-fail") {
          setDiagnosticCard({
            phase: "Filesystem Creation",
            subPhase: "Format ext4 Base Partition",
            action: "Creating ext4 filesystem with 4K block size",
            command: "mkfs.ext4 -F -L MACLITE_BASE /dev/sda3",
            status: "FATAL",
            elapsed: "1.9s",
            exitCode: 1,
            error: "mke2fs: Could not allocate inode table: I/O error while writing block 32768.",
            recovery: "Run badblocks check on internal SSD or re-run installer after re-powering.",
          });
          setStage("error");
          persistState("FAILED", "error", 45);
          return;
        }

        setProgress(45);
        persistState("RUNNING", "installing", 45);
        setCurrentStepText("Formatting fresh filesystems (FAT32 & ext4)...");
        setLogs((prev) => [
          ...prev,
          "=== STATE: INSTALLING ===",
          `[STEP 3 (30%)]: Repartitioning target disk ${targetDisk.node} (GPT, EFI, Data, Base)`,
          "+ sgdisk --zap-all " + targetDisk.node,
          "+ sgdisk -n 1:0:+256M -t 1:ef00 -c 1:MACLITE_BOOT " + targetDisk.node,
          "+ sgdisk -n 2:0:+4G -t 2:8300 -c 2:MACLITE_DATA " + targetDisk.node,
          "+ sgdisk -n 3:0:0 -t 3:8300 -c 3:MACLITE_BASE " + targetDisk.node,
          "[STEP 4 (45%)]: Formatting fresh filesystems",
          "+ mkfs.vfat -F32 -n MACLITE_BOOT /dev/sda1 (UUID=78FA-C9B2)",
          "+ mkfs.ext4 -F -L MACLITE_DATA /dev/sda2 (UUID=4a12b3c4-7d8e-4f01-9a23-11bb22cc33dd)",
          "+ mkfs.ext4 -F -L MACLITE_BASE /dev/sda3 (UUID=e78d910a-3142-4f81-9b16-5fa4e872c019)",
        ]);
      }, 700);

      const t2 = setTimeout(() => {
        if (scenario === "mkfs-format-fail") return;

        setProgress(60);
        persistState("RUNNING", "installing", 60);
        setCurrentStepText("Installing immutable base system & configuring fstab...");
        setLogs((prev) => [
          ...prev,
          "[STEP 5 (60%)]: Copying verified base system to target root",
          "+ Synchronizing system image to /dev/sda3...",
          "+ Generating UUID-bound /etc/fstab",
          "+ Emitted INSTALLED_BASE_UUID=e78d910a-3142-4f81-9b16-5fa4e872c019",
        ]);
      }, 1500);

      const t3 = setTimeout(() => {
        if (scenario === "mkfs-format-fail") return;

        setProgress(72);
        persistState("RUNNING", "installing", 72);
        setCurrentStepText("Installing Apple EFI fallback bootloader & grub.cfg...");
        setLogs((prev) => [
          ...prev,
          "[STEP 6 (72%)]: Installing Apple EFI bootloader and UUID-bound boot configuration",
          "+ Installing /EFI/BOOT/BOOTX64.EFI into ESP (/dev/sda1)",
          "+ Generating grub.cfg with root=UUID=e78d910a-3142-4f81-9b16-5fa4e872c019",
          "+ Setting Apple Safe Graphics: nomodeset radeon.modeset=0 fbcon=map:0 reboot=pci panic=0",
          "+ Creating Apple Option Boot .disk_label ('G1OS')",
        ]);
      }, 2300);

      const t4 = setTimeout(() => {
        if (scenario === "mkfs-format-fail") return;

        setProgress(78);
        persistState("RUNNING", "verifying", 78);
        setCurrentStepText("Finalizing disk sync & preparing mandatory verification...");
        setLogs((prev) => [
          ...prev,
          "=== STATE: FINALIZING ===",
          "[STEP 7 (78%)]: Filesystem synchronization and unmount complete",
          "+ sync && umount /tmp/g1os-base /tmp/g1os-boot",
          "=== STATE: VERIFYING ===",
          "[STAGE]: Transitioning to mandatory Final Verification stage. No direct completion allowed.",
        ]);
        setStage("verifying");
      }, 3100);

      return () => {
        clearTimeout(t1);
        clearTimeout(t2);
        clearTimeout(t3);
        clearTimeout(t4);
      };
    }

    if (stage === "verifying") {
      let checkIdx = 0;
      const runCheckInterval = setInterval(() => {
        if (checkIdx >= VERIFICATION_DOMAINS.length) {
          clearInterval(runCheckInterval);
          // All checks passed!
          setProgress(100);
          persistState("SUCCESS", "summary", 100);
          setCurrentStepText("All required checks passed. Installation Complete.");
          setLogs((prev) => [
            ...prev,
            "=== STATE: COMPLETED ===",
            "✓ Final Verification Passed: 16/16 domains certified healthy.",
            "✓ 100% authoritative progress reached.",
            "SUCCESS: G1OS installation verified healthy and bootable on /dev/sda.",
          ]);
          setTimeout(() => setStage("summary"), 600);
          return;
        }

        const domain = VERIFICATION_DOMAINS[checkIdx];
        const pct = Math.min(80 + Math.round((checkIdx / VERIFICATION_DOMAINS.length) * 20), 99);
        setProgress(pct);
        persistState("RUNNING", "verifying", pct);
        setCurrentStepText(`Verifying ${domain.domain}...`);

        // Check for injected scenario failures
        let willFail = false;
        let failDiagnostic: DiagnosticCardData | null = null;

        if (scenario === "failed-copy" && domain.id === "files") {
          willFail = true;
          failDiagnostic = {
            phase: "Final Verification",
            subPhase: "System Files Domain",
            action: "Verifying presence of desktop shell binary /usr/bin/mica-shell",
            command: "test -x /mnt/target/usr/bin/mica-shell",
            status: "FATAL",
            elapsed: "0.2s",
            exitCode: 1,
            error: "Required system binary /usr/bin/mica-shell is missing or has zero length.",
            recovery: "Retry the installation to re-synchronize system binaries.",
          };
        } else if (scenario === "failed-bootloader" && domain.id === "bootloader") {
          willFail = true;
          failDiagnostic = {
            phase: "Final Verification",
            subPhase: "Bootloader Integrity Domain",
            action: "Verifying Apple EFI fallback bootloader executable",
            command: "test -f /mnt/target/boot/efi/EFI/BOOT/BOOTX64.EFI",
            status: "FATAL",
            elapsed: "0.1s",
            exitCode: 1,
            error: "Apple EFI fallback bootloader /EFI/BOOT/BOOTX64.EFI is missing or corrupt.",
            recovery: "Re-run installer to reinstall bootloader and recreate EFI partition structure.",
          };
        } else if (scenario === "missing-driver" && domain.id === "drivers") {
          willFail = true;
          failDiagnostic = {
            phase: "Final Verification",
            subPhase: "Kernel Driver Modules Domain",
            action: "Checking GPU driver module radeon.ko / amdgpu.ko in initramfs",
            command: "modinfo -b /mnt/target/lib/modules/6.6.21-g1os radeon",
            status: "FATAL",
            elapsed: "0.4s",
            exitCode: 1,
            error: "Required hardware driver module 'radeon' is missing from rootfs.",
            recovery: "Ensure Safe Graphics fallback mode is selected.",
          };
        } else if (scenario === "corrupt-config" && domain.id === "config" && !repaired) {
          willFail = true;
          failDiagnostic = {
            phase: "Final Verification",
            subPhase: "Configuration Domain",
            action: "Verifying /etc/fstab UUID syntax and root partition binding",
            command: "grep -E '^UUID=[0-9a-f-]{36} +/ +' /mnt/target/etc/fstab",
            status: "FAILED",
            elapsed: "0.1s",
            exitCode: 1,
            error: "Invalid /etc/fstab syntax: Root filesystem is missing UUID binding.",
            recovery: "Click Automatic Repair to regenerate UUID-bound /etc/fstab automatically.",
            canAutoRepair: true,
          };
        } else if (scenario === "permission-bits-broken" && domain.id === "permissions" && !repaired) {
          willFail = true;
          failDiagnostic = {
            phase: "Final Verification",
            subPhase: "Permissions Domain",
            action: "Auditing executable bits on /bin/sh and /usr/bin/mica-comp",
            command: "test -x /mnt/target/bin/sh && test -x /mnt/target/usr/bin/mica-comp",
            status: "FAILED",
            elapsed: "0.2s",
            exitCode: 1,
            error: "Security Audit Failure: /bin/sh has permission mode 0644 (missing +x execute bit).",
            recovery: "Click Automatic Repair to restore standard 0755 root permissions.",
            canAutoRepair: true,
          };
        } else if (scenario === "low-disk-space" && domain.id === "diskspace") {
          willFail = true;
          failDiagnostic = {
            phase: "Final Verification",
            subPhase: "Disk Space Allocation Domain",
            action: "Querying free space on EFI System Partition (/dev/sda1)",
            command: "df -m /mnt/target/boot/efi",
            status: "FATAL",
            elapsed: "0.1s",
            exitCode: 1,
            error: "Critically low disk space: EFI partition has only 4.1 MB remaining (minimum 10 MB required).",
            recovery: "Re-partition internal drive with larger EFI partition allocation.",
          };
        } else if (scenario === "interrupted-install" && domain.id === "nopartial") {
          willFail = true;
          failDiagnostic = {
            phase: "Final Verification",
            subPhase: "Installation State Domain",
            action: "Scanning for temporary partial markers (/etc/.g1os_installing)",
            command: "test ! -f /mnt/target/etc/.g1os_installing",
            status: "FATAL",
            elapsed: "0.1s",
            exitCode: 1,
            error: "Incomplete installation marker detected: Previous copy was aborted unexpectedly.",
            recovery: "Perform a clean re-installation to ensure no partial libraries exist.",
          };
        }

        if (willFail && failDiagnostic) {
          clearInterval(runCheckInterval);
          setChecks((prev) =>
            prev.map((c, i) => (i === checkIdx ? { ...c, status: "fail", details: failDiagnostic?.error } : c))
          );
          setDiagnosticCard(failDiagnostic);
          setLogs((prev) => [
            ...prev,
            `Verification FAIL: ${domain.domain} — ${failDiagnostic?.error}`,
            "ERROR: Installation halted by No-Break Guarantee. System reboot is disabled.",
          ]);
          setStage("error");
          persistState("FAILED", "error", pct);
          return;
        }

        // Passed this check
        setChecks((prev) =>
          prev.map((c, i) =>
            i === checkIdx
              ? {
                  ...c,
                  status: "pass",
                  details: "Verified healthy and consistent",
                }
              : c
          )
        );

        setLogs((prev) => [
          ...prev,
          `[VERIFY ${checkIdx + 1} (${pct}%)]: ${domain.domain} [PASS] — ${domain.description}`,
        ]);

        checkIdx++;
      }, 200);

      return () => clearInterval(runCheckInterval);
    }
  }, [stage, scenario, repaired]);

  const handleStartInstall = () => {
    if (!confirmed || targetDisk.isUsbBoot) return;
    setRepaired(false);
    setStage("installing");
  };

  const handleRetry = () => {
    setRepaired(false);
    setDiagnosticCard(null);
    if (stage === "error") {
      // Determine appropriate stage to retry
      if (scenario.includes("wifi") || scenario.includes("dhcp") || scenario.includes("dns") || scenario.includes("cdn")) {
        setStage("network_validating");
      } else if (scenario.includes("download")) {
        setStage("downloading");
      } else {
        setStage("installing");
      }
    } else {
      setStage("installing");
    }
  };

  const handleRepair = () => {
    setLogs((prev) => [
      ...prev,
      "=== INITIATING AUTOMATIC SAFE REPAIR ===",
      "+ Regenerating missing /etc/fstab configuration with verified UUIDs...",
      "+ Repairing permissions on system mount points (chmod 0755 /bin /usr/bin)...",
      "+ Re-triggering Final Verification...",
    ]);
    setRepaired(true);
    setDiagnosticCard(null);
    setStage("verifying");
  };

  const handleFallbackOffline = () => {
    setLogs((prev) => [
      ...prev,
      "=== FALLBACK TO OFFLINE INSTALLATION ===",
      "+ Remote package download skipped.",
      "+ Switching to verified bundled packages from live USB media /dev/sdb.",
    ]);
    setDiagnosticCard(null);
    setStage("select");
  };

  const handleRestart = () => {
    if (stage !== "complete" || progress < 100) return;
    localStorage.removeItem(STORAGE_STATE_KEY);
    os.notify("G1OS", "Restarting", "System restart confirmed. Unplug USB media to boot into internal G1OS.", "restart");
    os.setPowerState("restarting");
    window.setTimeout(() => os.setPowerState("running"), 1600);
  };

  return (
    <div className="flex h-full w-full flex-col bg-[#1c1e26] text-white select-none relative">
      {/* Interrupted Session Resume Modal Banner */}
      {interruptedState && (
        <div className="absolute top-16 left-6 right-6 z-50 rounded-xl border border-amber-500/40 bg-[#1c140a]/95 p-4 shadow-2xl backdrop-blur-md animate-in fade-in slide-in-from-top-4">
          <div className="flex items-start justify-between gap-4">
            <div className="flex items-start gap-3">
              <div className="rounded-lg bg-amber-500/20 p-2 text-amber-400">
                <AlertTriangle size={20} />
              </div>
              <div>
                <h4 className="text-[13.5px] font-bold text-amber-200">Interrupted Installation Session Detected</h4>
                <p className="mt-0.5 text-[12px] text-amber-300/80 leading-relaxed">
                  An unfinished installation run was detected at stage{" "}
                  <span className="font-mono font-semibold text-white">{interruptedState.stage}</span> (progress:{" "}
                  <span className="font-semibold text-white">{interruptedState.progress}%</span>) on{" "}
                  <span className="font-mono text-white">{interruptedState.selectedDiskId}</span>.
                </p>
              </div>
            </div>

            <div className="flex items-center gap-2 flex-none">
              <button
                type="button"
                onClick={handleDiscardResume}
                className="rounded-lg border border-white/20 bg-white/5 px-3 py-1.5 text-[12px] font-medium text-white/80 hover:bg-white/10 transition-all cursor-pointer"
              >
                Start Fresh
              </button>
              <button
                type="button"
                onClick={handleResumeSession}
                className="rounded-lg bg-amber-600 px-4 py-1.5 text-[12px] font-semibold text-white shadow-md hover:bg-amber-500 transition-all cursor-pointer"
              >
                Resume Session
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Header */}
      <div className="flex h-14 flex-none items-center justify-between border-b border-white/10 bg-[#232632] px-6">
        <div className="flex items-center gap-3">
          <G1Icon name="installer" size={30} />
          <div>
            <h1 className="text-[14px] font-semibold tracking-wide">G1OS Installer</h1>
            <p className="text-[11px] text-white/50">iMac Mid-2010 Setup Wizard · Final Verification & No-Break Guarantee</p>
          </div>
        </div>

        {/* Acceptance Test Scenario Selector */}
        <div className="flex items-center gap-3">
          <div className="flex items-center gap-1.5 rounded-lg border border-white/10 bg-black/30 px-2 py-1 text-[11px] text-white/70">
            <Sliders size={12} className="text-blue-400" />
            <span className="text-white/50">Test Scenario:</span>
            <select
              value={scenario}
              onChange={(e) => setScenario(e.target.value as TestScenario)}
              disabled={stage === "installing" || stage === "verifying"}
              className="bg-transparent font-medium text-blue-300 outline-none cursor-pointer max-w-[190px] truncate"
            >
              <option value="normal" className="bg-[#232632] text-white">Normal (100% Pass)</option>
              <option value="wifi-auth-fail" className="bg-[#232632] text-white">1. Wi-Fi Handshake Fail</option>
              <option value="dhcp-lease-fail" className="bg-[#232632] text-white">2. DHCP Lease Timeout</option>
              <option value="dns-resolution-fail" className="bg-[#232632] text-white">3. DNS SERVFAIL</option>
              <option value="cdn-tls-fail" className="bg-[#232632] text-white">4. CDN TLS Handshake Fail</option>
              <option value="download-sha256-corrupt" className="bg-[#232632] text-white">5. Package SHA-256 Corrupt</option>
              <option value="target-disk-readonly" className="bg-[#232632] text-white">6. Target Disk Read-Only</option>
              <option value="partition-table-error" className="bg-[#232632] text-white">7. Partition Geometry Error</option>
              <option value="mkfs-format-fail" className="bg-[#232632] text-white">8. mkfs.ext4 Superblock Fail</option>
              <option value="failed-copy" className="bg-[#232632] text-white">9. Base Copy Missing Binary</option>
              <option value="failed-bootloader" className="bg-[#232632] text-white">10. Bootloader EFI Missing</option>
              <option value="missing-driver" className="bg-[#232632] text-white">11. GPU Driver Missing</option>
              <option value="corrupt-config" className="bg-[#232632] text-white">12. Corrupt fstab (Auto-Repair)</option>
              <option value="permission-bits-broken" className="bg-[#232632] text-white">13. Permission Error (Auto-Repair)</option>
              <option value="low-disk-space" className="bg-[#232632] text-white">14. Low Disk Space on EFI</option>
              <option value="interrupted-install" className="bg-[#232632] text-white">15. Incomplete Marker Found</option>
            </select>
          </div>

          <span className="flex items-center gap-1.5 rounded-full bg-emerald-500/15 px-2.5 py-1 text-[11px] font-medium text-emerald-300 border border-emerald-500/20">
            <ShieldCheck size={13} /> Live USB Protected
          </span>
        </div>
      </div>

      {/* Main Content Area */}
      <div className="flex-1 overflow-y-auto p-6 flex flex-col justify-center">
        {/* 1. Welcome Screen */}
        {stage === "welcome" && (
          <div className="mx-auto max-w-lg text-center space-y-6 animate-in fade-in duration-300">
            <div className="mx-auto flex items-center justify-center drop-shadow-2xl">
              <G1Icon name="installer" size={76} />
            </div>

            <div>
              <h2 className="text-[26px] font-bold tracking-tight text-white">Install G1OS</h2>
              <p className="mt-1.5 text-[15px] font-medium text-blue-400">“Giving life to older machines.”</p>
              <p className="mt-3 text-[13px] leading-relaxed text-white/60">
                A lightweight operating system built for older hardware with dedicated Apple EFI fallback,
                UUID-bound storage, and an authoritative <strong>Final Verification stage</strong> ensuring
                the installed system is bootable, healthy, and ready before completion.
              </p>
            </div>

            {/* Feature Cards */}
            <div className="grid grid-cols-2 gap-3 text-left">
              <div className="rounded-xl border border-white/10 bg-white/[0.03] p-3.5">
                <div className="flex items-center gap-2 text-[13px] font-medium text-white/90">
                  <Check size={16} className="text-emerald-400" />
                  <span>No-Break Guarantee</span>
                </div>
                <p className="mt-1 text-[11.5px] text-white/50">
                  Every subsystem is verified before reboot. Never reports success when broken.
                </p>
              </div>

              <div className="rounded-xl border border-white/10 bg-white/[0.03] p-3.5">
                <div className="flex items-center gap-2 text-[13px] font-medium text-white/90">
                  <Check size={16} className="text-emerald-400" />
                  <span>Apple EFI Fallback</span>
                </div>
                <p className="mt-1 text-[11.5px] text-white/50">
                  Boots reliably even after NVRAM reset, fully bound to internal storage UUID.
                </p>
              </div>
            </div>

            <div className="pt-2">
              <button
                type="button"
                onClick={() => setStage("preflight")}
                className="inline-flex items-center gap-2 rounded-xl bg-blue-600 px-7 py-3 text-[14px] font-semibold text-white shadow-lg shadow-blue-900/40 hover:bg-blue-500 transition-all cursor-pointer"
              >
                Continue to Hardware Check <ChevronRight size={16} />
              </button>
            </div>
          </div>
        )}

        {/* 1.5. Hardware Preflight Check Screen */}
        {stage === "preflight" && (
          <div className="mx-auto max-w-2xl w-full space-y-6 animate-in fade-in duration-300">
            <div>
              <div className="inline-flex items-center gap-2 px-3 py-1 rounded-full bg-blue-500/15 border border-blue-500/30 text-blue-400 text-[11px] font-semibold tracking-wide uppercase mb-2">
                <ShieldCheck size={14} /> Stage 1: Hardware Pre-Flight Audit
              </div>
              <h2 className="text-[20px] font-bold text-white">Target Hardware Verification</h2>
              <p className="mt-1 text-[13px] text-white/60">
                Auditing iMac Mid-2010 compatibility, safe graphics fallback, internal storage, and network options.
              </p>
            </div>

            {/* Checklist of audited hardware */}
            <div className="grid grid-cols-1 gap-2.5">
              <div className="flex items-center justify-between p-3 rounded-xl border border-emerald-500/25 bg-emerald-950/15">
                <div className="flex items-center gap-3">
                  <div className="flex h-7 w-7 items-center justify-center rounded-full bg-emerald-500/20 text-emerald-400">
                    <Check size={16} />
                  </div>
                  <div>
                    <div className="text-[13px] font-semibold text-white">CPU Architecture</div>
                    <div className="text-[11.5px] text-white/60">Intel Core i3/i5/i7 (x86_64) · 64-bit Kernel · mitigations=off</div>
                  </div>
                </div>
                <span className="text-[11px] font-mono px-2 py-0.5 rounded bg-emerald-500/20 text-emerald-300 font-medium">PASSED</span>
              </div>

              <div className="flex items-center justify-between p-3 rounded-xl border border-emerald-500/25 bg-emerald-950/15">
                <div className="flex items-center gap-3">
                  <div className="flex h-7 w-7 items-center justify-center rounded-full bg-emerald-500/20 text-emerald-400">
                    <Check size={16} />
                  </div>
                  <div>
                    <div className="text-[13px] font-semibold text-white">System Memory (RAM)</div>
                    <div className="text-[11.5px] text-white/60">4.0 GB DDR3-1333 detected · System idle RSS &lt; 250 MB</div>
                  </div>
                </div>
                <span className="text-[11px] font-mono px-2 py-0.5 rounded bg-emerald-500/20 text-emerald-300 font-medium">PASSED</span>
              </div>

              <div className="flex items-center justify-between p-3 rounded-xl border border-emerald-500/25 bg-emerald-950/15">
                <div className="flex items-center gap-3">
                  <div className="flex h-7 w-7 items-center justify-center rounded-full bg-emerald-500/20 text-emerald-400">
                    <Check size={16} />
                  </div>
                  <div>
                    <div className="text-[13px] font-semibold text-white">Graphics &amp; Compositor Engine</div>
                    <div className="text-[11.5px] text-white/60">Safe Graphics Active · nomodeset + EFI GOP Framebuffer (Prevents VBIOS freeze)</div>
                  </div>
                </div>
                <span className="text-[11px] font-mono px-2 py-0.5 rounded bg-emerald-500/20 text-emerald-300 font-medium">CERTIFIED</span>
              </div>

              <div className="flex items-center justify-between p-3 rounded-xl border border-emerald-500/25 bg-emerald-950/15">
                <div className="flex items-center gap-3">
                  <div className="flex h-7 w-7 items-center justify-center rounded-full bg-emerald-500/20 text-emerald-400">
                    <Check size={16} />
                  </div>
                  <div>
                    <div className="text-[13px] font-semibold text-white">Target Storage Controller</div>
                    <div className="text-[11.5px] text-white/60">Internal SATA AHCI (/dev/sda Crucial 500GB SSD) · GPT Partitionable</div>
                  </div>
                </div>
                <span className="text-[11px] font-mono px-2 py-0.5 rounded bg-emerald-500/20 text-emerald-300 font-medium">READY</span>
              </div>

              <div className="flex items-center justify-between p-3 rounded-xl border border-emerald-500/25 bg-emerald-950/15">
                <div className="flex items-center gap-3">
                  <div className="flex h-7 w-7 items-center justify-center rounded-full bg-emerald-500/20 text-emerald-400">
                    <Check size={16} />
                  </div>
                  <div>
                    <div className="text-[13px] font-semibold text-white">Live Media Protection</div>
                    <div className="text-[11.5px] text-white/60">USB flash drive /dev/sdb detected · Write-lock safeguard armed</div>
                  </div>
                </div>
                <span className="text-[11px] font-mono px-2 py-0.5 rounded bg-emerald-500/20 text-emerald-300 font-medium">PROTECTED</span>
              </div>

              <div className="flex items-center justify-between p-3 rounded-xl border border-emerald-500/25 bg-emerald-950/15">
                <div className="flex items-center gap-3">
                  <div className="flex h-7 w-7 items-center justify-center rounded-full bg-emerald-500/20 text-emerald-400">
                    <Check size={16} />
                  </div>
                  <div>
                    <div className="text-[13px] font-semibold text-white">Boot Environment</div>
                    <div className="text-[11.5px] text-white/60">Apple EFI 1.10 · BOOTX64.EFI fallback &amp; UUID-bound GRUB verified</div>
                  </div>
                </div>
                <span className="text-[11px] font-mono px-2 py-0.5 rounded bg-emerald-500/20 text-emerald-300 font-medium">VERIFIED</span>
              </div>
            </div>

            {/* Navigation buttons */}
            <div className="flex items-center justify-between pt-2">
              <button
                type="button"
                onClick={() => setStage("welcome")}
                className="flex items-center gap-1.5 text-[13px] text-white/60 hover:text-white transition-colors cursor-pointer"
              >
                <ArrowLeft size={15} /> Back
              </button>

              <button
                type="button"
                onClick={() => setStage("wifi_select")}
                className="flex items-center gap-2 rounded-xl bg-blue-600 px-6 py-2.5 text-[13px] font-semibold text-white shadow-md hover:bg-blue-500 transition-all cursor-pointer"
              >
                Configure Network <ChevronRight size={15} />
              </button>
            </div>
          </div>
        )}

        {/* 1.6. Wi-Fi Network Selection Screen */}
        {stage === "wifi_select" && (
          <div className="mx-auto max-w-xl w-full space-y-5 animate-in fade-in duration-300">
            <div>
              <div className="inline-flex items-center gap-2 px-3 py-1 rounded-full bg-blue-500/15 border border-blue-500/30 text-blue-400 text-[11px] font-semibold tracking-wide uppercase mb-2">
                <Wifi size={14} /> Stage 2: Wi-Fi &amp; Network Setup
              </div>
              <h2 className="text-[20px] font-bold text-white">Select Wi-Fi Network</h2>
              <p className="mt-1 text-[13px] text-white/60">
                Connect to download updated packages, or select Offline Mode to install directly from the live USB.
              </p>
            </div>

            <div className="space-y-2.5">
              {AVAILABLE_NETWORKS.map((net) => {
                const isSelected = selectedNetwork.ssid === net.ssid;
                return (
                  <div
                    key={net.ssid}
                    onClick={() => setSelectedNetwork(net)}
                    className={`flex items-center justify-between p-3.5 rounded-xl border transition-all cursor-pointer ${
                      isSelected
                        ? "border-blue-500 bg-blue-600/15 shadow-[0_0_15px_rgba(59,130,246,0.15)] ring-1 ring-blue-500"
                        : "border-white/10 bg-white/[0.03] hover:border-white/20 hover:bg-white/[0.06]"
                    }`}
                  >
                    <div className="flex items-center gap-3">
                      <div className="flex h-9 w-9 items-center justify-center rounded-lg bg-white/10 text-white/80">
                        {net.isOffline ? <HardDrive size={18} /> : <Wifi size={18} />}
                      </div>
                      <div>
                        <div className="flex items-center gap-2">
                          <span className="text-[13.5px] font-semibold text-white">{net.ssid}</span>
                          {net.isOffline ? (
                            <span className="rounded bg-emerald-500/20 px-2 py-0.5 text-[10px] font-medium text-emerald-300 border border-emerald-500/30">
                              Zero Downloads Required
                            </span>
                          ) : (
                            <span className="rounded bg-white/10 px-1.5 py-0.5 text-[10px] font-mono text-white/60">
                              {net.security} · {net.freq}
                            </span>
                          )}
                        </div>
                        <p className="mt-0.5 text-[11.5px] text-white/50">
                          {net.isOffline
                            ? "Complete live filesystem image already present on bootable media"
                            : `Signal strength: ${net.signal}% · Ready for automated multi-stage network validation`}
                        </p>
                      </div>
                    </div>

                    <div>
                      {isSelected ? (
                        <div className="flex h-6 w-6 items-center justify-center rounded-full bg-blue-500 text-white">
                          <Check size={14} />
                        </div>
                      ) : (
                        <div className="h-4 w-4 rounded-full border border-white/20" />
                      )}
                    </div>
                  </div>
                );
              })}
            </div>

            {/* Navigation buttons */}
            <div className="flex items-center justify-between pt-2">
              <button
                type="button"
                onClick={() => setStage("preflight")}
                className="flex items-center gap-1.5 text-[13px] text-white/60 hover:text-white transition-colors cursor-pointer"
              >
                <ArrowLeft size={15} /> Back
              </button>

              <button
                type="button"
                onClick={() => {
                  if (selectedNetwork.isOffline) {
                    setStage("select");
                  } else if (selectedNetwork.security === "Open") {
                    setStage("network_validating");
                  } else {
                    setStage("wifi_auth");
                  }
                }}
                className="flex items-center gap-2 rounded-xl bg-blue-600 px-6 py-2.5 text-[13px] font-semibold text-white shadow-md hover:bg-blue-500 transition-all cursor-pointer"
              >
                {selectedNetwork.isOffline ? "Proceed Offline" : "Connect & Verify"} <ChevronRight size={15} />
              </button>
            </div>
          </div>
        )}

        {/* 1.7. Wi-Fi Authentication Screen */}
        {stage === "wifi_auth" && (
          <div className="mx-auto max-w-md w-full space-y-6 animate-in fade-in duration-300">
            <div>
              <h2 className="text-[20px] font-bold text-white">Join “{selectedNetwork.ssid}”</h2>
              <p className="mt-1 text-[13px] text-white/60">
                Enter the WPA2/WPA3 Pre-Shared Key for this Wi-Fi network.
              </p>
            </div>

            <div className="rounded-xl border border-white/10 bg-white/[0.03] p-4 space-y-4">
              <div>
                <label className="block text-[12px] font-medium text-white/70 mb-1.5">
                  Network Password
                </label>
                <div className="relative">
                  <input
                    type={showPassword ? "text" : "password"}
                    value={wifiPassword}
                    onChange={(e) => setWifiPassword(e.target.value)}
                    className="w-full rounded-lg border border-white/20 bg-black/40 px-3 py-2 text-[13px] text-white placeholder-white/30 focus:border-blue-500 focus:outline-none"
                    placeholder="Enter password"
                  />
                  <button
                    type="button"
                    onClick={() => setShowPassword(!showPassword)}
                    className="absolute right-2.5 top-2.5 text-[11px] text-blue-400 hover:text-blue-300 cursor-pointer"
                  >
                    {showPassword ? "Hide" : "Show"}
                  </button>
                </div>
              </div>

              <div className="flex items-center gap-2 text-[11.5px] text-white/50">
                <Lock size={13} className="text-emerald-400" />
                <span>Protected with WPA2-Personal (AES-CCMP)</span>
              </div>
            </div>

            <div className="flex items-center justify-between pt-2">
              <button
                type="button"
                onClick={() => setStage("wifi_select")}
                className="flex items-center gap-1.5 text-[13px] text-white/60 hover:text-white transition-colors cursor-pointer"
              >
                <ArrowLeft size={15} /> Back
              </button>

              <button
                type="button"
                onClick={() => setStage("network_validating")}
                className="flex items-center gap-2 rounded-xl bg-blue-600 px-6 py-2.5 text-[13px] font-semibold text-white shadow-md hover:bg-blue-500 transition-all cursor-pointer"
              >
                Join Network <ChevronRight size={15} />
              </button>
            </div>
          </div>
        )}

        {/* 1.8. Network Validation Screen */}
        {stage === "network_validating" && (
          <div className="mx-auto max-w-lg w-full space-y-6 animate-in fade-in duration-300">
            <div className="text-center space-y-1.5">
              <div className="inline-block animate-spin text-blue-400">
                <RotateCw size={32} />
              </div>
              <h2 className="text-[20px] font-bold text-white">Validating Network Connection</h2>
              <p className="text-[13px] text-white/60">
                Testing link, DHCP lease, DNS resolution, and package CDN reachability...
              </p>
            </div>

            <div className="rounded-xl border border-white/10 bg-white/[0.03] p-4 space-y-3">
              {networkValidationSteps.map((step, idx) => (
                <div key={idx} className="flex items-center justify-between p-2 rounded-lg bg-black/20 text-[12.5px]">
                  <div className="flex items-center gap-2.5">
                    {step.status === "pass" ? (
                      <Check size={15} className="text-emerald-400 flex-none" />
                    ) : step.status === "fail" ? (
                      <XCircle size={15} className="text-red-400 flex-none" />
                    ) : (
                      <RotateCw size={14} className="text-blue-400 animate-spin flex-none" />
                    )}
                    <span className="font-medium text-white/90">{step.label}</span>
                  </div>
                  <span className="text-[11px] font-mono text-white/50">{step.detail}</span>
                </div>
              ))}
            </div>

            <div className="flex justify-center">
              <button
                type="button"
                onClick={handleFallbackOffline}
                className="text-[12px] text-white/50 hover:text-white underline cursor-pointer"
              >
                Skip and continue using bundled offline packages
              </button>
            </div>
          </div>
        )}

        {/* 1.9. Package Downloading Screen */}
        {stage === "downloading" && (
          <div className="mx-auto max-w-lg w-full space-y-6 animate-in fade-in duration-300">
            <div className="text-center space-y-1.5">
              <div className="mx-auto flex h-12 w-12 items-center justify-center rounded-full bg-blue-500/20 text-blue-400">
                <DownloadCloud size={24} />
              </div>
              <h2 className="text-[20px] font-bold text-white">Downloading G1OS Packages</h2>
              <p className="text-[13px] text-blue-300 font-medium">{downloadPhase}</p>
            </div>

            {/* Progress Bar */}
            <div className="space-y-2">
              <div className="flex justify-between text-[12px] font-medium text-white/70">
                <span className="text-blue-400 font-mono">{downloadTransferred}</span>
                <span className="font-mono text-white">{downloadProgress}%</span>
              </div>

              <div className="h-2.5 w-full overflow-hidden rounded-full bg-white/10 p-0.5">
                <div
                  className="h-full rounded-full bg-gradient-to-r from-blue-500 to-cyan-400 transition-all duration-200"
                  style={{ width: `${downloadProgress}%` }}
                />
              </div>

              <div className="flex justify-between text-[11px] font-mono text-white/50 pt-1">
                <span>Speed: {downloadSpeed}</span>
                <span>ETA: {downloadEta}</span>
              </div>
            </div>

            <div className="rounded-xl border border-white/10 bg-white/[0.02] p-3 text-[11.5px] text-white/60 space-y-1">
              <div>✓ Release Mirror: <span className="font-mono text-white/80">cdn.g1os.org/v1</span> (TLS 1.3 encrypted)</div>
              <div>✓ SHA-256 verification active on complete archive stream</div>
            </div>
          </div>
        )}

        {/* 2. Select Disk Screen */}
        {stage === "select" && (
          <div className="mx-auto max-w-2xl w-full space-y-6 animate-in fade-in duration-300">
            <div>
              <div className="inline-flex items-center gap-2 px-3 py-1 rounded-full bg-blue-500/15 border border-blue-500/30 text-blue-400 text-[11px] font-semibold tracking-wide uppercase mb-2">
                <HardDrive size={14} /> Stage 3: Destination Storage
              </div>
              <h2 className="text-[20px] font-bold text-white">Choose Where to Install G1OS</h2>
              <p className="mt-1 text-[13px] text-white/60">
                Live USB media is automatically protected and excluded. Select the internal drive.
              </p>
            </div>

            {/* Disk Cards */}
            <div className="space-y-3">
              {DETECTED_DISKS.map((d) => {
                const isSelected = selectedDiskId === d.id && !d.isUsbBoot;
                return (
                  <div
                    key={d.id}
                    onClick={() => {
                      if (!d.isUsbBoot) {
                        setSelectedDiskId(d.id);
                        setConfirmed(false);
                      }
                    }}
                    className={`relative flex items-center justify-between rounded-xl border p-4 transition-all ${
                      d.isUsbBoot
                        ? "cursor-not-allowed border-amber-500/30 bg-amber-950/15 opacity-75"
                        : isSelected
                        ? "cursor-pointer border-blue-500 bg-blue-600/15 shadow-[0_0_20px_rgba(59,130,246,0.15)] ring-1 ring-blue-500"
                        : "cursor-pointer border-white/10 bg-white/[0.03] hover:border-white/20 hover:bg-white/[0.06]"
                    }`}
                  >
                    <div className="flex items-center gap-3.5">
                      <div className="flex-none drop-shadow">
                        <G1Icon name={d.isUsbBoot ? "usb-drive" : "macintosh-hd"} size={36} />
                      </div>
                      <div>
                        <div className="flex items-center gap-2">
                          <span className="text-[14px] font-semibold text-white/90">{d.model}</span>
                          <span className="rounded bg-white/10 px-1.5 py-0.5 font-mono text-[10px] text-white/70">
                            {d.node}
                          </span>
                        </div>
                        <p className="mt-0.5 text-[12px] text-white/50">
                          {d.size} · {d.bus} {d.isInternal && "· Recommended"}
                        </p>
                      </div>
                    </div>

                    <div>
                      {d.isUsbBoot ? (
                        <div className="flex items-center gap-1.5 rounded-full bg-amber-500/20 px-3 py-1 text-[11px] font-medium text-amber-300 border border-amber-500/30">
                          <Lock size={12} /> Live USB (Protected)
                        </div>
                      ) : isSelected ? (
                        <div className="flex items-center gap-1 rounded-full bg-blue-500 px-3 py-1 text-[11px] font-medium text-white shadow-sm">
                          Selected Target
                        </div>
                      ) : (
                        <button className="text-[12px] text-white/40 hover:text-white/80">Select</button>
                      )}
                    </div>
                  </div>
                );
              })}
            </div>

            {/* Existing Partitions Preview */}
            <div className="rounded-xl border border-white/10 bg-white/[0.02] p-4">
              <h3 className="text-[12.5px] font-medium text-white/75 flex items-center gap-2">
                <Info size={14} className="text-blue-400" />
                Target Layout on {targetDisk.node} ({targetDisk.model}):
              </h3>
              <ul className="mt-2 space-y-1.5">
                {targetDisk.partitions.map((p, idx) => (
                  <li key={idx} className="flex items-center gap-2 text-[12px] text-white/60">
                    <span className="h-1.5 w-1.5 rounded-full bg-white/30" />
                    {p}
                  </li>
                ))}
              </ul>
            </div>

            {/* Navigation buttons */}
            <div className="flex items-center justify-between pt-2">
              <button
                type="button"
                onClick={() => setStage("wifi_select")}
                className="flex items-center gap-1.5 text-[13px] text-white/60 hover:text-white transition-colors cursor-pointer"
              >
                <ArrowLeft size={15} /> Back
              </button>

              <button
                type="button"
                onClick={() => setStage("confirm")}
                className="flex items-center gap-2 rounded-xl bg-blue-600 px-6 py-2.5 text-[13px] font-semibold text-white shadow-md hover:bg-blue-500 transition-all cursor-pointer"
              >
                Continue <ChevronRight size={15} />
              </button>
            </div>
          </div>
        )}

        {/* 3. Interactive Confirmation Screen */}
        {stage === "confirm" && (
          <div className="mx-auto max-w-xl w-full space-y-6 animate-in fade-in duration-300">
            <div>
              <h2 className="text-[20px] font-bold text-white">Confirm Installation Destination</h2>
              <p className="mt-1 text-[13px] text-white/60">
                Please review your installation target carefully before proceeding.
              </p>
            </div>

            {/* Target Disk Summary Card */}
            <div className="rounded-xl border border-blue-500/30 bg-blue-950/20 p-5 space-y-3">
              <div className="text-[12px] font-semibold uppercase tracking-wider text-blue-400">
                Install G1OS on:
              </div>
              <div className="flex items-start gap-4">
                <div className="flex-none drop-shadow">
                  <G1Icon name="macintosh-hd" size={44} />
                </div>
                <div className="space-y-1">
                  <h3 className="text-[16px] font-bold text-white">{targetDisk.model}</h3>
                  <div className="text-[13px] text-white/70">
                    Device: <span className="font-mono text-white font-medium">{targetDisk.node}</span> · Capacity:{" "}
                    <span className="text-white font-medium">{targetDisk.size}</span>
                  </div>
                </div>
              </div>
            </div>

            {/* Destructive Warning */}
            <div className="flex items-start gap-3.5 rounded-xl border border-red-500/30 bg-red-950/25 p-4">
              <AlertTriangle size={22} className="flex-none text-red-400 mt-0.5" />
              <div className="space-y-1">
                <h4 className="text-[13.5px] font-bold text-red-200">
                  This will erase the selected disk ({targetDisk.node})
                </h4>
                <p className="text-[12px] leading-relaxed text-red-300/80">
                  All existing partitions and data will be permanently removed. The installer will partition, format,
                  install G1OS, and perform rigorous pre-flight verification before completing.
                </p>
              </div>
            </div>

            {/* Explicit Confirmation Checkbox */}
            <label className="flex items-center gap-3 rounded-xl border border-white/10 bg-white/[0.03] p-4 cursor-pointer hover:border-white/20 transition-all">
              <input
                type="checkbox"
                checked={confirmed}
                onChange={(e) => setConfirmed(e.target.checked)}
                className="h-4 w-4 rounded border-white/30 bg-white/10 text-red-500 focus:ring-0 cursor-pointer"
              />
              <span className="text-[13px] font-medium text-white/85">
                I understand that all data on <span className="font-mono text-white font-semibold">{targetDisk.node}</span>{" "}
                will be permanently erased.
              </span>
            </label>

            {/* Actions */}
            <div className="flex items-center justify-between pt-2">
              <button
                type="button"
                onClick={() => setStage("select")}
                className="flex items-center gap-1.5 text-[13px] text-white/60 hover:text-white transition-colors cursor-pointer"
              >
                <ArrowLeft size={15} /> Back
              </button>

              <button
                type="button"
                onClick={handleStartInstall}
                disabled={!confirmed}
                className={`flex items-center gap-2 rounded-xl px-6 py-2.5 text-[13px] font-semibold transition-all ${
                  confirmed
                    ? "bg-red-600 text-white hover:bg-red-500 shadow-lg shadow-red-900/30 cursor-pointer"
                    : "bg-white/10 text-white/30 cursor-not-allowed"
                }`}
              >
                Erase &amp; Install G1OS <ChevronRight size={15} />
              </button>
            </div>
          </div>
        )}

        {/* 4. Installing Stage */}
        {stage === "installing" && (
          <div className="mx-auto max-w-xl w-full py-6 space-y-6 animate-in fade-in duration-300">
            <div className="text-center space-y-2">
              <div className="inline-block animate-spin text-blue-400">
                <RotateCw size={36} />
              </div>
              <h2 className="text-[22px] font-bold text-white">Installing G1OS</h2>
              <p className="text-[13px] text-blue-300 font-medium">
                {currentStepText}
              </p>
            </div>

            {/* Progress Bar & Stage Ribbon */}
            <div className="space-y-3">
              <div className="flex justify-between text-[12px] font-medium text-white/70">
                <span className="text-blue-400 font-semibold uppercase tracking-wider text-[11px]">
                  State: INSTALLING
                </span>
                <span className="font-mono text-white/90">{progress}%</span>
              </div>

              <div className="h-2.5 w-full overflow-hidden rounded-full bg-white/10 p-0.5">
                <div
                  className="h-full rounded-full bg-gradient-to-r from-blue-500 via-sky-400 to-indigo-500 transition-all duration-300 shadow-sm"
                  style={{ width: `${progress}%` }}
                />
              </div>

              <div className="flex justify-between text-[10px] text-white/40 pt-1">
                <span>Preparing</span>
                <span>Partitioning</span>
                <span>Formatting</span>
                <span>Installing</span>
                <span>Finalizing</span>
                <span className="text-blue-400 font-semibold">Verification</span>
              </div>
            </div>

            {/* Collapsible Details */}
            <div className="rounded-xl border border-white/10 bg-white/[0.03] overflow-hidden">
              <button
                type="button"
                onClick={() => setShowLogs(!showLogs)}
                className="w-full flex items-center justify-between px-4 py-3 text-[12px] font-medium text-white/75 hover:bg-white/[0.04] transition-colors cursor-pointer"
              >
                <span className="flex items-center gap-2">
                  <Terminal size={14} className="text-blue-400" />
                  Installation Activity &amp; Logs
                </span>
                <span className="flex items-center gap-1 text-[11px] text-blue-400">
                  {showLogs ? "Hide details" : "Show live log"}
                  {showLogs ? <ChevronDown size={14} /> : <ChevronRight size={14} />}
                </span>
              </button>

              {showLogs && (
                <div className="h-44 overflow-y-auto border-t border-white/10 bg-black/60 p-3.5 font-mono text-[11px] text-emerald-400/90 space-y-1">
                  {logs.map((l, i) => (
                    <div key={i} className="leading-relaxed">{l}</div>
                  ))}
                </div>
              )}
            </div>
          </div>
        )}

        {/* 5. Verification Stage (Section 18) */}
        {stage === "verifying" && (
          <div className="mx-auto max-w-2xl w-full py-4 space-y-5 animate-in fade-in duration-300">
            <div className="text-center space-y-1.5">
              <div className="inline-flex items-center gap-2 px-3 py-1 rounded-full bg-emerald-500/15 border border-emerald-500/30 text-emerald-400 text-[11px] font-semibold tracking-wide uppercase">
                <ShieldCheck size={14} /> Mandatory Verification Stage
              </div>
              <h2 className="text-[22px] font-bold text-white">Verifying G1OS</h2>
              <p className="text-[13px] text-emerald-300 font-medium">
                {currentStepText}
              </p>
            </div>

            {/* Verification Progress Bar */}
            <div className="space-y-2">
              <div className="flex justify-between text-[12px] font-medium text-white/70">
                <span className="text-emerald-400 font-semibold uppercase tracking-wider text-[11px]">
                  Authoritative Progress
                </span>
                <span className="font-mono text-emerald-300 text-[13px] font-bold">{progress}%</span>
              </div>

              <div className="h-2.5 w-full overflow-hidden rounded-full bg-white/10 p-0.5">
                <div
                  className="h-full rounded-full bg-gradient-to-r from-teal-500 via-emerald-400 to-green-500 transition-all duration-200 shadow-sm"
                  style={{ width: `${progress}%` }}
                />
              </div>
            </div>

            {/* Live 16-Domain Verification Checklist Grid */}
            <div className="rounded-xl border border-white/10 bg-white/[0.02] p-3.5 max-h-56 overflow-y-auto space-y-2">
              <div className="text-[11px] font-semibold uppercase tracking-wider text-white/50 px-1">
                Integrity &amp; Bootability Domains (16 Checks)
              </div>
              <div className="grid grid-cols-2 gap-2 text-[12px]">
                {checks.map((chk) => (
                  <div
                    key={chk.id}
                    className={`flex items-center justify-between p-2 rounded-lg border transition-all ${
                      chk.status === "pass"
                        ? "border-emerald-500/30 bg-emerald-950/20 text-emerald-200"
                        : chk.status === "warn"
                        ? "border-amber-500/30 bg-amber-950/20 text-amber-200"
                        : chk.status === "fail"
                        ? "border-red-500/40 bg-red-950/30 text-red-200"
                        : "border-white/5 bg-white/[0.01] text-white/40"
                    }`}
                  >
                    <div className="flex items-center gap-2 truncate pr-2">
                      {chk.status === "pass" ? (
                        <Check size={14} className="text-emerald-400 flex-none" />
                      ) : chk.status === "warn" ? (
                        <AlertTriangle size={14} className="text-amber-400 flex-none" />
                      ) : chk.status === "fail" ? (
                        <XCircle size={14} className="text-red-400 flex-none" />
                      ) : (
                        <span className="h-2 w-2 rounded-full bg-white/20 flex-none" />
                      )}
                      <span className="truncate font-medium">{chk.domain}</span>
                    </div>

                    <span className="text-[10px] uppercase font-mono px-1.5 py-0.5 rounded bg-black/40">
                      {chk.status}
                    </span>
                  </div>
                ))}
              </div>
            </div>

            {/* Details toggle */}
            <div className="rounded-xl border border-white/10 bg-white/[0.03] overflow-hidden">
              <button
                type="button"
                onClick={() => setShowLogs(!showLogs)}
                className="w-full flex items-center justify-between px-4 py-2.5 text-[12px] font-medium text-white/75 hover:bg-white/[0.04] transition-colors cursor-pointer"
              >
                <span className="flex items-center gap-2">
                  <Terminal size={14} className="text-emerald-400" />
                  Verification Console
                </span>
                <span className="flex items-center gap-1 text-[11px] text-emerald-400">
                  {showLogs ? "Hide details" : "Show live log"}
                  {showLogs ? <ChevronDown size={14} /> : <ChevronRight size={14} />}
                </span>
              </button>

              {showLogs && (
                <div className="h-32 overflow-y-auto border-t border-white/10 bg-black/60 p-3 font-mono text-[11px] text-emerald-400/90 space-y-1">
                  {logs.map((l, i) => (
                    <div key={i} className="leading-relaxed">{l}</div>
                  ))}
                </div>
              )}
            </div>
          </div>
        )}

        {/* 6. Section 19: Verification Summary Screen */}
        {stage === "summary" && (
          <div className="mx-auto max-w-lg w-full py-4 text-center space-y-5 animate-in fade-in duration-300">
            <div className="mx-auto grid h-14 w-14 place-items-center rounded-full bg-emerald-500/20 text-emerald-400 ring-8 ring-emerald-500/10">
              <ShieldCheck size={32} />
            </div>

            <div>
              <h2 className="text-[22px] font-bold text-white">G1OS Installation</h2>
              <p className="mt-1 text-[13px] text-emerald-300 font-medium">
                All required verification checks passed.
              </p>
            </div>

            {/* 10 Domain Checklist Summary Box */}
            <div className="rounded-2xl border border-emerald-500/30 bg-emerald-950/15 p-5 text-left space-y-3">
              <div className="grid grid-cols-2 gap-2.5 text-[12.5px]">
                {SUMMARY_ITEMS.map((item, idx) => (
                  <div key={idx} className="flex items-center gap-2.5 text-white/90">
                    <div className="flex h-5 w-5 items-center justify-center rounded-full bg-emerald-500/20 text-emerald-400">
                      <Check size={13} />
                    </div>
                    <span className="font-medium">{item}</span>
                  </div>
                ))}
              </div>

              <div className="pt-2 border-t border-white/10 text-center">
                <span className="text-[13px] font-semibold text-emerald-300">
                  Everything is ready.
                </span>
              </div>
            </div>

            {/* Actions */}
            <div className="flex items-center justify-between pt-2">
              <button
                type="button"
                onClick={() => setShowLogs(!showLogs)}
                className="flex items-center gap-1.5 text-[12px] text-white/60 hover:text-white transition-colors cursor-pointer"
              >
                <Terminal size={14} /> {showLogs ? "Hide details" : "View Details"}
              </button>

              <button
                type="button"
                onClick={() => setStage("complete")}
                className="inline-flex items-center gap-2 rounded-xl bg-blue-600 px-6 py-2.5 text-[13px] font-semibold text-white shadow-lg hover:bg-blue-500 transition-all cursor-pointer"
              >
                Continue to Complete <ChevronRight size={15} />
              </button>
            </div>

            {showLogs && (
              <div className="h-32 text-left overflow-y-auto rounded-xl border border-white/10 bg-black/60 p-3 font-mono text-[11px] text-emerald-400/90 space-y-1">
                {logs.map((l, i) => (
                  <div key={i} className="leading-relaxed">{l}</div>
                ))}
              </div>
            )}
          </div>
        )}

        {/* 7. Section 24: Installation Complete Screen & Reboot Safety Gate */}
        {stage === "complete" && (
          <div className="mx-auto max-w-lg w-full py-4 text-center space-y-5 animate-in fade-in duration-300">
            <div className="mx-auto grid h-16 w-16 place-items-center rounded-full bg-emerald-500/20 text-emerald-400 ring-8 ring-emerald-500/10">
              <CheckCircle2 size={38} />
            </div>

            <div>
              <div className="inline-block rounded-full bg-emerald-500/20 px-3 py-1 font-mono text-[12px] font-bold text-emerald-300 mb-2">
                100% · VERIFIED
              </div>
              <h2 className="text-[22px] font-bold text-white">Installation Complete</h2>
              <p className="mt-1.5 text-[14px] font-medium text-emerald-300">
                All required verification checks passed. G1OS is ready to start.
              </p>
              <p className="mt-2 text-[12.5px] leading-relaxed text-white/60">
                Remove the USB installer before restarting your iMac so it boots cleanly into the internal installation.
              </p>
            </div>

            {/* Checklist summary */}
            <div className="rounded-xl border border-white/10 bg-white/[0.03] p-4 text-left space-y-1.5 text-[12px] text-white/75">
              <div className="flex items-center gap-2">
                <Check size={13} className="text-emerald-400" />
                <span>Apple EFI 1.1 fallback <span className="font-mono text-white/90">/EFI/BOOT/BOOTX64.EFI</span> verified</span>
              </div>
              <div className="flex items-center gap-2">
                <Check size={13} className="text-emerald-400" />
                <span>GRUB configuration strictly bound to target UUID (no USB dependency)</span>
              </div>
              <div className="flex items-center gap-2">
                <Check size={13} className="text-emerald-400" />
                <span>Kernel, drivers, and desktop compositor certified</span>
              </div>
            </div>

            {/* Section 24: Reboot Safety Gate - Restart button is only active if 100% verified */}
            <div className="pt-2">
              <button
                type="button"
                onClick={handleRestart}
                disabled={progress < 100}
                className="inline-flex items-center gap-2.5 rounded-xl bg-blue-600 px-8 py-3 text-[14px] font-semibold text-white shadow-xl shadow-blue-900/50 hover:bg-blue-500 transition-all cursor-pointer disabled:opacity-50 disabled:cursor-not-allowed"
              >
                <RotateCw size={17} /> Restart iMac Now
              </button>
            </div>
          </div>
        )}

        {/* 8. Section 20: Failure Summary Screen with Automatic Repair and Rich Diagnostic Card */}
        {stage === "error" && (
          <div className="mx-auto max-w-xl w-full py-4 text-center space-y-5 animate-in fade-in duration-300">
            <div className="mx-auto grid h-14 w-14 place-items-center rounded-full bg-red-500/20 text-red-400 ring-8 ring-red-500/10">
              <XCircle size={34} />
            </div>

            <div>
              <h2 className="text-[20px] font-bold text-white">Installation Could Not Be Completed</h2>
              <p className="mt-1 text-[12.5px] text-red-300/80 max-w-md mx-auto">
                A verification or execution pre-condition failed. The No-Break Guarantee has halted progress to protect hardware.
              </p>
            </div>

            {/* Rich Diagnostic Card (§25 Acceptance Test Format) */}
            {diagnosticCard && (
              <div className="rounded-xl border border-red-500/30 bg-[#251314]/90 p-4 text-left shadow-lg space-y-2.5">
                <div className="flex items-center justify-between border-b border-red-500/20 pb-2">
                  <div className="flex items-center gap-2 text-[12.5px] font-bold text-red-200">
                    <AlertTriangle size={15} className="text-red-400" />
                    <span>DIAGNOSTIC CARD: {diagnosticCard.phase}</span>
                  </div>
                  <span className="rounded bg-red-500/25 px-2 py-0.5 font-mono text-[10px] font-bold text-red-300 border border-red-500/30">
                    STATUS: {diagnosticCard.status} (exit {diagnosticCard.exitCode})
                  </span>
                </div>

                <div className="grid grid-cols-2 gap-2 text-[11.5px] text-white/70">
                  <div><span className="text-white/40">Sub-phase:</span> <span className="text-white font-medium">{diagnosticCard.subPhase}</span></div>
                  <div><span className="text-white/40">Elapsed:</span> <span className="text-white font-medium">{diagnosticCard.elapsed}</span></div>
                </div>

                <div className="text-[11.5px]">
                  <span className="text-white/40">Action:</span>{" "}
                  <span className="text-white/90">{diagnosticCard.action}</span>
                </div>

                <div className="rounded-lg bg-black/60 p-2 font-mono text-[11px] text-amber-300/90 break-all border border-white/5">
                  <span className="text-white/40">$ </span>{diagnosticCard.command}
                </div>

                <div className="rounded-lg bg-red-950/40 p-2.5 border border-red-500/20 text-[11.5px] text-red-200">
                  <div className="font-semibold text-red-300 mb-0.5">Error Detail:</div>
                  <div className="leading-relaxed">{diagnosticCard.error}</div>
                </div>

                <div className="rounded-lg bg-emerald-950/30 p-2.5 border border-emerald-500/20 text-[11.5px] text-emerald-200">
                  <div className="font-semibold text-emerald-300 mb-0.5">Suggested Recovery:</div>
                  <div className="leading-relaxed">{diagnosticCard.recovery}</div>
                </div>
              </div>
            )}

            {/* Error Log Box */}
            <div className="rounded-xl border border-red-500/20 bg-black/60 p-3 text-left font-mono text-[11px] text-red-300/90 h-28 overflow-y-auto space-y-1">
              {logs.slice(-6).map((l, i) => (
                <div key={i} className="leading-relaxed">{l}</div>
              ))}
            </div>

            {/* Action buttons: [Retry] [Repair] [Fallback to Offline] [View Details] */}
            <div className="flex items-center justify-center gap-2.5 pt-1">
              <button
                type="button"
                onClick={handleRetry}
                className="inline-flex items-center gap-1.5 rounded-xl bg-white/10 px-4 py-2 text-[12.5px] font-semibold text-white hover:bg-white/20 transition-all cursor-pointer"
              >
                <RotateCw size={13} /> Retry Stage
              </button>

              {diagnosticCard?.canFallbackOffline && (
                <button
                  type="button"
                  onClick={handleFallbackOffline}
                  className="inline-flex items-center gap-1.5 rounded-xl bg-blue-600 px-4 py-2 text-[12.5px] font-semibold text-white shadow-md hover:bg-blue-500 transition-all cursor-pointer"
                >
                  <HardDrive size={13} /> Continue in Offline Mode
                </button>
              )}

              {diagnosticCard?.canAutoRepair && (
                <button
                  type="button"
                  onClick={handleRepair}
                  className="inline-flex items-center gap-1.5 rounded-xl bg-emerald-600 px-5 py-2 text-[12.5px] font-semibold text-white shadow-lg shadow-emerald-900/40 hover:bg-emerald-500 transition-all cursor-pointer"
                >
                  <ShieldCheck size={14} /> Automatic Repair &amp; Re-Verify
                </button>
              )}

              <button
                type="button"
                onClick={() => setShowLogs(!showLogs)}
                className="inline-flex items-center gap-1.5 rounded-xl border border-white/20 px-3.5 py-2 text-[12.5px] font-medium text-white/80 hover:bg-white/10 transition-all cursor-pointer"
              >
                <Terminal size={13} /> {showLogs ? "Hide Logs" : "Logs"}
              </button>
            </div>

            <p className="text-[11px] text-white/40">
              The system reboot gate is locked. G1OS will never declare success or reboot on a broken installation.
            </p>
          </div>
        )}
      </div>
    </div>
  );
}
