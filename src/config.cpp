// config.cpp
#include <Arduino.h>
#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <cstring>

Config config;

void loadConfig()
{
    if (!LittleFS.begin(true))
    { // true = format on fail
        Serial.println("[FS] LittleFS Mount Failed");
        return;
    }

    // === Шаг 1: Устанавливаем значения по умолчанию ===
    memset(&config, 0, sizeof(Config));
    strcpy(config.ssid, "");
    strcpy(config.password, "");
    strcpy(config.mqtt_server, "");
    config.mqtt_port = 1883;
    strcpy(config.mqtt_user, "");
    strcpy(config.mqtt_password, "");
    strcpy(config.web_password, "admin"); // ← КЛЮЧЕВОЕ: пароль по умолчанию
    strcpy(config.uid, "");
    strcpy(config.post_url, "");
    strcpy(config.ota_url, "");
    strcpy(config.ota_result_url, "");
    config.publishingInterval = 10000;
    config.temp_offset = 0.0f;

    // === Шаг 2: Если файл существует — перезаписываем значения из него ===
    if (LittleFS.exists(CONFIG_FILE))
    {
        File file = LittleFS.open(CONFIG_FILE, "r");
        if (file)
        {
            DynamicJsonDocument doc(1024);
            DeserializationError error = deserializeJson(doc, file);
            file.close();

            if (!error)
            {
                strlcpy(config.ssid, doc["ssid"] | "", sizeof(config.ssid));
                strlcpy(config.password, doc["password"] | "", sizeof(config.password));
                strlcpy(config.mqtt_server, doc["mqtt_server"] | "", sizeof(config.mqtt_server));
                config.mqtt_port = doc["mqtt_port"] | 1883;
                strlcpy(config.mqtt_user, doc["mqtt_user"] | "", sizeof(config.mqtt_user));
                strlcpy(config.mqtt_password, doc["mqtt_password"] | "", sizeof(config.mqtt_password));
                strlcpy(config.web_password, doc["web_password"] | "admin", sizeof(config.web_password));
                strlcpy(config.uid, doc["uid"] | "", sizeof(config.uid));
                strlcpy(config.post_url, doc["post_url"] | "", sizeof(config.post_url));
                strlcpy(config.ota_url, doc["ota_url"] | "", sizeof(config.ota_url));
                strlcpy(config.ota_result_url, doc["ota_result_url"] | "", sizeof(config.ota_result_url));
                config.publishingInterval = doc["publishingInterval"] | 10000UL;
                config.temp_offset = doc["temp_offset"] | 0.0f;
            }
            else
            {
                Serial.printf("[CONFIG] JSON parse error: %s\n", error.c_str());
            }
        }
        else
        {
            Serial.println("[CONFIG] Failed to open config file for reading");
        }
    }
    else
    {
        // === Шаг 3: Файл не существует → создаём его с настройками по умолчанию ===
        Serial.println("[CONFIG] Config file not found — saving defaults");
        saveConfig();
    }

    LittleFS.end();
}

void saveConfig()
{
    if (!LittleFS.begin(true))
    { // true = format on fail
        Serial.println("[FS] LittleFS Mount Failed");
        return;
    }

    DynamicJsonDocument doc(1024);
    doc["ssid"] = config.ssid;
    doc["password"] = config.password;
    doc["mqtt_server"] = config.mqtt_server;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_user"] = config.mqtt_user;
    doc["mqtt_password"] = config.mqtt_password;
    doc["web_password"] = config.web_password;
    doc["uid"] = config.uid;
    doc["post_url"] = config.post_url;
    doc["ota_url"] = config.ota_url;
    doc["ota_result_url"] = config.ota_result_url;
    doc["publishingInterval"] = config.publishingInterval;
    doc["temp_offset"] = config.temp_offset;

    File file = LittleFS.open(CONFIG_FILE, "w");
    if (file)
    {
        serializeJson(doc, file);
        file.close();
        Serial.println("[CONFIG] Config saved to LittleFS");
    }
    else
    {
        Serial.println("[CONFIG] Failed to open config file for writing");
    }
    LittleFS.end();
}
