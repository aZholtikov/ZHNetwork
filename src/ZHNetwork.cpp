#include "ZHNetwork.h"

struct TransmittedData
{
    char netName[10]{0};
    byte targetMAC[6]{0};
    byte senderMAC[6]{0};
    char message[200]{0};
};

char netName[10]{0};
byte localMAC[6]{0};
uint64_t lastMessageSentTime{0};
byte maxWaitingTimeBetweenTransmissions{50};
bool updateMode{false};
bool confirmReceiving{false};

std::queue<TransmittedData> queueForOutgoingData;
std::queue<TransmittedData> queueForIncomingData;

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
    memcpy(netName, name, sizeof(netName));
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
    memcpy(netName, name, sizeof(netName));
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
    confirmReceiving = status ? false : true;
}

void IRAM_ATTR ZHNetwork::onDataReceive(byte *mac, byte *data, byte length)
{
    if (length != sizeof(TransmittedData))
        return;
    TransmittedData incomingData;
    memcpy(&incomingData, data, sizeof(TransmittedData));
    queueForIncomingData.push(incomingData);
}

void ZHNetwork::sendBroadcastMessage(const char *data)
{
    TransmittedData outgoingData;
    memcpy(outgoingData.netName, netName, sizeof(netName));
    memcpy(outgoingData.targetMAC, broadcastMAC, 6);
    memcpy(outgoingData.senderMAC, localMAC, 6);
    strcpy(outgoingData.message, data);
    queueForOutgoingData.push(outgoingData);
}

void ZHNetwork::sendUnicastMessage(const char *data, const byte *target)
{
    TransmittedData outgoingData;
    memcpy(outgoingData.netName, netName, sizeof(netName));
    memcpy(outgoingData.targetMAC, target, 6);
    memcpy(outgoingData.senderMAC, localMAC, 6);
    strcpy(outgoingData.message, data);
    queueForOutgoingData.push(outgoingData);
}

void ZHNetwork::maintenance()
{
    if (updateMode)
        ArduinoOTA.handle();
    if (confirmReceiving && onConfirmReceivingCallback)
    {
        confirmReceiving = false;
        onConfirmReceivingCallback();
    }
    if (!queueForOutgoingData.empty() && ((millis() - lastMessageSentTime) > maxWaitingTimeBetweenTransmissions))
    {
        TransmittedData outgoingData = queueForOutgoingData.front();
        queueForOutgoingData.pop();
        esp_now_send(outgoingData.targetMAC, (byte *)&outgoingData, sizeof(TransmittedData));
        lastMessageSentTime = millis();
    }
    if (!queueForIncomingData.empty())
    {
        TransmittedData incomingData = queueForIncomingData.front();
        queueForIncomingData.pop();
        if (String(incomingData.netName) != String(netName))
            return;
        if (macToString(incomingData.targetMAC) == macToString(broadcastMAC) && onBroadcastReceivingCallback)
            onBroadcastReceivingCallback(incomingData.message, incomingData.senderMAC);
        if (macToString(incomingData.targetMAC) == macToString(localMAC) && onUnicastReceivingCallback)
            onUnicastReceivingCallback(incomingData.message, incomingData.senderMAC);
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