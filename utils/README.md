# MQTT capture and KML utilities

The tracker publishes JSON to the public EMQX broker at
`broker.emqx.io:1883`, on
`warg/efs-lte-tracker/<session-id>/location`.

## Capture live MQTT messages to CSV

From the repository root in PowerShell:

```powershell
py -m venv utils\.venv
utils\.venv\Scripts\python.exe -m pip install -r utils\requirements.txt
utils\.venv\Scripts\python.exe utils\subscribe_mqtt.py
```

The subscriber listens on `warg/efs-lte-tracker/+/location`, creates the CSV
if needed, appends each new message, and flushes each row immediately. It
ignores duplicate `(session_id, sequence)` messages, including duplicates
already present in the output CSV. By default it writes to
`utils/data/tracker_locations.csv`; files in that folder are git-ignored.
Leave it running while the tracker is publishing; stop with **Ctrl+C**. The
public broker is unauthenticated and unencrypted, so anyone can read or publish
to these topics.

To select one session or another output:

```powershell
utils\.venv\Scripts\python.exe utils\subscribe_mqtt.py `
  --topic "warg/efs-lte-tracker/YOUR-SESSION-ID/location" `
  --output utils\data\my_track.csv
```

## Convert a CSV to KML

This script uses only the Python standard library:

```powershell
py utils\csv_to_kml.py utils\data\tracker_locations.csv
```

By default the KML is written to `utils/data/<csv-name>.kml`. Each session
becomes a folder; valid fixes are included as point placemarks and connected
as a track line. No-fix samples are omitted. Fake test samples (negative
sequence numbers) are excluded unless `--include-test` is supplied:

```powershell
py utils\csv_to_kml.py utils\data\tracker_locations.csv --include-test
```

The MQTT broker is a live relay, not historical storage. Start the CSV
subscriber before or while the tracker publishes; messages published before
the subscriber is connected cannot be recovered from this public broker.
