extern "C" {
#include <sntp.h>
}
#include <pgmspace.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include "espwebcore.h"
#include <EEPROM.h>
#include "Date.h"
#include "RTCmem.h"

/*
 *   ESPWebCore class implementation
 */
ESPWebCore::ESPWebCore(){
    httpServer = new ESP8266WebServer(80);
}

void ESPWebCore::reboot() {
#ifndef NOSERIAL
  Serial.println(F("Rebooting..."));
  Serial.flush();
#endif
  ESP.restart();
}

String ESPWebCore::getBoardId() {
  String result;
  result = String(ESP.getChipId(), HEX);
  result.toUpperCase();
  return result;
}

String ESPWebCore::getHostName() {
  String result;
  result = FPSTR(defSSID);
  result += getBoardId();
  return result;
}

bool ESPWebCore::setupWiFiAsStation() {
  const uint32_t timeout = 60000; // 1 min.

  uint32_t maxTime = millis() + timeout;

  if (! _ssid.length()) {
    #ifndef NOSERIAL
        Serial.println(F("Empty SSID!"));
    #endif
    return false;
  }

  #ifndef NOSERIAL
    Serial.print(F("Connecting to "));
    Serial.println(_ssid);
  #endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(_ssid.c_str(), _password.c_str());

  while (WiFi.status() != WL_CONNECTED) {
    #ifndef NOSERIAL
      Serial.print(".");
    #endif
    delay(500);
    if ((int32_t)millis() >= (int32_t)maxTime) {
      #ifndef NOSERIAL
        Serial.println(F("FAIL!"));
      #endif
      return false;
    }
  }
  #ifndef NOSERIAL
    Serial.println(WiFi.localIP());
  #endif
  return true;
}

void ESPWebCore::setupWiFiAsAP() {
  String ssid, password;

  if (_apMode) {
    ssid = _ssid;
    password = _password;
  } else {
    ssid = getHostName();
    password = FPSTR(defPassword);
  }
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid.c_str(), password.c_str());
  #ifndef NOSERIAL
    Serial.print(F("Configuring access point "));
    Serial.print(ssid);
    Serial.print(F(" with password "));
    Serial.print(password);
    Serial.print(F(" on IP address "));
    Serial.println(WiFi.softAPIP());
  #endif
}

void ESPWebCore::setupWiFi() {
  if (_apMode || (! setupWiFiAsStation()))
    setupWiFiAsAP();

  if (_domain.length()) {
    if (MDNS.begin(_domain.c_str())) {
      MDNS.addService("http", "tcp", 80);
      #ifndef NOSERIAL
        Serial.println(F("mDNS responder started"));
      #endif
    } else {
      #ifndef NOSERIAL
        Serial.println(F("Error setting up mDNS responder!"));
      #endif
    }
  }
  onWiFiConnected();
}

void ESPWebCore::onWiFiConnected() {
  httpServer->begin();
  #ifndef NOSERIAL
    Serial.println(F("HTTP server started"));
  #endif
}

void ESPWebCore::setup() {
  analogWriteRange(255);
  EEPROM.begin(eeprom_size);
  if (! readConfig()) {
    #ifndef NOSERIAL
      Serial.println(F("EEPROM is empty!"));
    #endif
  }

  if (! readRTCmemory()) {
    #ifndef NOSERIAL
      Serial.println(F("RTC memory is empty!"));
    #endif
  }

  /* if (! SPIFFS.begin()) {
    _log->println(F("Unable to mount SPIFFS!"));
  } */

  if (! WiFi.hostname(getHostName())) {
    #ifndef NOSERIAL
      Serial.println(F("Unable to change host name!"));
    #endif
  }

  if (_ntpServer1.length() || _ntpServer2.length() || _ntpServer3.length()) {
    sntp_set_timezone(_ntpTimeZone);
    if (_ntpServer1.length())
      sntp_setservername(0, (char*)_ntpServer1.c_str());
    if (_ntpServer2.length())
      sntp_setservername(1, (char*)_ntpServer2.c_str());
    if (_ntpServer3.length())
      sntp_setservername(2, (char*)_ntpServer3.c_str());
    sntp_init();
  }
  _lastNtpTime = 0;
  _lastNtpUpdate = 0;
  setupExtra();
  setupWiFi();
  setupHttpServer();
}

void ESPWebCore::loop() {
  const uint32_t timeout = 300000; // 5 min.
  static uint32_t nextTime = timeout;

  if ((!_apMode) && (WiFi.status() != WL_CONNECTED) && ((WiFi.getMode() == WIFI_STA) || ((int32_t)millis() >= (int32_t)nextTime))) {
    setupWiFi();
    nextTime = millis() + timeout;
  }

  httpServer->handleClient();
  loopExtra();

  delay(1); // For WiFi maintenance
}

void ESPWebCore::setupExtra() {
  // Stub
}

void ESPWebCore::loopExtra() { 
  // Stub
}

bool ESPWebCore::adminAuthenticate() {
  if (_adminPassword.length()) {
    if (! httpServer->authenticate(String(FPSTR(strAdminName)).c_str(), _adminPassword.c_str())) {
      httpServer->requestAuthentication();
      return false;
    }
  }
  return true;
}

