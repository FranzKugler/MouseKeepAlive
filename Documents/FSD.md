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

### 7.1 Initial Deployment (First Flash)

1. Install ESP-IDF ≥ 5.2 on macOS per the ESP-IDF Getting Started guide.
2. Clone the repository: `git clone https://github.com/FranzKugler/MouseKeepAlive`
3. Configure the project: `idf.py menuconfig` — verify target is `esp32s3`, flash size is 16 MB.
4. Build: `idf.py build`
5. Connect the XIAO ESP32-S3 Plus via USB-C (enter download mode by holding BOOT while pressing RESET).
6. Flash: `idf.py -p <PORT> flash` — or use the workbench RFC2217 flash command.
7. Monitor: `idf.py -p <PORT> monitor` — verify boot log and AP mode advertisement.

### 7.2 Provisioning (First Boot)

1. On first boot, the device starts AP mode: SSID `MouseKeepAlive-XXXX`.
2. Connect a phone or laptop to the AP (open, no password).
3. A captive portal page opens automatically (or navigate to `http://192.168.4.1`).
4. Select your WiFi network and enter the password.
5. Configure wiggle interval and amplitude on the App Config page.
6. Tap **Save & Reboot** — the device restarts in STA mode and connects to WiFi.
7. Verify: `GET http://<device-ip>/status` returns `ble_connections: 0` and WiFi RSSI.

### 7.3 Pairing BLE HID Mouse with Windows

1. On the Windows machine, open **Settings → Bluetooth → Add Device → Bluetooth**.
2. The device appears as `MouseKeepAlive` — click to pair.
3. No PIN required (no-input-output pairing).
4. Windows installs the HID driver automatically and the device appears under **Mice and other pointing devices**.
5. Repeat for the second Windows machine.
6. After pairing, verify that mouse wiggles appear in Windows mouse pointer movement at the configured interval.

### 7.4 OTA Firmware Update

```bash
# Upload new firmware to workbench
curl -X POST http://workbench.local:8080/api/firmware/upload \
  -F "project=MouseKeepAlive" -F "file=@build/MouseKeepAlive.bin"

# Trigger OTA on device
curl -X POST http://<device-ip>/ota/trigger \
  -H 'Content-Type: application/json' \
  -d '{"url": "http://workbench.local:8080/api/firmware/MouseKeepAlive/latest"}'
```

Monitor progress via UDP logs: `curl "http://workbench.local:8080/api/udplog?limit=50"`

### 7.5 Factory Reset

**Via button**: Hold BOOT button for 5 seconds. LED (if available) blinks rapidly.  
**Via HTTP**: `POST http://<device-ip>/config/reset`  
**Via workbench NVS erase**: `esptool.py --port <PORT> erase_region 0x9000 0x6000`

After factory reset the device erases all configuration and bond data, then reboots into captive portal AP mode.

### 7.6 Recovery Procedures

| Scenario | Recovery |
|----------|---------|
| Device cannot connect to WiFi | Hold BOOT 5 s → captive portal → re-provision |
| WiFi password changed at router | Same as above — fallback to AP mode after repeated auth failures |
| OTA bricked firmware | A/B scheme automatically rolls back to previous firmware |
| All OTA partitions corrupt | Re-flash factory partition via USB with `idf.py flash` |
| BLE bonds stale (Windows re-paired) | Factory reset or `POST /config/reset` to clear bond store |

---

## 8. Verification & Validation

### 8.1 Phase 1 Verification

