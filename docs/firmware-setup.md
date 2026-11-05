# LTE tracker firmware

## Hardware and behavior

The firmware targets the ESP32-C3-MINI-1, SIM7080G, and an external Holybro M10
GNSS receiver. It reads NMEA from the M10 once per second, logs each sample to
a session-specific CSV when a card is present, and publishes samples to the
public EMQX MQTT broker over the SIM7080G LTE data connection. The SIM7080G is
used for cellular data only; GNSS is read from the separate M10 UART.
Coordinates are null until the M10 has a fix. Each boot creates a new UUID
session ID.

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
page shows GNSS, cellular, SD, session, and last-publish status. Telemetry uses
a new session UUID for each power cycle.

The access point and HTTP server start before modem initialization. M10 NMEA
sampling and SD logging run independently from the SIM7080G cellular and MQTT publishing task. Thus LTE socket operations no
longer occupy the GPS
UART or prevent GNSS sampling.

## Configure and build

1. Copy `include/secrets.h.example` to `include/secrets.h`.
2. Set the carrier APN. The public test broker does not require credentials.
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

## MQTT publishing

The firmware connects to `broker.emqx.io:1883` using MQTT 3.1.1 over plain
TCP. It keeps one connection open, publishes each sample separately at QoS 1,
and removes a sample from the RAM queue only after the broker returns PUBACK.
The tracker uses the topic
`warg/efs-lte-tracker/<session-id>/location`, where `<session-id>` is printed
at startup and shown on the debug page. Each JSON message includes the session
ID, sequence, elapsed milliseconds, fix state, GNSS UTC time, coordinates,
altitude, and speed.

This EMQX public broker is for testing only. Messages and topic names are
public, unauthenticated, unencrypted, and are not a durable history or database.
QoS 1 confirms receipt by the broker, not by a subscriber or database. A
subscriber must be online to receive live messages; use a separate subscriber
or bridge to persist them elsewhere. For example, with Mosquitto clients:

```text
mosquitto_sub -h broker.emqx.io -p 1883 -t "warg/efs-lte-tracker/<session-id>/location" -v
```

The firmware buffers up to 128 unsent samples in RAM and retries failed
publishes. The queue is volatile and loses its contents on power loss. If it
fills, new samples are dropped and the debug page's dropped counter increases;
continuous delivery cannot be guaranteed through long outages without durable
storage. The debug page's **Send test coordinate** button publishes a fake
coordinate (47.6553, -122.3080) with a negative sequence number; test
coordinates are not written to the SD card.

GNSS uses its own M10 UART and task, so SIM7080G LTE operations do not block
GPS reads. The debug page reports missed sample intervals and M10 NMEA
byte/checksum counts to help distinguish a missing UART signal from a receiver
that has not acquired a fix. SD logging is independent of the MQTT queue.

The debug page distinguishes cellular registration from an active packet-data
session. SIM7080G `CGATT` reports cellular packet attachment, not whether the
application PDP context has an IP address; the firmware checks `CNACT`/local IP
and activates the PDP context when needed.

## CSV format

Each power cycle creates `/track_<session-id>.csv`, with columns for session
ID, sample sequence, uptime, GNSS UTC, fix state, coordinates, altitude, and
speed. The SD card is mounted over SPI; removal pauses logging, and insertion
resumes it. NMEA sampling runs on a separate FreeRTOS task from modem
initialization, network reconnects, and MQTT publishing.
