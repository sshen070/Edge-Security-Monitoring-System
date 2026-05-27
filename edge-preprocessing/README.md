# edge-preprocessing

Lightweight edge preprocessing service for ingesting JSON messages from microcontrollers.

Features:
- HTTP endpoint: POST /ingest
- UDP listener: listens on port 9999 for JSON payloads (async)
- Schema validation against shared/schemas using jsonschema
- Normalizes minimal fields and writes processed JSON to `out/`

Run locally (recommended inside a virtualenv):

1. Install dependencies:

```powershell
python -m pip install -r requirements.txt
```

2. Start the FastAPI app (development):

```powershell
uvicorn edge_preprocessing.app.main:app --reload
```

3. The UDP listener can be started by importing and running the `udp_listener` coroutine from `edge_preprocessing.app.main`.
