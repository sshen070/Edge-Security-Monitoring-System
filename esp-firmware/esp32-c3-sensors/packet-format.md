# ESP32-C3 Device Registration Payload

The Jetson Orin Nano registry expects ESP32-C3 nodes to register over HTTP after joining `ESP-NET`.

Endpoint:

```text
POST http://10.42.0.1:8080/api/devices/register
```

Example sensor node payload:

```json
{
  "device_id": "sensor-node-01",
  "device_type": "esp32-c3-sensor",
  "ip": "10.42.0.34",
  "mac": "AA:BB:CC:DD:EE:11",
  "firmware": "jetson_sensor_node_demo",
  "capabilities": ["pir", "light", "i2c-scan"],
  "metadata": {
    "light_pin": "A0",
    "pir_pin": "D0",
    "i2c_sda": "D4",
    "i2c_scl": "D5"
  }
}
```

Required fields:

- `device_id`
- `device_type`

Recommended fields:

- `ip`
- `mac`
- `firmware`
- `capabilities`
- `metadata`

Device IDs should be stable across reboots.
