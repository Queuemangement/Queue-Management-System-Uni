#include <WiFi.h>
#include <FirebaseESP32.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <time.h>
#include <Arduino.h>
#include <esp_now.h>

// Wi-Fi настройки
const char* ssid = "WifiName";
const char* password = "WifiPassWord";

// NTP настройки
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3 * 3600;  // Смещение времени UTC+3
const int daylightOffset_sec = 0;     // Летнее время

// Firebase настройки
FirebaseConfig config;
FirebaseAuth auth;
FirebaseData firebaseData;


// RFID пины
#define RST_PIN 0  //отсоединено чтобы решить проблему прекращения считавания карты через какое то время (несколько часов)
#define SDA_PIN 5
MFRC522 rfid(SDA_PIN, RST_PIN);

// ЖК-дисплей
LiquidCrystal_I2C lcd(0x27, 16, 2);

// EEPROM настройки
#define EEPROM_SIZE 512
#define MAX_ORDERS 20
#define EEPROM_DATE_ADDR 0      // Адрес для хранения дня
#define EEPROM_COUNTER_ADDR 4   // Адрес для хранения orderCounter
#define EEPROM_HOURS_ADDR 8     // Адрес для хранения часов
#define EEPROM_MINUTES_ADDR 12  // Адрес для хранения минут

// Структура для хранения заказа
struct Order {
  char uid[12];
  uint8_t status;
  char orderNumber[4];
};

// Глобальные переменные
int orderCounter = 1;
int currentDay = 0;  // Текущий день
bool eepromBusy = false;
unsigned long time1 = 0;        // Инициализация времени
int updateIntervalMinutes = 5;  // Интервал обновления времени в минутах
#define buzzer 4

// ESP-NOW Peer Address (Slave MAC Address)
uint8_t slaveAddress[] = { 0xB0, 0xA7, 0x32, 0xF2, 0xAD, 0x98 };  // Replace with the MAC address of your slave

// Order to be sent
char orderToSend[4] = "";  // Buffer to hold the order number

void setup() {
  lcd.init();
  lcd.backlight();
  Serial.begin(115200);

  // Инициализация EEPROM
  initEEPROM();
  // testEEPROM();

  // Подключение к Wi-Fi
  lcd.setCursor(0, 0);
  lcd.print("Connecting Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi!");
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Getting time");
  // Настройка NTP
  setupNTP();

  // Получение текущего дня
  currentDay = getCurrentDay();
  if (currentDay == -1) {
    Serial.println("Failed to obtain current day. Restarting...");
    ESP.restart();
  }
  lcd.setCursor(0, 1);
  lcd.print("Success");
  Serial.printf("Today's day is: %d\n", currentDay);
  // Проверка и установка orderCounter
  checkAndSetOrderCounter();
  Serial.printf("Order counter starts at: %d\n", orderCounter);

  // Проверка разницы времени и очистка заказов, если необходимо
  checkAndClearOrdersBasedOnTime();

  // Инициализация Firebase
  config.database_url = "https://queue-management-system-21507-default-rtdb.firebaseio.com/";
  config.signer.tokens.legacy_token = "LFPBdhXOLDQLtNtMNfgZrIENL6nNxQ7FRCr119TB";
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  lcd.setCursor(0, 0);
  lcd.print("Firebase ready");
  delay(500);
  lcd.clear();

  // Инициализация RFID
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID initialized.");

  pinMode(buzzer, OUTPUT);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register peer
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, slaveAddress, 6);
  peerInfo.channel = 0;  // Default channel
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  xTaskCreatePinnedToCore(taskMaintain, "Maintainence", 8192, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(taskUpdateTime, "UpdateTime", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(taskRFID, "RFID", 8192, NULL, 1, NULL, 1);
}

// Настройка времени через NTP
void setupNTP() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("NTP server configured");
}

void initEEPROM() {
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("Failed to initialize EEPROM");
    while (true) {
      delay(1000);
    }
  }
  Serial.println("EEPROM initialized");
}

// Функция для сохранения текущего времени в EEPROM
void saveTimeToEEPROM(int hours, int minutes) {
  EEPROM.put(EEPROM_HOURS_ADDR, hours);
  EEPROM.put(EEPROM_MINUTES_ADDR, minutes);
  EEPROM.commit();
  Serial.printf("Saved time to EEPROM: %02d:%02d\n", hours, minutes);
}

