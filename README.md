# Smart-Parking-System
A Smart Parking Management System built using the ESP32 that monitors parking slot occupancy in real time, controls the parking gate using a servo motor, and provides a web-based interface for users and the watchman. The system automatically generates a parking code, calculates parking charges based on the duration of stay, and updates slot availability dynamically.

## Features

* Real-time monitoring of 3 parking slots using ultrasonic sensors
* Automatic vehicle entry detection
* Unique 6-digit parking code generation
* Parking fee calculation (₹1/minute)
* Password-protected watchman panel
* Servo-controlled parking gate
* Live parking slot availability on a web page
* Automatic slot release after vehicle exit
* Manual gate control using a push button

## Hardware

* ESP32 Development Board
* HC-SR04 Ultrasonic Sensors ×3
* Servo Motor (SG90/MG996R)
* Push Button
* Breadboard
* Jumper Wires
* 5V Power Supply

## Software

* Arduino IDE
* ESP32 Arduino Core
* ESP32Servo Library
* WiFi Library
* WebServer Library

## Pin Mapping

| Component                  | ESP32 Pin | Type           | Description         |
| -------------------------- | --------: | -------------- | ------------------- |
| Ultrasonic Sensor 1 (TRIG) |    GPIO 5 | Digital Output | Trigger signal      |
| Ultrasonic Sensor 1 (ECHO) |   GPIO 23 | Digital Input  | Echo signal         |
| Ultrasonic Sensor 2 (TRIG) |   GPIO 18 | Digital Output | Trigger signal      |
| Ultrasonic Sensor 2 (ECHO) |   GPIO 22 | Digital Input  | Echo signal         |
| Ultrasonic Sensor 3 (TRIG) |   GPIO 19 | Digital Output | Trigger signal      |
| Ultrasonic Sensor 3 (ECHO) |    GPIO 4 | Digital Input  | Echo signal         |
| Servo Motor                |   GPIO 15 | PWM Output     | Gate control        |
| Push Button                |   GPIO 27 | Digital Input  | Manual gate control |

## Power Connections

| Component          | Supply                    |
| ------------------ | ------------------------- |
| ESP32              | USB (5V)                  |
| Ultrasonic Sensors | 5V                        |
| Servo Motor        | External 5V (Recommended) |
| Push Button        | GPIO + GND                |

> **Note:** All components must share a common ground.

## System Architecture

```text
          +----------------------+
          |    User Web Browser  |
          +----------+-----------+
                     |
                Wi-Fi / HTTP
                     |
          +----------v-----------+
          |        ESP32         |
          |     Web Server       |
          +----------+-----------+
                     |
 +-------------------+-------------------+
 |                   |                   |
 |                   |                   |
+----v----+      +---v-----+       +----v-------+
| HC-SR04 |      |  Servo  |       | Push Button|
| Sensors |      |  Motor  |       | Manual Ctrl|
+---------+      +---------+       +------------+
```

## Working Principle

1. ESP32 initializes sensors, servo motor, Wi-Fi, and web server.
2. Ultrasonic sensors continuously monitor parking slots.
3. When a vehicle enters an empty slot:

   * A unique 6-digit parking code is generated.
   * Entry time is stored using `millis()`.
   * Users can access the web page to view slot availability.
4. During exit:

   * User enters the parking code.
   * System calculates parking duration.
   * Parking fee is displayed.
   * Exit permission is granted.
5. Once the ultrasonic sensor confirms that the vehicle has left, the slot is marked as available.

## Web Pages

### User Dashboard

* Live parking slot availability
* Parking code entry
* Parking bill display

### Watchman Dashboard

* Password authentication
* Manual gate control
* Live parking slot monitoring

## Communication Protocols

| Protocol                | Purpose                       |
| ----------------------- | ----------------------------- |
| Wi-Fi (IEEE 802.11)     | Wireless communication        |
| HTTP                    | Browser ↔ ESP32 communication |
| PWM                     | Servo motor control           |
| UART (115200 baud)      | Serial debugging              |
| Ultrasonic Pulse Timing | Distance measurement          |

## Future Improvements

* RFID-based vehicle authentication
* QR code entry and exit
* Cloud database integration
* Mobile application
* OTA firmware updates
* HTTPS communication
* EEPROM/NVS data storage
* Automatic payment gateway integration

