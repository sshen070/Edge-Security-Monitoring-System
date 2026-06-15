# Edge-Based IoT Security Monitoring System

## Overview

A distributed IoT security monitoring platform that combines embedded sensors, edge computing, cloud services, and a web-based dashboard to provide real-time monitoring of environmental activity and security events.

The system leverages ESP32-based sensor nodes for data collection, a Jetson Orin Nano for edge processing, cloud-hosted APIs for event storage and retrieval, and a monitoring dashboard for visualization and remote access.

The architecture follows a layered IoT design consisting of the IoT Layer, Communication Layer, Edge/Fog Layer, and Cloud Layer.

---

# Project Objectives

* Monitor environmental activity using distributed IoT sensors
* Detect motion and door activity events
* Process data at the edge to reduce latency
* Store processed events in a cloud backend
* Provide real-time monitoring through a web dashboard
* Demonstrate integration of embedded systems, networking, edge computing, and cloud infrastructure

---

# System Architecture

## IoT Layer (Data Collection)

### ESP32-C6

Responsible for:

* Door activity detection
* Motion detection
* Environmental sensing
* Zigbee communication
* Sensor packet generation

### ESP32-S3 Camera Module

Responsible for:

* Image capture
* Video streaming
* Wi-Fi communication
* Camera status reporting

---

## Communication Layer

### Zigbee

Used for:

* Lightweight sensor communication
* Low-power operation
* Small packet transmission

### Wi-Fi

Used for:

* Camera data transfer
* Image streaming
* Video streaming
* Communication with cloud services

---

## Edge/Fog Layer

### NVIDIA Jetson Orin Nano

The Jetson serves as the local edge gateway and processing engine.

Responsibilities include:

* Aggregating sensor data
* Filtering raw sensor events
* Local event processing
* Motion and activity detection
* Data reduction before cloud transmission
* Device coordination

### Benefits

* Reduced cloud bandwidth consumption
* Lower latency
* Improved privacy
* Faster event response

---

## Cloud Layer

The cloud backend provides:

* Device registration
* Event storage
* API services
* Dashboard integration
* Remote monitoring capabilities

Processed event data is stored and made available through REST APIs.

---

# Device Catalog

| Device                  | Category       | Purpose                            | Communication         |
| ----------------------- | -------------- | ---------------------------------- | --------------------- |
| NVIDIA Jetson Orin Nano | Edge Device    | Local processing and aggregation   | Wi-Fi / LAN           |
| ESP32-C6                | IoT Sensor     | Motion and door activity detection | Zigbee                |
| ESP32-S3 Camera Module  | IoT Camera     | Image and video capture            | Wi-Fi                 |
| PIR Motion Sensor       | Sensor         | Motion detection                   | Connected to ESP32-C6 |
| Zigbee Network          | Communication  | Sensor messaging                   | Zigbee                |
| Wi-Fi Network           | Communication  | Camera and backend communication   | Wi-Fi                 |
| Cloud Storage/API       | Infrastructure | Event storage and retrieval        | Internet              |
| Web Dashboard           | User Interface | Monitoring and visualization       | Browser               |

---

# Software Stack

## Backend Technologies

| Technology  | Purpose                       |
| ----------- | ----------------------------- |
| Python 3.11 | Runtime environment           |
| FastAPI     | REST API framework            |
| Uvicorn     | ASGI web server               |
| Pydantic    | Request/response validation   |
| SQLAlchemy  | Database ORM                  |
| SQLite      | Persistent event storage      |
| Swagger UI  | API testing and documentation |

## Frontend Technologies

| Technology | Purpose               |
| ---------- | --------------------- |
| HTML/CSS   | Dashboard interface   |
| JavaScript | Dynamic functionality |
| Chart.js   | Data visualization    |
| REST APIs  | Backend communication |

---

# Backend Architecture

The backend is implemented using FastAPI and exposes REST endpoints for device registration and event management.

## Main Endpoints

### Health Check

```http
GET /v1/health
```

### Devices

```http
GET /v1/devices
POST /v1/devices
```

### Events

```http
GET /v1/events
POST /v1/events
```

---

