#include "web.h"
#include "config.h"
#include "sensors.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

AsyncWebServer server(80);

// === Стили (без изменений) ===
const char MAIN_STYLE[] PROGMEM = R"rawliteral(
<style>
:root {
  --primary: #10B981;
  --secondary: #0EA5E9;
  --gray-100: #f3f4f6;
  --gray-200: #e5e7eb;
  --gray-700: #374151;
  --gray-900: #111827;
  --danger: #ef4444;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
  background-color: var(--gray-100);
  color: var(--gray-900);
  line-height: 1.5;
  padding: 1rem;
}
.container {
  max-width: 800px;
  margin: 0 auto;
}
.card {
  background: white;
  border-radius: 0.75rem;
  box-shadow: 0 4px 6px -1px rgba(0,0,0,0.1);
  padding: 1.5rem;
  margin-bottom: 1.5rem;
}
h1 {
  font-size: 1.875rem;
  font-weight: 700;
  text-align: center;
  margin-bottom: 1rem;
  color: var(--primary);
}
.nav-tabs {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
  justify-content: center;
  margin-bottom: 1.5rem;
}
.nav-tab {
  padding: 0.5rem 1rem;
  text-decoration: none;
  color: var(--gray-700);
  background: var(--gray-200);
  border-radius: 0.5rem;
  font-size: 0.875rem;
}
.nav-tab:hover {
  background: var(--primary);
  color: white;
}
.form-group {
  margin-bottom: 1rem;
}
.form-group label {
  display: block;
  margin-bottom: 0.5rem;
  font-weight: 600;
}
input, select, button {
  width: 100%;
  padding: 0.75rem;
  border: 1px solid var(--gray-200);
  border-radius: 0.5rem;
  font-size: 1rem;
}
input:focus, select:focus {
  outline: 2px solid var(--primary);
}
.btn {
  display: inline-block;
  padding: 0.75rem 1.5rem;
  font-weight: 600;
  text-align: center;
  text-decoration: none;
  border-radius: 0.5rem;
  cursor: pointer;
  transition: opacity 0.2s;
}
.btn-primary {
  background-color: var(--primary);
  color: white;
  border: none;
}
.btn-primary:hover {
  opacity: 0.9;
}
.btn-outline {
  background: transparent;
  border: 1px solid var(--gray-200);
  color: var(--gray-700);
}
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  gap: 1.25rem;
  margin: 1.5rem 0;
}
.metric-card {
  background: white;
  padding: 1.25rem;
  border-radius: 0.75rem;
  text-align: center;
  box-shadow: 0 1px 3px rgba(0,0,0,0.1);
}
.metric-label {
  font-size: 0.875rem;
  color: var(--gray-700);
  margin-bottom: 0.5rem;
}
.metric-value {
  font-size: 1.5rem;
  font-weight: 700;
}
.error {
  background: #fee;
  color: var(--danger);
  padding: 0.75rem;
  border-radius: 0.5rem;
  margin-bottom: 1rem;
  text-align: center;
}
.status-connected { color: var(--primary); }
.status-disconnected { color: var(--danger); }
.status-ap { color: var(--secondary); }
</style>
)rawliteral";

String getWebHeader(const String &title)
{
  return String(R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>)rawliteral") +
         title + R"rawliteral(</title>
)rawliteral" +
         String(MAIN_STYLE) + R"rawliteral(
</head>
<body>
  <div class="container">
    <h1>)rawliteral" +
         title + R"rawliteral(</h1>
    <div class="nav-tabs">
      <a href="/" class="nav-tab">Dashboard</a>
      <a href="/options/base" class="nav-tab">Base</a>
      <a href="/options/wifi" class="nav-tab">Wi-Fi</a>
      <a href="/options/mqtt" class="nav-tab">MQTT</a>
    </div>
)rawliteral";
}

String getWebFooter()
{
  return R"rawliteral(
  </div>
</body>
</html>
)rawliteral";
}

