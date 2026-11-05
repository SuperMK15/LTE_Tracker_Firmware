# LTE tracker firmware

## Hardware and behavior

The firmware targets the ESP32-C3-MINI-1 and SIM7080G shown in
`schematic.pdf`. It polls `AT+CGNSINF` once per second, logs every sample to a
session-specific CSV when a card is present, and posts each sample to
Supabase over the modem's LTE data connection. GNSS coordinates are null until
the modem has a fix. Each boot creates a new UUID session ID.

The board pin assignments are in `include/BoardConfig.h`: modem UART RX/TX are
GPIO20/21, modem PWRKEY is GPIO0, and the SD card SPI bus is GPIO6/7/2 with
chip-select GPIO10 and card detect GPIO3. The card-detect input is treated as
active-low.

The local debug access point serves `data/` from LittleFS. Its SSID ends with
the last four bytes of the ESP32 Wi-Fi MAC address, so it remains the same
across reboots. Its password and URL are printed to the serial monitor. The
page shows GNSS, cellular, SD, session, and last-upload status. Telemetry still
uses a new session UUID for each power cycle.

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
diagnostics. If the log reports no SIM7080G AT response, cellular registration
has not started yet: check modem supply, the PWRKEY circuit, and UART
connections. The firmware probes common UART baud rates; an APN or SIM-plan
issue can only be diagnosed after the modem answers AT.

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

The REST endpoint is `/rest/v1/tracker_locations`; the firmware sends one
insert per sample, including samples without a GNSS fix. LTE registration,
signal conditions, and HTTP latency can delay samples; the SD CSV is the local
record when available.

The debug page distinguishes cellular registration from an active packet-data
session. SIM7080G `CGATT` reports cellular packet attachment, not whether the
application PDP context has an IP address; the firmware checks `CNACT`/local IP
and activates the PDP context when needed.

## CSV format

Each power cycle creates `/track_<session-id>.csv`, with columns for session
ID, sample sequence, uptime, GNSS UTC, fix state, coordinates, altitude, and
speed. The SD card is mounted over SPI; removal pauses logging, and insertion
resumes it.
