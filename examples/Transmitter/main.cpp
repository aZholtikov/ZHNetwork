#include "ZHNetwork.h"

void onConfirmReceiving(const uint8_t *target, const uint16_t id, const bool status);

ZHNetwork myNet;

uint64_t messageLastTime{0};
uint16_t messageTimerDelay{5000};
const uint8_t target[6]{0xA8, 0x48, 0xFA, 0xDC, 0x5B, 0xFA};

void setup()
{
  Serial.begin(115200);
  Serial.println();
  myNet.begin("ZHNetwork");
  // myNet.setCryptKey("VERY_LONG_CRYPT_KEY"); // If encryption is used, the key must be set same of all another ESP-NOW devices in network.
  myNet.setOnConfirmReceivingCallback(onConfirmReceiving);
  Serial.print("MAC: ");
  Serial.print(myNet.getNodeMac());
  Serial.print(". Firmware version: ");
  Serial.print(myNet.getFirmwareVersion());
  Serial.println(".");
}

void loop()
{
  if ((millis() - messageLastTime) > messageTimerDelay)
  {
    Serial.println("Broadcast message sended.");
    myNet.sendBroadcastMessage("Hello world!");

    Serial.print("Unicast message to MAC ");
    Serial.print(myNet.macToString(target));
    Serial.println(" sended.");
    myNet.sendUnicastMessage("Hello world!", target);

    Serial.print("Unicast with confirm message to MAC ");
    Serial.print(myNet.macToString(target));
    Serial.print(" ID ");
    Serial.print(myNet.sendUnicastMessage("Hello world!", target, true));
    Serial.println(" sended.");

    messageLastTime = millis();
  }
  myNet.maintenance();
}

void onConfirmReceiving(const uint8_t *target, const uint16_t id, const bool status)
{
  Serial.print("Message to MAC ");
  Serial.print(myNet.macToString(target));
  Serial.print(" ID ");
  Serial.print(id);
  Serial.println(status ? " delivered." : " undelivered.");
}