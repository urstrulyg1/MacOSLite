#include "ml/icon.h"

/* MacLiteOS "Mica" icon set — original geometric artwork (spec §38).
 *
 * Em grid 0..100, y grows down. Template icons use an 8-unit monoline stroke so
 * they read at 14 px in the menu bar and at 22 px in dialogs. App icons are a
 * rounded gradient tile with a white glyph on top.
 *
 * Designated initializers on purpose: missing fields default to zero/NULL, so a
 * new icon cannot silently shift into the wrong column. */
const ml_icon_def ml_icon_table[] = {
    /* ------------------------- status / template ------------------------- */
    { .name = "wifi", .sw = 8, .pad = 4,
      .stroke = "M12,42 Q50,8 88,42 M24,58 Q50,32 76,58 M36,73 Q50,60 64,73 M50,85 L50,86" },
    { .name = "wifi-off", .sw = 8, .pad = 4,
      .stroke = "M12,42 Q50,8 88,42 M24,58 Q50,32 76,58 M36,73 Q50,60 64,73 M50,85 L50,86 M16,16 L86,88" },
    { .name = "volume-high", .sw = 8, .pad = 4,
      .fill = "M18,38 L32,38 L50,20 L50,80 L32,62 L18,62 Z",
      .stroke = "M64,36 Q74,50 64,64 M74,25 Q90,50 74,75" },
    { .name = "volume-low", .sw = 8, .pad = 4,
      .fill = "M18,38 L32,38 L50,20 L50,80 L32,62 L18,62 Z",
      .stroke = "M64,36 Q74,50 64,64" },
    { .name = "volume-mute", .sw = 8, .pad = 4,
      .fill = "M18,38 L32,38 L50,20 L50,80 L32,62 L18,62 Z",
      .stroke = "M64,38 L86,62 M86,38 L64,62" },
    { .name = "battery", .sw = 8, .pad = 4,
      .stroke = "R 10,32 66,36 9 M82,44 L90,44 L90,56 L82,56" },
    { .name = "battery-charge", .sw = 7, .pad = 4,
      .fill = "M46,26 L30,54 L44,54 L38,76 L60,46 L45,46 L54,26 Z",
      .stroke = "R 10,32 66,36 9 M82,44 L90,44 L90,56 L82,56" },
    { .name = "brightness", .sw = 7, .pad = 2,
      .stroke = "M50,28 Q72,28 72,50 Q72,72 50,72 Q28,72 28,50 Q28,28 50,28 Z "
                "M50,6 L50,16 M50,84 L50,94 M6,50 L16,50 M84,50 L94,50 "
                "M19,19 L26,26 M74,74 L81,81 M81,19 L74,26 M26,74 L19,81" },
    { .name = "display", .sw = 7, .pad = 4,
      .stroke = "R 10,18 80,52 7 M42,78 L58,78 M50,70 L50,78 M30,88 L70,88" },
    { .name = "power", .sw = 8, .pad = 4,
      .stroke = "M50,12 L50,46 M31,25 Q12,39 14,62 Q16,86 50,88 Q84,86 86,62 Q88,39 69,25" },
    { .name = "chevron-down", .sw = 9, .pad = 8, .stroke = "M26,38 L50,62 L74,38" },
    { .name = "chevron-up", .sw = 9, .pad = 8, .stroke = "M26,62 L50,38 L74,62" },
    { .name = "chevron-left", .sw = 9, .pad = 8, .stroke = "M62,24 L38,50 L62,76" },
    { .name = "chevron-right", .sw = 9, .pad = 8, .stroke = "M38,24 L62,50 L38,76" },
    { .name = "check", .sw = 9, .pad = 6, .stroke = "M18,54 L40,76 L84,26" },
    { .name = "search", .sw = 8, .pad = 4,
      .stroke = "M42,18 Q66,18 66,42 Q66,66 42,66 Q18,66 18,42 Q18,18 42,18 Z M60,60 L84,84" },
    { .name = "close", .sw = 9, .pad = 8, .stroke = "M26,26 L74,74 M74,26 L26,74" },
    { .name = "minimize", .sw = 9, .pad = 10, .stroke = "M22,52 L78,52" },
    { .name = "maximize", .sw = 8, .pad = 8, .stroke = "M26,26 L74,26 L74,74 L26,74 Z" },
    { .name = "fullscreen", .sw = 8, .pad = 6,
      .stroke = "M18,38 L18,18 L38,18 M62,18 L82,18 L82,38 M82,62 L82,82 L62,82 M38,82 L18,82 L18,62" },
    { .name = "folder", .pad = 4, .fill = "M8,28 L36,28 L45,38 L92,38 L92,82 L8,82 Z" },
    { .name = "file", .sw = 7, .pad = 4, .stroke = "M22,8 L60,8 L82,30 L82,92 L22,92 Z M60,8 L60,30 L82,30" },
    { .name = "doc-lines", .sw = 6, .pad = 2,
      .stroke = "M22,8 L60,8 L82,30 L82,92 L22,92 Z M60,8 L60,30 L82,30 M34,48 L70,48 M34,62 L70,62 M34,76 L58,76" },
    { .name = "drive", .sw = 7, .pad = 6, .stroke = "R 8,30 84,40 8 M78,50 L79,50 M18,50 L30,50" },
    { .name = "usb", .sw = 7, .pad = 6,
      .stroke = "M50,88 L50,14 M50,14 L42,26 M50,14 L58,26 M50,46 L70,36 M50,62 L30,52" },
    { .name = "network", .sw = 6, .pad = 4,
      .stroke = "M50,10 Q86,10 86,50 Q86,90 50,90 Q14,90 14,50 Q14,10 50,10 Z M14,50 L86,50 "
                "M50,10 Q28,50 50,90 M50,10 Q72,50 50,90" },
    { .name = "trash", .sw = 7, .pad = 4,
      .stroke = "M28,28 L72,28 L67,90 L33,90 Z M20,28 L80,28 M40,14 L60,14 M43,40 L43,78 M57,40 L57,78" },
    { .name = "lock", .sw = 7, .pad = 4,
      .stroke = "R 22,44 56,44 9 M34,44 L34,32 Q34,14 50,14 Q66,14 66,32 L66,44 M50,60 L50,72" },
    { .name = "user", .sw = 7, .pad = 4,
      .stroke = "M50,14 Q68,14 68,32 Q68,50 50,50 Q32,50 32,32 Q32,14 50,14 Z M16,90 Q18,62 50,62 Q82,62 84,90" },
    { .name = "users", .sw = 6, .pad = 2,
      .stroke = "M38,16 Q54,16 54,32 Q54,48 38,48 Q22,48 22,32 Q22,16 38,16 Z M8,88 Q10,62 38,62 Q66,62 68,88 "
                "M64,20 Q78,20 78,34 Q78,46 68,48 M72,62 Q92,64 92,88" },
    { .name = "cpu", .sw = 6, .pad = 4,
      .stroke = "R 26,26 48,48 7 M38,10 L38,26 M62,10 L62,26 M38,74 L38,90 M62,74 L62,90 "
                "M10,38 L26,38 M10,62 L26,62 M74,38 L90,38 M74,62 L90,62" },
    { .name = "gpu", .sw = 6, .pad = 4, .stroke = "R 6,26 88,44 7 M50,48 Q62,48 62,36 M30,70 L30,88 M70,70 L70,88" },
    { .name = "ram", .sw = 6, .pad = 6,
      .stroke = "R 8,30 84,36 5 M20,66 L20,74 M36,66 L36,74 M52,66 L52,74 M68,66 L68,74 "
                "M22,42 L22,54 M44,42 L44,54 M66,42 L66,54" },
    { .name = "refresh", .sw = 7, .pad = 4,
      .stroke = "M80,40 Q72,14 46,16 Q18,18 16,48 Q14,78 44,84 Q70,88 80,66 M82,18 L82,42 L58,42" },
    { .name = "play", .pad = 4, .fill = "M28,16 L84,50 L28,84 Z" },
    { .name = "pause", .sw = 12, .pad = 8, .stroke = "M32,20 L32,80 M68,20 L68,80" },
    { .name = "next", .sw = 9, .pad = 6, .fill = "M20,18 L64,50 L20,82 Z", .stroke = "M76,20 L76,80" },
    { .name = "prev", .sw = 9, .pad = 6, .fill = "M80,18 L36,50 L80,82 Z", .stroke = "M24,20 L24,80" },
    { .name = "plus", .sw = 9, .pad = 6, .stroke = "M50,20 L50,80 M20,50 L80,50" },
    { .name = "minus", .sw = 9, .pad = 10, .stroke = "M20,50 L80,50" },
    { .name = "grid-view", .pad = 4,
      .fill = "R 12,12 32,32 6 R 56,12 32,32 6 R 12,56 32,32 6 R 56,56 32,32 6" },
    { .name = "list-view", .pad = 6, .fill = "R 12,16 76,12 4 R 12,44 76,12 4 R 12,72 76,12 4" },
    { .name = "eject", .sw = 8, .pad = 6, .fill = "M50,16 L84,60 L16,60 Z", .stroke = "M18,74 L82,74" },
    { .name = "arrow-left", .sw = 8, .pad = 4, .stroke = "M84,50 L18,50 M40,26 L16,50 L40,74" },
    { .name = "arrow-right", .sw = 8, .pad = 4, .stroke = "M16,50 L82,50 M60,26 L84,50 L60,74" },
    { .name = "arrow-up", .sw = 8, .pad = 4, .stroke = "M50,84 L50,18 M26,42 L50,18 L74,42" },
    { .name = "info", .sw = 7, .pad = 2,
      .stroke = "M50,10 Q90,10 90,50 Q90,90 50,90 Q10,90 10,50 Q10,10 50,10 Z M50,44 L50,72 M50,28 L50,31" },
    { .name = "warn", .sw = 7, .pad = 4, .stroke = "M50,12 L92,84 L8,84 Z M50,40 L50,62 M50,72 L50,75" },
    { .name = "bell", .sw = 7, .pad = 4,
      .stroke = "M24,68 L24,44 Q24,18 50,18 Q76,18 76,44 L76,68 L86,78 L14,78 Z M40,84 Q50,94 60,84" },
    { .name = "gear", .sw = 7, .pad = 2,
      .stroke = "M50,32 Q68,32 68,50 Q68,68 50,68 Q32,68 32,50 Q32,32 50,32 Z "
                "M50,8 L50,22 M50,78 L50,92 M8,50 L22,50 M78,50 L92,50 "
                "M20,20 L30,30 M70,70 L80,80 M80,20 L70,30 M30,70 L20,80" },
    { .name = "clock-face", .sw = 7, .pad = 2,
      .stroke = "M50,10 Q90,10 90,50 Q90,90 50,90 Q10,90 10,50 Q10,10 50,10 Z M50,28 L50,52 L68,62" },
    { .name = "key", .sw = 6, .pad = 4,
      .stroke = "M34,20 Q56,20 56,40 Q56,52 46,58 L46,84 L34,84 L34,72 L24,72 L24,60 L34,60 L34,58 "
                "Q22,52 22,40 Q22,20 34,20 Z M39,34 Q44,34 44,39 Q44,44 39,44 Q34,44 34,39 Q34,34 39,34 Z" },
    { .name = "download", .sw = 8, .pad = 6, .stroke = "M50,10 L50,62 M28,42 L50,64 L72,42 M14,82 L86,82" },
    { .name = "shield", .sw = 6, .pad = 4,
      .stroke = "M50,10 L84,24 L84,52 Q84,78 50,92 Q16,78 16,52 L16,24 Z M36,50 L46,62 L66,38" },
    { .name = "sidebar", .sw = 7, .pad = 4, .stroke = "R 8,18 84,64 8 M36,18 L36,82" },
    { .name = "sort", .sw = 7, .pad = 6,
      .stroke = "M28,20 L28,80 M16,66 L28,80 L40,66 M60,24 L84,24 M60,50 L76,50 M60,76 L68,76" },
    { .name = "eye", .sw = 6, .pad = 2,
      .stroke = "M8,50 Q30,22 50,22 Q70,22 92,50 Q70,78 50,78 Q30,78 8,50 Z "
                "M50,38 Q62,38 62,50 Q62,62 50,62 Q38,62 38,50 Q38,38 50,38 Z" },
    { .name = "pulse", .sw = 7, .pad = 6, .stroke = "M6,52 L28,52 L38,26 L54,78 L66,44 L74,52 L94,52" },
    { .name = "terminal-prompt", .sw = 9, .pad = 6, .stroke = "M18,30 L44,52 L18,74 M52,78 L84,78" },
    { .name = "photo", .sw = 6, .pad = 2,
      .stroke = "R 8,18 84,64 8 M30,42 Q38,42 38,34 Q38,26 30,26 Q22,26 22,34 Q22,42 30,42 Z "
                "M12,72 L40,48 L60,66 L74,54 L90,72" },
    { .name = "film", .sw = 6, .pad = 2,
      .stroke = "R 8,20 84,60 7 M26,20 L26,80 M74,20 L74,80 M34,50 L66,50 M34,36 L66,36 M34,64 L66,64" },
    { .name = "note", .sw = 7, .pad = 4,
      .stroke = "M34,72 Q22,72 22,62 Q22,52 34,52 Q44,52 44,62 L44,20 L78,14 L78,26 L44,32" },
    { .name = "pencil", .sw = 6, .pad = 4,
      .stroke = "M18,82 L26,60 L70,16 L84,30 L40,74 Z M62,24 L76,38 M18,82 L34,76" },
    { .name = "launchpad", .pad = 4,
      .fill = "R 12,12 22,22 6 R 40,12 22,22 6 R 68,12 22,22 6 R 12,40 22,22 6 R 40,40 22,22 6 "
              "R 68,40 22,22 6 R 12,68 22,22 6 R 40,68 22,22 6 R 68,68 22,22 6" },
    { .name = "sliders", .sw = 7, .pad = 6,
      .stroke = "M14,30 L86,30 M14,50 L86,50 M14,70 L86,70 M36,22 L36,38 M60,42 L60,58 M28,62 L28,78" },
    { .name = "thermometer", .sw = 6, .pad = 4,
      .stroke = "M42,16 Q42,8 50,8 Q58,8 58,16 L58,58 Q70,64 70,76 Q70,92 50,92 Q30,92 30,76 Q30,64 42,58 Z "
                "M50,30 L50,74" },
    { .name = "fan", .sw = 5, .pad = 4,
      .stroke = "M50,50 Q34,20 52,12 Q70,6 66,26 Q62,44 50,50 Z M50,50 Q84,44 86,62 Q88,80 68,72 Q52,66 50,50 Z "
                "M50,50 Q34,80 18,70 Q2,60 18,48 Q34,40 50,50 Z" },

    /* ------------------------------ app tiles ---------------------------- */
    { .name = "app-files", .tile = 1, .grad_a = "#4aa8ff", .grad_b = "#1f6fe0", .pad = 16,
      .fill = "M14,32 L38,32 L47,42 L86,42 L86,78 L14,78 Z", .fill_color = "#ffffff" },
    { .name = "app-terminal", .tile = 1, .grad_a = "#3a3f4b", .grad_b = "#16181d", .pad = 14,
      .stroke = "M28,38 L48,56 L28,74 M54,74 L76,74", .sw = 8, .stroke_color = "#e8ecf5" },
    { .name = "app-settings", .tile = 1, .grad_a = "#9aa4b2", .grad_b = "#5b6472", .pad = 14,
      .stroke = "M50,36 Q64,36 64,50 Q64,64 50,64 Q36,64 36,50 Q36,36 50,36 Z "
                "M50,20 L50,28 M50,72 L50,80 M20,50 L28,50 M72,50 L80,50 "
                "M29,29 L35,35 M65,65 L71,71 M71,29 L65,35 M35,65 L29,71",
      .sw = 6, .stroke_color = "#ffffff" },
    { .name = "app-sysinfo", .tile = 1, .grad_a = "#5ac8fa", .grad_b = "#1d7fd6", .pad = 14,
      .stroke = "M50,20 Q80,20 80,50 Q80,80 50,80 Q20,80 20,50 Q20,20 50,20 Z M50,44 L50,66 M50,32 L50,35",
      .sw = 6, .stroke_color = "#ffffff" },
    { .name = "app-image", .tile = 1, .grad_a = "#ffb35c", .grad_b = "#f0763a", .pad = 16,
      .fill = "M34,42 Q42,42 42,34 Q42,26 34,26 Q26,26 26,34 Q26,42 34,42 Z "
              "M22,74 L44,52 L60,68 L70,58 L80,74 Z",
      .fill_color = "#ffffff" },
    { .name = "app-video", .tile = 1, .grad_a = "#f2607d", .grad_b = "#b52a55", .pad = 14,
      .fill = "M38,32 L72,52 L38,72 Z", .fill_color = "#ffffff",
      .stroke = "R 18,20 66,62 8", .sw = 6, .stroke_color = "#ffffffcc" },
    { .name = "app-music", .tile = 1, .grad_a = "#ff6a8b", .grad_b = "#e0336b", .pad = 16,
      .stroke = "M38,68 Q28,68 28,60 Q28,52 38,52 Q46,52 46,60 L46,30 L72,25 L72,34 L46,39",
      .sw = 7, .stroke_color = "#ffffff" },
    { .name = "app-pdf", .tile = 1, .grad_a = "#ff7a6b", .grad_b = "#d93b3b", .pad = 14,
      .stroke = "M32,22 L58,22 L74,38 L74,80 L32,80 Z M58,22 L58,38 L74,38 M42,52 L64,52 M42,62 L64,62 M42,72 L56,72",
      .sw = 6, .stroke_color = "#ffffff" },
    { .name = "app-textedit", .tile = 1, .grad_a = "#8f9bff", .grad_b = "#4a54d6", .pad = 14,
      .stroke = "M30,74 L36,56 L68,24 L80,36 L48,68 Z M62,30 L74,42 M30,74 L44,70",
      .sw = 6, .stroke_color = "#ffffff" },
    { .name = "app-launcher", .tile = 1, .grad_a = "#7d8bff", .grad_b = "#3a3fd6", .pad = 14,
      .fill = "R 24,24 16,16 5 R 44,24 16,16 5 R 64,24 16,16 5 R 24,44 16,16 5 R 44,44 16,16 5 "
              "R 64,44 16,16 5 R 24,64 16,16 5 R 44,64 16,16 5 R 64,64 16,16 5",
      .fill_color = "#ffffff" },
    { .name = "app-diagnostics", .tile = 1, .grad_a = "#43d9a3", .grad_b = "#12a37a", .pad = 14,
      .stroke = "M18,52 L34,52 L42,32 L56,72 L64,46 L70,52 L84,52", .sw = 7, .stroke_color = "#ffffff" },
    { .name = "app-installer", .tile = 1, .grad_a = "#59c2ff", .grad_b = "#2b6fd6", .pad = 16,
      .stroke = "M50,22 L50,58 M34,44 L50,60 L66,44 M26,72 L74,72", .sw = 8, .stroke_color = "#ffffff" },
    { .name = "app-trash", .tile = 1, .grad_a = "#b9c2cf", .grad_b = "#7c8797", .pad = 14,
      .stroke = "M34,32 L66,32 L62,80 L38,80 Z M28,32 L72,32 M43,20 L57,20 M45,42 L45,70 M55,42 L55,70",
      .sw = 6, .stroke_color = "#ffffff" },

    /* --------------------------- brand mark ------------------------------ */
    { .name = "mica-mark", .sw = 9, .pad = 6,
      .stroke = "M20,76 Q20,26 50,14 Q62,34 50,52 Q44,62 44,76 M56,76 Q58,48 80,40 Q84,60 70,76" },
};
const size_t ml_icon_table_n = sizeof(ml_icon_table) / sizeof(ml_icon_table[0]);
