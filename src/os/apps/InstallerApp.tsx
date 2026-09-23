import { useState, useEffect } from "react";
import {
  HardDrive,
  Usb,
  AlertTriangle,
  CheckCircle2,
  XCircle,
  RotateCw,
  ShieldCheck,
  ChevronRight,
  ChevronDown,
  Terminal,
  Lock,
  ArrowLeft,
  Sparkles,
  Check,
  Info,
} from "lucide-react";
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
      "sda1: 209.7 MB (FAT32 · EFI System Partition)",
      "sda2: 499.8 GB (APFS · macOS High Sierra / Personal Data)",
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
    partitions: ["sdb1: 612 MB (ISO9660 · MacLiteOS Live Root)"],
  },
];

interface StepInfo {
  label: string;
  stageName: string;
}

const INSTALL_STEPS: StepInfo[] = [
  { label: "Preparing MacLiteOS installation environment...", stageName: "Preparing" },
  { label: "Detecting disk geometry & scanning storage controller...", stageName: "Detecting Disk" },
  { label: "Zapping partition table & creating GPT partitions...", stageName: "Partitioning" },
  { label: "Formatting MACLITE_BOOT (FAT32) & MACLITE_DATA (ext4)...", stageName: "Formatting" },
  { label: "Copying MacLiteOS immutable base system & configuring fstab...", stageName: "Installing" },
  { label: "Configuring Apple EFI bootloader & UUID binding...", stageName: "Configuring Boot" },
  { label: "Performing offline pre-flight verification pass...", stageName: "Finalizing" },
];