String getWifiNetworksOptions()
{
  String options = "";
  int n = WiFi.scanNetworks();
  if (n == 0)
  {
    options = "<option>Сети не найдены</option>";
  }
  else
  {
    for (int i = 0; i < n; i++)
    {
      String ssid = WiFi.SSID(i);
      ssid.replace("\"", "&quot;");
      String selected = (ssid == config.ssid) ? " selected" : "";
      options += "<option value=\"" + ssid + "\"" + selected + ">" + ssid + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }
  }
  return options;
}

void restartAfterDelay(void *pvParameter)
{
  delay(2000); // даём время на отправку
  ESP.restart();
  vTaskDelete(NULL);
}

bool isAuthorized(AsyncWebServerRequest *request)
{
    // Защищаем ВСЕ страницы, включая "/"
    const char* username = "admin"; // фиксированный логин
    const char* password = config.web_password;

    // Используем встроенную проверку авторизации AsyncWebServer
    if (!request->authenticate(username, password, "Secure Area"))
    {        // Запрашиваем логин/пароль
        request->requestAuthentication();
        return true;
    }

    return true;
}

// === Обработчики GET ===

void handleRoot(AsyncWebServerRequest *request)
{
  String status = "Не подключено";
  String statusClass = "status-disconnected";
  if (WiFi.status() == WL_CONNECTED)
  {
    status = "Подключено";
    statusClass = "status-connected";
  }

  String html = getWebHeader("Дашборд");
  html += R"rawliteral(
        <div class="card">
          <div class="grid">
            <div class="metric-card">
              <div class="metric-label">Температура</div>
              <div class="metric-value" style="color:var(--primary);">)rawliteral" +
          (isnan(currentTemp) ? "--" : String(currentTemp, 1)) + R"rawliteral(°C</div>
            </div>
            <div class="metric-card">
              <div class="metric-label">Влажность</div>
              <div class="metric-value" style="color:var(--secondary);">)rawliteral" +
          (currentHumidity > 0 ? String(currentHumidity, 1) : "--") + R"rawliteral(%</div>
            </div>
            <div class="metric-card">
              <div class="metric-label">Давление</div>
              <div class="metric-value" style="color:var(--primary);">)rawliteral" +
          (isnan(currentPressure) ? "--" : String(currentPressure, 1)) + R"rawliteral( мм.рт.ст.</div>
            </div>
            <div class="metric-card">
              <div class="metric-label">VCC</div>
              <div class="metric-value">)rawliteral" +
          String(currentVcc, 2) + R"rawliteral( V</div>
            </div>
            <div class="metric-card">
              <div class="metric-label">RSSI</div>
              <div class="metric-value">)rawliteral" +
          String(WiFi.RSSI()) + R"rawliteral( dBm</div>
            </div>
          </div>
          <p style="text-align:center; margin-top:1rem;">
            <span class=")rawliteral" +
          statusClass + R"rawliteral(">Статус Wi-Fi: )rawliteral" + status + R"rawliteral(</span>
          </p>
        </div>
    )rawliteral";
  html += getWebFooter();
  html += R"rawliteral(<script>setTimeout(() => location.reload(), 5000);</script>)rawliteral";

  request->send(200, "text/html; charset=utf-8", html);
}

void handleWifiOptions(AsyncWebServerRequest *request)
{
  String networkOptions = getWifiNetworksOptions();
  String checked = (strlen(config.ssid) == 0) ? "checked" : "";

  String html = getWebHeader("Настройки Wi-Fi");
  html += R"rawliteral(
        <div class="card">
            <form method="POST" action="/save/wifi">
                <div class="form-group">
                  <label class="checkbox-container">
                    <input type="checkbox" name="ap_mode" value="1" )rawliteral" +
          checked + R"rawliteral(>
                    Режим точки доступа (AP)
                  </label>
                </div>
                <div class="form-group">
                    <label>Wi-Fi сеть</label>
                    <select name="ssid" class="input">
                      <option value="">-- Выберите сеть --</option>
                      )rawliteral" +
          networkOptions + R"rawliteral(
                    </select>
                </div>
                <div class="form-group">
                    <label>Пароль</label>
                    <input type="password" name="password" value=")rawliteral" +
          String(config.password) + R"rawliteral(" class="input">
                </div>
                <button type="submit" class="btn btn-primary">Сохранить и перезагрузить</button>
            </form>
        </div>
    )rawliteral";
  html += getWebFooter();

  request->send(200, "text/html; charset=utf-8", html);
}

