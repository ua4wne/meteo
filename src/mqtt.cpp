#include "mqtt.h"
#include "config.h"
#include "sensors.h"
#include <ArduinoJson.h>

// Глобальные переменные
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Флаг первой публикации
bool firstPublish = true;
unsigned long lastPublishTime = 0;

// Коэффициент конвертации hPa в мм.рт.ст.
const float HPA_TO_MMHG = 0.7500616827;

/**
 * @brief Конвертация давления из hPa в мм.рт.ст.
 */
float convertPressureToMmHg(float pressureHpa) {
    return pressureHpa * HPA_TO_MMHG;
}

/**
 * @brief Генерация базового топика MQTT на основе MAC-адреса
 */
String generateMqttBaseTopic() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();
    return "/iot/" + mac + "/sensors";
}

/**
 * @brief Проверка настроек MQTT
 */
bool isMqttConfigured() {
    return strlen(config.mqtt_server) > 0 &&
           config.mqtt_port > 0;
}

/**
 * @brief Инициализация MQTT клиента
 */
void initMqtt() {
    if (!isMqttConfigured()) {
        Serial.println("[MQTT] Configuration incomplete, MQTT disabled");
        return;
    }
    
    mqttClient.setServer(config.mqtt_server, config.mqtt_port);
    mqttClient.setBufferSize(256);
    
    Serial.println("[MQTT] Client initialized");
    Serial.printf("[MQTT] Server: %s:%d\n", config.mqtt_server, config.mqtt_port);
}

/**
 * @brief Подключение/переподключение к MQTT брокеру
 */
void reconnectMqtt() {
    if (!isMqttConfigured())
        return;
        
    if (mqttClient.connected())
        return;
    
    String clientId = "esp32c3_" + WiFi.macAddress();
    clientId.replace(":", "");
    
    // Serial.print("[MQTT] Attempting connection to ");
    // Serial.print(config.mqtt_server);
    // Serial.print(":");
    // Serial.print(config.mqtt_port);
    // Serial.print(" as ");
    // Serial.print(clientId);
    // Serial.println("...");
    
    bool connected = false;
    
    if (strlen(config.mqtt_user) > 0 && strlen(config.mqtt_password) > 0) {
        connected = mqttClient.connect(
            clientId.c_str(),
            config.mqtt_user,
            config.mqtt_password
        );
    } else {
        connected = mqttClient.connect(clientId.c_str());
    }
    
    if (connected) {
        Serial.println("[MQTT] Connected successfully");
    } else {
        Serial.print("[MQTT] Connection failed, state=");
        Serial.println(mqttClient.state());
    }
}

/**
 * @brief Публикация данных датчиков
 */
void publishSensorData(float currentTemp, float currentHumidity, float currentPressure) {
    if (!isMqttConfigured())
        return;
    
    unsigned long now = millis();
    
    // Используем config.publishingInterval вместо STATUS_RETRY_DELAY
    // Публикуем при первом включении или по истечении интервала
    if (!firstPublish && (now - lastPublishTime < config.publishingInterval))
        return;
    
    // Проверяем подключение
    reconnectMqtt();
    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Not connected, skipping publish");
        return;
    }
    
    String baseTopic = generateMqttBaseTopic();
    bool publishSuccess = true;
    
    // Публикуем температуру
    if (!isnan(currentTemp) && currentTemp > -100 && currentTemp < 100) {
        String tempTopic = baseTopic + "/temperature";
        String tempValue = String(currentTemp, 1);
        
        if (mqttClient.publish(tempTopic.c_str(), tempValue.c_str(), true)) {
            //Serial.printf("[MQTT] Published to %s: %s°C\n", 
                        // tempTopic.c_str(), tempValue.c_str());
        } else {
            Serial.printf("[MQTT] Failed to publish temperature to %s\n", tempTopic.c_str());
            publishSuccess = false;
        }
    } else {
        Serial.println("[MQTT] Temperature value invalid, skipping");
    }
    
    // Публикуем влажность
    if (!isnan(currentHumidity) && currentHumidity >= 0 && currentHumidity <= 100) {
        String humTopic = baseTopic + "/humidity";
        String humValue = String(currentHumidity, 1);
        
        if (mqttClient.publish(humTopic.c_str(), humValue.c_str(), true)) {
            //Serial.printf("[MQTT] Published to %s: %s%%\n", 
            //             humTopic.c_str(), humValue.c_str());
        } else {
            Serial.printf("[MQTT] Failed to publish humidity to %s\n", humTopic.c_str());
            publishSuccess = false;
        }
    } else {
        Serial.println("[MQTT] Humidity value invalid, skipping");
    }
    
    // Публикуем давление в мм.рт.ст.
    if (!isnan(currentPressure) && currentPressure > 800 && currentPressure < 1200) {
        float pressureMmHg = convertPressureToMmHg(currentPressure);
        String pressTopic = baseTopic + "/pressure";
        String pressValue = String(pressureMmHg, 1);
        
        if (mqttClient.publish(pressTopic.c_str(), pressValue.c_str(), true)) {
            //Serial.printf("[MQTT] Published to %s: %s мм.рт.ст.\n", 
             //            pressTopic.c_str(), pressValue.c_str());
            //Serial.printf("[MQTT] Original value: %.1f hPa = %.1f mmHg\n", 
            //             currentPressure, pressureMmHg);
        } else {
            Serial.printf("[MQTT] Failed to publish pressure to %s\n", pressTopic.c_str());
            publishSuccess = false;
        }
    } else {
        Serial.println("[MQTT] Pressure value invalid, skipping");
    }
    
    // Публикуем статус устройства
    // String statusTopic = baseTopic + "/status";
    // String statusValue = publishSuccess ? "online" : "error";
    
    // mqttClient.publish(statusTopic.c_str(), statusValue.c_str(), true);
    //Serial.printf("[MQTT] Status: %s\n", statusValue.c_str());
    
    // Обновляем время и флаг
    lastPublishTime = now;
    firstPublish = false;
    
    //Serial.printf("[MQTT] Next publish in %.1f minutes\n", 
    //             config.publishingInterval / 60000.0);
}

/**
 * @brief Обработка MQTT (вызывать в loop)
 */
void handleMqtt() {
    if (!isMqttConfigured())
        return;
    
    // Поддерживаем соединение
    if (!mqttClient.connected()) {
        static unsigned long lastReconnectAttempt = 0;
        unsigned long now = millis();
        
        // Пытаемся переподключиться каждые 5 секунд
        if (now - lastReconnectAttempt > 5000) {
            lastReconnectAttempt = now;
            reconnectMqtt();
        }
    } else {
        // Обрабатываем входящие сообщения
        mqttClient.loop();
    }
}
