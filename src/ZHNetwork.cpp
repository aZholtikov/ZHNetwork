#include "ZHNetwork.h"

routing_vector_t routingVector;
incoming_queue_t queueForIncomingData;
outgoing_queue_t queueForOutgoingData;
waiting_queue_t queueForRoutingVectorWaiting;

const String firmware{"1.0"};
const uint8_t broadcastMAC[6]{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

bool criticalProcessSemaphore{false};
bool sentMessageSemaphore{false};
bool confirmReceivingSemaphore{false};
bool confirmReceiving{false};
uint8_t localMAC[6]{0};
uint8_t numberOfAttemptsToSend{1};
uint16_t lastMessageID[10]{0};
uint64_t lastMessageSentTime{0};

work_mode_t workMode_{ESP_NOW};
char netName_[20]{0};
char apSsid_[32]{"ESP-NOW NODE"};
char apPassword_[64]{0};
char staSsid_[32]{0};
char staPassword_[64]{0};
uint8_t maxNumberOfAttempts_{3};
uint8_t maxWaitingTimeBetweenTransmissions_{50};
uint16_t maxTimeForRoutingInfoWaiting_{500};

ZHNetwork &ZHNetwork::setOnBroadcastReceivingCallback(on_message_t onBroadcastReceivingCallback)
{
    this->onBroadcastReceivingCallback = onBroadcastReceivingCallback;
    return *this;
}

ZHNetwork &ZHNetwork::setOnUnicastReceivingCallback(on_message_t onUnicastReceivingCallback)
{
    this->onUnicastReceivingCallback = onUnicastReceivingCallback;
    return *this;
}

ZHNetwork &ZHNetwork::setOnConfirmReceivingCallback(on_confirm_t onConfirmReceivingCallback)
{
    this->onConfirmReceivingCallback = onConfirmReceivingCallback;
    return *this;
}

error_code_t ZHNetwork::setWorkMode(const work_mode_t workMode)
{
    if (workMode < ESP_NOW || workMode > ESP_NOW_STA)
        return ERROR;
    workMode_ = workMode;
    return SUCCESS;
}

work_mode_t ZHNetwork::getWorkMode()
{
    return workMode_;
}

error_code_t ZHNetwork::setNetName(const char *netName)
{
    if (strlen(netName) < 1 || strlen(netName) > 20)
        return ERROR;
    memset(&netName_, 0, strlen(netName));
    strcpy(netName_, netName);
    return SUCCESS;
}

String ZHNetwork::getNetName()
{
    return String(netName_);
}

error_code_t ZHNetwork::setStaSetting(const char *ssid, const char *password)
{
    if (strlen(ssid) < 1 || strlen(ssid) > 32 || strlen(password) > 64)
        return ERROR;
    memset(&staSsid_, 0, strlen(ssid));
    strcpy(staSsid_, ssid);
    memset(&staPassword_, 0, strlen(password));
    strcpy(staPassword_, password);
    return SUCCESS;
}

error_code_t ZHNetwork::setApSetting(const char *ssid, const char *password)
{
    if (strlen(ssid) < 1 || strlen(ssid) > 32 || strlen(password) < 8 || strlen(password) > 64)
        return ERROR;
    memset(&apSsid_, 0, strlen(ssid));
    strcpy(apSsid_, ssid);
    memset(&apPassword_, 0, strlen(password));
    strcpy(apPassword_, password);
    return SUCCESS;
}

error_code_t ZHNetwork::begin()
{
    randomSeed(analogRead(0));
#ifdef PRINT_LOG
    Serial.begin(115200);
#endif
    switch (workMode_)
    {
    case ESP_NOW:
        WiFi.mode(WIFI_STA);
        break;
    case ESP_NOW_AP:
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(apSsid_, apPassword_);
        break;
    case ESP_NOW_STA:
        WiFi.mode(WIFI_STA);
        WiFi.begin(staSsid_, staPassword_);
        while (WiFi.status() != WL_CONNECTED)
        {
            if (WiFi.status() == WL_NO_SSID_AVAIL || WiFi.status() == WL_CONNECT_FAILED)
                return ERROR;
            delay(500);
        }
        break;
    default:
        break;
    }
    esp_now_init();
#if defined(ESP8266)
    wifi_get_macaddr(STATION_IF, localMAC);
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
#endif
#if defined(ESP32)
    esp_wifi_get_mac((wifi_interface_t)ESP_IF_WIFI_STA, localMAC);
#endif
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataReceive);
    return SUCCESS;
}

