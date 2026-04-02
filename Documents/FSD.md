# MouseKeepAlive — Functional Specification Document (FSD)

**Version**: 1.0  
**Date**: 2026-04-01  
**Repository**: https://github.com/FranzKugler/MouseKeepAlive

---

## 1. System Overview

### 1.1 Purpose

MouseKeepAlive prevents Windows workstations from entering lock or sleep mode by emulating a secondary Bluetooth LE HID mouse that periodically sends small mouse-movement reports ("wiggles"). The device operates autonomously after initial provisioning — no software is required on the target Windows machines beyond standard Bluetooth pairing.

### 1.2 Problem Statement

Users who are occupied with non-keyboard/mouse tasks (reading, watching, monitoring remote sessions) frequently have their Windows workstations lock automatically due to inactivity timeouts enforced by IT policy. MouseKeepAlive provides a hardware-based solution that bypasses this timeout without disabling it system-wide.

### 1.3 Users / Stakeholders

| Stakeholder | Interest |
|-------------|---------|
| End user | Seamless keep-alive across 1–2 Windows machines |
| Developer/operator | Ability to update firmware OTA without USB access |

### 1.4 Goals

- Emulate a BLE HID mouse and maintain persistent connections to up to 2 Windows hosts simultaneously.
- Send configurable mouse wiggles at configurable intervals to prevent inactivity lockout.
- Support OTA firmware updates over WiFi (USB port is used for power only).
- Expose a captive portal for initial provisioning and configuration.

### 1.5 Non-Goals

- Does not act as a USB HID device.
- Does not simulate keyboard input.
- Does not disable or bypass Windows lock-screen policies — it only prevents triggering them.
- Does not support macOS, Linux, or Android target hosts in v1.

### 1.6 High-Level System Flow

```
[Windows PC 1] <--- BLE HID (HOGP) ---\
                                        [ESP32-S3 MouseKeepAlive Device]
[Windows PC 2] <--- BLE HID (HOGP) ---/         |
                                               [WiFi STA]
                                                 |
                                         [Home/Office Router]
                                                 |
                                     [OTA HTTP Server / Workbench]
```

On power-up, the device connects to the provisioned WiFi network, starts BLE advertising, reconnects to bonded Windows hosts, and begins the periodic wiggle timer. An HTTP endpoint exposes status and OTA trigger.

---

## 2. System Architecture

### 2.1 Logical Architecture

| Subsystem | Responsibility |
|-----------|---------------|
| **BLE HID Manager** | Advertise, connect, bond, and send HID mouse reports to up to 2 hosts |
| **Wiggle Scheduler** | Fire timer-driven wiggle events and dispatch HID reports to all connected hosts |
| **WiFi Manager** | Connect to STA, reconnect on drop, expose status |
| **OTA Manager** | Download and apply firmware updates via HTTP; A/B partition rollback |
| **Captive Portal** | AP-mode web server for initial WiFi provisioning and app configuration |
| **Config Manager** | Read/write NVS parameters; enforce defaults; factory reset |
| **Logger** | Serial + UDP log forwarding to workbench |
| **HTTP Server** | Status endpoint, OTA trigger, runtime config update |

**Runtime Interaction (FreeRTOS tasks):**

```
boot
 └── config_manager_init()      (NVS load / defaults)
 └── wifi_manager_start()       (STA or AP fallback)
      └── [STA connected] → http_server_start()
                           → ota_manager_init()
                           → udp_log_init()
 └── ble_hid_manager_start()    (advertise + reconnect bonds)
 └── wiggle_scheduler_start()   (FreeRTOS timer)
```

### 2.2 Hardware / Platform Architecture

| Item | Detail |
|------|--------|
| MCU | Seeed XIAO ESP32-S3 Plus |
| Flash | 16 MB (Quad SPI) |
| PSRAM | 8 MB (Octal SPI, 80 MHz) |
| USB | USB-C — used for power only (no serial/HID) |
| BLE | Bluetooth 5.0 LE (ESP32-S3 integrated radio; Classic BT not supported) |
| WiFi | 802.11 b/g/n 2.4 GHz STA + AP concurrent |
| Button | BOOT button — repurposed for factory reset (5 s hold) |
| Power | USB 5 V / onboard 3.3 V regulator |

> **Note**: The ESP32-S3 does not support Classic Bluetooth (BR/EDR). BLE HID uses the
> HID over GATT Profile (HOGP / BT SIG specification). Windows 10/11 supports HOGP
> natively and will enumerate the device as a standard HID mouse.

### 2.3 Software Architecture

**Framework**: ESP-IDF (latest stable release)  
**BLE Stack**: NimBLE (ESP-IDF built-in, preferred over BlueDroid for lower footprint)

**Partition Table** (custom, 16 MB flash):

| Partition | Type | Size | Notes |
|-----------|------|------|-------|
| nvs | data/nvs | 0x6000 | Config, credentials, bonds |
| otadata | data/ota | 0x2000 | OTA state |
| phy_init | data/phy | 0x1000 | RF calibration |
| factory | app/factory | 1 MB | Emergency fallback |
| ota_0 | app/ota_0 | 3 MB | A partition |
| ota_1 | app/ota_1 | 3 MB | B partition |
| spiffs | data/spiffs | 2 MB | Portal web assets (assumed) |

