import html
import json
import os
from datetime import datetime, timezone
from typing import Annotated, Optional

import httpx
from fastapi import Depends, FastAPI, HTTPException, Query, Request, status
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import Response, StreamingResponse
from fastapi import APIRouter

from app import cloud_client
from app.database import db_session, init_db, row_to_device, row_to_reading, utc_now_iso
from app.schemas import DeviceHeartbeat, DeviceRead, DeviceRegister, SensorReadingIn, SensorReadingOut

CAMERA_REQUEST_TIMEOUT = float(os.getenv("CAMERA_REQUEST_TIMEOUT", "10"))

app = FastAPI(title="Jetson Device Gateway", version="0.2.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.on_event("startup")
def startup() -> None:
    init_db()


def client_ip(request: Request) -> Optional[str]:
    forwarded = request.headers.get("x-forwarded-for")
    if forwarded:
        return forwarded.split(",", 1)[0].strip()
    return request.client.host if request.client else None


ClientIp = Annotated[Optional[str], Depends(client_ip)]


def html_escape(value: object) -> str:
    return html.escape("" if value is None else str(value), quote=True)


def device_status_label(last_seen_at: Optional[str]) -> tuple[str, str]:
    if not last_seen_at:
        return "Unknown", "offline"
    try:
        last_seen = datetime.fromisoformat(last_seen_at)
        if last_seen.tzinfo is None:
            last_seen = last_seen.replace(tzinfo=timezone.utc)
        age_seconds = (datetime.now(timezone.utc) - last_seen).total_seconds()
    except ValueError:
        return "Unknown", "offline"
    if age_seconds < 30:
        return "Online", "online"
    if age_seconds < 180:
        return "Stale", "stale"
    return "Offline", "offline"


def latest_activity_at(device: dict, latest_reading: Optional[dict]) -> Optional[str]:
    candidates = [device.get("last_seen_at")]
    if latest_reading:
        candidates.append(latest_reading.get("received_at"))

    latest_value = None
    latest_time = None
    for value in candidates:
        if not value:
            continue
        try:
            parsed = datetime.fromisoformat(value)
            if parsed.tzinfo is None:
                parsed = parsed.replace(tzinfo=timezone.utc)
        except ValueError:
            continue
        if latest_time is None or parsed > latest_time:
            latest_time = parsed
            latest_value = value
    return latest_value


def example_endpoint_path(path: str, devices: list[dict], cameras: list[dict], sensors: list[dict]) -> str:
    if "{device_id}" not in path:
        return path
    if path.startswith("/api/cameras/") and cameras:
        return path.replace("{device_id}", cameras[0]["device_id"])
    if path.startswith("/api/sensors/") and sensors:
        return path.replace("{device_id}", sensors[0]["device_id"])
    if path.startswith("/api/devices/") and devices:
        return path.replace("{device_id}", devices[0]["device_id"])
    return path


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/")
def index() -> Response:
    with db_session() as db:
        device_rows = db.execute("SELECT * FROM devices ORDER BY last_seen_at DESC").fetchall()
        devices = [row_to_device(row) for row in device_rows]
        reading_rows = db.execute(
            """
            SELECT r.*
            FROM sensor_readings r
            INNER JOIN (
                SELECT device_id, MAX(id) AS max_id
                FROM sensor_readings
                GROUP BY device_id
            ) latest ON latest.max_id = r.id
            ORDER BY r.received_at DESC
            """
        ).fetchall()
        sensors = [row_to_reading(row) for row in reading_rows]

    cameras = [
        device
        for device in devices
        if device.get("device_type") == "esp32-s3-camera" or "camera" in device.get("capabilities", [])
    ]
    latest_by_device = {sensor["device_id"]: sensor for sensor in sensors}

    device_cards_html = []
    modal_sections_html = []
    for index, device in enumerate(devices):
        modal_id = f"device-modal-{index}"
        device_id = html_escape(device.get("device_id"))
        raw_device_id = str(device.get("device_id"))
        device_type = html_escape(device.get("device_type"))
        ip = html_escape(device.get("ip") or "-")
        mac = html_escape(device.get("mac") or "-")
        capabilities = ", ".join(device.get("capabilities", [])) or "-"
        latest = latest_by_device.get(raw_device_id)
        activity_at = latest_activity_at(device, latest)
        label, class_name = device_status_label(activity_at)
        is_available = class_name in {"online", "stale"}
        latest_summary = ""
        if latest:
            latest_summary = ", ".join(f"{key}: {value}" for key, value in latest.get("reading", {}).items())

        if is_available:
            card_attrs = f'type="button" onclick="openDeviceModal(\'{modal_id}\')"'
            card_class = "device-card clickable"
        else:
            card_attrs = "type=\"button\" disabled"
            card_class = "device-card disabled"
        device_cards_html.append(
            f"""
            <button class="{card_class}" {card_attrs}>
              <span class="device-top"><strong>{device_id}</strong><span class="pill {class_name}">{label}</span></span>
              <span>{device_type}</span>
              <span>IP: {ip}</span>
              <span class="muted">{html_escape(capabilities)}</span>
              {f'<span class="muted">{html_escape(latest_summary)}</span>' if latest_summary else ''}
            </button>
            """
        )

        action_rows = [
            ("GET", f"/api/devices/{raw_device_id}", "Device registry entry"),
            ("POST", f"/api/devices/{raw_device_id}/heartbeat", "Update last-seen state"),
        ]
        if device.get("device_type") == "esp32-s3-camera" or "camera" in device.get("capabilities", []):
            action_rows.extend(
                [
                    ("GET", f"/api/cameras/{raw_device_id}", "Camera URLs"),
                    ("GET", f"/api/cameras/{raw_device_id}/portal", "Camera settings portal"),
                    ("GET", f"/api/cameras/{raw_device_id}/viewer", "Stream viewer"),
                    ("GET", f"/api/cameras/{raw_device_id}/capture", "Raw JPEG capture"),
                    ("GET", f"/api/cameras/{raw_device_id}/stream", "Raw MJPEG stream"),
                    ("GET", f"/api/cameras/{raw_device_id}/status", "Camera status JSON"),
                    ("GET", f"/api/cameras/{raw_device_id}/control?var=quality&val=10", "Camera control example"),
                ]
            )
        if latest or "sensor" in str(device.get("device_type", "")) or "light" in device.get("capabilities", []):
            action_rows.extend(
                [
                    ("GET", f"/api/sensors/{raw_device_id}/latest", "Latest sensor reading"),
                    ("GET", f"/api/sensors/{raw_device_id}/readings?limit=100", "Sensor reading history"),
                    ("POST", f"/api/sensors/{raw_device_id}/readings", "Post a sensor reading"),
                ]
            )

        action_rows_html = []
        for method, path, description in action_rows:
            path_text = html_escape(path)
            if method == "GET":
                path_html = f'<a href="{path_text}"><code>{path_text}</code></a>'
            else:
                path_html = f"<code>{path_text}</code>"
            action_rows_html.append(
                f"<tr><td><code>{method}</code></td><td>{path_html}</td><td>{html_escape(description)}</td></tr>"
            )

        curl_lines = [f"curl \"$GATEWAY/api/devices/{raw_device_id}\""]
        if device.get("device_type") == "esp32-s3-camera" or "camera" in device.get("capabilities", []):
            curl_lines.extend(
                [
                    f"curl \"$GATEWAY/api/cameras/{raw_device_id}/status\"",
                    f"curl \"$GATEWAY/api/cameras/{raw_device_id}/capture\" --output {raw_device_id}.jpg",
                ]
            )
        if latest or "sensor" in str(device.get("device_type", "")) or "light" in device.get("capabilities", []):
            curl_lines.append(f"curl \"$GATEWAY/api/sensors/{raw_device_id}/latest\"")
        curl_examples = "\n".join(curl_lines)

        modal_sections_html.append(
            f"""
            <section id="{modal_id}" class="modal-panel" hidden>
              <h3>{device_id}</h3>
              <div class="meta-grid">
                <span>Type</span><strong>{device_type}</strong>
                <span>IP</span><strong>{ip}</strong>
                <span>MAC</span><strong>{mac}</strong>
                <span>Status</span><strong>{label}</strong>
                <span>Activity</span><strong>{html_escape(activity_at)}</strong>
                <span>Capabilities</span><strong>{html_escape(capabilities)}</strong>
              </div>
              <h4>Available Calls</h4>
              <div class="table-wrap">
                <table>
                  <tbody>{''.join(action_rows_html)}</tbody>
                </table>
              </div>
              <h4>curl Examples</h4>
              <pre><code>GATEWAY="http://10.42.0.1:8080"
{html_escape(curl_examples)}</code></pre>
              <p class="muted">Change <code>GATEWAY</code> if you access the Jetson through another IP.</p>
            </section>
            """
        )

    sensor_cards_html = []
    for sensor in sensors:
        reading = sensor.get("reading", {})
        fields = ", ".join(f"{key}: {value}" for key, value in reading.items())
        device_id = html_escape(sensor["device_id"])
        sensor_cards_html.append(
            f"""
            <article class="item">
              <h3>{device_id}</h3>
              <p>{html_escape(fields or "No reading fields")}</p>
              <p>Received: {html_escape(sensor.get("received_at"))}</p>
              <div class="links">
                <a href="/api/sensors/{device_id}/latest">Latest</a>
                <a href="/api/sensors/{device_id}/readings">History</a>
              </div>
            </article>
            """
        )

    endpoint_rows = [
        ("GET", "/health", "Gateway health check"),
        ("GET", "/api/devices", "List registered ESP devices"),
        ("GET", "/api/devices/{device_id}", "Get one registered device"),
        ("POST", "/api/devices/register", "Register or update an ESP device"),
        ("POST", "/api/devices/{device_id}/heartbeat", "Update device last-seen state"),
        ("GET", "/api/cameras", "List camera devices"),
        ("GET", "/api/cameras/{device_id}", "Camera URLs and direct ESP URLs"),
        ("GET", "/api/cameras/{device_id}/portal", "Gateway-hosted camera settings portal"),
        ("GET", "/api/cameras/{device_id}/viewer", "Browser MJPEG stream viewer"),
        ("GET", "/api/cameras/{device_id}/capture", "Proxy raw JPEG capture"),
        ("GET", "/api/cameras/{device_id}/stream", "Proxy raw MJPEG stream"),
        ("GET", "/api/cameras/{device_id}/status", "Proxy camera status JSON"),
        ("GET", "/api/cameras/{device_id}/control?var=quality&val=10", "Proxy camera control call"),
        ("GET", "/api/sensors", "Latest reading for each sensor"),
        ("GET", "/api/sensors/{device_id}/latest", "Latest reading for one sensor"),
        ("GET", "/api/sensors/{device_id}/readings?limit=100", "Sensor reading history"),
        ("POST", "/api/sensors/{device_id}/readings", "Ingest a sensor reading"),
        ("DELETE", "/api/sensors/{device_id}/readings", "Clear sensor readings"),
    ]
    endpoint_rows_html_parts = []
    for method, path, description in endpoint_rows:
        example_path = example_endpoint_path(path, devices, cameras, sensors)
        if method == "GET":
            path_html = f'<a href="{html_escape(example_path)}"><code>{html_escape(path)}</code></a>'
        else:
            path_html = f"<code>{html_escape(path)}</code>"
        endpoint_rows_html_parts.append(
            f"<tr><td><code>{method}</code></td><td>{path_html}</td><td>{html_escape(description)}</td></tr>"
        )
    endpoint_rows_html = "\n".join(endpoint_rows_html_parts)

    html_body = f"""<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Jetson Device Gateway</title>
<style>
body{{margin:0;background:#f6f7f9;color:#172033;font-family:Arial,Helvetica,sans-serif;}}
header{{background:#18212f;color:white;padding:16px 22px;}}
header h1{{margin:0 0 4px;font-size:24px;}}
header p{{margin:0;color:#cbd5e1;}}
main{{display:grid;gap:14px;padding:18px;max-width:1240px;margin:0 auto;}}
section{{background:white;border:1px solid #dfe3ea;border-radius:8px;padding:14px;box-shadow:0 1px 3px rgba(10,20,30,.06);}}
h2{{margin:0 0 12px;font-size:18px;}}
.summary{{display:grid;grid-template-columns:repeat(4,minmax(120px,1fr));gap:12px;}}
.stat{{background:white;border:1px solid #dfe3ea;border-radius:8px;padding:14px;}}
.stat strong{{display:block;font-size:28px;}}
.grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px;}}
.item{{border:1px solid #e5e9f0;border-radius:7px;padding:10px;background:#fafbfc;}}
.item h3{{margin:0 0 6px;font-size:15px;}}
.item p{{margin:4px 0;color:#526071;}}
.device-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:10px;}}
.device-card{{border:1px solid #e0e6ef;border-radius:8px;background:#fff;padding:12px;display:grid;gap:5px;text-align:left;color:#172033;font:inherit;min-height:118px;}}
.device-card.clickable{{cursor:pointer;box-shadow:0 1px 2px rgba(10,20,30,.05);}}
.device-card.clickable:hover{{border-color:#8fb6ff;background:#f7fbff;}}
.device-card.disabled{{opacity:.62;cursor:not-allowed;background:#f4f5f7;}}
.device-card span{{display:block;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}}
.device-top{{display:flex!important;align-items:center;justify-content:space-between;gap:8px;}}
.muted{{color:#667385;}}
.links{{display:flex;gap:8px;flex-wrap:wrap;margin-top:8px;}}
a{{color:#1d5fd1;text-decoration:none;}}
.links a{{background:#eef4ff;border:1px solid #c9dcff;border-radius:5px;padding:5px 8px;}}
table{{width:100%;border-collapse:collapse;font-size:14px;}}
th,td{{border-bottom:1px solid #e5e9f0;padding:8px;text-align:left;vertical-align:top;}}
th{{color:#526071;font-size:12px;text-transform:uppercase;}}
td span{{color:#667385;font-size:12px;}}
code{{font-family:Menlo,Consolas,monospace;font-size:13px;}}
pre{{background:#101827;color:#dbeafe;border-radius:7px;padding:10px;overflow:auto;}}
details summary{{cursor:pointer;font-weight:700;}}
.pill{{display:inline-block;border-radius:999px;padding:3px 8px;font-weight:700;font-size:12px;}}
.online{{background:#dff7e8;color:#17643a;}}
.stale{{background:#fff1d6;color:#8a5700;}}
.offline{{background:#ffe2e2;color:#9b1c1c;}}
.table-wrap{{overflow-x:auto;}}
.modal{{position:fixed;inset:0;background:rgba(15,23,42,.55);display:none;align-items:center;justify-content:center;padding:18px;z-index:50;}}
.modal.open{{display:flex;}}
.modal-card{{background:white;border-radius:9px;box-shadow:0 24px 64px rgba(0,0,0,.28);width:min(860px,100%);max-height:88vh;overflow:auto;}}
.modal-head{{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:12px 14px;border-bottom:1px solid #e5e9f0;}}
.modal-body{{padding:14px;}}
.modal button{{border:1px solid #cfd8e6;background:#f7f9fc;border-radius:5px;padding:6px 10px;cursor:pointer;}}
.modal-panel{{display:block;background:transparent;border:0;box-shadow:none;padding:0;}}
.modal-panel h3{{margin:0 0 10px;font-size:20px;}}
.modal-panel h4{{margin:16px 0 8px;}}
.meta-grid{{display:grid;grid-template-columns:110px minmax(0,1fr);gap:7px 10px;font-size:14px;}}
.meta-grid span{{color:#667385;}}
.meta-grid strong{{min-width:0;overflow-wrap:anywhere;}}
@media(max-width:760px){{.summary{{grid-template-columns:repeat(2,minmax(0,1fr));}}}}
</style>
</head>
<body>
<header>
  <h1>Jetson Device Gateway</h1>
  <p>ESP device registry, sensor readings, and S3 camera proxy endpoints.</p>
</header>
<main>
  <div class="summary">
    <div class="stat"><strong>{len(devices)}</strong>devices</div>
    <div class="stat"><strong>{len(cameras)}</strong>cameras</div>
    <div class="stat"><strong>{len(sensors)}</strong>sensor feeds</div>
    <div class="stat"><strong><a href="/health">ok</a></strong>health</div>
  </div>

  <section>
    <h2>Devices</h2>
    <div class="device-grid">{''.join(device_cards_html) or '<p>No devices registered yet.</p>'}</div>
  </section>

  <section>
    <details>
      <summary>Latest sensor readings</summary>
      <div class="grid" style="margin-top:12px">{''.join(sensor_cards_html) or '<p>No sensor readings yet.</p>'}</div>
    </details>
  </section>

  <section>
    <details>
      <summary>API endpoint reference</summary>
      <div class="table-wrap" style="margin-top:12px">
        <table>
          <thead><tr><th>Method</th><th>Path</th><th>Purpose</th></tr></thead>
          <tbody>{endpoint_rows_html}</tbody>
        </table>
      </div>
    </details>
  </section>
</main>
<div id="device-modal" class="modal" onclick="closeDeviceModal(event)">
  <div class="modal-card" role="dialog" aria-modal="true" aria-labelledby="modal-title" onclick="event.stopPropagation()">
    <div class="modal-head">
      <strong id="modal-title">Device API</strong>
      <button type="button" onclick="closeDeviceModal()">Close</button>
    </div>
    <div id="modal-body" class="modal-body"></div>
  </div>
</div>
<div id="modal-source" hidden>{''.join(modal_sections_html)}</div>
<script>
const modal = document.getElementById('device-modal');
const modalBody = document.getElementById('modal-body');
function openDeviceModal(id) {{
  const source = document.getElementById(id);
  if (!source) return;
  modalBody.innerHTML = source.innerHTML;
  modal.classList.add('open');
}}
function closeDeviceModal(event) {{
  if (event && event.target !== modal) return;
  modal.classList.remove('open');
  modalBody.innerHTML = '';
}}
document.addEventListener('keydown', event => {{
  if (event.key === 'Escape') closeDeviceModal();
}});
</script>
</body>
</html>"""
    return Response(content=html_body, media_type="text/html")


@app.post("/api/devices/register", response_model=DeviceRead, status_code=status.HTTP_201_CREATED)
def register_device(body: DeviceRegister, source_ip: ClientIp) -> dict:
    now = utc_now_iso()
    ip = body.ip or source_ip
    capabilities_json = json.dumps(body.capabilities)
    metadata_json = json.dumps(body.metadata)

    with db_session() as db:
        existing = db.execute(
            "SELECT created_at FROM devices WHERE device_id = ?",
            (body.device_id,),
        ).fetchone()
        created_at = existing["created_at"] if existing else now

        db.execute(
            """
            INSERT INTO devices (
                device_id,
                device_type,
                ip,
                mac,
                firmware,
                capabilities_json,
                metadata_json,
                created_at,
                updated_at,
                last_seen_at
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(device_id) DO UPDATE SET
                device_type = excluded.device_type,
                ip = excluded.ip,
                mac = excluded.mac,
                firmware = excluded.firmware,
                capabilities_json = excluded.capabilities_json,
                metadata_json = excluded.metadata_json,
                updated_at = excluded.updated_at,
                last_seen_at = excluded.last_seen_at
            """,
            (
                body.device_id,
                body.device_type,
                ip,
                body.mac,
                body.firmware,
                capabilities_json,
                metadata_json,
                created_at,
                now,
                now,
            ),
        )
        row = db.execute(
            "SELECT * FROM devices WHERE device_id = ?",
            (body.device_id,),
        ).fetchone()

        device = row_to_device(row)

    cloud_client.register_device(
        device["device_id"],
        device["device_type"]
    )

    if cloud_client.is_camera_device(device["device_type"]):
        cloud_client.forward_camera_status(
            device["device_id"],
            device["device_type"],
            {
                "online": True,
                "ip": device.get("ip"),
                "firmware": device.get("firmware"),
            },
        )

    return device


@app.get("/api/devices", response_model=list[DeviceRead])
def list_devices(device_type: Optional[str] = None) -> list[dict]:
    with db_session() as db:
        if device_type:
            rows = db.execute(
                "SELECT * FROM devices WHERE device_type = ? ORDER BY last_seen_at DESC",
                (device_type,),
            ).fetchall()
        else:
            rows = db.execute("SELECT * FROM devices ORDER BY last_seen_at DESC").fetchall()
        return [row_to_device(row) for row in rows]


@app.get("/api/devices/{device_id}", response_model=DeviceRead)
def get_device(device_id: str) -> dict:
    with db_session() as db:
        row = db.execute("SELECT * FROM devices WHERE device_id = ?", (device_id,)).fetchone()
        if row is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Device not found")
        return row_to_device(row)


@app.post("/api/devices/{device_id}/heartbeat", response_model=DeviceRead)
def heartbeat(device_id: str, body: DeviceHeartbeat, source_ip: ClientIp) -> dict:
    now = utc_now_iso()
    with db_session() as db:
        row = db.execute("SELECT * FROM devices WHERE device_id = ?", (device_id,)).fetchone()
        if row is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Device not found")

        current = row_to_device(row)
        metadata = current["metadata"]
        if body.metadata is not None:
            metadata.update(body.metadata)

        db.execute(
            """
            UPDATE devices
            SET ip = ?,
                firmware = COALESCE(?, firmware),
                metadata_json = ?,
                updated_at = ?,
                last_seen_at = ?
            WHERE device_id = ?
            """,
            (
                body.ip or source_ip or current["ip"],
                body.firmware,
                json.dumps(metadata),
                now,
                now,
                device_id,
            ),
        )
        updated = db.execute("SELECT * FROM devices WHERE device_id = ?", (device_id,)).fetchone()
        return row_to_device(updated)


def device_by_id(device_id: str) -> dict:
    with db_session() as db:
        row = db.execute("SELECT * FROM devices WHERE device_id = ?", (device_id,)).fetchone()
        if row is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Device not found")
        return row_to_device(row)


def resolve_camera(device_id: str) -> dict:
    device = device_by_id(device_id)
    capabilities = device.get("capabilities", [])
    if device.get("device_type") != "esp32-s3-camera" and "camera" not in capabilities:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Device is not registered as a camera")
    if not device.get("ip"):
        raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Camera has no registered IP")
    return device


@app.get("/api/cameras")
def list_cameras() -> list[dict]:
    with db_session() as db:
        rows = db.execute("SELECT * FROM devices ORDER BY last_seen_at DESC").fetchall()
        devices = [row_to_device(row) for row in rows]
        return [
            device
            for device in devices
            if device.get("device_type") == "esp32-s3-camera" or "camera" in device.get("capabilities", [])
        ]


@app.get("/api/cameras/{device_id}")
def get_camera(device_id: str) -> dict:
    device = resolve_camera(device_id)
    ip = device["ip"]
    return {
        "device": device,
        "portal_url": f"/api/cameras/{device_id}/portal",
        "capture_url": f"/api/cameras/{device_id}/capture",
        "stream_url": f"/api/cameras/{device_id}/stream",
        "viewer_url": f"/api/cameras/{device_id}/viewer",
        "status_url": f"/api/cameras/{device_id}/status",
        "control_url": f"/api/cameras/{device_id}/control",
        "direct_portal_url": f"http://{ip}/portal",
        "direct_capture_url": f"http://{ip}/capture",
        "direct_stream_url": f"http://{ip}:81/stream",
    }


@app.get("/api/cameras/{device_id}/portal")
def camera_portal(device_id: str) -> Response:
    resolve_camera(device_id)
    html = f"""<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Camera Portal</title>
<style>
html,body{{margin:0;background:#151515;color:#eee;font-family:Arial,Helvetica,sans-serif;font-size:15px;}}
header{{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 12px;background:#1d1d1d;border-bottom:1px solid #303030;}}
main{{display:grid;grid-template-columns:minmax(0,1fr) 340px;gap:12px;padding:12px;}}
section{{min-width:0;}}
img{{display:block;max-width:100%;max-height:calc(100vh - 94px);background:#050505;border:1px solid #303030;}}
.panel{{background:#242424;border:1px solid #333;padding:10px;}}
.row{{display:grid;grid-template-columns:132px minmax(0,1fr);align-items:center;gap:8px;margin:8px 0;}}
.actions{{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px;}}
button,a{{color:#fff;background:#ff3034;border:0;border-radius:5px;padding:7px 12px;text-decoration:none;cursor:pointer;font-size:15px;}}
button:hover,a:hover{{background:#ff494d;}}
input,select{{min-width:0;width:100%;}}
input[type=checkbox]{{width:auto;justify-self:start;}}
#status{{min-height:18px;color:#bbb;font-size:13px;}}
@media(max-width:760px){{main{{grid-template-columns:1fr;}}img{{max-height:none;}}.row{{grid-template-columns:120px minmax(0,1fr);}}}}
</style>
</head>
<body>
<header><strong>ESP32 Camera Portal</strong><a id="raw-stream" href="/api/cameras/{device_id}/viewer">Stream</a></header>
<main>
<section>
<div class="actions"><button id="capture">Refresh Still</button><a id="save" href="/api/cameras/{device_id}/capture" download="capture.jpg">Save Still</a></div>
<img id="still" alt="camera still">
</section>
<aside class="panel">
<div class="row"><label for="framesize">Frame size</label><select id="framesize" data-var="framesize">
<option value="3">QVGA 320x240</option><option value="4">CIF 400x296</option><option value="5">HVGA 480x320</option><option value="6">VGA 640x480</option><option value="7">SVGA 800x600</option><option value="8">XGA 1024x768</option><option value="9">HD 1280x720</option><option value="10">UXGA 1600x1200</option>
</select></div>
<div class="row"><label for="quality">JPEG quality</label><input id="quality" data-var="quality" type="range" min="4" max="63"></div>
<div class="row"><label for="brightness">Brightness</label><input id="brightness" data-var="brightness" type="range" min="-2" max="2"></div>
<div class="row"><label for="contrast">Contrast</label><input id="contrast" data-var="contrast" type="range" min="-2" max="2"></div>
<div class="row"><label for="saturation">Saturation</label><input id="saturation" data-var="saturation" type="range" min="-2" max="2"></div>
<div class="row"><label for="special_effect">Effect</label><select id="special_effect" data-var="special_effect">
<option value="0">None</option><option value="1">Negative</option><option value="2">Grayscale</option><option value="3">Red tint</option><option value="4">Green tint</option><option value="5">Blue tint</option><option value="6">Sepia</option>
</select></div>
<div class="row"><label for="wb_mode">White balance</label><select id="wb_mode" data-var="wb_mode">
<option value="0">Auto</option><option value="1">Sunny</option><option value="2">Cloudy</option><option value="3">Office</option><option value="4">Home</option>
</select></div>
<div class="row"><label for="awb">AWB</label><input id="awb" data-var="awb" type="checkbox"></div>
<div class="row"><label for="awb_gain">AWB gain</label><input id="awb_gain" data-var="awb_gain" type="checkbox"></div>
<div class="row"><label for="aec">Exposure</label><input id="aec" data-var="aec" type="checkbox"></div>
<div class="row"><label for="aec2">DSP exposure</label><input id="aec2" data-var="aec2" type="checkbox"></div>
<div class="row"><label for="ae_level">AE level</label><input id="ae_level" data-var="ae_level" type="range" min="-2" max="2"></div>
<div class="row"><label for="agc">Gain control</label><input id="agc" data-var="agc" type="checkbox"></div>
<div class="row"><label for="gainceiling">Gain ceiling</label><input id="gainceiling" data-var="gainceiling" type="range" min="0" max="6"></div>
<div class="row"><label for="hmirror">Mirror</label><input id="hmirror" data-var="hmirror" type="checkbox"></div>
<div class="row"><label for="vflip">Flip</label><input id="vflip" data-var="vflip" type="checkbox"></div>
<div class="row"><label for="dcw">Downsize</label><input id="dcw" data-var="dcw" type="checkbox"></div>
<div class="row"><label for="colorbar">Color bar</label><input id="colorbar" data-var="colorbar" type="checkbox"></div>
<div class="row"><label for="led_intensity">LED</label><input id="led_intensity" data-var="led_intensity" type="range" min="0" max="255"></div>
<div id="status"></div>
</aside>
</main>
<script>
const base='/api/cameras/{device_id}';
const still=document.getElementById('still');
const save=document.getElementById('save');
const statusEl=document.getElementById('status');
function setStatus(text){{statusEl.textContent=text;}}
function refreshStill(){{const url=base+'/capture?_cb='+Date.now();still.src=url;save.href=url;}}
function setControl(el){{
  const value=el.type==='checkbox'?(el.checked?1:0):el.value;
  fetch(base+'/control?var='+encodeURIComponent(el.dataset.var)+'&val='+encodeURIComponent(value))
    .then(r=>{{if(!r.ok)throw new Error(r.status);setStatus('Updated '+el.dataset.var);}})
    .catch(()=>setStatus('Update failed for '+el.dataset.var));
}}
fetch(base+'/status').then(r=>r.json()).then(data=>{{
  document.querySelectorAll('[data-var]').forEach(el=>{{
    if(data[el.dataset.var]===undefined)return;
    if(el.type==='checkbox')el.checked=!!data[el.dataset.var];else el.value=data[el.dataset.var];
  }});
  setStatus('Ready');
}}).catch(()=>setStatus('Status unavailable'));
document.querySelectorAll('[data-var]').forEach(el=>el.addEventListener('change',()=>setControl(el)));
document.getElementById('capture').addEventListener('click',refreshStill);
refreshStill();
</script>
</body>
</html>"""
    return Response(content=html, media_type="text/html")


@app.get("/api/cameras/{device_id}/viewer")
def camera_viewer(device_id: str) -> Response:
    resolve_camera(device_id)
    html = f"""<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{device_id} Stream</title>
<style>
html,body{{margin:0;background:#111827;color:#e5e7eb;font-family:Arial,Helvetica,sans-serif;}}
header{{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 12px;background:#1f2937;border-bottom:1px solid #374151;}}
a{{color:#fff;background:#2563eb;border-radius:5px;padding:7px 12px;text-decoration:none;}}
main{{min-height:calc(100vh - 48px);display:flex;align-items:center;justify-content:center;padding:12px;}}
img{{display:block;max-width:100%;max-height:calc(100vh - 72px);background:#050505;border:1px solid #374151;}}
</style>
</head>
<body>
<header><strong>{device_id}</strong><a href="/api/cameras/{device_id}/portal">Portal</a></header>
<main><img src="/api/cameras/{device_id}/stream" alt="{device_id} live stream"></main>
</body>
</html>"""
    return Response(content=html, media_type="text/html")


@app.get("/api/cameras/{device_id}/status")
async def camera_status(device_id: str) -> Response:
    device = resolve_camera(device_id)
    url = f"http://{device['ip']}/status"

    try:
        async with httpx.AsyncClient(timeout=CAMERA_REQUEST_TIMEOUT) as client:
            response = await client.get(url)
    except httpx.RequestError as exc:
        raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=f"Camera unavailable: {exc}") from exc

    if response.status_code >= 400:
        raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=f"Camera error: {response.status_code}")
    return Response(content=response.content, media_type=response.headers.get("content-type", "application/json"))


