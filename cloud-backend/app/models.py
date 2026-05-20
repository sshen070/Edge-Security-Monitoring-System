import uuid
from datetime import datetime, timezone

from sqlalchemy import JSON, Column, DateTime, ForeignKey, String, Text

from app.database import Base


def utc_now():
    return datetime.now(timezone.utc)


class Device(Base):
    __tablename__ = "devices"

    id = Column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    device_id = Column(String(128), unique=True, index=True)
    display_name = Column(String(256), nullable=True)
    device_type = Column(String(64))
    created_at = Column(DateTime(timezone=True), default=utc_now)
    last_seen_at = Column(DateTime(timezone=True), nullable=True)


class Event(Base):
    __tablename__ = "events"

    id = Column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    device_row_id = Column(String(36), ForeignKey("devices.id"), index=True)
    device_id = Column(String(128), index=True)
    event_type = Column(String(64), index=True)
    source = Column(String(32))
    occurred_at = Column(DateTime(timezone=True), nullable=True)
    received_at = Column(DateTime(timezone=True), default=utc_now)
    summary = Column(Text, nullable=True)
    payload = Column(JSON, default=dict)
