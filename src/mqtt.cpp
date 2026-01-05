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
    
    String clientId = "esp32_" + WiFi.macAddress();
    clientId.replace(":", "");
    
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
    
    // В режиме сна firstPublish = true → публикуем всегда
    // В обычном режиме — учитываем интервал
    if (!firstPublish && (now - lastPublishTime < config.publishingInterval))
        return;
    
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
        if (!mqttClient.publish(tempTopic.c_str(), tempValue.c_str(), true)) {
            publishSuccess = false;
        }
    }
    
    // Публикуем влажность
    if (!isnan(currentHumidity) && currentHumidity >= 0 && currentHumidity <= 100) {
        String humTopic = baseTopic + "/humidity";
        String humValue = String(currentHumidity, 1);
        if (!mqttClient.publish(humTopic.c_str(), humValue.c_str(), true)) {
            publishSuccess = false;
        }
    }
    
    // Публикуем давление в мм.рт.ст.
    if (!isnan(currentPressure) && currentPressure > 300 && currentPressure < 1200) {
        String pressTopic = baseTopic + "/pressure";
        String pressValue = String(currentPressure, 1);
        if (!mqttClient.publish(pressTopic.c_str(), pressValue.c_str(), true)) {
            publishSuccess = false;
        }
    }    
    
    // Обновляем время и флаг
    lastPublishTime = now;
    firstPublish = false;
    
    if (publishSuccess) {
        Serial.println("[MQTT] Data published successfully");
    } else {
        Serial.println("[MQTT] Partial publish failure");
    }
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
