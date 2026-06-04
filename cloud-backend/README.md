# Cloud Backend

FastAPI backend for device and event storage.

## Run

From `cloud-backend/`:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
uvicorn app.main:app --reload
```

Server URL:

```text
http://127.0.0.1:8000
```

Docs:

```text
http://127.0.0.1:8000/docs
```

## Main API Routes

```text
GET  /v1/health
GET  /v1/devices
POST /v1/devices
GET  /v1/events
POST /v1/events
```

Register a device before posting events for that `device_id`.
