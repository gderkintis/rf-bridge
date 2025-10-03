#include <RCSwitch.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <EEPROM.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <ESP8266WebServer.h> 
#include <WebSocketsServer.h>
#include <WebSocketsClient.h>
#include <vector>
#include <ArduinoJson.h>
#include <Updater.h>
#include <time.h>
#include "web_page.h"

// Fallback definition for UPDATE_SIZE_UNKNOWN if not provided by Updater.h
#ifndef UPDATE_SIZE_UNKNOWN
  #define UPDATE_SIZE_UNKNOWN 0xFFFFFFFFU
#endif

// Globals for OTA Update Status
bool otaUpdateInProgress = false;
bool otaUpdateSuccess = false;
String otaUpdateMessage = "";

RCSwitch mySwitch = RCSwitch();
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
WebSocketsClient RPC;

// --- Configuration ---
String currentSSID = "";
String currentPassword = "";

const int RF_DATA_PIN = D1;

// CC1101 Status Globals
float currentFrequency = 433.92;
float currentDataRate = 2.8;
float currentBandwidth = 203;
String currentCC1101Mode = "RX";

// --- RPC Globals ---
bool isDeviceConnected = false;
unsigned long lastDeviceAttempt = 0;
const unsigned long RPC_RECONNECT_INTERVAL_NORMAL = 10000; // 10 seconds
unsigned long lastRPCPingSent = 0;
const unsigned long RPC_PING_INTERVAL = 30000; // Send a PING every 30 seconds
unsigned int DeviceMsgId = 1; // For uniquely identifying RPC messages
bool currentRPCRelayIsOn = false; // Last known state of the RPC device
bool RPCRelayStateKnown = false;  // Whether the relay state has been successfully fetched

// New globals for RPC command timeout
unsigned long DeviceCmdSentTime = 0;
unsigned int DeviceCmdIdSent = 0;
uint16_t DeviceCmdOriginatingMappingId = 0; // Store the mapping ID that initiated the command
bool DeviceCmdWaitingForResponse = false;
const unsigned long RPC_CMD_TIMEOUT_MS = 5000; // 5 seconds
const int RPC_CMD_TIMEOUT_STATUS_CODE = -100; // Special timeout command

// --- Globals for RF Action Mappings ---
int learnModeForMappingIndex = -1; // -1 if not in learn mode, otherwise index of mapping being learned
unsigned long learnModeStartTime = 0; // Timestamp for learn mode activation
const unsigned long LEARN_MODE_TIMEOUT_MS = 30000; // 30 seconds for learn mode timeout

// --- New RF Action Mapping Structures ---
#define MAX_URL_LEN 64       // Max length for URLs
#define MAX_HEADERS_LEN 64   // Max length for HTTP POST headers (e.g., "Content-Type: application/json")
#define MAX_JSON_DATA_LEN 64 // Max length for HTTP POST JSON data (e.g., {"value":1})
#define MAX_HTTP_STEPS_PER_MAPPING 2 // Max number of HTTP commands in a chain for one mapping

// Define IP_MAX_LEN before it's used in RfActionMapping struct and other places
#define IP_MAX_LEN 16 // For "xxx.xxx.xxx.xxx\0"

enum HttpMethod : uint8_t {
  HTTP_METHOD_GET = 0,
  HTTP_METHOD_POST = 1
};

struct HttpStep {
  HttpMethod method;
  char url[MAX_URL_LEN];
  char headers[MAX_HEADERS_LEN];
  char jsonData[MAX_JSON_DATA_LEN];
  int lastHttpStatusCode;

  HttpStep() : method(HTTP_METHOD_GET), lastHttpStatusCode(0) {
    memset(url, 0, MAX_URL_LEN);
    memset(headers, 0, MAX_HEADERS_LEN);
    memset(jsonData, 0, MAX_JSON_DATA_LEN);
  }
};

enum ActionType : uint8_t {
  ACTION_NONE = 0,
  ACTION_RPC_TOGGLE = 1,
  ACTION_RPC_ON = 2,
  ACTION_RPC_OFF = 3,
  ACTION_HTTP = 4
};

struct RfActionMapping {
  uint16_t id; // Unique ID for this mapping, assigned by the server
  unsigned long rfCode;
  ActionType actionType;
  char RPC_IP[IP_MAX_LEN];
  int RPC_SwitchId;
  HttpStep httpSteps[MAX_HTTP_STEPS_PER_MAPPING];
  uint8_t numHttpSteps;
  bool enabled;

  RfActionMapping() : id(0), rfCode(0), actionType(ACTION_NONE), RPC_SwitchId(0), numHttpSteps(0), enabled(false) {
    memset(RPC_IP, 0, IP_MAX_LEN);
  }
};

std::vector<RfActionMapping> rfActionMappings; // Dynamic list of mappings
uint16_t nextRfMappingId = 1; // Counter for unique RF Mapping IDs

// --- EEPROM Configuration --- //
#define EEPROM_SIZE 4096 // Using 4KB EEPROM size

// RF Action Mappings Storage
#define MAX_SUPPORTED_RF_MAPPINGS 9    // Max RF mappings with 4KB EEPROM and 2 HTTP steps per mapping
#define RF_MAPPINGS_STORED_FLAG_ADDR 0 // Start address for the magic byte
#define RF_MAPPINGS_STORED_MAGIC_BYTE 0xB6 // Magic byte for stored mappings array
#define NEXT_RF_MAPPING_ID_EEPROM_ADDR (RF_MAPPINGS_STORED_FLAG_ADDR + sizeof(byte))
#define NUM_RF_MAPPINGS_EEPROM_ADDR (NEXT_RF_MAPPING_ID_EEPROM_ADDR + sizeof(uint16_t))
#define RF_MAPPINGS_DATA_START_ADDR (NUM_RF_MAPPINGS_EEPROM_ADDR + sizeof(uint16_t)) // Where actual mapping structs start

// General Configuration Storage
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 64
#define IP_MAX_LEN 16
#define GENERAL_CONFIG_START_ADDR 3820 
#define CONFIG_STORED_MAGIC_BYTE 0xC7 // A magic byte for general config
#define WIFI_SSID_EEPROM_ADDR (CONFIG_STORED_FLAG_ADDR + sizeof(byte))
#define WIFI_PASS_EEPROM_ADDR (WIFI_SSID_EEPROM_ADDR + WIFI_SSID_MAX_LEN + 1) // +1 for null terminator

// CC1101 Configuration Storage
#define CONFIG_STORED_FLAG_ADDR GENERAL_CONFIG_START_ADDR // The flag for general config
#define WIFI_SSID_EEPROM_ADDR (CONFIG_STORED_FLAG_ADDR + sizeof(byte))
#define WIFI_PASS_EEPROM_ADDR (WIFI_SSID_EEPROM_ADDR + WIFI_SSID_MAX_LEN + 1)
#define CC1101_FREQ_EEPROM_ADDR (WIFI_PASS_EEPROM_ADDR + WIFI_PASS_MAX_LEN + 1)
#define CC1101_BANDWIDTH_EEPROM_ADDR (CC1101_FREQ_EEPROM_ADDR + sizeof(float))
#define CC1101_RATE_EEPROM_ADDR (CC1101_BANDWIDTH_EEPROM_ADDR + sizeof(float))
#define EEPROM_REBOOT_FLAG_ADDR (CC1101_RATE_EEPROM_ADDR + sizeof(float))
const byte REBOOT_FLAG_MAGIC_BYTE = 0xD7; // Magic byte for the EEPROM reboot flag
// --- End EEPROM Configuration ---

// --- Global Variables ---
void loadRfMappingsFromEEPROM();
bool saveRfMappingsToEEPROM();
void handleSerialCommands();
void printHelp();
void initialize_cc1101();
bool loadConfiguration();
void saveConfiguration();
void handleRoot();
void handleNotFound();

void setClock();
void parseAndAddHeader(HTTPClient& http, const char* headerLine);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length); // WebSocket event handler
void RPCEvent(WStype_t type, uint8_t * payload, size_t length); // RPC WebSocket client event handler
void requestDeviceSwitchStatus(); // New: to get current device switch state
void sendRpcCommand(const JsonDocument& doc, int switchId, uint16_t originatingMappingId, const String& commandType);
void sendDeviceToggleCommand(const char* deviceIp, int switchId, uint16_t originatingMappingId);
void sendDeviceSetCommand(const char* deviceIp, int switchId, bool turnOn, uint16_t originatingMappingId);
void executeActionForRfCode(unsigned long rfCode); // Executes the action for a given mapping index (found by RF code)
void executeConfiguredAction(RfActionMapping& mapping); // Executes a specific pre-found mapping (now takes non-const ref)
void broadcastStatusUpdate(); // Send full status to all WebSocket clients
void broadcastAlert(const String& paneId, const String& message, const String& alertType, int duration = 5000); // Send alert to all WebSocket clients
void handleWebSocketCommand(uint8_t clientNum, const char* action, JsonObject& cmdPayload); // New handler for WS commands
String escapeJsonString(const String& str); // Forward declaration
void interactiveConfigSetup(); // Forward declaration for interactive setup

// --- Global Variables ---
bool g_justRebootedFromClientCommand = false; // Flag to indicate if reboot was client-initiated

