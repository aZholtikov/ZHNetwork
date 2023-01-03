#include "ZHNetwork.h"

routing_vector_t ZHNetwork::routingVector;
incoming_queue_t ZHNetwork::queueForIncomingData;
outgoing_queue_t ZHNetwork::queueForOutgoingData;
waiting_queue_t ZHNetwork::queueForRoutingVectorWaiting;

bool ZHNetwork::criticalProcessSemaphore{false};
bool ZHNetwork::sentMessageSemaphore{false};
bool ZHNetwork::confirmReceivingSemaphore{false};
bool ZHNetwork::confirmReceiving{false};
char ZHNetwork::netName_[20]{0};
uint8_t ZHNetwork::localMAC[6]{0};
uint16_t ZHNetwork::lastMessageID[10]{0};

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

error_code_t ZHNetwork::begin(const char *netName, const bool gateway)
{
    randomSeed(analogRead(0));
    if (strlen(netName) > 1 && strlen(netName) < 20)
        strcpy(netName_, netName);
#ifdef PRINT_LOG
    Serial.begin(115200);
#endif
    WiFi.mode(gateway ? WIFI_AP_STA : WIFI_STA);
    esp_now_init();
#if defined(ESP8266)
    wifi_get_macaddr(gateway ? SOFTAP_IF : STATION_IF, localMAC);
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
#endif
#if defined(ESP32)
    esp_wifi_get_mac(gateway ? (wifi_interface_t)ESP_IF_WIFI_AP : (wifi_interface_t)ESP_IF_WIFI_STA, localMAC);
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
            Serial.println(F("OK."));
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
            Serial.println(F("FAULT."));
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
                        Serial.print(F("CHECKING ROUTING TABLE... Routing to MAC "));
                        Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
                        Serial.println(F(" deleted."));
#endif
                    }
                }
                waiting_data_t waitingData;
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
            Serial.print(F("BROADCAST"));
            break;
        case UNICAST:
            Serial.print(F("UNICAST"));
            break;
        case UNICAST_WITH_CONFIRM:
            Serial.print(F("UNICAST_WITH_CONFIRM"));
            break;
        case DELIVERY_CONFIRM_RESPONSE:
            Serial.print(F("DELIVERY_CONFIRM_RESPONSE"));
            break;
        case SEARCH_REQUEST:
            Serial.print(F("SEARCH_REQUEST"));
            break;
        case SEARCH_RESPONSE:
            Serial.print(F("SEARCH_RESPONSE"));
            break;
        default:
            break;
        }
        Serial.print(F(" message from MAC "));
        Serial.print(macToString(outgoingData.transmittedData.originalSenderMAC));
        Serial.print(F(" to MAC "));
        Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
        Serial.print(F(" via MAC "));
        Serial.print(macToString(outgoingData.intermediateTargetMAC));
        Serial.print(F(" sended. Status "));
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
            Serial.print(F("BROADCAST message from MAC "));
            Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
            Serial.println(F(" received."));
#endif
            if (onBroadcastReceivingCallback)
                onBroadcastReceivingCallback(incomingData.transmittedData.message, incomingData.transmittedData.originalSenderMAC);
            forward = true;
            break;
        case UNICAST:
#ifdef PRINT_LOG
            Serial.print(F("UNICAST message from MAC "));
            Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
            Serial.print(F(" to MAC "));
            Serial.print(macToString(incomingData.transmittedData.originalTargetMAC));
            Serial.print(F(" via MAC "));
            Serial.print(macToString(incomingData.intermediateSenderMAC));
            Serial.println(F(" received."));
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
            Serial.print(F("UNICAST_WITH_CONFIRM message from MAC "));
            Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
            Serial.print(F(" to MAC "));
            Serial.print(macToString(incomingData.transmittedData.originalTargetMAC));
            Serial.print(F(" via MAC "));
            Serial.print(macToString(incomingData.intermediateSenderMAC));
            Serial.println(F(" received."));
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
            Serial.print(F("DELIVERY_CONFIRM_RESPONSE message from MAC "));
            Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
            Serial.print(F(" to MAC "));
            Serial.print(macToString(incomingData.transmittedData.originalTargetMAC));
            Serial.print(F(" via MAC "));
            Serial.print(macToString(incomingData.intermediateSenderMAC));
            Serial.println(F(" received."));
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
            Serial.print(F("SEARCH_REQUEST message from MAC "));
            Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
            Serial.print(F(" to MAC "));
            Serial.print(macToString(incomingData.transmittedData.originalTargetMAC));
            Serial.println(F(" received."));
#endif
            if (macToString(incomingData.transmittedData.originalTargetMAC) == macToString(localMAC))
                broadcastMessage("", incomingData.transmittedData.originalSenderMAC, SEARCH_RESPONSE);
            else
                forward = true;
            routingUpdate = true;
            break;
        case SEARCH_RESPONSE:
#ifdef PRINT_LOG
            Serial.print(F("SEARCH_RESPONSE message from MAC "));
            Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
            Serial.print(F(" to MAC "));
            Serial.print(macToString(incomingData.transmittedData.originalTargetMAC));
            Serial.println(F(" received."));
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
                        Serial.print(F("CHECKING ROUTING TABLE... Routing to MAC "));
                        Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
                        Serial.print(F(" updated. Target is "));
                        Serial.print(macToString(incomingData.intermediateSenderMAC));
                        Serial.println(F("."));
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
                    Serial.print(F("CHECKING ROUTING TABLE... Routing to MAC "));
                    Serial.print(macToString(incomingData.transmittedData.originalSenderMAC));
                    Serial.print(F(" added. Target is "));
                    Serial.print(macToString(incomingData.intermediateSenderMAC));
                    Serial.println(F("."));
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
                memcpy(&outgoingData.transmittedData, &waitingData.transmittedData, sizeof(transmitted_data_t));
                memcpy(&outgoingData.intermediateTargetMAC, &routingTable.intermediateTargetMAC, 6);
                queueForOutgoingData.push(outgoingData);
#ifdef PRINT_LOG
                Serial.print(F("CHECKING ROUTING TABLE... Routing to MAC "));
                Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
                Serial.print(F(" found. Target is "));
                Serial.print(macToString(outgoingData.intermediateTargetMAC));
                Serial.println(F("."));
#endif
                return;
            }
        }
        if ((millis() - waitingData.time) > maxTimeForRoutingInfoWaiting_)
        {
            queueForRoutingVectorWaiting.pop();
#ifdef PRINT_LOG
            Serial.print(F("CHECKING ROUTING TABLE... Routing to MAC "));
            Serial.print(macToString(waitingData.transmittedData.originalTargetMAC));
            Serial.println(F(" not found."));
            switch (waitingData.transmittedData.messageType)
            {
            case UNICAST:
                Serial.print(F("UNICAST"));
                break;
            case UNICAST_WITH_CONFIRM:
                Serial.print(F("UNICAST_WITH_CONFIRM"));
                break;
            case DELIVERY_CONFIRM_RESPONSE:
                Serial.print(F("DELIVERY_CONFIRM_RESPONSE"));
                break;
            default:
                break;
            }
            Serial.print(F(" message from MAC "));
            Serial.print(macToString(waitingData.transmittedData.originalSenderMAC));
            Serial.print(F(" to MAC "));
            Serial.print(macToString(waitingData.transmittedData.originalTargetMAC));
            Serial.print(F(" via MAC "));
            Serial.print(macToString(waitingData.intermediateTargetMAC));
            Serial.println(F(" undelivered."));
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
        Serial.print(F("BROADCAST"));
        break;
    case SEARCH_REQUEST:
        Serial.print(F("SEARCH_REQUEST"));
        break;
    case SEARCH_RESPONSE:
        Serial.print(F("SEARCH_RESPONSE"));
        break;
    default:
        break;
    }
    Serial.print(F(" message from MAC "));
    Serial.print(macToString(outgoingData.transmittedData.originalSenderMAC));
    Serial.print(F(" to MAC "));
    Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
    Serial.println(F(" added to queue."));
#endif
}

void ZHNetwork::unicastMessage(const char *data, const uint8_t *target, const uint8_t *sender, message_type_t type)
{
    outgoing_data_t outgoingData;
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
            Serial.print(F("CHECKING ROUTING TABLE... Routing to MAC "));
            Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
            Serial.print(F(" found. Target is "));
            Serial.print(macToString(outgoingData.intermediateTargetMAC));
            Serial.println(F("."));
            switch (outgoingData.transmittedData.messageType)
            {
            case UNICAST:
                Serial.print(F("UNICAST"));
                break;
            case UNICAST_WITH_CONFIRM:
                Serial.print(F("UNICAST_WITH_CONFIRM"));
                break;
            case DELIVERY_CONFIRM_RESPONSE:
                Serial.print(F("DELIVERY_CONFIRM_RESPONSE"));
                break;
            default:
                break;
            }
            Serial.print(F(" message from MAC "));
            Serial.print(macToString(outgoingData.transmittedData.originalSenderMAC));
            Serial.print(F(" to MAC "));
            Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
            Serial.print(F(" via MAC "));
            Serial.print(macToString(outgoingData.intermediateTargetMAC));
            Serial.println(F(" added to queue."));
#endif
            return;
        }
    }
    memcpy(&outgoingData.intermediateTargetMAC, target, 6);
    queueForOutgoingData.push(outgoingData);
#ifdef PRINT_LOG
    Serial.print(F("CHECKING ROUTING TABLE... Routing to MAC "));
    Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
    Serial.print(F(" not found. Target is "));
    Serial.print(macToString(outgoingData.intermediateTargetMAC));
    Serial.println(F("."));
    switch (outgoingData.transmittedData.messageType)
    {
    case UNICAST:
        Serial.print(F("UNICAST"));
        break;
    case UNICAST_WITH_CONFIRM:
        Serial.print(F("UNICAST_WITH_CONFIRM"));
        break;
    case DELIVERY_CONFIRM_RESPONSE:
        Serial.print(F("DELIVERY_CONFIRM_RESPONSE"));
        break;
    default:
        break;
    }
    Serial.print(F(" message from MAC "));
    Serial.print(macToString(outgoingData.transmittedData.originalSenderMAC));
    Serial.print(F(" to MAC "));
    Serial.print(macToString(outgoingData.transmittedData.originalTargetMAC));
    Serial.print(F(" via MAC "));
    Serial.print(macToString(outgoingData.intermediateTargetMAC));
    Serial.println(F(" added to queue."));
#endif
}