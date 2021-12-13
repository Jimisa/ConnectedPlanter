#include <Arduino.h>
#include <ConnectionUtils.h>

/*
  LectureVoltage

  API Arduino MQTT client : https://github.com/arduino-libraries/ArduinoMqttClient

  DONE : Nettoyer le code car lenteur quand utilisatiobn d'un callback -> Ok avec l'implémentation du pattern 'state machine'
  TO DO : Ajouter des publish message sur le status
  TO DO : utiliser flag retain message pour l'état de la led
*/

// Constants (pins, intervals...)
const int INPUT_BATTERY_SENSOR = A0;
const int INPUT_LIGHT_SENSOR = A1;
const int OUTPUT_PUMP_RELAY = 4;
const int NIGHT_TRESHOLD = 80;
const long MEASURE_INTERVAL = 3000;
const int numReadings = 10;
const float voltTreshold[4] = {12.4,12.25,12.1,11.8};
const size_t capacity = JSON_OBJECT_SIZE(2);

// variables for measuring tension
int readIndex = 0;
float readings[numReadings];
float total = 0;
float averageTension = 0.0;

// time counters for different states 
unsigned long previousPumpCounter=0;
unsigned long previousLightCounter=0;
unsigned long previousMeasureCounter = 0;


bool currentPumpState = false; // init => false = off
bool dayNight; // true = day, false = night
byte sequence_dayNight = 0;
unsigned int pumpTimersArray[2][2][5];

StaticJsonDocument<capacity> docDay;

/*
 * 
 */
void setup() {
  
  Serial.begin(9600);
  //while(!Serial);
  analogReference(AR_DEFAULT);
  //analogReadCorrection(22, 2082);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(OUTPUT_PUMP_RELAY, OUTPUT);

  // init the array for average of sensor
  for(int thisReading = 0;thisReading < numReadings;thisReading++) {
    readings[thisReading] = 0;
  }
  // Init the pump timer matrix
  // [1] 0 = NUIT, 1 = JOUR, [2] 0 = OFF, 1 = ON, [3] seuil tension -> 0:>12.4//1:>12,25//2:>12.1//3:>11.9//4:<11.75
  pumpTimersArray[0][0][0] = 60; 
  pumpTimersArray[0][0][1] = 120;
  pumpTimersArray[0][0][2] = 180;
  pumpTimersArray[0][0][3] = 240;
  pumpTimersArray[0][0][4] = 240;
  pumpTimersArray[0][1][0] = 2;
  pumpTimersArray[0][1][1] = 2;
  pumpTimersArray[0][1][2] = 2;
  pumpTimersArray[0][1][3] = 1;
  pumpTimersArray[0][1][4] = 0;
  pumpTimersArray[1][0][0] = 10;
  pumpTimersArray[1][0][1] = 20;
  pumpTimersArray[1][0][2] = 30;
  pumpTimersArray[1][0][3] = 60;
  pumpTimersArray[1][0][4] = 60;
  pumpTimersArray[1][1][0] = 2;
  pumpTimersArray[1][1][1] = 2;
  pumpTimersArray[1][1][2] = 1;
  pumpTimersArray[1][1][3] = 1;
  pumpTimersArray[1][1][4] = 0;

  connectionsInitCheck();
  Serial.flush();

  // init the day night value : 
  sequence_dayNight = ((analogRead(INPUT_LIGHT_SENSOR)-NIGHT_TRESHOLD)<0)?0:1;
  //Serial.println(sequence_dayNight,BIN);
}