void setup() {
  Serial.begin(9600);
  Serial.println("\nRF Bridge Initializing...");

  initialize_cc1101(); // Initialize CC1101 for reception
  mySwitch.enableReceive(RF_DATA_PIN);

  // Initialize EEPROM and load settings
  Serial.println("Initializing EEPROM...");
  EEPROM.begin(EEPROM_SIZE);

  // Check for client-initiated reboot flag
  if (EEPROM.read(EEPROM_REBOOT_FLAG_ADDR) == REBOOT_FLAG_MAGIC_BYTE) {
    g_justRebootedFromClientCommand = true;
    EEPROM.write(EEPROM_REBOOT_FLAG_ADDR, 0x00);
    EEPROM.commit();
  }

  loadRfMappingsFromEEPROM();

  // --- WiFi Setup ---
  WiFi.mode(WIFI_STA);
  bool config_loaded = loadConfiguration();
  bool connected = false;

  if (config_loaded && currentSSID.length() > 0) {
    Serial.println("Attempting to connect with stored configuration...");
    Serial.print("SSID: "); Serial.println(currentSSID);
    WiFi.begin(currentSSID.c_str(), currentPassword.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
    } else {
      Serial.println("\nFailed to connect with stored configuration.");
      WiFi.disconnect(true);
      delay(100);
    }
  }

  if (!connected) {
    interactiveConfigSetup();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    setClock();
  } else {
    Serial.println("\nFailed to connect to WiFi. RPC control will not work.");
    Serial.println("Use 'config setup' command or restart to try setup again.");
  }

  // --- Web Server Setup ---
  server.on("/", HTTP_GET, handleRoot);

  // OTA Firmware Update Handler
  server.on("/update", HTTP_POST, []() { // Main handler: called after upload is complete or aborted
    server.sendHeader("Connection", "close"); // Important to close connection before reboot

    // Send JSON response back to the client's XMLHttpRequest
    String jsonResponse = "{";
    jsonResponse += "\"success\":" + String(otaUpdateSuccess ? "true" : "false") + ",";
    jsonResponse += "\"message\":\"" + escapeJsonString(otaUpdateMessage) + "\"";
    jsonResponse += "}";
    server.send(200, "application/json", jsonResponse);

    bool rebootAfterResponse = otaUpdateSuccess;

    // Clear status after sending response
    otaUpdateSuccess = false;
    otaUpdateMessage = "";

    if (rebootAfterResponse) {
      Serial.println("OTA Update successful. Rebooting after sending client response.");
      EEPROM.write(EEPROM_REBOOT_FLAG_ADDR, REBOOT_FLAG_MAGIC_BYTE);
      if (EEPROM.commit()) {
          // Serial.println("Reboot flag set and EEPROM committed (OTA)."); // DEBUG
      } else {
      }
      delay(50); // Brief delay for EEPROM commit

      Serial.println("OTA: Attempting to gracefully disconnect WebSocket clients before reboot...");
      const uint8_t maxClients = 4;
      for (uint8_t i = 0; i < maxClients; i++) {
          if (webSocket.clientIsConnected(i)) {
              Serial.printf("OTA: Disconnecting client %u.\n", i);
              webSocket.disconnect(i);
          }
      }
      
      // Allow disconnect frames to be processed and sent
      for(int i=0; i<10; ++i) { // Pump the loop again
          webSocket.loop();
          delay(20); // Small delay, total 200ms for disconnects to go out
      }
      Serial.println("OTA: WebSocket client disconnection attempt complete.");

      delay(1000); // Increased delay to allow the response to be fully sent and seen by client
      ESP.restart();
    } else {
      Serial.println("OTA Update failed or was aborted. Not rebooting.");
    }
  }, []() { // Uploader lambda: called for each chunk and for start/end/abort events
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Firmware Update: %s\n", upload.filename.c_str());
      // UPDATE_SIZE_UNKNOWN allows the Updater library to detect the size.
      // Ensure your partition scheme has enough space for an OTA update.
      otaUpdateInProgress = true;
      otaUpdateSuccess = false; // Reset status for new upload
      otaUpdateMessage = "Starting firmware update..."; 

      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
        otaUpdateMessage = "Update begin failed: " + Update.getErrorString();
        // otaUpdateSuccess remains false
        otaUpdateInProgress = false; // Mark as failed early
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (otaUpdateInProgress) { // Only write if begin was successful
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
          otaUpdateMessage = "Update write failed: " + Update.getErrorString();
          otaUpdateSuccess = false;
          otaUpdateInProgress = false; // Mark as failed
        }
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (otaUpdateInProgress) { // Only finalize if begin and writes were successful
        if (Update.end(true)) { // true to set the success flag and trigger the update on reboot
          otaUpdateSuccess = true;
          otaUpdateMessage = "Uploaded: " + String(upload.totalSize) + " bytes.";
          Serial.printf("Update Success: %u bytes\n", upload.totalSize);
        } else {
          Update.printError(Serial);
          otaUpdateMessage = "Update end failed: " + Update.getErrorString();
          otaUpdateSuccess = false;
        }
      }
      otaUpdateInProgress = false; // Upload process finished (success or fail)
      Serial.println("Firmware upload process finished (end event).");
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      otaUpdateMessage = "Firmware upload aborted by client or connection lost.";
      otaUpdateSuccess = false;
      otaUpdateInProgress = false;
      Update.end(false); // Finalize the update process, indicating failure
      Serial.println("Firmware upload aborted.");
    }
  });

  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started. Access at http://" + WiFi.localIP().toString() + "/");

  // --- Find first RPC device and attempt connection ---
  if (WiFi.status() == WL_CONNECTED) {
    String firstRpcIp = "";
    for (const auto& mapping : rfActionMappings) {
        if ((mapping.actionType >= ACTION_RPC_TOGGLE && mapping.actionType <= ACTION_RPC_OFF) && strlen(mapping.RPC_IP) > 0) {
            firstRpcIp = mapping.RPC_IP;
            break;
        }
    }

    if (firstRpcIp.length() > 0) {
        Serial.printf("Found first RPC device in mappings. Attempting to connect to %s\n", firstRpcIp.c_str());
        // The port for Shelly Gen2/Plus/Pro RPC is 80, and the URL is /rpc
        RPC.begin(firstRpcIp.c_str(), 80, "/rpc");
    }
  }

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started on port 81.");

  // Initialize RPC WebSocket client
  RPC.onEvent(RPCEvent);
  RPC.setReconnectInterval(5000); // Library's own reconnect interval

  Serial.println("Setup complete. Waiting for RF signals or serial commands...");
  Serial.println("Type 'help' for a list of commands.");
}

void setClock() {
  // Set time via NTP, necessary for TLS/SSL certificates
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) { // A simple check to see if time is plausible
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}

