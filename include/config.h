#pragma once

#define CONFIG_FILE "/config.json"

struct Config
{
  char ssid[16];
  char password[64];
  char mqtt_server[64];
  int mqtt_port;
  char mqtt_user[32];
  char mqtt_password[64];
  char web_password[64] = "admin";          // ← значение по умолчанию
  unsigned long publishingInterval = 10000; // Интервал отправки данных (в миллисекундах)
  float temp_offset = 0.0;                  // Калибровка температуры
  char post_url[64] = "";                   // POST
  char ota_url[64] = "";                    // OTA
  char ota_result_url[64] = "";             // OTA RESULT
  char uid[32] = "";
};

extern Config config;

void saveConfig();
void loadConfig();