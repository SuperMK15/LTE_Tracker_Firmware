#!/usr/bin/env python3
"""Subscribe to tracker MQTT messages and append them to a CSV file."""

from __future__ import annotations

import argparse
import csv
import json
import logging
import math
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

BROKER = "broker.emqx.io"
PORT = 1883
TOPIC = "warg/efs-lte-tracker/+/location"
DATA_DIR = Path(__file__).resolve().parent / "data"
FIELDS = (
    "received_at_utc",
    "topic",
    "session_id",
    "sequence",
    "elapsed_ms",
    "fix",
    "gnss_time",
    "latitude",
    "longitude",
    "altitude_m",
    "speed_kmh",
)
NUMBER_FIELDS = ("latitude", "longitude", "altitude_m", "speed_kmh")
LOGGER = logging.getLogger("mqtt_csv")


def optional_number(value: Any, field: str) -> float | int | None:
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{field} must be a number or null")
    if not math.isfinite(value):
        raise ValueError(f"{field} must be finite")
    return value


def normalize_message(topic: str, payload: bytes) -> dict[str, Any]:
    parts = topic.split("/")
    if (
        len(parts) != 4
        or parts[:2] != ["warg", "efs-lte-tracker"]
        or parts[3] != "location"
        or not parts[2]
    ):
        raise ValueError("unexpected MQTT topic")

    message = json.loads(payload)
    if not isinstance(message, dict):
        raise ValueError("payload must be a JSON object")

    session_id = message.get("session_id")
    sequence = message.get("sequence")
    elapsed_ms = message.get("elapsed_ms")
    fix = message.get("fix")
    if session_id != parts[2]:
        raise ValueError("payload session_id does not match the topic")
    if isinstance(sequence, bool) or not isinstance(sequence, int):
        raise ValueError("sequence must be an integer")
    if isinstance(elapsed_ms, bool) or not isinstance(elapsed_ms, int):
        raise ValueError("elapsed_ms must be an integer")
    if not isinstance(fix, bool):
        raise ValueError("fix must be a boolean")

    row: dict[str, Any] = {
        "received_at_utc": datetime.now(timezone.utc).isoformat(),
        "topic": topic,
        "session_id": session_id,
        "sequence": sequence,
        "elapsed_ms": elapsed_ms,
        "fix": fix,
        "gnss_time": message.get("gnss_time"),
    }

    for field in NUMBER_FIELDS:
        row[field] = optional_number(message.get(field), field)

    if fix:
        latitude = row["latitude"]
        longitude = row["longitude"]
        if latitude is None or not -90 <= latitude <= 90:
            raise ValueError("fixed sample must have a valid latitude")
        if longitude is None or not -180 <= longitude <= 180:
            raise ValueError("fixed sample must have a valid longitude")

    return row


def existing_keys(path: Path) -> set[tuple[str, int]]:
    if not path.exists() or path.stat().st_size == 0:
        return set()
    with path.open("r", newline="", encoding="utf-8") as csv_file:
        return {
            (row["session_id"], int(row["sequence"]))
            for row in csv.DictReader(csv_file)
            if row.get("session_id") and row.get("sequence")
        }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Subscribe to tracker coordinates and append them to CSV."
    )
    parser.add_argument("--host", default=BROKER, help=f"MQTT broker (default: {BROKER})")
    parser.add_argument("--port", type=int, default=PORT, help=f"MQTT port (default: {PORT})")
    parser.add_argument("--topic", default=TOPIC, help=f"MQTT topic filter (default: {TOPIC})")
    parser.add_argument(
        "--output",
        type=Path,
        default=DATA_DIR / "tracker_locations.csv",
        help=f"CSV output path (default: {DATA_DIR / 'tracker_locations.csv'})",
    )
    parser.add_argument(
        "--client-id",
        default="warg-tracker-csv-recorder",
        help="MQTT client ID; use a unique value for each subscriber",
    )
    return parser.parse_args()


def main() -> int:
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        LOGGER.error(
            "Missing dependency. Install with: "
            "python -m pip install -r utils\\requirements.txt"
        )
        return 2

    args = parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    seen = existing_keys(args.output)
    needs_header = not args.output.exists() or args.output.stat().st_size == 0

    try:
        output_file = args.output.open("a", newline="", encoding="utf-8")
    except OSError as error:
        LOGGER.error("Cannot open CSV output %s: %s", args.output, error)
        return 1

    writer = csv.DictWriter(output_file, fieldnames=FIELDS)
    if needs_header:
        writer.writeheader()
        output_file.flush()

    def on_connect(
        client: Any, userdata: Any, flags: Any, reason_code: Any, properties: Any
    ) -> None:
        if reason_code != 0:
            LOGGER.error("MQTT connection rejected: %s", reason_code)
            return
        result, _ = client.subscribe(args.topic, qos=1)
        if result != mqtt.MQTT_ERR_SUCCESS:
            LOGGER.error("Failed to subscribe to %s (code %s)", args.topic, result)
            return
        LOGGER.info("Connected to %s:%d; subscribed to %s", args.host, args.port, args.topic)

    def on_message(client: Any, userdata: Any, message: Any) -> None:
        try:
            row = normalize_message(message.topic, message.payload)
            key = (row["session_id"], row["sequence"])
            if key in seen:
                LOGGER.debug("Ignoring duplicate %s sequence %s", *key)
                return
            writer.writerow(row)
            output_file.flush()
            seen.add(key)
            LOGGER.info(
                "Saved session %s sequence %s (fix=%s)",
                row["session_id"],
                row["sequence"],
                row["fix"],
            )
        except (UnicodeDecodeError, json.JSONDecodeError, ValueError, TypeError) as error:
            LOGGER.warning("Ignoring invalid message on %s: %s", message.topic, error)
        except OSError:
            LOGGER.exception("Failed writing to %s; stopping subscriber", args.output)
            client.disconnect()

    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=args.client_id,
    )
    client.on_connect = on_connect
    client.on_message = on_message

    LOGGER.info("Appending live telemetry to %s", args.output.resolve())
    LOGGER.info("Public broker traffic is unauthenticated and unencrypted.")
    try:
        client.connect(args.host, args.port, keepalive=60)
        client.loop_forever()
    except KeyboardInterrupt:
        LOGGER.info("Stopping subscriber.")
    except OSError as error:
        LOGGER.error("MQTT connection failed: %s", error)
        return 1
    finally:
        client.disconnect()
        output_file.close()
    return 0


if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )
    sys.exit(main())
