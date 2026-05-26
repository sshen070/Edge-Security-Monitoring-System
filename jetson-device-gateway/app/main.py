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
        "capture_url": f"/api/cameras/{device_id}/capture",
        "stream_url": f"/api/cameras/{device_id}/stream",
        "direct_capture_url": f"http://{ip}/capture",
        "direct_stream_url": f"http://{ip}:81/stream",
    }


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