**Key sdkconfig settings** (inherited from sibling project `esptest`):

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_TYPE_AUTO=y
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2
CONFIG_PARTITION_TABLE_CUSTOM=y
```

**Update Model**: HTTP-triggered OTA via dual A/B partitions. Workbench hosts the `.bin`
file and sends a POST to `/ota/trigger`. Device downloads, verifies (ESP-IDF app_update
integrity check), writes to inactive partition, and reboots. Rollback fires automatically
if the new firmware fails to call `esp_ota_mark_app_valid_cancel_rollback()` within 30 s.

---

## 3. Implementation Phases

### 3.1 Phase 1 — Infrastructure Foundation

**Scope**: Establish the full non-BLE infrastructure: WiFi, OTA, captive portal,
NVS config, UDP logging, HTTP status/OTA endpoint. Phase 1 produces a deployable
base firmware that can be updated OTA — no BLE HID functionality yet.

**Deliverables**:
- ESP-IDF project with custom partition table, sdkconfig for XIAO ESP32-S3 Plus
- WiFi STA + captive portal AP-mode fallback
- NVS-backed configuration (wiggle interval, amplitude, WiFi creds, UDP log target)
- HTTP server: `GET /status`, `POST /ota/trigger`, `POST /config`
- UDP log forwarding to workbench
- OTA download + A/B partition swap + rollback
- Factory reset via BOOT button (5 s hold)
- GitHub repo initialised at https://github.com/FranzKugler/MouseKeepAlive

**Exit Criteria**:
- [ ] Device provisions via captive portal and connects to WiFi
- [ ] OTA update succeeds: flash v1 → v2 → verify version via `/status`
- [ ] OTA rollback test passes (intentionally broken firmware rolls back to v1)
- [ ] NVS config persists across power cycle
- [ ] Factory reset clears NVS and re-enters AP mode
- [ ] UDP logs visible in workbench during boot and OTA

**Dependencies**: Workbench available for OTA testing; ESP-IDF ≥ 5.2 installed.

---

### 3.2 Phase 2 — BLE HID Mouse Emulation

**Scope**: Implement BLE HID peripheral (HOGP) using NimBLE. Device shall advertise,
accept connections from up to 2 Windows hosts, maintain bonds, and send periodic mouse
wiggle reports. Workbench shall simulate the Windows "receiver" side for automated tests.

**Deliverables**:
- NimBLE HOGP HID mouse service (Mouse Report Descriptor: buttons + X/Y delta)
- BLE advertising with device name `MouseKeepAlive` and HID appearance code (0x03C2)
- Pairing + bonding support (stored in NVS); reconnect on boot
- Wiggle scheduler: configurable interval (default 300 s) and XY delta (default ±5)
- Configurable via captive portal and HTTP `POST /config`
- Simultaneous 2-connection support (NimBLE `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2`)
- Coexistence test: BLE + WiFi operational simultaneously (OTA while wiggling)

**Exit Criteria**:
- [ ] Windows PC1 pairs with device and sees mouse movement every configured interval
- [ ] Windows PC2 pairs simultaneously; both receive wiggles independently
- [ ] Bonds survive reboot: both PCs reconnect without re-pairing
- [ ] Wiggle interval and amplitude updated live via HTTP `/config`
- [ ] OTA update completes while BLE connections are active; device reconnects after reboot
- [ ] BLE coexistence test passes (EC-BLE-203)

**Dependencies**: Phase 1 complete; 2 Windows test machines; workbench BLE scan available.

---

## 4. Functional Requirements

### 4.1 Functional Requirements (FR)

#### FR-1: BLE HID Mouse Emulation

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-1.1 | Must | The device shall implement a BLE HID peripheral using the HID over GATT Profile (HOGP) and expose a standard HID Mouse report descriptor (X/Y delta, buttons). |
| FR-1.2 | Must | The device shall support at least 2 simultaneous BLE connections to Windows host machines. |
| FR-1.3 | Must | The device shall advertise continuously when fewer than 2 hosts are connected, resuming advertising after any disconnection. |
| FR-1.4 | Must | The device shall send a mouse wiggle HID report at the configured interval to all currently connected hosts. |
| FR-1.5 | Must | The wiggle shall consist of a move report (+X, +Y delta) followed by a return report (−X, −Y delta) within 200 ms to simulate natural mouse jitter without displacing the cursor. |
| FR-1.6 | Must | The device shall support BLE pairing and bonding; bonding information shall persist in NVS. |
| FR-1.7 | Should | The device shall automatically reconnect to bonded hosts on boot without requiring re-pairing. |
| FR-1.8 | Should | The device shall log BLE connection, disconnection, pairing, and bonding events. |
| FR-1.9 | May | Each host connection slot shall be independently configurable for wiggle interval and amplitude. |

#### FR-2: Wiggle Configuration

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-2.1 | Must | The wiggle interval shall be configurable in the range 10–3600 seconds (default: 300 s). |
| FR-2.2 | Must | The wiggle XY delta shall be configurable in the range 1–50 pixels (default: 5). |
| FR-2.3 | Must | Configuration shall take effect on the next scheduled wiggle event without rebooting. |
| FR-2.4 | Must | Configuration shall persist in NVS across reboots and power cycles. |

#### FR-3: WiFi & Network

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-3.1 | Must | The device shall connect to a configured WiFi network in STA mode. |
| FR-3.2 | Must | The device shall automatically reconnect to WiFi on disconnect without rebooting. |
| FR-3.3 | Must | WiFi credentials shall be stored in NVS (encrypted by ESP-IDF NVS encryption). |
| FR-3.4 | Must | The device shall start AP mode (captive portal) when no valid WiFi credentials exist in NVS. |
| FR-3.5 | Should | The device shall log WiFi connection status changes (connected, disconnected, IP assigned). |

#### FR-4: Captive Portal & Provisioning

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-4.1 | Must | The AP shall use SSID `MouseKeepAlive-{MAC_LAST_4}` and serve a captive portal. |
| FR-4.2 | Must | The portal shall allow WiFi network selection and credential entry. |
| FR-4.3 | Must | The portal shall allow configuration of wiggle interval, amplitude, and UDP log target. |
| FR-4.4 | Must | Configuration submitted via the portal shall be saved to NVS before the device reboots. |
| FR-4.5 | Should | AP mode shall be triggerable at runtime by holding the BOOT button for 5 seconds. |
| FR-4.6 | Should | AP mode shall auto-deactivate after 5 minutes of inactivity (no client connected). |

#### FR-5: OTA Firmware Update

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-5.1 | Must | The device shall support OTA firmware update via HTTP download triggered by `POST /ota/trigger`. |
| FR-5.2 | Must | OTA shall use ESP-IDF dual A/B OTA partitions. |
| FR-5.3 | Must | The device shall rollback to the previous firmware if the new firmware fails to mark itself valid within 30 seconds of boot. |
| FR-5.4 | Must | The device shall reject firmware with invalid magic bytes, wrong chip target, or checksum failure. |
| FR-5.5 | Should | OTA progress shall be logged (start, percentage, complete/error). |
| FR-5.6 | Should | The current firmware version shall be reported via `GET /status`. |
| FR-5.7 | Should | Active BLE connections shall be maintained during the OTA download phase; BLE reconnection shall occur automatically after reboot. |

#### FR-6: Configuration & NVS

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-6.1 | Must | The device shall use factory defaults for all parameters when NVS is empty or unreadable. |
| FR-6.2 | Must | Factory reset shall be triggerable by holding the BOOT button for 5 seconds. |
| FR-6.3 | Must | Factory reset shall erase all NVS configuration and BLE bonding data, then enter captive portal AP mode. |
| FR-6.4 | Should | Factory reset shall also be triggerable via `POST /config/reset` HTTP endpoint. |

#### FR-7: Logging

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-7.1 | Must | The device shall output structured log messages via serial UART0 (even though USB is used for power — UART0 is available via the pin header for development). |
| FR-7.2 | Should | The device shall forward log messages via UDP to a configurable host:port (default: workbench IP, port 5555). |
| FR-7.3 | Must | Log messages shall include firmware version, timestamp, component tag, and message. |
| FR-7.4 | Must | The device shall log boot sequence including reset reason and firmware version. |
| FR-7.5 | Must | WiFi credentials and BLE bonding keys shall never appear in any log output. |

### 4.2 Non-Functional Requirements (NFR)

| ID | Priority | Requirement |
|----|----------|-------------|
| NFR-1.1 | Must | Mouse wiggle HID reports shall be dispatched within 500 ms of the scheduled timer tick. |
| NFR-1.2 | Should | BLE reconnection to a bonded host shall complete within 15 seconds of the host becoming available. |
| NFR-1.3 | Must | An OTA download shall not cause a missed wiggle event — OTA and wiggle scheduler shall operate concurrently. |
| NFR-1.4 | Must | The device shall recover from any unexpected reset and resume normal operation (WiFi + BLE + wiggling) within 60 seconds. |
| NFR-1.5 | Should | Free heap shall remain above 30 KB during normal operation (BLE + WiFi + OTA concurrent). |
| NFR-1.6 | Should | BLE advertising shall restart within 1 second of a host disconnection. |
| NFR-1.7 | Must | The device shall operate continuously (no scheduled maintenance reboots required). |

### 4.3 Constraints

- **BLE stack**: ESP32-S3 supports BLE only (no Classic Bluetooth BR/EDR). HID is implemented via HOGP.
- **Simultaneous connections**: NimBLE `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` must be set to 2; radio coexistence (BLE + WiFi) managed by ESP-IDF coex scheduler.
- **USB port**: USB-C is used for 5V power only. Serial debug access requires the UART0 test pads or the workbench RFC2217 serial bridge.
- **OTA server**: Firmware `.bin` files are hosted by the ESP32-Workbench HTTP service.
- **Apple native tools**: Development and flashing toolchain must be compatible with macOS (ESP-IDF is supported natively on macOS).

---

## 5. Risks, Assumptions & Dependencies

### 5.1 Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|-----------|
| BLE HID + WiFi coexistence causes wiggle jitter or missed reports | Medium | Medium | Use ESP-IDF BLE/WiFi coexistence mode; tune coex intervals; validate with EC-BLE-203 |
| Windows host unilaterally disconnects BLE HID device (power management) | High | High | Implement aggressive reconnection loop in BLE manager; set HID report interval hint in HOGP characteristics |
| Simultaneous 2-connection BLE HID not supported by NimBLE HOGP implementation | Medium | High | Validate early in Phase 2 spike; fallback: time-multiplex connections if concurrent fails |
| Windows Bluetooth stack rejects HOGP peripheral without encryption | Low | High | Enable BLE pairing with `BLE_SM_IO_CAP_NO_IO`; test with Windows 11 Bluetooth settings |
| PSRAM not needed for this application and wastes power | Low | Low | Accept; PSRAM is on the chosen module and always available |

### 5.2 Assumptions

- (assumed) WiFi OTA server is the ESP32-Workbench running on the local network; URL format: `http://workbench.local:8080/api/firmware/...`
- (assumed) The captive portal web assets (HTML/CSS/JS) are served from SPIFFS.
- (assumed) The BOOT button (GPIO0) is used for factory reset; no additional button hardware is required.
- (assumed) Each Windows machine is counted as one BLE connection slot.
- (assumed) The wiggle pattern is a symmetric back-and-forth: move (+dx, +dy), pause 100 ms, move (−dx, −dy) — net cursor displacement is zero.
- (assumed) NVS encryption is enabled at the ESP-IDF level (flash encryption not required for v1).

