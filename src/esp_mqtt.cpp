#include <pgmspace.h>
#include "esp_mqtt.h"

/*
 * ESPMQTT class implementation
 */

ESPMQTT::ESPMQTT() : ESPWebCore() {
  _espClient = new WiFiClient();
  pubSubClient = new PubSubClient(*_espClient);
}

void ESPMQTT::setupExtra() {
  if (_mqttServer != "") {
    pubSubClient->setServer(_mqttServer.c_str(), _mqttPort);
    pubSubClient->setCallback(std::bind(&ESPMQTT::mqttCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
  }
}

void ESPMQTT::loopExtra() {
  if ((_mqttServer != "") && ((WiFi.getMode() == WIFI_STA) && (WiFi.status() == WL_CONNECTED))) {
    if (! pubSubClient->connected())
      mqttReconnect();
    if (pubSubClient->connected())
      pubSubClient->loop();
  }
}

uint16_t ESPMQTT::readConfig() {
  uint16_t offset = ESPWebCore::readConfig();

  if (offset) {
    uint16_t start = offset;
    offset = readEEPROMString(offset, _mqttServer, maxStringLen);
    getEEPROM(offset, _mqttPort);
    offset += sizeof(_mqttPort);
    offset = readEEPROMString(offset, _mqttUser, maxStringLen);
    offset = readEEPROMString(offset, _mqttPassword, maxStringLen);
    offset = readEEPROMString(offset, _mqttClient, maxStringLen);
    uint8_t crc = crc8EEPROM(start, offset);
    if (readEEPROM(offset++) != crc) {
      #ifndef NOSERIAL
        Serial.println(F("CRC mismatch! Use default MQTT parameters."));
      #endif
      defaultConfig(1);
    }
  }

  return offset;
}

uint16_t ESPMQTT::writeConfig(bool commit) {
  uint16_t offset = ESPWebCore::writeConfig(false);
  uint16_t start = offset;

  offset = writeEEPROMString(offset, _mqttServer, maxStringLen);
  putEEPROM(offset, _mqttPort);
  offset += sizeof(_mqttPort);
  offset = writeEEPROMString(offset, _mqttUser, maxStringLen);
  offset = writeEEPROMString(offset, _mqttPassword, maxStringLen);
  offset = writeEEPROMString(offset, _mqttClient, maxStringLen);
  uint8_t crc = crc8EEPROM(start, offset);
  writeEEPROM(offset++, crc);
  if (commit)
    commitConfig();

  return offset;
}

void ESPMQTT::defaultConfig(uint8_t level) {
  if (level < 1)
    ESPWebCore::defaultConfig(level);

  if (level < 2) {
    _mqttServer = String();
    _mqttPort = defMQTTPort;
    _mqttUser = String();
    _mqttPassword = String();
    _mqttClient = FPSTR(defMQTTClient);
    _mqttClient += getBoardId();
  }
}

bool ESPMQTT::setConfigParam(const String& name, const String& value) {
  if (! ESPWebCore::setConfigParam(name, value)) {
    if (name.equals(FPSTR(paramMQTTServer)))
      _mqttServer = value;
    else if (name.equals(FPSTR(paramMQTTPort)))
      _mqttPort = value.toInt();
    else if (name.equals(FPSTR(paramMQTTUser)))
      _mqttUser = value;
    else if (name.equals(FPSTR(paramMQTTPassword)))
      _mqttPassword = value;
    else if (name.equals(FPSTR(paramMQTTClient)))
      _mqttClient = value;
    else
      return false;
  }
  return true;
}

void ESPMQTT::setupHttpServer() {
  ESPWebCore::setupHttpServer();
  httpServer->on(String(FPSTR(pathMQTT)).c_str(), std::bind(&ESPMQTT::handleMQTTConfig, this));
}

void ESPMQTT::handleRootPage() {
  if (! adminAuthenticate())
    return;

  String script = ESPWebCore::StdJs();
  script += F("function refreshData() {\n\
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
  script += F("').innerHTML = data.");
  script += FPSTR(jsonUptime);
  script += F(";\n");
  if (WiFi.getMode() == WIFI_STA) {
    script += FPSTR(getElementById);
    script += FPSTR(jsonRSSI);
    script += F("').innerHTML = data.");
    script += FPSTR(jsonRSSI);
    script += F(";\n");
  }
  script += F("}\n}\nrequest.send(null);\n}\nsetInterval(refreshData, 1000);\n");

  String page = ESPWebCore::webPageStart(ESPWebCore::getHostName());
  page += ESPWebCore::StdCss();
  page += ESPWebCore::webPageScript(script);
  page += ESPWebCore::webPageBody();
  page += F("<h3>ESP8266</h3>\n<div class=\"container\">\n<div class=\"params-left\">\
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
  page += F("</div>\n<div class=\"params-right\"></div>");
  page += navigator();
  page += ESPWebCore::webPageEnd();

  httpServer->send(200, FPSTR(textHtml), page);
}

void ESPMQTT::handleMQTTConfig() {
  if (! adminAuthenticate())
    return;

  String page = ESPWebCore::webPageStart(F("MQTT Setup"));
  page += ESPWebCore::StdCss();
  page += ESPWebCore::webPageBody();
  page += F("<form name=\"mqtt\" method=\"GET\" action=\"");
  page += FPSTR(pathStore);
  page += F("\">\n<h3>MQTT Setup</h3>\n\
    <label>Server:</label>\n");
  page += String(F("<input type=\"text\" name=\"mqttserver\" value=\"")) + _mqttServer + String(F("\" size=")) + String(maxStringLen) + String(F(" maxlength=")) + String(maxStringLen) + F(" placeholder=\"leave blank to ignore MQTT\">\n");  
  page += F("<label>Port:</label>\n");
  page += String(F("<input type=\"text\" name=\"mqttport\" value=\"")) + String(_mqttPort) + String(F("\" size=5 maxlength=5>\n"));
  page += F("<label>User:</label>\n");
  page += String(F("<input type=\"text\" name=\"mqttuser\" value=\"")) + _mqttUser + String(F("\" size=")) + String(maxStringLen) + String(F(" maxlength=")) + String(maxStringLen) + F(" placeholder=\"leave blank if authorization is not required\">\n");  
  page += F("<label>Password:</label>\n");
  page += String(F("<input type=\"password\" name=\"mqttpswd\" value=\"")) + _mqttPassword + String(F("\" size=")) + String(maxStringLen) + String(F(" maxlength=")) + String(maxStringLen) + F(" >\n");
  page += F("<label>Client:</label>\n");
  page += String(F("<input type=\"text\" name=\"mqttclient\" value=\"")) + _mqttClient + String(F("\" size=")) + String(maxStringLen) + String(F(" maxlength=")) + String(maxStringLen) + F(" placeholder=\"leave blank if authorization is not required\">\n");  
  page += F("<hr>\n<div class=\"btn-group\">\n<input type=\"submit\" value=\"Save\">\n");
  page += btnBack();
  page += F("<input type=\"hidden\" name=\"reboot\" value=\"1\">\n</div>\n</form>\n");
  page += ESPWebCore::webPageEnd();
  httpServer->send(200, FPSTR(textHtml), page);
}

String ESPMQTT::jsonData() {
  String result = ESPWebCore::jsonData();
  result += F(",\"");
  result += FPSTR(jsonMQTTConnected);
  result += F("\":");
  if (pubSubClient->connected())
    result += FPSTR(bools[1]);
  else
    result += FPSTR(bools[0]);

  return result;
}

String ESPMQTT::btnMQTTConfig() {
  String result = F("<input type=\"button\" value=\"MQTT Setup\" onclick=\"location.href='/mqtt'\">\n");
  return result;
}

String ESPMQTT::navigator() {
  String result = F("</div>\n<hr>\n<div class=\"btn-group\">\n");
  result += btnWiFiConfig();
  result += btnTimeConfig();
  result += btnMQTTConfig();
  result += btnReboot();
  result += F("</div>");
  return result;
}

bool ESPMQTT::mqttReconnect() {
  const uint32_t timeout = 30000;

  static uint32_t nextTime;
  bool result = false;

  if ((int32_t)millis() >= (int32_t)nextTime) {
    #ifndef NOSERIAL
        Serial.print(F("Attempting MQTT connection..."));
    #endif
//    enablePulse(PULSE);
    if (_mqttUser != "")
      result = pubSubClient->connect(_mqttClient.c_str(), _mqttUser.c_str(), _mqttPassword.c_str());
    else
      result = pubSubClient->connect(_mqttClient.c_str());
//    enablePulse(WiFi.getMode() == WIFI_STA ? BREATH : FADEIN);
//    enablePulse(BREATH);
    if (result) {
      #ifndef NOSERIAL
        Serial.println(F(" connected"));
      #endif
      mqttResubscribe();
    } else {
      #ifndef NOSERIAL
        Serial.print(F(" failed, rc="));
        Serial.println(pubSubClient->state());
      #endif
    }
    nextTime = millis() + timeout;
  }
  return result;
}

void ESPMQTT::mqttCallback(char* topic, byte* payload, unsigned int length) {
  #ifndef NOSERIAL
    Serial.print(F("MQTT message arrived ["));
    Serial.print(topic);
    Serial.print(F("] "));
  for (int i = 0; i < length; ++i) {
    Serial.print((char)payload[i]);
  }
    Serial.println();
  #endif
}

void ESPMQTT::mqttResubscribe() {
  String topic;

  if (_mqttClient != "") {
    topic += "/";
    topic += _mqttClient;
    topic += F("/#");
    mqttSubscribe(topic);
  }
}

bool ESPMQTT::mqttSubscribe(const String& topic) {
  #ifndef NOSERIAL
    Serial.print(F("MQTT subscribe to topic \""));
    Serial.print(topic);
    Serial.println('\"');
  #endif
  return pubSubClient->subscribe(topic.c_str());
}

bool ESPMQTT::mqttPublish(const String& topic, const String& value) {
  #ifndef NOSERIAL
    Serial.print(F("MQTT publish topic \""));
    Serial.print(topic);
    Serial.print(F("\" with value \""));
    Serial.print(value);
    Serial.println('\"');
  #endif
  return pubSubClient->publish(topic.c_str(), value.c_str());
}
