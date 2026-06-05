import logging
import os

import httpx

logger = logging.getLogger(__name__)

ALERT_TYPES = {"motion_detected", "light_anomaly", "weak_signal", "wifi_reconnected"}
CAMERA_TYPES = {"esp32-s3-camera", "camera"}


def normalize_sensor_reading(reading: dict) -> dict:
    normalized = dict(reading)
    if "light_raw" not in normalized and "light" in normalized:
        normalized["light_raw"] = normalized["light"]
    if "light" not in normalized and "light_raw" in normalized:
        normalized["light"] = normalized["light_raw"]
    return normalized


def is_camera_device(device_type: str) -> bool:
    lowered = device_type.lower()
    return lowered in CAMERA_TYPES or "camera" in lowered


def _base_url() -> str:
    return os.getenv("CLOUD_BACKEND_URL", "").strip().rstrip("/")


def _headers() -> dict[str, str]:
    headers = {"Content-Type": "application/json"}
    api_key = os.getenv("CLOUD_API_KEY", "").strip()
    if api_key:
        headers["X-API-Key"] = api_key
    return headers


def enabled() -> bool:
    return bool(_base_url())


def register_device(device_id: str, device_type: str, display_name: str | None = None) -> None:
    if not enabled():
        return

    payload = {
        "device_id": device_id,
        "device_type": device_type,
        "display_name": display_name or device_id,
    }
    try:
        response = httpx.post(
            f"{_base_url()}/v1/devices",
            json=payload,
            headers=_headers(),
            timeout=5.0,
        )
        if response.status_code not in (200, 201):
            logger.warning("cloud register failed: %s %s", response.status_code, response.text)
    except httpx.HTTPError:
        logger.exception("cloud register request failed for %s", device_id)


def post_event(
    device_id: str,
    event_type: str,
    payload: dict,
    summary: str | None = None,
) -> None:
    if not enabled():
        return

    body = {
        "device_id": device_id,
        "event_type": event_type,
        "source": "jetson",
        "summary": summary,
        "payload": payload,
    }
    try:
        response = httpx.post(
            f"{_base_url()}/v1/events",
            json=body,
            headers=_headers(),
            timeout=5.0,
        )
        if response.status_code not in (200, 201):
            logger.warning("cloud event failed: %s %s", response.status_code, response.text)
    except httpx.HTTPError:
        logger.exception("cloud event request failed for %s", device_id)


def forward_sensor_reading(device_id: str, reading: dict) -> None:
    if not enabled():
        return

    reading = normalize_sensor_reading(reading)
    register_device(device_id, "sensor")

    event_kind = reading.get("type")
    if event_kind in ALERT_TYPES:
        post_event(
            device_id,
            "alert",
            reading,
            summary=str(event_kind).replace("_", " "),
        )
        return

    post_event(device_id, "sensor", reading, summary="sensor reading")


def forward_camera_status(device_id: str, device_type: str, payload: dict) -> None:
    if not enabled():
        return

    register_device(device_id, device_type)
    post_event(device_id, "camera_status", payload, summary="camera status")