// Функция для загрузки времени из EEPROM
void loadTimeFromEEPROM(int& hours, int& minutes) {
  EEPROM.get(EEPROM_HOURS_ADDR, hours);
  EEPROM.get(EEPROM_MINUTES_ADDR, minutes);
  Serial.printf("Loaded time from EEPROM: %02d:%02d\n", hours, minutes);
}

// Функция для вычисления разницы времени в минутах
int calculateTimeDifference(int currentHours, int currentMinutes, int storedHours, int storedMinutes) {
  int currentTotalMinutes = currentHours * 60 + currentMinutes;
  int storedTotalMinutes = storedHours * 60 + storedMinutes;
  int difference = abs(currentTotalMinutes - storedTotalMinutes);

  // Если текущее время меньше записанного (перекат через полночь)
  if (currentTotalMinutes < storedTotalMinutes) {
    difference = (24 * 60 - storedTotalMinutes) + currentTotalMinutes;
  }

  return difference;
}

void sendOrderToSlave(const char* orderNumber) {
  strcpy(orderToSend, orderNumber);

  esp_err_t result = esp_now_send(slaveAddress, (uint8_t*)orderToSend, sizeof(orderToSend));
  if (result == ESP_OK) {
    Serial.println("Order sent successfully");
  } else {
    Serial.println("Error sending the order");
  }
}

// Функция проверки времени и очистки заказов
void checkAndClearOrdersBasedOnTime() {
  int storedHours = 0, storedMinutes = 0;
  int currentHours, currentMinutes;

  // Получаем текущее время
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) {
    Serial.println("Failed to obtain current time.");
    return;
  }
  currentHours = timeInfo.tm_hour;
  currentMinutes = timeInfo.tm_min;

  // Загружаем сохраненное время из EEPROM
  loadTimeFromEEPROM(storedHours, storedMinutes);

  // Вычисляем разницу во времени
  int timeDifference = calculateTimeDifference(currentHours, currentMinutes, storedHours, storedMinutes);
  Serial.printf("Time difference: %d minutes\n", timeDifference);

  // Если разница >= 30 минут, очищаем заказы
  if (timeDifference >= 30) {
    Serial.println("Time difference >= 30 minutes, clearing all orders...");
    clearAllOrders();
  }

  // Обновляем время в EEPROM
  saveTimeToEEPROM(currentHours, currentMinutes);
}
// Получение текущего дня через time.h
int getCurrentDay() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) {
    Serial.println("Failed to obtain time from NTP");
    return -1;  // Возвращаем ошибку
  }
  return timeInfo.tm_mday;  // Возвращаем день месяца
}

// Сохранение дня и счетчика в EEPROM
void saveOrderCounter(int counter) {
  EEPROM.put(EEPROM_COUNTER_ADDR, counter);
  EEPROM.commit();
  Serial.printf("Saved to EEPROM Counter = %d\n", counter);
}

// Загрузка дня и счетчика из EEPROM
void loadDateAndOrderCounter(int& day, int& counter) {
  EEPROM.get(EEPROM_DATE_ADDR, day);
  EEPROM.get(EEPROM_COUNTER_ADDR, counter);
  Serial.printf("Loaded from EEPROM: Day = %d, Counter = %d\n", day, counter);
}

void saveOrderToEEPROM(int index, Order order) {
  int addr = index * sizeof(Order);
  Serial.print("Saving to EEPROM at index ");
  Serial.print(index);
  Serial.print(", address ");
  Serial.println(addr);

  memset(order.uid + sizeof(order.uid) - 1, 0, 1);
  memset(order.orderNumber + sizeof(order.orderNumber) - 1, 0, 1);

  EEPROM.put(addr, order);
  EEPROM.commit();
  Serial.println("Order saved:");
  Serial.print("  UID: ");
  Serial.println(order.uid);
  Serial.print("  Status: ");
  Serial.println(order.status);
  Serial.print("  Order Number: ");
  Serial.println(order.orderNumber);
}

