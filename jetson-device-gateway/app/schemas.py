from typing import Optional

from pydantic import BaseModel, Field


class DeviceRegister(BaseModel):
    device_id: str = Field(min_length=1, max_length=128)
    device_type: str = Field(min_length=1, max_length=64)
    ip: Optional[str] = Field(default=None, max_length=64)
    mac: Optional[str] = Field(default=None, max_length=32)
    firmware: Optional[str] = Field(default=None, max_length=128)
    capabilities: list[str] = Field(default_factory=list)
    metadata: dict = Field(default_factory=dict)


class DeviceHeartbeat(BaseModel):
    ip: Optional[str] = Field(default=None, max_length=64)
    firmware: Optional[str] = Field(default=None, max_length=128)
    metadata: Optional[dict] = None


class DeviceRead(BaseModel):
    device_id: str
    device_type: str
    ip: Optional[str] = None
    mac: Optional[str] = None
    firmware: Optional[str] = None
    capabilities: list[str]
    metadata: dict
    created_at: str
    updated_at: str
    last_seen_at: str


class SensorReadingIn(BaseModel):
    reading: dict = Field(default_factory=dict)


class SensorReadingOut(BaseModel):
    id: int
    device_id: str
    reading: dict
    received_at: str