| Test ID | Feature | Procedure | Success Criteria |
|---------|---------|-----------|-----------------|
| TC-CP-100 | Captive portal first boot | Power on with empty NVS; connect to AP; configure WiFi | Device connects to WiFi, config persists |
| TC-CP-101 | Captive portal via button | Hold BOOT 5 s; connect; update WiFi | New credentials saved, device reconnects |
| TC-CP-102 | Portal network scan | Open WiFi page in portal | Available SSIDs listed with RSSI |
| WIFI-001 | WiFi STA connection | Provision with valid credentials | Device connects, IP assigned |
| WIFI-003 | Auto-reconnect | Disconnect WiFi AP; restore | Reconnects without reboot |
| TC-NVS-100 | Config persistence | Set wiggle config; reboot | Values identical after reboot |
| TC-NVS-101 | First boot defaults | Erase NVS; boot | Default values used, AP mode active |
| TC-NVS-102 | Factory reset (button) | Hold BOOT 5 s | NVS cleared, AP mode |
| TC-NVS-103 | Factory reset (HTTP) | `POST /config/reset` | NVS cleared, AP mode |
| TC-OTA-100 | Successful OTA update | Flash v1; upload v2; trigger OTA | v2 running after reboot |
| TC-OTA-101 | OTA rollback | Flash broken firmware via OTA | Device rolls back to v1 |
| TC-OTA-102 | OTA version reporting | `GET /status` before and after OTA | Version string matches firmware |
| TC-LOG-100 | Serial boot log | Monitor serial at boot | Version, reset reason, WiFi events logged |
| TC-LOG-101 | UDP log delivery | Configure UDP target; boot | Boot messages received at workbench |
| EC-OTA-200 | Network loss during OTA | Kill WiFi at 50% | No brick; retry succeeds |
| EC-OTA-202 | Invalid firmware rejection | Upload random binary via OTA | Rejected; device unaffected |
| EC-NVS-200 | NVS corruption recovery | Corrupt NVS via esptool; boot | Falls back to defaults; AP mode |

### 8.2 Phase 2 Verification

| Test ID | Feature | Procedure | Success Criteria |
|---------|---------|-----------|-----------------|
| TC-BLE-100 | BLE discovery & connection | Scan from Windows; pair | Device visible; connection established |
| HID-PAIR-01 | Windows HID pairing | Pair via Windows Bluetooth settings | Appears as mouse in Device Manager |
| HID-WIGGLE-01 | Wiggle delivery | Wait for interval; watch mouse pointer | Pointer moves by amplitude and returns |
| HID-WIGGLE-02 | Dual-host wiggle | Two Windows PCs paired; wait interval | Both PCs receive wiggle simultaneously |
| HID-INTERVAL-01 | Interval config | Set interval to 30 s via HTTP; observe | Wiggle fires every 30 s ± 500 ms |
| HID-AMPLITUDE-01 | Amplitude config | Set amplitude to 10; observe | Pointer moves 10 px each direction |
| TC-BLE-100 | Advertising resume | Disconnect one host; wait | Device re-advertises within 1 s |
| EC-BLE-204 | Bond persistence | Pair; reboot; reconnect | No re-pairing needed |
| EC-BLE-203 | BLE + WiFi coexistence | Run OTA while BLE connected and wiggling | Both complete; no crash |
| TC-OTA-100 (P2) | OTA with active BLE | Trigger OTA while 2 BLE hosts connected | Wiggle continues during download; reconnects after reboot |
| EC-BLE-200 | Disconnect during operation | Force-disconnect one host | Device recovers, resumes advertising |
| EC-BLE-201 | Rapid connect/disconnect | 20 rapid cycles | No memory leak; device stable |

### 8.3 Acceptance Tests

| Test | Scenario | Success Criteria |
|------|---------|-----------------|
| AT-01 | 24-hour continuous operation | Device runs uninterrupted for 24 h with 2 Windows hosts paired | No crash, no missed wiggles (log review), heap stable |
| AT-02 | Windows lock-screen prevention | Configure 5-min interval on PC with 2-min lock timeout | PC never locks during 1-hour observation |
| AT-03 | Full OTA cycle end-to-end | Provision → pair 2 PCs → OTA update → verify both PCs reconnect | All steps succeed without manual BLE re-pairing |
| AT-04 | Power loss recovery | Unplug device mid-operation; replug | Reconnects to both Windows hosts within 60 s |

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