void interactiveConfigSetup() {
  Serial.println("\n--- Interactive WiFi Setup ---");
  WiFi.disconnect(true); // Disconnect if previously connected
  delay(100);

  Serial.println("Scanning for WiFi networks...");
  int n = WiFi.scanNetworks();
  Serial.println("Scan complete.");

  if (n == 0) {
    Serial.println("No networks found. Please check your WiFi environment and restart.");
    // Don't return, allow user to manually enter SSID if they know it
  } else {
    Serial.print(n);
    Serial.println(" networks found:");
    for (int i = 0; i < n; ++i) {
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print("dBm) ");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
  }
  Serial.println();

  String input_ssid = "";
  String input_pass = "";
  while (input_ssid == "") {
    Serial.print("Enter WiFi SSID (or number from scan list): ");
    while (Serial.available() == 0) { delay(50); } // Wait for input
    String selection = Serial.readStringUntil('\n');
    selection.trim();

    int network_num = selection.toInt(); // Try to parse as number
    if (network_num > 0 && network_num <= n) { // User entered a number from the list
      input_ssid = WiFi.SSID(network_num - 1);
      Serial.print("Selected network SSID: "); Serial.println(input_ssid);
    } else if (selection.length() > 0) { // User entered SSID directly
      input_ssid = selection;
      Serial.print("Entered network SSID: "); Serial.println(input_ssid);
    } else {
      Serial.println("Invalid input. Please enter SSID or a number from the list.");
    }
  }

  Serial.print("Enter password for '"); Serial.print(input_ssid); Serial.print("': ");
  while (Serial.available() == 0) { delay(50); } // Wait for input
  input_pass = Serial.readStringUntil('\n');
  input_pass.trim();
  Serial.println("\nPassword received (not echoed).");

  // Device IP and Switch ID are no longer part of this interactive setup. They will be set via the web UI.
  // If a device IP was previously configured for a mapping, it will be retained.

  Serial.print("Attempting to connect to "); Serial.println(input_ssid);
  WiFi.begin(input_ssid.c_str(), input_pass.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) { // Try for ~30 seconds
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nSuccessfully connected to WiFi!");
    currentSSID = input_ssid;
    currentPassword = input_pass;
    saveConfiguration();
  } else {
    Serial.println("\nFailed to connect. Please check password and try again by restarting.");
    WiFi.disconnect(true);
  }
}

void initialize_cc1101() {
  Serial.println("Configuring CC1101 with current settings for RX mode...");
  pinMode(RF_DATA_PIN, INPUT);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setMHZ(currentFrequency);     // Apply current global frequency
  ELECHOUSE_cc1101.setRxBW(currentBandwidth);  // Apply current global Bandwidth
  ELECHOUSE_cc1101.setDRate(currentDataRate);    // Apply current global Data Rate

  ELECHOUSE_cc1101.setCrc(0);
  ELECHOUSE_cc1101.setCRC_AF(0);
  ELECHOUSE_cc1101.setPktFormat(0);
  ELECHOUSE_cc1101.setPacketLength(2); // Default, might need review for general purpose
  ELECHOUSE_cc1101.setDcFilterOff(0);
  ELECHOUSE_cc1101.setFEC(0);
  ELECHOUSE_cc1101.setPQT(4);
  ELECHOUSE_cc1101.setSyncMode(0);
  ELECHOUSE_cc1101.setAdrChk(0);
  ELECHOUSE_cc1101.setWhiteData(0);

  ELECHOUSE_cc1101.SetRx();
  currentCC1101Mode = "RX"; // Set current mode
  Serial.print("CC1101 configured. Freq: "); Serial.print(currentFrequency,3);
  Serial.print("MHz, Bandwidth: "); Serial.print(currentBandwidth,0);
  Serial.print("kHz, Data Rate: "); Serial.print(currentDataRate,1);
  Serial.println("kBaud. Mode: RX");
}

bool loadConfiguration() {
  if (EEPROM.read(CONFIG_STORED_FLAG_ADDR) == CONFIG_STORED_MAGIC_BYTE) {
    char ssid_buffer[WIFI_SSID_MAX_LEN + 1];
    char pass_buffer[WIFI_PASS_MAX_LEN + 1];

    for (int i = 0; i <= WIFI_SSID_MAX_LEN; ++i) {
      ssid_buffer[i] = EEPROM.read(WIFI_SSID_EEPROM_ADDR + i);
    }
    ssid_buffer[WIFI_SSID_MAX_LEN] = '\0'; // Ensure null termination
    for (int i = 0; i <= WIFI_PASS_MAX_LEN; ++i) {
      pass_buffer[i] = EEPROM.read(WIFI_PASS_EEPROM_ADDR + i);
    }
    pass_buffer[WIFI_PASS_MAX_LEN] = '\0'; // Ensure null termination

    currentSSID = String(ssid_buffer);
    currentPassword = String(pass_buffer);

    // Load CC1101 settings
    EEPROM.get(CC1101_FREQ_EEPROM_ADDR, currentFrequency);
    EEPROM.get(CC1101_BANDWIDTH_EEPROM_ADDR, currentBandwidth);
    EEPROM.get(CC1101_RATE_EEPROM_ADDR, currentDataRate);

    // Validate loaded CC1101 settings and revert to defaults if invalid
    if (!(currentFrequency >= 387.000f && currentFrequency <= 464.000f)) { // Also catches NaN
        Serial.print("Warning: Loaded CC1101 frequency ("); Serial.print(currentFrequency);
        Serial.println(") is invalid/out of range. Resetting to 433.92 MHz.");
        currentFrequency = 433.92f;
    }

    bool validLoadedBandwidth = false;
    // Note: currentBandwidth is a float, but represents discrete KHz values.
    // We compare it against the known allowed integer values.
    int allowedBandwidths[] = {58, 67, 81, 101, 116, 135, 162, 203, 232, 270, 325, 406, 464, 541, 650, 812};
    for (int bw_val : allowedBandwidths) {
        // Using a small tolerance for float comparison, though direct comparison might work
        // if values were saved precisely.
        if (abs(currentBandwidth - (float)bw_val) < 0.01f) {
            validLoadedBandwidth = true;
            break;
        }
    }
    if (!validLoadedBandwidth) {
        Serial.print("Warning: Loaded CC1101 Bandwidth ("); Serial.print(currentBandwidth);
        Serial.println(") is invalid. Resetting to 101 KHz.");
        currentBandwidth = 101.0f;
    }

    if (!(currentDataRate >= 0.6f && currentDataRate <= 600.0f)) { // Also catches NaN
        Serial.print("Warning: Loaded CC1101 data rate ("); Serial.print(currentDataRate);
        Serial.println(") is invalid/out of range. Resetting to 2.5 kBaud.");
        currentDataRate = 2.5f;
    }

    Serial.println("Configuration loaded from EEPROM.");
    Serial.print("  Loaded SSID: "); Serial.println(currentSSID);
    Serial.print("  Loaded CC1101 Freq: "); Serial.print(currentFrequency, 3); Serial.println(" MHz");    Serial.print("  Loaded CC1101 Bandwidth: "); Serial.print(currentBandwidth, 0); Serial.println(" KHz");    Serial.print("  Loaded CC1101 Data Rate: "); Serial.print(currentDataRate, 1); Serial.println(" kBaud"); return true;
  }
  Serial.println("No configuration found in EEPROM.");
  return false;
}

void saveConfiguration() {
  Serial.println("Saving configuration to EEPROM...");
  for (unsigned int i = 0; i < WIFI_SSID_MAX_LEN; ++i) {
    EEPROM.write(WIFI_SSID_EEPROM_ADDR + i, (i < currentSSID.length()) ? currentSSID.charAt(i) : 0);
  }
  EEPROM.write(WIFI_SSID_EEPROM_ADDR + WIFI_SSID_MAX_LEN, 0); // Null terminator

  for (unsigned int i = 0; i < WIFI_PASS_MAX_LEN; ++i) {
    EEPROM.write(WIFI_PASS_EEPROM_ADDR + i, (i < currentPassword.length()) ? currentPassword.charAt(i) : 0);
  }
  EEPROM.write(WIFI_PASS_EEPROM_ADDR + WIFI_PASS_MAX_LEN, 0); // Null terminator

  // Save CC1101 settings
  EEPROM.put(CC1101_FREQ_EEPROM_ADDR, currentFrequency);
  EEPROM.put(CC1101_BANDWIDTH_EEPROM_ADDR, currentBandwidth);
  EEPROM.put(CC1101_RATE_EEPROM_ADDR, currentDataRate);

  EEPROM.write(CONFIG_STORED_FLAG_ADDR, CONFIG_STORED_MAGIC_BYTE);

  if (EEPROM.commit()) {
    Serial.println("Configuration saved successfully.");
  } else {
    Serial.println("Error saving configuration to EEPROM.");
  }
}

void loadRfMappingsFromEEPROM() {
  byte flag = EEPROM.read(RF_MAPPINGS_STORED_FLAG_ADDR);
  rfActionMappings.clear(); // Clear existing vector

  if (flag == RF_MAPPINGS_STORED_MAGIC_BYTE) {
    EEPROM.get(NEXT_RF_MAPPING_ID_EEPROM_ADDR, nextRfMappingId);
    uint16_t numMappingsStored = 0;
    EEPROM.get(NUM_RF_MAPPINGS_EEPROM_ADDR, numMappingsStored);

    Serial.printf("Found %u RF Action Mappings in EEPROM. Next ID: %u\n", numMappingsStored, nextRfMappingId);
    rfActionMappings.reserve(numMappingsStored); // Pre-allocate memory

    for (uint16_t i = 0; i < numMappingsStored; ++i) {
      RfActionMapping mapping;
      EEPROM.get(RF_MAPPINGS_DATA_START_ADDR + (i * sizeof(RfActionMapping)), mapping);
      rfActionMappings.push_back(mapping);
    }
    Serial.println("Finished loading RF Action Mappings from EEPROM.");
  } else {
    Serial.println("No RF Action Mappings found in EEPROM, or EEPROM uninitialized.");
    // Vector is already empty.
    nextRfMappingId = 1; // Reset ID counter if no mappings found
  }
}

bool saveRfMappingsToEEPROM() {
  EEPROM.write(RF_MAPPINGS_STORED_FLAG_ADDR, RF_MAPPINGS_STORED_MAGIC_BYTE);
  EEPROM.put(NEXT_RF_MAPPING_ID_EEPROM_ADDR, nextRfMappingId);
  EEPROM.put(NUM_RF_MAPPINGS_EEPROM_ADDR, (uint16_t)rfActionMappings.size());
  for (uint16_t i = 0; i < rfActionMappings.size(); ++i) {
    EEPROM.put(RF_MAPPINGS_DATA_START_ADDR + (i * sizeof(RfActionMapping)), rfActionMappings[i]);
  }

  if (EEPROM.commit()) {
    Serial.println("Successfully saved RF Action Mappings and config to EEPROM.");
    return true;
  } else {
    Serial.println("Error saving RF Action Mappings to EEPROM.");
    return false;
  }
}

void parseAndAddHeader(HTTPClient& http, const char* headerLine) {
    if (strlen(headerLine) > 0) {
        String hL(headerLine);
        int cI = hL.indexOf(':');
        if (cI > 0) {
            String hN = hL.substring(0, cI);
            String hV = hL.substring(cI + 1);
            hN.trim();
            hV.trim();
            if (hN.length() > 0 && hV.length() > 0) {
                http.addHeader(hN, hV);
                Serial.printf("      Added Header: '%s: %s'\n", hN.c_str(), hV.c_str());
            }
        }
    }
}

void executeConfiguredAction(RfActionMapping& mapping) { // Changed to non-const reference
    Serial.printf("Executing action for Mapping ID %u: Type %d\n", mapping.id, (int)mapping.actionType);

    if (mapping.actionType == ACTION_RPC_TOGGLE || mapping.actionType == ACTION_RPC_ON || mapping.actionType == ACTION_RPC_OFF || mapping.actionType == ACTION_HTTP) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi Disconnected. Cannot execute network-dependent action.");
            broadcastAlert("rfControlPane", "WiFi Disconnected. Action for mapping ID " + String(mapping.id) + " not executed.", "danger", 5000);
            return;
        }
        if ((mapping.actionType >= ACTION_RPC_TOGGLE && mapping.actionType <= ACTION_RPC_OFF) && strlen(mapping.RPC_IP) == 0) {
            Serial.printf("Device IP not configured for mapping ID %u. Cannot execute RPC action.\n", mapping.id);
            broadcastAlert("rfControlPane", "Device IP not configured for mapping ID " + String(mapping.id) + ". Action not executed.", "warning", 5000);
            return;
        }
    }

    switch (mapping.actionType) {
        case ACTION_RPC_TOGGLE:
            sendDeviceToggleCommand(mapping.RPC_IP, mapping.RPC_SwitchId, mapping.id);
            break;
        case ACTION_RPC_ON:
            sendDeviceSetCommand(mapping.RPC_IP, mapping.RPC_SwitchId, true, mapping.id);
            break;
        case ACTION_RPC_OFF:
            sendDeviceSetCommand(mapping.RPC_IP, mapping.RPC_SwitchId, false, mapping.id);
            break;
        case ACTION_HTTP:
            Serial.printf("  Action: HTTP Chain with %d steps.\n", mapping.numHttpSteps);
            if (mapping.numHttpSteps == 0) {
                broadcastAlert("rfControlPane", "HTTP Chain action for mapping ID " + String(mapping.id) + " has no steps configured.", "warning", 5000);
                break;
            }
            for (uint8_t i = 0; i < mapping.numHttpSteps && i < MAX_HTTP_STEPS_PER_MAPPING; ++i) {
                HttpStep& step = mapping.httpSteps[i]; // Get a reference to modify lastHttpStatusCode
                if (strlen(step.url) == 0) {
                    Serial.printf("    Step %d: URL is empty. Skipping.\n", i + 1);
                    broadcastAlert("rfControlPane", "HTTP Chain step " + String(i+1) + " for mapping ID " + String(mapping.id) + " has empty URL. Skipped.", "warning", 5000);
                    continue;
                }

                HTTPClient http;
                String urlStr = String(step.url);

                if (urlStr.startsWith("https")) {
                    // For HTTPS, we use WiFiClientSecure.
                    // setInsecure() is used to bypass certificate validation.
                    // WARNING: This is insecure and makes the connection vulnerable to man-in-the-middle attacks.
                    // For production environments, you should implement certificate validation.
                    std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
                    client->setInsecure();
                    // The ESP8266 has limited RAM. The default TLS buffer sizes can be too large,
                    // causing an Out-Of-Memory (OOM) crash. Reducing them is crucial for stability.
                    // 512 bytes for both RX and TX is a common and effective setting.
                    client->setBufferSizes(512, 512);
                    http.begin(*client, urlStr);
                } else {
                    // For standard HTTP
                    WiFiClient client;
                    http.begin(client, urlStr);
                }

                Serial.printf("    Executing Step %d: %s to %s\n", i + 1, (step.method == HTTP_METHOD_GET ? "GET" : "POST"), step.url);
                broadcastAlert("rfControlPane", "HTTP Chain: Step " + String(i+1) + "/" + String(mapping.numHttpSteps) + " (" + (step.method == HTTP_METHOD_GET ? "GET" : "POST") + " to " + String(step.url) + ") for mapping ID " + String(mapping.id) + "...", "info", 0);

                int httpCode = 0;
                parseAndAddHeader(http, step.headers);

                if (step.method == HTTP_METHOD_GET) {
                    httpCode = http.GET();
                } else { // POST
                    if (strlen(step.jsonData) > 0 && strstr(step.headers, "Content-Type") == nullptr) {
                        http.addHeader("Content-Type", "application/json"); // Default to JSON if data exists and no C-T header
                    }
                    httpCode = http.POST(String(step.jsonData));
                }
                step.lastHttpStatusCode = httpCode;

                if (httpCode > 0) {
                    String responsePayload = http.getString();
                    Serial.printf("      HTTP Step %d response code: %d\n      Response: %s\n", i + 1, httpCode, responsePayload.c_str());
                    broadcastAlert("rfControlPane", "HTTP Chain Step " + String(i+1) + " for mapping ID " + String(mapping.id) + " - Code: " + String(httpCode), (httpCode >= 200 && httpCode < 300) ? "success" : "warning", 5000);
                } else {
                    Serial.printf("      HTTP Step %d failed, error: %s\n", i + 1, http.errorToString(httpCode).c_str());
                    broadcastAlert("rfControlPane", "HTTP Chain Step " + String(i+1) + " for mapping ID " + String(mapping.id) + " failed: " + http.errorToString(httpCode), "danger", 5000);
                    break; 
                }
                http.end();
                yield(); // Allow other tasks to run between steps
            }
            break;
        case ACTION_NONE:
        default:
            Serial.println("  Action: None or unknown.");
            broadcastAlert("rfControlPane", "Action type 'None' or unknown for mapping ID " + String(mapping.id) + ".", "info", 3000);
            break;
    }
    // After action execution, especially if it might change state like lastHttpStatusCode
    broadcastStatusUpdate(); 
}

