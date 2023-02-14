//декларация класса-ядра по базовой работе с web для ESP8266
#include <ESP8266WebServer.h>

// Односимвольные константы
const char charOpenBrace = '{';
const char charCloseBrace = '}';
const char charQuote = '"';
const char charGreater = '>';

const char defSSID[] PROGMEM = "ESP_"; // Префикс имени точки доступа по умолчанию
const char defPassword[] PROGMEM = "P@$$w0rd"; // Пароль точки доступа по умолчанию
const char defAdminPassword[] PROGMEM = "12345678"; // Пароль для административного доступа
const char defNtpServer[] PROGMEM = "pool.ntp.org"; // NTP-сервер по умолчанию
const int8_t defNtpTimeZone = 3; // Временная зона по умолчанию (-11..13, +3 - Москва)
const uint32_t defNtpUpdateInterval = 3600000; // Интервал в миллисекундах для обновления времени с NTP-серверов (по умолчанию 1 час)
const char getElementById[] PROGMEM = "document.getElementById('";

// Имена JSON-переменных
const char jsonUptime[] PROGMEM = "uptime";
const char jsonRSSI[] PROGMEM = "rssi";
const char jsonUnixTime[] PROGMEM = "unixtime";
const char jsonDate[] PROGMEM = "date";
const char jsonTime[] PROGMEM = "time";
const char bools[][6] PROGMEM = { "false", "true" };

// Имена параметров для Web-форм
const char paramApMode[] PROGMEM = "apmode";
const char paramSSID[] PROGMEM = "ssid";
const char paramPassword[] PROGMEM = "password";
const char paramDomain[] PROGMEM = "domain";
const char paramUserPassword[] PROGMEM = "userpswd";
const char paramAdminPassword[] PROGMEM = "adminpswd";
const char paramNtpServer1[] PROGMEM = "ntpserver1";
const char paramNtpServer2[] PROGMEM = "ntpserver2";
const char paramNtpServer3[] PROGMEM = "ntpserver3";
const char paramNtpTimeZone[] PROGMEM = "ntptimezone";
const char paramNtpUpdateInterval[] PROGMEM = "ntpupdateinterval";
const char paramTime[] PROGMEM = "time";
//const char paramReboot[] PROGMEM = "reboot";

const uint16_t maxStringLen = 32; // Максимальная длина строковых параметров в Web-интерфейсе
const char strAdminName[] PROGMEM = "admin";
const char pathStdJs[] PROGMEM = "/std.js";
const char pathStore[] PROGMEM = "/store"; // Путь до страницы сохранения параметров
const char pathReboot[] PROGMEM = "/reboot"; // Путь до страницы перезагрузки
const char pathData[] PROGMEM = "/data"; // Путь до страницы получения JSON-пакета данных
const char pathTime[] PROGMEM = "/time"; // Путь до страницы конфигурации параметров времени
const char pathGetTime[] PROGMEM = "/gettime"; // Путь до страницы получения JSON-пакета времени
const char pathSetTime[] PROGMEM = "/settime"; // Путь до страницы ручной установки времени
const char pathSPIFFS[] PROGMEM = "/spiffs"; // Путь до страницы просмотра содержимого SPIFFS
const char pathUpdate[] PROGMEM = "/update"; // Путь до страницы OTA-обновления
const char textHtml[] PROGMEM = "text/html";
const char textJson[] PROGMEM = "text/json";
const char textCss[] PROGMEM = "text/css";
const char applicationJavascript[] PROGMEM = "application/javascript";
const char textPlain[] PROGMEM = "text/plain";
const char headerScriptOpen[] PROGMEM = "<script type=\"text/javascript\">\n";
const char headerScriptClose[] PROGMEM = "</script>\n";
const char headerScriptExtOpen[] PROGMEM = "<script type=\"text/javascript\" src=\"";
const char headerScriptExtClose[] PROGMEM = "\"></script>\n";
const char headerTitleOpen[] PROGMEM = "<!DOCTYPE html>\n<html>\n<head>\n\
<meta name=\"viewport\" content=\"width=device-width; initial-scale=1.0\">\
<meta charset=\"UTF-8\">\n\
<title>";
const char headerTitleClose[] PROGMEM = "</title>\n";
const char headerBodyOpen[] PROGMEM = "</head>\n\
<body";
const char footerBodyClose[] PROGMEM = "</body>\n\
</html>";

class ESPWebCore { // Базовый класс
public:
  ESPWebCore();
  virtual void setup(); // Метод должен быть вызван из функции setup() скетча
  virtual void loop(); // Метод должен быть вызван из функции loop() скетча
  virtual void reboot(); // Перезагрузка модуля
  ESP8266WebServer* httpServer; // Web-сервер

protected:
  virtual void setupExtra(); // Дополнительный код инициализации
  virtual void loopExtra(); // Дополнительный код главного цикла
  static String webPageStart(const String& title); // HTML-код заголовка Web-страницы
  static String webPageStyle(const String& style); // HTML-код стилевого блока
  static String webPageScript(const String& script); // HTML-код скриптового блока
  static String webPageBody(); // HTML-код заголовка тела страницы
  static String webPageEnd(); // HTML-код завершения Web-страницы
  static String escapeQuote(const String& str); // Экранирование двойных кавычек для строковых значений в Web-формах
  const uint16_t eeprom_size = 4096; //объем FLASH, выделяемый под пользовательские настройки
  bool _apMode = false; // Режим точки доступа (true) или инфраструктуры (false)
  String _ssid; // Имя сети или точки доступа
  String _password; // Пароль сети
  String _domain; // mDNS домен
  String _adminPassword;
  String _ntpServer1; // NTP-серверы
  String _ntpServer2;
  String _ntpServer3;
  int8_t _ntpTimeZone; // Временная зона (в часах от UTC)
  uint32_t _ntpUpdateInterval; // Период в миллисекундах для обновления времени с NTP-серверов
  virtual String getBoardId(); // Строковый идентификатор модуля ESP8266
  virtual String getHostName();

