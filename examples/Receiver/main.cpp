#include "ZHNetwork.h"

void onBroadcastReceiving(const char *data, const uint8_t *sender);
void onUnicastReceiving(const char *data, const uint8_t *sender);

ZHNetwork myNet;

void setup()
{
  Serial.begin(115200);
  Serial.println();
  // *** ESP-NOW mode only.
  myNet.setWorkMode(ESP_NOW);
  // *** Or ESP-NOW + access point mode.
  // myNet.setWorkMode(ESP_NOW_AP);
  // myNet.setApSetting("ESP NODE TEST", "12345678");
  // *** Or ESP-NOW + connect to your router mode.
  // myNet.setWorkMode(ESP_NOW_STA);
  // myNet.setStaSetting("SSID", "PASSWORD");
  // ***
  myNet.setNetName("ZHNetwork");                   // Optional.
  myNet.setMaxNumberOfAttempts(3);                 // Optional.
  myNet.setMaxWaitingTimeBetweenTransmissions(50); // Optional.
  myNet.setMaxWaitingTimeForRoutingInfo(500);      // Optional.
  myNet.begin();
  myNet.setOnBroadcastReceivingCallback(onBroadcastReceiving);
  myNet.setOnUnicastReceivingCallback(onUnicastReceiving);
  Serial.print("MAC: ");
  Serial.print(myNet.getNodeMac());
  Serial.print(". IP: ");
  Serial.print(myNet.getNodeIp());
  Serial.print(". Firmware version: ");
  Serial.print(myNet.getFirmwareVersion());
  Serial.println(".");
}

void loop()
{
  myNet.maintenance();
}

void onBroadcastReceiving(const char *data, const uint8_t *sender)
{
  Serial.print("Broadcast message from MAC ");
  Serial.print(myNet.macToString(sender));
  Serial.println(" received.");
  Serial.print("Message: ");
  Serial.println(data);
}
void onUnicastReceiving(const char *data, const uint8_t *sender)
{
  Serial.print("Unicast message from MAC ");
  Serial.print(myNet.macToString(sender));
  Serial.println(" received.");
  Serial.print("Message: ");
  Serial.println(data);
}