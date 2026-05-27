import json
import numpy as np
import paho.mqtt.client as mqtt
import time

class SensorAnomalyDetector:
    def __init__(self):
        # Keep track of previous averages
        self.prev = None

        # Moving averages (baseline)
        self.avg = None
        self.alpha = 0.2  # smoothing factor

        # Anomaly Thresholds ~ modify based on environment
        self.LIGHT_THRESHOLD = 80
        self.TEMP_THRESHOLD = 1.0
        self.RSSI_THRESHOLD = -75

        # Motion state tracking
        self.motion_state = 0
        self.motion_event_cooldown = 2  # seconds
        self.last_motion_time = 0

    # Keep track of temperature & light value after detection 
    def update_avg(self, data):
        values = np.array([
            data["temperature"],
            data["light"]
        ])

        if self.avg is None:
            self.avg = values
        else:
            self.avg = self.alpha * values + (1 - self.alpha) * self.avg

        return self.avg

    # Detection algorithm
    def detect(self, data):
        events = []

        # Initialize baseline
        if self.prev is None:
            self.prev = data
            self.update_avg(data)
            return events

        self.update_avg(data)

        # Light Anomaly
        light_delta = abs(data["light"] - self.avg[1])
        if light_delta > self.LIGHT_THRESHOLD:
            events.append({
                "type": "light_anomaly",
                "delta": float(light_delta),
                "value": data["light"]
            })

        # Temperature Anomoly
        temp_delta = abs(data["temperature"] - self.avg[0])
        if temp_delta > self.TEMP_THRESHOLD:
            events.append({
                "type": "temperature_anomaly",
                "delta": float(temp_delta),
                "value": data["temperature"]
            })

        # Motion Detection
        current_time = time.time()

        if data["motion"] == 1:
            if (current_time - self.last_motion_time) > self.motion_event_cooldown:
                events.append({
                    "type": "motion_detected"
                })
                self.last_motion_time = current_time

        # # RSSI Comparision ~ Omitted Temporarily
        # if data["rssi"] < self.RSSI_THRESHOLD:
        #     events.append({
        #         "type": "weak_signal",
        #         "rssi": data["rssi"]
        #     })

        # Update previous
        self.prev = data

        return events


detector = SensorAnomalyDetector()

MQTT_BROKER = "x.x.x.x"   # Change to Jetson IP
MQTT_TOPIC = "sensors/esp32c3"


def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker:", rc)
    client.subscribe(MQTT_TOPIC)


def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8")
        data = json.loads(payload)

        events = detector.detect(data)

        if events:
            print("\nEVENT DETECTED:")
            for e in events:
                print("  ->", e)

            # Forward to cloud / logging system (in development)
            # forward_events(events)

        else:
            print("No event:", data)

    except Exception as e:
        print("Error processing message:", e)


def main():
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    print("Connecting to MQTT broker...")
    client.connect(MQTT_BROKER, 1883, 60)

    client.loop_forever()


if __name__ == "__main__":
    main()