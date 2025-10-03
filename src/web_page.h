#pragma once
#include <Arduino.h>

const char PAGE_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>RF Bridge Control</title>
    <style>
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

        <div class="mb-3" style="max-width: 720px; margin-left: auto; margin-right: auto;">
            <div class="card">
                <div class="card-header">Status</div>
                <div class="card-body">
                    <div class='rf-controls-row' style='margin-bottom: 0;'>
                        <div class='mb-2'><strong>WiFi:</strong> <span id='wifi-connected-val'>Loading...</span></div>
                        <div class='mb-2'><strong>IP:</strong> <span id='wifi-ip-val'>Loading...</span></div>
                        <div class='mb-2'><strong>MAC:</strong> <span id='wifi-mac-val'>Loading...</span></div>
                        <div class='mb-2'><strong>Hostname:</strong> <span id='wifi-hostname-val'>Loading...</span></div>
                    </div>
                </div>
            </div>
        </div>

        <div class="mb-3" style="max-width: 720px; margin-left: auto; margin-right: auto;">
            <div class="card">
                <div class="card-header">CC1101 Config <span id="cc1101ConfigPane-result" class="action-result"></span></div>
                <div class="card-body">
                    <div class='rf-controls-row'>
                        <div class='mb-2'><strong>Frequency:</strong> <span id='cc1101-freq-val'>Loading...</span></div>
                        <div class='mb-2'><strong>Bandwidth:</strong> <span id='cc1101-bandwidth-val'>Loading...</span></div>
                        <div class='mb-2'><strong>Data Rate:</strong> <span id='cc1101-rate-val'>Loading...</span></div>
                        <div class='mb-2'><strong>Mode:</strong> <span id='cc1101-mode-val'>Loading...</span></div>
                    </div>
                    <hr class='border-secondary mt-3 mb-3'>
                    <div class='rf-controls-row'>
                        <div style="max-width: 220px; width: 100%; margin-bottom: 1rem; text-align: center;">
                            <label for='cc1101Frequency' class='form-label d-block mb-2'>Frequency (MHz)</label>
                            <input type='number' class='form-control-dark text-center mb-2' id='cc1101Frequency' name='frequency' step='0.001' min='387.000' max='464.000' required>
                            <button type='button' id='setFrequencyBtn' class='btn btn-primary w-100 action-button'>Set Frequency</button>
                        </div>
                        <div style="max-width: 220px; width: 100%; margin-bottom: 1rem; text-align: center;">
                            <label for='cc1101Bandwidth' class='form-label d-block mb-2'>Bandwidth (KHz)</label>
                            <select class='form-control-dark text-center mb-2' id='cc1101Bandwidth' name='bandwidth' required>
                                <option value='58'>58</option><option value='67'>67</option><option value='81'>81</option><option value='101' selected>101</option><option value='116'>116</option><option value='135'>135</option>
                                <option value='162'>162</option><option value='203'>203</option><option value='232'>232</option><option value='270'>270</option><option value='325'>325</option><option value='406'>406</option>
                                <option value='464'>464</option><option value='541'>541</option><option value='650'>650</option><option value='812'>812</option>
                            </select>
                            <button type='button' id='setBandwidthBtn' class='btn btn-primary w-100 action-button'>Set Bandwidth</button>
                        </div>
                        <div style="max-width: 220px; width: 100%; margin-bottom: 1rem; text-align: center;">
                            <label for='cc1101DataRate' class='form-label d-block mb-2'>Data Rate (kBaud)</label>
                            <input type='number' class='form-control-dark text-center mb-2' id='cc1101DataRate' name='data_rate' step='0.1' min='0.6' max='600.0' required>
                            <button type='button' id='setDataRateBtn' class='btn btn-primary w-100 action-button'>Set Data Rate</button>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <div class="mb-3" style="max-width: 720px; margin-left: auto; margin-right: auto;">
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
        </div>

        <div class="mb-3" style="max-width: 720px; margin-left: auto; margin-right: auto;">
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
        </div>

        <p class="text-center text-muted mt-4">ESP8266 RF Bridge</p>
    </div>

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
</body>
</html>
)=====";