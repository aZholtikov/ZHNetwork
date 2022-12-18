#include "ZHNetwork.h"

void onConfirmReceiving(const uint8_t *target, const bool status);

ZHNetwork myNet;

uint64_t messageLastTime{0};
uint16_t messageTimerDelay{5000};
const uint8_t target[6]{0xA8, 0x48, 0xFA, 0xDC, 0x5B, 0xFA};

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
  myNet.setOnConfirmReceivingCallback(onConfirmReceiving);
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
    Serial.println(" sended.");
    myNet.sendUnicastMessage("Hello world!", target, true);
    messageLastTime = millis();
  }
  myNet.maintenance();
}

void onConfirmReceiving(const uint8_t *target, const bool status)
{
  Serial.print("Message to MAC ");
  Serial.print(myNet.macToString(target));
  Serial.println(status ? " delivered." : " undelivered.");
}