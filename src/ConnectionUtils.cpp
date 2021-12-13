#include "ConnectionUtils.h"
#include "arduino_secrets.h"


const char broker[] = BROKER_ADRESS;
const char ssid[] = HOME_SSID;
const char wpa[] = KEY_WPA;
const int b_port = BROKER_PORT;

#define WIFI_TIMEOUT_INTERVAL 5000
#define MQTT_POLL_INTERVAL 100

int MQTTValueReceived[5]; // sequence : pump_mode - t_night_off - t_night_on - t_day_off - t_day_on

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

String ID;

String MACasString() {
  byte mac[6];
  WiFi.macAddress(mac);
  String ID = "";
  for (int i = 0; i < 6;i++) {
    ID += String(mac[i],HEX);
    if (i != 5) ID+= ":";
  }
  return ID;
}

void connectionsInitCheck() {
  if ((WiFi.status() == WL_NO_MODULE) || (WiFi.status() == WL_NO_SHIELD)) {
    Serial.print("ERROR : during initialization wifi -> ");
    Serial.println(WiFi.status());
    while (true);
  }
  
  ID = "NANO_33_IoT_"+MACasString();
  mqttClient.setId(ID);
  Serial.println("INFO : ID = "+ID);
  mqttClient.setUsernamePassword(BROKER_USER,BROKER_PWD);
  
  // Set Will topic & message
  Serial.println("INFO : initializing last will testament LWT");
  String willPayload = "Offline"; 
  mqttClient.beginWill("client/"+ID+"/status",willPayload.length(),true, 1);
  mqttClient.print(willPayload);
  mqttClient.endWill();
}

void connectionsTasks() {
  static int wifiStatus = WL_IDLE_STATUS;
  static int state = 0;
  static int wifiConnectTry = 0;
  static int mqttConnectTry = 0;
  static unsigned long previousMillis = 0;
  unsigned long currentMillis = 0;
  
  switch (state)
  {
    // connect if first time OR fails others. If status OK, change state
    case WIFI_CONNECT:
      if (wifiStatus == WL_CONNECTED) {
        Serial.println("INFO : Connected to WiFi");
        state++;
        break;
      }
      if ((millis() - previousMillis < WIFI_TIMEOUT_INTERVAL) && (wifiConnectTry > 0)) {
        // didn't reach the time out interval -> continue
        break;
      }
      if (wifiConnectTry > 10) {
        // to long to connect -> restart
        state = WIFI_STATE_RESTART;
        break;
      }      
      Serial.print("INFO : Connecting to WiFi... Status : ");
      wifiStatus = WiFi.begin(ssid,wpa);
      wifiConnectTry++;
      Serial.println(wifiStatus);
      previousMillis = millis();
      break;
    // attempts to connect to the broker. if too many tries -> start all over again
    case MQTT_CONNECT:
      if (mqttClient.connect(broker,b_port)) {
        state++;
        Serial.println("INFO : connected to MQTT broker");
        sendMQTTMessage("client/"+ID+"/status","Online",true);
      }
      else {
        Serial.print("ERROR : MQTT connection failed -> ");
        Serial.print(mqttClient.connectError());
        mqttConnectTry++;
        if (mqttConnectTry> 10)
        {
          state = WIFI_STATE_RESTART;
        }
      }
    break;
    // register the callback and the topics subscribed
    case MQTT_SUB:
      Serial.println("INFO : MQTT Subscription");
      mqttClient.onMessage(onMqttMessage);
      mqttClient.subscribe("general/garden/action/#",0);
      state++;
      break;
    // send keepalive to broker. if no more connected, start again
    case MQTT_POLL:
      currentMillis = millis();
      if ( currentMillis - previousMillis > MQTT_POLL_INTERVAL )
      {
        previousMillis = currentMillis;
        // call poll() regularly to allow the library to send MQTT keep alives which
        // avoids being disconnected by the broker
        mqttClient.poll();
        //Serial.println("INFO : polling MQTT");
        if ( !mqttClient.connected() )
        {
          Serial.println ("WARNING : MQTT Disconnected");
          state = WIFI_STATE_RESTART;
          //mqttReconnectCount++;
          break;
        }
        break;
      }
      break;
    // WIFI_STATE_RESTART re init all the variables - disconnect from wifi -start from status 0
    default:
      state = 0;
      wifiConnectTry = 0;
      mqttConnectTry = 0;
      wifiStatus = WL_IDLE_STATUS;
      WiFi.disconnect();
      WiFi.end();
      //Serial.println("WARNING : WiFi restart");
  }
}

void onMqttMessage(int messageSize) {

  char message[messageSize];
  int i = 0;
  String topic = mqttClient.messageTopic();

  // use the Stream interface to print the contents
  while (mqttClient.available()) {
    message[i]= (char)mqttClient.read();
    i++;
  }  
    //Serial.println(message);
    String messageString(message);
    int pumpModeIndex = topic.indexOf("pump_mode");
    
    if (pumpModeIndex != -1) {
      MQTTValueReceived[0] = messageString.toInt();//(int)message-48;
    } else {
      int setTimerIndex = topic.indexOf("timer/");
      if (setTimerIndex != -1) {
        if (topic.indexOf("night/off",setTimerIndex) != -1) {
          MQTTValueReceived[1] = messageString.toInt();//String(message)-48;
        } else if (topic.indexOf("night/on",setTimerIndex) != -1) {
          MQTTValueReceived[2] = messageString.toInt();//(int)message-48;
        } else if (topic.indexOf("day/off",setTimerIndex) != -1) {
          MQTTValueReceived[3] = messageString.toInt();//(int)message-48;
        } else if (topic.indexOf("day/on",setTimerIndex) != -1) {
          MQTTValueReceived[4] = messageString.toInt();//(int)message-48;
        }
      }
    }
  
  
  //Serial.print("', length ");
  //Serial.print(messageSize);
  //
  // use the Stream interface to print the contents
//  while (mqttClient.available()) { 
//    char pumpTrigger = (char)mqttClient.read();
//    switch (pumpTrigger) {
//      case '0':
//        digitalWrite(LED_BUILTIN,LOW);
//        break;
//      case '1':
//        digitalWrite(LED_BUILTIN,HIGH);
//        break;
//      default:
//        break;
//    }
//  }
  Serial.println("INFO : message read "+topic);
    // TO DO : tester le code ci dessous
//  if (strcomp(pumpTrigger,"false") == 0) {
//    
//  }
}

void sendMQTTMessage(String topic, float value, int decimal) {
  mqttClient.beginMessage(topic);
  mqttClient.print(value,decimal);
  mqttClient.endMessage();
//    Serial.print("SUCCESS : Message "+topic+" sent with value :");
//    Serial.println(value);
}

void sendMQTTMessage(String topic, String value,bool retain) {
    mqttClient.beginMessage(topic,retain);
    mqttClient.print(value);
    mqttClient.endMessage();
}

void sendMQTTMessage_JSON(String topic, DynamicJsonDocument doc, bool retain) {
  mqttClient.beginMessage(topic, retain);
  serializeJson(doc,mqttClient);
  mqttClient.endMessage();  
}

//void sendMQTTMessage(String topic, bool value,bool retain) {
//  if(value) {
//    sendMQTTMessage(topic, String(1),retain);
//  } else {
//    sendMQTTMessage(topic, String(0),retain);
//    //mqttClient.beginMessage(String(0),retain);
//  }
////    
////    mqttClient.print(value);
////    mqttClient.endMessage();
//}