void ZHNetwork::sendBroadcastMessage(const char *data)
{
    broadcastMessage(data, broadcastMAC, BROADCAST);
}

void ZHNetwork::sendUnicastMessage(const char *data, const uint8_t *target, const bool confirm)
{
    unicastMessage(data, target, localMAC, confirm ? UNICAST_WITH_CONFIRM : UNICAST);
}

void ZHNetwork::maintenance()
{
    if (sentMessageSemaphore && confirmReceivingSemaphore)
    {
        sentMessageSemaphore = false;
        confirmReceivingSemaphore = false;
        if (confirmReceiving)
        {
#ifdef PRINT_LOG
            Serial.println("OK.");
#endif
            outgoing_data_t outgoingData = queueForOutgoingData.front();
            queueForOutgoingData.pop();
#if defined(ESP32)
            esp_now_del_peer(outgoingData.intermediateTargetMAC);
#endif
            if (onConfirmReceivingCallback && macToString(outgoingData.transmittedData.originalSenderMAC) == macToString(localMAC) && outgoingData.transmittedData.messageType == BROADCAST)
                onConfirmReceivingCallback(outgoingData.transmittedData.originalTargetMAC, true);
        }
        else
        {
#ifdef PRINT_LOG
            Serial.println("FAULT.");
#endif
            if (numberOfAttemptsToSend < maxNumberOfAttempts_)
                ++numberOfAttemptsToSend;
            else
            {
                outgoing_data_t outgoingData = queueForOutgoingData.front();
                queueForOutgoingData.pop();
#if defined(ESP32)
                esp_now_del_peer(outgoingData.intermediateTargetMAC);
#endif
                numberOfAttemptsToSend = 1;
                for (uint16_t i{0}; i < routingVector.size(); ++i)
                {
                    routing_table_t routingTable = routingVector[i];
                    if (macToString(routingTable.originalTargetMAC) == macToString(outgoingData.transmittedData.originalTargetMAC))
                    {
                        routingVector.erase(routingVector.begin() + i);
#ifdef PRINT_LOG
                        Serial.print("CHECKING ROUTING TABLE... Routing to MAC ");
                        Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
                        Serial.println(" deleted.");
#endif
                    }
                }
                waiting_data_t waitingData;
                esp_memset(&waitingData, 0, sizeof(waiting_data_t));
                waitingData.time = millis();
                memcpy(&waitingData.intermediateTargetMAC, &outgoingData.intermediateTargetMAC, 6);
                memcpy(&waitingData.transmittedData, &outgoingData.transmittedData, sizeof(transmitted_data_t));
                queueForRoutingVectorWaiting.push(waitingData);
                broadcastMessage("", outgoingData.transmittedData.originalTargetMAC, SEARCH_REQUEST);
            }
        }
    }
    if (!queueForOutgoingData.empty() && ((millis() - lastMessageSentTime) > maxWaitingTimeBetweenTransmissions_))
    {
        outgoing_data_t outgoingData = queueForOutgoingData.front();
#if defined(ESP32)
        esp_now_peer_info_t peerInfo;
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, outgoingData.intermediateTargetMAC, 6);
        peerInfo.channel = 1;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
#endif
        esp_now_send(outgoingData.intermediateTargetMAC, (uint8_t *)&outgoingData.transmittedData, sizeof(transmitted_data_t));
        lastMessageSentTime = millis();
        sentMessageSemaphore = true;
#ifdef PRINT_LOG
        switch (outgoingData.transmittedData.messageType)
        {
        case BROADCAST:
            Serial.print("BROADCAST");
            break;
        case UNICAST:
            Serial.print("UNICAST");
            break;
        case UNICAST_WITH_CONFIRM:
            Serial.print("UNICAST_WITH_CONFIRM");
            break;
        case DELIVERY_CONFIRM_RESPONSE:
            Serial.print("DELIVERY_CONFIRM_RESPONSE");
            break;
        case SEARCH_REQUEST:
            Serial.print("SEARCH_REQUEST");
            break;
        case SEARCH_RESPONSE:
            Serial.print("SEARCH_RESPONSE");
            break;
        default:
            break;
        }
        Serial.print(" message from MAC ");
        Serial.print(macToString(outgoingData.transmittedData.originalSenderMAC));
        Serial.print(" to MAC ");
        Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
        Serial.print(" via MAC ");
        Serial.print(macToString(outgoingData.intermediateTargetMAC));
        Serial.print(" sended. Status ");
#endif
    }
    if (!queueForIncomingData.empty())
    {
        criticalProcessSemaphore = true;
        incoming_data_t incomingData = queueForIncomingData.front();
        queueForIncomingData.pop();
        criticalProcessSemaphore = false;
        bool forward{false};
        bool routingUpdate{false};
        switch (incomingData.transmittedData.messageType)
        {
        case BROADCAST:
#ifdef PRINT_LOG
            Serial.print("BROADCAST message from MAC ");
            Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
            Serial.println(" received.");
#endif
            if (onBroadcastReceivingCallback)
                onBroadcastReceivingCallback(incomingData.transmittedData.message, incomingData.transmittedData.originalSenderMAC);
            forward = true;
            break;
        case UNICAST:
#ifdef PRINT_LOG
            Serial.print("UNICAST message from MAC ");
            Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
            Serial.print(" to MAC ");
            Serial.print(macToString(incomingData.transmittedData.originalTargetMAC));
            Serial.print(" via MAC ");
            Serial.print(macToString(incomingData.intermediateSenderMAC));
            Serial.println(" received.");
#endif
            if (macToString(incomingData.transmittedData.originalTargetMAC) == macToString(localMAC))
            {
                if (onUnicastReceivingCallback)
                    onUnicastReceivingCallback(incomingData.transmittedData.message, incomingData.transmittedData.originalSenderMAC);
            }
            else
                unicastMessage(incomingData.transmittedData.message, incomingData.transmittedData.originalTargetMAC, incomingData.transmittedData.originalSenderMAC, UNICAST);
            break;
        case UNICAST_WITH_CONFIRM:
#ifdef PRINT_LOG
            Serial.print("UNICAST_WITH_CONFIRM message from MAC ");
            Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
            Serial.print(" to MAC ");
            Serial.print(macToString(incomingData.transmittedData.originalTargetMAC));
            Serial.print(" via MAC ");
            Serial.print(macToString(incomingData.intermediateSenderMAC));
            Serial.println(" received.");
#endif
            if (macToString(incomingData.transmittedData.originalTargetMAC) == macToString(localMAC))
            {
                if (onUnicastReceivingCallback)
                    onUnicastReceivingCallback(incomingData.transmittedData.message, incomingData.transmittedData.originalSenderMAC);
                unicastMessage("", incomingData.transmittedData.originalSenderMAC, localMAC, DELIVERY_CONFIRM_RESPONSE);
            }
            else
                unicastMessage(incomingData.transmittedData.message, incomingData.transmittedData.originalTargetMAC, incomingData.transmittedData.originalSenderMAC, UNICAST_WITH_CONFIRM);
            break;
        case DELIVERY_CONFIRM_RESPONSE:
#ifdef PRINT_LOG
            Serial.print("DELIVERY_CONFIRM_RESPONSE message from MAC ");
            Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
            Serial.print(" to MAC ");
            Serial.print(macToString(incomingData.transmittedData.originalTargetMAC));
            Serial.print(" via MAC ");
            Serial.print(macToString(incomingData.intermediateSenderMAC));
            Serial.println(" received.");
#endif
            if (macToString(incomingData.transmittedData.originalTargetMAC) == macToString(localMAC))
            {
                if (onConfirmReceivingCallback)
                    onConfirmReceivingCallback(incomingData.transmittedData.originalSenderMAC, true);
            }
            else
                unicastMessage(incomingData.transmittedData.message, incomingData.transmittedData.originalTargetMAC, incomingData.transmittedData.originalSenderMAC, DELIVERY_CONFIRM_RESPONSE);
            break;
        case SEARCH_REQUEST:
#ifdef PRINT_LOG
            Serial.print("SEARCH_REQUEST message from MAC ");
            Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
            Serial.print(" to MAC ");
            Serial.print(macToString(incomingData.transmittedData.originalTargetMAC));
            Serial.println(" received.");
#endif
            if (macToString(incomingData.transmittedData.originalTargetMAC) == macToString(localMAC))
                broadcastMessage("", incomingData.transmittedData.originalSenderMAC, SEARCH_RESPONSE);
            else
                forward = true;
            routingUpdate = true;
            break;
        case SEARCH_RESPONSE:
#ifdef PRINT_LOG
            Serial.print("SEARCH_RESPONSE message from MAC ");
            Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
            Serial.print(" to MAC ");
            Serial.print(macToString(incomingData.transmittedData.originalTargetMAC));
            Serial.println(" received.");
#endif
            if (macToString(incomingData.transmittedData.originalTargetMAC) != macToString(localMAC))
                forward = true;
            routingUpdate = true;
            break;
        default:
            break;
        }
        if (forward)
        {
            outgoing_data_t outgoingData;
            memcpy(&outgoingData.transmittedData, &incomingData.transmittedData, sizeof(transmitted_data_t));
            memcpy(&outgoingData.intermediateTargetMAC, &broadcastMAC, 6);
            queueForOutgoingData.push(outgoingData);
            delay(random(10));
        }
        if (routingUpdate)
        {
            bool routeFound{false};
            for (uint16_t i{0}; i < routingVector.size(); ++i)
            {
                routing_table_t routingTable = routingVector[i];
                if (macToString(routingTable.originalTargetMAC) == macToString(incomingData.transmittedData.originalSenderMAC))
                {
                    routeFound = true;
                    if (macToString(routingTable.intermediateTargetMAC) != macToString(incomingData.intermediateSenderMAC))
                    {
                        memcpy(&routingTable.intermediateTargetMAC, &incomingData.intermediateSenderMAC, 6);
                        routingVector.at(i) = routingTable;
#ifdef PRINT_LOG
                        Serial.print("CHECKING ROUTING TABLE... Routing to MAC ");
                        Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
                        Serial.print(" updated. Target is ");
                        Serial.print(macToString(incomingData.intermediateSenderMAC));
                        Serial.println(".");
#endif
                    }
                }
            }
            if (!routeFound)
            {
                if (macToString(incomingData.transmittedData.originalSenderMAC) != macToString(incomingData.intermediateSenderMAC))
                {
                    routing_table_t routingTable;
                    memcpy(&routingTable.originalTargetMAC, &incomingData.transmittedData.originalSenderMAC, 6);
                    memcpy(&routingTable.intermediateTargetMAC, &incomingData.intermediateSenderMAC, 6);
                    routingVector.push_back(routingTable);
#ifdef PRINT_LOG
                    Serial.print("CHECKING ROUTING TABLE... Routing to MAC ");
                    Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
                    Serial.print(" added. Target is ");
                    Serial.print(macToString(incomingData.intermediateSenderMAC));
                    Serial.println(".");
#endif
                }
            }
        }
    }
    if (!queueForRoutingVectorWaiting.empty())
    {
        waiting_data_t waitingData = queueForRoutingVectorWaiting.front();
        for (uint16_t i{0}; i < routingVector.size(); ++i)
        {
            routing_table_t routingTable = routingVector[i];
            if (macToString(routingTable.originalTargetMAC) == macToString(waitingData.transmittedData.originalTargetMAC))
            {
                queueForRoutingVectorWaiting.pop();
                outgoing_data_t outgoingData;
                esp_memset(&outgoingData, 0, sizeof(outgoing_data_t));
                memcpy(&outgoingData.transmittedData, &waitingData.transmittedData, sizeof(transmitted_data_t));
                memcpy(&outgoingData.intermediateTargetMAC, &routingTable.intermediateTargetMAC, 6);
                queueForOutgoingData.push(outgoingData);
#ifdef PRINT_LOG
                Serial.print("CHECKING ROUTING TABLE... Routing to MAC ");
                Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
                Serial.print(" found. Target is ");
                Serial.print(macToString(outgoingData.intermediateTargetMAC));
                Serial.println(".");
#endif
                return;
            }
        }
        if ((millis() - waitingData.time) > maxTimeForRoutingInfoWaiting_)
        {
            queueForRoutingVectorWaiting.pop();
#ifdef PRINT_LOG
            Serial.print("CHECKING ROUTING TABLE... Routing to MAC ");
            Serial.print(macToString(waitingData.transmittedData.originalTargetMAC));
            Serial.println(" not found.");
            switch (waitingData.transmittedData.messageType)
            {
            case UNICAST:
                Serial.print("UNICAST");
                break;
            case UNICAST_WITH_CONFIRM:
                Serial.print("UNICAST_WITH_CONFIRM");
                break;
            case DELIVERY_CONFIRM_RESPONSE:
                Serial.print("DELIVERY_CONFIRM_RESPONSE");
                break;
            default:
                break;
            }
            Serial.print(" message from MAC ");
            Serial.print(macToString(waitingData.transmittedData.originalSenderMAC));
            Serial.print(" to MAC ");
            Serial.print(macToString(waitingData.transmittedData.originalTargetMAC));
            Serial.print(" via MAC ");
            Serial.print(macToString(waitingData.intermediateTargetMAC));
            Serial.println(" undelivered.");
#endif
            if (waitingData.transmittedData.messageType == UNICAST_WITH_CONFIRM && macToString(waitingData.transmittedData.originalSenderMAC) == macToString(localMAC))
                if (onConfirmReceivingCallback)
                    onConfirmReceivingCallback(waitingData.transmittedData.originalTargetMAC, false);
        }
    }
}

