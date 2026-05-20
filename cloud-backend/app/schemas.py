from datetime import datetime
from enum import Enum
from typing import Any
from uuid import UUID

from pydantic import BaseModel, ConfigDict, Field


class DeviceCreate(BaseModel):
    device_id: str
    display_name: str | None = None
    device_type: str


class DeviceRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: UUID
    device_id: str
    display_name: str | None
    device_type: str
    created_at: datetime
    last_seen_at: datetime | None = None


class EventType(str, Enum):
    SENSOR = "sensor"
    CAMERA_STATUS = "camera_status"
    ALERT = "alert"
    HEARTBEAT = "heartbeat"


class EventSource(str, Enum):
    JETSON = "jetson"
    ESP = "esp"


class EventCreate(BaseModel):
    device_id: str
    event_type: EventType
    source: EventSource = EventSource.JETSON
    occurred_at: datetime | None = None
    summary: str | None = None
    payload: dict[str, Any] = Field(default_factory=dict)


class EventRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: UUID
    device_id: str
    event_type: EventType
    source: EventSource
    occurred_at: datetime | None
    received_at: datetime
    summary: str | None
    payload: dict[str, Any]
