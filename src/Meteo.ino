#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <OneWire.h>
#include <pgmspace.h>
#include "ESPWebMQTT.h"
#include "Events.h"
#include "Date.h"
#include "RTCmem.h"
#include "DHT.h"
#include <ESP8266HTTPClient.h>
ADC_MODE(ADC_VCC);

//Отключаем лишние функции для экономии батареи аккумулятора
//Выводы RST и D0 необходимо соединить
//Датчик раз в 30 сек будет просыпаться, отправлять данные и снова засыпать

//#define DHTTYPE DHT11   // DHT 11
#define DHTTYPE DHT21   // DHT 21 (AM2301)
//#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
#define ERROR_VALUE 2147483647

const float defTemperatureTolerance = 0.2; // Порог изменения температуры
const float defHumidityTolerance = 1.0; // Порог изменения влажности
const float defPressureTolerance = 1.0; // Порог изменения давления
const uint8_t climatePin = 14; // Пин, к которому подключен датчик температуры/влажности
uint8_t stateCode = 0; //код статуса (1, 3 - статусы датчиков и их суммы)

const uint8_t dat1 = 12; //контроль датчика охраны №1
const uint8_t dat2 = 15; //контроль датчика охраны №2
const uint8_t sleep_on = 13; //подаем землю - разрешаем работу в DEEP SLEEP режиме

Adafruit_BMP085 bmp;


const char overSSID[] PROGMEM = "WAVGAT_"; // Префикс имени точки доступа по умолчанию
const char overMQTTClient[] PROGMEM = "WAVGAT_"; // Префикс имени MQTT-клиента по умолчанию
char uid[16]; //идентификатор устройства
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
const char paramReadSensor[] PROGMEM = "readSensor";
const char paramSendData[] PROGMEM = "psendData";
const char paramSendStatus[] PROGMEM = "sendStatus";

// Имена JSON-переменных
const char jsonTemperature[] PROGMEM = "temperature";
const char jsonHumidity[] PROGMEM = "humidity";
const char jsonPressure[] PROGMEM = "pressure";
const char jsonState[] PROGMEM = "state";


// Названия топиков для MQTT
const char mqttTemperatureTopic[] PROGMEM = "/Temperature";
const char mqttHumidityTopic[] PROGMEM = "/Humidity";
const char mqttPressureTopic[] PROGMEM = "/Pressure";

class ESPWebMQTT : public ESPWebMQTTBase {
  public:
    ESPWebMQTT() : ESPWebMQTTBase() {}

    void reboot();

  protected:
    void setupExtra();
    void loopExtra();

    String getHostName();

    uint16_t readRTCmemory();
    uint16_t writeRTCmemory();
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
    void sendSensorData(); //отправка данных с сенсоров на сервер

    Events *events;
    float climateTempTolerance; // Порог изменения температуры
    float climateHumTolerance; // Порог изменения влажности
    float climatePressTolerance; // Порог изменения давления
    //enum turn_t : uint8_t { TURN_OFF, TURN_ON, TURN_TOGGLE };

    //enum sensor_t : uint8_t { SENSOR_NONE, SENSOR_DS1820};

    uint32_t climateReadTime; // Время в миллисекундах, после которого можно считывать новое значение сенсоров
    uint32_t climateSendTime; // Время в миллисекундах, после которого можно отправлять на сервер новое значение сенсоров
    uint32_t statusReadTime; // Время в миллисекундах, после которого можно отправлять на сервер данные о состоянии датчиков протечки
    float climateTemperature; // Значение успешно прочитанной температуры
    float climateHumidity; // Значение успешно прочитанной влажности
    float Pressure; // Значение успешно прочитанного давления