String ZHNetwork::getNodeMac()
{
    return macToString(localMAC);
}

IPAddress ZHNetwork::getNodeIp()
{
    if (workMode_ == ESP_NOW_AP)
        return WiFi.softAPIP();
    if (workMode_ == ESP_NOW_STA)
        return WiFi.localIP();
    return IPAddress(0, 0, 0, 0);
}

String ZHNetwork::getFirmwareVersion()
{
    return firmware;
}

String ZHNetwork::macToString(const uint8_t *mac)
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

uint8_t *ZHNetwork::stringToMac(const String &string, uint8_t *mac)
{
    const uint8_t baseChars[75]{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0,
                                10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0, 0, 0, 0, 0, 0,
                                10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35};
    for (uint32_t i = 0; i < 6; ++i)
        mac[i] = (pgm_read_byte(baseChars + string.charAt(i * 2) - '0') << 4) + pgm_read_byte(baseChars + string.charAt(i * 2 + 1) - '0');
    return mac;
}

error_code_t ZHNetwork::setMaxNumberOfAttempts(const uint8_t maxNumberOfAttempts)
{
    if (maxNumberOfAttempts < 1 || maxNumberOfAttempts > 10)
        return ERROR;
    maxNumberOfAttempts_ = maxNumberOfAttempts;
    return SUCCESS;
}

