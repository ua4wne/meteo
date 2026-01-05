#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <LittleFS.h>
#include "config.h"
#include "sensors.h"
#include "mqtt.h"
#include "web.h"
#include <ArduinoJson.h>
#include "fw_version.h"

#define FORMAT_LITTLEFS_IF_FAILED true

const uint8_t sleep_on = 23;
const uint8_t LED_PIN = 2;
const unsigned long OTA_CHECK_INTERVAL = 3600000;
const unsigned long AP_RETRY_DELAY = 600000;

String CURRENT_FIRMWARE_VERSION = FIRMWARE_VERSION;
const char *VERSION_FILE = "/version.txt";
const char *OTA_PENDING_FILE = "/ota_pending.txt";

unsigned long lastOtaCheck = 0;
unsigned long apStartTime = 0;
bool forcedApMode = false;
bool wifiConnected = false;
SemaphoreHandle_t sensorMutex;

// --- Все вспомогательные функции: http_url, saveFirmwareVersion, loadFirmwareVersion,
//     sendPostRequest, sendOtaResult, checkAndReportPendingOta, checkFirmwareVersion,
//     parseVersionFromJson — вставьте их сюда (они не изменились) ---

String http_url(const char *url) {
    String postUrl = String(url);
    if (postUrl.startsWith("https://")) {
        postUrl = "http://" + postUrl.substring(8);
    }
    return postUrl;
}

void saveFirmwareVersion() {
    if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) return;
    File file = LittleFS.open(VERSION_FILE, "w");
    if (file) {
        file.print(CURRENT_FIRMWARE_VERSION);
        file.close();
    }
    LittleFS.end();
}

void loadFirmwareVersion() {
    if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) return;
    if (LittleFS.exists(VERSION_FILE)) {
        File file = LittleFS.open(VERSION_FILE, "r");
        if (file) {
            String versionStr = file.readString();
            versionStr.trim();
            if (versionStr.length() > 0 && versionStr.length() < 20) {
                CURRENT_FIRMWARE_VERSION = versionStr;
            } else {
                CURRENT_FIRMWARE_VERSION = FIRMWARE_VERSION;
                saveFirmwareVersion();
            }
            file.close();
        }
    } else {
        CURRENT_FIRMWARE_VERSION = FIRMWARE_VERSION;
        saveFirmwareVersion();
    }
    LittleFS.end();
}

void sendPostRequest() {
    if (strlen(config.post_url) == 0 || WiFi.status() != WL_CONNECTED) return;
    String postUrl = http_url(config.post_url);
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(postUrl.c_str())) return;
    DynamicJsonDocument doc(512);
    doc["uid"] = config.uid;
    JsonArray items = doc.createNestedArray("items");
    items.createNestedObject()["name"] = "rssi"; items[0]["value"] = String(WiFi.RSSI());
    items.createNestedObject()["name"] = "vcc"; items[1]["value"] = String(currentVcc, 2);
    String json; serializeJson(doc, json);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(json);
    http.end();
}

void sendOtaResult(const String &status, const String &oldVersion = "", const String &newVersion = "", int errorCode = 0, const String &errorMessage = "") {
    if (WiFi.status() != WL_CONNECTED || strlen(config.uid) == 0 || strlen(config.ota_result_url) == 0) return;
    String postUrl = http_url(config.ota_result_url);
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(postUrl.c_str())) return;
    DynamicJsonDocument doc(512);
    doc["uid"] = config.uid; doc["status"] = status;
    if (status == "success") { doc["old_version"] = oldVersion; doc["new_version"] = newVersion; }
    else { doc["error_code"] = errorCode; doc["error_message"] = errorMessage; }
    String json; serializeJson(doc, json);
    http.addHeader("Content-Type", "application/json");
    http.POST(json);
    http.end();
}

void checkAndReportPendingOta() {
    if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) return;
    if (LittleFS.exists(OTA_PENDING_FILE)) {
        File file = LittleFS.open(OTA_PENDING_FILE, "r");
        String oldVer, newVer;
        if (file) {
            oldVer = file.readStringUntil('\n'); newVer = file.readStringUntil('\n');
            oldVer.trim(); newVer.trim(); file.close();
            LittleFS.remove(OTA_PENDING_FILE);
        }
        LittleFS.end();
        WiFi.begin(config.ssid, config.password);
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start) < 10000) delay(500);
        if (WiFi.status() == WL_CONNECTED) { sendOtaResult("success", oldVer, newVer); delay(2000); }
        WiFi.disconnect(true);
    } else LittleFS.end();
}

String checkFirmwareVersion() {
    if (strlen(config.uid) == 0 || strlen(config.ota_url) == 0) return "";
    String checkUrl = http_url(config.ota_url) + "?uid=" + String(config.uid) + "&check_version=true";
    HTTPClient http; http.setTimeout(10000);
    if (!http.begin(checkUrl.c_str())) return "";
    int code = http.GET();
    String response = (code == 200) ? http.getString() : "";
    http.end();
    return response;
}

