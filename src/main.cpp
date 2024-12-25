#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <OneWire.h>
#include <pgmspace.h>
#include "esp_mqtt.h"
#include "Date.h"
#include "RTCmem.h"
#include "DHT.h"
#include <SPI.h>
#include <ESP8266HTTPClient.h>

ADC_MODE(ADC_VCC);

#define USE_MQTT
//#define NOSERIAL
//Отключаем лишние функции для экономии батареи аккумулятора
//Выводы RST и D0 необходимо соединить
//Датчик раз в 5 минут будет просыпаться, отправлять данные и снова засыпать

//#define DHTTYPE DHT11   // DHT 11
#define DHTTYPE DHT21   // DHT 21 (AM2301)
//#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
#define ERROR_VALUE 2147483647

const float defTemperatureTolerance = 0.2; // Порог изменения температуры
const float defHumidityTolerance = 1.0; // Порог изменения влажности
const float defPressureTolerance = 1.0; // Порог изменения давления
const uint8_t climatePin = 14; // Пин, к которому подключен датчик температуры/влажности
uint8_t stateCode = 0; //код статуса (1, 3 - статусы датчиков и их суммы)

const uint8_t sleep_on = 13; //подаем землю - разрешаем работу в DEEP SLEEP режиме

Adafruit_BMP085 bmp;


const char overSSID[] PROGMEM = "METEO_"; // Префикс имени точки доступа по умолчанию
const char overMQTTClient[] PROGMEM = "METEO_"; // Префикс имени MQTT-клиента по умолчанию
char uid[16]; //идентификатор устройства
char webServer[16]; //web server name
char readSensor[7]; //период опроса датчиков температуры
char psendData[7]; //период отправки данных температуры
char sendStatus[7]; //период отправки статуса

const char pathOption[] PROGMEM = "/option"; // Путь до страницы настройки параметров

uint32_t timeReadSensor = 5000; //период опроса датчиков температуры
uint32_t timeSendData = 60000; //период отправки данных на сервер
uint32_t timeSendStatus = 30000; //период отправки статуса
uint32_t sleep_time = 30e7; //период засыпания 5 минут

// Имена параметров для Web-форм
const char paramUid[] PROGMEM = "uid";
const char paramWebServer[] PROGMEM = "webServer";
const char paramReadSensor[] PROGMEM = "readSensor";
const char paramSendData[] PROGMEM = "psendData";
const char paramSendStatus[] PROGMEM = "sendStatus";

// Имена JSON-переменных
const char jsonTemperature[] PROGMEM = "temperature";
const char jsonHumidity[] PROGMEM = "humidity";
const char jsonPressure[] PROGMEM = "pressure";


// Названия топиков для MQTT
const char mqttTemperatureTopic[] PROGMEM = "/Temperature";
const char mqttHumidityTopic[] PROGMEM = "/Humidity";
const char mqttPressureTopic[] PROGMEM = "/Pressure";

WiFiClient client;

class METEO : public ESPMQTT { 
  public:
    METEO() : ESPMQTT() {}
    void reboot();

  protected:
    void setupExtra();
    void loopExtra();

    String getHostName();
    uint16_t readConfig();
    uint16_t writeConfig(bool commit = true);
    void defaultConfig(uint8_t level = 0);
    bool setConfigParam(const String &name, const String &value);

    void setupHttpServer();
    void handleRootPage();
    String jsonData();
    void handleOptionConfig(); // Обработчик страницы настройки параметров

    String navigator();
    String btnOptionConfig(); // HTML-код кнопки вызова пользовательских настроек

    void mqttCallback(char* topic, byte* payload, unsigned int length);
    void mqttResubscribe();

  private:
    void publishTemperature(); // Публикация температуры в MQTT
    void publishHumidity(); // Публикация влажности в MQTT
    void publishPressure(); // Публикация давления в MQTT
    void sendReport(); // Отправка данных состояния на сервер
    void readSensors(); //чтение данных с сенсоров
    float climateTempTolerance; // Порог изменения температуры
    float climateHumTolerance; // Порог изменения влажности
    float climatePressTolerance; // Порог изменения давления
    uint32_t climateReadTime; // Время в миллисекундах, после которого можно считывать новое значение сенсоров
    uint32_t climateSendTime; // Время в миллисекундах, после которого можно отправлять на сервер новое значение сенсоров
    uint32_t statusReadTime; // Время в миллисекундах, после которого можно отправлять на сервер данные о состоянии датчиков протечки
    float climateTemperature; // Значение успешно прочитанной температуры
    float climateHumidity; // Значение успешно прочитанной влажности
    float Pressure; // Значение успешно прочитанного давления

