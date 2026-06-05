import os
from datetime import datetime
from typing import Annotated

from fastapi import APIRouter, Depends, Header, HTTPException, Query, status
from sqlalchemy.orm import Session

from app.database import get_db
from app.models import Device, Event, utc_now
from app.schemas import (
    CameraLatestRead,
    DeviceCreate,
    DeviceRead,
    EventCreate,
    EventRead,
    EventType,
    SensorLatestRead,
)

router = APIRouter(prefix="/v1", tags=["v1"])


def check_api_key(x_api_key: Annotated[str | None, Header()] = None) -> None:
    expected = os.getenv("API_KEY", "").strip()
    if not expected:
        return
    if not x_api_key or x_api_key.strip() != expected:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Missing or wrong X-API-Key header.",
        )


DbSession = Annotated[Session, Depends(get_db)]
WriteAuth = Annotated[None, Depends(check_api_key)]


@router.get("/health")
def health_check():
    return {"status": "ok"}


@router.get("/devices", response_model=list[DeviceRead])
def list_devices(db: DbSession):
    devices = db.query(Device).order_by(Device.created_at.desc()).all()
    return [DeviceRead.model_validate(d) for d in devices]


@router.post("/devices", response_model=DeviceRead, status_code=status.HTTP_201_CREATED)
def register_device(body: DeviceCreate, db: DbSession, _: WriteAuth):
    # update if device_id already exists
    device = db.query(Device).filter(Device.device_id == body.device_id).first()
    if device:
        device.display_name = body.display_name
        device.device_type = body.device_type
    else:
        device = Device(
            device_id=body.device_id,
            display_name=body.display_name,
            device_type=body.device_type,
        )
        db.add(device)

    db.commit()
    db.refresh(device)
    return DeviceRead.model_validate(device)


@router.get("/events", response_model=list[EventRead])
def list_events(
    db: DbSession,
    device_id: str | None = Query(default=None),
    event_type: EventType | None = Query(default=None),
    since: datetime | None = Query(default=None, description="only events after this time"),
    limit: int = Query(default=100, ge=1, le=500),
):
    query = db.query(Event).order_by(Event.received_at.desc())

    if device_id:
        query = query.filter(Event.device_id == device_id)
    if event_type:
        query = query.filter(Event.event_type == event_type.value)
    if since:
        query = query.filter(Event.received_at >= since)

    events = query.limit(limit).all()
    return [EventRead.model_validate(e) for e in events]


def _latest_events_by_device(db: Session, event_types: list[str]) -> list[Event]:
    events = (
        db.query(Event)
        .filter(Event.event_type.in_(event_types))
        .order_by(Event.received_at.desc())
        .all()
    )
    seen: set[str] = set()
    latest: list[Event] = []
    for event in events:
        if event.device_id in seen:
            continue
        seen.add(event.device_id)
        latest.append(event)
    return latest


@router.get("/sensors/latest", response_model=list[SensorLatestRead])
def latest_sensor_readings(db: DbSession):
    rows = _latest_events_by_device(db, [EventType.SENSOR.value, EventType.ALERT.value])
    return [
        SensorLatestRead(
            device_id=event.device_id,
            reading=event.payload or {},
            received_at=event.received_at,
        )
        for event in rows
    ]


@router.get("/cameras/latest", response_model=list[CameraLatestRead])
def latest_camera_status(db: DbSession):
    rows = _latest_events_by_device(db, [EventType.CAMERA_STATUS.value])
    return [
        CameraLatestRead(
            device_id=event.device_id,
            status=event.payload or {},
            received_at=event.received_at,
        )
        for event in rows
    ]


@router.post("/events", response_model=EventRead, status_code=status.HTTP_201_CREATED)
def ingest_event(body: EventCreate, db: DbSession, _: WriteAuth):
    device = db.query(Device).filter(Device.device_id == body.device_id).first()
    if device is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Unknown device_id={body.device_id!r}. Register it with POST /v1/devices first.",
        )

    now = utc_now()
    event = Event(
        device_row_id=device.id,
        device_id=body.device_id,
        event_type=body.event_type.value,
        source=body.source.value,
        occurred_at=body.occurred_at,
        received_at=now,
        summary=body.summary,
        payload=body.payload,
    )
    device.last_seen_at = now

    db.add(event)
    db.commit()
    db.refresh(event)
    return EventRead.model_validate(event)