void executeActionForRfCode(unsigned long rfCode) {
    int foundMappingIndex = -1;
    for (size_t i = 0; i < rfActionMappings.size(); ++i) {
        // For RF-triggered actions, rfCode must be set and non-zero.
        if (rfActionMappings[i].enabled && rfActionMappings[i].rfCode != 0 && rfActionMappings[i].rfCode == rfCode) {
            foundMappingIndex = i;
            break;
        }
    }

    if (foundMappingIndex == -1) {
        return;
    }
    
    // Call the new function
    executeConfiguredAction(rfActionMappings[foundMappingIndex]);
}

void printHelp() {
  Serial.println("\nAvailable commands:");
  Serial.println("  send <rf_code> [repeats] - Transmits EV1527 (24-bit, Protocol 1) RF code.");
  Serial.println("  wifi setup - Initiates interactive WiFi setup.");
  Serial.println("  set freq <frequency_mhz> - Sets CC1101 Frequency (387.000-464.000 MHz).");
  Serial.println("  set bandwidth <bw_khz> - Sets CC1101 Bandwidth (e.g., 101).");
  Serial.println("  set rate <data_rate_kbaud> - Sets CC1101 Data Rate (0.6-600.0 kBaud).");
  Serial.println("  help  - Displays this help message.");
  Serial.println();
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.equalsIgnoreCase("help")) {
      printHelp();
    } else if (command.equalsIgnoreCase("wifi setup")) {
      interactiveConfigSetup();
    } else if (command.length() > 0) { // Potential send command or unknown
      String commandCopy = command; // Work with a copy for modification
      commandCopy.toLowerCase(); // Convert to lowercase in-place for command matching

      if (commandCopy.startsWith("send ")) {
        String args = command.substring(5); // Get arguments after "send "
        args.trim();

        unsigned long codeToSend = 0;
        const unsigned int bitLength = 24;    // Fixed for EV1527
        const unsigned int protocol = 1;      // Fixed for EV1527
        unsigned int repeats = 200;      // Updated Default

        // Parse arguments: code [repeats]
        String part;
        int spaceIdx;

        // 1. RF Code (mandatory)
        spaceIdx = args.indexOf(' ');
        if (spaceIdx == -1) { part = args; args = ""; }
        else { part = args.substring(0, spaceIdx); args = args.substring(spaceIdx + 1); args.trim(); }
        
        if (part.length() > 0) {
          codeToSend = strtoul(part.c_str(), nullptr, 10);
        }
        if (codeToSend == 0) {
          Serial.println("Send command error: RF code is missing or invalid (cannot be 0).");
          Serial.println("Usage: send <rf_code> [repeats]");
          return; // Exit if code is invalid
        }
        
        // 2. Repeats (optional)
        if (args.length() > 0) {
          // The rest of the string (if any) is repeats
          part = args;
          if (part.length() > 0) repeats = atoi(part.c_str());
          if (repeats == 0 && part != "0") { // If atoi failed (and input wasn't "0") or input was invalid
            repeats = 200; // Reset to default
          } else if (repeats == 0 && part == "0") { // User explicitly sent 0, which is invalid for repeats
            repeats = 1; // Or handle as an error, but for now, let's set to a minimum sensible value
          }
        }

        Serial.print("Preparing to send RF Code: "); Serial.println(codeToSend);
        Serial.print("  Bit Length: "); Serial.println(bitLength); // Fixed
        Serial.print("  Protocol: "); Serial.println(protocol);   // Fixed
        Serial.print("  Pulse Length: Using protocol default");
        Serial.print("  Repeats: "); Serial.println(repeats);

        // --- CC1101 TX Sequence ---
        mySwitch.disableReceive();

        ELECHOUSE_cc1101.setPktFormat(3);
        ELECHOUSE_cc1101.setGDO0(RF_DATA_PIN); // Configure GDO0 for TX data (actual CC1101 register config for GDO0 function is also needed if this pin is CC1101's GDO0)
        ELECHOUSE_cc1101.setPA(10);          // Set TX power
        ELECHOUSE_cc1101.SetTx(); // Put CC1101 into TX state
        currentCC1101Mode = "TX"; // Update global

        mySwitch.enableTransmit(RF_DATA_PIN);
        mySwitch.setProtocol(protocol);       // Uses default pulse length for protocol 1

        Serial.println("CC1101 configured for Transmitting Data. Sending repeats manually...");

        for (unsigned int i = 0; i < repeats; ++i) {
            mySwitch.send(codeToSend, bitLength); // Send the code one time
            // Serial.print("."); // Optional: print a dot for each transmission for visual feedback
            yield(); // Crucial: Allow ESP8266 background tasks and WDT reset
        }
        Serial.println("\nAll RF Signal repeats sent via Serial.");

        // --- Reset to Receive Mode ---
        mySwitch.disableTransmit(); // Release the transmit pin
        initialize_cc1101();        // Reinitialize CC1101 for receive
        mySwitch.enableReceive(RF_DATA_PIN);
        Serial.println("Transmission complete. Receiver re-enabled.");

        broadcastStatusUpdate();
      } else if (commandCopy.startsWith("set deviceip ")) {
        Serial.println("Global 'set deviceip' command is deprecated. Configure Device IP per RF mapping via UI.");
      } else if (commandCopy.startsWith("set deviceid ")) {
        Serial.println("Global 'set deviceid' command is deprecated. Configure Device Switch ID per RF mapping via UI.");
      } else if (commandCopy.startsWith("set freq ")) {
        String freqStr = command.substring(9); // "set freq ".length() is 9
        freqStr.trim();
        float newFrequency = freqStr.toFloat();

        // Validation: Frequency 387.000 to 464.000 MHz
        if (newFrequency >= 387.000f && newFrequency <= 464.000f) {
            currentFrequency = newFrequency;
            initialize_cc1101(); // Reinitialize with new frequency
            Serial.print("CC1101 Frequency updated via Serial to: "); Serial.print(currentFrequency, 3); Serial.println(" MHz.");
            saveConfiguration(); 
            broadcastAlert("cc1101ConfigPane", "CC1101 Frequency updated via Serial to: " + String(currentFrequency, 3) + " MHz.", "success", 5000);
            broadcastStatusUpdate();
        } else {
            Serial.print("Invalid Frequency '"); Serial.print(freqStr); Serial.println("'. Must be between 387.000 and 464.000 MHz.");
            broadcastAlert("cc1101ConfigPane", "Invalid Frequency '" + escapeJsonString(freqStr) + "'. Must be between 387.000 and 464.000 MHz.", "danger", 5000);
        }
      } else if (commandCopy.startsWith("set bandwidth ")) {
        String bwStr = command.substring(14); // "set bandwidth ".length() is 14
        bwStr.trim();
        int newBandwidthInt = bwStr.toInt();

        // Validation: Check against allowed values
        bool validBandwidth = false;
        int allowedBandwidths[] = {58, 67, 81, 101, 116, 135, 162, 203, 232, 270, 325, 406, 464, 541, 650, 812};
        for (int bw : allowedBandwidths) {
          if (newBandwidthInt == bw) {
            validBandwidth = true;
            break;
          }
        }

        if (validBandwidth) {
            currentBandwidth = (float)newBandwidthInt; // Store as float
            initialize_cc1101(); // Reinitialize with new RX BW
            Serial.print("CC1101 Bandwidth updated via Serial to: "); Serial.print(currentBandwidth, 0); Serial.println(" KHz.");            saveConfiguration(); 
            broadcastAlert("cc1101ConfigPane", "CC1101 Bandwidth updated via Serial to: " + String(currentBandwidth, 0) + " KHz.", "success", 5000);
            broadcastStatusUpdate();
        } else {
            Serial.print("Invalid Bandwidth '"); Serial.print(bwStr); Serial.println("'. Select from allowed values (58, 67, 81, 101, 116, 135, 162, 203, 232, 270, 325, 406, 464, 541, 650, 812).");
            broadcastAlert("cc1101ConfigPane", "Invalid Bandwidth '" + escapeJsonString(bwStr) + "'. Select from allowed values.", "danger", 5000);
        }
      } else if (commandCopy.startsWith("set rate ")) {
        String rateStr = command.substring(9); // "set rate ".length() is 9
        rateStr.trim();
        float newDataRate = rateStr.toFloat();

        // Validation: Data Rate 0.6 to 600.0 kBaud
        if (newDataRate >= 0.6f && newDataRate <= 600.0f) {
            currentDataRate = newDataRate;
            initialize_cc1101(); // Reinitialize with new data rate
            Serial.print("CC1101 Data Rate updated via Serial to: "); Serial.print(currentDataRate, 1); Serial.println(" kBaud.");
            saveConfiguration(); 
            broadcastAlert("cc1101ConfigPane", "CC1101 Data Rate updated via Serial to: " + String(currentDataRate, 1) + " kBaud.", "success", 5000);
            broadcastStatusUpdate();
        } else {
            Serial.print("Invalid Data Rate '"); Serial.print(rateStr); Serial.println("'. Must be between 0.6 and 600.0 kBaud.");
            broadcastAlert("cc1101ConfigPane", "Invalid Data Rate '" + escapeJsonString(rateStr) + "'. Must be between 0.6 and 600.0 kBaud.", "danger", 5000);
        }
      } else { 
        Serial.print("Unknown command: ");
        Serial.println(command);
        printHelp();
      }
    } else {
      // Empty command, do nothing
    }
    // Clear remaining serial buffer
    while(Serial.available() > 0) { Serial.read(); }
  }
}

