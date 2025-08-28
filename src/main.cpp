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
// --- End EEPROM Configuration ---

// --- Web Page Content (PROGMEM) ---

// --- Global Variables ---
bool g_justRebootedFromClientCommand = false; // Flag to indicate if reboot was client-initiated
const byte REBOOT_FLAG_MAGIC_BYTE = 0xD7; // Magic byte for the EEPROM reboot flag
const char PAGE_MAIN_HEADER_START[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>RF Bridge Control</title>
    <style>
)=====";

const char PAGE_STYLE[] PROGMEM = R"=====(
        body { padding-top: 1rem; padding-bottom: 1rem; background-color: #212529; color: #dee2e6; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif; margin:0; }
        .container { 
            width: 100%; /* Full width by default for mobile */
            padding-left: 15px; 
            padding-right: 15px; 
            margin-left: auto; 
            margin-right: auto; 
        }
        /* Responsive max-widths based on common breakpoints */
        @media (min-width: 576px) { .container { max-width: 540px; } } /* Small devices (e.g., landscape phones) */
        @media (min-width: 768px) { .container { max-width: 720px; } } /* Medium devices (e.g., tablets) */
        @media (min-width: 992px) { .container { max-width: 960px; } } /* Large devices (e.g., desktops) */
        @media (min-width: 1200px) { .container { max-width: 1140px; } } /* Extra large devices (e.g., large desktops) */

        .card { margin-bottom: 1.5rem; border: 1px solid #495057; border-radius: 0.375rem; background-color: #2c3034; }
        .card-header { background-color: #343a40; border-bottom: 1px solid #495057; padding: 0.75rem 1.25rem; font-weight: bold; border-top-left-radius: calc(0.375rem - 1px); border-top-right-radius: calc(0.375rem - 1px); }
        .card-body { padding: 1.25rem; }
        .form-control-dark { display: block; width: 100%; box-sizing: border-box; padding: 0.375rem 0.75rem; font-size: 1rem; font-weight: 400; line-height: 1.5; color: #fff; background-color: #343a40; background-clip: padding-box; border: 1px solid #495057; appearance: none; border-radius: 0.25rem; transition: border-color .15s ease-in-out,box-shadow .15s ease-in-out; }
        .form-control-dark:focus { color: #fff; background-color: #343a40; border-color: #86b7fe; outline: 0; box-shadow: 0 0 0 0.25rem rgba(13,110,253,.25); }
        .form-label { margin-bottom: 0.5rem; display: inline-block; }
        .btn { display: inline-block; font-weight: 400; line-height: 1.5; color: #212529; text-align: center; text-decoration: none; vertical-align: middle; cursor: pointer; user-select: none; background-color: transparent; border: 1px solid transparent; padding: 0.375rem 0.75rem; font-size: 1rem; border-radius: 0.25rem; transition: color .15s ease-in-out,background-color .15s ease-in-out,border-color .15s ease-in-out,box-shadow .15s ease-in-out; }
        .btn-primary { color: #fff; background-color: #0d6efd; border-color: #0d6efd; } .btn-primary:hover { background-color: #0b5ed7; border-color: #0a58ca; }
        .btn-danger { color: #fff; background-color: #dc3545; border-color: #dc3545; } .btn-danger:hover { background-color: #bb2d3b; border-color: #b02a37; }
        .btn-success { color: #fff; background-color: #198754; border-color: #198754; } .btn-success:hover { background-color: #157347; border-color: #146c43; }
        .btn-info { color: #000; background-color: #0dcaf0; border-color: #0dcaf0; } .btn-info:hover { background-color: #31d2f2; border-color: #25cff2; }
        .btn-warning { color: #000; background-color: #ffc107; border-color: #ffc107; } .btn-warning:hover { background-color: #ffca2c; border-color: #ffc720; }
        .alert-fixed-top { position: fixed; top: 10px; left: 50%; transform: translateX(-50%); z-index: 1050; min-width: 300px; padding: 1rem 1rem; margin-bottom: 1rem; border: 1px solid transparent; border-radius: 0.25rem; box-shadow: 0 0.5rem 1rem rgba(0,0,0,.15); }
        .alert-success { color: #0f5132; background-color: #d1e7dd; border-color: #badbcc; } .alert-info { color: #055160; background-color: #cff4fc; border-color: #b6effb; }
        .alert-warning { color: #664d03; background-color: #fff3cd; border-color: #ffecb5; } .alert-danger { color: #842029; background-color: #f8d7da; border-color: #f5c2c7; }
        .mb-1 { margin-bottom: 0.25rem !important; } .mb-2 { margin-bottom: 0.5rem !important; } .mb-3 { margin-bottom: 1rem !important; } .mb-4 { margin-bottom: 1.5rem !important; }
        .mt-3 { margin-top: 1rem !important; } .mt-4 { margin-top: 1.5rem !important; }
        .me-2 { margin-right: 0.5rem !important; }
        /* Add missing flex utility classes */
        .justify-content-between { justify-content: space-between !important; }
        .align-items-center { align-items: center !important; }
        .ms-auto { margin-left: auto !important; } /* Added for margin-left: auto */
        .mx-2 { margin-left: 0.5rem !important; margin-right: 0.5rem !important; }
        .text-light { color: #f8f9fa !important; } .text-muted { color: #6c757d !important; } .text-success { color: #198754 !important; }
        .text-danger { color: #dc3545 !important; } .text-warning { color: #ffc107 !important; } .text-info { color: #0dcaf0 !important; }
        .text-center { text-align: center !important; }
        hr.border-secondary { border: 0; border-top: 1px solid #495057; }
        .row { display: flex; flex-wrap: wrap; margin-right: -15px; margin-left: -15px; }
        /* Base for all md columns - full width by default (mobile first) */
        .col-md-2, .col-md-3, .col-md-4, .col-md-5, .col-md-6, .col-md-7 { 
            position: relative; 
            width: 100%; 
            padding-right: 15px; /* Gutters, potentially overridden by .g-3 */
            padding-left: 15px;  /* Gutters, potentially overridden by .g-3 */
            box-sizing: border-box; 
        }
        /* Specific widths for md breakpoint (768px and up) */
        @media (min-width: 768px) { 
            .col-md-2 { flex: 0 0 auto; width: 16.66666667%; } 
            .col-md-3 { flex: 0 0 auto; width: 25%; } 
            .col-md-4 { flex: 0 0 auto; width: 33.33333333%; } 
            .col-md-5 { flex: 0 0 auto; width: 41.66666667%; }
            .col-md-6 { flex: 0 0 auto; width: 50%; } 
            .col-md-7 { flex: 0 0 auto; width: 58.33333333%; } 
        }
        .d-inline-block { display: inline-block !important; } .w-100 { width: 100% !important; } .flex-wrap { flex-wrap: wrap !important; }
        .g-3 { margin-right: -0.75rem; margin-left: -0.75rem; } .g-3 > [class*="col-"] { padding-right: 0.75rem; padding-left: 0.75rem; margin-bottom: 1rem; }
        .d-flex { display: flex !important; } .align-items-end { align-items: flex-end !important; }
        .status-badge { display: inline-block; padding: 0.35em 0.65em; font-size: .85em; font-weight: 700; line-height: 1; color: #fff; text-align: center; white-space: nowrap; vertical-align: baseline; border-radius: 0.375rem; }
        .badge-success { background-color: #198754; } /* Green */
        .badge-danger { background-color: #dc3545; } /* Red */
        .badge-warning { background-color: #ffc107; color: #000; } /* Yellow, black text */
        .badge-info { background-color: #0dcaf0; color: #000; } /* Cyan, black text */
        .badge-muted { background-color: #6c757d; } /* Grey */
        .rf-controls-row { display: flex; flex-wrap: wrap; align-items: center; justify-content: space-around; margin-bottom: 1rem; } /* For the RF control buttons/status line */
        .rf-mapping-card .card-header { padding: 0.5rem 1rem; } /* Slightly less padding for mapping card headers */
        .card-header .action-result { float: right; font-size: 0.85em; font-weight: normal; margin-left: 10px; padding: 0.25em 0.5em; border-radius: 0.25rem; border: 1px solid transparent; }
        .action-result.success { color: #0f5132; background-color: #d1e7dd; border-color: #badbcc; }
        .action-result.danger { color: #842029; background-color: #f8d7da; border-color: #f5c2c7; } /* Similar to .alert-danger */
        .action-result.info { color: #055160; background-color: #cff4fc; border-color: #b6effb; } /* Similar to .alert-info */
        .action-result.warning { color: #664d03; background-color: #fff3cd; border-color: #ffecb5; } /* Similar to .alert-warning */        
        /* Modal styles */
        .modal { display: none; position: fixed; z-index: 1055; left: 0; top: 0; width: 100%; height: 100%; overflow: auto; background-color: rgba(0,0,0,0.5); }
        .modal-content { background-color: #2c3034; margin: 10% auto; padding: 20px; border: 1px solid #495057; border-radius: 0.375rem; width: 80%; max-width: 600px; color: #dee2e6; }
        .modal-header { padding-bottom: 10px; border-bottom: 1px solid #495057; margin-bottom: 15px; }
        .modal-header h5 { margin: 0; font-weight: bold; }
        .modal-footer { padding-top: 10px; border-top: 1px solid #495057; margin-top: 15px; text-align: right; }
        .close-button { color: #aaa; float: right; font-size: 28px; font-weight: bold; }
        .close-button:hover, .close-button:focus { color: #fff; text-decoration: none; cursor: pointer; }
        .form-group { margin-bottom: 1rem; }
        .table, .table th, .table td { border: 1px solid #495057; } /* Table borders */
        .table th, .table td { padding: 0.5rem; } /* Table cell padding */
)=====";

const char PAGE_FILE_INPUT_STYLE[] PROGMEM = R"=====(
input[type="file"]::file-selector-button {
  padding: 0.375rem 0.75rem; /* Match btn padding */
  font-size: 1rem; /* Match btn font size */
  line-height: 1.5; /* Match btn line height */
  border-radius: 0.25rem; /* Match btn border radius */
  color: #fff; /* Match btn-primary text color */
  background-color: #0d6efd; /* Match btn-primary background */
  border: 1px solid #0d6efd; /* Match btn-primary border */
  cursor: pointer;
  transition: color .15s ease-in-out,background-color .15s ease-in-out,border-color .15s ease-in-out,box-shadow .15s ease-in-out; /* Match btn transition */
}
input[type="file"]::file-selector-button:hover {
  background-color: #0b5ed7; /* Match btn-primary hover background */
  border-color: #0a58ca; /* Match btn-primary hover border */
}
)=====";

const char PAGE_BODY_START[] PROGMEM = R"=====(
    </style>
</head>
<body>
    <div id="alert-container"></div>
    <div class="container">
        <h1 class="mb-4 text-light text-center">RF Bridge Control</h1>

        <!-- Modal for Add/Edit RF Mapping -->
        <div id="rfMappingModal" class="modal"> <!-- Will be hidden by CSS initially -->
            <div class="modal-content">
                <div class="modal-header">
                    <h5 id="rfMappingModalTitle">Add RF Mapping</h5>
                    <span class="close-button" onclick="closeRfMappingModal()">&times;</span>
                </div>
                <form id="rfMappingForm">
                    <input type="hidden" id="rfMappingIndex" name="rfMappingIndex" value="-1">
                    <div class="form-group">
                        <label for="rfActionType" class="form-label">Action Type</label>
                        <select id="rfActionType" name="rfActionType" class="form-control-dark" onchange="toggleRfMappingFormFields()">
                            <option value="1">RPC Toggle</option>
                            <option value="2">RPC ON</option>
                            <option value="3">RPC OFF</option>
                            <option value="4">HTTP Request</option>
                        </select>
                    </div>
                    <div id="httpStepFieldsContainer"> 
                        <!-- Dynamically generated HTTP step fields will appear here -->
                    </div>
                    <button type="button" id="addHttpStepBtn" class="btn btn-info mt-2 mb-2" onclick="addHttpStepField()" style="display:none;">Add HTTP Request</button>
                    <div class="form-group" id="formGroupRPC_IP"> <!-- New field for RPC IP -->
                        <label for="rfRPC_IP" class="form-label">RPC IP Address</label>
                        <input type="text" id="rfRPC_IP" name="rfRPC_IP" class="form-control-dark" maxlength="15" placeholder="e.g., 192.168.1.50">
                    </div>
                    <div class="form-group" id="formGroupRPC_Id">
                        <label for="rfRPC_SwitchId" class="form-label">RPC Switch ID</label>
                        <input type="number" id="rfRPC_SwitchId" name="rfRPC_SwitchId" class="form-control-dark" value="0" min="0">
                    </div>
                </form>
                <div class="modal-footer">
                    <button type="button" class="btn btn-warning me-2" onclick="closeRfMappingModal()">Cancel</button>
                    <button type="button" class="btn btn-primary action-button" onclick="saveRfMapping()">Save Mapping</button>
                </div>
            </div>
        </div>

)=====";

const char PAGE_SCRIPT[] PROGMEM = R"=====(
<script>
// Global vars for managing the RF Send button state
let rfSendButtonElement = null;
let rfSendButtonOriginalText = '';
let paneAlertTimeouts = {}; // Object to store timeout IDs for each pane

function showPaneAlert(paneId, message, type = 'info', duration = 5000) {
    const resultSpan = document.getElementById(paneId + '-result');
    if (!resultSpan) {
        // Fallback to old global alert if specific pane result span isn't found
        console.warn('Pane result span not found for:', paneId, 'Falling back to global alert.');
        const alertContainer = document.getElementById('alert-container');
        if (!alertContainer) return;
        const alertDiv = document.createElement('div');
        alertDiv.className = `alert-fixed-top alert alert-${type}`;
        alertDiv.role = 'alert';
        alertDiv.innerHTML = message;
        alertContainer.innerHTML = ''; 
        alertContainer.appendChild(alertDiv);

        // Clear existing global timeout if any
        if (paneAlertTimeouts['global_fallback']) {
            clearTimeout(paneAlertTimeouts['global_fallback']);
        }
        if (duration > 0) {
            paneAlertTimeouts['global_fallback'] = setTimeout(() => { 
                if (alertDiv.parentNode === alertContainer) { // Check if it's still the same alert
                    alertDiv.remove(); 
                }
            }, duration);
        } else {
            delete paneAlertTimeouts['global_fallback']; // No timeout for sticky global alert
        }
        return;
    }

    // Always clear previous content and timeout for this pane
    resultSpan.innerHTML = '';
    resultSpan.className = 'action-result'; // Reset class

    // Clear existing timeout for this specific pane, if any
    if (paneAlertTimeouts[paneId]) {
        clearTimeout(paneAlertTimeouts[paneId]);
    }

    resultSpan.innerHTML = message;
    resultSpan.className = 'action-result ' + type; // type will be 'success' or 'danger'

    // Set a new timeout to clear the message if duration is positive
    if (duration > 0) {
        paneAlertTimeouts[paneId] = setTimeout(() => {
            resultSpan.innerHTML = '';
            resultSpan.className = 'action-result'; // Reset class
        }, duration);
    } else {
        delete paneAlertTimeouts[paneId]; // No timeout for sticky pane alert
    }
}

// fetchStatus function is no longer needed as initial status and updates are handled by WebSockets.

function sendWebSocketCommand(action, payload, buttonElement, paneId) {
    if (!socket || socket.readyState !== WebSocket.OPEN) {
        showPaneAlert(paneId || 'global', 'WebSocket not connected. Cannot send command.', 'danger');
        if (buttonElement) { // Re-enable the specific button if WS is not connected
            buttonElement.disabled = false;
            // Consider restoring original text if you have it stored globally or can retrieve it
        }
        return;
    }

    let originalButtonText = '';
    // It's better to only disable/modify the clicked button, not all action buttons,
    // unless that's the desired UX to prevent concurrent actions.
    // For now, let's stick to the original broader disable logic for consistency.
    const allActionButtons = document.querySelectorAll('.action-button'); 

    allActionButtons.forEach(btn => { // This loop disables all action buttons
        if (btn === buttonElement) {
            originalButtonText = btn.innerHTML;
            if (action === 'sendRfCode') { // Keep specific for direct send
                 btn.innerHTML = 'Sending...';
                 const codeToSend = payload && payload.code ? payload.code : 'N/A';
                 showPaneAlert(paneId, `Sending RF code: ${codeToSend}...`, 'info', 0); // Sticky alert
                 if (modeEl) modeEl.innerHTML = "<span class='status-badge badge-warning'>TX</span>"; // Optimistic UI update
                 rfSendButtonElement = btn; // Store for later restoration
                 rfSendButtonOriginalText = originalButtonText;
            } else if (action === 'toggleRelay') {
                btn.innerHTML = 'Toggling...';
                showPaneAlert(paneId, 'Attempting to toggle relay...', 'info', 0); // Sticky alert
            } else if (action.startsWith('learnForMappingIndex')) { // For new learn command
                btn.innerHTML = 'Learning...';
            } else {
                btn.innerHTML = 'Processing...'; // Default for other commands
            }
        }
        btn.disabled = true;
    });

    const commandMessage = {
        type: "command",
        action: action,
        payload: payload || {} // Ensure payload is an object
    };

    try {
        socket.send(JSON.stringify(commandMessage));
        console.log("Sent WS Command:", commandMessage);
    } catch (error) {
        console.error('Error sending WebSocket command:', error);
        showPaneAlert(paneId || 'global', 'Error sending command: ' + String(error), 'danger');
    } finally {
        allActionButtons.forEach(btn => {
            if (btn === rfSendButtonElement && action === 'sendRfCode') {
                // For the RF Send button, it's already 'Sending...' and disabled.
                // It will be re-enabled and text restored by the onmessage handler.
                // So, do nothing here for this specific button in the finally block.
            } else if (btn === buttonElement) { // For other clicked buttons
                btn.innerHTML = originalButtonText; // Restore their original text
                btn.disabled = false; // Re-enable them
            } else { // For other non-clicked action buttons
                btn.disabled = false; // Just re-enable them
            }
        });
    }
}

function getActionTypeString(actionType) {
    switch (actionType) {
        case 0: return 'None';
        case 1: return 'RPC Toggle';
        case 2: return 'RPC ON';
        case 3: return 'RPC OFF';
        case 4: return 'HTTP Request';
        default: return 'Unknown';
    }
}

let currentRfMappings = []; // Store current mappings globally for easy access by edit function
let expandedCardIds = new Set(); // Keep track of expanded card IDs

function _renderSingleMappingCard(mapping) {
    const container = document.getElementById('rf-mappings-container');
    if (!container) return;

    const cardDiv = document.createElement('div');
    cardDiv.className = 'card mb-2 rf-mapping-card';
    cardDiv.id = `rf-mapping-card-${mapping.id}`;

    // Card Header
    const cardHeader = document.createElement('div');
    cardHeader.className = 'card-header d-flex justify-content-between align-items-center p-2';
    cardHeader.style.cursor = 'pointer';
    cardHeader.onclick = function() {
        const cardElement = this.parentElement; // cardDiv
        const mappingId = parseInt(cardElement.id.split('-').pop()); // Extract mapping.id

        const cardBody = this.nextElementSibling;
        const isHidden = cardBody.style.display === 'none' || cardBody.style.display === '';
        cardBody.style.display = isHidden ? 'block' : 'none';

        if (isHidden) { // Card is now expanded
            expandedCardIds.add(mappingId);
        } else { // Card is now collapsed
            expandedCardIds.delete(mappingId);
        }

        const icon = this.querySelector('.expand-icon');
        if (icon) {
            icon.textContent = expandedCardIds.has(mappingId) ? ' [-]' : ' [+]';
        }
    };

    const headerInfoContainer = document.createElement('div');
    headerInfoContainer.className = 'd-flex align-items-center flex-wrap'; // Allow left content to wrap

    // Generate status badge HTML first as it's needed for the header controls container
    let statusBadgeHtml = '';
    if (mapping.action_type >= 1 && mapping.action_type <= 3) { // ACTION_RPC_TOGGLE, ACTION_RPC_ON, ACTION_RPC_OFF
        if (mapping.last_http_status_code === -100) { // RPC_CMD_TIMEOUT_STATUS_CODE
            statusBadgeHtml = '<span class="status-badge badge-danger" style="padding: 0.25em 0.5em;">Status: Timeout</span>';
        } else {
            // Global RPC status will be updated by updateStatusDisplay
            // The class global-RPC-status-cell is important here
            statusBadgeHtml = `<span class="global-RPC-status-cell status-badge badge-muted" style="padding: 0.25em 0.5em;">Status: N/A</span>`;
        }
    } else if (mapping.action_type === 4) { // ACTION_HTTP
        let httpStatusText = "N/A";
        let httpStatusClass = "badge-muted";
        if (mapping.last_http_status_code !== undefined && mapping.last_http_status_code !== 0) {
            if (mapping.last_http_status_code > 0) {
                if (mapping.last_http_status_code >= 200 && mapping.last_http_status_code < 300) {
                    httpStatusText = `OK (${mapping.last_http_status_code})`;
                    httpStatusClass = "badge-success";
                } else {
                    httpStatusText = `Fail (${mapping.last_http_status_code})`;
                    httpStatusClass = "badge-danger";
                }
            } else {
                if (mapping.last_http_status_code === -4 || mapping.last_http_status_code === -1) {
                    httpStatusText = `Timeout`;
                    httpStatusClass = "badge-danger";
                } else {
                    httpStatusText = `Error (${mapping.last_http_status_code})`;
                    httpStatusClass = "badge-warning";
                }
            }
        }
        statusBadgeHtml = `<span class="status-badge ${httpStatusClass}" style="padding: 0.25em 0.5em;">Status: ${httpStatusText}</span>`;
    } else { // ACTION_NONE or other
        statusBadgeHtml = '<span class="status-badge badge-muted" style="padding: 0.25em 0.5em;">Status: N/A</span>';
    }

    const idSpan = document.createElement('span');
    idSpan.className = 'fw-bold'; // Removed me-3
    idSpan.innerHTML = `<span class="status-badge badge-info" style="padding: 0.25em 0.5em;">ID: ${mapping.id}</span>`;

    const separator1 = document.createElement('span');
    separator1.className = 'mx-2 text-muted';
    separator1.innerHTML = '|';

    const rfSpan = document.createElement('span');
    const rfCodeDisplay = mapping.rf_code ? String(mapping.rf_code) : 'Not Set';
    const rfCodeClass = mapping.rf_code ? 'badge-success' : 'badge-muted';
    rfSpan.innerHTML = `<span class="status-badge ${rfCodeClass}" style="padding: 0.25em 0.5em;">RF: ${rfCodeDisplay}</span>`;
    
    const separator2 = document.createElement('span');
    separator2.className = 'mx-2 text-muted';
    separator2.innerHTML = '|';

    const typeSpan = document.createElement('span');
    typeSpan.innerHTML = `<span class="status-badge badge-warning" style="padding: 0.25em 0.5em;">Type: ${getActionTypeString(mapping.action_type)}</span>`;

    headerInfoContainer.appendChild(idSpan);
    headerInfoContainer.appendChild(separator1);
    headerInfoContainer.appendChild(rfSpan);
    headerInfoContainer.appendChild(separator2);
    headerInfoContainer.appendChild(typeSpan);

    // Header controls container will now hold the status and the expand icon
    const headerControlsContainer = document.createElement('div');
    headerControlsContainer.className = 'd-flex align-items-center ms-auto'; // Added ms-auto

    const expandIconSpan = document.createElement('span');
    expandIconSpan.className = 'expand-icon';
    expandIconSpan.textContent = expandedCardIds.has(mapping.id) ? ' [-]' : ' [+]'; // Set based on stored state
    expandIconSpan.style.minWidth = '25px'; 
    expandIconSpan.style.textAlign = 'right';

    const statusHeaderSpan = document.createElement('span');
    statusHeaderSpan.className = 'me-2'; // Add some margin before the expand icon
    statusHeaderSpan.innerHTML = statusBadgeHtml; 

    cardHeader.appendChild(headerInfoContainer);
    // Add status first, then icon to the controls container
    headerControlsContainer.appendChild(statusHeaderSpan); 
    headerControlsContainer.appendChild(expandIconSpan);
    cardHeader.appendChild(headerControlsContainer);

    // Card Body (initially hidden)
    const cardBody = document.createElement('div');
    cardBody.className = 'card-body';
    cardBody.style.display = expandedCardIds.has(mapping.id) ? 'block' : 'none'; // Set based on stored state

    let paramsHtml = '';
    if (mapping.action_type >= 1 && mapping.action_type <= 3) { // RPC actions
        paramsHtml = `<div class="mb-1"><strong>RPC IP:</strong> ${mapping.RPC_ip || '<span class="text-muted">Not Set</span>'}</div>
                      <div><strong>RPC ID:</strong> ${mapping.RPC_switch_id}</div>`;
    } else if (mapping.action_type === 4) { // ACTION_HTTP
        for (let k = 0; k < mapping.num_http_steps; k++) {
            const step = mapping.http_steps[k];
            paramsHtml += `<div class="mb-1 ms-3"><strong>Request ${k+1} (${step.method === 0 ? 'GET' : 'POST'}):</strong> ${step.url || '<span class="text-muted">No URL</span>'}</div>`;
            if (step.method === 1) { // POST
                 if (step.headers && step.headers.length > 0) {
                    paramsHtml += `<div class="mb-1 ms-4">Headers: ${step.headers}</div>`;
                 }
                 if (step.jsonData && step.jsonData.length > 0) {
                    paramsHtml += `<div class="ms-4">Data: ${step.jsonData}</div>`;
                 }
            }
        }
    }

    const parametersDiv = document.createElement('div');
    parametersDiv.className = 'mb-2';
    parametersDiv.style.textAlign = 'center'; // Center the heading and the content block

    const parametersHeading = document.createElement('h6');
    parametersHeading.textContent = 'Parameters:';
    parametersDiv.appendChild(parametersHeading);

    const paramsContentWrapper = document.createElement('div');
    paramsContentWrapper.style.display = 'inline-block'; // To center this block of parameters
    paramsContentWrapper.style.textAlign = 'left';     // To left-align text within parameter lines
    paramsContentWrapper.innerHTML = (paramsHtml.length > 0 ? paramsHtml : ' <span class="text-muted">N/A</span>');
    parametersDiv.appendChild(paramsContentWrapper);

    const actionsDiv = document.createElement('div');
    actionsDiv.className = 'mt-3'; // Margin top
    actionsDiv.style.textAlign = 'center'; // Center the heading and the button container

    const actionsHeading = document.createElement('h6');
    actionsHeading.textContent = 'Actions:';
    actionsDiv.appendChild(actionsHeading);

    // Create the Enable/Disable button here
    const enableDisableButtonText = mapping.enabled ? 'Enabled' : 'Disabled'; // Text reflects current state
    const enableDisableButtonClass = mapping.enabled ? 'btn-success' : 'btn-danger'; // Green if Enabled, Red if Disabled
    let actionButtonsHtml = `<button class="btn btn-sm ${enableDisableButtonClass} me-2 mb-2 action-button" style="width: 90px;" onclick="toggleEnableRfMapping(${mapping.id}, event)">${enableDisableButtonText}</button>`;
    
    actionButtonsHtml += `<button class="btn btn-sm btn-info me-2 mb-2 action-button" onclick="learnRfForMapping(${mapping.id})">Learn RF</button>`;
    actionButtonsHtml += `<button class="btn btn-sm btn-warning me-2 mb-2 action-button" onclick="editRfMapping(${mapping.id})">Edit</button>`; // Edit button

        if (mapping.enabled) {
            if (mapping.action_type === 1) { // ACTION_RPC_TOGGLE (now "Execute")
                actionButtonsHtml += `<button class="btn btn-sm btn-primary me-2 mb-2 action-button" onclick="executeConfiguredActionForRow(${mapping.id}, event)">Execute</button>`;
            } else if (mapping.action_type === 2 || mapping.action_type === 3) { // ACTION_RPC_ON or ACTION_RPC_OFF
                actionButtonsHtml += `<button class="btn btn-sm btn-primary me-2 mb-2 action-button" onclick="executeConfiguredActionForRow(${mapping.id}, event)">Execute</button>`;
            } else if (mapping.action_type === 4 && mapping.num_http_steps > 0) { // ACTION_HTTP
                actionButtonsHtml += `<button class="btn btn-sm btn-primary me-2 mb-2 action-button" onclick="executeConfiguredActionForRow(${mapping.id}, event)">Execute</button>`;
            }
        }

    actionButtonsHtml += `<button class="btn btn-sm btn-danger mb-2 action-button" onclick="deleteRfMapping(${mapping.id})">Delete Mapping</button>`;
    
    const actionButtonsContainer = document.createElement('div');
    actionButtonsContainer.className = 'mt-1'; // This div is block, will be centered by parent's text-align
    actionButtonsContainer.innerHTML = actionButtonsHtml; // Buttons are inline-block, will center.
    actionsDiv.appendChild(actionButtonsContainer);

    cardBody.appendChild(parametersDiv);
    cardBody.appendChild(actionsDiv);

    cardDiv.appendChild(cardHeader);
    cardDiv.appendChild(cardBody);
    container.appendChild(cardDiv);
}

function executeConfiguredActionForRow(mappingId, event) {
    // This function is called by the "Execute" or "Toggle" button on each row.
    // It sends a command to the backend to trigger the action for this mappingId.
    console.log(`Execute action for mapping ID: ${mappingId}`);
    sendWebSocketCommand('executeActionById', { id: mappingId }, event.target, 'rfControlPane');
}

function toggleEnableRfMapping(mappingId, event) {
    console.log(`Toggle enable for mapping ID: ${mappingId}`);
    sendWebSocketCommand('toggleEnableRfMapping', { id: mappingId }, event.target, 'rfControlPane');
}

function updateAddButtonState() {
    const addBtn = document.getElementById('addRfMappingBtn');
    if (addBtn) addBtn.disabled = false; 
}

function renderRfMappingsTable(mappings) {
    const container = document.getElementById('rf-mappings-container'); // Changed from tbody
    if (!container) return;
    container.innerHTML = ''; 
    currentRfMappings = mappings; // This is the array of mappings from the server
    
    for (let i = 0; i < currentRfMappings.length; i++) {
        const mapping = currentRfMappings[i];
        if (mapping) { // Ensure mapping object exists
            _renderSingleMappingCard(mapping); // Call the new card rendering function
        }
    }
    updateAddButtonState(); // Update based on initially rendered rows
}

function learnRfForMapping(id) { // id is 1-based
    console.log(`Learn RF for mapping ID: ${id}`);
    sendWebSocketCommand('learnForMappingIndex', { index: id }, event.target, 'rfControlPane');
}

function deleteRfMapping(id) { // id is 1-based
    console.log(`Delete RF mapping ID: ${id}`);
    sendWebSocketCommand('deleteRfMapping', { index: id }, event.target, 'rfControlPane');
}

function editRfMapping(id) { // id is 1-based
    console.log(`Edit RF mapping ID: ${id}`);
    // Find the mapping object by its 1-based id
    const mappingData = currentRfMappings.find(m => m.id === id);
    if (mappingData) {
        openRfMappingModal(id, mappingData); // Pass 1-based id
    } else {
        console.error(`Mapping data not found for ID: ${id}`);
    }
}

// Modal Control Functions
function openRfMappingModal(index = -1, mapping = null) {
    const modal = document.getElementById('rfMappingModal');
    const form = document.getElementById('rfMappingForm');
    const title = document.getElementById('rfMappingModalTitle');

    // Clear previous dynamic fields
    document.getElementById('httpStepFieldsContainer').innerHTML = '';
    form.reset(); // Clear previous values
    document.getElementById('rfMappingIndex').value = index; // Store the 1-based ID or -1 for add

    if (index === -1 || !mapping) { // Add mode
        title.textContent = 'Add New RF Mapping';
        document.getElementById('rfActionType').value = '1'; // Default to "RPC Toggle"
    } else { // Edit mode
        title.textContent = `Edit RF Mapping (ID: ${index})`;
        document.getElementById('rfRPC_IP').value = mapping.RPC_IP || ''; // Populate RPC IP
        document.getElementById('rfActionType').value = mapping.action_type;
        document.getElementById('rfRPC_SwitchId').value = mapping.RPC_switch_id || 0;

        if (mapping.action_type === 4 && mapping.http_steps && mapping.http_steps.length > 0) {
            mapping.http_steps.forEach(stepData => {
                addHttpStepField(stepData);
            });
        } else if (mapping.action_type === 4) { // HTTP action but no steps, add one empty
             addHttpStepField(null, true);
        }
    }
    toggleRfMappingFormFields(); // Show/hide fields based on action type
    updateHttpStepRemoveButtonsVisibility(); // Ensure visibility is correct after modal setup
    modal.style.display = 'block';
}

function closeRfMappingModal() {
    const modal = document.getElementById('rfMappingModal');
    modal.style.display = 'none';
}

function createHttpStepFields(stepNum, stepData = {}) {
    const stepDiv = document.createElement('div');
    stepDiv.className = 'http-step mb-3 border p-2'; // Added border and padding
    stepDiv.dataset.stepIndex = stepNum; // Store step number for easier removal/management

    const removeBtn = document.createElement('button');
    removeBtn.type = 'button';
    removeBtn.className = 'btn btn-sm btn-danger float-end'; // Bootstrap float utility
    removeBtn.textContent = 'Remove HTTP Request';
    removeBtn.onclick = function() { 
        this.parentElement.remove();
        updateAddHttpStepButtonVisibility(); 
        updateHttpStepRemoveButtonsVisibility(); // Update remove button visibility for remaining steps
    };

    const methodGroup = document.createElement('div');
    methodGroup.className = 'form-group';
    const methodLabel = document.createElement('label');
    methodLabel.className = 'form-label';
    methodLabel.textContent = 'Method';
    const methodSelect = document.createElement('select');
    methodSelect.className = 'form-control-dark';
    methodSelect.id = `httpStepMethod${stepNum}`;
    methodSelect.innerHTML = '<option value="0">GET</option><option value="1">POST</option>';
    methodSelect.value = stepData.method !== undefined ? stepData.method : 0; // Use stepData if available
    methodGroup.appendChild(methodLabel);
    methodGroup.appendChild(methodSelect);
    stepDiv.appendChild(methodGroup);

    const urlGroup = document.createElement('div');
    urlGroup.className = 'form-group';
    const urlLabel = document.createElement('label');
    urlLabel.className = 'form-label';
    urlLabel.textContent = 'URL';
    const urlInput = document.createElement('input');
    urlInput.type = 'text';
    urlInput.className = 'form-control-dark';
    urlInput.id = `httpStepUrl${stepNum}`;
    urlInput.maxLength = 63;
    urlInput.value = stepData.url || '';
    urlGroup.appendChild(urlLabel);
    urlGroup.appendChild(urlInput);
    stepDiv.appendChild(urlGroup);

    const headersGroup = document.createElement('div');
    headersGroup.className = 'form-group';
    const headersLabel = document.createElement('label');
    headersLabel.className = 'form-label';
    headersLabel.textContent = 'Headers';
    const headersInput = document.createElement('input');
    headersInput.type = 'text';
    headersInput.className = 'form-control-dark';
    headersInput.id = `httpStepHeaders${stepNum}`;
    headersInput.maxLength = 63;
    headersInput.value = stepData.headers || '';
    headersGroup.appendChild(headersLabel);
    headersGroup.appendChild(headersInput);
    stepDiv.appendChild(headersGroup);

    const jsonDataGroup = document.createElement('div');
    jsonDataGroup.className = 'form-group';
    const jsonDataLabel = document.createElement('label');
    jsonDataLabel.className = 'form-label';
    jsonDataLabel.textContent = 'JSON Data';
    const jsonDataTextarea = document.createElement('textarea');
    jsonDataTextarea.className = 'form-control-dark';
    jsonDataTextarea.id = `httpStepJsonData${stepNum}`;
    jsonDataTextarea.rows = 2;
    jsonDataTextarea.maxLength = 63;
    jsonDataTextarea.value = stepData.jsonData || '';
    jsonDataGroup.appendChild(jsonDataLabel);
    jsonDataGroup.appendChild(jsonDataTextarea);
    stepDiv.appendChild(jsonDataGroup);
    stepDiv.appendChild(removeBtn);

    return stepDiv;
}

function addHttpStepField(stepData = null, isInitialEmptyStep = false) {
    const container = document.getElementById('httpStepFieldsContainer');
    const existingSteps = container.querySelectorAll('.http-step').length;
    const maxSteps = 2; // From your MAX_HTTP_STEPS_PER_MAPPING

    if (existingSteps >= maxSteps) {
        showPaneAlert('rfControlPane', `Maximum ${maxSteps} HTTP steps allowed.`, 'warning', 3000);
        return;
    }

    // Add a breakline before the new step if it's not the first one
    if (existingSteps > 0) {
        const breakline = document.createElement('br');
        container.appendChild(breakline);
    }

    const newStepIndex = Date.now();
    const stepFields = createHttpStepFields(newStepIndex, stepData || {});
    container.appendChild(stepFields);
    updateAddHttpStepButtonVisibility();
    updateHttpStepRemoveButtonsVisibility();
}

function updateAddHttpStepButtonVisibility() {
    const container = document.getElementById('httpStepFieldsContainer');
    const addBtn = document.getElementById('addHttpStepBtn');
    const existingSteps = container.querySelectorAll('.http-step').length;
    const maxSteps = 2; // From MAX_HTTP_STEPS_PER_MAPPING

    if (existingSteps >= maxSteps) {
        addBtn.style.display = 'none';
    } else {
        // Show it only if action type is HTTP
        const actionType = document.getElementById('rfActionType').value;
        if (actionType === '4') {
            addBtn.style.display = 'inline-block';
        } else {
            addBtn.style.display = 'none';
        }
    }
}

function updateHttpStepRemoveButtonsVisibility() {
    const container = document.getElementById('httpStepFieldsContainer');
    const steps = container.querySelectorAll('.http-step');
    steps.forEach((step) => {
        const removeBtn = step.querySelector('.btn-danger'); // Find the remove button within this step        
        if (removeBtn) {
            if (step === steps[0]) { // Hide if it's the first step in the list
                removeBtn.style.display = 'none'; // Hide if it's the only step
            } else {
                removeBtn.style.display = 'inline-block'; 
            }
        }
    });
}
function toggleRfMappingFormFields() {
    const actionType = document.getElementById('rfActionType').value;
    const RPC_IpGroup = document.getElementById('formGroupRPC_IP'); // Get RPC IP group
    const RPC_IdGroup = document.getElementById('formGroupRPC_Id');
    const httpStepFieldsContainer = document.getElementById('httpStepFieldsContainer');
    const addHttpStepBtn = document.getElementById('addHttpStepBtn');

    RPC_IpGroup.style.display = 'none';
    RPC_IdGroup.style.display = 'none';
    
    addHttpStepBtn.style.display = 'none';

    switch (actionType) {
        case '1': // RPC Toggle
        case '2': // RPC ON
        case '3': // RPC OFF
            RPC_IpGroup.style.display = 'block'; // Show Device IP for RPC actions
            RPC_IdGroup.style.display = 'block';
            break;
        case '4': // HTTP Request
            if (httpStepFieldsContainer.children.length === 0) {
                addHttpStepField(null, true);
            }
            addHttpStepBtn.style.display = 'inline-block';
            updateAddHttpStepButtonVisibility();
            break;
    }
}

// --- Helper Functions (Keep or add if not present elsewhere) ---
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}




function saveRfMapping() {
    const form = document.getElementById('rfMappingForm');
    const index = parseInt(document.getElementById('rfMappingIndex').value, 10);
    const actionType = parseInt(document.getElementById('rfActionType').value, 10);
    const RPC_IP = document.getElementById('rfRPC_IP').value; // Get RPC IP
    const RPC_SwitchId = parseInt(document.getElementById('rfRPC_SwitchId').value, 10) || 0;
    
    const httpSteps = [];
    let numHttpSteps = 0;

    if (actionType === 4) {
        const stepElements = document.getElementById('httpStepFieldsContainer').querySelectorAll('.http-step');
        numHttpSteps = stepElements.length;
        stepElements.forEach((stepEl, i) => {
            const step = {
                method: parseInt(stepEl.querySelector('select[id^="httpStepMethod"]').value, 10),
                url: stepEl.querySelector('input[id^="httpStepUrl"]').value,
                headers: stepEl.querySelector('input[id^="httpStepHeaders"]').value,
                jsonData: stepEl.querySelector('textarea[id^="httpStepJsonData"]').value
            };
            httpSteps.push(step);
        });
    }

    const payload = {
        id: index, // For edit, this is the mapping.id. For add, it's -1 (or similar marker for new)
        rf_code: 0,
        action_type: actionType,
        RPC_IP: RPC_IP,
        RPC_SwitchId: RPC_SwitchId,

        // Include http steps data only for HTTP action type
        ...(actionType === 4 && {
            num_http_steps: numHttpSteps,
            http_steps: httpSteps
        }),
        enabled: (index === -1) ? true : undefined // Default to true for new mappings; for edit, 'enabled' is not sent or handled by this command.
    };

    const commandAction = (index === -1) ? 'addRfMapping' : 'editRfMapping';
)=====";

const char PAGE_SCRIPT_PART2[] PROGMEM = R"=====(
    sendWebSocketCommand(commandAction, payload, form.querySelector('.action-button'), 'rfControlPane');
    closeRfMappingModal();
}

// WebSocket connection setup
const socketUrl = `ws://${window.location.hostname}:81/`;
let socket;
let modeEl; // For CC1101 mode status

function connectWebSocket() {
    console.log('Attempting WebSocket connection to:', socketUrl);
    socket = new WebSocket(socketUrl);

    socket.onopen = function(event) {
        console.log('WebSocket connected.');
    };

    socket.onmessage = function(event) {
        try {
            const data = JSON.parse(event.data);
            if (data.type === 'statusUpdate' && data.payload) {
                updateStatusDisplay(data.payload);
                if (data.payload.rf_mappings) {
                    renderRfMappingsTable(data.payload.rf_mappings);
                }
            } else if (data.type === 'alert' && data.payload) {
                showPaneAlert(data.payload.paneId, data.payload.message, data.payload.alertType, data.payload.duration);
                // Check if this alert is for RF code send completion
                if (rfSendButtonElement && 
                    data.payload.paneId === 'rfControlPane' &&
                    data.payload.message && // Ensure message exists
                    data.payload.message.startsWith('RF code ') && 
                    data.payload.message.includes(' sent ')) {
                        rfSendButtonElement.innerHTML = rfSendButtonOriginalText;
                        rfSendButtonElement.disabled = false;
                        rfSendButtonElement = null; // Clear stored reference
                }
            } else {
                console.warn('Unknown WebSocket message type or format:', data);
            }
        } catch (e) {
            console.error('Error parsing WebSocket message or handling data:', e);
            showPaneAlert('global', 'Error processing message from bridge.', 'danger');
        }
    };

    socket.onclose = function(event) {
        console.log('WebSocket disconnected. Reason:', event.reason, 'Code:', event.code);
        setTimeout(connectWebSocket, 5000); // Try to reconnect every 5 seconds
    };

    socket.onerror = function(error) {
        console.error('WebSocket error:', error);
        showPaneAlert('global', 'WebSocket connection error.', 'danger');
        // Note: onclose will likely be called after onerror, so reconnection logic is there.
    };
}

function updateStatusDisplay(status) {
    // WiFi Status
    document.getElementById('wifi-connected-val').innerHTML = status['wifi-connected-val'] || 'N/A';
    document.getElementById('wifi-ip-val').innerHTML = status['wifi-ip-val'] || 'N/A';
    document.getElementById('wifi-mac-val').innerHTML = status['wifi-mac-val'] || 'N/A';
    document.getElementById('wifi-hostname-val').innerHTML = status['wifi-hostname-val'] || 'N/A';

    // CC1101 Status & Form Values
    document.getElementById('cc1101-freq-val').innerHTML = status['cc1101-freq-val'] || 'N/A';
    document.getElementById('cc1101-bandwidth-val').innerHTML = status['cc1101-bandwidth-val'] || 'N/A';
    document.getElementById('cc1101-rate-val').innerHTML = status['cc1101-rate-val'] || 'N/A';
    modeEl = document.getElementById('cc1101-mode-val');
    if(modeEl) modeEl.innerHTML = status['cc1101-mode-val'] || 'N/A';

    const freqInput = document.getElementById('cc1101Frequency');
    if (freqInput && status['cc1101Frequency_form_val']) freqInput.value = status['cc1101Frequency_form_val'];
    
    const bandwidthSelect = document.getElementById('cc1101Bandwidth');
    if (bandwidthSelect && status['cc1101Bandwidth_form_val']) bandwidthSelect.value = status['cc1101Bandwidth_form_val'];

    const dataRateInput = document.getElementById('cc1101DataRate');
    if (dataRateInput && status['cc1101DataRate_form_val']) dataRateInput.value = status['cc1101DataRate_form_val'];

    // Update Global RPC Status in RF Control Table
    const rpcStatusCells = document.querySelectorAll('.global-RPC-status-cell');
    const newRpcStatusHTML = status['device-relay-status-val'] || '<span class="status-badge badge-muted">Unknown</span>';
    rpcStatusCells.forEach(cell => {
        cell.innerHTML = newRpcStatusHTML;
    });
}
document.addEventListener('DOMContentLoaded', function() {
    connectWebSocket(); // Initialize WebSocket connection

    // CC1101 Config
    document.getElementById('setFrequencyBtn').addEventListener('click', function(event) {
        const freq = document.getElementById('cc1101Frequency').value;
        sendWebSocketCommand('setCc1101Frequency', { frequency: parseFloat(freq) }, event.target, 'cc1101ConfigPane');
    });
    document.getElementById('setBandwidthBtn').addEventListener('click', function(event) {
        const bandwidth = document.getElementById('cc1101Bandwidth').value;
        sendWebSocketCommand('setCc1101Bandwidth', { bandwidth: parseInt(bandwidth) }, event.target, 'cc1101ConfigPane');
    });
    document.getElementById('setDataRateBtn').addEventListener('click', function(event) {
        const dataRate = document.getElementById('cc1101DataRate').value;
        sendWebSocketCommand('setCc1101DataRate', { data_rate: parseFloat(dataRate) }, event.target, 'cc1101ConfigPane');
    });

    // RF Control - Send Code Form
    document.getElementById('sendCodeForm').addEventListener('submit', function(event) {
        event.preventDefault();
        const code = document.getElementById('rfCode').value;
        const repeats = document.getElementById('repeats').value;
        sendWebSocketCommand('sendRfCode', { code: parseInt(code), repeats: parseInt(repeats) }, event.target.querySelector('button[type="submit"]'), 'rfControlPane');
    });
    
    document.getElementById('addRfMappingBtn').addEventListener('click', function() {
        // Open modal for adding a new mapping. Server will assign ID.
        openRfMappingModal(-1, null); 
    });

    // System Maintenance
    document.getElementById('rebootDeviceBtn').addEventListener('click', function(event) {
        sendWebSocketCommand('rebootDevice', {}, event.target, 'systemMaintenancePane');
    });
    
    const firmwareForm = document.getElementById('firmwareUpdateForm');
    firmwareForm.addEventListener('submit', function(event) {
        event.preventDefault();
        const formData = new FormData(firmwareForm);
        const fileInput = firmwareForm.querySelector('input[type="file"]');
        const submitButton = firmwareForm.querySelector('button[type="submit"]');
        
        if (!fileInput.files || fileInput.files.length === 0) {
            showPaneAlert('systemMaintenancePane', 'Please select a firmware file first.', 'warning'); return;
        }
        if (!fileInput.files[0].name.endsWith('.bin')) {
            showPaneAlert('systemMaintenancePane', 'Invalid file type. Please select a .bin file.', 'warning'); return;
        }
        submitButton.disabled = true; submitButton.innerHTML = 'Uploading...';
        showPaneAlert('systemMaintenancePane', 'Uploading firmware... Do not navigate away.', 'info', 0);

        const xhr = new XMLHttpRequest();
        xhr.open('POST', '/update', true);
        xhr.onload = function() {
            submitButton.disabled = false; submitButton.innerHTML = 'Upload Firmware';
            if (xhr.status === 200) {
                try { const response = JSON.parse(xhr.responseText);
                    showPaneAlert('systemMaintenancePane', (response.success ? 'Firmware upload successful! ' : 'Firmware upload failed: ') + (response.message || ''), response.success ? 'success' : 'danger', 0);
                } catch (e) { showPaneAlert('systemMaintenancePane', 'Error parsing server response.', 'danger', 0); }
            } else { showPaneAlert('systemMaintenancePane', `Upload error: Server status ${xhr.status}`, 'danger', 0); }
        };
        xhr.onerror = function() {
            submitButton.disabled = false; submitButton.innerHTML = 'Upload Firmware';
            showPaneAlert('systemMaintenancePane', 'Firmware upload network error.', 'danger', 0);
        };
        xhr.send(formData);
    });

    window.onclick = function(event) {
        const modal = document.getElementById('rfMappingModal');
        if (event.target == modal) closeRfMappingModal();
    }
    toggleRfMappingFormFields(); 
});
</script>
)=====";
 
const char PAGE_FOOTER[] PROGMEM = R"=====( 
        <p class="text-center text-muted mt-4">ESP8266 RF Bridge</p>
    </div>
</body>
)=====";

// --- Status Card (PROGMEM) ---
const char PAGE_STATUS_CARD_START[] PROGMEM = R"=====(
        <div class="card">
            <div class="card-header">Status</div>
            <div class="card-body">
)=====";
const char PAGE_STATUS_CARD_END[] PROGMEM = R"=====(
            </div>
        </div>
)=====";

// --- CC1101 Config Card (PROGMEM) ---
const char PAGE_CC1101_CONFIG_CARD_START[] PROGMEM = R"=====(
        <div class="card">
            <div class="card-header">CC1101 Config <span id="cc1101ConfigPane-result" class="action-result"></span></div>
            <div class="card-body">
)=====";
const char PAGE_CC1101_CONFIG_CARD_END[] PROGMEM = R"=====(
            </div>
        </div>
)=====";

const char PAGE_RF_CONTROL_CARD[] PROGMEM = R"=====(
        <div class="card">
            <div class="card-header">RF Control <span id="rfControlPane-result" class="action-result"></span></div>
            <div class="card-body">
                <!-- This div will hold the RF Mapping cards -->
                <div id="rf-mappings-container">
                    <!-- Cards will be inserted here by JavaScript -->
                </div>
                <div class="text-center mt-2"> <!-- Wrapper to center the button -->
                    <button type="button" id="addRfMappingBtn" class="btn btn-success action-button">Add New Mapping</button> <!-- mt-2 moved to wrapper -->
                </div>
                <hr class="border-secondary mt-3 mb-3">
                <h5 class="mt-3 mb-3">Send RF Code</h5>
                <form id="sendCodeForm" class="row g-3">
                    <div class="col-md-6">
                        <label for="rfCode" class="form-label">RF Code (Decimal)</label>
                        <input type="number" class="form-control-dark" id="rfCode" name="code" required>
                    </div>
                    <div class="col-md-4">
                        <label for="repeats" class="form-label">Repeats</label>
                        <input type="number" class="form-control-dark" id="repeats" name="repeats" value="200" min="1">
                    </div>
                    <div class="col-md-2 d-flex align-items-end"><button type="submit" class="btn btn-success w-100 action-button">Send</button></div>
                </form>
            </div>
        </div>
)=====";

// --- Firmware Update Card (PROGMEM) ---
const char PAGE_FIRMWARE_UPDATE_CARD[] PROGMEM = R"=====(
<div class="card">
    <div class="card-header">System Maintenance <span id="systemMaintenancePane-result" class="action-result"></span></div>
    <div class="card-body text-center">
        <p class="mb-3">Upload a new firmware (.bin) file:</p>
        
        <form id="firmwareUpdateForm" 
              style="max-width: 350px; width: 100%; display: block; margin-left: auto; margin-right: auto; margin-bottom: 1rem;" 
              method="POST" action="/update" enctype="multipart/form-data">
            <div style="max-width: 300px; display: block; margin-left: auto; margin-right: auto;" class="mb-3">
                <input type="file" name="firmware" accept=".bin" class="form-control-dark w-100">
            </div>
            <button type="submit" class="btn btn-primary w-100" 
                    style="max-width: 220px; display: block; margin-left: auto; margin-right: auto;">Upload Firmware</button>
        </form>

        <hr class="border-secondary mt-4 mb-3 w-75" style="display: block; margin-left: auto; margin-right: auto;">
        <p class="mb-3">Reboot the device:</p>
        <button type="button" id="rebootDeviceBtn" class="btn btn-danger action-button w-100" 
                style="max-width: 220px; display: block; margin-left: auto; margin-right: auto;">Reboot Device</button>
    </div>
</div>
)=====";
void loadRfMappingsFromEEPROM();
bool saveRfMappingsToEEPROM();
void handleSerialCommands();
void printHelp();
void initialize_cc1101();
bool loadConfiguration();
void saveConfiguration();
void interactiveConfigSetup();
void handleRoot();
void handleNotFound();

void setClock();
void parseAndAddHeader(HTTPClient& http, const char* headerLine);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length); // WebSocket event handler
void RPCEvent(WStype_t type, uint8_t * payload, size_t length); // RPC WebSocket client event handler
void connectToDevice();
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

void sendCardWrapperStart() {
    server.sendContent("<div class=\"mb-3\" style=\"max-width: 720px; margin-left: auto; margin-right: auto;\">");
}

void sendCardWrapperEnd() {
    server.sendContent("</div>");
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
  server.send(200, "text/html", ""); 

  server.sendContent_P(PAGE_MAIN_HEADER_START);
  server.sendContent_P(PAGE_FILE_INPUT_STYLE);
  server.sendContent_P(PAGE_STYLE);
  server.sendContent_P(PAGE_BODY_START);
  
  // Send JavaScript first so functions are defined before HTML elements try to use them
  server.sendContent_P(PAGE_SCRIPT); 
  server.sendContent_P(PAGE_SCRIPT_PART2);

  // Status Card Placeholders (content filled by JavaScript)
  sendCardWrapperStart();
  server.sendContent_P(PAGE_STATUS_CARD_START);
  server.sendContent("<div class='rf-controls-row' style='margin-bottom: 0;'>");
  server.sendContent("  <div class='mb-2'><strong>WiFi:</strong> <span id='wifi-connected-val'>Loading...</span></div>");
  server.sendContent("  <div class='mb-2'><strong>IP:</strong> <span id='wifi-ip-val'>Loading...</span></div>");
  server.sendContent("  <div class='mb-2'><strong>MAC:</strong> <span id='wifi-mac-val'>Loading...</span></div>");
  server.sendContent("  <div class='mb-2'><strong>Hostname:</strong> <span id='wifi-hostname-val'>Loading...</span></div>");
  server.sendContent("</div>");
  server.sendContent_P(PAGE_STATUS_CARD_END);
  sendCardWrapperEnd();

  // --- CC1101 Config Card ---
  sendCardWrapperStart();
  server.sendContent_P(PAGE_CC1101_CONFIG_CARD_START);
  
  // CC1101 Status
  server.sendContent("<div class='rf-controls-row'>");
  server.sendContent("  <div class='mb-2'><strong>Frequency:</strong> <span id='cc1101-freq-val'>Loading...</span></div>");
  server.sendContent("  <div class='mb-2'><strong>Bandwidth:</strong> <span id='cc1101-bandwidth-val'>Loading...</span></div>");
  server.sendContent("  <div class='mb-2'><strong>Data Rate:</strong> <span id='cc1101-rate-val'>Loading...</span></div>");
  server.sendContent("  <div class='mb-2'><strong>Mode:</strong> <span id='cc1101-mode-val'>Loading...</span></div>");
  server.sendContent("</div>");

  server.sendContent("<hr class='border-secondary mt-3 mb-3'>"); // Separator for config form

  server.sendContent("<div class='rf-controls-row'>");

  // CC1101 Configuration - Frequency
  server.sendContent("  <div style=\"max-width: 220px; width: 100%; margin-bottom: 1rem; text-align: center;\">");
  server.sendContent("  <label for='cc1101Frequency' class='form-label d-block mb-2'>Frequency (MHz)</label>");
  server.sendContent("  <input type='number' class='form-control-dark text-center mb-2' id='cc1101Frequency' name='frequency' step='0.001' min='387.000' max='464.000' required>");
  server.sendContent("  <button type='button' id='setFrequencyBtn' class='btn btn-primary w-100 action-button'>Set Frequency</button>");
  server.sendContent("  </div>");

  // CC1101 Configuration - Bandwidth
  server.sendContent("  <div style=\"max-width: 220px; width: 100%; margin-bottom: 1rem; text-align: center;\">");
  server.sendContent("  <label for='cc1101Bandwidth' class='form-label d-block mb-2'>Bandwidth (KHz)</label>");
  server.sendContent("  <select class='form-control-dark text-center mb-2' id='cc1101Bandwidth' name='bandwidth' required>");
  server.sendContent("      <option value='58'>58</option><option value='67'>67</option><option value='81'>81</option><option value='101' selected>101</option><option value='116'>116</option><option value='135'>135</option>");
  server.sendContent("      <option value='162'>162</option><option value='203'>203</option><option value='232'>232</option><option value='270'>270</option><option value='325'>325</option><option value='406'>406</option>");
  server.sendContent("      <option value='464'>464</option><option value='541'>541</option><option value='650'>650</option><option value='812'>812</option>");
  server.sendContent("    </select>");
  server.sendContent("  <button type='button' id='setBandwidthBtn' class='btn btn-primary w-100 action-button'>Set Bandwidth</button>");
  server.sendContent("  </div>");

  // CC1101 Configuration - Data Rate
  server.sendContent("  <div style=\"max-width: 220px; width: 100%; margin-bottom: 1rem; text-align: center;\">");
  server.sendContent("  <label for='cc1101DataRate' class='form-label d-block mb-2'>Data Rate (kBaud)</label>");
  server.sendContent("  <input type='number' class='form-control-dark text-center mb-2' id='cc1101DataRate' name='data_rate' step='0.1' min='0.6' max='600.0' required>");
  server.sendContent("  <button type='button' id='setDataRateBtn' class='btn btn-primary w-100 action-button'>Set Data Rate</button>");
  server.sendContent("  </div>");
  server.sendContent("</div>");

  server.sendContent_P(PAGE_CC1101_CONFIG_CARD_END);
  sendCardWrapperEnd();

  sendCardWrapperStart();
  server.sendContent_P(PAGE_RF_CONTROL_CARD);
  sendCardWrapperEnd();

  // --- Firmware Update Card ---
  sendCardWrapperStart();
  server.sendContent_P(PAGE_FIRMWARE_UPDATE_CARD);
  sendCardWrapperEnd();

  server.sendContent_P(PAGE_FOOTER);
  server.sendContent("");
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

void connectToDevice() {
    // This function needs a target IP to connect to.
    // It's no longer suitable for a global, automatic connection attempt without a global IP.
    // It might be called by individual actions with a specific IP in the future.
    Serial.println("connectToDevice() called - Note: This function is now intended for on-demand connection with a specific IP (not yet implemented here).");
    // The actual connection result will be handled in RPCEvent
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