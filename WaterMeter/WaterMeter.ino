/* Water Meter based on the PubSub example

Copyright: Andreas Spiess, 19.2.2021

MIT license
*/


#include <WiFi.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <credentials.h>

#define BUILTIN_LED 2

#define OUTTOPIC "water"
#define IRPIN 36

//#define DEBUG

// Update these with values suitable for your network.

const char* ssid = mySSID;
const char* password = myPASSWORD;
const char* mqtt_server = "192.168.0.203";

int waterTicks, lastTicks;
int irLevel, lastLevel, diff, lastDiff;
int irMin = 9999;
int irMiddle;
int irMax = 0;
unsigned long entry = millis();
int machineStat = 0;
unsigned long currentMeasurement, lastMeasurement;
float consumption;

int addr = 0;
#define EEPROM_SIZE 10

struct eepromLayout
{
  int magicNumber;
  unsigned long value;
} eepromContent;

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE	(200)
char msg[MSG_BUFFER_SIZE];
int value = 0;

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

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
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish(OUTTOPIC"/debug", "hello world");
      // ... and resubscribe
      client.subscribe("inTTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void incrementTicks() {
  currentMeasurement = millis();
  Serial.println("Increment");
  digitalWrite(BUILTIN_LED, HIGH);   // Turn the LED on (Note that LOW is the voltage level
  delay(500);
  digitalWrite(BUILTIN_LED, LOW);  // Turn the LED off by making the voltage HIGH
  waterTicks++;
  if (waterTicks % 2 == 0) {
    if (currentMeasurement - lastMeasurement > 0) consumption = 2000.0 / (currentMeasurement - lastMeasurement);  // two ticks divided by the time between the two ticks; protect against division by zero
    else consumption = 0.0;
    Serial.println(consumption);

    snprintf (msg, MSG_BUFFER_SIZE, "{\"consumption\":%lf, \"total\":%ld}", consumption, waterTicks);
    Serial.print("Water ");
    Serial.println(msg);
    client.publish(OUTTOPIC"/value", msg);
    lastMeasurement = currentMeasurement;
  }

  if (((waterTicks % 1000) == 0)) {
    snprintf (msg, MSG_BUFFER_SIZE, "Stored %ld", waterTicks);
    Serial.print("Store in eeprom ");
    Serial.println(msg);
    client.publish(OUTTOPIC"/debug", msg);

    eepromContent.value = waterTicks;
    EEPROM.put(0, eepromContent);
    EEPROM.commit();
  }
}

void setup() {
  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  Serial.begin(115200);

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

  if (!EEPROM.begin(EEPROM_SIZE))
  {
    Serial.println("failed to initialise EEPROM");
    delay(1000);
    ESP.restart();
  }
  EEPROM.get(0, eepromContent);
  if (eepromContent.magicNumber != 0x55) {
    Serial.println("EEprom Corrupted");
    eepromContent.magicNumber = 0x55;
    eepromContent.value =  1591001;
    EEPROM.put(0, eepromContent);
    EEPROM.commit();
  }
  else waterTicks = eepromContent.value;
  Serial.print("Stored consumption ");
  Serial.println(waterTicks);
  currentMeasurement = millis();
  lastMeasurement = currentMeasurement;
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
    diff = (3 * diff + (irLevel - lastLevel)) / 4;  // first derivative (averaged)

    switch (machineStat) {
      case 0:
        if ((diff < 2) && (irLevel > (irMiddle + 100))) {
          incrementTicks();
          machineStat = 1;
        }
        break;
      case 1:
        if ((diff > 10) && irLevel < irMiddle - 100) machineStat = 2;
        break;
      case 2:
        if ((diff < 2) && (irLevel < (irMiddle - 100))) machineStat = 3;
        break;
      case 3:
        if (diff > 10 && irLevel > irMiddle + 100) machineStat = 0;
        break;
    }

    
#ifdef DEBUG
    snprintf (msg, MSG_BUFFER_SIZE, "Min %ld, Middle %ld, Max %ld, Level %ld, diff %ld, Stat %ld, Ticks %ld", irMin, irMiddle, irMax, irLevel, diff, machineStat, waterTicks);
    client.publish(OUTTOPIC"/debug", msg);
    delay(500);
#endif
    lastLevel = irLevel;
  }
}