uint8_t ZHNetwork::getMaxNumberOfAttempts()
{
    return maxNumberOfAttempts_;
}

error_code_t ZHNetwork::setMaxWaitingTimeBetweenTransmissions(const uint8_t maxWaitingTimeBetweenTransmissions)
{
    if (maxWaitingTimeBetweenTransmissions < 50 || maxWaitingTimeBetweenTransmissions > 250)
        return ERROR;
    maxWaitingTimeBetweenTransmissions_ = maxWaitingTimeBetweenTransmissions;
    return SUCCESS;
}

uint8_t ZHNetwork::getMaxWaitingTimeBetweenTransmissions()
{
    return maxWaitingTimeBetweenTransmissions_;
}

error_code_t ZHNetwork::setMaxWaitingTimeForRoutingInfo(const uint16_t maxTimeForRoutingInfoWaiting)
{
    if (maxTimeForRoutingInfoWaiting < 500 || maxTimeForRoutingInfoWaiting > 5000)
        return ERROR;
    maxTimeForRoutingInfoWaiting_ = maxTimeForRoutingInfoWaiting;
    return SUCCESS;
}

uint16_t ZHNetwork::getMaxWaitingTimeForRoutingInfo()
{
    return maxTimeForRoutingInfoWaiting_;
}

#if defined(ESP8266)
void IRAM_ATTR ZHNetwork::onDataSent(uint8_t *mac, uint8_t status)
#endif
#if defined(ESP32)
    void IRAM_ATTR ZHNetwork::onDataSent(const uint8_t *mac, esp_now_send_status_t status)