### 5.3 External Dependencies

| Dependency | Version / Source | Risk |
|------------|-----------------|------|
| ESP-IDF | ≥ 5.2 | API changes between versions |
| NimBLE (bundled with ESP-IDF) | - | HOGP multi-connection support |
| ESP32-Workbench | https://github.com/SensorsIot/Universal-ESP32-Workbench | OTA server, BLE test, serial relay |
| Windows 10/11 Bluetooth HID stack | Target OS | HID enumeration and pairing behavior |

---

## 6. Interface Specifications

### 6.1 BLE Interface

**Advertising**:
- Device name: `MouseKeepAlive`
- Appearance: Generic HID Mouse (0x03C2)
- Advertising interval: 100 ms (connectable undirected)
- Directed advertising: attempted to bonded hosts on boot

**GATT Services (HOGP)**:

| Service | UUID | Description |
|---------|------|-------------|
| HID Service | 0x1812 | Core HID over GATT |
| Battery Service | 0x180F | Battery level (static 100%) |
| Device Information | 0x180A | Manufacturer, model, firmware version |

**HID Mouse Report Descriptor** (5-byte input report):

| Byte | Field | Description |
|------|-------|-------------|
| 0 | Buttons | Bits 0–2: Left, Right, Middle button (always 0x00) |
| 1 | X delta | Signed int8: horizontal movement |
| 2 | Y delta | Signed int8: vertical movement |
| 3 | Wheel | Signed int8: scroll wheel (always 0x00) |
| 4 | AC Pan | Signed int8: horizontal scroll (always 0x00) |