  virtual uint16_t readRTCmemory(); // Чтение параметров из RTC-памяти ESP8266
  virtual uint16_t writeRTCmemory(); // Запись параметров в RTC-память ESP8266

  virtual uint8_t readEEPROM(uint16_t offset); // Чтение одного байта из EEPROM
  virtual void readEEPROM(uint16_t offset, uint8_t* buf, uint16_t len); // Чтение буфера из EEPROM
  virtual void writeEEPROM(uint16_t offset, uint8_t data); // Запись одного байта в EEPROM
  virtual void writeEEPROM(uint16_t offset, const uint8_t* buf, uint16_t len); // Запись буфера в EEPROM
  virtual uint16_t readEEPROMString(uint16_t offset, String& str, uint16_t maxlen); // Чтение строкового параметра из EEPROM, при успехе возвращает смещение следующего параметра
  virtual uint16_t writeEEPROMString(uint16_t offset, const String& str, uint16_t maxlen); // Запись строкового параметра в EEPROM, возвращает смещение следующего параметра
  template<typename T> T& getEEPROM(uint16_t offset, T& t) { // Шаблон чтения переменной из EEPROM
    readEEPROM(offset, (uint8_t*)&t, sizeof(T));
    return t;
  }
  template<typename T> const T& putEEPROM(uint16_t offset, const T& t) { // Шаблон записи переменной в EEPROM
    writeEEPROM(offset, (const uint8_t*)&t, sizeof(T));
    return t;
  }
  virtual void commitEEPROM(); // Завершает запись в EEPROM
  virtual void clearEEPROM(); // Стирает конфигурацию в EEPROM
  virtual uint8_t crc8EEPROM(uint16_t start, uint16_t end); // Вычисление 8-ми битной контрольной суммы участка EEPROM

  virtual uint16_t readConfig(); // Чтение конфигурационных параметров из EEPROM
  virtual uint16_t writeConfig(bool commit = true); // Запись конфигурационных параметров в EEPROM
  virtual void commitConfig(); // Подтверждение сохранения EEPROM
  virtual void defaultConfig(uint8_t level = 0); // Установление параметров в значения по умолчанию
  virtual bool setConfigParam(const String& name, const String& value); // Присвоение значений параметрам по их имени
 
  virtual bool setupWiFiAsStation(); // Настройка модуля в режиме инфраструктуры
  virtual void setupWiFiAsAP(); // Настройка модуля в режиме точки доступа
  virtual void setupWiFi(); // Попытка настройки модуля в заданный параметрами режим, при неудаче принудительный переход в режим точки доступа
  virtual void onWiFiConnected(); // Вызывается после активации беспроводной сети
  virtual bool adminAuthenticate();
  virtual uint32_t getTime(); // Возвращает время в формате UNIX-time с учетом часового пояса или 0, если ни разу не удалось получить точное время
  virtual void setTime(uint32_t now); // Ручная установка времени в формате UNIX-time
  virtual void setupHttpServer(); // Настройка Web-сервера (переопределяется для добавления обработчиков новых страниц)
  virtual String StdCss(); //общий стиль CSS
  virtual String StdJs(); //общий код JS
  virtual void handleNotFound(); // Обработчик несуществующей страницы
  virtual void handleRootPage(); // Обработчик главной страницы
  virtual void handleFileUploaded(); // Обработчик страницы окончания загрузки файла в SPIFFS
  virtual void handleFileUpload(); // Обработчик страницы загрузки файла в SPIFFS
  virtual void handleFileDelete(); // Обработчик страницы удаления файла из SPIFFS
  virtual void handleSPIFFS(); // Обработчик страницы просмотра списка файлов в SPIFFS
  virtual void handleUpdate(); // Обработчик страницы выбора файла для OTA-обновления скетча
  virtual void handleSketchUpdated(); // Обработчик страницы окончания OTA-обновления скетча
  virtual void handleSketchUpdate(); // Обработчик страницы OTA-обновления скетча
  virtual void handleReboot(); // Обработчик страницы перезагрузки модуля
  virtual void handleWiFiConfig(); // Обработчик страницы настройки параметров беспроводной сети
  virtual void handleStoreConfig(); // Обработчик страницы сохранения параметров
  virtual void handleTimeConfig(); // Обработчик страницы настройки параметров времени
  virtual void handleGetTime(); // Обработчик страницы, возвращающей JSON-пакет времени
  virtual void handleSetTime(); // Обработчик страницы ручной установки времени
  virtual void handleData(); // Обработчик страницы, возвращающей JSON-пакет данных
  virtual String jsonData(); // Формирование JSON-пакета данных
  virtual String btnBack(); // HTML-код кнопки "назад" для интерфейса
  virtual String btnWiFiConfig(); // HTML-код кнопки настройки параметров беспроводной сети
  virtual String btnTimeConfig(); // HTML-код кнопки настройки параметров времени
  virtual String btnReboot(); // HTML-код кнопки перезагрузки
  virtual String navigator(); // HTML-код кнопок интерфейса главной страницы
  virtual String getContentType(const String& fileName); // MIME-тип фала по его расширению
  static const char _signEEPROM[4] PROGMEM; // Сигнатура в начале EEPROM для определения, что параметры имеет смысл пытаться прочитать

  private:
  uint32_t _lastNtpTime; // Последнее полученное от NTP-серверов время в формате UNIX-time
  uint32_t _lastNtpUpdate; // Значение millis() в момент последней синхронизации времени

};