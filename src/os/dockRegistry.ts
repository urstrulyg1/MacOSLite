/**
 * Live Dock icon centers, in viewport pixels.
 * Window genie animations read this so open / minimize / restore stay
 * spatially connected to the icon that represents the app.
 * The Dock writes; everyone else only reads. No React state, no re-render.
 */

export interface DockPoint {
  cx: number;
  cy: number;
  size: number;
}

const points = new Map<string, DockPoint>();

export function setDockTarget(id: string, pt: DockPoint | null) {
  if (!pt) points.delete(id);
  else points.set(id, pt);
}

export function getDockTarget(id: string): DockPoint | null {
  return points.get(id) ?? points.get("finder") ?? null;
}

export function dockTargetIds(): string[] {
  return [...points.keys()];
}