**Wiggle Sequence** (one interval tick):
1. Send report: `[0x00, +dx, +dy, 0x00, 0x00]`
2. Wait 100 ms
3. Send report: `[0x00, -dx, -dy, 0x00, 0x00]`

### 6.2 HTTP API

Base URL: `http://<device-ip>/`

| Method | Endpoint | Description |
|--------|---------|-------------|
| GET | `/status` | Returns JSON: firmware version, WiFi RSSI, BLE connection count, wiggle config, uptime |
| POST | `/config` | Update wiggle interval, amplitude, UDP log target. Body: JSON |
| POST | `/ota/trigger` | Begin OTA download. Body: `{"url": "http://..."}` |
| POST | `/config/reset` | Trigger factory reset |

**`GET /status` response example**:

```json
{
  "firmware": "1.0.0",
  "uptime_s": 3600,
  "wifi_rssi": -58,
  "ble_connections": 2,
  "wiggle_interval_s": 300,
  "wiggle_amplitude": 5,
  "next_wiggle_in_s": 47
}
```

**`POST /config` body**:

```json
{
  "wiggle_interval_s": 300,
  "wiggle_amplitude": 5,
  "udp_log_host": "192.168.1.100",
  "udp_log_port": 5555
}
```

### 6.3 NVS Configuration Schema

Namespace: `mka_config`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `wifi_ssid` | String | — | WiFi network SSID |
| `wifi_pass` | String (encrypted) | — | WiFi password |
| `wiggle_interval` | UInt32 | 300 | Wiggle interval in seconds |
| `wiggle_amplitude` | UInt8 | 5 | XY delta magnitude (pixels) |
| `udp_log_host` | String | — | UDP log destination IP |
| `udp_log_port` | UInt16 | 5555 | UDP log destination port |
| `ble_bond_count` | UInt8 | 0 | Number of stored bonds |

BLE bond data is stored by NimBLE in its own NVS namespace (`ble_bond`).

### 6.4 Captive Portal Configuration Pages

| Page | Path | Fields |
|------|------|--------|
| WiFi Setup | `/portal/wifi` | SSID (scan list), Password |
| App Config | `/portal/config` | Wiggle Interval (s), Wiggle Amplitude, UDP Log Host, UDP Log Port |
| Status | `/portal/status` | Read-only: firmware version, IP, connected BLE hosts |
| Factory Reset | `/portal/reset` | Confirmation button |

---

## 7. Operational Procedures

### 7.1 Hardware Setup

| What | Where |
|------|-------|
| ESP32-S3 USB | Workbench SLOT1 — `/dev/ttyACM0` |
| RFC2217 serial | `rfc2217://workbench.local:4001` |
| GPIO BOOT | GPIO 18 (workbench control) |
| GPIO EN (reset) | GPIO 17 (workbench control) |
| Workbench host | `workbench.local:8080` (172.22.50.10) |
| UDP log sink | `workbench.local:5555` |
| OTA firmware URL | `http://workbench.local:8080/api/firmware/MouseKeepAlive/MouseKeepAlive.bin` |

#### Project-Specific Values

| Value | Setting |
|-------|---------|
| WiFi portal SSID | `MouseKeepAlive-XXXX` (last 4 hex digits of SoftAP MAC) |
| Workbench AP SSID | `WB-TestAP` |
| Workbench AP password | `wbtestpass` |
| BLE device name | `MouseKeepAlive` |
| NVS namespace | `mka_config` |
| NUS RX characteristic | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |

---

### 7.2 Flashing (Initial or Recovery)

The device uses RFC2217 remote serial for flashing. From the project root after `idf.py build`:

```bash
# Flash all partitions via workbench RFC2217
source ~/esp/esp-idf/export.sh
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset \
  --port rfc2217://workbench.local:4001 \
  write_flash "@build/flash_args"
```

Or use the `esp-idf-handling` skill — it handles download mode (GPIO BOOT=18, EN=17) and flashing via the workbench.

---

### 7.3 WiFi Provisioning

WiFi provisioning is required before OTA, UDP logs, or HTTP endpoints are usable. Do this once after the initial flash.

**Phase 1 — Ensure device is in AP mode.**

If freshly flashed, the device boots straight into AP mode (no NVS credentials). If previously provisioned, trigger a WiFi reset first:

```bash
# BLE WiFi reset — connect to device's NUS and send CMD_WIFI_RESET
curl -s -X POST http://workbench.local:8080/api/ble/scan \
  -H 'Content-Type: application/json' -d '{"timeout": 5}' | jq .

curl -s -X POST http://workbench.local:8080/api/ble/connect \
  -H 'Content-Type: application/json' -d '{"name": "MouseKeepAlive"}'

curl -s -X POST http://workbench.local:8080/api/ble/write \
  -H 'Content-Type: application/json' \
  -d '{"characteristic": "6e400002-b5a3-f393-e0a9-e50e24dcca9e", "data": "20"}'
```

Confirm via serial monitor: `"WiFi reset requested"` → `"WiFi credentials erased"` → reboot → `"No WiFi credentials"` → `"AP mode: SSID='MouseKeepAlive-XXXX'"`.

**Phase 2 — Provision via captive portal.**

```bash
curl -s -X POST http://workbench.local:8080/api/enter-portal \
  -H 'Content-Type: application/json' \
  -d '{
    "portal_ssid": "MouseKeepAlive-XXXX",
    "ssid":        "WB-TestAP",
    "password":    "wbtestpass"
  }'
```

Expected serial events: `"Portal page requested"` → `"Credentials saved"` → reboot → `"STA mode, connecting to 'WB-TestAP'"` → `"STA got IP"`.

**Troubleshooting provisioning:**

- `enter-portal` times out → confirm device logged `"AP mode: SSID='...'"` before calling enter-portal
- `"STA disconnect, retry"` loops → check workbench AP is up; verify SSID/password values
- Portal page not served → check serial for any HTTP server error after AP start

---

### 7.4 BLE Commands

The workbench connects to the device's NUS (Nordic UART Service) to send control commands.

```bash
# Scan for device
curl -s -X POST http://workbench.local:8080/api/ble/scan \
  -H 'Content-Type: application/json' -d '{"timeout": 5}'

# Connect
curl -s -X POST http://workbench.local:8080/api/ble/connect \
  -H 'Content-Type: application/json' -d '{"name": "MouseKeepAlive"}'

# Send command (hex bytes written to NUS RX characteristic)
curl -s -X POST http://workbench.local:8080/api/ble/write \
  -H 'Content-Type: application/json' \
  -d '{"characteristic": "6e400002-b5a3-f393-e0a9-e50e24dcca9e", "data": "<hex>"}'

# Disconnect
curl -s -X POST http://workbench.local:8080/api/ble/disconnect
```

#### BLE Command Reference

| Opcode | Hex example | Description | Expected log |
|--------|-------------|-------------|--------------|
| `0x10` | `10` | Trigger OTA (default URL) | `"OTA update requested"` |
| `0x10 <url>` | `10687474703a2f2f...` | Trigger OTA with custom URL | `"OTA update requested"` |
| `0x20` | `20` | WiFi reset (erase creds + reboot) | `"WiFi reset requested"` → `"WiFi credentials erased"` |

---

### 7.5 OTA Firmware Update

```bash
# Step 1: Build new firmware
source ~/esp/esp-idf/export.sh && idf.py build

# Step 2: Upload binary to workbench
curl -s -X POST http://workbench.local:8080/api/firmware/upload \
  -F "project=MouseKeepAlive" \
  -F "file=@build/MouseKeepAlive.bin"

# Step 3a: Trigger OTA via HTTP relay (preferred — device must have WiFi IP)
curl -s -X POST http://workbench.local:8080/api/wifi/http \
  -H 'Content-Type: application/json' \
  -d '{
    "method": "POST",
    "path": "/ota/trigger",
    "body": {"url": "http://workbench.local:8080/api/firmware/MouseKeepAlive/MouseKeepAlive.bin"}
  }'

# Step 3b: Trigger OTA via BLE (alternative — works even before WiFi is stable)
curl -s -X POST http://workbench.local:8080/api/ble/write \
  -H 'Content-Type: application/json' \
  -d '{"characteristic": "6e400002-b5a3-f393-e0a9-e50e24dcca9e", "data": "10"}'
```

Monitor OTA via UDP logs until `"OTA succeeded"` or `"OTA failed"`:

```bash
curl -s "http://workbench.local:8080/api/udplog?limit=50" | jq .
```

---

### 7.6 HTTP Endpoints

All endpoints are available at `http://<device-ip>/` once the device has a WiFi IP. Use the workbench HTTP relay to reach the device without knowing its IP:

```bash
# HTTP relay syntax
curl -s -X POST http://workbench.local:8080/api/wifi/http \
  -H 'Content-Type: application/json' \
  -d '{"method": "GET", "path": "/status"}'
```