String parseVersionFromJson(const String &jsonStr) {
    DynamicJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, jsonStr);
    if (err || !doc.containsKey("version")) return "";
    String version = doc["version"].as<String>(); version.trim();
    return version;
}

bool performOTAUpdate(const String &newVersion) {
    if (strlen(config.uid) == 0 || strlen(config.ota_url) == 0) return false;
    String firmwareUrl = http_url(config.ota_url) + "?uid=" + String(config.uid) +
                        "&current_version=" + CURRENT_FIRMWARE_VERSION + "&check_version=false";
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(15000);
    if (!http.begin(client, firmwareUrl.c_str())) return false;
    int code = http.GET();
    if (code != 200) { http.end(); return false; }
    int contentLength = http.getSize();
    if (contentLength <= 0) { http.end(); return false; }
    WiFi.disconnect(true); delay(100);
    if (!Update.begin(contentLength, U_FLASH)) { http.end(); return false; }
    WiFiClient *stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();
    if (written == (size_t)contentLength && Update.end()) {
        if (LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
            File file = LittleFS.open(OTA_PENDING_FILE, "w");
            if (file) { file.println(CURRENT_FIRMWARE_VERSION); file.println(newVersion); file.close(); }
            LittleFS.end();
        }
        CURRENT_FIRMWARE_VERSION = newVersion;
        saveFirmwareVersion();
        delay(2000);
        ESP.restart();
        return true;
    }
    return false;
}

void setupWifi()
{
    if (strlen(config.ssid) == 0 || strlen(config.password) == 0)
    {
        String mac = WiFi.macAddress();
        mac.replace(":", "");
        String apName = "Sensor_" + mac;  // ← убрали .c_str()
        WiFi.softAP(apName.c_str());
        forcedApMode = true;
        apStartTime = millis();
        wifiConnected = false;
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(config.ssid, config.password);
    int attempt = 0;
    while (attempt < 5 && WiFi.status() != WL_CONNECTED)
    {
        delay(2000);
        attempt++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        wifiConnected = true;
        forcedApMode = false;
    }
    else
    {
        String mac = WiFi.macAddress();
        mac.replace(":", "");
        String apName = "Sensor_" + mac;  // ← убрали .c_str()
        WiFi.softAP(apName.c_str());
        forcedApMode = true;
        apStartTime = millis();
        wifiConnected = false;
    }
}

void sensorTask(void *parameter) {
    while (true) {
        if (wifiConnected) {
            if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                readSensors();
                float t = currentTemp, h = currentHumidity, p = currentPressure;
                xSemaphoreGive(sensorMutex);
                publishSensorData(t, h, p);
                sendPostRequest();
            }
        }
        vTaskDelay(config.publishingInterval / portTICK_PERIOD_MS);
    }
}

void systemTask(void *parameter) {
    while (true) {
        if (wifiConnected && (millis() - lastOtaCheck > OTA_CHECK_INTERVAL)) {
            lastOtaCheck = millis();
            String resp = checkFirmwareVersion();
            if (!resp.isEmpty()) {
                String newVer = parseVersionFromJson(resp);
                if (!newVer.isEmpty() && newVer != CURRENT_FIRMWARE_VERSION) {
                    performOTAUpdate(newVer);
                }
            }
        }
        if (forcedApMode && strlen(config.ssid) > 0 && strlen(config.password) > 0 &&
            (millis() - apStartTime > AP_RETRY_DELAY)) {
            forcedApMode = false;
            setupWifi();
            wifiConnected = (WiFi.status() == WL_CONNECTED);
        }
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}

void setup() {
    pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW); delay(50); digitalWrite(LED_PIN, HIGH);
    pinMode(sleep_on, INPUT_PULLUP);

    if (digitalRead(sleep_on) == LOW) {
        Serial.begin(115200);
        Serial.println("[DEEP SLEEP] GPIO23 grounded");
        if (LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) { loadConfig(); LittleFS.end(); }
        WiFi.begin(config.ssid, config.password);
        int a = 0; while (WiFi.status() != WL_CONNECTED && a++ < 15) delay(1000);
        if (WiFi.status() == WL_CONNECTED) {
            initSensors(); readSensors();
            publishSensorData(currentTemp, currentHumidity, currentPressure);
            sendPostRequest();
        }
        WiFi.disconnect(true); delay(100);
        uint64_t s = 5ULL * 60 * 1000000;
        if (currentVcc < 2.9f) s = 3600ULL * 1000000;
        else if (currentVcc < 3.0f) s = 1800ULL * 1000000;
        esp_deep_sleep(s);
    }

    Serial.begin(115200);
    if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) Serial.println("[FS] Failed");
    loadFirmwareVersion(); loadConfig(); checkAndReportPendingOta();
    setupWifi(); wifiConnected = (WiFi.status() == WL_CONNECTED);
    initSensors(); initMqtt(); initWebServer();
    sensorMutex = xSemaphoreCreateMutex();
    xTaskCreate(sensorTask, "Sensor", 4096, NULL, 1, NULL);
    xTaskCreate(systemTask, "System", 4096, NULL, 1, NULL);
    Serial.println("✓ RTOS started");
}

void loop() { vTaskDelay(portMAX_DELAY); }
