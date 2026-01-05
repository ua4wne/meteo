#ifndef WEB_H
#define WEB_H

#include <ESPAsyncWebServer.h>
#include <Arduino.h>

extern AsyncWebServer server;

void initWebServer();
bool isAuthorized(AsyncWebServerRequest *request);

#endif