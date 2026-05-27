import time
import numpy as np

class SensorAnomalyDetector:
    def __init__(self, config):
        # Keep track of previous averages
        self.prev = None

        # Moving averages (baseline)
        self.avg = None
        self.alpha = config["moving_average_alpha"]

        # Anomaly Thresholds ~ modify based on environment
        self.light_threshold = config["light_threshold"]
        self.temperature_threshold = config["temperature_threshold"]
        self.rssi_threshold = config["rssi_threshold"]

        # Motion state tracking
        self.motion_cooldown = config["motion_cooldown_seconds"]
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
            self.avg = (
                self.alpha * values
                + (1 - self.alpha) * self.avg
            )

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
        if light_delta > self.light_threshold:
            events.append({
                "type": "light_anomaly",
                "delta": float(light_delta),
                "value": data["light"]
            })

        # Temperature Anomoly
        temp_delta = abs(data["temperature"] - self.avg[0])
        if temp_delta > self.temperature_threshold:
            events.append({
                "type": "temperature_anomaly",
                "delta": float(temp_delta),
                "value": data["temperature"]
            })

        # Motion Detection
        current_time = time.time()

        if data["motion"] == 1:
            if (current_time - self.last_motion_time) > self.motion_cooldown:
                events.append({
                    "type": "motion_detected"
                })
                self.last_motion_time = current_time

        # # RSSI Comparision ~ Omitted Temporarily
        # if data["rssi"] < self.rssi_threshold:
        #     events.append({
        #         "type": "weak_signal",
        #         "rssi": data["rssi"]
        #     })

        # Update previous
        self.prev = data

        return events