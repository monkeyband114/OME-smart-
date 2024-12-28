#include "arduino_secrets.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <MFRC522.h>
#include <time.h>

// WiFi credentials
const char* ssid = "Dinstack";
const char* password = "355blmgat";

// MQTT Broker settings
const char* mqtt_server = "a9a4aebc.ala.us-east-1.emqxsl.com";
const int mqtt_port = 8883;
const char* mqtt_user = "diniot";
const char* mqtt_password = "125566aa";

// MQTT topics
const char* door_topic = "actuators/door";
const char* light_topic = "actuators/light";
const char* gas_topic = "sensors/gas";
const char* light_sensor_topic = "sensors/light";

const String AUTHORIZED_UID = "E3CAD630";

// Pin definitions for ESP32
const int DOOR_LOCK_PIN = 21;    // Replace with GPIO number
const int LIGHT_PIN = 22;        // Replace with GPIO number
const int LDR_PIN = 34;          // Replace with GPIO number
const int MQ2_PIN = 35;          // Replace with GPIO number
const int RFID_SS_PIN = 5;       // Use GPIO5 for SS
const int RFID_RST_PIN = 4;      // Use GPIO4 for RST

// RFID
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

// WiFi and MQTT clients
WiFiClientSecure espClient;
PubSubClient client(espClient);

// Timers for sensor readings
unsigned long lastLightReading = 0;
unsigned long lastGasReading = 0;
const long sensorInterval = 5000; // Read sensors every 5 seconds

// NTP Server settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;

void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(DOOR_LOCK_PIN, OUTPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(MQ2_PIN, INPUT);

  digitalWrite(DOOR_LOCK_PIN, HIGH);

  // Initialize RFID
  SPI.begin();
  rfid.PCD_Init();
  
  // Connect to WiFi
  setup_wifi();
  
  // Configure time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Configure MQTT
  espClient.setInsecure(); // Only for testing, use certificates in production
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(message);

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, message);

  if (strcmp(topic, door_topic) == 0) {
    String state = doc["command"];
    if (state == "open") {
      digitalWrite(DOOR_LOCK_PIN, HIGH);
      Serial.println("Door unlocked");
    } else if (state == "closed") {
      digitalWrite(DOOR_LOCK_PIN, LOW);
      Serial.println("Door locked");
    }
  }
  
  if (strcmp(topic, light_topic) == 0) {
    String state = doc["command"];
    if (state == "on") {
      digitalWrite(LIGHT_PIN, HIGH);
      Serial.println("Light turned on");
    } else {
      digitalWrite(LIGHT_PIN, LOW);
      Serial.println("Light turned off");
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("connected");
      client.subscribe(door_topic);
      client.subscribe(light_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println("try again in 5 seconds");
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long currentMillis = millis();

  // Read and publish light sensor data
  if (currentMillis - lastLightReading >= sensorInterval) {
    lastLightReading = currentMillis;
    int ldrValue = analogRead(LDR_PIN);
    
    DynamicJsonDocument doc(1024);
    doc["value"] = ldrValue;
    
    char buffer[256];
    serializeJson(doc, buffer);
    client.publish(light_sensor_topic, buffer);
    
    Serial.print("Light sensor value: ");
    Serial.println(ldrValue);
  }

  // Read and publish gas sensor data
  if (currentMillis - lastGasReading >= sensorInterval) {
    lastGasReading = currentMillis;
    int gasValue = analogRead(MQ2_PIN);
    
    DynamicJsonDocument doc(1024);
    doc["value"] = gasValue;
    
    char buffer[256];
    serializeJson(doc, buffer);
    client.publish(gas_topic, buffer);
    
    Serial.print("Gas sensor value: ");
    Serial.println(gasValue);
  }

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    // Build RFID UID as a single string
    String rfidTag = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      rfidTag += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
      rfidTag += String(rfid.uid.uidByte[i], HEX);
    }
    rfidTag.toUpperCase();

    Serial.print("RFID Tag detected: ");
    Serial.println(rfidTag);

    // Check if RFID UID matches the authorized UID
    if (rfidTag == AUTHORIZED_UID) {
      Serial.println("Authorized UID detected, unlocking door.");
      digitalWrite(DOOR_LOCK_PIN, HIGH);
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }
}
