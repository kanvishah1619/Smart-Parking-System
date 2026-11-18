# Smart Parking System

An ESP32 monitors three parking slots with ultrasonic sensors, hosts separate driver and watchman web pages, and sends parking statistics to ThingSpeak. The watchman assigns spaces and operates the servo gate. Drivers use a six-digit session code to check out.

## How a parking session works

1. **Assign a slot.** The watchman generates a code for the first empty, unassigned space. Billing starts when the code is issued. An assignment expires after 10 minutes if the vehicle has never been detected.
2. **Detect the vehicle.** The ESP32 cycles through the three sensors, sampling one every 80 ms. Three consecutive readings between 0 and 25 cm (exclusive) mark a slot occupied. If a vehicle parks in an unassigned space, the firmware can move the first pending session to that space.
3. **Check out.** The driver enters the code on the driver page. The ESP32 calculates the charge at ₹1 per minute, prorated with a ₹1 minimum, and displays a receipt. This is local billing without a payment processor.
4. **Release the space.** Checkout clears the assignment. The sensor must report a clear space for two seconds before an occupied slot becomes free. The watchman opens or closes the gate separately.

## Hardware and GPIO mapping

| Slot | Ultrasonic TRIG | Ultrasonic ECHO | Green LED |
|---|---:|---:|---:|
| S1 | 23 | 34 | 13 |
| S2 | 19 | 35 | 14 |
| S3 | 18 | 39 | 16 |

| Peripheral | ESP32 GPIO | Behavior |
|---|---|---|
| Servo gate | 17 | 0° closed, 90° open |
| Buzzer | 4 | Audible feedback for session and gate actions |
| 128×64 SSD1306 OLED | SDA 21, SCL 22 | Probes I²C addresses `0x3C` and `0x3D` |

The sketch specifies a 1 kΩ / 2 kΩ voltage divider for each ultrasonic ECHO line. The green LED shows physical vacancy, so it can stay lit for a reserved space until a vehicle arrives. The OLED distinguishes **F** (free), **A** (assigned), and **B** (occupied), and displays the number of assignable spaces and the gate state.

## Run the project

1. Open `claudeoled.ino` in Arduino IDE. Install the ESP32 board package and select your ESP32 board and port.
2. Install **ESP32Servo**, **Adafruit GFX Library**, and **Adafruit SSD1306** using Library Manager, along with their dependencies.
3. Set `ssid` and `password` for your 2.4 GHz Wi-Fi network, `watchmanPassword` for the operator controls, and `thingSpeakAPIKey` to your channel’s Write API Key. Keep personal credentials out of the public repository.
4. Upload the sketch and open Serial Monitor at **115200 baud**. On a device connected to the same network, open the portal addresses printed by the ESP32.

| Local route | Purpose |
|---|---|
| `/` | Driver page for slot availability and checkout |
| `/watchman` | Operator page for slot assignment and manual gate control |
| `/status` | JSON snapshot of slot states, availability, gate state, completed checkouts, and revenue |

## ThingSpeak telemetry

While Wi-Fi is connected, the sketch attempts a channel update every 15 seconds.

| Field | Value |
|---:|---|
| 1 | Spaces available for assignment |
| 2 | Physically occupied spaces |
| 3–5 | Occupancy of S1, S2, and S3 respectively (1 occupied, 0 empty) |
| 6 | Total completed checkouts |
| 7 | Cumulative billed revenue in rupees |
| 8 | Commanded gate state (1 open, 0 closed) |

Completed checkout count and revenue persist across restarts using ESP32 Preferences. Active assignments and session codes are held in RAM and clear on restart.

## Deployment scope

The sketch embeds the watchman password in the web page it serves. Run this prototype on a trusted local network. Five invalid exit-code attempts trigger a shared 10-second checkout lockout.

