# LTE tracker firmware

## Hardware and behavior

The firmware targets the ESP32-C3-MINI-1, SIM7080G, and an external Holybro M10
GNSS receiver. It reads NMEA from the M10 once per second, logs each sample to
a session-specific CSV when a card is present, and posts samples to Supabase
over the SIM7080G LTE data connection. The SIM7080G is used for cellular data
only; GNSS is read from the separate M10 UART. Coordinates are null until the
M10 has a fix. Each boot creates a new UUID session ID.

The board pin assignments are in `include/BoardConfig.h`: modem UART RX/TX are
GPIO20/21, modem PWRKEY is GPIO0, M10 GPS-to-ESP RX is GPIO4, and ESP-to-GPS TX
is GPIO5 (115200 baud). On the GPS-side connector labels, GPS RX is wired to
GPIO4 and GPS TX to GPIO5; the ESP32 UART therefore uses RX=GPIO5 and TX=GPIO4.
SD SPI uses SCK/MISO/MOSI GPIO6/2/7, chip-select GPIO10,
and active-low card detect GPIO3. The GPS UART wiring is shown on sheet 2 of
`schematic.pdf`.

The local debug access point serves `data/` from LittleFS. Its SSID ends with
the last four bytes of the ESP32 Wi-Fi MAC address, so it remains the same
across reboots. Its password and URL are printed to the serial monitor. The
page shows GNSS, cellular, SD, session, and last-upload status. Telemetry still
uses a new session UUID for each power cycle.

The access point and HTTP server start before modem initialization. M10 NMEA
sampling and SD logging run independently from the SIM7080G cellular and
Supabase-upload task. Thus LTE socket operations no longer occupy the GPS
UART or prevent GNSS sampling.

## Configure and build

1. Copy `include/secrets.h.example` to `include/secrets.h`.
2. Set the carrier APN and Supabase project URL/publishable (or anon) key.
   Do not put a Supabase `service_role` key in device firmware.
3. Build and upload the firmware with PlatformIO:

   ```text
   pio run
   pio run -t upload
   pio run -t uploadfs
   pio device monitor
   ```

Upload the filesystem image after changing any files in `data/`.

The serial console uses the ESP32-C3's native USB Serial/JTAG connection at
115200 baud. Open the monitor before resetting the board to capture startup
diagnostics. The SIM7080G UART is configured for 115200 baud. If the log
reports no SIM7080G AT response, check modem supply, the PWRKEY circuit, UART
connections, and the modem's configured baud rate before diagnosing APN or
SIM-plan issues.

Supabase requests use HTTPS/TLS encryption without validating the server's
certificate. This avoids installing a CA on the SIM7080G, but leaves the
connection vulnerable to server impersonation and interception. Use only a
publishable/anon key, never a service-role key.

## Supabase table

Create the destination table and insert-only policy in the Supabase SQL
editor. The publishable/anon key can then insert telemetry without granting
read access. The sample policy is intentionally open to inserts by anyone
holding that public key; for production, put an authenticated Edge Function or
other device-authenticated endpoint in front of this table.

```sql
create table public.tracker_locations (
  session_id uuid not null,
  sequence bigint not null,
  elapsed_ms bigint not null,
  fix boolean not null,
  gnss_time timestamptz,
  latitude double precision,
  longitude double precision,
  altitude_m double precision,
  speed_kmh double precision,
  inserted_at timestamptz not null default now(),
  primary key (session_id, sequence)
);

alter table public.tracker_locations enable row level security;
grant insert on public.tracker_locations to anon;
create policy "device telemetry insert"
  on public.tracker_locations
  for insert to anon
  with check (true);
```

The REST endpoint is `/rest/v1/tracker_locations`; the firmware sends all
currently queued samples as one JSON-array insert, including samples without a
GNSS fix. LTE registration,
signal conditions, and HTTP latency can delay samples; the SD CSV is the local
record when available. After a successful upload, the firmware removes those
samples from its RAM queue; samples collected during the request are sent in
the next request. Failed requests are retried with exponential backoff. GNSS
uses its own M10 UART and task, so SIM7080G LTE operations do not
block GPS reads. The debug page reports missed sample intervals and M10 NMEA
byte/checksum counts to help distinguish a missing UART signal from a receiver
that has not acquired a fix. Up to 32 unsent samples are queued in RAM; if that
queue fills, its oldest entries are discarded and the dropped-sample counter
increases. SD logging is independent of the upload queue.

The debug page's **Send test coordinate** button queues a fake coordinate
(47.6553, -122.3080) for the same Supabase table. Test rows use negative
sequence numbers, are not written to the SD card, and should be excluded from
real location analysis.

The debug page distinguishes cellular registration from an active packet-data
session. SIM7080G `CGATT` reports cellular packet attachment, not whether the
application PDP context has an IP address; the firmware checks `CNACT`/local IP
and activates the PDP context when needed.

## CSV format

Each power cycle creates `/track_<session-id>.csv`, with columns for session
ID, sample sequence, uptime, GNSS UTC, fix state, coordinates, altitude, and
speed. The SD card is mounted over SPI; removal pauses logging, and insertion
resumes it. NMEA sampling runs on a separate FreeRTOS task from modem
initialization, network reconnects, and HTTPS uploads.