// the loop routine runs over and over again forever:
void loop() {
  static unsigned int t_day_on;
  static unsigned int t_day_off;
  static unsigned int t_night_off;
  static unsigned int t_night_on;
  
  connectionsTasks();
  
  unsigned long currentCounter = millis();

  /*
   * ###########################################################################################
   *                                     VOLTAGE MEASURE
   *  One measure each MEASURE INTERVAL
   *  averaging with 10 readings
   * ###########################################################################################
   */
  if (currentCounter - previousMeasureCounter >= MEASURE_INTERVAL) { 
    total = total - readings[readIndex];
  
    readings[readIndex] = getBatteryVoltage();
    //Serial.println(readings[readIndex],3);
    total = total + readings[readIndex];
    readIndex += 1;
  
    if (readIndex >= numReadings) {
      readIndex = 0;
    }

   

    previousMeasureCounter = currentCounter;
    // Sending the value to the broker ony after all the average arry is filled (10+2 min/max)
    if (currentCounter > 13*MEASURE_INTERVAL) { 
      averageTension = (total - maxReading() - minReading()) / (numReadings-2);
      sendMQTTMessage("general/garden/monitor/battery",averageTension,3);
    }
  
    /*
     * ########################################################################################
     *                                      LIGHT SENSOR
     * if the value reached is beyond the treshold set, notify the changing state via MQTT                            
     * and set the boolean dayNight
     * needs to be put inside the measure interval because otherwise the state is too much unstable 
     * from state Day to Night
     *                            
     * ########################################################################################
     */ 
    int lightValue = analogRead(INPUT_LIGHT_SENSOR);
    bool current_dayNight = (((lightValue-NIGHT_TRESHOLD)<0)?false:true);
    
    // shift sequence to include current_dayNight at LSB
    sequence_dayNight<<=1;
    bitWrite(sequence_dayNight,0,current_dayNight);
    
    // XOR condition between n and n-1 state anf filter the last 5 bits, meaning the 5 last states. 16 DEC = 10000 BIN, so if the last 4 states havn't changed (0) after one change (1), thus the state is switched
    if (((sequence_dayNight^(sequence_dayNight>>1))&B00011111)==16) {
    
      docDay["value"]=current_dayNight;
      docDay["interval"]=currentCounter-previousLightCounter;
       
      previousLightCounter=currentCounter;
      sendMQTTMessage_JSON("general/garden/monitor/daynight",docDay,true);
      //sendMQTTMessage("general/garden/monitor/daynight",(bool)current_dayNight,false);
      dayNight=current_dayNight;
    }
  }

  // Don't set anything if averageTension is not settled (13 times the interval)
  if (averageTension!=0.0) {
    /*
     * #########################################################################################
     *                                    SETTING THE TIMERS VALUE
     * depending of the battery tension and the value of the MQTT mode state 
     * (MQTTValueReceived[0]). MQTT sent : alert if battery too low
     * 
     * #########################################################################################
     */
    if ((averageTension<voltTreshold[3])&&(averageTension>11.0)) {
      // Battery too low -> STOP pumping
      t_night_off = pumpTimersArray[0][0][4];
      t_night_on = pumpTimersArray[0][1][4];
      t_day_off = pumpTimersArray[1][0][4];
      t_day_on = pumpTimersArray[1][1][4];
      sendMQTTMessage("general/garden/alert/lowbattery","true");
    } else if (MQTTValueReceived[0]==1) {
        // manuel
        //Serial.println("MANUAL");
        t_night_off = (int)MQTTValueReceived[1];
        t_night_on = (int)MQTTValueReceived[2];
        t_day_off = (int)MQTTValueReceived[3];
        t_day_on = (int)MQTTValueReceived[4];
      } else {
        //Serial.println("AUTO");
        // auto
        if (averageTension>voltTreshold[0]) {
          t_night_off = pumpTimersArray[0][0][0];
          t_night_on = pumpTimersArray[0][1][0];
          t_day_off = pumpTimersArray[1][0][0];
          t_day_on = pumpTimersArray[1][1][0];
        } else if (averageTension>voltTreshold[1])  {
          t_night_off = pumpTimersArray[0][0][1];
          t_night_on = pumpTimersArray[0][1][1];
          t_day_off = pumpTimersArray[1][0][1];
          t_day_on = pumpTimersArray[1][1][1];
        } else if (averageTension>voltTreshold[2]) {
          t_night_off = pumpTimersArray[0][0][2];
          t_night_on = pumpTimersArray[0][1][2];
          t_day_off = pumpTimersArray[1][0][2];
          t_day_on = pumpTimersArray[1][1][2];
        } else if (averageTension>voltTreshold[3]) {
          t_night_off = pumpTimersArray[0][0][3];
          t_night_on = pumpTimersArray[0][1][3];
          t_day_off = pumpTimersArray[1][0][3];
          t_day_on = pumpTimersArray[1][1][3];
        }
      }
//      Serial.print("n-off : ");
//      Serial.println(t_night_off);
//      Serial.print("n-on : ");
//      Serial.println(t_night_on);
//      Serial.print("d-off : ");
//      Serial.println(t_day_off);
//      Serial.print("d-on : ");
//      Serial.println(t_day_on);
//      Serial.flush();    
//      Serial.print("Jour/Nuit? ");
//      Serial.println(dayNight==0?"nuit":"jour");
//      Serial.print("Etat pompe : ");
//      Serial.println(currentPumpState==0?"off":"on");
//      Serial.flush();

      /* #########################################################################################
       *                                TRIGGER PUMP STATE
       * if the current counter exceed the relevant timer, according the ligth state
       *  
       * #########################################################################################
       */
      if (!dayNight) {
        // NIGHT
        if (!currentPumpState) {
          // OFF
          if ((currentCounter-previousPumpCounter) > (t_night_off*60000)) {
            //Serial.print("durée night OFF terminé : ");
            //Serial.println(t_night_off);
            changePumpState(currentCounter-previousPumpCounter);
            previousPumpCounter = currentCounter;
          }
        } else {
          // ON
           if ((currentCounter-previousPumpCounter) > (t_night_on*60000)) {
            //Serial.println("durée night ON terminé :");
            //Serial.println(t_night_on);
            changePumpState(currentCounter-previousPumpCounter);
            previousPumpCounter = currentCounter;        
          }
        }
      } else {
        // DAY
        if (!currentPumpState) {
          // OFF
          if ((currentCounter-previousPumpCounter) > (t_day_off*60000)) {
            //Serial.println("durée day OFF terminé : ");
            //Serial.println(t_day_off);
            changePumpState(currentCounter-previousPumpCounter);
            previousPumpCounter = currentCounter;
          }
        } else {
          // ON
           if (((currentCounter-previousPumpCounter) > t_day_on*60000)) {
            //Serial.println("durée day ON terminé : ");
            //Serial.println(t_day_on);
            changePumpState(currentCounter-previousPumpCounter);
            previousPumpCounter = currentCounter;        
          }
        }
      }
  }
}

