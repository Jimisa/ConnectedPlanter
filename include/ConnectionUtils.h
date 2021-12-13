
#ifndef _CONNECTION_UTILS_H_
#define _CONNECTION_UTILS_H_

#include <Arduino.h>
#include <ArduinoMqttClient.h>
#include <WiFiNINA.h>
#include <ArduinoJson.h>



#define WIFI_CONNECT        0
#define MQTT_CONNECT        1
#define MQTT_SUB            2
#define MQTT_POLL           3
#define WIFI_STATE_RESTART  255

extern int MQTTValueReceived[5];

void connectionsInitCheck(void);
void connectionsTasks(void);
void onMqttMessage(int);
void sendMQTTMessage(String,float,int);
void sendMQTTMessage(String,String,bool retain = false);
//void sendMQTTMessage(String,bool,bool retain = false);
void sendMQTTMessage_JSON(String,DynamicJsonDocument,bool);
String MACasString(void);

#endif