| Method | Endpoint | Description |
|--------|---------|-------------|
| GET | `/status` | JSON: firmware version, uptime, WiFi RSSI, BLE connection, wiggle config |
| POST | `/config` | Update `wiggle_interval_s`, `wiggle_amplitude`, `udp_log_host`, `udp_log_port` |
| POST | `/ota/trigger` | Start OTA download. Body: `{"url": "http://..."}` |
| POST | `/config/reset` | Factory reset (erases NVS, reboots into AP mode) |

---

### 7.7 Log Monitoring

**Serial monitor** (boot events, crashes):

```bash
curl -s -X POST http://workbench.local:8080/api/serial/monitor \
  -H 'Content-Type: application/json' \
  -d '{"slot": "SLOT1", "pattern": "Init complete", "timeout": 30}'
```

**UDP logs** (live operation after WiFi is up):

```bash
curl -s "http://workbench.local:8080/api/udplog?limit=100" | jq .
```

---

### 7.8 Factory Reset

**Via BOOT button**: Hold GPIO0 (physical BOOT button) for 5 seconds. Device erases NVS and reboots into AP mode.

**Via HTTP**:
```bash
curl -s -X POST http://workbench.local:8080/api/wifi/http \
  -H 'Content-Type: application/json' \
  -d '{"method": "POST", "path": "/config/reset"}'
```

**Via workbench NVS erase** (recovery when device is unresponsive):
```bash
curl -s -X POST http://workbench.local:8080/api/serial/nvs-erase \
  -H 'Content-Type: application/json' -d '{"slot": "SLOT1"}'
```

---

## 8. Verification & Validation

### 8.1 Phase 1 Verification

| Step | Feature | Test procedure | Success criteria |
|------|---------|---------------|-----------------|
| 1 | Serial boot log | Flash firmware; monitor serial (see §7.7) | Logs contain: firmware version, `"NVS initialized"`, `"Init complete"` |
| 2 | UDP log delivery | After provisioning, reboot; poll `GET /api/udplog` | Boot log messages appear in workbench UDP sink |
| 3 | AP mode first boot | NVS erase (§7.8); reboot; check serial | `"No WiFi credentials"` → `"AP mode: SSID='MouseKeepAlive-XXXX'"` within 10 s |
| 4 | Captive portal served | Issue enter-portal Phase 1 only; GET `/` via workbench HTTP relay to `192.168.4.1` | Serial: `"Portal page requested"`; HTTP 200 with HTML body |
| 5 | WiFi provisioning | Full provisioning sequence (see §7.3) | Serial: `"Credentials saved"` → `"STA got IP"`; `GET /status` returns `wifi_connected: true` |
| 6 | NVS config persistence | POST `/config` with `wiggle_interval_s: 60`; reboot; GET `/status` | `wiggle_interval_s` is `60` after reboot |
| 7 | Factory reset (button) | Hold GPIO BOOT 5 s via workbench GPIO; monitor serial | `"No WiFi credentials"` → `"AP mode"` logged; `/status` unavailable until re-provisioned |
| 8 | Factory reset (HTTP) | Provision; POST `/config/reset` via HTTP relay | Same as step 7 |
| 9 | OTA v1 → v2 | Build v2 (bump version string); upload to workbench; trigger via HTTP relay (§7.5) | Serial: `"OTA succeeded"`; reboot; `GET /status` returns new version string |
| 10 | OTA rollback | Build intentionally broken firmware (omit `esp_ota_mark_app_valid`); OTA it | Device reboots twice; second boot logs previous version; broken version absent from `/status` |
| 11 | BLE NUS advertising | After boot; BLE scan from workbench | Device name `MouseKeepAlive` appears in scan results |
| 12 | BLE CMD_WIFI_RESET | Connect BLE; write `0x20` to NUS RX | `"WiFi reset requested"` → `"WiFi credentials erased"` in serial/UDP |
| 13 | BLE CMD_OTA | Provision; connect BLE; write `0x10` to NUS RX | `"OTA update requested"` in serial; OTA task starts |
| 14 | Heartbeat | Leave device running 60 s; check UDP logs | `"alive N"` log line appears every ~10 s |
| 15 | WiFi auto-reconnect | Disable workbench AP for 30 s; re-enable | `"STA disconnect, retry"` logged; `"STA got IP"` logged on reconnect |

---

### 8.2 Phase 2 Verification

| Step | Feature | Test procedure | Success criteria |
|------|---------|---------------|-----------------|
| 1 | BLE HID advertising | Boot Phase 2 firmware; workbench BLE scan | Device name `MouseKeepAlive` with appearance 0x03C2 in scan |
| 2 | Windows HID pairing (PC1) | Pair `MouseKeepAlive` via Windows Bluetooth settings | Device Manager shows "MouseKeepAlive" under Mice and other pointing devices |
| 3 | Windows HID pairing (PC2) | Repeat on second PC while PC1 is connected | Both connections shown in `GET /status` as `ble_connections: 2` |
| 4 | Wiggle delivery — PC1 | Set interval to 30 s via POST `/config`; wait 35 s | Pointer on PC1 moves and returns; `wiggle_interval_s` in `/status` is `30` |
| 5 | Wiggle delivery — PC2 | Same 30 s interval; observe both PCs | Both pointers move simultaneously |
| 6 | Wiggle return symmetry | Set amplitude to 5; observe cursor position before and after wiggle | Net cursor displacement is zero after `[+5,+5]` → 100 ms → `[-5,-5]` |
| 7 | Bond persistence | Reboot device; wait 15 s | Both Windows PCs reconnect without re-pairing; `ble_connections: 2` |
| 8 | Advertising resume | Force-disconnect PC1 (disable Bluetooth); wait 2 s; re-enable | Serial: disconnect logged; re-advertise logged; PC1 reconnects |
| 9 | Interval live config | POST `/config` `wiggle_interval_s: 300`; observe | Next wiggle fires ~300 s later; no reboot required |
| 10 | OTA while BLE active | 2 PCs connected; trigger OTA via HTTP relay (§7.5) | Wiggle fires at least once during download; both PCs reconnect after reboot (EC-BLE-203) |