    union {
      DHT *dht;
    };

    HTTPClient httpClient;
};

String charBufToString(const char* str, uint16_t bufSize) {
  String result;

  result.reserve(bufSize);
  while (bufSize-- && *str) {
    result += *str++;
  }

  return result;
}

/***
   METEO class implemenattion
*/

void METEO::reboot() {
  ESPMQTT::reboot();
}

void METEO::setupExtra() {
  ESPMQTT::setupExtra();
  pinMode(sleep_on, INPUT_PULLUP);
  
  //применяем настройки параметров
  String strReadSensor(readSensor);
  String strSendData(psendData);
  String strSendStatus(sendStatus);
  if (strReadSensor.length()) {
    timeReadSensor = strReadSensor.toInt();
  }
  else {
    timeReadSensor = 5000;
  }
  #ifndef NOSERIAL
    Serial.println("timeReadSensor=" + String(timeReadSensor));
  #endif
  if (strSendData.length()) {
    timeSendData = strSendData.toInt();
  }
  else {
    timeSendData = 60000;
  }
  #ifndef NOSERIAL
    Serial.println("timeSendData=" + String(timeSendData));
  #endif
  if (strSendStatus.length()) {
    timeSendStatus = strSendStatus.toInt();
  }
  else {
    timeSendStatus = 30000;
  }
  #ifndef NOSERIAL
    Serial.println("timeSendStatus=" + String(timeSendStatus));
  #endif

  // Initialize DHT sensor.
  uint8_t type;
  type = DHTTYPE;
  dht = new DHT(climatePin, type);
  dht->begin();
  climateReadTime = millis() + timeReadSensor;
  climateSendTime = millis() + timeSendData;
  climateTemperature = NAN;
  climateHumidity = NAN;
  if (!bmp.begin()) {
    Serial.println("Could not find a valid BMP085 sensor, check wiring!");
  }
  else {
    Serial.println("Found BMP085 sensor!");
  }
}

void METEO::loopExtra() {
  ESPMQTT::loopExtra();
  uint8_t sleep = digitalRead(sleep_on);
  
  if((ESP.getVcc()/1024.0) < 2.8)
    sleep_time = 18e8; //просыпаемся раз в полчаса
  if((ESP.getVcc()/1024.0) < 2.7)
    sleep_time = 36e8; //просыпаемся раз в час
  if ((int32_t)millis() >= (int32_t)statusReadTime) {
    sendReport();
    statusReadTime = millis() + timeSendStatus;
  }
  if ((int32_t)millis() >= (int32_t)climateReadTime) {
    readSensors();
    climateReadTime = millis() + timeReadSensor;
  }

  if ((int32_t)millis() >= (int32_t)climateSendTime) {
    #ifdef USE_MQTT
    publishTemperature();
    publishHumidity();
    publishPressure();
    #endif
    climateSendTime = millis() + timeSendData;
  }
  if (sleep) {
    ESP.deepSleep(sleep_time);
  }
}

String METEO::getHostName() {
  String result;
  result = FPSTR(overSSID);
  result += getBoardId();
  return result;
}

uint16_t METEO::readConfig() {
  uint16_t offset = ESPMQTT::readConfig();

  if (offset) {
    uint16_t start = offset;
    getEEPROM(offset, uid);
    offset += sizeof(uid);
    getEEPROM(offset, webServer);
    offset += sizeof(webServer);
    getEEPROM(offset, readSensor);
    offset += sizeof(readSensor);
    getEEPROM(offset, psendData);
    offset += sizeof(psendData);
    getEEPROM(offset, sendStatus);
    offset += sizeof(sendStatus);
    uint8_t crc = crc8EEPROM(start, offset);
    if (readEEPROM(offset++) != crc) {
      #ifndef NOSERIAL
        Serial.println(F("CRC mismatch! Use default parameters."));
      #endif
      defaultConfig(2);
    }
  }
  return offset;
}

uint16_t METEO::writeConfig(bool commit) {
  uint16_t offset = ESPMQTT::writeConfig(false);
  uint16_t start = offset;
  putEEPROM(offset, uid);
  offset += sizeof(uid);
  putEEPROM(offset, webServer);
  offset += sizeof(webServer);
  putEEPROM(offset, readSensor);
  offset += sizeof(readSensor);
  putEEPROM(offset, psendData);
  offset += sizeof(psendData);
  putEEPROM(offset, sendStatus);
  offset += sizeof(sendStatus);
  uint8_t crc = crc8EEPROM(start, offset);
  writeEEPROM(offset++, crc);
  if (commit)
    commitConfig();

  return offset;
}