Order loadOrderFromEEPROM(int index) {
  int addr = index * sizeof(Order);
  Order order;

  // Читаем данные из EEPROM
  EEPROM.get(addr, order);

  // Проверяем, является ли ячейка пустой
  if (strlen(order.uid) == 0 || strlen(order.uid) > sizeof(order.uid) - 1 || strlen(order.orderNumber) == 0 || strlen(order.orderNumber) > sizeof(order.orderNumber) - 1) {
    memset(&order, 0, sizeof(Order));  // Обнуляем данные, если они некорректны
    return order;                      // Возвращаем пустую структуру
  }

  // Вывод информации, только если ячейка не пустая
  Serial.print("Loading from EEPROM at index ");
  Serial.print(index);
  Serial.print(", address ");
  Serial.println(addr);

  Serial.println("Order loaded:");
  Serial.print("  UID: ");
  Serial.println(order.uid);
  Serial.print("  Status: ");
  Serial.println(order.status);
  Serial.print("  Order Number: ");
  Serial.println(order.orderNumber);

  return order;  // Возвращаем заполненную структуру
}


void deleteOrderFromEEPROM(int index) {
  int addr = index * sizeof(Order);
  Order emptyOrder = {};
  EEPROM.put(addr, emptyOrder);
  EEPROM.commit();
  Serial.print("Cleared EEPROM at index ");
  Serial.println(index);
}

int findOrderIndex(String uid) {
  for (int i = 0; i < MAX_ORDERS; i++) {
    Order order = loadOrderFromEEPROM(i);
    if (String(order.uid) == uid) {
      Serial.print("Order found at index: ");
      Serial.println(i);
      return i;
    }
  }
  Serial.println("Order not found.");
  return -1;
}

int getNextAvailableIndex() {
  for (int i = 1; i < MAX_ORDERS; i++) {  //начинаем с 1, т.к. 0 занят уже для хранения EEPROM_DATE_ADDR
    if (i != EEPROM_COUNTER_ADDR) {       //исключаем адрес EEPROM_COUNTER_ADDR
      Order order = loadOrderFromEEPROM(i);
      if (strlen(order.uid) == 0) {
        return i;
      }
    }
  }
  return -1;
}

String getUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  Serial.println("Scanned UID: " + uid);
  beepSound(150);
  return uid;
}

void maintainFirebaseConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Reconnecting to Wi-Fi...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.print(".");
    }
    Serial.println("\nReconnected to Wi-Fi!");
  }

  if (!Firebase.ready()) {
    Serial.println("Reinitializing Firebase connection...");
    ESP.restart();
  }
}

// Проверка текущего дня и обновление orderCounter
void checkAndSetOrderCounter() {
  int savedDay = 0;
  int savedCounter = 0;

  // Загружаем сохраненные данные
  loadDateAndOrderCounter(savedDay, savedCounter);

  if (currentDay != savedDay) {
    // Если дни не совпадают, сбрасываем счетчик и обновляем день
    orderCounter = 1;
    saveOrderCounter(orderCounter);
    clearAllOrders();
    EEPROM.put(EEPROM_DATE_ADDR, currentDay);
    EEPROM.commit();
    Serial.println("New day detected, counter reset to 1");
  } else {
    // Если дни совпадают, продолжаем с последнего значения
    orderCounter = savedCounter + 1;
    Serial.println("Continuing from last saved counter");
  }
}

