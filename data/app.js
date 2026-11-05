const setText = (id, value) => {
  document.getElementById(id).textContent = value;
};

const yesNo = (value) => (value ? "Yes" : "No");

async function refresh() {
  try {
    const response = await fetch("/api/status", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    const data = await response.json();

    setText("fix", yesNo(data.fix));
    setText(
      "coordinates",
      data.fix ? `${data.latitude}, ${data.longitude}` : "No fix"
    );
    setText("gnss-time", data.gnss_time || "—");
    setText("gps-bytes", data.gps_chars_processed);
    setText("gps-valid", data.gps_nmea_valid);
    setText("gps-errors", data.gps_nmea_checksum_errors);
    setText("altitude", data.fix ? `${data.altitude_m} m` : "—");
    setText("speed", data.fix ? `${data.speed_kmh} km/h` : "—");
    setText("modem", yesNo(data.modem_available));
    setText("network", yesNo(data.network_registered));
    setText("packet-data", yesNo(data.packet_data_connected));
    setText("sd", yesNo(data.sd_available));
    setText("upload", data.last_upload);
    setText("session", data.session_id);
    setText("sequence", data.sequence);
    setText("uploaded", data.uploaded_sequence);
    setText("queued", data.queued_samples);
    setText("dropped", data.dropped_samples);
    setText("missed", data.missed_samples);
    setText("updated", `Updated ${new Date().toLocaleTimeString()}`);
  } catch (error) {
    setText("updated", `Unable to load device status: ${error.message}`);
  }
}

refresh();
setInterval(refresh, 1000);

document.getElementById("test-location").addEventListener("click", async (event) => {
  const button = event.currentTarget;
  const result = document.getElementById("test-result");
  button.disabled = true;
  result.textContent = "Queueing test coordinate…";
  try {
    const response = await fetch("/api/test-location", { method: "POST" });
    const payload = await response.json();
    if (!response.ok) {
      throw new Error(payload.error || `HTTP ${response.status}`);
    }
    result.textContent =
      "Test coordinate queued. Check Last MQTT publish for its result.";
  } catch (error) {
    result.textContent = `Unable to queue test coordinate: ${error.message}`;
  } finally {
    button.disabled = false;
  }
});