void METEO::defaultConfig(uint8_t level) {
  if (level < 2) {
    ESPMQTT::defaultConfig(level);

    if (level < 1) {
      _ssid = FPSTR(overSSID);
      _ssid += getBoardId();
    }
    _mqttClient = FPSTR(overMQTTClient);
    _mqttClient += getBoardId();
  }

  if (level < 3) {
    climateTempTolerance = defTemperatureTolerance;
    climateHumTolerance = defHumidityTolerance;
    climatePressTolerance = defPressureTolerance;
  }
}

bool METEO::setConfigParam(const String &name, const String &value) {
  if (! ESPMQTT::setConfigParam(name, value)) {
    if (name.equals(FPSTR(paramUid))) {
      strncpy(uid, value.c_str(), sizeof(uid));
    }
    else if (name.equals(FPSTR(paramWebServer))) {
      strncpy(webServer, value.c_str(), sizeof(webServer));
    }
    else if (name.equals(FPSTR(paramReadSensor))) {
      strncpy(readSensor, value.c_str(), sizeof(readSensor));
    }
    else if (name.equals(FPSTR(paramSendData))) {
      strncpy(psendData, value.c_str(), sizeof(psendData));
    }
    else if (name.equals(FPSTR(paramSendStatus))) {
      strncpy(sendStatus, value.c_str(), sizeof(sendStatus));
    }
    else
      return false;
  }
  return true;
}

void METEO::setupHttpServer() {
  ESPMQTT::setupHttpServer();
  httpServer->on(String(FPSTR(pathOption)).c_str(), std::bind(&METEO::handleOptionConfig, this));
}

void METEO::handleRootPage() {
  if (! adminAuthenticate())
    return;
  String script = ESPWebCore::StdJs();
  script += F("function uptimeToStr(uptime) {\n\
  var tm, uptimestr = '';\n\
  if (uptime >= 86400)\n\
  uptimestr = parseInt(uptime / 86400) + ' day(s) ';\n\
  tm = parseInt(uptime % 86400 / 3600);\n\
  if (tm < 10)\n\
  uptimestr += '0';\n\
  uptimestr += tm + ':';\n\
  tm = parseInt(uptime % 3600 / 60);\n\
  if (tm < 10)\n\
  uptimestr += '0';\n\
  uptimestr += tm + ':';\n\
  tm = parseInt(uptime % 60);\n\
  if (tm < 10)\n\
  uptimestr += '0';\n\
  uptimestr += tm;\n\
  return uptimestr;\n\
  }\n\
  function refreshData() {\n\
  var request = getXmlHttpRequest();\n\
  request.open('GET', '");
  script += FPSTR(pathData);
  script += F("?dummy=' + Date.now(), true);\n\
request.onreadystatechange = function() {\n\
if (request.readyState == 4) {\n\
var data = JSON.parse(request.responseText);\n");
  script += FPSTR(getElementById);
  script += FPSTR(jsonMQTTConnected);
  script += F("').innerHTML = (data.");
  script += FPSTR(jsonMQTTConnected);
  script += F(" != true ? \"not \" : \"\") + \"connected\";\n");
  script += FPSTR(getElementById);
  script += FPSTR(jsonUptime);
  script += F("').innerHTML = uptimeToStr(data.");
  script += FPSTR(jsonUptime);
  script += F(");\n");
  if (WiFi.getMode() == WIFI_STA) {
    script += FPSTR(getElementById);
    script += FPSTR(jsonRSSI);
    script += F("').innerHTML = data.");
    script += FPSTR(jsonRSSI);
    script += F(";\n");
  }
  script += FPSTR(getElementById);
  script += FPSTR(jsonTemperature);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonTemperature);
  script += F(";\n");
  script += FPSTR(getElementById);
  script += FPSTR(jsonHumidity);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonHumidity);
  script += F(";\n");
  script += FPSTR(getElementById);
  script += FPSTR(jsonPressure);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonPressure);
  script += F(";\n");
  script += F("}\n}\n\
  request.send(null);\n\
  }\n\
  setInterval(refreshData, 1000);\n");

  String page = ESPWebCore::webPageStart(F("WiFi Sensor"));
  page += ESPWebCore::StdCss();
  page += ESPWebCore::webPageScript(script);
  page += ESPWebCore::webPageBody();
  page += F("<h3>Weather Station</h3>\n<div class=\"container\">\n<div class=\"params-left\">\
    <p>Uptime: <span id=\"");
  page += FPSTR(jsonUptime);
  page += F("\">0</span> seconds</p>\n");
  if (WiFi.getMode() == WIFI_STA) {
    page += F("<p>Signal strength: <span id=\"");
    page += FPSTR(jsonRSSI);
    page += F("\">?</span> dBm</p>\n");
  }
  page += F("<p>MQTT broker: <span id=\"");
  page += FPSTR(jsonMQTTConnected);
  page += F("\">?</span></p>\n");
  page += F("</div>\n<div class=\"params-right\">\n");
  page += F("<p>Temperature: <span id=\"");
  page += FPSTR(jsonTemperature);
  page += F("\">?</span> <sup>o</sup>C</p>\n");
  page += F("<p>Humidity: <span id=\"");
  page += FPSTR(jsonHumidity);
  page += F("\">?</span> %</p>\n");
  page += F("<p>Pressure: <span id=\"");
  page += FPSTR(jsonPressure);
  page += F("\">?</span> mm</p>\n");
  page += F("</div>");
  page += navigator();
  page += F("</div>");
  page += ESPWebCore::webPageEnd();

  httpServer->send(200, FPSTR(textHtml), page);
}