@app.get("/api/cameras/{device_id}/control")
async def camera_control(device_id: str, request: Request) -> Response:
    device = resolve_camera(device_id)
    url = f"http://{device['ip']}/control"
    if request.url.query:
        url = f"{url}?{request.url.query}"

    try:
        async with httpx.AsyncClient(timeout=CAMERA_REQUEST_TIMEOUT) as client:
            response = await client.get(url)
    except httpx.RequestError as exc:
        raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=f"Camera unavailable: {exc}") from exc

    if response.status_code >= 400:
        raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=f"Camera error: {response.status_code}")
    return Response(content=response.content, media_type=response.headers.get("content-type", "text/plain"))


@app.get("/api/cameras/{device_id}/capture")
async def camera_capture(device_id: str, request: Request) -> Response:
    device = resolve_camera(device_id)
    url = f"http://{device['ip']}/capture"
    if request.url.query:
        url = f"{url}?{request.url.query}"

    try:
        async with httpx.AsyncClient(timeout=CAMERA_REQUEST_TIMEOUT) as client:
            response = await client.get(url)
    except httpx.RequestError as exc:
        raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=f"Camera unavailable: {exc}") from exc

    if response.status_code >= 400:
        raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=f"Camera error: {response.status_code}")
    return Response(content=response.content, media_type=response.headers.get("content-type", "image/jpeg"))