#endif
{
    confirmReceivingSemaphore = true;
    confirmReceiving = status ? false : true;
}

#if defined(ESP8266)
void IRAM_ATTR ZHNetwork::onDataReceive(uint8_t *mac, uint8_t *data, uint8_t length)
#endif
#if defined(ESP32)
    void IRAM_ATTR ZHNetwork::onDataReceive(const uint8_t *mac, const uint8_t *data, int length)
#endif
{
    if (criticalProcessSemaphore)
        return;
    criticalProcessSemaphore = true;
    if (length != sizeof(transmitted_data_t))
    {
        criticalProcessSemaphore = false;
        return;
    }
    incoming_data_t incomingData;
    memcpy(&incomingData.transmittedData, data, sizeof(transmitted_data_t));
    if (macToString(incomingData.transmittedData.originalSenderMAC) == macToString(localMAC))
    {
        criticalProcessSemaphore = false;
        return;
    }
    if (String(netName_) != "")
    {
        if (String(incomingData.transmittedData.netName) != String(netName_))
        {
            criticalProcessSemaphore = false;
            return;
        }
    }
    for (uint8_t i{0}; i < sizeof(lastMessageID) / 2; ++i)
        if (lastMessageID[i] == incomingData.transmittedData.messageID)
        {
            criticalProcessSemaphore = false;
            return;
        }
    for (uint8_t i{sizeof(lastMessageID) / 2 - 1}; i >= 1; --i)
        lastMessageID[i] = lastMessageID[i - 1];
    lastMessageID[0] = incomingData.transmittedData.messageID;
    memcpy(&incomingData.intermediateSenderMAC, mac, 6);
    queueForIncomingData.push(incomingData);
    criticalProcessSemaphore = false;
}

