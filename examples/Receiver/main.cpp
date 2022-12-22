#include "ZHNetwork.h"

void onBroadcastReceiving(const char *data, const uint8_t *sender);
void onUnicastReceiving(const char *data, const uint8_t *sender);

ZHNetwork myNet;

void setup()
{
  Serial.begin(115200);
  Serial.println();
  myNet.begin("ZHNetwork");
  myNet.setOnBroadcastReceivingCallback(onBroadcastReceiving);
  myNet.setOnUnicastReceivingCallback(onUnicastReceiving);
  Serial.print("MAC: ");
  Serial.print(myNet.getNodeMac());
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