@app.get("/api/cameras/{device_id}/stream")
async def camera_stream(device_id: str, request: Request) -> StreamingResponse:
    device = resolve_camera(device_id)
    url = f"http://{device['ip']}:81/stream"
    if request.url.query:
        url = f"{url}?{request.url.query}"

    async def iter_stream():
        async with httpx.AsyncClient(timeout=None) as client:
            try:
                async with client.stream("GET", url) as response:
                    if response.status_code >= 400:
                        return
                    async for chunk in response.aiter_bytes():
                        if chunk:
                            yield chunk
            except httpx.RequestError:
                return

    return StreamingResponse(
        iter_stream(),
        media_type="multipart/x-mixed-replace; boundary=123456789000000000000987654321",
    )


@app.post("/api/sensors/{device_id}/readings", response_model=SensorReadingOut, status_code=status.HTTP_201_CREATED)
def ingest_sensor_reading(device_id: str, body: SensorReadingIn, source_ip: ClientIp) -> dict:
    now = utc_now_iso()
    with db_session() as db:
        cursor = db.execute(
            "INSERT INTO sensor_readings (device_id, reading_json, received_at) VALUES (?, ?, ?)",
            (device_id, json.dumps(body.reading), now),
        )
        db.execute(
            """
            UPDATE devices
            SET ip = COALESCE(?, ip),
                updated_at = ?,
                last_seen_at = ?
            WHERE device_id = ?
            """,
            (source_ip, now, now, device_id),
        )
        row = db.execute("SELECT * FROM sensor_readings WHERE id = ?", (cursor.lastrowid,)).fetchone()
        reading = row_to_reading(row)

    cloud_client.forward_sensor_reading(device_id, body.reading)
    return reading