// Helper to escape strings for JSON
String escapeJsonString(const String& str) {
  String escapedStr = str;
  escapedStr.replace("\\", "\\\\"); // Must be first
  escapedStr.replace("\"", "\\\"");
  return escapedStr;
}

// --- Web Server Handlers ---
void handleRoot() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  // Send the entire page from the single PROGMEM variable in web_page.h
  server.send_P(200, "text/html", PAGE_HTML);
}

void handleNotFound(){
  server.send(404, "text/plain", "404: Not Found");
}

// --- WebSocket Helper Functions ---
void sendFullStatusToClient(uint8_t clientNum) {
  String wifi_connected_html;
  String wifi_ip_html = "<span class='status-badge badge-muted'>N/A</span>";
  String wifi_mac_html = "<span class='status-badge badge-muted'>N/A</span>";
  String wifi_hostname_html = "<span class='status-badge badge-muted'>N/A</span>";

  if (WiFi.status() == WL_CONNECTED) {
    wifi_connected_html = "<span class='status-badge badge-success'>Connected</span>";
    wifi_ip_html = "<span class='status-badge badge-muted'>" + WiFi.localIP().toString() + "</span>";
    wifi_mac_html = "<span class='status-badge badge-muted'>" + WiFi.macAddress() + "</span>";
    wifi_hostname_html = "<span class='status-badge badge-muted'>" + WiFi.hostname() + "</span>";
  } else {
    wifi_connected_html = "<span class='status-badge badge-danger'>Disconnected</span>";
  }

  String device_relay_status_html;
  if (RPCRelayStateKnown) {
    device_relay_status_html = currentRPCRelayIsOn ?
        "<span class='status-badge badge-success'>ON</span>" : 
        "<span class='status-badge badge-danger'>OFF</span>";
  } else {
    device_relay_status_html = "<span class='status-badge badge-muted'>Unknown</span>";
  }
  
  String cc1101_freq_html = "<span class='status-badge badge-muted'>" + String(currentFrequency, 2) + " MHz</span>";
  String cc1101_bandwidth_html = "<span class='status-badge badge-muted'>" + String(currentBandwidth, 0) + " KHz</span>";
  String cc1101_rate_html = "<span class='status-badge badge-muted'>" + String(currentDataRate, 1) + " kBaud</span>";
  String cc1101_mode_html = (currentCC1101Mode == "RX") ? "<span class='status-badge badge-success'>RX</span>" : 
                           (currentCC1101Mode == "TX") ? "<span class='status-badge badge-warning'>TX</span>" :
                                                         "<span class='status-badge badge-muted'>" + currentCC1101Mode + "</span>";

  String jsonPayload = "{";
  jsonPayload += "\"wifi-connected-val\":\"" + escapeJsonString(wifi_connected_html) + "\",";
  jsonPayload += "\"wifi-ip-val\":\"" + escapeJsonString(wifi_ip_html) + "\",";
  jsonPayload += "\"wifi-mac-val\":\"" + escapeJsonString(wifi_mac_html) + "\",";
  jsonPayload += "\"wifi-hostname-val\":\"" + escapeJsonString(wifi_hostname_html) + "\",";
  jsonPayload += "\"cc1101-freq-val\":\"" + escapeJsonString(cc1101_freq_html) + "\",";
  jsonPayload += "\"cc1101-bandwidth-val\":\"" + escapeJsonString(cc1101_bandwidth_html) + "\",";
  jsonPayload += "\"cc1101-rate-val\":\"" + escapeJsonString(cc1101_rate_html) + "\",";
  jsonPayload += "\"cc1101-mode-val\":\"" + escapeJsonString(cc1101_mode_html) + "\",";
  jsonPayload += "\"cc1101Frequency_form_val\":\"" + String(currentFrequency, 3) + "\",";
  jsonPayload += "\"cc1101Bandwidth_form_val\":\"" + String(currentBandwidth, 0) + "\",";
  jsonPayload += "\"cc1101DataRate_form_val\":\"" + String(currentDataRate, 1) + "\",";
  jsonPayload += "\"device-relay-status-val\":\"" + escapeJsonString(device_relay_status_html) + "\",";
  jsonPayload += "\"device_switch_id_form_val\":\"0\""; // Default for modal if no specific mapping is loaded, or consider removing if not used globally

  // Add RF Mappings to payload
  jsonPayload += ",\"rf_mappings\":[";
  for (size_t i = 0; i < rfActionMappings.size(); ++i) {
    if (i > 0) jsonPayload += ",";
    const RfActionMapping& mapping = rfActionMappings[i];
    jsonPayload += "{";
    jsonPayload += "\"id\":" + String(mapping.id) + ","; 
    jsonPayload += "\"rf_code\":" + String(mapping.rfCode) + ",";
    jsonPayload += "\"action_type\":" + String((int)mapping.actionType) + ",";
    jsonPayload += "\"RPC_ip\":\"" + escapeJsonString(String(mapping.RPC_IP)) + "\",";
    jsonPayload += "\"RPC_switch_id\":" + String(mapping.RPC_SwitchId) + ",";
    jsonPayload += "\"num_http_steps\":" + String(mapping.numHttpSteps) + ",";
    jsonPayload += "\"http_steps\":["; // Start of http_steps array
    for (uint8_t j = 0; j < mapping.numHttpSteps && j < MAX_HTTP_STEPS_PER_MAPPING; ++j) {
        if (j > 0) jsonPayload += ",";
        const HttpStep& step = mapping.httpSteps[j];
        jsonPayload += "{";
        jsonPayload += "\"method\":" + String(step.method) + ",";
        jsonPayload += "\"url\":\"" + escapeJsonString(String(step.url)) + "\",";
        jsonPayload += "\"headers\":\"" + escapeJsonString(String(step.headers)) + "\",";
        jsonPayload += "\"jsonData\":\"" + escapeJsonString(String(step.jsonData)) + "\",";
        jsonPayload += "\"lastHttpStatusCode\":" + String(step.lastHttpStatusCode);
        jsonPayload += "}";
    }
    jsonPayload += "],"; // End of http_steps array
    jsonPayload += "\"enabled\":" + String(mapping.enabled ? "true" : "false");
    jsonPayload += "}";
  }
  jsonPayload += "]";
  jsonPayload += "}";

  String fullJsonResponse = "{\"type\":\"statusUpdate\", \"payload\":" + jsonPayload + "}";
  webSocket.sendTXT(clientNum, fullJsonResponse);
}

void broadcastStatusUpdate() {
  for (uint8_t i = 0; i < webSocket.connectedClients(); i++) {
    if (webSocket.clientIsConnected(i)) {
      sendFullStatusToClient(i);
    }
  }
}

void broadcastAlert(const String& paneId, const String& message, const String& alertType, int duration /*= 5000*/) {
  String jsonResponse = "{\"type\":\"alert\", \"payload\":{\"paneId\":\"" + escapeJsonString(paneId) + 
                        "\", \"message\":\"" + escapeJsonString(message) + 
                        "\", \"alertType\":\"" + escapeJsonString(alertType) +
                        "\", \"duration\":" + String(duration) + 
                        "}}";
  webSocket.broadcastTXT(jsonResponse);
}

void loop() {
  server.handleClient();
  webSocket.loop();
  handleSerialCommands();

  // RPC Command Timeout Check
  if (DeviceCmdWaitingForResponse && (millis() - DeviceCmdSentTime > RPC_CMD_TIMEOUT_MS)) {
    Serial.printf("[RPC] Command (MsgID %u, MappingID %u) timed out.\n", DeviceCmdIdSent, DeviceCmdOriginatingMappingId);
    broadcastAlert("rfControlPane", "RPC command for Mapping ID " + String(DeviceCmdOriginatingMappingId) + " (MsgID " + String(DeviceCmdIdSent) + ") timed out.", "warning", 5000);
    DeviceCmdWaitingForResponse = false; // Stop waiting for this specific command
    // Consecutive failure tracking removed.
    // Update the specific mapping's status to indicate timeout
    if (DeviceCmdOriginatingMappingId != 0) {
        for (size_t i = 0; i < rfActionMappings.size(); ++i) {
            if (rfActionMappings[i].id == DeviceCmdOriginatingMappingId) {
                // lastHttpStatusCode is not directly on RfActionMapping for RPC actions.
                // The alert already informs the user. If a specific status needs to be stored for the mapping,
                // a new member or different handling would be required. For now, rely on the alert.
                break;
            }
        }
    }
  }


  // Check for learn mode timeout
  if (learnModeForMappingIndex != -1 && (millis() - learnModeStartTime > LEARN_MODE_TIMEOUT_MS)) {
    Serial.printf("LEARN MODE TIMEOUT for mapping ID %d. No RF signal received.\n", learnModeForMappingIndex);
    broadcastAlert("rfControlPane", "Learn mode timed out for mapping ID " + String(learnModeForMappingIndex) + ".", "warning", 5000);
    learnModeForMappingIndex = -1; // Exit learn mode
    broadcastStatusUpdate(); // Update UI to reflect learn mode ended
  }

  if (mySwitch.available()) {
    unsigned long receivedCode = mySwitch.getReceivedValue();
    unsigned int receivedBitlength = mySwitch.getReceivedBitlength();
    unsigned int receivedProtocol = mySwitch.getReceivedProtocol();


    Serial.print("Received: ");
    Serial.print(receivedCode);
    Serial.print(" / Bitlength: ");
    Serial.print(receivedBitlength);
    Serial.print(" / Protocol: ");
    Serial.println(receivedProtocol);

    if (learnModeForMappingIndex != -1) { // If in learn mode for a specific mapping
      // learnModeForMappingIndex now stores the 'id' of the mapping to learn for.
      if (receivedCode != 0) { // Ensure a valid code is received
        bool foundAndUpdated = false;
        for (size_t i = 0; i < rfActionMappings.size(); ++i) {
          if (rfActionMappings[i].id == (uint16_t)learnModeForMappingIndex) {
            rfActionMappings[i].rfCode = receivedCode;
            // rfActionMappings[i].enabled = true; // Do not auto-enable
            if (saveRfMappingsToEEPROM()) {
                Serial.printf("LEARN MODE DEACTIVATED. New code %lu saved to mapping ID %d.\n", receivedCode, learnModeForMappingIndex);
                broadcastAlert("rfControlPane", "New RF Code (" + String(receivedCode) + ") learned for mapping ID " + String(learnModeForMappingIndex) + " and saved!", "success", 5000);
            } else {
                broadcastAlert("rfControlPane", "Error saving newly learned RF Code for mapping ID " + String(learnModeForMappingIndex) + ".", "danger", 5000);
            }
            foundAndUpdated = true;
            break;
          }
        }
        if (!foundAndUpdated) {
            Serial.printf("Learn mode: Mapping ID %d not found to save RF code.\n", learnModeForMappingIndex);
            broadcastAlert("rfControlPane", "Learn mode error: Mapping ID " + String(learnModeForMappingIndex) + " not found.", "danger", 5000);
        }
        // learnModeForMappingIndex is set to -1 after this block
        // broadcastStatusUpdate() will update the UI
        mySwitch.resetAvailable(); // Mark this specific signal as handled
        learnModeForMappingIndex = -1;
        broadcastStatusUpdate(); // Update UI
        return; 
      } else {
        Serial.printf("Received invalid code (0) during learn mode for mapping ID %d. Still in learn mode.\n", learnModeForMappingIndex);
      }
    } else { // Normal operation mode
      // Iterate through dynamic list to find a match
      for (size_t i = 0; i < rfActionMappings.size(); ++i) {
        if (rfActionMappings[i].enabled && rfActionMappings[i].rfCode != 0 && rfActionMappings[i].rfCode == receivedCode) {
          Serial.printf("Matched RF code %lu from mapping ID %u.\n", receivedCode, rfActionMappings[i].id);
          broadcastAlert("rfControlPane", "Matched RF code (" + String(receivedCode) + ") from mapping ID " + String(rfActionMappings[i].id) + ".", "info", 3000);
          executeActionForRfCode(receivedCode); // Pass RF code to find the mapping again or pass mapping.id
          break; // Found a match, execute and stop checking
        }
      }
    }
    mySwitch.resetAvailable();
  }

  // The global RPC.loop() and connectToDevice() calls in loop() are
  // problematic without a single global device IP. For now, RPC actions will attempt to connect on-demand.
  // A more robust solution would involve managing multiple client connections or a single dynamic one.
  RPC.loop(); // Still need to process events if a connection was manually established by an action
  yield(); // Allow ESP8266 background tasks to run
}