export default function InstallerApp() {
  const os = useOS();
  const [stage, setStage] = useState<"welcome" | "select" | "confirm" | "installing" | "complete" | "error">("welcome");
  const [selectedDiskId, setSelectedDiskId] = useState("sda");
  const [confirmed, setConfirmed] = useState(false);
  const [progress, setProgress] = useState(0);
  const [currentStepIdx, setCurrentStepIdx] = useState(0);
  const [logs, setLogs] = useState<string[]>([]);
  const [showLogs, setShowLogs] = useState(false);

  const targetDisk = DETECTED_DISKS.find((d) => d.id === selectedDiskId) || DETECTED_DISKS[0];

  useEffect(() => {
    if (stage !== "installing") return;

    let step = 0;
    setProgress(10);
    setLogs([
      "[STEP 1 (10%)]: Preparing MacLiteOS installation environment...",
      "+ Probing block devices on PCI SATA controller...",
      "+ Target locked: /dev/sda (Crucial CT500MX500SSD1, 500.1 GB)",
      "+ Live USB boot media /dev/sdb excluded and write-protected",
    ]);

    const interval = setInterval(() => {
      step++;
      if (step < INSTALL_STEPS.length) {
        setCurrentStepIdx(step);
        const pct = Math.round(((step + 1) / INSTALL_STEPS.length) * 100);
        setProgress(pct);
        setLogs((prev) => [
          ...prev,
          `[STEP ${step + 1} (${pct}%)]: ${INSTALL_STEPS[step].label}`,
          step === 2
            ? "+ sgdisk --zap-all /dev/sda && sgdisk -n 1:0:+256M -t 1:ef00 -c 1:MACLITE_BOOT /dev/sda"
            : step === 3
            ? "+ mkfs.vfat -F32 /dev/sda1 && mkfs.ext4 -F /dev/sda2"
            : step === 4
            ? "+ Synchronizing rootfs base to /dev/sda3 & generating UUID-bound /etc/fstab"
            : step === 5
            ? "+ Installing /EFI/BOOT/BOOTX64.EFI fallback and binding grub.cfg to UUID 78FA-C9B2"
            : "+ Pre-flight offline verification: BOOTX64.EFI, grub.cfg, vmlinuz, and .disk_label validated",
        ]);
      } else {
        clearInterval(interval);
        setProgress(100);
        setLogs((prev) => [
          ...prev,
          "=================================================================",
          "✓ Pre-flight Verification Passed: Internal EFI partition exists",
          "✓ Pre-flight Verification Passed: Fallback BOOTX64.EFI verified",
          "✓ Pre-flight Verification Passed: grub.cfg verified without USB references",
          "✓ Pre-flight Verification Passed: Kernel & initramfs present",
          "✓ Pre-flight Verification Passed: Apple .disk_label ('MacLiteOS') created",
          "SUCCESS: MacLiteOS has been fully installed to /dev/sda.",
          "=================================================================",
        ]);
        setTimeout(() => setStage("complete"), 800);
      }
    }, 950);

    return () => clearInterval(interval);
  }, [stage]);

  const handleStartInstall = () => {
    if (!confirmed || targetDisk.isUsbBoot) return;
    setStage("installing");
  };

  const handleRestart = () => {
    os.notify("MacLiteOS", "System Restarting", "Rebooting into internal drive...");
    setTimeout(() => {
      window.location.reload();
    }, 800);
  };

  return (
    <div className="flex h-full w-full flex-col bg-[#1c1e26] text-white select-none">
      {/* Header */}
      <div className="flex h-14 flex-none items-center justify-between border-b border-white/10 bg-[#232632] px-6">
        <div className="flex items-center gap-3">
          <G1Icon name="installer" size={30} />
          <div>
            <h1 className="text-[14px] font-semibold tracking-wide">G1OS Installer</h1>
            <p className="text-[11px] text-white/50">iMac Mid-2010 Setup Wizard · EFI 1.1 Compliant</p>
          </div>
        </div>

        <div className="flex items-center gap-2">
          <span className="flex items-center gap-1.5 rounded-full bg-emerald-500/15 px-2.5 py-1 text-[11px] font-medium text-emerald-300 border border-emerald-500/20">
            <ShieldCheck size={13} /> Live USB Protected
          </span>
        </div>
      </div>

      {/* Main Body */}
      <div className="flex-1 overflow-y-auto p-6 flex flex-col justify-center">
        {/* 1. Welcome Screen */}
        {stage === "welcome" && (
          <div className="mx-auto max-w-lg text-center space-y-6 animate-in fade-in duration-300">
            <div className="mx-auto flex items-center justify-center drop-shadow-2xl">
              <G1Icon name="installer" size={76} />
            </div>

            <div>
              <h2 className="text-[26px] font-bold tracking-tight text-white">Welcome to G1OS</h2>
              <p className="mt-1.5 text-[15px] font-medium text-blue-400">“Giving life to older machines.”</p>
              <p className="mt-3 text-[13px] leading-relaxed text-white/60">
                G1OS is a modern, lightweight operating system engineered specifically for vintage Macs. It delivers
                an ultra-responsive classic macOS experience, dedicated hardware acceleration, and seamless internal disk installation.
              </p>
            </div>

            {/* Feature Cards */}
            <div className="grid grid-cols-2 gap-3 text-left">
              <div className="rounded-xl border border-white/10 bg-white/[0.03] p-3.5">
                <div className="flex items-center gap-2 text-[13px] font-medium text-white/90">
                  <Check size={16} className="text-emerald-400" />
                  <span>Apple EFI 1.1 Fallback</span>
                </div>
                <p className="mt-1 text-[11.5px] text-white/50">
                  Reliably boots from internal disk even if PRAM/NVRAM entries are reset.
                </p>
              </div>

              <div className="rounded-xl border border-white/10 bg-white/[0.03] p-3.5">
                <div className="flex items-center gap-2 text-[13px] font-medium text-white/90">
                  <Check size={16} className="text-emerald-400" />
                  <span>UUID-Bound Boot</span>
                </div>
                <p className="mt-1 text-[11.5px] text-white/50">
                  Guarantees clean startup without conflicting with live USB drives.
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
              <h2 className="text-[20px] font-bold text-white">Select Installation Destination</h2>
              <p className="mt-1 text-[13px] text-white/60">
                MacLiteOS automatically detects your internal storage and excludes the booted USB drive from being targeted.
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
                          <Lock size={12} /> Live USB (Locked)
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
                Existing Partitions on {targetDisk.node} ({targetDisk.model}):
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
                Install MacLiteOS on:
              </div>
              <div className="flex items-start gap-4">
                <div className="flex-none drop-shadow">
                  <G1Icon name="macintosh-hd" size={44} />
                </div>
                <div className="space-y-1">
                  <h3 className="text-[16px] font-bold text-white">{targetDisk.model}</h3>
                  <div className="text-[13px] text-white/70">
                    Type: <span className="text-white font-medium">Internal SSD/HDD</span> · Device:{" "}
                    <span className="font-mono text-white font-medium">{targetDisk.node}</span>
                  </div>
                  <div className="text-[13px] text-white/70">
                    Capacity: <span className="text-white font-medium">{targetDisk.size}</span>
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
                  Existing partitions and operating systems on this drive will be deleted and replaced with a clean
                  MacLiteOS installation. Ensure any personal files on this drive have been backed up.
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
                Erase & Install MacLiteOS <ChevronRight size={15} />
              </button>
            </div>
          </div>
        )}

        {/* 4. Installing Screen */}
        {stage === "installing" && (
          <div className="mx-auto max-w-xl w-full py-6 space-y-6 animate-in fade-in duration-300">
            <div className="text-center space-y-2">
              <div className="inline-block animate-spin text-blue-400">
                <RotateCw size={36} />
              </div>
              <h2 className="text-[22px] font-bold text-white">Installing MacLiteOS...</h2>
              <p className="text-[13px] text-blue-300 font-medium">
                {INSTALL_STEPS[currentStepIdx]?.label || "Configuring system..."}
              </p>
            </div>

            {/* Progress Bar & Stage Ribbon */}
            <div className="space-y-3">
              <div className="flex justify-between text-[12px] font-medium text-white/70">
                <span className="text-blue-400 font-semibold uppercase tracking-wider text-[11px]">
                  Stage: {INSTALL_STEPS[currentStepIdx]?.stageName}
                </span>
                <span className="font-mono text-white/90">{progress}%</span>
              </div>

              <div className="h-2.5 w-full overflow-hidden rounded-full bg-white/10 p-0.5">
                <div
                  className="h-full rounded-full bg-gradient-to-r from-blue-500 via-sky-400 to-indigo-500 transition-all duration-500 shadow-sm"
                  style={{ width: `${progress}%` }}
                />
              </div>

              {/* Progress Milestones Breadcrumbs */}
              <div className="flex justify-between text-[10px] text-white/40 pt-1">
                <span>Preparing</span>
                <span>Detecting</span>
                <span>Partitioning</span>
                <span>Installing</span>
                <span>Configuring</span>
                <span>Verifying</span>
              </div>
            </div>

            {/* Collapsible Installation Details Section */}
            <div className="rounded-xl border border-white/10 bg-white/[0.03] overflow-hidden">
              <button
                type="button"
                onClick={() => setShowLogs(!showLogs)}
                className="w-full flex items-center justify-between px-4 py-3 text-[12px] font-medium text-white/75 hover:bg-white/[0.04] transition-colors cursor-pointer"
              >
                <span className="flex items-center gap-2">
                  <Terminal size={14} className="text-blue-400" />
                  Installation Details
                </span>
                <span className="flex items-center gap-1 text-[11px] text-blue-400">
                  {showLogs ? "Hide details" : "Show live log"}
                  {showLogs ? <ChevronDown size={14} /> : <ChevronRight size={14} />}
                </span>
              </button>

              {showLogs && (
                <div className="h-44 overflow-y-auto border-t border-white/10 bg-black/60 p-3.5 font-mono text-[11px] text-emerald-400/90 space-y-1">
                  {logs.map((l, i) => (
                    <div key={i} className="leading-relaxed">
                      {l}
                    </div>
                  ))}
                </div>
              )}
            </div>
          </div>
        )}

        {/* 5. Complete Screen */}
        {stage === "complete" && (
          <div className="mx-auto max-w-lg w-full py-4 text-center space-y-5 animate-in fade-in duration-300">
            <div className="mx-auto grid h-16 w-16 place-items-center rounded-full bg-emerald-500/20 text-emerald-400 ring-8 ring-emerald-500/10">
              <CheckCircle2 size={38} />
            </div>

            <div>
              <h2 className="text-[22px] font-bold text-white">G1OS Installation Complete</h2>
              <p className="mt-1.5 text-[14px] font-medium text-emerald-300">
                Remove the USB drive and restart your iMac.
              </p>
              <p className="mt-2 text-[12.5px] leading-relaxed text-white/60">
                The operating system has been successfully verified on <span className="font-mono text-white font-medium">{targetDisk.node}</span>.
                Unplug the USB installer drive so the iMac boots directly into internal G1OS.
              </p>
            </div>

            {/* Offline Pre-Flight Verification Checklist */}
            <div className="rounded-xl border border-white/10 bg-white/[0.03] p-4 text-left space-y-2 text-[12px]">
              <div className="font-semibold text-white/90 pb-1 border-b border-white/5 flex items-center gap-2">
                <ShieldCheck size={15} className="text-emerald-400" />
                Offline Pre-Flight Verification Passed:
              </div>
              <div className="space-y-1 text-white/70">
                <div className="flex items-center gap-2">
                  <Check size={13} className="text-emerald-400" />
                  <span>Internal EFI partition & Fallback <span className="font-mono text-white/90">BOOTX64.EFI</span></span>
                </div>
                <div className="flex items-center gap-2">
                  <Check size={13} className="text-emerald-400" />
                  <span>Filesystem UUID binding (No USB dependencies)</span>
                </div>
                <div className="flex items-center gap-2">
                  <Check size={13} className="text-emerald-400" />
                  <span>Immutable base system & Persistent /var/data storage</span>
                </div>
                <div className="flex items-center gap-2">
                  <Check size={13} className="text-emerald-400" />
                  <span>Apple Option Boot Picker (.disk_label)</span>
                </div>
              </div>
            </div>

            {/* Large Prominent Restart Button */}
            <div className="pt-2">
              <button
                type="button"
                onClick={handleRestart}
                className="inline-flex items-center gap-2.5 rounded-xl bg-blue-600 px-8 py-3 text-[14px] font-semibold text-white shadow-xl shadow-blue-900/50 hover:bg-blue-500 transition-all cursor-pointer"
              >
                <RotateCw size={17} /> Restart iMac Now
              </button>
            </div>
          </div>
        )}

        {/* 6. Error Screen */}
        {stage === "error" && (
          <div className="mx-auto max-w-lg w-full py-8 text-center space-y-5 animate-in fade-in duration-300">
            <div className="mx-auto grid h-16 w-16 place-items-center rounded-full bg-red-500/20 text-red-400 ring-8 ring-red-500/10">
              <XCircle size={38} />
            </div>

            <div>
              <h2 className="text-[20px] font-bold text-white">Installation Failed</h2>
              <p className="mt-1 text-[13px] text-red-300/80">
                An error occurred during disk partitioning or EFI bootloader setup.
              </p>
            </div>

            <div className="flex justify-center gap-3 pt-3">
              <button
                type="button"
                onClick={() => setStage("select")}
                className="rounded-xl bg-white/10 px-6 py-2.5 text-[13px] font-medium text-white hover:bg-white/20 cursor-pointer"
              >
                Try Again
              </button>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
