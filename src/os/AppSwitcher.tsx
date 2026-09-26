/**
 * G1OS Command-Tab App Switcher
 *
 * macOS-style overlay: hold ⌘, press Tab to cycle forward, `~` or Shift+Tab
 * to go back, release ⌘ to activate. The panel animates in with a spring
 * and slides out on release. Like macOS it sits centered and large, with
 * running-app tiles, highlight following selection, and app names.
 */
import { useEffect, useRef } from "react";
import { APPS, useOS } from "./os";
import { G1Icon } from "./icons/IconSystem";

export default function AppSwitcher() {
  const os = useOS();
  const apps = os.switcherApps;
  const idx = Math.max(0, Math.min(os.switcherIdx, apps.length - 1));
  const released = useRef(false);

  useEffect(() => {
    if (!os.switcherOpen) return;
    released.current = false;
    const onKey = (e: KeyboardEvent) => {
      const mod = e.metaKey || e.ctrlKey;
      if (e.key === "Tab" && mod) {
        e.preventDefault();
        os.setSwitcherIdx((os.switcherIdx + 1) % apps.length);
      } else if ((e.key === "`" || e.key === "~") && mod) {
        e.preventDefault();
        os.setSwitcherIdx((os.switcherIdx - 1 + apps.length) % apps.length);
      } else if (e.key === "Shift" && mod) {
        // Shift alone doesn't fire a nav; Shift+Tab does.
      } else if (e.key === "Escape") {
        released.current = true;
        os.setSwitcherOpen(false);
      } else if (!mod && !["Shift", "Meta", "Control", "Alt"].includes(e.key)) {
        // Non-modifier key while switcher is open = dismiss
        released.current = true;
        os.setSwitcherOpen(false);
      }
    };
    const onKeyUp = (e: KeyboardEvent) => {
      if (e.key === "Meta" || e.key === "Control" || e.key === "Super") {
        // Release ⌘  → activate selected app
        released.current = true;
        const app = apps[idx];
        os.setSwitcherOpen(false);
        if (app) {
          // Bring that app to front: find its visible window, restore if minimized.
          const existing = os.wins.find((w) => w.app === app && w.animState !== "closing");
          if (existing) {
            if (existing.min) os.setMin(existing.id, false);
            os.focusWin(existing.id);
          } else {
            os.openApp(app);
          }
        }
      }
    };
    window.addEventListener("keydown", onKey);
    window.addEventListener("keyup", onKeyUp);
    return () => {
      window.removeEventListener("keydown", onKey);
      window.removeEventListener("keyup", onKeyUp);
    };
  }, [os.switcherOpen, idx, apps, os]);

  if (!os.switcherOpen) return null;

  return (
    <div
      className="absolute inset-0 z-[620] flex items-center justify-center"
      style={{ pointerEvents: "none", animation: "fade-in 0.12s ease-out both" }}
    >
      <div
        className="flex items-end gap-3 rounded-2xl px-5 py-4"
        style={{
          background: "rgba(30, 30, 36, 0.78)",
          backdropFilter: "blur(40px) saturate(1.8)",
          WebkitBackdropFilter: "blur(40px) saturate(1.8)",
          boxShadow: "0 22px 60px rgba(0,0,0,0.5), 0 0 0 0.5px rgba(255,255,255,0.1)",
          animation: "switcher-in 0.22s cubic-bezier(0.175,0.885,0.32,1.075) both",
          pointerEvents: "auto",
        }}
      >
        {apps.map((app, i) => {
          const def = APPS[app];
          if (!def) return null;
          const selected = i === idx;
          return (
            <div key={app} className="flex flex-col items-center gap-2" style={{ width: 76 }}>
              <div
                className="relative flex items-center justify-center rounded-xl transition-all duration-150"
                style={{
                  width: 64, height: 64,
                  background: selected ? "rgba(255,255,255,0.18)" : "transparent",
                  transform: selected ? "scale(1.08)" : "scale(1)",
                }}
              >
                <G1Icon name={def.icon} size={52} />
              </div>
              <span
                className="max-w-[72px] truncate text-[11px] font-medium text-white/90 text-center"
                style={{ opacity: selected ? 1 : 0.75 }}
              >
                {def.name}
              </span>
            </div>
          );
        })}
      </div>
    </div>
  );
}