void requestDeviceSwitchStatus() {
    // This function also needs a target IP and switch ID for a specific device.
    // It's not suitable for a generic global status request anymore.
    if (!isDeviceConnected) {
        Serial.println("Not connected to RPC device. Cannot request switch status.");
        // Optionally try to connect: connectToDevice();
        return;
    }

    JsonDocument doc;
    unsigned int currentCmdId = DeviceMsgId++; // Capture ID before incrementing for next use
    doc["id"] = currentCmdId;
    doc["method"] = "Switch.GetStatus";
    doc["params"]["id"] = 0; // Placeholder, this needs to be dynamic based on context or removed if not used globally

    String rpcMessage;
    serializeJson(doc, rpcMessage);
    Serial.print("Requesting Device Switch Status (ID: "); Serial.print(currentCmdId); Serial.print("): "); Serial.println(rpcMessage);
    
    // Set up for timeout tracking
    DeviceCmdIdSent = currentCmdId;
    DeviceCmdSentTime = millis();
    // DeviceCmdOriginatingMappingId is not set here as this is a general status request, not tied to a specific mapping.
    DeviceCmdWaitingForResponse = true;
    
    RPC.sendTXT(rpcMessage);
}

void sendRpcCommand(const JsonDocument& doc, int switchId, uint16_t originatingMappingId, const String& commandType) {
    String rpcMessage;
    serializeJson(doc, rpcMessage);

    unsigned int cmdId = doc["id"];

    Serial.print("Sending RPC " + commandType + " (MsgID: "); Serial.print(cmdId); Serial.print(", MappingID: "); Serial.print(originatingMappingId); Serial.print(" for switch "); Serial.print(switchId); Serial.print("): ");
    Serial.println(rpcMessage);

    // Set up for timeout tracking
    DeviceCmdIdSent = cmdId;
    DeviceCmdSentTime = millis();
    DeviceCmdOriginatingMappingId = originatingMappingId;
    DeviceCmdWaitingForResponse = true;

    RPC.sendTXT(rpcMessage);
    broadcastAlert("rfControlPane", "RPC " + commandType + " for Mapping ID " + String(originatingMappingId) + " (Switch " + String(switchId) + ") sent. Waiting...", "info", 0);
}

void sendDeviceToggleCommand(const char* deviceIp, int switchId, uint16_t originatingMappingId) {
    if (!isDeviceConnected) {
        Serial.printf("Attempting Toggle for %s (ID %d) - RPC not connected. Action may fail.\n", deviceIp, switchId);
    }
    JsonDocument doc;
    doc["id"] = DeviceMsgId++;
    doc["method"] = "Switch.Toggle";
    doc["params"]["id"] = switchId;

    sendRpcCommand(doc, switchId, originatingMappingId, "Toggle");
}

void sendDeviceSetCommand(const char* deviceIp, int switchId, bool turnOn, uint16_t originatingMappingId) {
    if (!isDeviceConnected) {
        Serial.printf("Attempting Set for %s (ID %d) - RPC not connected. Action may fail.\n", deviceIp, switchId);
    }

    JsonDocument doc;
    doc["id"] = DeviceMsgId++;
    doc["method"] = "Switch.Set";
    JsonObject params = doc["params"].to<JsonObject>();
    params["id"] = switchId;
    params["on"] = turnOn;

    sendRpcCommand(doc, switchId, originatingMappingId, "Set");
}

void RPCEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            isDeviceConnected = false;
            Serial.println("[RPC] Disconnected!");
            // Consecutive failure tracking removed.
            break;
        case WStype_CONNECTED:
            isDeviceConnected = true;
            Serial.print("[RPC] Connected to: "); Serial.println((char*)payload);
            RPCRelayStateKnown = false; // State is unknown until GetStatus returns
            // Consecutive failure tracking removed.
            requestDeviceSwitchStatus(); // Request current status upon connection, this will set up timeout tracking
            break;
        case WStype_TEXT:
            // Some RPC devices send simple text-based pings. We handle them here to keep the log clean.
            if (strcmp((const char*)payload, "ping") == 0) {
                RPC.sendTXT("pong"); // Respond to the ping to keep the connection alive
                break;               // And don't log it
            }
            Serial.printf("[RPC] Got text: %s\n", payload);
            {
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, payload, length);
                if (error) {
                    Serial.printf("[RPC] JSON parse error: %s\n", error.c_str());
                    broadcastAlert("rfControlPane", "RPC: Error parsing response.", "danger", 5000);
                    break;
                }

                unsigned int responseId = doc["id"].as<unsigned int>(); // Get response ID

                if (DeviceCmdWaitingForResponse && responseId == DeviceCmdIdSent) {
                    Serial.printf("[RPC] Received response for command ID %u.\n", responseId);
                    DeviceCmdWaitingForResponse = false; // Clear waiting flag
                    // Consecutive failure tracking removed.
                    // If this command was from a mapping, clear its timeout status
                    if (DeviceCmdOriginatingMappingId != 0) {
                        for (size_t i = 0; i < rfActionMappings.size(); ++i) {
                            if (rfActionMappings[i].id == DeviceCmdOriginatingMappingId) {
                                // lastHttpStatusCode is not directly on RfActionMapping for RPC actions.
                                // Clearing a timeout status would be handled differently if needed.
                                // broadcastStatusUpdate() will be called later if relay state changes
                                break;
                            }
                        }
                    }
                    // Consecutive failure tracking removed.
                } else if (DeviceCmdWaitingForResponse) {
                    Serial.printf("[RPC] Received response ID %u, but was waiting for ID %u (or not waiting).\n", responseId, DeviceCmdIdSent);
                    // This could be an old/unexpected response. Don't clear DeviceCmdWaitingForResponse for the current outstanding command.
                }

                if (doc["error"].is<JsonObject>()) { // Updated check
                    String errorMsg = doc["error"]["message"].as<String>();
                    broadcastAlert("rfControlPane", "RPC Error: " + escapeJsonString(errorMsg), "danger", 5000);
                } else if (doc["result"].is<JsonObject>()) { // Updated check
                    JsonObject result = doc["result"];
                    // Check if it's a response to Switch.Toggle (has "was_on")
                    if (result["was_on"].is<bool>()) { // Updated check
                        bool was_on = result["was_on"].as<bool>();
                        currentRPCRelayIsOn = !was_on;
                        RPCRelayStateKnown = true;
                        int toggledId = result["id"].is<int>() ? result["id"].as<int>() : -1; // Assuming "id" is in the result for Toggle
                        broadcastAlert("rfControlPane", "Device relay (ID " + String(toggledId) + ") toggled. New state: " + String(currentRPCRelayIsOn ? "ON" : "OFF"), "success", 5000);
                        broadcastStatusUpdate();
                    }
                    else if (result["ison"].is<bool>() || result["output"].is<bool>()) { 
                        currentRPCRelayIsOn = result["ison"].is<bool>() ? result["ison"].as<bool>() : result["output"].as<bool>();
                        RPCRelayStateKnown = true;
                        int statusId = result["id"].is<int>() ? result["id"].as<int>() : -1; // Assuming "id" is in the result for GetStatus
                        Serial.println("[RPC] Status updated for ID " + String(statusId) + ". State: " + String(currentRPCRelayIsOn ? "ON" : "OFF"));
                        broadcastStatusUpdate();
                    } else {
                        broadcastAlert("rfControlPane", "RPC command successful (unknown response type).", "success", 5000);
                    }
                }
            }
            break;
        case WStype_PONG:
            break;
        case WStype_ERROR: Serial.printf("[RPC] Error: %s\n", payload); isDeviceConnected = false; break;
        default: break;
    }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
            Serial.printf("[%u] Connected from %s url: %s\n", num, ip.toString().c_str(), payload);
            // Send current status to the newly connected client
            sendFullStatusToClient(num);

            // If the device was just rebooted by a client command, send a specific alert
            if (g_justRebootedFromClientCommand) {
                String reconnectedAlertJson = "{\"type\":\"alert\", \"payload\":{\"paneId\":\"systemMaintenancePane\","
                                         "\"message\":\"Device has reconnected.\",\"alertType\":\"success\",\"duration\":5000}}";
                if (webSocket.sendTXT(num, reconnectedAlertJson)) {
                    Serial.printf("[%u] Sent 'Device has reconnected.' alert.\n", num);
                } else {
                    Serial.printf("[%u] Failed to send 'Device has reconnected.' alert.\n", num);
                }
                g_justRebootedFromClientCommand = false; 
            }
            }
            break;
        case WStype_TEXT:
            { // Scope for StaticJsonDocument
                JsonDocument doc; // ArduinoJson v7+ uses JsonDocument
                DeserializationError error = deserializeJson(doc, payload, length);

                if (error) {
                    Serial.printf("[%u] deserializeJson() failed: %s\n", num, error.c_str());
                    broadcastAlert("global", "Invalid command format received.", "danger", 5000);
                    return;
                }

                const char* msgType = doc["type"];
                if (msgType && strcmp(msgType, "command") == 0) {
                    const char* action = doc["action"].as<const char*>();
                    JsonObject cmdPayload = doc["payload"]; // If no payload, cmdPayload.isNull() will be true
                    handleWebSocketCommand(num, action, cmdPayload);
                }
            }
            break;
        case WStype_BIN:
            Serial.printf("[%u] get binary length: %u\n", num, length);
            break;
        case WStype_PING:
            // Library automatically sends PONG
            break;
        case WStype_PONG:
            break;
        case WStype_ERROR:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
        default:
            Serial.printf("[%u] Unknown WStype: %d\n", num, type);
            break;
    }
}

