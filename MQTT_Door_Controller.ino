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

#include <EEPROM.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

// EEPROM details
#define EEPROM_SIZE 256
#define MAX_STRING_LENGTH 64 // includes null terminator
#define FLAG_ADDRESS (EEPROM_SIZE - 1)
#define INIT_FLAG 0x2D // used to determine if values have been stored

// Preprocessor directives for hash function
#define SEED 5381   // seed value
#define PRIME 7919  // prime number for modulo operation

// Preproccessor directives for input/output pins
#define BTN_WIFI_RES D0
#define LED_LOCK D1
#define LED_UNLOCK D2

// Wi-Fi access point details
const char *portalName = "Door Lock Config";

// EEPROM locations for MQTT server parameters
#define MQTT_SERVER_START 0
#define MQTT_USER_START (MQTT_SERVER_START + MAX_STRING_LENGTH)
#define MQTT_PASSWORD_START (MQTT_USER_START + MAX_STRING_LENGTH)

// MQTT server details
char mqttServer[MAX_STRING_LENGTH];
const int mqttPort = 50000; // not configurable at this time
char mqttUser[MAX_STRING_LENGTH];
char mqttPassword[MAX_STRING_LENGTH];

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

// Initialize EEPROM
void initEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(FLAG_ADDRESS) != INIT_FLAG) {
    for (int i = 0; i < EEPROM_SIZE; i++) {
      EEPROM.write(i, '\0');
    }
    EEPROM.write(FLAG_ADDRESS, INIT_FLAG);
    EEPROM.commit();
  }
  EEPROM.end();
}

// Get a parameter in EEPROM using output parameter
void getSavedString(int start, char *buffer) {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < MAX_STRING_LENGTH; i++) {
    buffer[i] = EEPROM.read(start + i);
    if (buffer[i] == '\0') break; // stop at null terminator
  }
  EEPROM.end();
}

// Set a parameter in EEPROM
void setSavedString(int start, char *value) {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < MAX_STRING_LENGTH; i++) {
    EEPROM.write(start + i, value[i]);
    if (value[i] == '\0') break; // stop at null terminator
  }
  EEPROM.commit(); // save changes
  EEPROM.end();
}

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
String djb2Modified(const String& input) {
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
  Serial.println();
}

// MQTT callback function
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.printf("\nMessage arrived on topic: %s. Message: %s\n", topic, message.c_str());

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
        lastHash = djb2Modified(randomString);
        String payload = String(userNumber) + randomString;
        client.publish(topicValidationRequests, payload.c_str());
        Serial.printf("Validation request sent to user %d: %s\n", userNumber, payload.c_str());
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
      Serial.println("Hash mismatch, command not executed.\n");
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

  // Initialize EEPROM if no values are stored
  initEEPROM();

  // Get existing parameters from EEPROM (may be empty)
  getSavedString(MQTT_SERVER_START, mqttServer);
  getSavedString(MQTT_USER_START, mqttUser);
  getSavedString(MQTT_PASSWORD_START, mqttPassword);

  bool anyParamNotSet = strlen(mqttServer) == 0 || strlen(mqttUser) == 0 || strlen(mqttUser) == 0;

  // Initialize Wi-Fi via WiFiManager
  WiFi.mode(WIFI_STA);
  WiFiManager wm;

  // Open configuration if button is held
  if (digitalRead(BTN_WIFI_RES) == LOW || anyParamNotSet) {
    Serial.println(anyParamNotSet ? "MQTT Server parameters not set, opening configuration portal" : "User requested to open configuration portal");

    // Create custom parameters (will only show up if configure portal is opened for this purpose)
    char *tempMqttServer = mqttServer;
    char *tempMqttUser = mqttUser;
    char *tempMqttPassword = mqttPassword;
    WiFiManagerParameter mqttServerParam("mqttServer", "MQTT Server", tempMqttServer, MAX_STRING_LENGTH);
    WiFiManagerParameter mqttUserParam("mqttUser", "MQTT User", tempMqttUser, MAX_STRING_LENGTH);
    WiFiManagerParameter mqttPasswordParam("mqttPassword", "MQTT Password", tempMqttPassword, MAX_STRING_LENGTH);
    wm.addParameter(&mqttServerParam);
    wm.addParameter(&mqttUserParam);
    wm.addParameter(&mqttPasswordParam);

    wm.startConfigPortal(portalName);
    Serial.println("Configuration portal closed");

    // Save parameters
    strncpy(mqttServer, mqttServerParam.getValue(), MAX_STRING_LENGTH);
    strncpy(mqttUser, mqttUserParam.getValue(), MAX_STRING_LENGTH);
    strncpy(mqttPassword, mqttPasswordParam.getValue(), MAX_STRING_LENGTH);
    Serial.printf("New MQTT credentials from configuration portal:\nServer: <%s>\nUser: <%s>\nPassword: <%s>\nSaving to EEPROM\n", mqttServer, mqttUser, mqttPassword);
    setSavedString(MQTT_SERVER_START, mqttServer);
    setSavedString(MQTT_USER_START, mqttUser);
    setSavedString(MQTT_PASSWORD_START, mqttPassword);
  }

    Serial.println("Attempting to connect to WiFi...");
    bool res = wm.autoConnect(portalName);

  // Force manual reboot if Wi-Fi connection fails
  if (!res) {
      Serial.println("Failed to connect to WiFi. Restarting.");
      ESP.restart();
  }

  // Check again for empty parameters. Force reboot if present
  anyParamNotSet = strlen(mqttServer) == 0 || strlen(mqttUser) == 0 || strlen(mqttUser) == 0;
  if (anyParamNotSet) {
    Serial.println("Missing MQTT parameter. Restarting.");
      ESP.restart();
  }

  // Initialize MQTT
  client.setServer(mqttServer, mqttPort);
  client.setCallback(mqttCallback);

  // Connect to MQTT broker
  Serial.println("Connecting to MQTT broker...");
  while (!client.connected()) {
    if (client.connect(mqttUser, mqttUser, mqttPassword)) {
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
