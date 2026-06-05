import json
import logging
import os
from pathlib import Path

import paho.mqtt.client as mqtt
import requests

from detector.anomaly_detector import AnomalyDetector

# Logging Setup
LOG_DIR = Path("logs")
LOG_DIR.mkdir(exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.FileHandler(LOG_DIR / "sensor_events.log"),
        logging.StreamHandler()
    ]
)

# Load thresholds from config file
with open("config/thresholds.json", "r") as f:
    config = json.load(f)


# Initialize detector
detector = AnomalyDetector(config)

MQTT_BROKER = "mosquitto"
MQTT_PORT = 1883
MQTT_TOPIC = "sensors/data"
GATEWAY_URL = os.getenv("GATEWAY_URL", "")
GATEWAY_API_KEY = os.getenv("GATEWAY_API_KEY", "").strip()

def on_connect(client, userdata, flags, rc):
    logging.info(f"Connected to MQTT broker: {rc}")
    client.subscribe(MQTT_TOPIC)
    logging.info(f"Subscribed to topic: {MQTT_TOPIC}")

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8").strip()

        try:
            data = json.loads(payload)
        except json.JSONDecodeError:
            logging.error(f"Bad MQTT payload: {payload}")
            return

        logging.debug(f"Received sensor data: {data}")

        events = detector.detect(data)

        device_id = data.get("device_id") or data.get("id") or "unknown-device"

        # Forward anomaly events
        if events:
            logging.warning("ANOMALY EVENTS DETECTED")

            for event in events:
                logging.warning("EVENT %s", json.dumps(event))

                try:
                    if GATEWAY_URL:
                        post_url = (
                            f"{GATEWAY_URL.rstrip('/')}"
                            f"/api/sensors/{requests.utils.requote_uri(device_id)}/anomalies"
                        )

                        headers = {}
                        if GATEWAY_API_KEY:
                            headers["X-API-Key"] = GATEWAY_API_KEY

                        resp = requests.post(
                            post_url,
                            json={"event": event},
                            headers=headers,
                            timeout=5
                        )

                        if not resp.ok:
                            logging.warning(
                                f"Gateway anomaly POST failed: {resp.status_code} {resp.text}"
                            )

                except Exception:
                    logging.exception("Failed to forward event to gateway")

        else:
            logging.info("No anomalies detected")


        # Forward raw readings
        try:
            if GATEWAY_URL:
                post_url = (
                    f"{GATEWAY_URL.rstrip('/')}"
                    f"/api/sensors/{requests.utils.requote_uri(device_id)}/readings"
                )

                headers = {}
                if GATEWAY_API_KEY:
                    headers["X-API-Key"] = GATEWAY_API_KEY

                resp = requests.post(
                    post_url,
                    json={"reading": data},
                    headers=headers,
                    timeout=5
                )

                if not resp.ok:
                    logging.warning(
                        f"Gateway reading POST failed: {resp.status_code} {resp.text}"
                    )

        except Exception:
            logging.exception("Failed to forward raw reading to gateway")

    except Exception as e:
        logging.exception(f"Processing error: {e}")


def main():
    client = mqtt.Client()

    client.on_connect = on_connect
    client.on_message = on_message

    logging.info("Connecting to MQTT broker...")

    client.connect(MQTT_BROKER, MQTT_PORT, 60)

    client.loop_forever()


if __name__ == "__main__":
    main()