void ZHNetwork::broadcastMessage(const char *data, const uint8_t *target, message_type_t type)
{
    outgoing_data_t outgoingData;
    esp_memset(&outgoingData, 0, sizeof(outgoing_data_t));
    outgoingData.transmittedData.messageType = type;
    outgoingData.transmittedData.messageID = ((uint16_t)random(32767) << 8) | (uint16_t)random(32767);
    memcpy(&outgoingData.transmittedData.netName, &netName_, 20);
    memcpy(&outgoingData.transmittedData.originalTargetMAC, target, 6);
    memcpy(&outgoingData.transmittedData.originalSenderMAC, &localMAC, 6);
    strcpy(outgoingData.transmittedData.message, data);
    memcpy(&outgoingData.intermediateTargetMAC, &broadcastMAC, 6);
    queueForOutgoingData.push(outgoingData);
#ifdef PRINT_LOG
    switch (outgoingData.transmittedData.messageType)
    {
    case BROADCAST:
        Serial.print("BROADCAST");
        break;
    case SEARCH_REQUEST:
        Serial.print("SEARCH_REQUEST");
        break;
    case SEARCH_RESPONSE:
        Serial.print("SEARCH_RESPONSE");
        break;
    default:
        break;
    }
    Serial.print(" message from MAC ");
    Serial.print(macToString(outgoingData.transmittedData.originalSenderMAC));
    Serial.print(" to MAC ");
    Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
    Serial.println(" added to queue.");
#endif
}

