#include "ZHNetwork.h"

void onConfirmReceiving(); // Предварительное объявление необходимо для PlatformIO.

ZHNetwork myNet;

unsigned long messagelastTime{0};
unsigned long messageTimerDelay{5000};
const char *myNetName{"ZHNetwork"};
const uint8_t target[6]{0x48, 0x55, 0x19, 0x12, 0xDE, 0xB2}; // Измените для отправки целевого сообщения.

void setup()
{
  Serial.begin(115200);
  myNet.begin(myNetName);
  myNet.setOnConfirmReceivingCallback(onConfirmReceiving);
  Serial.println();
  Serial.print("The node is up and running. MAC is ");
  Serial.println(myNet.getNodeMac());
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