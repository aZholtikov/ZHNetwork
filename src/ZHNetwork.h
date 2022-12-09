#ifndef ZHNETWORK_H
#define ZHNETWORK_H

#include "Arduino.h"
#include "bits/stdc++.h"
#if defined(ESP8266)
#include "ESP8266WiFi.h"
#include "espnow.h"
#endif
#if defined(ESP32)
#include "WiFi.h"
#include "esp_wifi.h"
#include "esp_now.h"
#endif

// #define PRINT_LOG // Uncomment to display to serial port the full operation log.

#if defined(ESP8266)
#define esp_memset memset // Just for remove the compiler notice for ESP8266 at "memset". I don't know why this is happening...
#endif
#if defined(ESP32)
#define esp_memset memset
#endif

typedef struct
{
    uint8_t messageType;
    uint16_t messageID;
    char netName[20];
    uint8_t originalTargetMAC[6];
    uint8_t originalSenderMAC[6];
    char message[200];
} transmitted_data_t;

typedef struct
{
    uint8_t intermediateTargetMAC[6];
    transmitted_data_t transmittedData;
} outgoing_data_t;

typedef struct
{
    uint8_t intermediateSenderMAC[6];
    transmitted_data_t transmittedData;
} incoming_data_t;

typedef struct
{
    uint64_t time;
    uint8_t intermediateTargetMAC[6];
    transmitted_data_t transmittedData;
} waiting_data_t;

typedef struct
{
    uint8_t originalTargetMAC[6];
    uint8_t intermediateTargetMAC[6];
} routing_table_t;

typedef enum
{
    ESP_NOW = 1,
    ESP_NOW_AP,
    ESP_NOW_STA
} work_mode_t;

typedef enum
{
    BROADCAST = 1,
    UNICAST,
    UNICAST_WITH_CONFIRM,
    DELIVERY_CONFIRM_RESPONSE,
    SEARCH_REQUEST,
    SEARCH_RESPONSE
} message_type_t;

typedef enum // Just for further development.
{
    SUCCESS = 1,
    ERROR = 0
} error_code_t;

typedef std::function<void(const char *, const uint8_t *)> on_message_t;
typedef std::function<void(const uint8_t *, const bool)> on_confirm_t;
typedef std::vector<routing_table_t> routing_vector_t;
typedef std::queue<outgoing_data_t> outgoing_queue_t;
typedef std::queue<incoming_data_t> incoming_queue_t;
typedef std::queue<waiting_data_t> waiting_queue_t;

class ZHNetwork
{
public:
    ZHNetwork &setOnBroadcastReceivingCallback(on_message_t onBroadcastReceivingCallback);
    ZHNetwork &setOnUnicastReceivingCallback(on_message_t onUnicastReceivingCallback);
    ZHNetwork &setOnConfirmReceivingCallback(on_confirm_t onConfirmReceivingCallback);

    error_code_t setWorkMode(const work_mode_t workMode);
    work_mode_t getWorkMode(void);

    error_code_t setNetName(const char *netName);
    String getNetName(void);

    error_code_t setStaSetting(const char *ssid, const char *password);
    error_code_t setApSetting(const char *ssid, const char *password);

    error_code_t begin(void);

    void sendBroadcastMessage(const char *data);
    void sendUnicastMessage(const char *data, const uint8_t *target, const bool confirm = false);

    void maintenance(void);

    String getNodeMac(void);
    IPAddress getNodeIp(void);
    String getFirmwareVersion(void);
    String readErrorCode(error_code_t code); // Just for further development.

    static String macToString(const uint8_t *mac);
    uint8_t *stringToMac(const String &string, uint8_t *mac);

    error_code_t setMaxNumberOfAttempts(const uint8_t maxNumberOfAttempts);
    uint8_t getMaxNumberOfAttempts(void);
    error_code_t setMaxWaitingTimeBetweenTransmissions(const uint8_t maxWaitingTimeBetweenTransmissions);
    uint8_t getMaxWaitingTimeBetweenTransmissions(void);
    error_code_t setMaxWaitingTimeForRoutingInfo(const uint16_t maxTimeForRoutingInfoWaiting);
    uint16_t getMaxWaitingTimeForRoutingInfo(void);

private:
#if defined(ESP8266)
    static void onDataSent(uint8_t *mac, uint8_t status);
    static void onDataReceive(uint8_t *mac, uint8_t *data, uint8_t length);
#endif
#if defined(ESP32)
    static void onDataSent(const uint8_t *mac, esp_now_send_status_t status);
    static void onDataReceive(const uint8_t *mac, const uint8_t *data, int length);
#endif
    void broadcastMessage(const char *data, const uint8_t *target, message_type_t type);
    void unicastMessage(const char *data, const uint8_t *target, const uint8_t *sender, message_type_t type);
    on_message_t onBroadcastReceivingCallback;
    on_message_t onUnicastReceivingCallback;
    on_confirm_t onConfirmReceivingCallback;

protected:
};

#endif