uint32_t ESPWebCore::getTime() {
  if ((WiFi.getMode() == WIFI_STA) && (_ntpServer1.length() || _ntpServer2.length() || _ntpServer3.length()) && ((! _lastNtpTime) || (_ntpUpdateInterval && (millis() - _lastNtpUpdate >= _ntpUpdateInterval)))) {
    uint32_t now = sntp_get_current_timestamp();
    if (now) {
      _lastNtpTime = now;
      _lastNtpUpdate = millis();
      if (! now)
        now = getTime();
      if (now){
        #ifndef NOSERIAL
        Serial.print(dateTimeToStr(now));      
        Serial.println(F(" time updated successfully"));
        #endif
      }
      
    } else {
      static uint32_t lastError;

      if (millis() - lastError > 5000) {
        #ifndef NOSERIAL
        Serial.println(F("Unable to update time from NTP!"));
        #endif
        lastError = millis();
      }
    }
  }
  if (_lastNtpTime)
    return _lastNtpTime + (millis() - _lastNtpUpdate) / 1000;
  else
    return 0;
}

void ESPWebCore::setTime(uint32_t now) {
  _lastNtpTime = now;
  _lastNtpUpdate = millis();
  #ifndef NOSERIAL
  Serial.print(dateTimeToStr(now));
  Serial.println(F(" time updated manualy"));
  #endif
}

void ESPWebCore::setupHttpServer() {
  httpServer->onNotFound(std::bind(&ESPWebCore::handleNotFound, this));
  httpServer->on("/", HTTP_GET, std::bind(&ESPWebCore::handleRootPage, this));
  httpServer->on(String(String('/') + FPSTR("index.html")).c_str(), HTTP_GET, std::bind(&ESPWebCore::handleRootPage, this));
  httpServer->on(String(FPSTR(pathSPIFFS)).c_str(), HTTP_GET, std::bind(&ESPWebCore::handleSPIFFS, this));
  httpServer->on(String(FPSTR(pathSPIFFS)).c_str(), HTTP_POST, std::bind(&ESPWebCore::handleFileUploaded, this), std::bind(&ESPWebCore::handleFileUpload, this));
  httpServer->on(String(FPSTR(pathSPIFFS)).c_str(), HTTP_DELETE, std::bind(&ESPWebCore::handleFileDelete, this));
  httpServer->on(String(FPSTR(pathUpdate)).c_str(), HTTP_GET, std::bind(&ESPWebCore::handleUpdate, this));
  httpServer->on(String(FPSTR(pathUpdate)).c_str(), HTTP_POST, std::bind(&ESPWebCore::handleSketchUpdated, this), std::bind(&ESPWebCore::handleSketchUpdate, this));
  httpServer->on(String(FPSTR("/wifi")).c_str(), HTTP_GET, std::bind(&ESPWebCore::handleWiFiConfig, this));
  httpServer->on(String(FPSTR(pathTime)).c_str(), HTTP_GET, std::bind(&ESPWebCore::handleTimeConfig, this));
  httpServer->on(String(FPSTR(pathStore)).c_str(), HTTP_GET, std::bind(&ESPWebCore::handleStoreConfig, this));
  httpServer->on(String(FPSTR(pathStore)).c_str(), HTTP_POST, std::bind(&ESPWebCore::handleStoreConfig, this));
  httpServer->on(String(FPSTR(pathGetTime)).c_str(), HTTP_GET, std::bind(&ESPWebCore::handleGetTime, this));
  httpServer->on(String(FPSTR(pathSetTime)).c_str(), HTTP_GET, std::bind(&ESPWebCore::handleSetTime, this));
  httpServer->on(String(FPSTR(pathReboot)).c_str(), HTTP_GET, std::bind(&ESPWebCore::handleReboot, this));
  httpServer->on(String(FPSTR(pathData)).c_str(), HTTP_GET, std::bind(&ESPWebCore::handleData, this));
}

String ESPWebCore::StdCss() {
  String style = F("<style>\nbody {display: block;height: 100%;text-align: center;width: 500px;margin: 0 auto;font: 14px/21px \"Lucida Sans\", \"Lucida Grande\", \"Lucida Sans Unicode\", sans-serif;}");
  style += F("\n.container{min-height: 120px;margin-bottom: 10px}.content {border-radius: 5px;background-color: #f2f2f2;padding: 20px;}");
  style += F("\n.params-left{text-align: left;width: 50%;display: inline-block;float:left;}.params-right{text-align: right;width: 50%;display: inline-block;}");
  style += F("\n.btn-group input[type=button],.btn-group input[type=submit]{background-color: #4CAF50;border: 1px solid green;color: white;padding: 15px 32px;text-align: center;");
  style += F("\ntext-decoration: none;font-size: 16px;cursor: pointer;width: 100%;display: block;margin-bottom: 5px;}* {box-sizing: border-box;}");
  style += F("\nform h2, form label {font-family:Georgia, Times, \"Times New Roman\", serif;}label {padding: 12px 12px 12px 0;display: inline-block;}");
  style += F("\nh3{text-align: center; color: #346392; font-size: 32px;}label {padding: 12px 12px 12px 0;display: inline-block;}");
  style += F("\ninput[type=text],input[type=password],select {width: 100%;padding: 12px;border: 1px solid #ccc;border-radius: 4px;resize: vertical;}");
  style += F("\n.btn-group input[type=button]:not(:last-child) {border-bottom: none;}.btn-group input[type=button]:hover,.btn-group input[type=submit]:hover {background-color: #3e8e4b;}");
  style += F("\n@media (min-width: 300px) and (max-width: 600px) {body{width: 100%;height: 100%;}}\n</style>");
  return style;
  //httpServer->send(200, FPSTR("text/css"), style);
}