@app.get("/api/sensors", response_model=list[SensorReadingOut])
def list_sensors() -> list[dict]:
    with db_session() as db:
        rows = db.execute(
            """
            SELECT r.*
            FROM sensor_readings r
            INNER JOIN (
                SELECT device_id, MAX(id) AS max_id
                FROM sensor_readings
                GROUP BY device_id
            ) latest ON latest.max_id = r.id
            ORDER BY r.received_at DESC
            """
        ).fetchall()
        return [row_to_reading(row) for row in rows]


@app.get("/api/sensors/{device_id}/latest", response_model=SensorReadingOut)
def latest_sensor_reading(device_id: str) -> dict:
    with db_session() as db:
        row = db.execute(
            "SELECT * FROM sensor_readings WHERE device_id = ? ORDER BY id DESC LIMIT 1",
            (device_id,),
        ).fetchone()
        if row is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="No readings for device")
        return row_to_reading(row)


@app.get("/api/sensors/{device_id}/readings", response_model=list[SensorReadingOut])
def list_sensor_readings(device_id: str, limit: int = Query(default=100, ge=1, le=1000)) -> list[dict]:
    with db_session() as db:
        rows = db.execute(
            "SELECT * FROM sensor_readings WHERE device_id = ? ORDER BY id DESC LIMIT ?",
            (device_id, limit),
        ).fetchall()
        return [row_to_reading(row) for row in rows]
        

@app.post("/api/sensors/{device_id}/anomalies")
def add_anomaly(device_id: str, payload: dict):
    print("ANOMALY RECEIVED:", device_id, payload)
    return {"status": "ok"}


@app.delete("/api/sensors/{device_id}/readings", status_code=status.HTTP_204_NO_CONTENT)
def clear_sensor_readings(device_id: str) -> None:
    with db_session() as db:
        db.execute("DELETE FROM sensor_readings WHERE device_id = ?", (device_id,))