void ZHNetwork::unicastMessage(const char *data, const uint8_t *target, const uint8_t *sender, message_type_t type)
{
    outgoing_data_t outgoingData;
    esp_memset(&outgoingData, 0, sizeof(outgoing_data_t));
    outgoingData.transmittedData.messageType = type;
    outgoingData.transmittedData.messageID = ((uint16_t)random(32767) << 8) | (uint16_t)random(32767);
    memcpy(&outgoingData.transmittedData.netName, &netName_, 20);
    memcpy(&outgoingData.transmittedData.originalTargetMAC, target, 6);
    memcpy(&outgoingData.transmittedData.originalSenderMAC, sender, 6);
    strcpy(outgoingData.transmittedData.message, data);
    for (uint16_t i{0}; i < routingVector.size(); ++i)
    {
        routing_table_t routingTable = routingVector[i];
        if (macToString(routingTable.originalTargetMAC) == macToString(target))
        {
            memcpy(&outgoingData.intermediateTargetMAC, &routingTable.intermediateTargetMAC, 6);
            queueForOutgoingData.push(outgoingData);
#ifdef PRINT_LOG
            Serial.print("CHECKING ROUTING TABLE... Routing to MAC ");
            Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
            Serial.print(" found. Target is ");
            Serial.print(macToString(outgoingData.intermediateTargetMAC));
            Serial.println(".");
            switch (outgoingData.transmittedData.messageType)
            {
            case UNICAST:
                Serial.print("UNICAST");
                break;
            case UNICAST_WITH_CONFIRM:
                Serial.print("UNICAST_WITH_CONFIRM");
                break;
            case DELIVERY_CONFIRM_RESPONSE:
                Serial.print("DELIVERY_CONFIRM_RESPONSE");
                break;
            default:
                break;
            }
            Serial.print(" message from MAC ");
            Serial.print(macToString(outgoingData.transmittedData.originalSenderMAC));
            Serial.print(" to MAC ");
            Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
            Serial.print(" via MAC ");
            Serial.print(macToString(outgoingData.intermediateTargetMAC));
            Serial.println(" added to queue.");
#endif
            return;
        }
    }
    memcpy(&outgoingData.intermediateTargetMAC, target, 6);
    queueForOutgoingData.push(outgoingData);
#ifdef PRINT_LOG
    Serial.print("CHECKING ROUTING TABLE... Routing to MAC ");
    Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
    Serial.print(" not found. Target is ");
    Serial.print(macToString(outgoingData.intermediateTargetMAC));
    Serial.println(".");
    switch (outgoingData.transmittedData.messageType)
    {
    case UNICAST:
        Serial.print("UNICAST");
        break;
    case UNICAST_WITH_CONFIRM:
        Serial.print("UNICAST_WITH_CONFIRM");
        break;
    case DELIVERY_CONFIRM_RESPONSE:
        Serial.print("DELIVERY_CONFIRM_RESPONSE");
        break;
    default:
        break;
    }
    Serial.print(" message from MAC ");
    Serial.print(macToString(outgoingData.transmittedData.originalSenderMAC));
    Serial.print(" to MAC ");
    Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
    Serial.print(" via MAC ");
    Serial.print(macToString(outgoingData.intermediateTargetMAC));
    Serial.println(" added to queue.");
#endif
}