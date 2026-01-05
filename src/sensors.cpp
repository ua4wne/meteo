#include "sensors.h"
#include "config.h"
#include <DHT.h>
#include <Adafruit_BMP085.h>

// === ПИНЫ ===
// DHT22 подключён к GPIO18
const uint8_t DHT_PIN = 18;

// Глобальные объекты
DHT dht22(DHT_PIN, DHT22);
Adafruit_BMP085 bmp180;

// Глобальные переменные
float currentTemp = -999.0;
float currentHumidity = -999.0;
float currentPressure = -999.0;
float currentVcc = 0.0;
String lastError = "";
bool sensorsInitialized = false;

void initSensors()
{
    if (sensorsInitialized) return;

    // Инициализация I2C (SDA=21, SCL=22 на вашей плате)
    Wire.begin(21, 22); // явно указываем пины для MH-ET LIVE D1 Mini ESP32

    // Инициализация DHT22
    dht22.begin();

    // Инициализация BMP180
    if (!bmp180.begin()) {
        lastError = "BMP180 not found!";
        sensorsInitialized = false;
        return;
    }

    sensorsInitialized = true;
    lastError = "";
}

void readBatteryVoltage()
{
    analogSetAttenuation(ADC_11db); // до 3.9 В
    int raw = analogRead(34);
    float voltage = (raw * 3.3f / 4095.0f) * 2.0f; // 100k+100k = коэффициент 2.0
    // Калибровка (пример):
    // voltage = voltage * 1.017; // если реальное 4.12, а код показывает 4.05
    currentVcc = voltage;
}

void readSensors()
{
    if (!sensorsInitialized) {
        initSensors();
        if (!sensorsInitialized) return;
    }

    // Измеряем напряжение батареи
    readBatteryVoltage();

    // === Чтение DHT22 ===
    float humidity = dht22.readHumidity();
    float temp = dht22.readTemperature();

    if (isnan(humidity) || isnan(temp)) {
        lastError = "DHT22 error or disconnected!";
        currentTemp = -999.0;
        currentHumidity = -999.0;
    } else {
        currentTemp = temp + config.temp_offset;
        currentHumidity = humidity;
    }

    // === Чтение BMP180 ===
    // readPressure() возвращает давление в **Паскалях**
    int32_t pressure_pa = bmp180.readPressure(); // тип int32_t!

    if (pressure_pa <= 0) {
        lastError += (lastError.length() > 0 ? " | " : "") + String("BMP180 read error");
        currentPressure = -999.0;
    } else {
        // Переводим в гПа (hPa): 1 hPa = 100 Pa
        currentPressure = pressure_pa / 133.3f; // теперь в мм. рт. ст.
    }
}
