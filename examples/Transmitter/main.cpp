#include "ZHNetwork.h"

void onConfirmReceiving(); // Предварительное объявление необходимо для PlatformIO.

ZHNetwork myNet;

uint64_t messagelastTime{0};
uint16_t messageTimerDelay{5000};
const char *myNetName{"ZHNetwork"};
const uint8_t target[6]{0xA8, 0x48, 0xFA, 0xDC, 0xBB, 0xCD}; // Измените для отправки целевого сообщения.

void setup()
{
  Serial.begin(115200);
  myNet.begin(myNetName);
  myNet.setMaxNumberOfAttempts(3);                 // Опционально.
  myNet.setMaxWaitingTimeBetweenTransmissions(50); // Опционально.
  myNet.setMaxWaitingTimeForRoutingInfo(500);      // Опционально.
  myNet.setOnConfirmReceivingCallback(onConfirmReceiving);
  Serial.println();
  Serial.print("The node is up and running. MAC is ");
  Serial.print(myNet.getNodeMac());
  Serial.print(". Firmware version is ");
  Serial.println(myNet.getFirmwareVersion());
}

void loop()
{
  if ((millis() - messagelastTime) > messageTimerDelay)
  {
    myNet.sendBroadcastMessage("Hello world!");
    myNet.sendUnicastMessage("Hello world!", target);
    messagelastTime = millis();
  }
  myNet.maintenance();
}

void onConfirmReceiving()
{
  Serial.println("Message successfully sent");
}