String ESPWebCore::StdJs() {
  String script = F("function getXmlHttpRequest(){\n\
  var xmlhttp;\n\
  try{\n\
  xmlhttp=new ActiveXObject(\"Msxml2.XMLHTTP\");\n\
  }catch(e){\n\
  try{\n\
  xmlhttp=new ActiveXObject(\"Microsoft.XMLHTTP\");\n\
  }catch(E){\n\
  xmlhttp=false;\n\
  }\n\
  }\n\
  if ((!xmlhttp)&&(typeof XMLHttpRequest!='undefined')){\n\
  xmlhttp=new XMLHttpRequest();\n\
  }\n\
  return xmlhttp;\n\
  }\n\
  function openUrl(url){\n\
  var request=getXmlHttpRequest();\n\
  request.open(\"GET\",url,false);\n\
  request.send(null);\n\
  }");
  return script;
}

String ESPWebCore::webPageStyle(const String& style) {
  String result;
  result = FPSTR("<style type=\"text/css\">\n");
  result += style;
  result += FPSTR("</style>\n");
  return result;
}

String ESPWebCore::webPageScript(const String& script) {
  String result;  
  result = FPSTR(headerScriptOpen);
  result += script;
  result += FPSTR(headerScriptClose);
  return result;
}

// Ответ если страница не найдена
void ESPWebCore::handleNotFound() {
  String message = "Page not found!\n\n";
  message += "URI: ";
  message += httpServer->uri();
  message += "\nMethod: ";
  message += (httpServer->method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += httpServer->args();
  message += "\n";
  for (uint8_t i=0; i<httpServer->args(); i++){
    message += " " + httpServer->argName(i) + ": " + httpServer->arg(i) + "\n";
  }
  httpServer->send(404, "text/plain", message);
}

// Ответ при обращении к основной странице
void ESPWebCore::handleRootPage() {
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
  script += F("}\n\
}\n\
request.send(null);\n\
}\n\
setInterval(refreshData, 1000);\n");
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
  page += F("</div>\n<div class=\"params-right\"></div>");
  page += navigator();
  page += ESPWebCore::webPageEnd();

  httpServer->send(200, FPSTR(textHtml), page);
}

void ESPWebCore::handleWiFiConfig() {
  if (! adminAuthenticate())
    return;

  String script = F("function validateForm(form) {\n\
if (form.");
  script += FPSTR(paramSSID);
  script += F(".value == \"\") {\n\
alert(\"SSID must be set!\");\n\
form.");
  script += FPSTR(paramSSID);
  script += F(".focus();\n\
return false;\n\
}\n\
if (form.");
  script += FPSTR(paramPassword);
  script += F(".value == \"\") {\n\
alert(\"Password must be set!\");\n\
form.");
  script += FPSTR(paramPassword);
  script += F(".focus();\n\
return false;\n\
}\n\
form.scan.disabled = true;\n\
return true;\n\
}\n");

  String page = ESPWebCore::webPageStart(F("WiFi Setup"));
  page += ESPWebCore::StdCss();
  page += ESPWebCore::webPageScript(script);
  page += ESPWebCore::webPageBody();
  page += F("<form name=\"wifi\" method=\"GET\" action=\"");
  page += FPSTR(pathStore);
  page += F("\" onsubmit=\"return validateForm(this)\">\n\
<h3>WiFi Setup</h3>\n\
<fieldset>\n<legend>Режим работы</legend>\n");
  if (_apMode)
    page += F("<input type=\"radio\" class=\"radio\" id=\"ap\" name=\"apmode\" value=\"1\" onclick=\"document.getElementById('scan').style.display='none'\" checked>\n");
  else
    page += F("<input type=\"radio\" class=\"radio\" id=\"ap\" name=\"apmode\" value=\"1\" onclick=\"document.getElementById('scan').style.display='none'\">\n");
  page += F("<label for=\"ap\">AP</label>\n");
  if (! _apMode)
    page += F("<input type=\"radio\" class=\"radio\" id=\"radio\" name=\"apmode\" value=\"0\" onclick=\"document.getElementById('scan').style.display='block'\" checked>");
  else
    page += F("<input type=\"radio\" class=\"radio\" id=\"radio\" name=\"apmode\" value=\"0\" onclick=\"document.getElementById('scan').style.display='block'\">");
  page += F("<label for=\"radio\">Infrastructure</label>\n</fieldset>\n\
<div id=\"scan\"");
  if (_apMode)
    page += F(" style=\"display:none\"");
  page += F(">\n\
<label>Available WiFi:</label><br/>\n\
<select name=\"scan\" size=5 onchange=\"wifi.");
  page += FPSTR(paramSSID);
  page += F(".value=this.value\">\n");

  int8_t n = WiFi.scanNetworks();
  if (n > 0) {
    for (int8_t i = 0; i < n; ++i) {
      page += F("<option value=\"");
      page += WiFi.SSID(i);
      page += F("\">");
      page += WiFi.SSID(i);
      page += F(" (");
      page += WiFi.RSSI(i);
      page += F(" dBm)</option>\n");
    }
  }
  page += F("</select>\n\
</div>\n\
<label>SSID:</label>\n");
  page += String(F("<input type=\"text\" name=\"ssid\" value=\"")) + _ssid + String(F("\" size=")) + String(maxStringLen) + String(F(" maxlength=")) + String(maxStringLen) + F(" required>\n");
  page += F("<label>Password:</label>\n");
  page += String(F("<input type=\"password\" name=\"password\" value=\"")) + _password + String(F("\" size=")) + String(maxStringLen) + String(F(" maxlength=")) + String(maxStringLen) + F(" required>\n");
  page += F("<label>mDNS domain:</label>\n");
  page += String(F("<input type=\"text\" name=\"domain\" value=\"")) + _domain + String(F("\" size=")) + String(maxStringLen) + String(F(" maxlength=")) + String(maxStringLen) + F(" placeholder=\"leave blank to ignore mDNS\">\n");
  page += F("<label>Admin password:</label>\n");
  page += String(F("<input type=\"password\" name=\"adminpswd\" value=\"")) + _adminPassword + String(F("\" size=")) + String(maxStringLen) + String(F(" maxlength=")) + String(maxStringLen) + F(" required>\n");
  page += F("<hr>\n<div class=\"btn-group\">\n<input type=\"submit\" value=\"Save\">\n");
  page += btnBack();
  page += F("<input type=\"hidden\" name=\"reboot\" value=\"1\">\n</div>\n</form>\n");
  page += ESPWebCore::webPageEnd();
  httpServer->send(200, FPSTR(textHtml), page);
}

void ESPWebCore::handleTimeConfig() {
  if (! adminAuthenticate())
    return;
  String script = ESPWebCore::StdJs();
  script += F("function validateForm(form) {\n\
if (form.");
  script += FPSTR(paramNtpServer1);
  script += F(".value == \"\") {\n\
alert(\"NTP server #1 must be set!\");\n\
form.");
  script += FPSTR(paramNtpServer1);
  script += F(".focus();\n\
return false;\n\
}\n\
return true;\n\
}\n\
function updateTime() {\n\
openUrl('");
  script += FPSTR(pathSetTime);
  script += F("?time=' + Math.floor(Date.now() / 1000) + '&dummy=' + Date.now());\n\
}\n\
function refreshData() {\n\
var request = getXmlHttpRequest();\n\
request.open('GET', '");
  script += FPSTR(pathGetTime);
  script += F("?dummy=' + Date.now(), true);\n\
request.onreadystatechange = function() {\n\
if (request.readyState == 4) {\n\
var data = JSON.parse(request.responseText);\n\
if (data.");
  script += FPSTR(jsonUnixTime);
  script += F(" == 0) {\n");
  script += FPSTR(getElementById);
  script += FPSTR(jsonDate);
  script += F("').innerHTML = \"Unset\";\n");
  script += FPSTR(getElementById);
  script += FPSTR(jsonTime);
  script += F("').innerHTML = \"\";\n\
} else {\n");
  script += FPSTR(getElementById);
  script += FPSTR(jsonDate);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonDate);
  script += F(";\n");
  script += FPSTR(getElementById);
  script += FPSTR(jsonTime);
  script += F("').innerHTML = data.");
  script += FPSTR(jsonTime);
  script += F(";\n\
}\n\
}\n\
}\n\
request.send(null);\n\
}\n\
setInterval(refreshData, 1000);\n");

  String page = ESPWebCore::webPageStart(F("Time Setup"));
  page += ESPWebCore::StdCss();
  page += ESPWebCore::webPageScript(script);
  page += ESPWebCore::webPageBody();
  page += F("<form name=\"time\" method=\"GET\" action=\"");
  page += FPSTR(pathStore);
  page += F("\" onsubmit=\"return validateForm(this)\">\n\
  <h3>Time Setup</h3>\n<fieldset>\n<legend>Current date and time:</legend><span id=\"");
  page += FPSTR(jsonDate);
  page += F("\"></span> <span id=\"");
  page += FPSTR(jsonTime);
  page += F("\"></span>\n</fieldset>\n<label>NTP server #1:</label>\n");
  page += String(F("<input type=\"text\" name=\"ntpserver1\" value=\"")) + _ntpServer1 + String(F("\" size=")) + String(maxStringLen) + String(F(" maxlength=")) + String(maxStringLen) + F(">\n");
  page += F("<label>NTP server #2:</label>\n");
  page += String(F("<input type=\"text\" name=\"ntpserver2\" value=\"")) + _ntpServer2 + String(F("\" size=")) + String(maxStringLen) + String(F(" maxlength=")) + String(maxStringLen) + F(">\n");
  page += F("<label>NTP server #3:</label>\n");
  page += String(F("<input type=\"text\" name=\"ntpserver3\" value=\"")) + _ntpServer3 + String(F("\" size=")) + String(maxStringLen) + String(F(" maxlength=")) + String(maxStringLen) + F(">\n");
  page += F("<label>Time zone:</label>\n<select name=\"");
  page += FPSTR(paramNtpTimeZone);
  page += F("\" size=1>\n");
  for (int8_t i = -11; i <= 13; i++) {
    page += F("<option value=\"");
    page += String(i);
    page += charQuote;
    if (_ntpTimeZone == i)
      page += F(" selected");
    page += charGreater;
    page += F("GMT");
    if (i > 0)
      page += '+';
    page += String(i);
    page += F("</option>\n");
  }
  page += F("</select>\n<label>Update interval (in sec.):</label>\n");
  page += String(F("<input type=\"text\" name=\"ntpupdateinterval\" value=\"")) + String(_ntpUpdateInterval / 1000) + String(F("\" size=10 maxlength=10>\n"));
  page += F("<hr>\n<div class=\"btn-group\">\n");
  page += F("<input type=\"button\" value=\"Update time from browser\" onclick='updateTime()'>\n");
  page += F("<input type=\"submit\" value=\"Save\">\n");
  page += btnBack();
  page += F("<input type=\"hidden\" name=\"reboot\" value=\"1\">\n</div>\n</form>\n");
  page += ESPWebCore::webPageEnd();
  httpServer->send(200, FPSTR(textHtml), page);
}