---

### 8.3 Acceptance Tests

| Test | Scenario | Success criteria |
|------|---------|-----------------|
| AT-01 | 24-hour soak | Device runs with 2 PCs paired; UDP logs collected | No crash; `"alive N"` continuous; heap in `/status` stable (>30 KB) |
| AT-02 | Lock-screen prevention | 5-min wiggle interval; PC lock timeout 2 min | PC never locks during 1 h observation |
| AT-03 | Full end-to-end OTA | Provision → pair 2 PCs → OTA → both PCs reconnect | All steps succeed; no manual re-pairing |
| AT-04 | Power-loss recovery | Unplug mid-operation; replug | `"STA got IP"` + `ble_connections: 2` within 60 s |

---

### 8.4 Traceability Matrix

| Requirement | Priority | Test Case(s) | Status |
|-------------|---------|-------------|--------|
| FR-1.1 | Must | TC-BLE-100, HID-PAIR-01 | Covered |
| FR-1.2 | Must | HID-WIGGLE-02 | Covered |
| FR-1.3 | Must | TC-BLE-100, EC-BLE-200 | Covered |
| FR-1.4 | Must | HID-WIGGLE-01, HID-WIGGLE-02 | Covered |
| FR-1.5 | Must | HID-WIGGLE-01 | Covered |
| FR-1.6 | Must | TC-NVS-100, EC-BLE-204 | Covered |
| FR-1.7 | Should | EC-BLE-204 | Covered |
| FR-1.8 | Should | TC-LOG-103 | Covered |
| FR-1.9 | May | — | — |
| FR-2.1 | Must | HID-INTERVAL-01 | Covered |
| FR-2.2 | Must | HID-AMPLITUDE-01 | Covered |
| FR-2.3 | Must | HID-INTERVAL-01, HID-AMPLITUDE-01 | Covered |
| FR-2.4 | Must | TC-NVS-100 | Covered |
| FR-3.1 | Must | WIFI-001, TC-CP-100 | Covered |
| FR-3.2 | Must | WIFI-003, EC-100 | Covered |
| FR-3.3 | Must | EC-NVS-203 | Covered |
| FR-3.4 | Must | TC-CP-100 | Covered |
| FR-3.5 | Should | TC-LOG-103 | Covered |
| FR-4.1 | Must | TC-CP-100 | Covered |
| FR-4.2 | Must | TC-CP-100 | Covered |
| FR-4.3 | Must | TC-CP-100 | Covered |
| FR-4.4 | Must | TC-NVS-100, TC-CP-100 | Covered |
| FR-4.5 | Should | TC-CP-101 | Covered |
| FR-4.6 | Should | EC-CP-202 | Covered |
| FR-5.1 | Must | TC-OTA-100, TC-OTA-100 (P2) | Covered |
| FR-5.2 | Must | TC-OTA-101, EC-OTA-201 | Covered |
| FR-5.3 | Must | TC-OTA-101 | Covered |
| FR-5.4 | Must | EC-OTA-202 | Covered |
| FR-5.5 | Should | TC-LOG-103, EC-LOG-202 | Covered |
| FR-5.6 | Should | TC-OTA-102 | Covered |
| FR-5.7 | Should | TC-OTA-100 (P2), EC-BLE-203 | Covered |
| FR-6.1 | Must | TC-NVS-101 | Covered |
| FR-6.2 | Must | TC-NVS-102 | Covered |
| FR-6.3 | Must | TC-NVS-102, TC-NVS-103 | Covered |
| FR-6.4 | Should | TC-NVS-103 | Covered |
| FR-7.1 | Must | TC-LOG-100 | Covered |
| FR-7.2 | Should | TC-LOG-101 | Covered |
| FR-7.3 | Must | TC-LOG-100 | Covered |
| FR-7.4 | Must | TC-LOG-100 | Covered |
| FR-7.5 | Must | EC-NVS-203 | Covered |
| NFR-1.1 | Must | HID-INTERVAL-01 | Covered |
| NFR-1.2 | Should | EC-BLE-204 | Covered |
| NFR-1.3 | Must | TC-OTA-100 (P2), EC-BLE-203 | Covered |
| NFR-1.4 | Must | AT-04 | Covered |
| NFR-1.5 | Should | EC-BLE-201 | Covered |
| NFR-1.6 | Should | TC-BLE-100, EC-BLE-200 | Covered |
| NFR-1.7 | Must | AT-01 | Covered |

---

## 9. Troubleshooting Guide

### Logging Strategy

| Situation | Method | Why |
|-----------|--------|-----|
| Verify boot sequence | Serial monitor (`GET /api/serial/monitor`) | Captures UART output before WiFi is up |
| Monitor BLE commands | UDP logs (`GET /api/udplog`) | Non-blocking; works while device runs normally |
| Diagnose OTA failure | Serial monitor | Captures `"OTA failed"` + error code before any reboot |
| Capture crash/panic | Serial monitor | Only UART captures panic handler backtrace |
| Confirm provisioning | Serial monitor | WiFi/portal events fire before UDP log is active |

### Troubleshooting

