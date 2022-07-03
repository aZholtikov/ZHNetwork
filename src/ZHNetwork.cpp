#include "ZHNetwork.h"

struct TransmittedData
{
    byte messageType{0};
    uint16_t messageID{0};
    char netName[20]{0};
    byte originalTargetMAC[6]{0};
    byte originalSenderMAC[6]{0};
    char message[200]{0};
};

struct OutgoingData
{
    byte intermediateTargetMAC[6]{0};
    TransmittedData transmittedData;
};

struct IncomingData
{
    byte intermediateSenderMAC[6]{0};
    TransmittedData transmittedData;
};

struct RoutingTable
{
    byte originalTargetMAC[6]{0};
    byte intermediateTargetMAC[6]{0};
};

enum MessageType
{
    BROADCAST = 1,
    UNICAST,
};

std::vector<RoutingTable> routingVector;
std::queue<OutgoingData> queueForOutgoingData;
std::queue<OutgoingData> queueForRoutingVectorWaiting;
std::queue<IncomingData> queueForIncomingData;

const byte broadcastMAC[6]{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
char netName[20]{0};
byte localMAC[6]{0};
uint16_t lastMessageID[10]{0};
bool criticalProcessSemaphore{false};
bool sentMessageSemaphore{false};
bool confirmReceivingSemaphore{false};
bool confirmReceiving{false};

ZHNetwork &ZHNetwork::setOnBroadcastReceivingCallback(onMessage onBroadcastReceivingCallback)
{
    this->onBroadcastReceivingCallback = onBroadcastReceivingCallback;
    return *this;
}

ZHNetwork &ZHNetwork::setOnUnicastReceivingCallback(onMessage onUnicastReceivingCallback)
{
    this->onUnicastReceivingCallback = onUnicastReceivingCallback;
    return *this;
}

ZHNetwork &ZHNetwork::setOnConfirmReceivingCallback(onConfirm onConfirmReceivingCallback)
{
    this->onConfirmReceivingCallback = onConfirmReceivingCallback;
    return *this;
}

void ZHNetwork::begin(const char *name)
{
    os_memcpy(netName, name, sizeof(netName));
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    esp_now_init();
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    wifi_get_macaddr(STATION_IF, localMAC);
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataReceive);
}

bool ZHNetwork::begin(const char *name, const char *ssid, const char *password)
{
    os_memcpy(netName, name, sizeof(netName));
    String ssidApNamePrefix = "ESP-NOW GATEWAY ";
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.disconnect();
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        if (WiFi.status() == WL_NO_SSID_AVAIL || WiFi.status() == WL_CONNECT_FAILED)
            return false;
    }
    esp_now_init();
    wifi_get_macaddr(STATION_IF, localMAC);
    WiFi.softAP(ssidApNamePrefix + macToString(localMAC), macToString(localMAC), 1, true);
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataReceive);
    updateMode = true;
    ArduinoOTA.begin();
    return true;
}

void ZHNetwork::update()
{
    String ssidApNamePrefix = "ESP-NOW NODE ";
    IPAddress apIP(192, 168, 4, 1);
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    wifi_get_macaddr(STATION_IF, localMAC);
    WiFi.softAP(ssidApNamePrefix + macToString(localMAC));
    updateMode = true;
    ArduinoOTA.begin();
}

void IRAM_ATTR ZHNetwork::onDataSent(byte *mac, byte status)
{
    confirmReceivingSemaphore = true;
    confirmReceiving = status ? false : true;
}

void IRAM_ATTR ZHNetwork::onDataReceive(byte *mac, byte *data, byte length)
{
    if (criticalProcessSemaphore)
        return;
    criticalProcessSemaphore = true;
    if (length != sizeof(TransmittedData))
    {
        criticalProcessSemaphore = false;
        return;
    }
    IncomingData incomingData;
    os_memcpy(&incomingData.transmittedData, data, sizeof(TransmittedData));
    if (String(incomingData.transmittedData.netName) != String(netName) ||
        macToString(incomingData.transmittedData.originalSenderMAC) == macToString(localMAC))
    {
        criticalProcessSemaphore = false;
        return;
    }
    for (byte i{0}; i < sizeof(lastMessageID) / 2; ++i)
        if (lastMessageID[i] == incomingData.transmittedData.messageID)
        {
            criticalProcessSemaphore = false;
            return;
        }
    for (byte i{sizeof(lastMessageID) / 2 - 1}; i >= 1; --i)
        lastMessageID[i] = lastMessageID[i - 1];
    lastMessageID[0] = incomingData.transmittedData.messageID;
    os_memcpy(incomingData.intermediateSenderMAC, mac, 6);
    queueForIncomingData.push(incomingData);
    criticalProcessSemaphore = false;
}

void ZHNetwork::sendBroadcastMessage(const char *data)
{
    OutgoingData outgoingData;
    outgoingData.transmittedData.messageType = BROADCAST;
    outgoingData.transmittedData.messageID = ((uint16_t)ESP.random() << 8) | (uint16_t)ESP.random();
    os_memcpy(outgoingData.transmittedData.netName, netName, 20);
    os_memcpy(outgoingData.transmittedData.originalTargetMAC, broadcastMAC, 6);
    os_memcpy(outgoingData.transmittedData.originalSenderMAC, localMAC, 6);
    os_strcpy(outgoingData.transmittedData.message, data);
    os_memcpy(outgoingData.intermediateTargetMAC, broadcastMAC, 6);
    queueForOutgoingData.push(outgoingData);
}