# Dashboard Features

The monitoring dashboard provides:

* Device status monitoring
* Event visualization
* Sensor activity charts
* Alert notifications
* Camera status reporting
* Historical event viewing

The frontend retrieves data through FastAPI endpoints and displays information in real time.

---

# Communication Patterns

## Request-Response Architecture

The cloud layer follows a REST-based request-response pattern.

### Data Upload

Jetson Gateway → Backend API

```text
HTTP POST
JSON Payload
```

Used for:

* Event uploads
* Device updates
* Sensor activity reporting

### Data Retrieval

Dashboard → Backend API

```text
HTTP GET
JSON Response
```

Used for:

* Dashboard updates
* Device monitoring
* Event history retrieval

---

# Data Flow

1. Sensors detect motion or door activity.
2. ESP32-C6 generates sensor events.
3. Sensor data is transmitted through Zigbee.
4. Camera data is transmitted through Wi-Fi.
5. Jetson Orin Nano aggregates and processes incoming data.
6. Important events are identified locally.
7. Processed events are uploaded to the cloud backend.
8. FastAPI stores events in SQLite.
9. Dashboard retrieves data using REST APIs.
10. Users view alerts and device status through the web interface.

---

# Security, Privacy, and Reliability

## Security

Current security features include:

* Optional API key validation
* Controlled API access
* Sensitive files excluded from version control

### Future Improvements

* User authentication
* Role-based access control
* HTTPS deployment
* Token-based authorization

---

## Privacy

Privacy is improved through edge computing:

* Raw video remains at the edge
* Only processed event information is uploaded
* Reduced transmission of sensitive data

---

## Reliability

System reliability features include:

* Health-check endpoint
* Device heartbeat monitoring
* Event timestamp tracking
* Persistent SQLite storage

---

# Challenges Encountered

## Frontend and Backend Integration

Initially, the dashboard displayed static sample data. This was resolved by implementing API requests that retrieve live device and event information from the FastAPI backend.

## Cross-Origin Resource Sharing (CORS)

Because the frontend and backend operated on different ports, browsers blocked requests between them. This issue was resolved by configuring FastAPI CORS middleware, allowing communication between services.

## API Validation and Data Consistency

The frontend required data in a specific JSON structure. Swagger UI was used extensively to:

* Test endpoints
* Validate request formats
* Verify database storage
* Debug integration issues

---

# Current Status

## Completed

* Layered IoT architecture
* FastAPI backend
* SQLite event storage
* Device registration system
* Dashboard visualization
* Jetson edge-processing integration
* ESP32 sensor integration
* Zigbee communication architecture
* REST API communication
* Event monitoring workflow

## Demonstrated

The project demonstration successfully shows:

* Live sensor interaction
* Motion detection events
* Door activity events
* Event processing pipeline
* Dashboard updates
* Edge-to-cloud communication

## Limitations

Camera-based image and video analytics were designed but not fully integrated due to hardware and integration constraints encountered during development.

However, the complete architecture supporting camera processing, edge computing, cloud storage, and dashboard visualization was designed and partially implemented.

---

# Future Work

Future enhancements include:

* Full ESP32-S3 camera integration
* Computer vision analytics
* Real-time WebSocket updates
* Cloud deployment
* Mobile notifications
* Enhanced authentication
* Multi-gateway support
* Distributed edge processing
* AI-based anomaly detection
* Long-term event analytics

---

# Repository Structure

```text
├───cloud-backend
│   ├───app
│   └───data
├───cloud-dashboard
├───docs
├───edge-preprocessing
│   └───app
│       ├───esp32-c6-sensors
│       │   ├───detector
│       └───mosquitto
│           └───config
├───esp-firmware
│   ├───esp32-c6-sensors
│   │   ├───esp32_sensor_mqtt_node
│   │   │   └───build
│   ├───esp32-s3-camera
│   │   ├───camera_stream
├───jetson-device-gateway

---

# Demo

The project demonstration showcases:

* Live ESP32 sensor interactions
* Motion detection events
* Door activity monitoring
* Edge processing on the Jetson Orin Nano
* Cloud API integration
* Real-time dashboard updates
