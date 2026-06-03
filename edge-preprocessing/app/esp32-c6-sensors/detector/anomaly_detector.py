import time

class AnomalyDetector:
    def __init__(self, config):

        # Moving averages (baseline)
        self.avg_light = None
        self.avg_rssi = None
        self.alpha = config["moving_average_alpha"]

        # Anomaly Thresholds ~ modify based on environment
        self.light_threshold = config["light_threshold"]
        self.rssi_threshold = config["rssi_threshold"]

        # Motion state tracking
        self.motion_threshold = config["motion_threshold"]
        self.motion_cooldown = config["motion_cooldown_seconds"]
        self.last_motion_time = 0


    # Keep track of light & rssi value after detection 
    def update_avg(self, data):
        light = data["light"]
        rssi = data["rssi"]

        if self.avg_light is None:
            self.avg_light = light
            self.avg_rssi = rssi
        else:
            self.avg_light = (self.alpha * light
                + (1 - self.alpha) * self.avg_light
            )

            self.avg_rssi = (self.alpha * rssi
                + (1 - self.alpha) * self.avg_rssi
            )


    # Detection algorithm
    def detect(self, data):
        events = []

        # Initialize baseline
        if self.avg_light is None:
            self.update_avg(data)
            return events

        current_time = time.time()

        light_delta = abs(data["light"] - self.avg_light)

        # If light anomolgy & above motion threshold --> motion
        if (light_delta > self.motion_threshold and (current_time - self.last_motion_time) > self.motion_cooldown):
            events.append({
                "type": "motion_detected",
                "delta": float(light_delta),
                "value": data["light"]
            })

        self.last_motion_time = current_time

        # Significant lighting anomaly (light switched on/off) 
        if (light_delta > self.light_threshold): 
            events.append({ 
                "type": "light_anomaly", 
                "delta": float(light_delta), 
                "value": data["light"] 
            })
        
        # Weak Signal (RSSI)
        if data["rssi"] < self.rssi_threshold:
            events.append({
                "type": "weak_signal",
                "rssi": data["rssi"]
            })

        # Device reconnection (connection time < 10 sec)
        if data["connection_time_ms"] < 10000: 
            events.append({ 
                "type": "wifi_reconnected", 
                "connection_time_ms": data["connection_time_ms"] 
            })

        # # Temperature Anomoly
        # temp_delta = abs(data["temperature"] - self.avg_rssi)
        # if temp_delta > self.temperature_threshold:
        #     events.append({
        #         "type": "temperature_anomaly",
        #         "delta": float(temp_delta),
        #         "value": data["temperature"]
        #     })

        # Motion Detection
        # if data["motion"] > self.motion_threshold:
        #     if (current_time - self.last_motion_time) > self.motion_cooldown:
        #         events.append({
        #             "type": "motion_detected"
        #         })
        #         self.last_motion_time = current_time


        # Update previous
        self.update_avg(data)

        return events