void handleWebSocketCommand(uint8_t clientNum, const char* action, JsonObject& payload) {
    if (!action) {
        broadcastAlert("global", "Command action missing.", "danger", 5000);
        return;
    }
    Serial.printf("[%u] Processing WebSocket command: %s\n", clientNum, action);

    if (strcmp(action, "learnForMappingIndex") == 0) {
        if (payload["index"].isNull() || !payload["index"].is<int>()) {
            broadcastAlert("rfControlPane", "Missing or invalid ID for learn command.", "danger", 5000); return;
        }
        int mapping_id_to_learn = payload["index"].as<int>();
        bool id_exists = false;
        for(const auto& map_item : rfActionMappings) {
            if (map_item.id == (uint16_t)mapping_id_to_learn) {
                id_exists = true;
                break;
            }
        }
        if (id_exists) {
            learnModeForMappingIndex = mapping_id_to_learn; // Store the actual ID
            learnModeStartTime = millis();
            Serial.printf("LEARN MODE ACTIVATED via WebSocket for mapping ID %d. Send RF signal.\n", mapping_id_to_learn);
            broadcastAlert("rfControlPane", "Learn mode activated for mapping ID " + String(mapping_id_to_learn) + ". Send RF signal now!", "info", 0); // Sticky
            broadcastStatusUpdate(); // Update UI to reflect learn mode active (if UI supports it)
        } else {
            broadcastAlert("rfControlPane", "Mapping ID " + String(mapping_id_to_learn) + " not found for learn command.", "danger", 5000);
        }
    } else if (strcmp(action, "addRfMapping") == 0 || strcmp(action, "editRfMapping") == 0) {
        bool isEdit = strcmp(action, "editRfMapping") == 0;
        int mapping_id = payload["id"].as<int>(); // Client sends 'id' which is mapping.id for edit, or -1 for add

        if (isEdit) {
            bool found = false;
            for (size_t i = 0; i < rfActionMappings.size(); ++i) {
                if (rfActionMappings[i].id == (uint16_t)mapping_id) {
                    rfActionMappings[i].actionType = (ActionType)payload["action_type"].as<int>();
                    strncpy(rfActionMappings[i].RPC_IP, payload["RPC_IP"].as<const char*>(), IP_MAX_LEN - 1);
                    rfActionMappings[i].RPC_IP[IP_MAX_LEN - 1] = '\0';
                    rfActionMappings[i].RPC_SwitchId = payload["RPC_SwitchId"].as<int>();
                    rfActionMappings[i].numHttpSteps = 0;
                    if (payload["http_steps"].is<JsonArray>()) {
                        JsonArray stepsArray = payload["http_steps"].as<JsonArray>();
                        uint8_t stepCount = 0;
                        for (JsonVariant step_v : stepsArray) {
                            if (stepCount >= MAX_HTTP_STEPS_PER_MAPPING) break;
                            JsonObject stepObj = step_v.as<JsonObject>();
                            rfActionMappings[i].httpSteps[stepCount].method = (HttpMethod)stepObj["method"].as<int>();
                            strncpy(rfActionMappings[i].httpSteps[stepCount].url, stepObj["url"].as<const char*>(), MAX_URL_LEN -1); rfActionMappings[i].httpSteps[stepCount].url[MAX_URL_LEN-1] = '\0';
                            strncpy(rfActionMappings[i].httpSteps[stepCount].headers, stepObj["headers"].as<const char*>(), MAX_HEADERS_LEN -1); rfActionMappings[i].httpSteps[stepCount].headers[MAX_HEADERS_LEN-1] = '\0';
                            strncpy(rfActionMappings[i].httpSteps[stepCount].jsonData, stepObj["jsonData"].as<const char*>(), MAX_JSON_DATA_LEN -1); rfActionMappings[i].httpSteps[stepCount].jsonData[MAX_JSON_DATA_LEN-1] = '\0';
                            stepCount++;
                        }
                        rfActionMappings[i].numHttpSteps = stepCount;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                broadcastAlert("rfControlPane", "Mapping ID " + String(mapping_id) + " not found for edit.", "danger", 5000);
                return;
            }
        } else { // Add new mapping
            if (rfActionMappings.size() >= MAX_SUPPORTED_RF_MAPPINGS) { 
                 broadcastAlert("rfControlPane", "Cannot add new mapping: Max " + String(MAX_SUPPORTED_RF_MAPPINGS) + " mappings supported.", "warning", 5000);
                 return;
            }

            // Find the lowest available ID
            uint16_t newIdToAssign = 1;
            bool idFound = false;
            if (rfActionMappings.empty()) {
                newIdToAssign = 1; // Start with 1 if empty
            } else {
                // Sort by ID to easily find gaps or the next sequential ID
                std::sort(rfActionMappings.begin(), rfActionMappings.end(), [](const RfActionMapping& a, const RfActionMapping& b) {
                    return a.id < b.id;
                });
                for (size_t i = 0; i < rfActionMappings.size(); ++i) {
                    if (rfActionMappings[i].id != (uint16_t)(i + 1)) {
                        newIdToAssign = i + 1;
                        idFound = true;
                        break;
                    }
                }
                if (!idFound) { // All IDs are sequential, use the next one
                    newIdToAssign = rfActionMappings.size() + 1;
                }
            }

            RfActionMapping newMapping;
            newMapping.id = newIdToAssign; 
            // Explicitly set actionType from payload, with a check
            if (payload["action_type"].is<int>()) {
                newMapping.actionType = (ActionType)payload["action_type"].as<int>();
            } else {
                newMapping.actionType = ACTION_NONE; // Fallback, though payload should always have it
                Serial.println("Warning: 'action_type' missing or not an int in addRfMapping payload.");
            }
            newMapping.rfCode = 0; // Learned separately
            
            newMapping.numHttpSteps = 0;
            if (payload["http_steps"].is<JsonArray>()) {
                JsonArray stepsArray = payload["http_steps"].as<JsonArray>();
                uint8_t stepCount = 0;
                for (JsonVariant step_v : stepsArray) {
                    if (stepCount >= MAX_HTTP_STEPS_PER_MAPPING) break;
                    JsonObject stepObj = step_v.as<JsonObject>();
                    newMapping.httpSteps[stepCount].method = (HttpMethod)stepObj["method"].as<int>();
                    strncpy(newMapping.httpSteps[stepCount].url, stepObj["url"].as<const char*>(), MAX_URL_LEN -1); newMapping.httpSteps[stepCount].url[MAX_URL_LEN-1] = '\0';
                    strncpy(newMapping.httpSteps[stepCount].headers, stepObj["headers"].as<const char*>(), MAX_HEADERS_LEN -1); newMapping.httpSteps[stepCount].headers[MAX_HEADERS_LEN-1] = '\0';
                    strncpy(newMapping.httpSteps[stepCount].jsonData, stepObj["jsonData"].as<const char*>(), MAX_JSON_DATA_LEN -1); newMapping.httpSteps[stepCount].jsonData[MAX_JSON_DATA_LEN-1] = '\0';
                    stepCount++;
                }
                newMapping.numHttpSteps = stepCount;
            }

            if (payload["enabled"].is<bool>()) { // If client sends it (e.g. for add)
                newMapping.enabled = payload["enabled"].as<bool>();
            } else { // Default for add if not provided
                newMapping.enabled = true; 
            }
            rfActionMappings.push_back(newMapping);
            mapping_id = newMapping.id; // For the alert message

            if (newIdToAssign >= nextRfMappingId) { nextRfMappingId = newIdToAssign + 1; }
        }

        if (saveRfMappingsToEEPROM()) {
            Serial.printf("Mapping ID %d %s successfully.\n", mapping_id, isEdit ? "edited" : "added");
            broadcastAlert("rfControlPane", "Mapping ID " + String(mapping_id) + (isEdit ? " edited." : " added."), "success", 3000);
            broadcastStatusUpdate();
        } else {
            broadcastAlert("rfControlPane", "Error saving mapping ID " + String(mapping_id) + ".", "danger", 5000);
        }
    } else if (strcmp(action, "deleteRfMapping") == 0) {
        if (payload["index"].isNull() || !payload["index"].is<int>()) {
            broadcastAlert("rfControlPane", "Missing or invalid index for delete command.", "danger", 5000); return;
        }
        uint16_t id_to_delete = payload["index"].as<uint16_t>(); // This is mapping.id
        bool deleted = false;
        for (auto it = rfActionMappings.begin(); it != rfActionMappings.end(); ++it) {
            if (it->id == id_to_delete) {
                rfActionMappings.erase(it);
                deleted = true;
                break;
            }
        }
        if (deleted) {
             if (saveRfMappingsToEEPROM()) {
                Serial.printf("Mapping ID %u deleted successfully.\n", id_to_delete);
                broadcastAlert("rfControlPane", "Mapping ID " + String(id_to_delete) + " deleted.", "success", 3000);
                broadcastStatusUpdate();
            } else {
                broadcastAlert("rfControlPane", "Error saving after deleting mapping ID " + String(id_to_delete) + ".", "danger", 5000);
            }
        } else {
            broadcastAlert("rfControlPane", "Mapping ID " + String(id_to_delete) + " not found for delete.", "danger", 5000);
        }
    } else if (strcmp(action, "toggleEnableRfMapping") == 0) {
        if (payload["id"].isNull() || !payload["id"].is<int>()) {
            broadcastAlert("rfControlPane", "Missing ID for toggle enable command.", "danger", 5000); return;
        }
        uint16_t mapping_id_to_toggle = payload["id"].as<uint16_t>();
        bool found_and_toggled = false;
        for (size_t i = 0; i < rfActionMappings.size(); ++i) {
            if (rfActionMappings[i].id == mapping_id_to_toggle) {
                rfActionMappings[i].enabled = !rfActionMappings[i].enabled;
                if (saveRfMappingsToEEPROM()) {
                    Serial.printf("Mapping ID %u enabled state toggled to: %s\n", mapping_id_to_toggle, rfActionMappings[i].enabled ? "Enabled" : "Disabled");
                    broadcastAlert("rfControlPane", "Mapping ID " + String(mapping_id_to_toggle) + " is now " + (rfActionMappings[i].enabled ? "Enabled." : "Disabled."), "success", 3000);
                    broadcastStatusUpdate();
                } else {
                    broadcastAlert("rfControlPane", "Error saving toggle for mapping ID " + String(mapping_id_to_toggle) + ".", "danger", 5000);
                    rfActionMappings[i].enabled = !rfActionMappings[i].enabled; // Revert in memory if save failed
                }
                found_and_toggled = true;
                break;
            }
        }
        if (!found_and_toggled) {
            broadcastAlert("rfControlPane", "Mapping ID " + String(mapping_id_to_toggle) + " not found for toggle enable.", "danger", 5000);
        }
    } else if (strcmp(action, "sendRfCode") == 0) {
        if (payload["code"].isNull()) {
            broadcastAlert("rfControlPane", "RF code missing for send command.", "danger", 5000);
            return;
        }
        unsigned long codeToSend = payload["code"].as<unsigned long>();
        unsigned int repeats = payload["repeats"].is<unsigned int>() ? payload["repeats"].as<unsigned int>() : 200; // Default if not provided or invalid type
        if (repeats == 0) repeats = 200; // Default if 0 or invalid (was 10, changed to 200 for consistency)

        if (codeToSend == 0) {
            broadcastAlert("rfControlPane", "Invalid RF code (0).", "danger", 5000);
            return;
        }
        Serial.print("WebSocket request to send RF Code: "); Serial.print(codeToSend);
        Serial.print(", Repeats: "); Serial.println(repeats);

        mySwitch.disableReceive();
        ELECHOUSE_cc1101.setPktFormat(3);
        ELECHOUSE_cc1101.setGDO0(RF_DATA_PIN);
        ELECHOUSE_cc1101.setPA(10);
        ELECHOUSE_cc1101.SetTx();
        currentCC1101Mode = "TX";
        broadcastStatusUpdate(); // Show TX mode immediately

        mySwitch.enableTransmit(RF_DATA_PIN);
        mySwitch.setProtocol(1);
        mySwitch.setRepeatTransmit(1);
        for (unsigned int i = 0; i < repeats; ++i) {
            mySwitch.send(codeToSend, 24);
            yield();
        }
        mySwitch.disableTransmit();
        initialize_cc1101(); // Resets to RX
        mySwitch.enableReceive(RF_DATA_PIN);
        Serial.println("All RF Signal repeats sent via WebSocket. Receiver re-enabled.");
        
        broadcastAlert("rfControlPane", "RF code " + String(codeToSend) + " sent " + String(repeats) + " times.", "success", 5000);
        broadcastStatusUpdate(); // Show RX mode and other statuses

    } else if (strcmp(action, "executeActionById") == 0) {
        if (payload["id"].isNull() || !payload["id"].is<int>()) {
            broadcastAlert("rfControlPane", "Missing mapping ID for execute command.", "danger", 5000); return;
        }
        uint16_t mapping_id_to_execute = payload["id"].as<uint16_t>();
        bool found_mapping = false;
        for (size_t i = 0; i < rfActionMappings.size(); ++i) { // Changed to indexed loop
            if (rfActionMappings[i].id == mapping_id_to_execute) { // Iterate with index to get a non-const ref
                found_mapping = true;
                if (rfActionMappings[i].enabled) {
                    Serial.printf("WebSocket: Executing action for mapping ID %u (RF Code: %lu)\n", rfActionMappings[i].id, rfActionMappings[i].rfCode);
                    executeConfiguredAction(rfActionMappings[i]); // Pass by non-const reference
                } else {
                    broadcastAlert("rfControlPane", "Mapping ID " + String(mapping_id_to_execute) + " is not enabled.", "warning", 5000);
                }
                break;
            }
        }
        if (!found_mapping) {
            broadcastAlert("rfControlPane", "Mapping ID " + String(mapping_id_to_execute) + " not found for execution.", "danger", 5000);
        }
    } else if (strcmp(action, "performDeviceToggle") == 0) {
        if (payload["id"].isNull() || !payload["id"].is<int>()) {
            broadcastAlert("rfControlPane", "Missing mapping ID for Device Toggle.", "danger", 5000); return;
        }
        uint16_t mapping_id = payload["id"].as<uint16_t>();
        bool found = false;
        for (const auto& mapping : rfActionMappings) {
            if (mapping.id == mapping_id) {
                found = true;
                if (mapping.enabled && mapping.actionType == ACTION_RPC_TOGGLE && strlen(mapping.RPC_IP) > 0) {
                    Serial.printf("WebSocket: Performing Device Toggle for mapping ID %u, IP: %s, Switch ID: %d\n", mapping.id, mapping.RPC_IP, mapping.RPC_SwitchId);
                    sendDeviceToggleCommand(mapping.RPC_IP, mapping.RPC_SwitchId, mapping.id);
                } else {
                    broadcastAlert("rfControlPane", "Mapping ID " + String(mapping_id) + " is not enabled for Device Toggle or Device IP is missing.", "warning", 5000);
                }
                break;
            }
        }
        if (!found) {
            broadcastAlert("rfControlPane", "Mapping ID " + String(mapping_id) + " not found for Device Toggle.", "danger", 5000);
        }

    } else if (strcmp(action, "setCc1101Frequency") == 0) {
        if (payload["frequency"].isNull()) { broadcastAlert("cc1101ConfigPane", "Missing frequency.", "danger", 5000); return; }
        float newFrequency = payload["frequency"].as<float>();
        if (newFrequency >= 387.000f && newFrequency <= 464.000f) {
            currentFrequency = newFrequency; initialize_cc1101(); saveConfiguration();
            broadcastAlert("cc1101ConfigPane", "CC1101 Freq updated: " + String(currentFrequency, 3) + " MHz.", "success", 5000);
            broadcastStatusUpdate();
        } else { broadcastAlert("cc1101ConfigPane", "Invalid Freq. Must be 387-464 MHz.", "danger", 5000); }
    } else if (strcmp(action, "setCc1101Bandwidth") == 0) {
        if (payload["bandwidth"].isNull()) { broadcastAlert("cc1101ConfigPane", "Missing bandwidth value.", "danger", 5000); return; }
        int newBandwidthInt = payload["bandwidth"].as<int>();
        bool validBandwidth = false; int allowedBandwidths[] = {58,67,81,101,116,135,162,203,232,270,325,406,464,541,650,812};
        for (int bw : allowedBandwidths) if (newBandwidthInt == bw) { validBandwidth = true; break; }
        if (validBandwidth) {
            currentBandwidth = (float)newBandwidthInt; initialize_cc1101(); saveConfiguration();
            broadcastAlert("cc1101ConfigPane", "CC1101 Bandwidth updated: " + String(currentBandwidth, 0) + " KHz.", "success", 5000);
            broadcastStatusUpdate();
        } else { broadcastAlert("cc1101ConfigPane", "Invalid Bandwidth.", "danger", 5000); }
    } else if (strcmp(action, "setCc1101DataRate") == 0) {
        if (payload["data_rate"].isNull()) { broadcastAlert("cc1101ConfigPane", "Missing data rate.", "danger", 5000); return; }
        float newDataRate = payload["data_rate"].as<float>();
        if (newDataRate >= 0.6f && newDataRate <= 600.0f) {
            currentDataRate = newDataRate; initialize_cc1101(); saveConfiguration();
            broadcastAlert("cc1101ConfigPane", "CC1101 Data Rate updated: " + String(currentDataRate, 1) + " kBaud.", "success", 5000);
            broadcastStatusUpdate();
        } else { broadcastAlert("cc1101ConfigPane", "Invalid Data Rate. Must be 0.6-600.0 kBaud.", "danger", 5000);}
    } else if (strcmp(action, "rebootDevice") == 0) {
        Serial.println("Reboot command received via WebSocket. Rebooting...");
        broadcastAlert("systemMaintenancePane", "Reboot command received. Device is rebooting...", "warning", 0); // Sticky

        // Allow the alert to be processed and sent by pumping the WebSocket loop
        for(int i=0; i<5; ++i) {
            webSocket.loop();
            delay(10); // Small delay, total 50ms
        }

        Serial.println("Attempting to gracefully disconnect WebSocket clients before reboot...");
        const uint8_t maxClients = 4; // Assuming default
        for (uint8_t i = 0; i < maxClients; i++) {
            if (webSocket.clientIsConnected(i)) {
                Serial.printf("Disconnecting client %u.\n", i);
                webSocket.disconnect(i);
            }
        }
        
        // Allow disconnect frames to be processed and sent
        for(int i=0; i<10; ++i) { // Pump the loop again
            webSocket.loop();
            delay(20);
        }

        EEPROM.write(EEPROM_REBOOT_FLAG_ADDR, REBOOT_FLAG_MAGIC_BYTE);
        if (EEPROM.commit()) {
        } else {
            Serial.println("Error committing EEPROM for reboot flag. Rebooting anyway.");
        }
        delay(50); 

        Serial.println("Proceeding with ESP.restart() after a final delay.");
        delay(1000); // Original delay, also allows clients to process the close frame
        ESP.restart();
    } else {
        Serial.printf("[%u] Unknown WebSocket command action: %s\n", clientNum, action);
        String unknownCmdMsg = "Unknown command action: " + String(action);
        // Construct JSON with duration for the alert about unknown command
        String alertJson = "{\"type\":\"alert\", \"payload\":{\"paneId\":\"global\",\"message\":\"" + escapeJsonString(unknownCmdMsg) + "\",\"alertType\":\"danger\",\"duration\":5000}}";
        webSocket.sendTXT(clientNum, alertJson.c_str());
    }
}