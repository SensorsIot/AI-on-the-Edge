/* Water Meter based on the PubSub example

     IR sensor

  Copyright: Andreas Spiess, 19.2.2021

  MIT license
*/


#include <WiFi.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <credentials.h>
#include <ArduinoJson.h>

#define BUILTIN_LED 2

#define OUTTOPIC "water"
#define IRPIN 33

#define DEBUG

// Update these with values suitable for your network.

const char* ssid = mySSID;
const char* password = myPASSWORD;
const char* mqtt_server = "192.168.0.203";
int wifiTrials, mqttTrials = 0;
RTC_DATA_ATTR int bootCount;

int waterTicks, lastTicks;
int irLevel, lastLevel, diff, lastDiff;
int irMin = 9999;
int irMiddle;
int irMax = 0;
unsigned long entry, lastaliveMessage;
int machineStat, lastMaschStat;
int consumption;

WiFiClient espClient;
PubSubClient client(espClient);

#define MSG_BUFFER_SIZE	(200)
char msg[MSG_BUFFER_SIZE];
char msg1[MSG_BUFFER_SIZE];

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  wifiTrials = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTrials < 50) {
    delay(500);
    wifiTrials++;
    Serial.print(".");
  }

  if (wifiTrials >= 100) ESP.restart();

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(BUILTIN_LED, LOW);
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    // but actually the LED is on; this is because
    // it is active low on the ESP-01)
  } else {
  }

}

void reconnect() {
  // Loop until we're reconnected
  mqttTrials = 0;
  while (!client.connected() && (mqttTrials < 5)) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      StaticJsonDocument<200> doc;
      doc["messageType"] = "X";
      serializeJsonPretty(doc, Serial);
      serializeJsonPretty(doc, msg);
      client.publish(OUTTOPIC"/debug", msg);
      // ... and resubscribe
      client.subscribe("inTTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      Serial.print("RSSI: ");
      Serial.println(WiFi.RSSI());
      // Wait 5 seconds before retrying
      delay(5000);
    }
    mqttTrials++;
    Serial.print("mqttTrials: ");
    Serial.println(mqttTrials);
  }
  if (mqttTrials >= 5) ESP.restart();
}

void incrementTicks() {
  consumption = 1;
  /*
    Serial.println(consumption);
    snprintf (msg, MSG_BUFFER_SIZE, "{\"consumption\":%ld}", consumption);
    Serial.print("Water ");
    Serial.println(msg);
    client.publish(OUTTOPIC"/value", msg);
  */

  StaticJsonDocument<200> doc;
  lastaliveMessage = millis();
  doc["messageType"] = "C";
  doc["consumption"] = consumption;
  serializeJsonPretty(doc, Serial);
  for (int i = 0; i < MSG_BUFFER_SIZE; i++)msg[i] = 0;
  serializeJsonPretty(doc, msg);
  client.publish(OUTTOPIC"/value", msg);
  lastaliveMessage = millis();
}

void setup() {
  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  Serial.begin(115200);
  delay(1000);
  bootCount++;
  Serial.println("Boot number: " + String(bootCount));

  digitalWrite(BUILTIN_LED, HIGH);
  setup_wifi();
  ArduinoOTA.setHostname("WaterMeter");
  ArduinoOTA
  .onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  })
  .onEnd([]() {
    Serial.println("\nEnd");
  })
  .onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  })
  .onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  entry = millis();
  lastaliveMessage = millis();
}

void loop() {
  ArduinoOTA.handle();
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (millis() - entry > 500) {
    entry = millis();
    irLevel = analogRead(IRPIN);
    if (irLevel > irMax) irMax = irLevel;   // maximum signal
    if (irLevel < irMin) irMin = irLevel;   // minimum signal
    irMiddle = (irMax + irMin) / 2;   // calculate middle of the signal
    diff = irLevel - lastLevel;  // first derivative (averaged)
    lastLevel = irLevel;

#ifdef DEBUG
    snprintf (msg1, MSG_BUFFER_SIZE, "Alive Message: Min %ld, Middle %ld, Max %ld, Level %ld, diff %ld, RSSI %ld, Boot %ld, Stat %ld, ", irMin, irMiddle, irMax, irLevel, diff, WiFi.RSSI(), bootCount, machineStat);
    Serial.println(msg1);
#endif

    switch (machineStat) {
      case 0:
        if ((diff < 0) && (irLevel > irMiddle)) {
          incrementTicks();
          machineStat = 1;
        }
        digitalWrite(BUILTIN_LED, LOW);
        break;
      case 1:
        if ((diff < 0) && (irLevel < irMiddle)) {
          irMax = irMax - 100; // for longterm adjustment
          machineStat = 2;
        }
        digitalWrite(BUILTIN_LED, HIGH);
        break;
      case 2:
        if ((diff > 0) && (irLevel < irMiddle)) machineStat = 3;
        digitalWrite(BUILTIN_LED, LOW);
        break;
      case 3:
        if (diff > 0 && (irLevel > irMiddle)) {
          irMin = irMin + 100; // long-term adjustment
          machineStat = 0;
        }
        digitalWrite(BUILTIN_LED, HIGH);
        break;
      default:
        break;
    }
  }
#ifdef DEBUG
  if (machineStat != lastMaschStat) {
    StaticJsonDocument<200> doc;
    lastaliveMessage = millis();
    doc["messageType"] = "D";
    doc["min"] = irMin;
    doc["middle"] = irMiddle;
    doc["max"] = irMax;
    doc["level"] = irLevel;
    doc["diff"] = diff;
    doc["RSSI"] = WiFi.RSSI();
    doc["boot"] = bootCount;
    doc["Stat"] = machineStat;
    serializeJsonPretty(doc, Serial);
    serializeJsonPretty(doc, msg);
    client.publish(OUTTOPIC"/debug", msg);
    lastaliveMessage = millis();
    lastMaschStat = machineStat;
  }
#endif

  if (millis() > (lastaliveMessage + 1 * 60 * 1000)) {
    StaticJsonDocument<200> doc;
    doc["messageType"] = "A";
    doc["min"] = irMin;
    doc["middle"] = irMiddle;
    doc["max"] = irMax;
    doc["level"] = irLevel;
    doc["diff"] = diff;
    doc["RSSI"] = WiFi.RSSI();
    doc["boot"] = bootCount;
    doc["Stat"] = machineStat;
    serializeJsonPretty(doc, Serial);
    for (int i = 0; i < MSG_BUFFER_SIZE; i++) msg[i] = 0;
    serializeJsonPretty(doc, msg);
    client.publish(OUTTOPIC"/debug", msg);
    lastaliveMessage = millis();
  }
}