void ESPWebCore::handleFileUploaded() {
  httpServer->send(200, FPSTR(textHtml), F("<META http-equiv=\"refresh\" content=\"2;URL=\">Upload successful."));
}

void ESPWebCore::handleFileUpload() {
  static File uploadFile;

  if (httpServer->uri() != FPSTR(pathSPIFFS))
    return;
  HTTPUpload& upload = httpServer->upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (! filename.startsWith("/"))
      filename = "/" + filename;
    uploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile)
      uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile)
      uploadFile.close();
  }
}

void ESPWebCore::handleFileDelete() {
  if (httpServer->args() == 0)
    return httpServer->send(500, FPSTR(textPlain), F("BAD ARGS"));
  String path = httpServer->arg(0);
  if (path == "/")
    return httpServer->send(500, FPSTR(textPlain), F("BAD PATH"));
  if (! SPIFFS.exists(path))
    return httpServer->send(404, FPSTR(textPlain), FPSTR("FileNotFound"));
  SPIFFS.remove(path);
  httpServer->send(200, FPSTR(textPlain), "");
  path = String();
}

void ESPWebCore::handleSPIFFS() {
  if (! adminAuthenticate())
    return;

  String script = ESPWebCore::StdJs();
  script += F("function openUrl(url, method) {\n\
  var request = getXmlHttpRequest();\n\
  request.open(method, url, false);\n\
  request.send(null);\n\
  if (request.status != 200)\n\
  alert(request.responseText);\n\
  }\n\
  function getSelectedCount() {\n\
  var inputs = document.getElementsByTagName(\"input\");\n\
  var result = 0;\n\
  for (var i = 0; i < inputs.length; i++) {\n\
  if (inputs[i].type == \"checkbox\") {\n\
  if (inputs[i].checked == true)\n\
  result++;\n\
  }\n\
  }\n\
  return result;\n\
  }\n\
  function updateSelected() {\n\
  document.getElementsByName(\"delete\")[0].disabled = (getSelectedCount() > 0) ? false : true;\n\
  }\n\
  function deleteSelected() {\n\
  var inputs = document.getElementsByTagName(\"input\");\n\
  for (var i = 0; i < inputs.length; i++) {\n\
  if (inputs[i].type == \"checkbox\") {\n\
  if (inputs[i].checked == true)\n\
  openUrl(\"");
  script += FPSTR(pathSPIFFS);
  script += F("?filename=/\" + encodeURIComponent(inputs[i].value) + '&dummy=' + Date.now(), \"DELETE\");\n\
  }\n\
  }\n\
  location.reload(true);\n\
  }\n");

  String page = ESPWebCore::webPageStart(F("SPIFFS"));
  page += ESPWebCore::StdCss();
  page += ESPWebCore::webPageScript(script);
  page += ESPWebCore::webPageBody();
  page += F("<form method=\"POST\" action=\"\" enctype=\"multipart/form-data\" onsubmit=\"if (document.getElementsByName('upload')[0].files.length == 0) { alert('No file to upload!'); return false; }\">\n\
<h3>SPIFFS</h3>\n");

  Dir dir = SPIFFS.openDir("/");
  int cnt = 0;
  while (dir.next()) {
    cnt++;
    String fileName = dir.fileName();
    size_t fileSize = dir.fileSize();
    if (fileName.startsWith("/"))
      fileName = fileName.substring(1);
    page += F("<input type=\"checkbox\" name=\"file");
    page += String(cnt);
    page += F("\" value=\"");
    page += fileName;
    page += F("\" onchange=\"updateSelected()\"><a href=\"/");
    page += fileName;
    page += F("\" download>");
    page += fileName;
    page += F("</a>\t");
    page += String(fileSize);
    page += F("<br/>\n");
  }
  page += String(cnt);
  page += F(" file(s)<br/>\n\
<p>\n");
  page += F("<input type=\"button\" name=\"delete\" value=\"Delete\" onclick=\"if (confirm('Are you sure to delete selected file(s)?') == true) deleteSelected()\" disabled>\n");
  page += F("<p>\nUpload new file:</p>\n");
  page += F("<input type=\"file\" name=\"upload\" value=\"\">\n");
  page += F("<input type=\"submit\" value=\"Upload\">\n");
  page += F("</form>\n");
  //page += F("</div>");
  page += ESPWebCore::webPageEnd();
  httpServer->send(200, FPSTR(textHtml), page);
}

