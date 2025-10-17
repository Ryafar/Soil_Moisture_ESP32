# 🌱 ESP32 Soil Moisture Monitoring System

Modulares ESP32-basiertes System zur Überwachung von Bodenfeuchtigkeit und Batteriespannung mit automatischer Datenübertragung an InfluxDB über HTTPS.

## 📋 Inhaltsverzeichnis
- [Hardware Setup](#-hardware-setup)
- [Software Architektur](#-software-architektur)
- [Build & Flash](#-build--flash)
- [CMakeLists.txt Konfiguration](#-cmakeliststxt-konfiguration)
- [Test-Programme](#-test-programme)
- [Konfiguration](#-konfiguration)

---

## 🔌 Hardware Setup

### Komponenten
- ESP32 (ESP32-C6 oder Lolin Lite)
- Kapazitiver Bodenfeuchtigkeitssensor (TLC555I-basiert)
- Optional: Batterie mit Spannungsteiler

### Pin-Belegung

| Sensor/Modul    | ESP32 Pin   | ADC Kanal | Beschreibung           |
|-----------------|-------------|-----------|------------------------|
| Soil Sensor     | GPIO5       | ADC1_CH0  | Analogausgang Sensor   |
| Battery Monitor | GPIO8       | ADC1_CH3  | Batteriespannung/2     |
| Status LED      | GPIO22      | -         | Statusanzeige          |

> ⚠️ **Wichtig**: ADC2 kann nicht mit WiFi gleichzeitig verwendet werden! Nutze immer ADC1.

---

## 🏗️ Software Architektur

### Projektstruktur

```
main/
├── soil_project_main.c/h          # Haupteinstiegspunkt
├── config/
│   ├── esp32-config.h             # Hardware & System Konfiguration
│   └── credentials.h              # WiFi & InfluxDB Credentials
├── drivers/                        # Hardware-Treiber (Low-Level)
│   ├── adc/                       # ADC Abstraktion
│   ├── csm_v2_driver/             # Capacitive Soil Moisture Sensor
│   ├── wifi/                      # WiFi Manager
│   ├── http/                      # HTTP Client mit Buffering
│   ├── influxdb/                  # InfluxDB Line Protocol Client
│   └── led/                       # LED Steuerung
├── application/                    # Anwendungslogik (High-Level)
│   ├── soil_monitor_app.c/h       # Soil Monitoring Hauptlogik
│   ├── battery_monitor_task.c/h   # Battery Monitoring Task
│   └── influx_sender.c/h          # Dedizierte InfluxDB Sender Task
├── utils/                          # Hilfsfunktionen
│   ├── esp_utils.c/h              # Allgemeine ESP32 Utils
│   └── ntp_time.c/h               # NTP Zeitsynchronisation
└── 01_testing/                     # Standalone Test-Programme
    ├── wifi_connection_main.c
    ├── soil_monitor_main.c
    ├── battery_monitor_main.c
    └── influx_db_main.c/h
```

---

## 📦 Treiber-Übersicht

### 1. ADC Treiber (`drivers/adc/`)

| Datei            | Zweck                                    | Key Features                           |
|------------------|------------------------------------------|----------------------------------------|
| `adc.c/h`        | Low-Level ADC Abstraktion                | Einzelne ADC Kanäle konfigurieren      |
| `adc_manager.c/h`| Shared ADC Resource Management           | Multi-Sensor Support, Reference Counting|

**Verwendung:**
```c
// Shared ADC initialisieren (mehrere Sensoren auf ADC Unit)
adc_shared_init(ADC_UNIT_1);
adc_shared_add_channel(ADC_UNIT_1, ADC_CHANNEL_0, ADC_BITWIDTH_12, ADC_ATTEN_DB_11, 3.3f);

// Spannung auslesen
float voltage;
adc_shared_read_voltage(ADC_UNIT_1, ADC_CHANNEL_0, &voltage);
```

### 2. CSM_v2 Driver (`drivers/csm_v2_driver/`)

Capacitive Soil Moisture Sensor Treiber mit Kalibrierung.

**Features:**
- Automatische Voltage → Moisture % Konvertierung
- Dry/Wet Kalibrierung
- ADC Manager Integration

**Verwendung:**
```c
csm_v2_config_t config;
csm_v2_get_default_config(&config, ADC_UNIT_1, ADC_CHANNEL_0);
config.dry_voltage = 3.0f;  // Trocken
config.wet_voltage = 1.0f;  // Nass

csm_v2_driver_t driver;
csm_v2_init(&driver, &config);

csm_v2_reading_t reading;
csm_v2_read(&driver, &reading);
// reading.moisture_percent, reading.voltage, reading.raw_adc
```

### 3. WiFi Manager (`drivers/wifi/`)

Station-Mode WiFi mit automatischem Reconnect.

**Features:**
- Event-basiertes Management
- IP-Adresse auslesen
- Connection Status Callbacks

**Verwendung:**
```c
wifi_manager_config_t config = {
    .ssid = WIFI_SSID,
    .password = WIFI_PASSWORD,
    .max_retry = 10
};
wifi_manager_init(&config, NULL);
wifi_manager_connect();

if (wifi_manager_is_connected()) {
    char ip[16];
    wifi_manager_get_ip(ip);
}
```

### 4. HTTP Client (`drivers/http/`)

HTTP Client mit optionalem Buffering für offline Betrieb.

| Datei              | Zweck                                      |
|--------------------|--------------------------------------------|
| `http_client.c/h`  | Wrapper um esp_http_client                 |
| `http_buffer.c/h`  | Request Queue für Offline-Pufferung       |

### 5. InfluxDB Client (`drivers/influxdb/`)

HTTPS Client für InfluxDB v2 mit Line Protocol Support.

**Features:**
- TLS/HTTPS mit Certificate Bundle
- Nanosecond Precision Timestamps
- Separate Measurements: `soil_moisture`, `battery`
- Retry-Logik

**Konfiguration in `esp32-config.h`:**
```c
#define INFLUXDB_SERVER   "data.michipi.mywire.org"
#define INFLUXDB_PORT     443
#define INFLUXDB_BUCKET   "soil-data"
#define INFLUXDB_ORG      "Michipi"  // Case-sensitive!
```

**Line Protocol Format:**
```
soil_moisture,device=SOIL_XXXX voltage=2.5,moisture_percent=45.2,raw_adc=2048 1633024800000000000
battery,device=BATT_XXXX voltage=3.7,percentage=85.0 1633024800000000000
```

### 6. LED Driver (`drivers/led/`)

Einfache GPIO LED Steuerung.

```c
led_init(GPIO_NUM_22);
led_set_state(GPIO_NUM_22, true);  // ON
led_toggle(GPIO_NUM_22);
```

---

## 🧩 Application Layer

### Soil Monitor App (`application/soil_monitor_app.c`)

Haupt-Anwendung für Bodenfeuchtigkeit Monitoring.

**Task Stack:** 12 KB (für TLS/HTTP)  
**Funktionen:**
- Initialisiert alle Treiber (WiFi, InfluxDB, NTP, Sensor)
- Liest periodisch Sensordaten
- Sendet Daten via `influx_sender` Task

### Battery Monitor Task (`application/battery_monitor_task.c`)

Separate Task für Batteriespannungsüberwachung.

**Task Stack:** 12 KB  
**Features:**
- Unabhängig vom Soil Monitor
- Deep Sleep bei niedriger Spannung
- LED Statusanzeige

### InfluxDB Sender Task (`application/influx_sender.c`)

**Warum eine dedizierte Task?**  
TLS/HTTP Operationen benötigen viel Stack (12-14 KB). Um Stack Overflows in den Sensor-Tasks zu vermeiden, werden alle InfluxDB Writes über eine Queue an diese Task weitergeleitet.

**Stack:** 14 KB  
**Queue:** 10 Messages

```c
// In sensor tasks:
influx_sender_init();
influx_sender_enqueue_soil(&soil_data);      // Non-blocking
influx_sender_enqueue_battery(&battery_data); // Non-blocking
```

---

## 🔧 Build & Flash

### Prerequisites
- ESP-IDF v5.5.1 installiert
- VS Code mit ESP-IDF Extension

### Credentials konfigurieren

Erstelle `main/config/credentials.h`:
```c
#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PASSWORD "YourWiFiPassword"
#define INFLUXDB_TOKEN "your-influxdb-token-here"

#endif
```

### Build & Flash
```bash
idf.py build
idf.py flash monitor
```

Oder in VS Code:
- `Ctrl+Shift+P` → "ESP-IDF: Build, Flash and Monitor"

---

## ⚙️ CMakeLists.txt Konfiguration

Die `main/CMakeLists.txt` steuert, welches Programm kompiliert wird.

### Haupt-Anwendung (Standard)

```cmake
idf_component_register(SRCS "soil_project_main.c"
                            "drivers/adc/adc.c"
                            "drivers/adc/adc_manager.c"
                            "drivers/csm_v2_driver/csm_v2_driver.c"
                            "application/soil_monitor_app.c"
                            "application/influx_sender.c"
                            "drivers/wifi/wifi_manager.c"
                            "drivers/http/http_client.c"
                            "drivers/http/http_buffer.c"
                            "drivers/influxdb/influxdb_client.c"
                            "utils/esp_utils.c"
                            "utils/ntp_time.c"
                            "application/battery_monitor_task.c"
                            "drivers/led/led.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_adc esp_wifi esp_netif nvs_flash 
                                esp_event esp_http_client esp-tls json esp_timer lwip)
```

### Test-Programme aktivieren

Kommentiere die Haupt-App aus und aktiviere ein Test-Programm:

```cmake
# Haupt-App auskommentieren
# idf_component_register(SRCS "soil_project_main.c" ...

# Test-Programm aktivieren (Kommentar entfernen)
idf_component_register(SRCS "01_testing/influx_db_main.c"
                            "drivers/wifi/wifi_manager.c"
                            "drivers/influxdb/influxdb_client.c"
                            "utils/esp_utils.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_adc esp_wifi esp_netif nvs_flash 
                                esp_event esp_http_client esp-tls json esp_timer lwip)
```

**Wichtig nach Änderung:**
```bash
idf.py fullclean  # Cache löschen
idf.py build      # Neu kompilieren
```

---

## 🧪 Test-Programme

Alle Test-Programme befinden sich in `main/01_testing/`. Jedes Programm testet eine spezifische Komponente isoliert.

### Verfügbare Tests

| Test Programm              | Testet                    | Benötigte Dateien                              |
|----------------------------|---------------------------|------------------------------------------------|
| `wifi_connection_main.c`   | WiFi Verbindung, IP Abruf | wifi_manager, http_client, esp_utils          |
| `soil_monitor_main.c`      | Bodenfeuchtigkeit Sensor  | csm_v2_driver, adc, adc_manager, esp_utils    |
| `battery_monitor_main.c`   | Batterie Überwachung      | battery_monitor_task, adc, adc_manager, led    |
| `influx_db_main.c/h`       | InfluxDB HTTPS Upload     | wifi_manager, influxdb_client, esp_utils, NTP  |

### Beispiel: InfluxDB Test

`influx_db_main.c` ist der stabilste Test für InfluxDB Übertragung:

**Features:**
- Dedizierte Task mit 12 KB Stack
- NTP Synchronisation
- Periodisches Senden von Test-Daten (alle 5 Sekunden)
- HTTPS mit Certificate Bundle
- Detailliertes Logging

**Ablauf:**
1. WiFi verbinden
2. InfluxDB Client initialisieren
3. NTP Zeit synchronisieren (30s Timeout)
4. Test-Paket senden mit Timestamp
5. Loop: Test-Daten mit variierenden Werten senden

**Erwartete Ausgabe:**
```
I (3420) INFLUX_DB_MAIN: ✅ WiFi connection successful - ready for InfluxDB operations
I (3430) INFLUX_DB_MAIN: Influx demo task started
I (3440) INFLUX_DB_MAIN: Initializing NTP time synchronization...
I (4210) INFLUX_DB_MAIN: ✅ NTP synchronized successfully!
I (4215) INFLUX_DB_MAIN: 🕐 Current time: 2025-10-17T14:30:22
I (4890) INFLUX_DB_MAIN: ✅ Test InfluxDB packet sent successfully!
```

---

## 🔧 Konfiguration

### System-Konfiguration (`main/config/esp32-config.h`)

#### ADC Konfiguration
```c
#define SOIL_ADC_UNIT           ADC_UNIT_1
#define SOIL_ADC_CHANNEL        ADC_CHANNEL_0    // GPIO5
#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_3    // GPIO8
#define BATTERY_MONITOR_VOLTAGE_SCALE_FACTOR  2.0f  // Spannungsteiler 1:1
```

#### Task Konfiguration
```c
// Stack Sizes (12 KB für TLS/HTTP Tasks!)
#define SOIL_TASK_STACK_SIZE                (12 * 1024)
#define BATTERY_MONITOR_TASK_STACK_SIZE     (12 * 1024)

// Messintervalle
#define SOIL_MEASUREMENT_INTERVAL_MS        (10 * 60 * 1000)  // 10 Minuten
#define BATTERY_MONITOR_MEASUREMENT_INTERVAL_MS (10 * 60 * 1000)
```

#### InfluxDB Konfiguration
```c
#define INFLUXDB_SERVER         "data.michipi.mywire.org"
#define INFLUXDB_PORT           443
#define INFLUXDB_USE_HTTPS      1
#define INFLUXDB_BUCKET         "soil-data"
#define INFLUXDB_ORG            "Michipi"  // ⚠️ Case-sensitive!
#define INFLUXDB_ENDPOINT       "/api/v2/write"

#define HTTP_TIMEOUT_MS         15000
#define HTTP_MAX_RETRIES        3
```

### Kalibrierung Bodenfeuchtigkeit

```c
#define SOIL_DRY_VOLTAGE_DEFAULT    3.0f  // Sensor in trockener Erde
#define SOIL_WET_VOLTAGE_DEFAULT    1.0f  // Sensor in nassem Wasser
```

**Automatische Kalibrierung:**
```c
soil_monitor_calibrate(&app);  // Folgt Anweisungen im Log
```

---

## 📊 Datenformat in InfluxDB

### Measurement: `soil_moisture`
| Field               | Type  | Beschreibung              |
|---------------------|-------|---------------------------|
| voltage             | float | Sensor-Spannung (V)       |
| moisture_percent    | float | Feuchtigkeit (%)          |
| raw_adc             | int   | Raw ADC Wert (0-4095)     |

**Tag:** `device` (z.B. `SOIL_AABBCCDDEEFF`)

### Measurement: `battery`
| Field               | Type  | Beschreibung              |
|---------------------|-------|---------------------------|
| voltage             | float | Batteriespannung (V)      |
| percentage          | float | Ladezustand (%, optional) |

**Tag:** `device` (z.B. `BATT_AABBCCDDEEFF`)

**Timestamp:** Nanosekunden (precision=ns), NTP-synchronisiert

---

## 🐛 Troubleshooting

### Stack Overflow Errors

**Problem:** `***ERROR*** A stack overflow in task XYZ has been detected.`

**Lösung:**
1. Task Stack Size erhöhen (12-14 KB für TLS/HTTP Tasks)
2. Sicherstellen, dass `influx_sender` Task verwendet wird statt direkter HTTP Calls
3. Logging Level reduzieren: `idf.py menuconfig` → Component config → Log output → Warning

### InfluxDB 404 Error

**Problem:** `HTTP status 404`

**Lösung:**
- Organisation Name ist **case-sensitive**: `"Michipi"` ≠ `"michipi"`
- Prüfe in InfluxDB UI: Settings → About → Organization Name

### NTP Sync Timeout

**Problem:** `NTP sync timeout`

**Lösung:**
- Prüfe Firewall: UDP Port 123 muss offen sein
- Alternative NTP Server in `ntp_time.c` eintragen
- Timestamps funktionieren trotzdem (InfluxDB nutzt Server Time)

### WiFi nicht verbunden

**Problem:** `WiFi connection failed`

**Lösung:**
- Credentials in `credentials.h` prüfen
- SSID/Password dürfen keine Sonderzeichen haben
- Max Retry erhöhen: `WIFI_MAX_RETRY` in `esp32-config.h`

---

## 📚 Weitere Informationen

### Nützliche Ressourcen
- [ESP-IDF Dokumentation](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [InfluxDB Line Protocol](https://docs.influxdata.com/influxdb/v2/reference/syntax/line-protocol/)
- [ESP32 ADC Calibration](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/adc_calibration.html)
