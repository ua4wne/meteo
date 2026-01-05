#pragma once
#include <WiFi.h>
#include <PubSubClient.h>

extern WiFiClient espClient;
extern PubSubClient mqttClient;

void initMqtt();
void reconnectMqtt();
void handleMqtt();
void publishSensorData(float currentTemp, float currentHumidity, float currentPressure);
String generateMqttBaseTopic();
bool isMqttConfigured();
