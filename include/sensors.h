#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>               // ← для DHT22
#include <Adafruit_BMP085.h>   // ← для BMP180 (библиотека называется BMP085)

// Внешние объекты
extern DHT dht22;
extern Adafruit_BMP085 bmp180;

// Глобальные переменные
extern float currentTemp;
extern float currentHumidity;
extern float currentPressure;
extern float currentVcc;
extern String lastError;

// Функции
void initSensors();
void readSensors();

#endif