    union {
      //DS1820 *ds;
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
   ESPWebMQTT class implemenattion
*/

void ESPWebMQTT::reboot() {
  ESPWebMQTTBase::reboot();
}

void ESPWebMQTT::setupExtra() {
  ESPWebMQTTBase::setupExtra();
  pinMode(dat1, INPUT_PULLUP);
  pinMode(dat2, INPUT_PULLUP);
  pinMode(sleep_on, INPUT_PULLUP);
  //  events = new Events();

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
  _log->println("timeReadSensor=" + String(timeReadSensor));
  if (strSendData.length()) {
    timeSendData = strSendData.toInt();
  }
  else {
    timeSendData = 60000;
  }
  _log->println("timeSendData=" + String(timeSendData));
  if (strSendStatus.length()) {
    timeSendStatus = strSendStatus.toInt();
  }
  else {
    timeSendStatus = 30000;
  }
  _log->println("timeSendStatus=" + String(timeSendStatus));

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

void ESPWebMQTT::loopExtra() {
  ESPWebMQTTBase::loopExtra();
  uint8_t stateDat1 = digitalRead(dat1);
  uint8_t stateDat2 = digitalRead(dat2);
  uint8_t sleep = digitalRead(sleep_on);

  stateCode = stateDat1 + 3 * stateDat2;
  //if(stateCode) statusReadTime = 5000;
  if ((int32_t)millis() >= (int32_t)statusReadTime) {
    sendReport();
    statusReadTime = millis() + timeSendStatus;
  }
  if ((int32_t)millis() >= (int32_t)climateReadTime) {
    readSensors();
    climateReadTime = millis() + timeReadSensor;
  }

  if ((int32_t)millis() >= (int32_t)climateSendTime) {
    sendSensorData();
    climateSendTime = millis() + timeSendData;
  }
  if (sleep) {
    //sendReport();
    readSensors();
    sendSensorData();
    ESP.deepSleep(sleep_time);
  }
}

String ESPWebMQTT::getHostName() {
  String result;

  result = FPSTR(overSSID);
  result += getBoardId();

  return result;
}

uint16_t ESPWebMQTT::readRTCmemory() {
  uint16_t offset = ESPWebMQTTBase::readRTCmemory();

  if (offset) {
    uint32_t controlState;


    return offset;
  }
}

uint16_t ESPWebMQTT::writeRTCmemory() {
  uint16_t offset = ESPWebMQTTBase::writeRTCmemory();

  if (offset) {
    uint32_t controlState;

  }

  return offset;
}

uint16_t ESPWebMQTT::readConfig() {
  uint16_t offset = ESPWebMQTTBase::readConfig();

  if (offset) {
    uint16_t start = offset;
    getEEPROM(offset, uid);
    offset += sizeof(uid);
    getEEPROM(offset, readSensor);
    offset += sizeof(readSensor);
    getEEPROM(offset, psendData);
    offset += sizeof(psendData);
    getEEPROM(offset, sendStatus);
    offset += sizeof(sendStatus);
    uint8_t crc = crc8EEPROM(start, offset);
    if (readEEPROM(offset++) != crc) {
      _log->println(F("CRC mismatch! Use default relay parameters."));
      defaultConfig(2);
    }
  }
  return offset;
}

uint16_t ESPWebMQTT::writeConfig(bool commit) {
  uint16_t offset = ESPWebMQTTBase::writeConfig(false);
  uint16_t start = offset;
  putEEPROM(offset, uid);
  offset += sizeof(uid);
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

void ESPWebMQTT::defaultConfig(uint8_t level) {
  if (level < 2) {
    ESPWebMQTTBase::defaultConfig(level);

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

bool ESPWebMQTT::setConfigParam(const String &name, const String &value) {
  if (! ESPWebMQTTBase::setConfigParam(name, value)) {
    if (name.equals(FPSTR(paramUid))) {
      strncpy(uid, value.c_str(), sizeof(uid));
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

String quoteEscape(const String& str) {
  String result = "";
  int start = 0, pos;

  while (start < str.length()) {
    pos = str.indexOf('"', start);
    if (pos != -1) {
      result += str.substring(start, pos) + F("&quot;");
      start = pos + 1;
    } else {
      result += str.substring(start);
      break;
    }
  }

  return result;
}

void ESPWebMQTT::setupHttpServer() {
  ESPWebMQTTBase::setupHttpServer();

  httpServer->on(String(FPSTR(pathOption)).c_str(), std::bind(&ESPWebMQTT::handleOptionConfig, this));
  //httpServer->on(String(FPSTR(pathSwitch)).c_str(), std::bind(&ESPWebMQTTRelay::handleRelaySwitch, this));
  //httpServer->on(String(FPSTR(pathClimate)).c_str(), std::bind(&ESPWebMQTTRelay::handleClimateConfig, this));
}

void ESPWebMQTT::handleRootPage() {
  if (! userAuthenticate())
    return;
  String style = F(".checkbox {\n\
vertical-align:top;\n\
margin:0 3px 0 0;\n\
width:17px;\n\
height:17px;\n\
}\n\
.checkbox + label {\n\
cursor:pointer;\n\
}\n\
.checkbox:not(checked) {\n\
position:absolute;\n\
opacity:0;\n\
}\n\
.checkbox:not(checked) + label {\n\
position:relative;\n\
padding:0 0 0 60px;\n\
}\n\
.checkbox:not(checked) + label:before {\n\
content:'';\n\
position:absolute;\n\
top:-4px;\n\
left:0;\n\
width:50px;\n\
height:26px;\n\
border-radius:13px;\n\
background:#CDD1DA;\n\
box-shadow:inset 0 2px 3px rgba(0,0,0,.2);\n\
}\n\
.checkbox:not(checked) + label:after {\n\
content:'';\n\
position:absolute;\n\
top:-2px;\n\
left:2px;\n\
width:22px;\n\
height:22px;\n\
border-radius:10px;\n\
background:#FFF;\n\
box-shadow:0 2px 5px rgba(0,0,0,.3);\n\
transition:all .2s;\n\
}\n\
.checkbox:checked + label:before {\n\
background:#9FD468;\n\
}\n\
.checkbox:checked + label:after {\n\
left:26px;\n\
}\n");

  String script = F("function uptimeToStr(uptime) {\n\
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
  script += FPSTR(getElementById);
  script += FPSTR(jsonState);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonState);
  script += F(";\n");
  script += F("}\n\
}\n\
request.send(null);\n\
}\n\
setInterval(refreshData, 1000);\n");

  String page = ESPWebBase::webPageStart(F("WiFi Sensor"));
  page += ESPWebBase::webPageStdStyle();
  page += ESPWebBase::webPageStyle(style);
  page += ESPWebBase::webPageStdScript();
  page += ESPWebBase::webPageScript(script);
  page += ESPWebBase::webPageBody();
  page += F("<div id='header'>\n\<h3>Weather Station</h3>\n\
<p>\n\
MQTT broker: <span id=\"");
  page += FPSTR(jsonMQTTConnected);
  page += F("\">?</span><br/>\n\
Uptime: <span id=\"");
  page += FPSTR(jsonUptime);
  page += F("\">?</span><br/>\n");
  if (WiFi.getMode() == WIFI_STA) {
    page += F("Signal strength: <span id=\"");
    page += FPSTR(jsonRSSI);
    page += F("\">?</span> dBm<br/>\n");
  }
  page += F("Temperature: <span id=\"");
  page += FPSTR(jsonTemperature);
  page += F("\">?</span> <sup>o</sup>C<br/>\n");
  page += F("Humidity: <span id=\"");
  page += FPSTR(jsonHumidity);
  page += F("\">?</span> %<br/>\n");
  page += F("Pressure: <span id=\"");
  page += FPSTR(jsonPressure);
  page += F("\">?</span> mm<br/>\n");
  page += F("State code: <span id=\"");
  page += FPSTR(jsonState);
  page += F("\">?</span><br/>\n");
  page += F("</p>\n");
  page += navigator();
  page += F("</div>");
  page += ESPWebBase::webPageEnd();

  httpServer->send(200, FPSTR(textHtml), page);
}

void ESPWebMQTT::handleOptionConfig() {
  if (! adminAuthenticate())
    return;

  String page = ESPWebBase::webPageStart(F("Option Setup"));
  page += ESPWebBase::webPageStdStyle();
  page += ESPWebBase::webPageBody();
  page += F("<div id='header'>\n\<h3>Option Setup</h3><p>");
  page += F("<form name=\"option\" method=\"GET\" action=\"");
  page += FPSTR(pathStore);
  page += F("\">\n\<label>Sensor request period:</label><br/>\n\
  <input type=\"text\" name=\"");
  page += FPSTR(paramReadSensor);
  page += F("\" value=\"");
  page += escapeQuote(charBufToString(readSensor, sizeof(readSensor)));
  page += F("\" size=");
  page += String(sizeof(readSensor));
  page += F(" maxlength=");
  page += String(sizeof(readSensor));
  page += F("><br/>\n\
<p>\n");
  page += F("\n\<label>Send data period:</label><br/>\n\
  <input type=\"text\" name=\"");
  page += FPSTR(paramSendData);
  page += F("\" value=\"");
  page += escapeQuote(charBufToString(psendData, sizeof(psendData)));
  page += F("\" size=");
  page += String(sizeof(psendData));
  page += F(" maxlength=");
  page += String(sizeof(psendData));
  page += F("><br/>\n\
<p>\n");
  page += F("\n\<label>Send status period:</label><br/>\n\
  <input type=\"text\" name=\"");
  page += FPSTR(paramSendStatus);
  page += F("\" value=\"");
  page += escapeQuote(charBufToString(sendStatus, sizeof(sendStatus)));
  page += F("\" size=");
  page += String(sizeof(sendStatus));
  page += F(" maxlength=");
  page += String(sizeof(sendStatus));
  page += F("><br/>\n\
<p>\n");
  page += F("<label>device UID:</label><br/>\n\
  <input type=\"text\" name=\"");
  page += FPSTR(paramUid);
  page += F("\" value=\"");
  page += escapeQuote(charBufToString(uid, sizeof(uid)));
  page += F("\" size=");
  page += String(sizeof(uid));
  page += F(" maxlength=");
  page += String(sizeof(uid));
  page += F("><br/>\n\
<p>\n");
  page += ESPWebBase::tagInput(FPSTR(typeSubmit), strEmpty, F("Save"));
  page += charLF;
  page += btnBack();
  page += ESPWebBase::tagInput(FPSTR(typeHidden), FPSTR(paramReboot), "1");
  page += F("\n\</form>\n");
  page += F("</div>");
  page += ESPWebBase::webPageEnd();

  httpServer->send(200, FPSTR(textHtml), page);
}

String ESPWebMQTT::jsonData() {
  String result = ESPWebMQTTBase::jsonData();
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
  result += F(",\"");
  result += FPSTR(jsonState);
  result += F("\":");
  result += String(stateCode);
  return result;
}

String ESPWebMQTT::navigator() {
  String result = btnWiFiConfig();
  result += btnTimeConfig();
  result += btnMQTTConfig();
  result += btnOptionConfig();
  //result += btnClimateConfig();
  result += btnLog();
  result += btnReboot();

  return result;
}

String ESPWebMQTT::btnOptionConfig() {
  String result = ESPWebBase::tagInput(FPSTR(typeButton), strEmpty, F("Option Setup"), String(F("onclick=\"location.href='")) + String(FPSTR(pathOption)) + String(F("'\"")));
  result += charLF;

  return result;
}

void ESPWebMQTT::mqttCallback(char* topic, byte* payload, unsigned int length) {
  ESPWebMQTTBase::mqttCallback(topic, payload, length);

  //  String _mqttTopic = FPSTR(mqttRelayTopic);
  char* topicBody = topic + _mqttClient.length() + 1; // Skip "/ClientName" from topic

}

void ESPWebMQTT::mqttResubscribe() {
  String topic;

  if (_mqttClient != strEmpty) {
    topic += charSlash;
    topic += _mqttClient;
  }
  //topic += FPSTR(mqttRelayTopic);
  //mqttPublish(topic, String(digitalRead(relayPin) == relayLevel));

  mqttSubscribe(topic);
}

void ESPWebMQTT::publishTemperature() {
  if (pubSubClient->connected()) {
    String topic;

    if (_mqttClient != strEmpty) {
      topic += charSlash;
      topic += _mqttClient;
    }
    topic += FPSTR(mqttTemperatureTopic);
    mqttPublish(topic, String(climateTemperature));
  }
}

void ESPWebMQTT::publishHumidity() {
  if (pubSubClient->connected()) {
    String topic;

    if (_mqttClient != strEmpty) {
      topic += charSlash;
      topic += _mqttClient;
    }
    topic += FPSTR(mqttHumidityTopic);
    mqttPublish(topic, String(climateHumidity));
  }
}

void ESPWebMQTT::publishPressure() {
  if (pubSubClient->connected()) {
    String topic;

    if (_mqttClient != strEmpty) {
      topic += charSlash;
      topic += _mqttClient;
    }
    topic += FPSTR(mqttPressureTopic);
    mqttPublish(topic, String(Pressure));
  }
}

void ESPWebMQTT::sendReport() {
  String strUID(uid);
  strUID = strUID.substring(0, strUID.length()-1);
  if (strUID.length() && _mqttServer.length()) { //если задан параметр UID через конфигуратор страницы
    // Отправляем серверу данные состояния,уровня сигнала и батареи
    String post = "http://" + _mqttServer + "/control?device=" + strUID + "&alarm=";
    post += String(stateCode);
    post += "&rssi=";
    post += String(WiFi.RSSI());
    post += "&vcc=";
    post += String(ESP.getVcc() / 1024.0);
    httpClient.begin(post);
    int httpCode = httpClient.GET();
    httpClient.end();
    _log->println(post);
  }
}

void ESPWebMQTT::readSensors() {
  float v;
  uint32_t now = getTime();
  v = (dht->readTemperature() + bmp.readTemperature()) / 2;
  if (! isnan(v) && (v >= -50.0) && (v <= 50.0)) {
    if (isnan(climateTemperature) || (abs(climateTemperature - v) > climateTempTolerance)) {
      climateTemperature = v;
      publishTemperature();
    }
  } else {
    logDateTime(now);
    _log->println(F(" DHTx temperature read error!"));
  }

  v = dht->readHumidity();
  if (! isnan(v) && (v >= 0.0) && (v <= 100.0)) {
    if (isnan(climateHumidity) || (abs(climateHumidity - v) > climateHumTolerance)) {
      climateHumidity = v;
      publishHumidity();
    }
  } else {
    logDateTime(now);
    _log->println(F(" DHTx humidity read error!"));
  }

  v = bmp.readPressure() / 133.3;
  if (isnan(Pressure) || (abs(Pressure - v) > climatePressTolerance)) {
    Pressure = v;
    publishPressure();
  }
}

void ESPWebMQTT::sendSensorData() {
  String strUID(uid);
  strUID = strUID.substring(0, strUID.length()-1);
  if (strUID.length() && _mqttServer.length()) { //если задан параметр UID через конфигуратор страницы
    // Отправляем серверу данные температуры, влажности и давления
    String post = "http://" + _mqttServer + "/control?device=" + strUID + "&celsio=";
    post += String(climateTemperature);
    post += "&humidity=";
    post += String(climateHumidity);
    post += "&pressure=";
    post += String(Pressure);
    httpClient.begin(post);
    int httpCode = httpClient.GET();
    httpClient.end();
    _log->println(post);
  }
}

ESPWebMQTT *app = new ESPWebMQTT();

void setup() {
  Serial.begin(115200);
  Serial.println();
  app->setup();
}

void loop() {
  app->loop();
}
