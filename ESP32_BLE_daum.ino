/**
 * A BLE client example that is rich in capabilities.
 * There is a lot new capabilities implemented.
 * author unknown
 * updated by chegewara
 */

#include <Arduino.h>
#include "BLEDevice.h"
//#include "BLEScan.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

const String sketchName = "ESP32_HRM_1";

// The remote service we wish to connect to.
//static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
// The characteristic of the remote service we are interested in.
//static BLEUUID charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");
// The remote HRM service we wish to connect to.
static BLEUUID serviceUUID(BLEUUID((uint16_t)0x180D));
// The HRM characteristic of the remote service we are interested in.
static BLEUUID charUUID(BLEUUID((uint16_t)0x2A37));

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic *pRemoteCharacteristic;
static BLEAdvertisedDevice *myDevice;

// Wifi
WiFiClient wifi_client;
const char *ssid = "cytron";
const char *password = "Trust no 1. Make it so!";

// MQTT
PubSubClient mqtt(wifi_client);
const char *mqtt_server = "192.168.0.213";
const String base_topic = "iot/hrmtest/";
long lastReconnectAttempt = 0;

unsigned long screen_update, stats_update;

// TypeDef
typedef struct {
  char ID[20];
  uint16_t HRM;
} HRM;
 HRM hrm;

//--------------------------------------------------------------------------------------------
// Setup the Serial Port and output Sketch name and compile date
//--------------------------------------------------------------------------------------------
void startSerial(uint32_t baud) {

  // Setup Serial Port aut 115200 Baud
  Serial.begin(baud);

  delay(10);

  Serial.println();
  Serial.print(sketchName);
  Serial.print(F(" | Compiled: "));
  Serial.print(__DATE__);
  Serial.print(F("/"));
  Serial.println(__TIME__);
}  //

//--------------------------------------------------------------------------------------------
// MQTT callback routine
//--------------------------------------------------------------------------------------------
void callback(char *topic, byte *payload, unsigned int length) {
  // handle message arrived
}  // End of callback

//--------------------------------------------------------------------------------------------
// MQTT send update
//--------------------------------------------------------------------------------------------
void sendMqttStats() {
  mqtt.publish((base_topic + "esp32" + F("/$name")).c_str(), sketchName.c_str(), true);
  mqtt.publish((base_topic + "esp32" + F("/$localip")).c_str(), WiFi.localIP().toString().c_str(), true);
  mqtt.publish((base_topic + "esp32" + F("/$mac")).c_str(), WiFi.macAddress().c_str(), true);
  mqtt.publish((base_topic + "esp32" + F("/$arch")).c_str(), "xtensa-esp32", true);
  mqtt.publish((base_topic + "esp32" + F("/$sdk")).c_str(), String(ARDUINO).c_str(), true);
  mqtt.publish((base_topic + "esp32" + F("/$fwversion")).c_str(), (String(__DATE__) + "/" + String(__TIME__)).c_str(), true);
  mqtt.publish((base_topic + "esp32" + F("/$online")).c_str(), "true", true);
}  // End of sendMqttStats()

//--------------------------------------------------------------------------------------------
// MQTT reconnect
//--------------------------------------------------------------------------------------------
boolean reconnect() {
  if (mqtt.connect("ESP32Client", "alex", "volMper#691", (base_topic + "esp32" + F("/$online")).c_str(), 1, true, "false")) {
    Serial.println(F("MQTT Connected"));

    // Once connected, publish an announcement...
    sendMqttStats();

    // ... and resubscribe
  }
  return mqtt.connected();
}  // End of reconnect

//--------------------------------------------------------------------------------------------
// Publish JSON to MQTT
//--------------------------------------------------------------------------------------------
void mqttPublish(const char *topic, JsonObject &sendJson) {

  if (mqtt.connected()) {
    char buffer[sendJson.measureLength() + 1];
    sendJson.printTo(buffer, sizeof(buffer));
    mqtt.publish(topic, buffer, true);
  }

}  // End of mqttPublish

//--------------------------------------------------------------------------------------------
// Send HRM stats to MQTT
//--------------------------------------------------------------------------------------------
void sendHRMData() {

  DynamicJsonBuffer dataBuffer;
  JsonObject &dataJson = dataBuffer.createObject();

  dataJson[F("HRM")] = hrm.HRM;

  mqttPublish((base_topic + "esp32" + F("/json")).c_str(), dataJson);
}  // End of sendHRMData

// Callback function to handle notifications
static void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify) {
  // Serial.print("Notify callback for characteristic ");
  // Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
  // Serial.print(" of data length ");
  // Serial.println(length);
  // Serial.print("data: ");
  // Serial.write(pData, length);
  // Serial.println();

  // if (bitRead(pData[0], 0) == 1) {
  //   Serial.println(F("16bit HeartRate Detected"));
  // } else {
  //   Serial.println(F("8bit HeartRate Detected"));
  // }

  if (length == 2) {
    hrm.HRM = pData[1];
    Serial.print("Heart Rate ");
    Serial.print(hrm.HRM, DEC);
    Serial.println("bpm");

    //sendHRMData();
  }
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) {}

  void onDisconnect(BLEClient *pclient) {
    connected = false;
    Serial.println("onDisconnect");
  }
};

bool connectToServer() {
  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient *pClient = BLEDevice::createClient();
  Serial.println(" - Created client");

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remove BLE Server.
  pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(" - Connected to server");
  pClient->setMTU(517);  //set client to request maximum MTU from server (default is 23 otherwise)

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  // Read the value of the characteristic.
  if (pRemoteCharacteristic->canRead()) {
    String value = pRemoteCharacteristic->readValue();
    Serial.print("The characteristic value was: ");
    Serial.println(value.c_str());
  }

  if (pRemoteCharacteristic->canNotify()) {
    // Register/Subscribe for notifications
    pRemoteCharacteristic->registerForNotify(notifyCallback);
  }

  connected = true;
  return true;
}
/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;

    }  // Found our server
  }    // onResult
};     // MyAdvertisedDeviceCallbacks

void setup() {
  // Setup Serial Port aut 115200 Baud
  startSerial(115200);

  // We will connect to the WiFi network
  Serial.print(F("Connecting to "));
  Serial.println(ssid);

  /* Explicitly set the ESP32 to be a WiFi-client, otherwise, it by default,
     would try to act as both a client and an access-point and could cause
     network-issues with your other WiFi-devices on your WiFi-network. */
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }

  Serial.println("");
  Serial.println(F("WiFi connected"));
  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());

  // MQTT
  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(callback);

  // Start BLE
  BLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
}  // End of setup.

// This is the Arduino main loop function.
void loop() {

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
    } else {
      Serial.println("We have failed to connect to the server; there is nothing more we will do.");
    }
    doConnect = false;
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected) {
    //String newValue = "Time since boot: " + String(millis() / 1000);
    //Serial.println("Setting new characteristic value to \"" + newValue + "\"");

    // Set the characteristic's value to be the array of bytes that is actually a string.
    // pRemoteCharacteristic->writeValue(newValue.c_str(), newValue.length());
  } else if (doScan) {
    BLEDevice::getScan()->start(0);  // this is just example to start scan after disconnect, most likely there is better way to do it in arduino
  }

  if (!mqtt.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    // Client connected
    mqtt.loop();
  }
  
  sendHRMData();
    sendMqttStats();

  delay(1000);  // Delay a second between loops.
}  // End of loop
