import { useState, useEffect } from "react";
import {
  AlertTriangle, CheckCircle2, XCircle, RotateCw, ShieldCheck, ChevronRight,
  ChevronDown, Terminal, Lock, ArrowLeft, Check, Info, Sliders,
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

type InstallerStage =
  | "welcome"
  | "select"
  | "confirm"
  | "installing"
  | "verifying"
  | "summary"
  | "complete"
  | "error";

type TestScenario =
  | "normal"
  | "failed-copy"
  | "failed-bootloader"
  | "missing-driver"
  | "corrupt-config"
  | "low-disk-space"
  | "interrupted-install";

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

export default function InstallerApp() {
  const os = useOS();
  const [stage, setStage] = useState<InstallerStage>("welcome");
  const [selectedDiskId, setSelectedDiskId] = useState("sda");
  const [confirmed, setConfirmed] = useState(false);
  const [progress, setProgress] = useState(0);
  const [currentStepText, setCurrentStepText] = useState("Preparing installation...");
  const [logs, setLogs] = useState<string[]>([]);
  const [showLogs, setShowLogs] = useState(false);

  // Scenario testing state (§25 Final Acceptance Test)
  const [scenario, setScenario] = useState<TestScenario>("normal");
  const [repaired, setRepaired] = useState(false);

  // Verification checks state
  const [checks, setChecks] = useState<VerificationCheckItem[]>(
    VERIFICATION_DOMAINS.map((d) => ({ ...d, status: "pending" }))
  );
  const [failureInfo, setFailureInfo] = useState<{ title: string; message: string; canRepair: boolean } | null>(null);

  const targetDisk = DETECTED_DISKS.find((d) => d.id === selectedDiskId) || DETECTED_DISKS[0];

  // Reset checks
  const resetChecks = () => {
    setChecks(VERIFICATION_DOMAINS.map((d) => ({ ...d, status: "pending" })));
    setFailureInfo(null);
  };

  // Execution flow
  useEffect(() => {
    if (stage !== "installing" && stage !== "verifying") return;

    if (stage === "installing") {
      resetChecks();
      setLogs([
        "=== STATE: PREPARING ===",
        "[STEP 1 (10%)]: Validated target disk, live media, base system, kernel and initramfs",
        `+ Target device: ${targetDisk.node} (${targetDisk.model})`,
        "+ Live USB media /dev/sdb excluded and write-protected",
        "[STEP 2 (20%)]: Unmounting target partitions cleanly",
        "=== STATE: INSTALLING ===",
        `[STEP 3 (30%)]: Repartitioning target disk ${targetDisk.node} (GPT, EFI, Data, Base)`,
        "+ sgdisk --zap-all " + targetDisk.node,
        "+ sgdisk -n 1:0:+256M -t 1:ef00 -c 1:MACLITE_BOOT " + targetDisk.node,
        "+ sgdisk -n 2:0:+4G -t 2:8300 -c 2:MACLITE_DATA " + targetDisk.node,
        "+ sgdisk -n 3:0:0 -t 3:8300 -c 3:MACLITE_BASE " + targetDisk.node,
      ]);
      setProgress(30);

      const t1 = setTimeout(() => {
        setProgress(45);
        setCurrentStepText("Formatting fresh filesystems (FAT32 & ext4)...");
        setLogs((prev) => [
          ...prev,
          "[STEP 4 (45%)]: Formatting fresh filesystems",
          "+ mkfs.vfat -F32 -n MACLITE_BOOT /dev/sda1 (UUID=78FA-C9B2)",
          "+ mkfs.ext4 -F -L MACLITE_DATA /dev/sda2 (UUID=4a12b3c4-...)",
          "+ mkfs.ext4 -F -L MACLITE_BASE /dev/sda3 (UUID=e78d910a-...)",
        ]);
      }, 700);

      const t2 = setTimeout(() => {
        setProgress(60);
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
        setProgress(72);
        setCurrentStepText("Installing Apple EFI fallback bootloader & grub.cfg...");
        setLogs((prev) => [
          ...prev,
          "[STEP 6 (72%)]: Installing Apple EFI bootloader and UUID-bound boot configuration",
          "+ grub-mkimage -O x86_64-efi -o /EFI/BOOT/BOOTX64.EFI",
          "+ Generating grub.cfg with root=UUID=e78d910a-3142-4f81-9b16-5fa4e872c019",
          "+ Creating Apple Option Boot .disk_label ('G1OS')",
        ]);
      }, 2300);

      const t4 = setTimeout(() => {
        setProgress(78);
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
        setCurrentStepText(`Verifying ${domain.domain}...`);

        // Check for injected scenario failures
        let willFail = false;
        let failMessage = "";
        let canAutoRepair = false;

        if (scenario === "failed-copy" && domain.id === "files") {
          willFail = true;
          failMessage = "Required system binaries are missing or corrupted (/usr/bin/mica-shell not found).";
        } else if (scenario === "failed-bootloader" && domain.id === "bootloader") {
          willFail = true;
          failMessage = "The boot configuration could not be verified. Fallback BOOTX64.EFI missing or corrupted.";
        } else if (scenario === "missing-driver" && domain.id === "drivers") {
          willFail = true;
          failMessage = "Required GPU driver (radeon/amdgpu) is missing or initialization failed.";
        } else if (scenario === "corrupt-config" && domain.id === "config" && !repaired) {
          willFail = true;
          canAutoRepair = true;
          failMessage = "Invalid configuration syntax: /etc/fstab missing root UUID binding.";
        } else if (scenario === "low-disk-space" && domain.id === "diskspace") {
          willFail = true;
          failMessage = "Critically low disk space: EFI partition has only 4.1 MB remaining (minimum 10 MB required).";
        } else if (scenario === "interrupted-install" && domain.id === "nopartial") {
          willFail = true;
          failMessage = "Incomplete installation marker detected; file copy was interrupted unexpectedly.";
        }

        if (willFail) {
          clearInterval(runCheckInterval);
          setChecks((prev) =>
            prev.map((c, i) => (i === checkIdx ? { ...c, status: "fail", details: failMessage } : c))
          );
          setFailureInfo({
            title: `${domain.domain} verification`,
            message: failMessage,
            canRepair: canAutoRepair,
          });
          setLogs((prev) => [
            ...prev,
            `Verification FAIL: ${domain.domain} — ${failMessage}`,
            "ERROR: Installation halted by No-Break Guarantee. System reboot is disabled.",
          ]);
          setStage("error");
          return;
        }

        // Passed this check
        setChecks((prev) =>
          prev.map((c, i) =>
            i === checkIdx
              ? {
                  ...c,
                  status:
                    domain.id === "network" && scenario === "missing-driver"
                      ? "warn"
                      : "pass",
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
      }, 240);

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
    setStage("installing");
  };

  const handleRepair = () => {
    // Section 21 & 22: Automatic Repair and Re-Verification
    setLogs((prev) => [
      ...prev,
      "=== INITIATING AUTOMATIC SAFE REPAIR ===",
      "+ Regenerating missing /etc/fstab configuration with verified UUIDs...",
      "+ Repairing permissions on system mount points...",
      "+ Re-triggering Final Verification...",
    ]);
    setRepaired(true);
    setStage("verifying");
  };

  const handleRestart = () => {
    // Section 24: Reboot Safety Gate
    if (stage !== "complete" || progress < 100) return;
    os.notify("G1OS", "Restarting", "System restart confirmed. Unplug USB media to boot into internal G1OS.", "restart");
    os.setPowerState("restarting");
    window.setTimeout(() => os.setPowerState("running"), 1600);
  };

  return (
    <div className="flex h-full w-full flex-col bg-[#1c1e26] text-white select-none">
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
              className="bg-transparent font-medium text-blue-300 outline-none cursor-pointer"
            >
              <option value="normal" className="bg-[#232632] text-white">Normal (100% Pass)</option>
              <option value="failed-copy" className="bg-[#232632] text-white">Failed File Copy</option>
              <option value="failed-bootloader" className="bg-[#232632] text-white">Failed Bootloader</option>
              <option value="missing-driver" className="bg-[#232632] text-white">Missing Driver (GPU)</option>
              <option value="corrupt-config" className="bg-[#232632] text-white">Corrupted Config (Auto-Repair)</option>
              <option value="low-disk-space" className="bg-[#232632] text-white">Low Disk Space</option>
              <option value="interrupted-install" className="bg-[#232632] text-white">Interrupted Install</option>
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
                onClick={() => setStage("select")}
                className="inline-flex items-center gap-2 rounded-xl bg-blue-600 px-7 py-3 text-[14px] font-semibold text-white shadow-lg shadow-blue-900/40 hover:bg-blue-500 transition-all cursor-pointer"
              >
                Continue <ChevronRight size={16} />
              </button>
            </div>
          </div>
        )}

        {/* 2. Select Disk Screen */}
        {stage === "select" && (
          <div className="mx-auto max-w-2xl w-full space-y-6 animate-in fade-in duration-300">
            <div>
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
                onClick={() => setStage("welcome")}
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
                Erase & Install G1OS <ChevronRight size={15} />
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
                  Installation Activity & Logs
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
                Integrity & Bootability Domains (16 Checks)
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

        {/* 8. Section 20: Failure Summary Screen with Automatic Repair */}
        {stage === "error" && (
          <div className="mx-auto max-w-lg w-full py-6 text-center space-y-5 animate-in fade-in duration-300">
            <div className="mx-auto grid h-16 w-16 place-items-center rounded-full bg-red-500/20 text-red-400 ring-8 ring-red-500/10">
              <XCircle size={38} />
            </div>

            <div>
              <h2 className="text-[20px] font-bold text-white">Installation Could Not Be Completed</h2>
              <div className="mt-2 inline-flex items-center gap-2 rounded-lg bg-red-500/20 px-3.5 py-1.5 border border-red-500/30 text-red-300 font-semibold text-[13px]">
                <span>✗ {failureInfo?.title || "Verification failed"}</span>
              </div>
              <p className="mt-2 text-[12.5px] leading-relaxed text-red-200/80 max-w-md mx-auto">
                {failureInfo?.message || "A mandatory pre-flight verification check returned an error."}
              </p>
            </div>

            {/* Error Log Box */}
            <div className="rounded-xl border border-red-500/20 bg-black/60 p-3.5 text-left font-mono text-[11px] text-red-300/90 h-32 overflow-y-auto space-y-1">
              {logs.slice(-6).map((l, i) => (
                <div key={i} className="leading-relaxed">{l}</div>
              ))}
            </div>

            {/* Section 20 & 21: Action buttons: [Retry] [Repair] [View Details] */}
            <div className="flex items-center justify-center gap-3 pt-2">
              <button
                type="button"
                onClick={handleRetry}
                className="inline-flex items-center gap-1.5 rounded-xl bg-white/10 px-5 py-2.5 text-[13px] font-semibold text-white hover:bg-white/20 transition-all cursor-pointer"
              >
                <RotateCw size={14} /> Retry
              </button>

              {failureInfo?.canRepair ? (
                <button
                  type="button"
                  onClick={handleRepair}
                  className="inline-flex items-center gap-1.5 rounded-xl bg-emerald-600 px-6 py-2.5 text-[13px] font-semibold text-white shadow-lg shadow-emerald-900/40 hover:bg-emerald-500 transition-all cursor-pointer"
                >
                  <ShieldCheck size={14} /> Automatic Repair & Re-Verify
                </button>
              ) : (
                <button
                  type="button"
                  onClick={() => setShowLogs(!showLogs)}
                  className="inline-flex items-center gap-1.5 rounded-xl border border-white/20 px-5 py-2.5 text-[13px] font-medium text-white/80 hover:bg-white/10 transition-all cursor-pointer"
                >
                  <Terminal size={14} /> {showLogs ? "Hide Details" : "View Details"}
                </button>
              )}
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