/*
 * 
 */
void changePumpState(long interval) {
  StaticJsonDocument<capacity> docPump;
  docPump["pumpstate"]=currentPumpState;
  docPump["interval"]=interval;
  
  sendMQTTMessage_JSON("general/garden/monitor/pumpstate",docPump,true);
//  sendMQTTMessage("general/garden/monitor/pumpstate",currentPumpState,true);
  currentPumpState=!currentPumpState;
  digitalWrite(OUTPUT_PUMP_RELAY,currentPumpState);
}

/*
 * 
 */
float getBatteryVoltage() {
  // ratio = R2/(R1+R2)
  //const float R1 = 46800.0; 2eme Essai 21800.0 3eme Essai 4630
  //const float R2 = 9950.0 2eme Essai 4630.0 3eme Essai 994
  const float resistor_ratio = 0.17517972; //(1) : 0.1753303965 (2) :0.175185055  (3) : 176742532  
  float vin = 0.0;
  
  // read the input on analog pin:
  int sensorValue = analogRead(INPUT_BATTERY_SENSOR);
  // Convert the analog reading (which goes from 0 - 1023) to a voltage (0 - 3.3V):
  vin = (sensorValue+0.5) * 0.003132942; // = 3.205/1023
  return (float)(vin/resistor_ratio); //calcul après calibration au DVM. Mais écart de 0,1 -> *10.32231405
}

float maxReading() {
  float currentMax = 0.0;
  int index = 0;
    while(index < numReadings) {
      currentMax = max(currentMax,readings[index]);
      index++;
    }
  return currentMax;
}

float minReading() {
  float currentMin = 100.0;
  int index = 0;
    while(index < numReadings) {
      currentMin = min(currentMin,readings[index]);
      index++;
    }
  return currentMin;
}