| Symptom | Likely Cause | Diagnostic Steps | Corrective Action |
|---------|-------------|-----------------|-------------------|
| Device not visible in Windows Bluetooth scan | Not advertising, or BLE stack not started | Check UDP/serial log for `BLE advertising started` | Verify BLE init in boot log; power-cycle device |
| Windows pairs but no mouse movement | HID report not reaching Windows | Check log for wiggle timer ticks and `hid_report_sent` entries | Verify HOGP HID service UUID and report descriptor |
| Wiggle causes cursor to drift | Return report not sent or wrong sign | Check wiggle sequence in logs | Verify ±delta symmetry in `wiggle_scheduler.c` |
| Second Windows PC cannot pair | `MAX_CONNECTIONS=1` or connection slot full | Check log for `BLE max connections reached` | Confirm `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2` in sdkconfig |
| BLE disconnects shortly after pairing | Windows power management suspending BLE HID | Check Event Viewer on Windows | Set device power management to "Allow this device to wake the computer" |
| OTA download starts but never completes | WiFi drops or server unreachable | Check UDP log for `OTA error` | Verify workbench is reachable; retry `POST /ota/trigger` |
| Device boots into AP mode unexpectedly | WiFi credentials missing or corrupt | Serial log shows `NVS key not found: wifi_ssid` | Re-provision via captive portal |
| Heap keeps shrinking | Memory leak in BLE or OTA path | Monitor `free_heap` in `/status` and logs | Run EC-BLE-201 to isolate; check for unfreed buffers |
| Crash / panic on boot after OTA | New firmware defective | Serial log: panic backtrace visible | A/B rollback should fire automatically; if not, re-flash via USB |

---

## 10. Appendix

### 10.1 Configuration Defaults

| Parameter | Default | Min | Max | Unit |
|-----------|---------|-----|-----|------|
| `wiggle_interval` | 300 | 10 | 3600 | seconds |
| `wiggle_amplitude` | 5 | 1 | 50 | pixels |
| `udp_log_port` | 5555 | — | — | — |
| AP mode timeout | 300 | — | — | seconds |
| OTA rollback window | 30 | — | — | seconds |
| BOOT button hold (factory reset) | 5 | — | — | seconds |

### 10.2 BLE UUIDs

| Service / Characteristic | UUID |
|--------------------------|------|
| HID Service | 0x1812 |
| HID Information | 0x2A4A |
| Report Map | 0x2A4B |
| HID Control Point | 0x2A4C |
| HID Report (Input) | 0x2A4D |
| Battery Service | 0x180F |
| Battery Level | 0x2A19 |
| Device Information | 0x180A |
| Firmware Revision | 0x2A26 |

### 10.3 HTTP Endpoint Quick Reference

```
GET  /status          → JSON system status
POST /config          → Update wiggle config (JSON body)
POST /ota/trigger     → Start OTA download (JSON body: {"url": "..."})
POST /config/reset    → Factory reset
```

### 10.4 Workbench Commands (Quick Reference)

```bash
# Upload firmware
curl -X POST http://workbench.local:8080/api/firmware/upload \
  -F "project=MouseKeepAlive" -F "file=@build/MouseKeepAlive.bin"

# Trigger OTA
curl -X POST http://<device-ip>/ota/trigger \
  -H 'Content-Type: application/json' \
  -d '{"url": "http://workbench.local:8080/api/firmware/MouseKeepAlive/latest"}'

# View UDP logs
curl "http://workbench.local:8080/api/udplog?limit=50"

# BLE scan (workbench as Windows receiver)
curl -X POST http://workbench.local:8080/api/ble/scan \
  -H 'Content-Type: application/json' -d '{"timeout": 5}'

# Provision WiFi via workbench
curl -X POST http://workbench.local:8080/api/wifi/provision \
  -H 'Content-Type: application/json' \
  -d '{"ssid": "HomeNetwork", "password": "yourpassword"}'

# Erase NVS (local USB only)
esptool.py --port <PORT> erase_region 0x9000 0x6000
```

### 10.5 Wiggle Sequence Timing Diagram

```
t=0          t=interval           t=interval+100ms
 |                |                      |
[idle] ──────── [send +delta report] ── [send -delta report] ── [idle]
                      ↑                        ↑
               cursor moves right/down    cursor returns to origin
```
