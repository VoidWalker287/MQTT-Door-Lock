/**
 * MQTT SECURE DOOR LOCK
 * Authors:
 *  Kyle Smith
 *  Ryan Januszko
 * Description:
 *  This code is for a device intended to serve as a "smart" door lock. It uses an MQTT broker to communicate with a controller device.
 *  This device authorizes user commands via a shared hash function, seed value, and prime number. If a request is made, but a bad hash value
 *  is returned, the command will not execute. The hash function used is a modified from DJB2.
*/

#include <WiFiManager.h>
#include <PubSubClient.h>

// Preprocessor directives for hash function
#define SEED 5381   // seed value
#define PRIME 7919  // prime number for modulo operation

// Preproccessor directives for input/output pins
#define BTN_WIFI_RES D0
#define LED_LOCK D1
#define LED_UNLOCK D2

// Wi-Fi access point details
const char *portalName = "Door Lock Config";

// MQTT server details
#define MQTT_SERVER_MAX_LENGTH 64
#define MQTT_USER_MAX_LENGTH 15
#define MQTT_PASSWORD_MAX_LENGTH 20

char mqttServer[MQTT_SERVER_MAX_LENGTH];
int mqttPort = 50000;
char mqttUser[];
char *mqttPassword = "DoorLock1";
char mqttDeviceName = "DoorLock";

// Topics
const char *topicCommands = "commands";
const char *topicValidationRequests = "validation/requests";
const char *topicValidationResponses = "validation/responses";

// Global variables
String lastCommand = "";
String lastHash = "";
int userNumber = -1;

// Wi-Fi and MQTT clients
WiFiClient espClient;
PubSubClient client(espClient);

// Generate a random 8-character string
String generateRandomString() {
  String result = "";
  for (int i = 0; i < 8; i++) {
    char randomChar = 'A' + random(0, 26); // Random capital letter
    result += randomChar;
  }
  return result;
}

// Modified DJB2 hash function
String calculateHash(const String& input) {
  uint64_t hash = SEED;
  for (char c : input) {
    hash = (hash * 33) + c;
  }
  hash %= PRIME; // modulo with stored prime number
  return String(hash);
}

void lock() {
  digitalWrite(LED_LOCK, HIGH);
  digitalWrite(LED_UNLOCK, LOW);
}

void unlock() {
  digitalWrite(LED_LOCK, LOW);
  digitalWrite(LED_UNLOCK, HIGH);
}

// Execute the stored command
void executeCommand() {
  if (lastCommand == "unlock") {
    Serial.println("Executing unlock command...");
    unlock();
  } else if (lastCommand == "lock") {
    Serial.println("Executing lock command...");
    lock();
  } else {
    Serial.println("Unknown command, nothing to execute.");
  }
}

// MQTT callback function
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.printf("Message arrived on topic: %s. Message: %s\n", topic, message.c_str());

  if (String(topic) == topicCommands) {
    // Parse the command
    if (message.length() > 1 && isDigit(message[0])) {
      userNumber = message[0] - '0'; // Extract user number
      String command = message.substring(1); // Extract command
      if (command == "unlock" || command == "lock") {
        lastCommand = command;
        Serial.printf("Valid command received: User %d, Command %s\n", userNumber, lastCommand.c_str());
        // Generate a random string and hash
        String randomString = generateRandomString();
        lastHash = calculateHash(randomString);
        client.publish(topicValidationRequests, randomString.c_str());
        Serial.printf("Validation request sent: %s\n", randomString.c_str());
      } else {
        Serial.println("Invalid command received.");
      }
    } else {
      Serial.println("Malformed command received.");
    }
  } else if (String(topic) == topicValidationResponses) {
    // Compare the received hash with the stored hash
    Serial.printf("Hash received: %s from user %c. Expected: %s\n", message.substring(1), message[0], lastHash);
    if (message.charAt(0) == userNumber + '0' && message.substring(1).equals(lastHash)) {
      Serial.println("Hash matched, executing command.");
      executeCommand();
    } else {
      Serial.println("Hash mismatch, command not executed.");
    }
  }
}

void setup() {
  pinMode(BTN_WIFI_RES, INPUT_PULLUP);
  pinMode(LED_LOCK, OUTPUT);
  pinMode(LED_UNLOCK, OUTPUT);
  lock(); // for demonstration, begin in "locked" mode

  Serial.begin(115200);
  delay(100);
  Serial.println("########## MQTT Door Lock System --- Kyle Smith & Ryan Januszko ##########");

  // Initialize Wi-Fi via WiFiManager
  WiFi.mode(WIFI_STA);
  WiFiManager wm;

  // Open configuration if button is held
  if (digitalRead(BTN_WIFI_RES) == LOW) {
    Serial.println("User requested to open configuration portal");
    wm.startConfigPortal(portalName);
  } else {
    bool res;
    Serial.println("Attempting to connect to WiFi...");
    res = wm.autoConnect(portalName);

    if (!res) {
      Serial.println("Failed to connect to WiFi. Going to sleep. Reboot device.");
      esp_deep_sleep_start();
    }
  }

  // Initialize MQTT
  client.setServer(mqttServer, mqttPort);
  client.setCallback(mqttCallback);

  // Connect to MQTT broker
  Serial.println("Connecting to MQTT broker...");
  while (!client.connected()) {
    if (client.connect(mqttDeviceName, mqttUser, mqttPassword)) {
      Serial.println("Connected to MQTT broker");
      client.subscribe(topicCommands);
      client.subscribe(topicValidationResponses);
      Serial.println("Subscribed to topics");
    } else {
      Serial.print("Failed to connect, state: ");
      Serial.println(client.state());
      delay(5000);
    }
  }

  // Seed the random number generator
  randomSeed(analogRead(0));
}

void loop() {
  client.loop(); // Ensure the MQTT connection remains active
}
