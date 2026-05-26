import json
import os
from typing import Annotated, Optional

import httpx
from fastapi import Depends, FastAPI, HTTPException, Query, Request, status
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import Response, StreamingResponse

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


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


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
        row = db.execute("SELECT * FROM devices WHERE device_id = ?", (body.device_id,)).fetchone()
        return row_to_device(row)


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
def ingest_sensor_reading(device_id: str, body: SensorReadingIn) -> dict:
    now = utc_now_iso()
    with db_session() as db:
        cursor = db.execute(
            "INSERT INTO sensor_readings (device_id, reading_json, received_at) VALUES (?, ?, ?)",
            (device_id, json.dumps(body.reading), now),
        )
        row = db.execute("SELECT * FROM sensor_readings WHERE id = ?", (cursor.lastrowid,)).fetchone()
        return row_to_reading(row)


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


@app.delete("/api/sensors/{device_id}/readings", status_code=status.HTTP_204_NO_CONTENT)
def clear_sensor_readings(device_id: str) -> None:
    with db_session() as db:
        db.execute("DELETE FROM sensor_readings WHERE device_id = ?", (device_id,))
