import { useState } from "react";
import {
  ArrowLeft,
  ArrowRight,
  RotateCw,
  Share,
  Plus,
  Lock,
  Search,
  Bookmark,
  Tv,
  Film,
  Music,
  Globe,
  ShieldCheck,
  Zap,
  Maximize2,
  ExternalLink,
} from "lucide-react";
import { VIDEO_SRC } from "../os";

interface BookmarkItem {
  name: string;
  url: string;
  icon: string;
  color: string;
  category: "streaming" | "tools" | "social";
}

const BOOKMARKS: BookmarkItem[] = [
  { name: "YouTube", url: "https://youtube.com", icon: "▶", color: "from-red-500 to-red-700", category: "streaming" },
  { name: "Netflix", url: "https://netflix.com", icon: "N", color: "from-red-600 to-black", category: "streaming" },
  { name: "Apple TV+", url: "https://tv.apple.com", icon: "tv", color: "from-zinc-700 to-black", category: "streaming" },
  { name: "Spotify Web", url: "https://open.spotify.com", icon: "♫", color: "from-emerald-500 to-emerald-700", category: "streaming" },
  { name: "Wikipedia", url: "https://wikipedia.org", icon: "W", color: "from-zinc-400 to-zinc-600", category: "tools" },
  { name: "GitHub", url: "https://github.com", icon: "git", color: "from-zinc-800 to-zinc-950", category: "tools" },
];

