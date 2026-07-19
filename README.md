# Smart Adaptive Security System - IoT Smart Weighbridge & Emergency Node

An IoT-based smart weighbridge station integrated with real-time automated fire safety defense and remote cloud management via the ThingsBoard platform.

## 👥 Authors & Contributions
* **[Member 1 Name]** - VNUHCM - University of Science (Role: Master Node Development, System Architecture)
* **proamillion** - VNUHCM - University of Science (Role: Hardware Integration, ESP32 Firmware Development, ThingsBoard Cloud & RPC Configuration)

## 📺 Project Demonstration Video
Click the link below to watch the full system operation, hardware showcase, and cloud telemetry integration:
👉 **[WATCH THE DEMO VIDEO HERE](https://drive.google.com/file/d/16EiuUGAwQ4OMnEpaOETN3xUfWmSztvMC/view?usp=sharing)**

---

## 🛠️ System Architecture & Components
The system is built on the **ESP32 DevKit V1** microcontroller utilizing non-blocking asynchronous programming (`millis()`) to handle parallel tasks efficiently:

*   **Weight Measurement:** Load Cell paired with a 24-bit **HX711** ADC module.
*   **Access Control:** **RC522 RFID** reader module combined with an **SG90 Servo** motor for automated barrier gate operations.
*   **Object & Vehicle Detection:** Infrared (IR) Obstacle Sensor.
*   **Environmental & Safety Monitoring:** **DHT11** Temperature/Humidity sensor and an **Active-LOW Flame Sensor**.
*   **Local UI & Indicators:** **SSD1306 OLED** Display (configured on a secondary software I2C bus via GPIO 16, 17), active buzzer, and Traffic LED indicators (Red, Yellow, Green).
*   **Cloud Connectivity:** MQTT Protocol linking telemetry and executing **Remote Procedure Calls (RPC)** via **ThingsBoard**.

---

## ⚡ Key Technical Features & Solved Challenges

1. **Non-Blocking Multitasking Firmware:** Replaced all native `delay()` functions with custom `millis()` state-machines to prevent the RFID module from being unresponsive and ensure continuous connection with the cloud server.
2. **Hardware Conflict Resolution:** Fixed I2C bus freezing caused by low-level hardware interrupt conflicts between the DHT11 sensor data sampling and the SSD1306 OLED display refresh sequences.
3. **Power Management & Noise Mitigation:** Isolated the peak inrush currents from the SG90 Servo and Active Buzzer using a shared-ground topology to ensure system stability and eliminate accidental brownout resets.

---

## 📂 File Directory
*   `/src`: Contains the source code for the ESP32 firmware (`.ino`).
*   `/hardware`: Schematic diagrams and pin mapping configuration.
