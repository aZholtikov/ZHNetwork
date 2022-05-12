#include "ZHNetwork.h"

void onBroadcastReceiving(const char *data, const uint8_t *sender); // Предварительное объявление необходимо для PlatformIO.
void onUnicastReceiving(const char *data, const uint8_t *sender);   // Предварительное объявление необходимо для PlatformIO.

ZHNetwork myNet;

const char *myNetName{"ZHNetwork"};

void setup()
{
  Serial.begin(115200);
  myNet.begin(myNetName);
  myNet.setMaxNumberOfAttempts(3);                 // Опционально.
  myNet.setMaxWaitingTimeBetweenTransmissions(50); // Опционально.
  myNet.setOnBroadcastReceivingCallback(onBroadcastReceiving);
  myNet.setOnUnicastReceivingCallback(onUnicastReceiving);
  Serial.println();
  Serial.print("The node is up and running. MAC is ");
  Serial.print(myNet.getNodeMac());
  Serial.print(". Firmware version is ");
  Serial.println(myNet.getFirmwareVersion());
}

void loop()
{
  myNet.maintenance();
}

void onBroadcastReceiving(const char *data, const uint8_t *sender)
{
  Serial.print("Broadcast message from MAC ");
  Serial.print(myNet.macToString(sender));
  Serial.println(" received");
  Serial.print("Message: ");
  Serial.println(data);
}
void onUnicastReceiving(const char *data, const uint8_t *sender)
{
  Serial.print("Unicast message from MAC ");
  Serial.print(myNet.macToString(sender));
  Serial.println(" received");
  Serial.print("Message: ");
  Serial.println(data);
}