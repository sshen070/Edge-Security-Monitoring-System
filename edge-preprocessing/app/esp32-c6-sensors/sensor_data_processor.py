import json
import logging
from pathlib import Path

import paho.mqtt.client as mqtt

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
MQTT_TOPIC = "sensor/data"

def on_connect(client, userdata, flags, rc):
    logging.info(f"Connected to MQTT broker: {rc}")
    client.subscribe(MQTT_TOPIC)
    logging.info(f"Subscribed to topic: {MQTT_TOPIC}")


def on_message(client, userdata, msg):
    try:
        # Convert data into usable format
        payload = msg.payload.decode("utf-8")
        data = json.loads(payload)

        logging.info(f"Received sensor data: {data}")
        events = detector.detect(data)

        if events:
            logging.warning("ANOMALY EVENTS DETECTED")
            
            for event in events:
                logging.warning(event)

        else:
            logging.info("No anomalies detected")

    except Exception as e:
        logging.error(f"Processing error: {e}")


def main():
    client = mqtt.Client()

    client.on_connect = on_connect
    client.on_message = on_message

    logging.info("Connecting to MQTT broker...")

    client.connect(MQTT_BROKER, MQTT_PORT, 60)

    client.loop_forever()


if __name__ == "__main__":
    main()