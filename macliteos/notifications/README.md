# notifications/

In-memory notification center only (spec §39): banners are compositor-owned
surfaces with slide+fade springs; history lives in the compositor's RAM ring
and is deliberately not persisted (no indexing, no telemetry).
