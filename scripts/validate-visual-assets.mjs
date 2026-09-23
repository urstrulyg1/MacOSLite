#!/usr/bin/env node
/**
 * G1OS visual asset quality gate.
 *
 * The canonical icon family currently lives in vector TSX/SVG artwork under
 * src/os/icons. This check prevents accidental introduction of tiny raster
 * masters or raster-upscaling code and verifies the documented 8K ceiling.
 * It intentionally uses only Node built-ins so the quality gate adds no
 * runtime dependency to the lightweight OS.
 */

import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { join, extname } from "node:path";

const ROOT = process.cwd();
const ICON_DIR = join(ROOT, "src", "os", "icons");
const SOURCE_DIR = join(ROOT, "assets", "source");
const MAX_MASTER = 7680;
const RASTER_EXTENSIONS = new Set([".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp"]);
const FORBIDDEN_UPSCALE_MARKERS = [/nearest[-_ ]neighbor/i, /image[-_ ]?rendering\s*:\s*pixelated/i, /upscale|upscaling/i];

let failures = 0;
let checked = 0;

function fail(message) {
  console.error(`ERROR: ${message}`);
  failures += 1;
}

function walk(dir) {
  if (!existsSync(dir)) return [];
  const result = [];
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) result.push(...walk(path));
    else result.push(path);
  }
  return result;
}

function readPngDimensions(buffer) {
  if (buffer.length < 24 || buffer.toString("ascii", 1, 4) !== "PNG") return null;
  return { width: buffer.readUInt32BE(16), height: buffer.readUInt32BE(20) };
}

function readJpegDimensions(buffer) {
  if (buffer.length < 4 || buffer[0] !== 0xff || buffer[1] !== 0xd8) return null;
  let offset = 2;
  while (offset + 9 < buffer.length) {
    if (buffer[offset] !== 0xff) { offset += 1; continue; }
    const marker = buffer[offset + 1];
    const length = buffer.readUInt16BE(offset + 2);
    if (marker >= 0xc0 && marker <= 0xc3) {
      return { width: buffer.readUInt16BE(offset + 7), height: buffer.readUInt16BE(offset + 5) };
    }
    if (!length) break;
    offset += 2 + length;
  }
  return null;
}

for (const file of walk(ICON_DIR)) {
  const ext = extname(file).toLowerCase();
  const text = readFileSync(file, "utf8");
  checked += 1;

  for (const marker of FORBIDDEN_UPSCALE_MARKERS) {
    if (marker.test(text)) fail(`${file.replace(ROOT + "/", "")}: forbidden raster-upscaling marker ${marker}`);
  }
}

for (const file of walk(SOURCE_DIR)) {
  const ext = extname(file).toLowerCase();
  if (!RASTER_EXTENSIONS.has(ext)) continue;
  checked += 1;
  const buffer = readFileSync(file);
  const dimensions = ext === ".png" ? readPngDimensions(buffer) : ext === ".jpg" || ext === ".jpeg" ? readJpegDimensions(buffer) : null;
  if (dimensions && (dimensions.width > MAX_MASTER || dimensions.height > MAX_MASTER)) {
    fail(`${file.replace(ROOT + "/", "")}: master exceeds ${MAX_MASTER}px (${dimensions.width}x${dimensions.height})`);
  }
  if (dimensions && Math.min(dimensions.width, dimensions.height) < 1024) {
    fail(`${file.replace(ROOT + "/", "")}: raster master is below 1024px; use vector/source artwork instead`);
  }
}

if (!existsSync(ICON_DIR)) fail("src/os/icons is missing");

if (failures) {
  console.error(`Visual asset validation failed: ${failures} issue(s), ${checked} file(s) checked.`);
  process.exit(1);
}

console.log(`Visual asset validation passed: ${checked} file(s) checked; vector-first 8K policy is intact.`);