void handleBaseOptions(AsyncWebServerRequest *request)
{
  String html = getWebHeader("Базовые настройки");
  html += R"rawliteral(
        <div class="card">
            <form method="POST" action="/save/options">
                <div class="form-group">
                    <label>Device UID *</label>
                    <input name="uid" value=")rawliteral" +
          String(config.uid) + R"rawliteral(" required>
                    <p class="text-sm text-gray-500 mt-1">Обязательный уникальный идентификатор</p>
                </div>
                <div class="form-group">
                    <label>API URL</label>
                    <input name="post_url" value=")rawliteral" +
          String(config.post_url) + R"rawliteral(" >
                </div>
                <div class="form-group">
                    <label>OTA URL</label>
                    <input name="ota_url" value=")rawliteral" +
          String(config.ota_url) + R"rawliteral(" >
                </div>
                <div class="form-group">
                    <label>OTA RESULT URL</label>
                    <input name="ota_result_url" value=")rawliteral" +
          String(config.ota_result_url) + R"rawliteral(" >
                </div>
                <div class="form-group">
                    <label>Интервал публикации (мс)</label>
                    <input name="publishing_interval" value=")rawliteral" +
          String(config.publishingInterval) + R"rawliteral(" type="number">
                </div>
                <div class="form-group">
                    <label>Смещение температуры</label>
                    <input name="temp_offset" value=")rawliteral" +
          String(config.temp_offset, 2) + R"rawliteral(" step="0.1" type="number">
                </div>
                <button type="submit" class="btn btn-primary">Сохранить и перезагрузить</button>
            </form>
        </div>
    )rawliteral";
  html += getWebFooter();

  request->send(200, "text/html; charset=utf-8", html);
}

void handleMqttOptions(AsyncWebServerRequest *request)
{
  String html = getWebHeader("Настройки MQTT");
  html += R"rawliteral(
        <div class="card">
            <form method="POST" action="/save/mqtt">
                <div class="form-group">
                    <label>Сервер</label>
                    <input name="mqtt_server" value=")rawliteral" +
          String(config.mqtt_server) + R"rawliteral(" required>
                </div>
                <div class="form-group">
                    <label>Порт</label>
                    <input name="mqtt_port" value=")rawliteral" +
          String(config.mqtt_port) + R"rawliteral(" type="number" required>
                </div>
                <div class="form-group">
                    <label>Пользователь</label>
                    <input name="mqtt_user" value=")rawliteral" +
          String(config.mqtt_user) + R"rawliteral(">
                </div>
                <div class="form-group">
                    <label>Пароль</label>
                    <input type="password" name="mqtt_password" value=")rawliteral" +
          String(config.mqtt_password) + R"rawliteral(">
                </div>
                <button type="submit" class="btn btn-primary">Сохранить и перезагрузить</button>
            </form>
        </div>
    )rawliteral";
  html += getWebFooter();

  request->send(200, "text/html; charset=utf-8", html);
}

// === Обработчики POST ===
void handleSaveWifi(AsyncWebServerRequest *request)
{
  if (request->hasParam("ap_mode", true))
  {
    config.ssid[0] = '\0';
    config.password[0] = '\0';
  }
  else
  {
    if (request->hasParam("ssid", true))
    {
      strlcpy(config.ssid, request->getParam("ssid", true)->value().c_str(), sizeof(config.ssid));
    }
    if (request->hasParam("password", true))
    {
      strlcpy(config.password, request->getParam("password", true)->value().c_str(), sizeof(config.password));
    }
  }
  saveConfig();

  String html = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Wi-Fi сохранён</title></head>
<body style="text-align:center; padding:2rem; font-family:sans-serif;">
  <h2>✅ Wi-Fi настройки сохранены!</h2>
  <p>Устройство перезагружается...</p>
  <script>setTimeout(() => window.location.href = "/", 1500);</script>
</body></html>
    )rawliteral";

  request->send(200, "text/html; charset=utf-8", html);
  xTaskCreate(restartAfterDelay, "RestartTask", 2048, NULL, 1, NULL);
}