void METEO::handleOptionConfig() {
  if (! adminAuthenticate())
    return;

  String page = ESPWebCore::webPageStart(F("Option Setup"));
  page += ESPWebCore::StdCss();
  page += ESPWebCore::webPageBody();
  page += F("<h3>Option Setup</h3>");
  page += F("<form name=\"option\" method=\"GET\" action=\"");
  page += FPSTR(pathStore);
  page += F("\">\n<label>Sensor request period:</label>\n\
  <input type=\"text\" name=\"");
  page += FPSTR(paramReadSensor);
  page += F("\" value=\"");
  page += escapeQuote(charBufToString(readSensor, sizeof(readSensor)));
  page += F("\" size=");
  page += sizeof(readSensor);
  page += F(" maxlength=");
  page += sizeof(readSensor);
  page += F(">\n");
  page += F("<label>Send data period:</label>\n\
  <input type=\"text\" name=\"");
  page += FPSTR(paramSendData);
  page += F("\" value=\"");
  page += escapeQuote(charBufToString(psendData, sizeof(psendData)));
  page += F("\" size=");
  page += String(sizeof(psendData));
  page += F(" maxlength=");
  page += String(sizeof(psendData));
  page += F(">\n");
  page += F("<label>Send status period:</label>\n\
  <input type=\"text\" name=\"");
  page += FPSTR(paramSendStatus);
  page += F("\" value=\"");
  page += escapeQuote(charBufToString(sendStatus, sizeof(sendStatus)));
  page += F("\" size=");
  page += String(sizeof(sendStatus));
  page += F(" maxlength=");
  page += String(sizeof(sendStatus));
  page += F(">\n");
  page += F("<label>device UID:</label>\n\
  <input type=\"text\" name=\"");
  page += FPSTR(paramUid);
  page += F("\" value=\"");
  page += escapeQuote(charBufToString(uid, sizeof(uid)));
  page += F("\" size=");
  page += String(sizeof(uid));
  page += F(" maxlength=");
  page += String(sizeof(uid));
  page += F(">\n");
  page += F("<label>Web server:</label>\n\
  <input type=\"text\" name=\"");
  page += FPSTR(paramWebServer);
  page += F("\" value=\"");
  page += escapeQuote(charBufToString(webServer, sizeof(webServer)));
  page += F("\" size=");
  page += String(sizeof(webServer));
  page += F(" maxlength=");
  page += String(sizeof(webServer));
  page += F(">\n");
  page += F("<hr>\n<div class=\"btn-group\">\n<input type=\"submit\" value=\"Save\">\n");
  page += btnBack();
  page += F("<input type=\"hidden\" name=\"reboot\" value=\"1\">\n</div>\n</form>\n");
  page += ESPWebCore::webPageEnd();

  httpServer->send(200, FPSTR(textHtml), page);
}

