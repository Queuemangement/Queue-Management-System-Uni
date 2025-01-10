#include <WiFi.h>
#include <esp_now.h>
#include <TM1637Display.h>
#include <vector>

// TM1637 Display Pins
#define CLK 22
#define DIO 23

// Initialize the display
TM1637Display display(CLK, DIO);

// Vector to store order numbers (no size limit)
std::vector<int> orderNumbers;  // Vector to store order numbers

// Time tracking variables
unsigned long previousMillis = 0;
const long interval = 5000;  // Interval to display each order number (5 seconds)

// Callback when data is received
void onDataReceive(const esp_now_recv_info* info, const uint8_t* incomingData, int len) {
  // Convert incoming data to a string
  char receivedOrder[5] = "";
  memcpy(receivedOrder, incomingData, len);
  receivedOrder[len] = '\0'; // Null-terminate the received string

  // Convert the received string (char[]) to an integer (int)
  int orderNumber = atoi(receivedOrder);

  // Check if the order number already exists in the vector
  auto it = std::find(orderNumbers.begin(), orderNumbers.end(), orderNumber);

  // If the order number exists, remove it from the vector
  if (it != orderNumbers.end()) {
    orderNumbers.erase(it);  // Remove the duplicate order number
  } else {
    // If order number does not exist, add it to the vector
    orderNumbers.push_back(orderNumber);
  }

  // Print the order number
  Serial.print("Received Order: ");
  Serial.println(orderNumber);
  Serial.print("Stored Orders: ");
  for (int i = 0; i < orderNumbers.size(); i++) {
    Serial.print(orderNumbers[i]);
    Serial.print(" ");
  }
  Serial.println();
}

// Display orders every 5 seconds
void displayNextOrder() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    // Save the last time you displayed the order
    previousMillis = currentMillis;

    // If there are orders in the vector, display the next one
    if (!orderNumbers.empty()) {
      static int index = 0;  // Index to loop through the order numbers

      // Display the order number at the current index
      display.clear();
      display.showNumberDec(orderNumbers[index], true);

      // Move to the next order number
      index = (index + 1) % orderNumbers.size();
    }
    else{
      display.clear();
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize the display
  display.setBrightness(7);  // Set brightness to max
  display.clear();  // Clear display initially

  // Initialize Wi-Fi in STA mode
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register receive callback for ESP-NOW
  esp_now_register_recv_cb(onDataReceive);
}

void loop() {
  // Call the function to display orders
  displayNextOrder();
}