void ZHNetwork::sendUnicastMessage(const char *data, const byte *target)
{
    OutgoingData outgoingData;
    outgoingData.transmittedData.messageType = UNICAST;
    outgoingData.transmittedData.messageID = ((uint16_t)ESP.random() << 8) | (uint16_t)ESP.random();
    os_memcpy(outgoingData.transmittedData.netName, netName, 20);
    os_memcpy(outgoingData.transmittedData.originalTargetMAC, target, 6);
    os_memcpy(outgoingData.transmittedData.originalSenderMAC, localMAC, 6);
    os_strcpy(outgoingData.transmittedData.message, data);
    for (uint16_t i{0}; i < routingVector.size(); ++i)
    {
        RoutingTable routingTable = routingVector[i];
        if (macToString(routingTable.originalTargetMAC) == macToString(target))
        {
            os_memcpy(outgoingData.intermediateTargetMAC, routingTable.intermediateTargetMAC, 6);
            queueForOutgoingData.push(outgoingData);
            return;
        }
    }
    os_memcpy(outgoingData.intermediateTargetMAC, target, 6);
    queueForOutgoingData.push(outgoingData);
}

void ZHNetwork::sendUnicastMessage(const char *data, const byte *target, const byte *sender)
{
    OutgoingData outgoingData;
    outgoingData.transmittedData.messageType = UNICAST;
    outgoingData.transmittedData.messageID = ((uint16_t)ESP.random() << 8) | (uint16_t)ESP.random();
    os_memcpy(outgoingData.transmittedData.netName, netName, 20);
    os_memcpy(outgoingData.transmittedData.originalTargetMAC, target, 6);
    os_memcpy(outgoingData.transmittedData.originalSenderMAC, sender, 6);
    os_strcpy(outgoingData.transmittedData.message, data);
    for (uint16_t i{0}; i < routingVector.size(); ++i)
    {
        RoutingTable routingTable = routingVector[i];
        if (macToString(routingTable.originalTargetMAC) == macToString(target))
        {
            os_memcpy(outgoingData.intermediateTargetMAC, routingTable.intermediateTargetMAC, 6);
            queueForOutgoingData.push(outgoingData);
            return;
        }
    }
    os_memcpy(outgoingData.intermediateTargetMAC, target, 6);
    queueForOutgoingData.push(outgoingData);
}

void ZHNetwork::sendSearchMessage(const byte *target)
{
    OutgoingData outgoingData;
    outgoingData.transmittedData.messageType = BROADCAST;
    outgoingData.transmittedData.messageID = ((uint16_t)ESP.random() << 8) | (uint16_t)ESP.random();
    os_memcpy(outgoingData.transmittedData.netName, netName, 20);
    os_memcpy(outgoingData.transmittedData.originalTargetMAC, target, 6);
    os_memcpy(outgoingData.transmittedData.originalSenderMAC, localMAC, 6);
    os_strcpy(outgoingData.transmittedData.message, "");
    os_memcpy(outgoingData.intermediateTargetMAC, broadcastMAC, 6);
    queueForOutgoingData.push(outgoingData);
    lastSearchMessageSentTime = millis();
}