String METEO::jsonData() {
  String result = ESPMQTT::jsonData();
  result += F(",\"");
  result += FPSTR(jsonTemperature);
  result += F("\":");
  result += isnan(climateTemperature) ? F("\"?\"") : String(climateTemperature);
  result += F(",\"");
  result += FPSTR(jsonHumidity);
  result += F("\":");
  result += isnan(climateHumidity) ? F("\"?\"") : String(climateHumidity);
  result += F(",\"");
  result += FPSTR(jsonPressure);
  result += F("\":");
  result += isnan(Pressure) ? F("\"?\"") : String(Pressure);
  return result;
}

String METEO::navigator() {
  String result = F("</div>\n<hr>\n<div class=\"btn-group\">\n");
  result += btnWiFiConfig();
  result += btnTimeConfig();
  result += btnMQTTConfig();
  result += btnOptionConfig();
  result += btnReboot();
  result += F("</div>");
  return result;
}

String METEO::btnOptionConfig() {
  String result = F("<input type=\"button\" value=\"Option Setup\" onclick=\"location.href='/option'\">\n");
  return result;
}

void METEO::mqttCallback(char* topic, byte* payload, unsigned int length) {
  ESPMQTT::mqttCallback(topic, payload, length);
  //  String _mqttTopic = FPSTR(mqttRelayTopic);
  char* topicBody = topic + _mqttClient.length() + 1; // Skip "/ClientName" from topic
}

void METEO::mqttResubscribe() {
  String topic;

  if (_mqttClient != "") {
    topic += "/";
    topic += _mqttClient;
  }
  mqttSubscribe(topic);
}

void METEO::publishTemperature() {
  if (pubSubClient->connected()) {
    String topic;

    if (_mqttClient != "") {
      topic += "/";
      topic += _mqttClient;
    }
    topic += FPSTR(mqttTemperatureTopic);
    mqttPublish(topic, String(climateTemperature));
  }
}

void METEO::publishHumidity() {
  if (pubSubClient->connected()) {
    String topic;

    if (_mqttClient != "") {
      topic += "/";
      topic += _mqttClient;
    }
    topic += FPSTR(mqttHumidityTopic);
    mqttPublish(topic, String(climateHumidity));
  }
}

void METEO::publishPressure() {
  if (pubSubClient->connected()) {
    String topic;

    if (_mqttClient != "") {
      topic += "/";
      topic += _mqttClient;
    }
    topic += FPSTR(mqttPressureTopic);
    mqttPublish(topic, String(Pressure));
  }
}

void METEO::sendReport() {
  String strUID(uid);
  String strWebServer(webServer);
  //strUID = strUID.substring(0, strUID.length());
  if (strUID.length() && strWebServer.length()) { //если заданы параметры UID и WebServer через конфигуратор страницы
    // Отправляем серверу данные состояния,уровня сигнала и батареи
    String request = "https://" + strWebServer + "/api/v1/facts/data/?uid=" + strUID + "&name=rssi&value=";
    request += String(WiFi.RSSI());
    httpClient.begin(client, request);
    //int httpCode = httpClient.GET();
    httpClient.end();
    request = "https://" + strWebServer + "/api/v1/facts/data/?uid=" + strUID + "&name=vcc&value=";
    request += String(ESP.getVcc() / 1024.0);
    httpClient.begin(client, request);
    //int httpCode = httpClient.GET();
    httpClient.end();
    #ifndef NOSERIAL
      Serial.println(request);
    #endif
  }
}

void METEO::readSensors() {
  float v;
  uint32_t now = getTime();
  v = (dht->readTemperature() + bmp.readTemperature()) / 2;
  if (! isnan(v) && (v >= -50.0) && (v <= 50.0)) {
    if (isnan(climateTemperature) || (abs(climateTemperature - v) > climateTempTolerance)) {
      climateTemperature = v;
    //  publishTemperature();
    }
  } else {
    #ifndef NOSERIAL
      Serial.println(F(" DHTx temperature read error!"));
    #endif
  }

  v = dht->readHumidity();
  if (! isnan(v) && (v >= 0.0) && (v <= 100.0)) {
    if (isnan(climateHumidity) || (abs(climateHumidity - v) > climateHumTolerance)) {
      climateHumidity = v;
    //  publishHumidity();
    }
  } else {
    #ifndef NOSERIAL
      Serial.println(F(" DHTx humidity read error!"));
    #endif
  }

  v = bmp.readPressure() / 133.3;
  if (isnan(Pressure) || (abs(Pressure - v) > climatePressTolerance)) {
    Pressure = v;
  //  publishPressure();
  }
}

METEO *app = new METEO();

void setup() {
  Serial.begin(115200);
  Serial.println();
  app->setup();
}

void loop() {
  app->loop();
}