void ESPWebCore::handleUpdate() {
  if (! adminAuthenticate())
    return;

  String page = ESPWebCore::webPageStart(F("Sketch Update"));
  page += ESPWebCore::StdCss();
  page += ESPWebCore::webPageBody();
  page += F("<form method=\"POST\" action=\"\" enctype=\"multipart/form-data\" onsubmit=\"if (document.getElementsByName('update')[0].files.length == 0) { alert('No file to update!'); return false; }\">\n\
  Select compiled sketch to upload:<br/>\n");
  page += F("<input type=\"file\" name=\"update\" value=\"\">\n");
  page += F("<input type=\"submit\" value=\"Update\">\n");
  page += F("</form>\n");
  page += ESPWebCore::webPageEnd();
  httpServer->send(200, FPSTR(textHtml), page);
}

void ESPWebCore::handleSketchUpdated() {
  static const char updateFailed[] PROGMEM = "Update failed!";
  static const char updateSuccess[] PROGMEM = "<META http-equiv=\"refresh\" content=\"15;URL=\">Update successful! Rebooting...";
  httpServer->send(200, FPSTR(textHtml), Update.hasError() ? FPSTR(updateFailed) : FPSTR(updateSuccess));
  reboot();
}

void ESPWebCore::handleSketchUpdate() {
  if (httpServer->uri() != FPSTR(pathUpdate))
    return;
  HTTPUpload& upload = httpServer->upload();
  if (upload.status == UPLOAD_FILE_START) {
    WiFiUDP::stopAll();
    #ifndef NOSERIAL
    Serial.print(F("Update sketch from file \""));
    Serial.print(upload.filename.c_str());
    Serial.println(charQuote);
    #endif
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (! Update.begin(maxSketchSpace)) { // start with max available size
      #ifndef NOSERIAL
        Update.printError(Serial);
      #endif
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    #ifndef NOSERIAL
      Serial.print('.');
    #endif
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
    #ifndef NOSERIAL
      Update.printError(Serial);
    #endif
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) { // true to set the size to the current progress
    #ifndef NOSERIAL
      Serial.println();
      Serial.print(F("Updated "));
      Serial.print(upload.totalSize);
      Serial.println(F(" byte(s) successful. Rebooting..."));
      #endif
    } else {
      #ifndef NOSERIAL
        Update.printError(Serial);
      #endif
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    #ifndef NOSERIAL
      Serial.println(F("Update was aborted"));
    #endif
  }
  yield();
}

void ESPWebCore::handleStoreConfig() {
  String argName, argValue;
  for (uint8_t i = 0; i < httpServer->args(); ++i) {
    argName = httpServer->argName(i);
    argValue = httpServer->arg(i);
    setConfigParam(argName, argValue);
  }
  writeConfig();
  String page = ESPWebCore::webPageStart(F("Store Setup"));
  page += F("<meta http-equiv=\"refresh\" content=\"5;URL=/\">\n");
  page += ESPWebCore::StdCss();
  page += ESPWebCore::webPageBody();
  page += F("<h4>Configuration stored successfully.</h4>\n");
  if (httpServer->arg(FPSTR("reboot")) == "1")
    page += F("<p>You must reboot module to apply new configuration!</p>\n");
  page += F("<p>Wait for 5 sec. or click <a href=\"/\">this</a> to return to main page.</p>\n");
  page += ESPWebCore::webPageEnd();
  httpServer->send(200, FPSTR(textHtml), page);
}

void ESPWebCore::handleGetTime() {
  uint32_t now = getTime();
  String page;
  page += charOpenBrace;
  page += charQuote;
  page += FPSTR(jsonUnixTime);
  page += F("\":");
  page += String(now);
  if (now) {
    int8_t hh, mm, ss;
    uint8_t wd;
    int8_t d, m;
    int16_t y;
    parseUnixTime(now, hh, mm, ss, wd, d, m, y);
    page += F(",\"");
    page += FPSTR(jsonDate);
    page += F("\":\"");
    page += dateToStr(d, m, y);
    page += F("\",\"");
    page += FPSTR(jsonTime);
    page += F("\":\"");
    page += timeToStr(hh, mm, ss);
    page += charQuote;
  }
  page += charCloseBrace;
  httpServer->send(200, FPSTR(textJson), page);
}

void ESPWebCore::handleSetTime() {
  setTime(_max(0, httpServer->arg(FPSTR(paramTime)).toInt()) + _ntpTimeZone * 3600);
  httpServer->send(200, FPSTR(textHtml), "");
}

// Перезагрузка модуля
void ESPWebCore::handleReboot() {
  if (! adminAuthenticate())
    return;
  String page = webPageStart(F("Reboot"));
  page += F("<meta http-equiv=\"refresh\" content=\"5;URL=/\">\n");
  page += ESPWebCore::StdCss();
  page += ESPWebCore::webPageBody();
  page += F("Rebooting...\n");
  page += webPageEnd();
  httpServer->send(200, FPSTR("text/html"), page);
  reboot();
}

void ESPWebCore::handleData() {
  String page;
  page += charOpenBrace;
  page += jsonData();
  page += charCloseBrace;
  httpServer->send(200, FPSTR("text/json"), page);
}

String ESPWebCore::jsonData() {
  String result;

  result += charQuote;
//  result += FPSTR(jsonFreeHeap);
//  result += F("\":");
//  result += String(ESP.getFreeHeap());
//  result += F(",\"");
  result += FPSTR(jsonUptime);
  result += F("\":");
  result += String(millis() / 1000);
  if (WiFi.getMode() == WIFI_STA) {
    result += F(",\"");
    result += FPSTR(jsonRSSI);
    result += F("\":");
    result += String(WiFi.RSSI());
  }
  return result;
}

String ESPWebCore::btnBack() {
  String result = F("<input type=\"button\" value=\"Home\" onclick=\"location.href='/'\">\n");
  return result;
}

String ESPWebCore::btnWiFiConfig() {
  String result = F("<input type=\"button\" value=\"WiFi Setup\" onclick=\"location.href='/wifi'\">\n");
  return result;
}

String ESPWebCore::btnTimeConfig() {
  String result = F("<input type=\"button\" value=\"Time Setup\" onclick=\"location.href='/time'\">\n");
  return result;
}

String ESPWebCore::btnReboot() {
  String result = F("<input type=\"button\" value=\"Reboot!\" onclick=\"if (confirm('Are you sure to reboot?')) location.href='/reboot'\">");
  return result;
}

String ESPWebCore::navigator() {
  String result = F("</div>\n<hr>\n<div class=\"btn-group\">\n");
  result += btnWiFiConfig();
  result += btnTimeConfig();
  result += btnReboot();
  result += F("</div>");
  return result;
}

String ESPWebCore::getContentType(const String& fileName) {
  if (httpServer->hasArg(F("download")))
    return String(F("application/octet-stream"));
  else if (fileName.endsWith(F(".htm")) || fileName.endsWith(F(".html")))
    return String(FPSTR(textHtml));
  else if (fileName.endsWith(F(".css")))
    return String(FPSTR(textCss));
  else if (fileName.endsWith(F(".js")))
    return String(FPSTR(applicationJavascript));
  else if (fileName.endsWith(F(".png")))
    return String(F("image/png"));
  else if (fileName.endsWith(F(".gif")))
    return String(F("image/gif"));
  else if (fileName.endsWith(F(".jpg")) || fileName.endsWith(F(".jpeg")))
    return String(F("image/jpeg"));
  else if (fileName.endsWith(F(".ico")))
    return String(F("image/x-icon"));
  else if (fileName.endsWith(F(".xml")))
    return String(F("text/xml"));
  else if (fileName.endsWith(F(".pdf")))
    return String(F("application/x-pdf"));
  else if (fileName.endsWith(F(".zip")))
    return String(F("application/x-zip"));
  else if (fileName.endsWith(F(".gz")))
    return String(F("application/x-gzip"));

  return String(FPSTR(textPlain));
}

String ESPWebCore::webPageStart(const String& title) {
  String result = FPSTR(headerTitleOpen);
  result += title;
  result += FPSTR(headerTitleClose);
  return result;
}

String ESPWebCore::webPageBody() {
  String result = F("</head>\n<body>\n<div class=\"content\">\n");
  return result;
}

String ESPWebCore::webPageEnd() {
  String result = F("</div>");
  result += FPSTR(footerBodyClose);
  return result;
}

String ESPWebCore::escapeQuote(const String& str) {
  String result;
  int start = 0, pos;

  while (start < str.length()) {
    pos = str.indexOf(charQuote, start);
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

uint16_t ESPWebCore::readRTCmemory() {
  uint16_t offset = 0;
  #ifndef NOSERIAL
    Serial.println(F("Reading RTC memory"));
  #endif
  for (uint8_t i = 0; i < sizeof(ESPWebCore::_signEEPROM); ++i) {
    char c = pgm_read_byte(ESPWebCore::_signEEPROM + i);
    if (RTCmem.read(offset++) != c)
      break;
  }
  if (offset < sizeof(ESPWebCore::_signEEPROM)) {
    #ifndef NOSERIAL
      Serial.println(F("No signature found in RTC!"));
    #endif
    return 0;
  }

  return offset;
}

uint16_t ESPWebCore::writeRTCmemory() {
  uint16_t offset = 0;

  #ifndef NOSERIAL
    Serial.println(F("Writing config to RTC"));
  #endif
  for (uint8_t i = 0; i < sizeof(ESPWebCore::_signEEPROM); ++i) {
    char c = pgm_read_byte(ESPWebCore::_signEEPROM + i);
    RTCmem.write(offset++, c);
  }

  return offset;
}

uint8_t ESPWebCore::readEEPROM(uint16_t offset) {
  return EEPROM.read(offset);
}

void ESPWebCore::readEEPROM(uint16_t offset, uint8_t* buf, uint16_t len) {
  while (len--)
    *buf++ = EEPROM.read(offset++);
}

void ESPWebCore::writeEEPROM(uint16_t offset, uint8_t data) {
  EEPROM.write(offset, data);
}

void ESPWebCore::writeEEPROM(uint16_t offset, const uint8_t* buf, uint16_t len) {
  while (len--)
    EEPROM.write(offset++, *buf++);
}

uint16_t ESPWebCore::readEEPROMString(uint16_t offset, String& str, uint16_t maxlen) {
  str = "";
  for (uint16_t i = 0; i < maxlen; ++i) {
    char c = readEEPROM(offset + i);
    if (! c)
      break;
    else
      str += c;
  }

  return offset + maxlen;
}

uint16_t ESPWebCore::writeEEPROMString(uint16_t offset, const String& str, uint16_t maxlen) {
  int slen = str.length();
  for (uint16_t i = 0; i < maxlen; ++i) {
    if (i < slen)
      writeEEPROM(offset + i, str[i]);
    else
      writeEEPROM(offset + i, 0);
  }
  return offset + maxlen;
}

void ESPWebCore::commitEEPROM() {
  EEPROM.commit();
}

void ESPWebCore::clearEEPROM() {
  for (uint16_t i = 0; i < eeprom_size; ++i)
    writeEEPROM(i, 0xFF);
  commitEEPROM();
  #ifndef NOSERIAL
    Serial.println(F("EEPROM erased succefully!"));
  #endif
}

uint8_t ESPWebCore::crc8EEPROM(uint16_t start, uint16_t end) {
  uint8_t crc = 0;
  while (start < end) {
    crc ^= readEEPROM(start++);

    for (uint8_t i = 0; i < 8; ++i)
      crc = crc & 0x80 ? (crc << 1) ^ 0x31 : crc << 1;
  }
  return crc;
}

uint16_t ESPWebCore::readConfig() {
  uint16_t offset = 0;
  #ifndef NOSERIAL
    Serial.println(F("Reading config from EEPROM"));
  #endif
  for (uint8_t i = 0; i < sizeof(ESPWebCore::_signEEPROM); ++i) {
    char c = pgm_read_byte(ESPWebCore::_signEEPROM + i);
    if (readEEPROM(offset++) != c)
      break;
  }
  if (offset < sizeof(ESPWebCore::_signEEPROM)) {
    defaultConfig();
    return 0;
  }
  getEEPROM(offset, _apMode);
  offset += sizeof(_apMode);
  offset = readEEPROMString(offset, _ssid, maxStringLen);
  offset = readEEPROMString(offset, _password, maxStringLen);
  offset = readEEPROMString(offset, _domain, maxStringLen);
  offset = readEEPROMString(offset, _adminPassword, maxStringLen);
  offset = readEEPROMString(offset, _ntpServer1, maxStringLen);
  offset = readEEPROMString(offset, _ntpServer2, maxStringLen);
  offset = readEEPROMString(offset, _ntpServer3, maxStringLen);
  getEEPROM(offset, _ntpTimeZone);
  offset += sizeof(_ntpTimeZone);
  getEEPROM(offset, _ntpUpdateInterval);
  offset += sizeof(_ntpUpdateInterval);
  uint8_t crc = crc8EEPROM(0, offset);
  if (readEEPROM(offset++) != crc) {
    #ifndef NOSERIAL
      Serial.println(F("CRC mismatch! Use default WiFi parameters."));
    #endif
    defaultConfig();
  }

  return offset;
}

uint16_t ESPWebCore::writeConfig(bool commit) {
  uint16_t offset = 0;
  #ifndef NOSERIAL
    Serial.println(F("Writing config to EEPROM"));
  #endif
  for (uint8_t i = 0; i < sizeof(ESPWebCore::_signEEPROM); ++i) {
    char c = pgm_read_byte(ESPWebCore::_signEEPROM + i);
    writeEEPROM(offset++, c);
  }
  putEEPROM(offset, _apMode);
  offset += sizeof(_apMode);
  offset = writeEEPROMString(offset, _ssid, maxStringLen);
  offset = writeEEPROMString(offset, _password, maxStringLen);
  offset = writeEEPROMString(offset, _domain, maxStringLen);
  offset = writeEEPROMString(offset, _adminPassword, maxStringLen);
  offset = writeEEPROMString(offset, _ntpServer1, maxStringLen);
  offset = writeEEPROMString(offset, _ntpServer2, maxStringLen);
  offset = writeEEPROMString(offset, _ntpServer3, maxStringLen);
  putEEPROM(offset, _ntpTimeZone);
  offset += sizeof(_ntpTimeZone);
  putEEPROM(offset, _ntpUpdateInterval);
  offset += sizeof(_ntpUpdateInterval);
  uint8_t crc = crc8EEPROM(0, offset);
  writeEEPROM(offset++, crc);
  if (commit)
    commitConfig();

  return offset;
}

inline void ESPWebCore::commitConfig() {
  commitEEPROM();
}

void ESPWebCore::defaultConfig(uint8_t level) {
  if (level < 1) {
    _apMode = true;
    _ssid = FPSTR(defSSID);
    _ssid += getBoardId();
    _password = FPSTR(defPassword);
    _domain = String();
    _adminPassword = FPSTR(defAdminPassword);
    _ntpServer1 = FPSTR(defNtpServer);
    _ntpServer2 = String();
    _ntpServer3 = String();
    _ntpTimeZone = defNtpTimeZone;
    _ntpUpdateInterval = defNtpUpdateInterval;
  }
}

bool ESPWebCore::setConfigParam(const String& name, const String& value) {
  if (name.equals(FPSTR(paramApMode)))
    _apMode = constrain(value.toInt(), 0, 1);
  else if (name.equals(FPSTR(paramSSID)))
    _ssid = value;
  else if (name.equals(FPSTR(paramPassword)))
    _password = value;
  else if (name.equals(FPSTR(paramDomain)))
    _domain = value;
  else if (name.equals(FPSTR(paramAdminPassword)))
    _adminPassword = value;
  else if (name.equals(FPSTR(paramNtpServer1)))
    _ntpServer1 = value;
  else if (name.equals(FPSTR(paramNtpServer2)))
    _ntpServer2 = value;
  else if (name.equals(FPSTR(paramNtpServer3)))
    _ntpServer3 = value;
  else if (name.equals(FPSTR(paramNtpTimeZone)))
    _ntpTimeZone = constrain(value.toInt(), -11, 13);
  else if (name.equals(FPSTR(paramNtpUpdateInterval)))
    _ntpUpdateInterval = _max(0, value.toInt()) * 1000;
  else
    return false;

  return true;
}

const char ESPWebCore::_signEEPROM[4] PROGMEM = { '#', 'E', 'S', 'P' };