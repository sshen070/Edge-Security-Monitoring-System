# Cloud Dashboard

Static browser dashboard for the Jetson device gateway.

## Run

Open `index.html` in a browser.

From the Jetson itself, use:

```text
http://127.0.0.1:8080
```

From a Mac or another machine on the same upstream network, use the Jetson's
network IP:

```text
http://<jetson-ucr-ip>:8080
```

You can also pass the API URL directly:

```text
index.html?api=http://<jetson-ucr-ip>:8080
```

## Live Gateway Data

The dashboard reads:

```text
GET /api/devices
GET /api/sensors
GET /api/sensors/<device_id>/readings
GET /api/cameras
GET /api/cameras/<device_id>/stream
GET /api/cameras/<device_id>/capture
```

The Jetson gateway has CORS enabled, so this static page can call it directly.
