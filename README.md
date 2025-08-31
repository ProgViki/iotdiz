# IoT Smart Door Lock & Alarm System

## 📌 Project Overview
This project is an **IoT-based smart door lock and alarm system** built with **C programming**.  
It leverages a **microcontroller (ESP32/ESP8266)** with **Wi-Fi connectivity** to allow remote control and monitoring of door access via a mobile or web interface.  

The system:
- Controls door access through an **electronic lock (servo/relay)**.  
- Detects unauthorized entry attempts using **sensors (PIR/Magnetic switch)**.  
- Sends **real-time alerts** to the user via Wi-Fi (web app, mobile app, or MQTT).  
- Can trigger an **alarm buzzer** in case of forced entry.

---

## ⚙️ Features
- 🔐 **Smart Lock Control** – Unlock/lock the door remotely.  
- 📡 **Wi-Fi Communication** – Connects to a router and communicates with a server or MQTT broker.  
- 🚨 **Intrusion Detection** – Triggers buzzer/alarm when motion or forced entry is detected.  
- 📱 **Remote Monitoring** – View door status via mobile/web interface.  
- 🔋 **Low Power Mode** (optional) – Saves energy when idle.  

---

## 🛠️ Hardware Requirements
- **ESP8266 / ESP32** (recommended for Wi-Fi support)  
- **Servo Motor / Relay Module** (to control the lock)  
- **Magnetic Door Sensor** or **PIR Motion Sensor**  
- **Buzzer** (for alarm system)  
- **LED indicators** (optional for lock status)  
- **Power supply** (5V USB or battery)  

---

## 🧑‍💻 Software Requirements
- **C (ESP-IDF for ESP32 or Arduino Core for ESP8266/ESP32)**  
- **MQTT Broker** (e.g., Mosquitto) or HTTP server  
- **Mobile/Web app** (can be simple HTML/JS or MQTT dashboard like Node-RED)  
- **Wi-Fi network** for connectivity  

---

## 🔧 Installation & Setup

### 1. Clone the Repository
```bash
git clone https://github.com/your-username/iot-smart-door-lock.git
cd iot-smart-door-lock
