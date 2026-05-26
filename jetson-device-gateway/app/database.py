import json
import os
import sqlite3
from collections.abc import Iterator
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_DB_PATH = Path(__file__).resolve().parent.parent / "data" / "devices.db"
DB_PATH = Path(os.getenv("DEVICE_REGISTRY_DB", str(DEFAULT_DB_PATH)))


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def connect() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    return conn


@contextmanager
def db_session() -> Iterator[sqlite3.Connection]:
    conn = connect()
    try:
        yield conn
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


def init_db() -> None:
    with db_session() as db:
        db.execute(
            """
            CREATE TABLE IF NOT EXISTS devices (
                device_id TEXT PRIMARY KEY,
                device_type TEXT NOT NULL,
                ip TEXT,
                mac TEXT,
                firmware TEXT,
                capabilities_json TEXT NOT NULL DEFAULT '[]',
                metadata_json TEXT NOT NULL DEFAULT '{}',
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL,
                last_seen_at TEXT NOT NULL
            )
            """
        )
        db.execute("CREATE INDEX IF NOT EXISTS idx_devices_type ON devices(device_type)")
        db.execute("CREATE INDEX IF NOT EXISTS idx_devices_mac ON devices(mac)")
        db.execute("CREATE INDEX IF NOT EXISTS idx_devices_last_seen ON devices(last_seen_at)")
        db.execute(
            """
            CREATE TABLE IF NOT EXISTS sensor_readings (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                device_id TEXT NOT NULL,
                reading_json TEXT NOT NULL,
                received_at TEXT NOT NULL
            )
            """
        )
        db.execute("CREATE INDEX IF NOT EXISTS idx_sensor_readings_device_time ON sensor_readings(device_id, received_at)")


def row_to_device(row: sqlite3.Row) -> dict[str, Any]:
    device = dict(row)
    device["capabilities"] = json.loads(device.pop("capabilities_json") or "[]")
    device["metadata"] = json.loads(device.pop("metadata_json") or "{}")
    return device


def row_to_reading(row: sqlite3.Row) -> dict[str, Any]:
    return {
        "id": row["id"],
        "device_id": row["device_id"],
        "reading": json.loads(row["reading_json"]),
        "received_at": row["received_at"],
    }
