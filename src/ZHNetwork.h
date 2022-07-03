#ifndef ZHNETWORK_H
#define ZHNETWORK_H

#include "ESP8266WiFi.h"
#include "espnow.h"
#include "ArduinoOTA.h"
#include "arduino.h"
#include "queue"

typedef std::function<void(const char *, const byte *)> onMessage;
typedef std::function<void()> onConfirm;

class ZHNetwork
{
public:
    /**
     * Функция обработки входящего широковещательного сообщения.
     *
     * @param char* Полученные данные.
     * @param byte* MAC отправителя (массив 6 байт).
     *
     */
    ZHNetwork &setOnBroadcastReceivingCallback(onMessage onBroadcastReceivingCallback);

    /**
     * Функция обработки входящего целевого сообщения.
     *
     * @param char* Полученные данные.
     * @param byte* MAC отправителя (массив 6 байт).
     *
     */
    ZHNetwork &setOnUnicastReceivingCallback(onMessage onUnicastReceivingCallback);

    /**
     * Функция обработки подтверждения успешной отправки сообщения.
     * Внимание! Подтверждение успешной отправки только ближайшему устройству в цепочке (если выполняется пересылка до конечного адресата).
     *
     */
    ZHNetwork &setOnConfirmReceivingCallback(onConfirm onConfirmReceivingCallback);

    /**
     * Запуск устройства в режиме узла ESP-NOW.
     *
     * @param name Имя сети ESP-NOW. Максимальная длина - 20 байт.
     *
     */
    void begin(const char *name);

    /**
     * Запуск устройства в режиме шлюза между ESP-NOW и WiFi. Обновление прошивки через OTA доступно по IP-адресу.
     * Внимание! Для работы в этом режиме WiFi роутер должен быть настроен на канал 1.
     *
     * @param name Имя сети ESP-NOW. Максимальная длина - 20 байт.
     * @param ssid Имя WiFi сети.
     * @param password Пароль WiFi сети.
     *
     * @return Истина, если соединение WiFi установлено.
     */
    bool begin(const char *name, const char *ssid, const char *password);

    /**
     * Переключение устройства в режим обновления прошивки через OTA (для режима узла ESP-NOW).
     * Для обновления подключитесь к сети "ESP-NOW NODE XXXXXXXXXXXXXX" (без пароля).
     *
     */
    void update(void);

    /**
     * Отправка широковещательного сообщения всем устройствам в сети ESP-NOW.
     *
     * @param data Данные для передачи. Максимальная длина - 200 байт.
     *
     */
    void sendBroadcastMessage(const char *data);

    /**
     * Отправка целевого сообщения на устройство в сети ESP-NOW.
     *
     * @param data Данные для передачи. Максимальная длина - 200 байт.
     * @param target Массив (6 байт), содержащий MAC-адрес получателя.
     *
     */
    void sendUnicastMessage(const char *data, const byte *target);

    /**
     * Именно здесь происходит все волшебство, связанное с приемом и передачей сообщений.
     * Должен быть добавлен в loop.
     *
     */
    void maintenance(void);

    /**
     * Получить MAC-адрес узла/шлюза ESP-NOW.
     *
     * @return MAC-адрес в виде строки из 12 символов.
     */
    String getNodeMac(void);

    /**
     * Преобразование MAC-адреса (массив 6 байт) в строку из 12 символов.
     *
     * @param mac Массив (6 байт), содержащий MAC-адрес.
     *
     */
    static String macToString(const byte *mac);

    /**
     * Преобразование строки из 12 символов в массив (6 байт) MAC-адреса.
     *
     * @param string Строка из 12 символов.
     * @param mac Массив (6 байт), в который будет записан MAC-адрес.
     *
     */
    byte *stringToMac(const String &string, byte *mac);

    /**
     * Получить версию ПО узла/шлюза ESP-NOW.
     *
     * @return Версия ПО.
     */
    String getFirmwareVersion(void);

    /**
     * Установить максимальное количество попыток отправки сообщения (от 1 до 10). Значение по умолчанию - 3.
     *
     * @return Истина, если значение установлено.
     */
    bool setMaxNumberOfAttempts(const byte number);

    /**
     * Установить максимальное время ожидания между отправками сообщений (от 20 до 250 миллисекунд). Значение по умолчанию - 50.
     *
     * @return Истина, если значение установлено.
     */
    bool setMaxWaitingTimeBetweenTransmissions(const byte time);

    /**
     * Установить максимальное время ожидания получения маршрута до конечного получателя (от 500 до 5000 миллисекунд). Значение по умолчанию - 500.
     *
     * @return Истина, если значение установлено.
     */
    bool setMaxWaitingTimeForRoutingInfo(const uint16_t time);

private:
    const String firmware = "2.0";
    uint64_t lastMessageSentTime{0};
    uint64_t lastSearchMessageSentTime{0};
    byte maxWaitingTimeBetweenTransmissions{50};
    uint16_t maxTimeForRoutingInfoWaiting{500};
    bool updateMode{false};
    byte maxNumberOfAttempts{3};
    byte numberOfAttemptsToSend{1};

    static void onDataSent(byte *mac, byte status);
    static void onDataReceive(byte *mac, byte *data, byte length);
    void sendUnicastMessage(const char *data, const byte *target, const byte *sender);
    void sendSearchMessage(const byte *target);
    onMessage onBroadcastReceivingCallback;
    onMessage onUnicastReceivingCallback;
    onConfirm onConfirmReceivingCallback;

protected:
};

#endif