void ZHNetwork::maintenance()
{
    if (updateMode)
        ArduinoOTA.handle();
    if (sentMessageSemaphore && confirmReceivingSemaphore)
    {
        sentMessageSemaphore = false;
        confirmReceivingSemaphore = false;
        if (confirmReceiving)
        {
            OutgoingData outgoingData = queueForOutgoingData.front();
            queueForOutgoingData.pop();
            if (onConfirmReceivingCallback && macToString(outgoingData.transmittedData.originalSenderMAC) == macToString(localMAC))
                onConfirmReceivingCallback();
        }
        else
        {
            if (numberOfAttemptsToSend < maxNumberOfAttempts)
                ++numberOfAttemptsToSend;
            else
            {
                OutgoingData outgoingData = queueForOutgoingData.front();
                if (outgoingData.transmittedData.messageType == UNICAST)
                {
                    for (uint16_t i{0}; i < routingVector.size(); ++i)
                    {
                        RoutingTable routingTable = routingVector[i];
                        if (macToString(routingTable.originalTargetMAC) == macToString(outgoingData.transmittedData.originalTargetMAC))
                        {
                            routingVector.erase(routingVector.begin() + i);
                        }
                    }
                    queueForRoutingVectorWaiting.push(outgoingData);
                    sendSearchMessage(outgoingData.transmittedData.originalTargetMAC);
                }
                queueForOutgoingData.pop();
                numberOfAttemptsToSend = 1;
            }
        }
    }
    if (!queueForOutgoingData.empty() && ((millis() - lastMessageSentTime) > maxWaitingTimeBetweenTransmissions))
    {
        OutgoingData outgoingData = queueForOutgoingData.front();
        esp_now_send(outgoingData.intermediateTargetMAC, (byte *)&outgoingData.transmittedData, sizeof(TransmittedData));
        lastMessageSentTime = millis();
        sentMessageSemaphore = true;
    }
    if (!queueForIncomingData.empty())
    {
        criticalProcessSemaphore = true;
        IncomingData incomingData = queueForIncomingData.front();
        queueForIncomingData.pop();
        criticalProcessSemaphore = false;
        if (incomingData.transmittedData.messageType == BROADCAST)
        {
            if (macToString(incomingData.transmittedData.originalTargetMAC) != macToString(localMAC))
            {
                OutgoingData outgoingData;
                os_memcpy(&outgoingData.transmittedData, &incomingData.transmittedData, sizeof(TransmittedData));
                os_memcpy(outgoingData.intermediateTargetMAC, broadcastMAC, 6);
                queueForOutgoingData.push(outgoingData);
            }
            bool flag{false};
            for (uint16_t i{0}; i < routingVector.size(); ++i)
            {
                RoutingTable routingTable = routingVector[i];
                if (macToString(routingTable.originalTargetMAC) == macToString(incomingData.transmittedData.originalSenderMAC))
                {
                    flag = true;
                    os_memcpy(routingTable.intermediateTargetMAC, incomingData.intermediateSenderMAC, 6);
                    routingVector.at(i) = routingTable;
                }
            }
            if (!flag)
            {
                RoutingTable routingTable;
                os_memcpy(routingTable.originalTargetMAC, incomingData.transmittedData.originalSenderMAC, 6);
                os_memcpy(routingTable.intermediateTargetMAC, incomingData.intermediateSenderMAC, 6);
                routingVector.push_back(routingTable);
            }
            if (macToString(incomingData.transmittedData.originalTargetMAC) == macToString(broadcastMAC) &&
                int(incomingData.transmittedData.message[0]) && onBroadcastReceivingCallback)
                onBroadcastReceivingCallback(incomingData.transmittedData.message, incomingData.transmittedData.originalSenderMAC);
            if (macToString(incomingData.transmittedData.originalTargetMAC) == macToString(localMAC))
                sendBroadcastMessage("");
        }
        if (incomingData.transmittedData.messageType == UNICAST)
        {
            if (macToString(incomingData.transmittedData.originalTargetMAC) == macToString(localMAC) && onUnicastReceivingCallback)
                onUnicastReceivingCallback(incomingData.transmittedData.message, incomingData.transmittedData.originalSenderMAC);
            else
                sendUnicastMessage(incomingData.transmittedData.message, incomingData.transmittedData.originalTargetMAC, incomingData.transmittedData.originalSenderMAC);
        }
    }
    if (!queueForRoutingVectorWaiting.empty())
    {
        OutgoingData outgoingData = queueForRoutingVectorWaiting.front();
        for (uint16_t i{0}; i < routingVector.size(); ++i)
        {
            RoutingTable routingTable = routingVector[i];
            if (macToString(routingTable.originalTargetMAC) == macToString(outgoingData.transmittedData.originalTargetMAC))
            {
                queueForRoutingVectorWaiting.pop();
                os_memcpy(outgoingData.intermediateTargetMAC, routingTable.intermediateTargetMAC, 6);
                queueForOutgoingData.push(outgoingData);
                return;
            }
        }
        if ((millis() - lastSearchMessageSentTime) > maxTimeForRoutingInfoWaiting)
            queueForRoutingVectorWaiting.pop();
    }
}

String ZHNetwork::macToString(const byte *mac)
{
    String string;
    const char baseChars[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    for (uint32_t i{0}; i < 6; ++i)
    {
        string += (char)pgm_read_byte(baseChars + (mac[i] >> 4));
        string += (char)pgm_read_byte(baseChars + mac[i] % 16);
    }
    return string;
}

byte *ZHNetwork::stringToMac(const String &string, byte *mac)
{
    const byte baseChars[75]{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0,
                             10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0, 0, 0, 0, 0, 0,
                             10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35};
    for (uint32_t i = 0; i < 6; ++i)
        mac[i] = (pgm_read_byte(baseChars + string.charAt(i * 2) - '0') << 4) + pgm_read_byte(baseChars + string.charAt(i * 2 + 1) - '0');
    return mac;
}

String ZHNetwork::getNodeMac()
{
    return macToString(localMAC);
}

String ZHNetwork::getFirmwareVersion()
{
    return firmware;
}

bool ZHNetwork::setMaxNumberOfAttempts(const byte number)
{
    if (number < 1 || number > 10)
        return false;
    maxNumberOfAttempts = number;
    return true;
}

bool ZHNetwork::setMaxWaitingTimeBetweenTransmissions(const byte time)
{
    if (time < 20 || time > 250)
        return false;
    maxWaitingTimeBetweenTransmissions = time;
    return true;
}

bool ZHNetwork::setMaxWaitingTimeForRoutingInfo(const uint16_t time)
{
    if (time < 500 || time > 5000)
        return false;
    maxTimeForRoutingInfoWaiting = time;
    return true;
}