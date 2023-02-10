# ESP-NOW based Mesh network for ESP8266/ESP32

A simple library for creating ESP-NOW based Mesh network for ESP8266/ESP32.

## Features

1. The maximum size of transmitted data is 200 bytes.
2. Encrypted and unencrypted messages. Simple XOR crypting.
3. All nodes are not visible to the network scanner.
4. Not required a pre-pairings for data transfer.
5. Broadcast or unicast data transmissions.
6. There are no periodic/synchronous messages on the network. All devices are in "silent mode" and do not "hum" into the air.
7. Each node has its own independent routing table, updated only as needed.
8. Each node will receive/send a message if it "sees" at least one device on the network.
9. The number of devices on the network and the area of use is not limited (hypothetically). :-)

## Testing

1. Program 2 receivers and 1 transmitter (specify the MAC of the 1st receiver in the transmitter code).
2. Connect the 1st receiver to the computer. Switch on serial port monitor. Turn transmitter on. Receiver will start receiving data.
3. Move transmitter as far away from receiver as possible until receiver is able to receive data (reduce tx power and shield module if necessary).
4. Turn on the 2nd receiver and place it between the 1st receiver and transmitter (preferably in the middle). The 1st receiver will resume data reception (with relaying through the 2nd receiver). P.S. You can use a transmitter instead of the 2nd receiver - makes no difference.
5. Voila. ;-)
6. P.S. Uncomment #define PRINT_LOG in ZHNetwork.h for display to serial port the full operation log.

## Notes

1. Possibility uses WiFi AP or STA modes at the same time with ESP-NOW using the standard libraries.
2. For correct work at ESP-NOW + STA mode your WiFi router must be set on channel 1 and set gateway mode.

## Function descriptions

### Sets the callback function for processing a received broadcast message

Note. Possibility uses one callback function for recieved unicast and broadcast messages.

```cpp
myNet.setOnBroadcastReceivingCallback(onBroadcastReceiving);
void onBroadcastReceiving(const char *data, const uint8_t *sender)
{
    // Do something when receiving a broadcast message.
}
```

### Sets the callback function for processing a received unicast message

Note. Possibility uses one callback function for recieved unicast and broadcast messages.

```cpp
myNet.setOnUnicastReceivingCallback(onUnicastReceiving);
void onUnicastReceiving(const char *data, const uint8_t *sender)
{
    // Do something when receiving a unicast message.
}
```

### Sets the callback function for processing a received delivery/undelivery confirm message

Note. Called only at broadcast or unicast with confirm message. Status will always true at sending broadcast message.

```cpp
myNet.setOnConfirmReceivingCallback(onConfirmReceiving);
void onConfirmReceiving(const uint8_t *target, const uint16_t id, const bool status)
{
    // Do something when receiving a delivery/undelivery confirm message.
}
```

### ESP-NOW Mesh network initialization

1-20 characters.

Note. If network name not set node will work with all ESP-NOW networks. If set node will work with only one network.

```cpp
myNet.begin("ZHNetwork");
myNet.begin("ZHNetwork", true); // Gateway mode.
```

### Sends broadcast message to all nodes

Returns message ID.

```cpp
myNet.sendBroadcastMessage("Hello world!");
```

### Sends unicast message to node

Returns message ID.

```cpp
myNet.sendUnicastMessage("Hello world!", target); // Without confirm.
myNet.sendUnicastMessage("Hello world!", target, true); // With confirm.
```

### System processing

Attention! Must be uncluded in loop.

```cpp
myNet.maintenance();
```

### Gets node MAC adress

```cpp
myNet.getNodeMac();
```

### Gets version of this library

```cpp
myNet.getFirmwareVersion();
```

### Converts MAC adress to string

```cpp
myNet.macToString(mac);
```

### Converts string to MAC adress

```cpp
uint8_t mac[6]
myNet.stringToMac(string, mac);
```

### Sets crypt key

1-20 characters.

```cpp
myNet.setCryptKey("VERY_LONG_CRYPT_KEY"); 
```

### Sets max number of attempts to send message

1-10. 3 default value.

```cpp
myNet.setMaxNumberOfAttempts(3); 
```

### Gets max number of attempts to send message

```cpp
myNet.getMaxNumberOfAttempts(); 
```

### Sets max waiting time between transmissions

50-250 ms. 50 default value.

```cpp
myNet.setMaxWaitingTimeBetweenTransmissions(50);
```

### Gets max waiting time between transmissions

```cpp
myNet.getMaxWaitingTimeBetweenTransmissions();
```

### Sets max waiting time for routing info

500-5000 ms. 500 default value.

```cpp
myNet.setMaxWaitingTimeForRoutingInfo(500); 
```

### Gets max waiting time for routing info

```cpp
myNet.getMaxWaitingTimeForRoutingInfo(); 
```

## Example

```cpp
#include "ZHNetwork.h"

void onBroadcastReceiving(const char *data, const uint8_t *sender);
void onUnicastReceiving(const char *data, const uint8_t *sender);
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
  myNet.setCryptKey("VERY_LONG_CRYPT_KEY");
  myNet.setOnBroadcastReceivingCallback(onBroadcastReceiving);
  myNet.setOnUnicastReceivingCallback(onUnicastReceiving);
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

void onConfirmReceiving(const uint8_t *target, const uint16_t id, const bool status)
{
  Serial.print("Message to MAC ");
  Serial.print(myNet.macToString(target));
  Serial.print(" ID ");
  Serial.print(id);
  Serial.println(status ? " delivered." : " undelivered.");
}
```

Any feedback via [e-mail](mailto:github@zh.com.ru) would be appreciated. Or... [Buy me a coffee](https://paypal.me/aZholtikov).