// Аналогично для handleSaveBase и handleSaveMqtt

void handleSaveBase(AsyncWebServerRequest *request)
{
  if (request->hasParam("uid", true))
  {
    strlcpy(config.uid, request->getParam("uid", true)->value().c_str(), sizeof(config.uid));
  }
  if (request->hasParam("post_url", true))
  {
    strlcpy(config.post_url, request->getParam("post_url", true)->value().c_str(), sizeof(config.post_url));
  }
  if (request->hasParam("ota_url", true))
  {
    strlcpy(config.ota_url, request->getParam("ota_url", true)->value().c_str(), sizeof(config.ota_url));
  }
  if (request->hasParam("ota_result_url", true))
  {
    strlcpy(config.ota_result_url, request->getParam("ota_result_url", true)->value().c_str(), sizeof(config.ota_result_url));
  }
  if (request->hasParam("publishing_interval", true))
  {
    config.publishingInterval = request->getParam("publishing_interval", true)->value().toInt();
  }
  if (request->hasParam("temp_offset", true))
  {
    config.temp_offset = request->getParam("temp_offset", true)->value().toFloat();
  }
  saveConfig();

  String html = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Base settings saved. Rebooting...</title></head>
<body style="text-align:center; padding:2rem; font-family:sans-serif;">
  <h2>✅ Wi-Fi настройки сохранены!</h2>
  <p>Устройство перезагружается...</p>
  <script>setTimeout(() => window.location.href = "/", 1500);</script>
</body></html>
    )rawliteral";

  request->send(200, "text/html; charset=utf-8", html);
  xTaskCreate(restartAfterDelay, "RestartTask", 2048, NULL, 1, NULL);
}

void handleSaveMqtt(AsyncWebServerRequest *request)
{
  if (request->hasParam("mqtt_server", true))
  {
    strlcpy(config.mqtt_server, request->getParam("mqtt_server", true)->value().c_str(), sizeof(config.mqtt_server));
  }
  if (request->hasParam("mqtt_port", true))
  {
    config.mqtt_port = request->getParam("mqtt_port", true)->value().toInt();
  }
  if (request->hasParam("mqtt_user", true))
  {
    strlcpy(config.mqtt_user, request->getParam("mqtt_user", true)->value().c_str(), sizeof(config.mqtt_user));
  }
  if (request->hasParam("mqtt_password", true))
  {
    strlcpy(config.mqtt_password, request->getParam("mqtt_password", true)->value().c_str(), sizeof(config.mqtt_password));
  }
  saveConfig();

  String html = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>MQTT settings saved. Rebooting...</title></head>
<body style="text-align:center; padding:2rem; font-family:sans-serif;">
  <h2>✅ Wi-Fi настройки сохранены!</h2>
  <p>Устройство перезагружается...</p>
  <script>setTimeout(() => window.location.href = "/", 1500);</script>
</body></html>
    )rawliteral";

  request->send(200, "text/html; charset=utf-8", html);
  xTaskCreate(restartAfterDelay, "RestartTask", 2048, NULL, 1, NULL);
}

// === Инициализация сервера ===
void initWebServer()
{
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!isAuthorized(request)) return;
        handleRoot(request);
    });

    server.on("/options/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!isAuthorized(request)) return;
        handleWifiOptions(request);
    });

    server.on("/options/base", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!isAuthorized(request)) return;
        handleBaseOptions(request);
    });

    server.on("/options/mqtt", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!isAuthorized(request)) return;
        handleMqttOptions(request);
    });

    server.on("/save/wifi", HTTP_POST, [](AsyncWebServerRequest *request){
        if (!isAuthorized(request)) return;
        handleSaveWifi(request);
    });

    server.on("/save/options", HTTP_POST, [](AsyncWebServerRequest *request){
        if (!isAuthorized(request)) return;
        handleSaveBase(request);
    });

    server.on("/save/mqtt", HTTP_POST, [](AsyncWebServerRequest *request){
        if (!isAuthorized(request)) return;
        handleSaveMqtt(request);
    });

    server.begin();
    Serial.println("[WebServer] Async server started on port 80 with authentication");
}
