# ESP8266 RF/WiFi/HTTP Bridge

An advanced and configurable RF-to-Network bridge running on an ESP8266. This project receives 433MHz RF signals (from remote controls, sensors, etc.) and triggers complex, user-defined actions like HTTP/HTTPS requests or commands to smart home devices.

The entire system is configured through a responsive web interface, with all settings saved to the ESP8266's internal EEPROM.

## Key Features

- **RF Transceiver:** Receives and transmits 433MHz signals using a CC1101 module.
- **Dynamic Action Mapping:** Map specific RF codes to custom actions via the web UI.
- **RF Code Learning:** Capture and assign RF codes to actions directly from the interface.
- **Powerful Actions:**
  - **HTTP/HTTPS Requests:** Trigger GET or POST requests to any webhook or API endpoint.
  - **Chained HTTP Requests:** Execute a sequence of up to 2 HTTP requests for a single RF code.
  - **Shelly RPC Control:** Natively control Shelly devices (Gen2/Plus/Pro) with Toggle, On, and Off commands.
- **Web Interface:** Modern, responsive, and real-time UI for configuration and control.
  - Built-in status dashboard.
  - CC1101 parameter configuration (Frequency, Bandwidth, Data Rate).
  - Over-the-Air (OTA) firmware updates.
- **Real-time Updates:** Uses WebSockets to provide instant status updates to the UI without needing to refresh the page.
- **Persistent Storage:** All WiFi credentials and RF mappings are saved to EEPROM, surviving reboots.
- **Serial Console:** A command-line interface for debugging and manual control.

## Hardware Requirements

- **ESP8266 Board:** A NodeMCU, Wemos D1 Mini, or similar board.
- **CC1101 Transceiver Module:** A standard 433MHz CC1101 module.
- **Connecting Wires**

### Wiring

The CC1101 module connects to the ESP8266 via the SPI bus.

| CC1101 Pin | ESP8266 (NodeMCU) Pin | Description        |
| :--------- | :-------------------- | :----------------- |
| **VCC**    | 3V3                   | Power              |
| **GND**    | GND                   | Ground             |
| **MOSI**   | D7 (GPIO13)           | SPI MOSI           |
| **MISO**   | D6 (GPIO12)           | SPI MISO           |
| **SCK**    | D5 (GPIO14)           | SPI Clock          |
| **CSN**    | D8 (GPIO15)           | SPI Chip Select    |
| **GDO0**   | D1 (GPIO5)            | RF Data I/O        |
| **GDO2**   | *Not Connected*       | -                  |

*Note: The `rc-switch` library uses the `GDO0` pin for data, which is defined as `RF_DATA_PIN` in the code.*

## Software & Installation

This project is built using **PlatformIO**, which is the recommended way to compile and upload the firmware.

1.  **Install PlatformIO:** Follow the official instructions to install PlatformIO IDE for VSCode.
2.  **Clone the Repository:**
    ```bash
    git clone https://github.com/gderkintis/rf-bridge.git
    ```
3.  **Open in PlatformIO:** Open the cloned folder in VSCode. PlatformIO will automatically detect the `platformio.ini` file and download all the required libraries (`rc-switch`, `WebSockets`, `ArduinoJson`, etc.).
4.  **Build & Upload:** Connect your ESP8266 board via USB and click the "Upload" button in the PlatformIO toolbar.

## First-Time Setup

On the very first boot, the device will not have any WiFi credentials stored. It will automatically enter an interactive setup mode via the serial console.

1.  **Open Serial Monitor:** After uploading, open the PlatformIO Serial Monitor. The baud rate should be set to **9600**.
2.  **Scan for Networks:** The device will scan for available WiFi networks and list them.
3.  **Enter Credentials:** Follow the on-screen prompts to enter the name (or number) of your WiFi network and its password.

Once connected, the device will print its IP address to the serial monitor.

```
WiFi connected!
IP address: 192.168.1.118
HTTP server started. Access at http://192.168.1.118/
```

You can now access the web interface by navigating to this IP address in your browser.

## Using the Web Interface

The web interface is the primary way to control and configure the bridge.

### RF Mappings

- **Add a Mapping:** Click "Add New Mapping".
- **Choose Action Type:** Select whether the mapping should trigger an RPC command or an HTTP request.
- **Configure Parameters:**
  - For **RPC**, enter the IP address and Switch ID of the target Shelly device.
  - For **HTTP**, configure the URL, method (GET/POST), headers, and JSON body for each step in the chain.
- **Learn RF Code:** Click the "Learn RF" button for the mapping. The bridge will wait 30 seconds for you to press a button on your RF remote. The received code will be automatically saved to that mapping.
- **Enable/Disable:** Mappings can be individually enabled or disabled.
- **Execute:** Manually trigger an action for a mapping by clicking "Execute".

### System Maintenance

- **Reboot:** Remotely reboot the device.
- **Firmware Update:** Perform an Over-the-Air (OTA) update by uploading a new `firmware.bin` file. The device will reboot automatically upon successful update.

## License

This project is licensed under the MIT License. See the LICENSE file for details.