export default function BrowserApp() {
  const [url, setUrl] = useState("https://youtube.com");
  const [inputUrl, setInputUrl] = useState("https://youtube.com");
  const [activeTab, setActiveTab] = useState(0);
  const [tabs, setTabs] = useState([
    { id: 1, title: "YouTube — Watch Live", url: "https://youtube.com" },
    { id: 2, title: "G1OS Documentation", url: "https://g1os.internal/docs" },
  ]);
  const [isPlaying, setIsPlaying] = useState(true);

  const handleNavigate = (newUrl: string) => {
    setUrl(newUrl);
    setInputUrl(newUrl);
    setTabs((prev) =>
      prev.map((t, idx) => (idx === activeTab ? { ...t, url: newUrl, title: newUrl.replace("https://", "").split("/")[0] } : t))
    );
  };

  const handleNewTab = () => {
    const newTabId = tabs.length + 1;
    const newTab = { id: newTabId, title: "New Tab", url: "https://start.g1os.internal" };
    setTabs([...tabs, newTab]);
    setActiveTab(tabs.length);
    setUrl(newTab.url);
    setInputUrl(newTab.url);
  };

  const isStreamingSite = url.includes("youtube.com") || url.includes("netflix.com") || url.includes("tv.apple.com");

  return (
    <div className="flex h-full w-full flex-col bg-[#1e2029] text-white select-none overflow-hidden font-sans">
      {/* 1. Safari Tab Strip */}
      <div className="flex h-9 flex-none items-center bg-[#252834] px-2 pt-1 border-b border-white/10 gap-1 overflow-x-auto">
        {tabs.map((tab, idx) => {
          const isActive = idx === activeTab;
          return (
            <div
              key={tab.id}
              onClick={() => {
                setActiveTab(idx);
                setUrl(tab.url);
                setInputUrl(tab.url);
              }}
              className={`group relative flex h-7 max-w-[200px] flex-1 items-center justify-between rounded-t-lg px-3 text-[12px] transition-all cursor-pointer ${
                isActive
                  ? "bg-[#1c1e26] text-white font-medium shadow-sm"
                  : "bg-white/[0.04] text-white/60 hover:bg-white/[0.08] hover:text-white/80"
              }`}
            >
              <div className="flex items-center gap-1.5 truncate">
                <Globe size={11} className={isActive ? "text-blue-400" : "text-white/40"} />
                <span className="truncate">{tab.title}</span>
              </div>
              {tabs.length > 1 && (
                <button
                  onClick={(e) => {
                    e.stopPropagation();
                    const filtered = tabs.filter((_, i) => i !== idx);
                    setTabs(filtered);
                    setActiveTab(Math.max(0, idx - 1));
                    setUrl(filtered[Math.max(0, idx - 1)].url);
                    setInputUrl(filtered[Math.max(0, idx - 1)].url);
                  }}
                  className="ml-1 rounded p-0.5 opacity-0 group-hover:opacity-100 hover:bg-white/10 text-white/50 hover:text-white"
                >
                  ×
                </button>
              )}
            </div>
          );
        })}
        <button
          onClick={handleNewTab}
          className="grid h-6 w-6 place-items-center rounded-md hover:bg-white/10 text-white/60 hover:text-white transition-colors"
          title="New Tab"
        >
          <Plus size={14} />
        </button>
      </div>

      {/* 2. Navigation Bar */}
      <div className="flex h-11 flex-none items-center justify-between bg-[#1c1e26] px-3 gap-2 border-b border-white/10">
        <div className="flex items-center gap-1 text-white/70">
          <button
            onClick={() => handleNavigate("https://start.g1os.internal")}
            className="rounded p-1.5 hover:bg-white/10 hover:text-white transition-colors cursor-pointer"
            title="Back"
          >
            <ArrowLeft size={14} />
          </button>
          <button
            className="rounded p-1.5 opacity-40 cursor-not-allowed"
            title="Forward"
          >
            <ArrowRight size={14} />
          </button>
          <button
            onClick={() => handleNavigate(url)}
            className="rounded p-1.5 hover:bg-white/10 hover:text-white transition-colors cursor-pointer"
            title="Reload"
          >
            <RotateCw size={14} />
          </button>
        </div>

        {/* Smart Search Bar */}
        <div className="flex flex-1 max-w-xl items-center rounded-lg bg-white/[0.08] px-3 py-1 border border-white/10 focus-within:border-blue-500 focus-within:bg-black/40 transition-all">
          <Lock size={12} className="text-emerald-400 mr-2 flex-none" />
          <form
            onSubmit={(e) => {
              e.preventDefault();
              let formatted = inputUrl.trim();
              if (!formatted.startsWith("http://") && !formatted.startsWith("https://")) {
                formatted = "https://" + formatted;
              }
              handleNavigate(formatted);
            }}
            className="w-full flex items-center"
          >
            <input
              value={inputUrl}
              onChange={(e) => setInputUrl(e.target.value)}
              className="w-full bg-transparent text-[12px] font-mono text-white/90 outline-none"
              placeholder="Search or enter website name"
            />
          </form>
          <ShieldCheck size={13} className="text-white/40 ml-2" title="Hardware Protected Session" />
        </div>

        {/* Right Tools */}
        <div className="flex items-center gap-2">
          <span className="flex items-center gap-1 rounded bg-blue-500/20 px-2 py-0.5 text-[10.5px] font-medium text-blue-300 border border-blue-500/30">
            <Zap size={11} /> 1080p VDPAU
          </span>
          <button className="rounded p-1.5 hover:bg-white/10 text-white/70 hover:text-white transition-colors">
            <Share size={14} />
          </button>
        </div>
      </div>

      {/* 3. Bookmarks Favorites Bar */}
      <div className="flex h-7 flex-none items-center bg-[#21232e] px-4 gap-4 border-b border-white/5 overflow-x-auto text-[11px] text-white/70">
        <span className="font-semibold text-white/40 uppercase tracking-wider text-[10px] flex items-center gap-1">
          <Bookmark size={10} /> Favorites:
        </span>
        {BOOKMARKS.map((bm) => (
          <button
            key={bm.name}
            onClick={() => handleNavigate(bm.url)}
            className="flex items-center gap-1.5 hover:text-white transition-colors cursor-pointer truncate"
          >
            <span className="grid h-3.5 w-3.5 place-items-center rounded-sm bg-white/10 text-[9px] font-bold">
              {bm.icon}
            </span>
            <span>{bm.name}</span>
          </button>
        ))}
      </div>

      {/* 4. Web View Content */}
      <div className="flex-1 overflow-y-auto bg-[#14151b] p-6 flex flex-col justify-start items-center">
        {url.includes("start.g1os.internal") ? (
          <div className="w-full max-w-2xl py-8 space-y-8 animate-in fade-in duration-300">
            <div className="text-center space-y-2">
              <div className="inline-block p-3 rounded-2xl bg-gradient-to-tr from-sky-400 to-blue-600 shadow-xl shadow-blue-900/30 text-white mb-2">
                <Globe size={36} />
              </div>
              <h2 className="text-[24px] font-bold text-white">G1OS Safari</h2>
              <p className="text-[13px] text-white/60">Lightweight streaming browser tuned for iMac Mid-2010</p>
            </div>

            {/* Favorites Grid */}
            <div className="grid grid-cols-3 gap-3">
              {BOOKMARKS.map((bm) => (
                <div
                  key={bm.name}
                  onClick={() => handleNavigate(bm.url)}
                  className="flex items-center gap-3 p-3 rounded-xl border border-white/10 bg-white/[0.03] hover:bg-white/[0.08] hover:border-white/20 transition-all cursor-pointer group"
                >
                  <div
                    className={`grid h-10 w-10 place-items-center rounded-lg bg-gradient-to-br ${bm.color} text-white font-bold text-[14px] shadow-md group-hover:scale-105 transition-transform`}
                  >
                    {bm.icon}
                  </div>
                  <div>
                    <h4 className="text-[13px] font-medium text-white/90">{bm.name}</h4>
                    <p className="text-[11px] text-white/40 truncate">{bm.url.replace("https://", "")}</p>
                  </div>
                </div>
              ))}
            </div>

            {/* Hardware Accelerator Card */}
            <div className="rounded-xl border border-emerald-500/20 bg-emerald-950/20 p-4 flex items-center justify-between">
              <div className="flex items-center gap-3">
                <div className="grid h-9 w-9 place-items-center rounded-lg bg-emerald-500/20 text-emerald-400">
                  <Zap size={20} />
                </div>
                <div>
                  <h4 className="text-[13px] font-semibold text-emerald-200">Hardware Video Decode Active</h4>
                  <p className="text-[11.5px] text-emerald-300/70">
                    Radeon HD 4670 VDPAU ASIC acceleration handles H.264/AVC at zero CPU overhead.
                  </p>
                </div>
              </div>
              <span className="font-mono text-[11px] text-emerald-400 bg-emerald-500/15 px-2.5 py-1 rounded-full border border-emerald-500/30">
                1080p 60fps
              </span>
            </div>
          </div>
        ) : isStreamingSite ? (
          /* Streaming Player Container */
          <div className="w-full max-w-4xl space-y-4 animate-in fade-in duration-300">
            {/* Player Chrome */}
            <div className="relative aspect-video w-full overflow-hidden rounded-2xl border border-white/15 bg-black shadow-2xl">
              <video
                src={VIDEO_SRC}
                autoPlay
                loop
                muted
                playsInline
                className="h-full w-full object-cover"
              />

              {/* Live Overlay HUD */}
              <div className="absolute top-4 left-4 flex items-center gap-2">
                <span className="flex items-center gap-1.5 rounded-full bg-black/60 backdrop-blur-md px-3 py-1 text-[11.5px] font-medium text-white border border-white/20">
                  <span className="h-2 w-2 rounded-full bg-red-500 animate-pulse" /> Live Stream · 1080p HD
                </span>
                <span className="flex items-center gap-1 rounded-full bg-emerald-500/80 backdrop-blur-md px-2.5 py-1 text-[11px] font-semibold text-black">
                  <Zap size={12} /> VDPAU Direct Render
                </span>
              </div>

              {/* Title Overlay */}
              <div className="absolute bottom-0 inset-x-0 bg-gradient-to-t from-black/90 via-black/40 to-transparent p-5">
                <h3 className="text-[17px] font-bold text-white">4K Ocean Drift & Reef Life — 1080p Native Mode</h3>
                <p className="text-[12px] text-white/70 mt-0.5">
                  Streamed smoothly via G1OS Media Pipeline · 0.4% CPU · 48 MB Browser RSS
                </p>
              </div>
            </div>

            {/* Stream Info Bar */}
            <div className="flex items-center justify-between rounded-xl border border-white/10 bg-white/[0.03] p-4 text-[12px]">
              <div className="flex items-center gap-3">
                <div className="grid h-8 w-8 place-items-center rounded-lg bg-red-600 text-white font-bold">
                  ▶
                </div>
                <div>
                  <div className="font-semibold text-white/95">YouTube Streaming Player</div>
                  <div className="text-white/50 text-[11px]">Hardware accelerated for RV730 / Clarkdale CPU</div>
                </div>
              </div>

              <div className="flex items-center gap-2">
                <button
                  onClick={() => handleNavigate("https://start.g1os.internal")}
                  className="rounded-lg bg-white/10 px-3 py-1.5 text-white/80 hover:bg-white/20 hover:text-white transition-colors cursor-pointer"
                >
                  Return to Start
                </button>
              </div>
            </div>
          </div>
        ) : (
          /* General Web Page Container */
          <div className="w-full max-w-3xl space-y-6 animate-in fade-in duration-300 py-6">
            <div className="rounded-xl border border-white/10 bg-white/[0.04] p-6 space-y-4">
              <div className="flex items-center justify-between border-b border-white/10 pb-3">
                <h2 className="text-[18px] font-bold text-white">G1OS Operating System Documentation</h2>
                <span className="font-mono text-[11px] text-blue-400">Offline Manual</span>
              </div>

              <div className="text-[13px] leading-relaxed text-white/75 space-y-3">
                <p>
                  <strong>G1OS</strong> is engineered specifically for the <strong>iMac Mid-2010</strong> (iMac11,2 & iMac11,3).
                  Unlike standard Linux desktop environments that overwhelm vintage 2010 hardware with heavy background
                  daemons, indexing services, and unaccelerated composite trees, G1OS runs on a bespoke C11 graphics pipeline.
                </p>

                <h3 className="text-[14px] font-semibold text-white pt-2">Key Hardware Specifications:</h3>
                <ul className="list-disc pl-5 space-y-1 text-white/60 text-[12.5px]">
                  <li><strong>Target GPU</strong>: ATI Radeon HD 4670 (256MB) / HD 5670 (512MB) via Mesa Gallium r600.</li>
                  <li><strong>Native Display</strong>: 21.5" and 27" LED 1920×1080 modeset via Linux DRM/KMS.</li>
                  <li><strong>Apple SMC Fan Management</strong>: Automated quiet profile (ODD: 1000 RPM, HDD: 1100 RPM, CPU: 1200 RPM).</li>
                  <li><strong>Apple EFI 1.1 Support</strong>: Self-contained fallback loader at <code>/EFI/BOOT/BOOTX64.EFI</code> with UUID boot binding.</li>
                </ul>
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
