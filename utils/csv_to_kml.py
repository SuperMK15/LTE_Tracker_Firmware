#!/usr/bin/env python3
"""Convert the MQTT recorder CSV into a KML track grouped by session."""

from __future__ import annotations

import argparse
import csv
import math
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path
from typing import Any

KML_NAMESPACE = "http://www.opengis.net/kml/2.2"
DATA_DIR = Path(__file__).resolve().parent / "data"
ET.register_namespace("", KML_NAMESPACE)


def kml_tag(name: str) -> str:
    return f"{{{KML_NAMESPACE}}}{name}"


def parse_bool(value: str | None) -> bool:
    return (value or "").strip().lower() in {"true", "1", "yes"}


def parse_finite_float(value: str | None) -> float | None:
    if value is None or not value.strip():
        return None
    number = float(value)
    return number if math.isfinite(number) else None


def read_tracks(csv_path: Path, include_test: bool) -> dict[str, list[dict[str, Any]]]:
    tracks: dict[str, dict[int, dict[str, Any]]] = defaultdict(dict)
    with csv_path.open("r", newline="", encoding="utf-8-sig") as csv_file:
        reader = csv.DictReader(csv_file)
        required = {"session_id", "sequence", "fix", "latitude", "longitude"}
        missing = required.difference(reader.fieldnames or [])
        if missing:
            raise ValueError(f"CSV missing required columns: {', '.join(sorted(missing))}")

        for line_number, row in enumerate(reader, start=2):
            try:
                session_id = (row.get("session_id") or "").strip()
                sequence = int(row["sequence"])
                if not session_id or (sequence < 0 and not include_test):
                    continue
                if not parse_bool(row.get("fix")):
                    continue

                latitude = parse_finite_float(row.get("latitude"))
                longitude = parse_finite_float(row.get("longitude"))
                if (
                    latitude is None
                    or longitude is None
                    or not -90 <= latitude <= 90
                    or not -180 <= longitude <= 180
                ):
                    continue

                altitude = parse_finite_float(row.get("altitude_m"))
                tracks[session_id][sequence] = {
                    "sequence": sequence,
                    "latitude": latitude,
                    "longitude": longitude,
                    "altitude_m": altitude or 0.0,
                    "speed_kmh": parse_finite_float(row.get("speed_kmh")),
                    "gnss_time": (row.get("gnss_time") or "").strip(),
                    "received_at_utc": (row.get("received_at_utc") or "").strip(),
                }
            except (KeyError, TypeError, ValueError) as error:
                print(
                    f"Skipping invalid CSV row {line_number}: {error}",
                    file=sys.stderr,
                )

    return {
        session_id: [points[sequence] for sequence in sorted(points)]
        for session_id, points in tracks.items()
    }


def add_point(folder: ET.Element, point: dict[str, Any]) -> None:
    placemark = ET.SubElement(folder, kml_tag("Placemark"))
    ET.SubElement(placemark, kml_tag("name")).text = f"Sequence {point['sequence']}"

    timestamp = point["gnss_time"] or point["received_at_utc"]
    if timestamp:
        time_element = ET.SubElement(placemark, kml_tag("TimeStamp"))
        ET.SubElement(time_element, kml_tag("when")).text = timestamp

    description_parts = [f"Sequence: {point['sequence']}"]
    if point["speed_kmh"] is not None:
        description_parts.append(f"Speed: {point['speed_kmh']} km/h")
    ET.SubElement(placemark, kml_tag("description")).text = ", ".join(
        description_parts
    )

    point_element = ET.SubElement(placemark, kml_tag("Point"))
    ET.SubElement(point_element, kml_tag("coordinates")).text = (
        f"{point['longitude']:.8f},{point['latitude']:.8f},"
        f"{point['altitude_m']:.2f}"
    )


def write_kml(
    tracks: dict[str, list[dict[str, Any]]],
    output_path: Path,
) -> int:
    kml = ET.Element(kml_tag("kml"))
    document = ET.SubElement(kml, kml_tag("Document"))
    ET.SubElement(document, kml_tag("name")).text = "Tracker locations"

    total_points = 0
    for session_id, points in sorted(tracks.items()):
        folder = ET.SubElement(document, kml_tag("Folder"))
        ET.SubElement(folder, kml_tag("name")).text = session_id
        total_points += len(points)

        if len(points) >= 2:
            line_placemark = ET.SubElement(folder, kml_tag("Placemark"))
            ET.SubElement(line_placemark, kml_tag("name")).text = "Track"
            line_string = ET.SubElement(line_placemark, kml_tag("LineString"))
            ET.SubElement(line_string, kml_tag("tessellate")).text = "1"
            ET.SubElement(line_string, kml_tag("altitudeMode")).text = "absolute"
            ET.SubElement(line_string, kml_tag("coordinates")).text = "\n".join(
                f"{point['longitude']:.8f},{point['latitude']:.8f},"
                f"{point['altitude_m']:.2f}"
                for point in points
            )

        for point in points:
            add_point(folder, point)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    tree = ET.ElementTree(kml)
    ET.indent(tree, space="  ")
    tree.write(output_path, encoding="utf-8", xml_declaration=True)
    return total_points


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert tracker_locations.csv into a session-grouped KML."
    )
    parser.add_argument("csv_file", type=Path, help="Input CSV from subscribe_mqtt.py")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Output KML path (default: input file with .kml extension)",
    )
    parser.add_argument(
        "--include-test",
        action="store_true",
        help="Include negative-sequence fake test coordinates",
    )
    args = parser.parse_args()
    output_path = args.output or DATA_DIR / f"{args.csv_file.stem}.kml"

    try:
        tracks = read_tracks(args.csv_file, args.include_test)
        point_count = write_kml(tracks, output_path)
    except (OSError, UnicodeError, csv.Error, ValueError) as error:
        parser.error(str(error))

    print(
        f"Wrote {point_count} fixed coordinates across {len(tracks)} sessions "
        f"to {output_path}"
    )
    if point_count == 0:
        print("No valid GNSS fixes were present in the CSV.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