void processOrder(String uid) {
  if (eepromBusy == false) {
    eepromBusy = true;
    int orderIndex = findOrderIndex(uid);
    Order order;

    if (orderIndex != -1) {
      order = loadOrderFromEEPROM(orderIndex);
      if (order.status == 0) {
        order.status = 1;
        saveOrderToEEPROM(orderIndex, order);
        Serial.println("Order updated to Ready locally: " + String(order.orderNumber));
        // Send the order number to the slave device
        sendOrderToSlave(order.orderNumber);
        if (Firebase.ready()) {
          String path = "/orders/" + String(order.uid);
          Firebase.setString(firebaseData, path + "/status", "Ready");
        }
      } else if (order.status == 1) {
        deleteOrderFromEEPROM(orderIndex);
        // Send the order number to the slave device
        sendOrderToSlave(order.orderNumber);
        Serial.println("Order completed locally: " + String(order.orderNumber));

        if (Firebase.ready()) {
          String path = "/orders/" + String(order.uid);
          Firebase.deleteNode(firebaseData, path);
        }
      }
    } else {
      int index = getNextAvailableIndex();
      if (index != -1) {
        strcpy(order.uid, uid.c_str());
        order.status = 0;
        sprintf(order.orderNumber, "%03d", orderCounter);
        saveOrderToEEPROM(index, order);
        saveOrderCounter(orderCounter);
        Serial.println("New order created locally: " + String(order.orderNumber));
        lcd.clear();                           // Очищаем экран
        lcd.setCursor(0, 0);                   // Устанавливаем курсор
        lcd.print("Order: ");                  // Печатаем метку
        lcd.print(String(order.orderNumber));  // Печатаем номер заказа
        if (Firebase.ready()) {
          String path = "/orders/" + String(order.uid);
          Firebase.setString(firebaseData, path + "/orderNumber", String(order.orderNumber));
          Firebase.setString(firebaseData, path + "/status", "Pending");
        }
        vTaskDelay(6000 / portTICK_PERIOD_MS);  // Оставляем номер заказа на 7 секунд
        lcd.clear();
        orderCounter++;
      } else {
        Serial.println("EEPROM full. Cannot save new order.");
      }
    }
    eepromBusy = false;
  } else {
    vTaskDelay(5);
  }
}

void syncEEPROMToFirebase() {
  if (!Firebase.ready()) return;

  for (int i = 0; i < MAX_ORDERS; i++) {
    if (eepromBusy == false) {
      eepromBusy = true;
      Order order = loadOrderFromEEPROM(i);
      if (strlen(order.uid) > 0 && strlen(order.orderNumber) > 0) {
        String path = "/orders/" + String(order.uid);
        String status = (order.status == 0) ? "Pending" : "Ready";

        if (Firebase.setString(firebaseData, path + "/orderNumber", String(order.orderNumber)) && Firebase.setString(firebaseData, path + "/status", status)) {
          Serial.println("Order synced: " + String(order.orderNumber));
        } else {
          Serial.println("Failed to sync order: " + String(order.orderNumber));
        }
      }
      eepromBusy = false;
    } else {
      vTaskDelay(5);
    }
  }
}

void clearAllOrders() {
  Serial.println("Clearing all orders from EEPROM...");

  // Проходим по всем возможным индексам для заказов
  for (int i = 1; i < MAX_ORDERS; i++) {  // Начинаем с 1, так как 0 используется для других данных
    Order emptyOrder = {};                // Создаем пустой заказ
    int addr = i * sizeof(Order);         // Вычисляем адрес для записи

    // Сохраняем пустой заказ в EEPROM
    EEPROM.put(addr, emptyOrder);
    EEPROM.commit();

    Serial.printf("Order at index %d cleared.\n", i);
  }

  Serial.println("All orders have been cleared.");
}

void beepSound(int duration) {
  digitalWrite(buzzer, HIGH);
  delay(duration);
  digitalWrite(buzzer, LOW);
}

void taskMaintain(void* pvParameters) {
  while (1) {
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Free stack: %u bytes\n", uxTaskGetStackHighWaterMark(NULL));
    maintainFirebaseConnection();
    syncEEPROMToFirebase();
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Free stack: %u bytes\n", uxTaskGetStackHighWaterMark(NULL));
    vTaskDelay(30000);
  }
}
void taskRFID(void* pvParameters) {
  while (1) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      String uid = getUID();
      processOrder(uid);
      rfid.PICC_HaltA();
      vTaskDelay(300 / portTICK_PERIOD_MS);  // Пауза после обработки карты
    } else {
      vTaskDelay(50 / portTICK_PERIOD_MS);  // Уменьшение нагрузки в случае отсутствия карты
    }
  }
}

// Задача для периодического обновления времени
void taskUpdateTime(void* pvParameters) {
  while (1) {
    // Получаем текущее время
    struct tm timeInfo;
    if (!getLocalTime(&timeInfo)) {
      Serial.println("Failed to obtain current time for update.");
    } else {
      saveTimeToEEPROM(timeInfo.tm_hour, timeInfo.tm_min);
    }
    vTaskDelay(updateIntervalMinutes * 60000 / portTICK_PERIOD_MS);  // Задержка в N минут
  }
}

void loop() {
  delay(500);
}