| Test failure | Diagnostic | Fix |
|-------------|-----------|-----|
| Serial monitor shows no output | `GET /api/devices` → check SLOT1 present, not flapping | Re-flash; check USB connection |
| `enter-portal` times out | Serial: confirm `"AP mode: SSID='...'"` logged | Issue BLE CMD_WIFI_RESET first; check SSID value matches |
| `"STA disconnect, retry"` loops on provisioning | Wrong SSID/password sent to portal | Re-run provisioning with correct workbench AP credentials |
| UDP logs empty after provisioning | `"UDP logging -> ..."` missing from serial boot log | Check NVS has correct `udp_host` value; re-provision with correct host |
| BLE scan returns empty list | Device not advertising | Serial: check `"BLE NUS initialized"` logged; check `CONFIG_BT_ENABLED=y` |
| OTA task starts but `"OTA failed"` logged | Workbench not reachable or binary not uploaded | Verify `GET /api/firmware/list`; check URL in trigger request |
| Device does not roll back after bad OTA | `esp_ota_mark_app_valid` called despite boot failure | Remove or gate the call on application health check |
| `/status` returns 404 | HTTP server not started (WiFi not up yet) | Wait for `"STA got IP"` in logs; HTTP server starts on that event |
| BLE not visible in Phase 2 Windows scan | Phase 2 HID advertising not started | Confirm HOGP service added before `nimble_port_freertos_init` |
| Windows pairs but shows no mouse movement | HID report descriptor mismatch or no report sent | Verify descriptor bytes; check wiggle scheduler is firing |
| Second PC cannot pair simultaneously | `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2` not set | Run `idf.py menuconfig` or check `sdkconfig.defaults` |

---

## 10. Appendix

### 10.1 Required Log Patterns (Workbench Contract)

The workbench skills grep for these exact strings. They must not be reformatted.

| Pattern | Source file | When emitted |
|---------|------------|-------------|
| `"Init complete"` | `app_main.c` | End of `app_main()` |
| `"alive %lu"` | `app_main.c` | Every 10 s in `alive_task` |
| `"OTA succeeded"` | `ota_update.c` | OTA download complete |
| `"OTA failed"` | `ota_update.c` | OTA download error |
| `"OTA update requested"` | `cmd_handler.c` | BLE `CMD_OTA` received |
| `"WiFi reset requested"` | `cmd_handler.c` / `wifi_prov.c` | BLE `CMD_WIFI_RESET` / HTTP reset |
| `"WiFi credentials erased"` | `nvs_store.c` | After `nvs_store_erase_wifi()` |
| `"UDP logging -> %s:%d"` | `udp_log.c` | `udp_log_init()` called |
| `"No WiFi credentials"` | `wifi_prov.c` | No stored credentials found |
| `"AP mode: SSID='%s'"` | `wifi_prov.c` | SoftAP started |
| `"Portal page requested"` | `wifi_prov.c` | Portal GET handler called |
| `"Credentials saved"` | `wifi_prov.c` | Portal POST handler — creds saved |
| `"STA mode, connecting to '%s'"` | `wifi_prov.c` | `start_sta()` called |
| `"STA got IP"` | `wifi_prov.c` | `IP_EVENT_STA_GOT_IP` received |
| `"STA disconnect, retry"` | `wifi_prov.c` | `WIFI_EVENT_STA_DISCONNECTED` |
| `"BLE NUS initialized"` | `ble_nus.c` | `ble_nus_init()` complete |

### 10.2 NVS Schema (`mka_config` namespace)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `wifi_ssid` | String | — | WiFi network SSID |
| `wifi_pass` | String | — | WiFi password |
| `wiggle_int` | UInt32 | 300 | Wiggle interval (seconds) |
| `wiggle_amp` | UInt8 | 5 | Wiggle amplitude (pixels) |
| `udp_host` | String | — | UDP log target host/IP |
| `udp_port` | UInt16 | 5555 | UDP log target port |

Boot count is stored separately in NVS namespace `mka_sys`, key `boot_count`.

### 10.3 Partition Table (16 MB Flash)

| Partition | Type | Offset | Size |
|-----------|------|--------|------|
| nvs | data/nvs | 0x9000 | 24 KB |
| otadata | data/ota | 0xf000 | 8 KB |
| phy_init | data/phy | 0x11000 | 4 KB |
| factory | app/factory | 0x20000 | 1 MB |
| ota_0 | app/ota_0 | 0x120000 | 3 MB |
| ota_1 | app/ota_1 | 0x420000 | 3 MB |
| spiffs | data/spiffs | 0x720000 | 2 MB |

### 10.4 BLE Command Opcodes

| Opcode | Hex | Description |
|--------|-----|-------------|
| `CMD_OTA` | `0x10` | Start OTA; optional URL payload appended as ASCII string |
| `CMD_WIFI_RESET` | `0x20` | Erase WiFi credentials and reboot into AP mode |

### 10.5 HTTP Endpoint Quick Reference

```
GET  /status          → JSON: firmware, uptime, wifi_rssi, ble_connected, wiggle config
POST /config          → Body: {"wiggle_interval_s": N, "wiggle_amplitude": N}
POST /ota/trigger     → Body: {"url": "http://..."}
POST /config/reset    → Factory reset (no body required)
```

### 10.6 Wiggle Sequence

```
t = 0 (interval tick)
  → send HID report [0x00, +dx, +dy, 0x00, 0x00]
t = +100 ms
  → send HID report [0x00, -dx, -dy, 0x00, 0x00]
Net cursor displacement: zero
```
