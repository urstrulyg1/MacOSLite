/**
 * Resolution-aware visual asset policy for G1OS.
 *
 * Canonical pictorial icons are vector masters (TSX/SVG). This module keeps
 * runtime rasterization resolution-aware so a future raster-backed asset can
 * use the smallest sufficient texture without ever upscaling a tiny source.
 */

export const G1_ICON_MASTER_SIZE = 7680;

/** Supported logical export sizes, from normal UI through ultra-high-DPI work. */
export const G1_ICON_SIZES = [
  16, 20, 24, 32, 48, 64, 80, 128, 256, 512, 1024, 2048, 4096, 7680,
] as const;

export type G1IconResolution = (typeof G1_ICON_SIZES)[number];

/**
 * Pick the smallest generated raster that can cover a logical icon size at
 * the current display scale. Vector masters bypass this choice entirely.
 */
export function selectIconResolution(logicalSize: number, scale = 1): G1IconResolution {
  const required = Math.max(1, Math.ceil(logicalSize * scale));
  return G1_ICON_SIZES.find((size) => size >= required) ?? G1_ICON_SIZES[G1_ICON_SIZES.length - 1];
}

export function getIconRenderProfile(logicalSize: number) {
  const scale = typeof window === "undefined" ? 1 : Math.max(1, window.devicePixelRatio || 1);
  const resolution = selectIconResolution(logicalSize, scale);

  return {
    scale,
    resolution,
    masterSize: G1_ICON_MASTER_SIZE,
    vectorMaster: true,
  } as const;
}

/** Stable cache key for resolution-aware runtime texture caches. */
export function iconCacheKey(name: string, logicalSize: number, scale = 1): string {
  return `${name}:${selectIconResolution(logicalSize, scale)}@${scale.toFixed(2)}`;
}
