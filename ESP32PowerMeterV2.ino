#include <WiFiClientSecure.h>                // needed for the WiFi communication
#include <ESPmDNS.h>                         // for FOTA Suport
#include <WiFiUdp.h>                         
#include <ArduinoOTA.h>                     
#include <MQTTClient.h>                      //  https://github.com/256dpi/arduino-mqtt   keepalive manually to 15s
#include <PZEM004T.h>
#include <Preferences.h>

const char* Hostname = "ESPPowerMeter";          
String WiFi_SSID = "PINGU_2GHz";              
String WiFi_PW = "*******";                 
const char* OTA_PW = "iotsharing";                
String mqtt_broker = "10.99.68.248";         
String mqtt_user = "******";               
String mqtt_pw = "********";                   
String input_topic = "sensor/powermeter";        
unsigned long waitCount = 0;                 
uint8_t conn_stat = 0;                      
unsigned long lastStatus = 0;                
unsigned long lastTask = 0;                  
unsigned long lastPowerStatus =0;

const char* Version = "{\"Version\":\"low_prio_wifi_v2\"}";
const char* Status = "{\"Message\":\"Hello From ESPPowerMeter\"}";
int         buzzerPin=32;

WiFiClient  TCP;                        
MQTTClient mqttClient(512);                  


HardwareSerial Serial22(2);     // Use hwserial UART2 at pins IO-16 (RX2) and IO-17 (TX2)
PZEM004T pzem(&Serial22);
IPAddress ip(192,168,1,1);

Preferences preferences;

void MQTTmessageReceived(String &topic, String &payload) {
  Serial.begin(115200);
  Serial.println("incoming: " + topic + " - " + payload);
  if(topic == "sensor/powermeter/setpowerthreshold"){
    Serial.println("Threshold set to "+payload);
    preferences.putUInt("powerThreshold", payload.toInt());
  }
  if(topic == "sensor/powermeter/setreadinginterval"){
   preferences.putUInt("readinginterval", payload.toInt());   
  }
  if(topic == "sensor/powermeter/sayhello"){
    beepbeep(200);
  }
 
}

void beepbeep(int beepDelay){
   pinMode(buzzerPin,  OUTPUT);
   digitalWrite(buzzerPin,  HIGH);
   delay(beepDelay);
   digitalWrite(buzzerPin,  LOW);
}

void setup() {
  Serial.begin(115200);

  beepbeep(2000);
  preferences.begin("powerMeter", false);

   
  WiFi.mode(WIFI_STA);                                            // config WiFi as client
  MDNS.begin(Hostname);                                           // start MDNS, needed for OTA
  ArduinoOTA.setHostname(Hostname);                               // initialize and start OTA
  ArduinoOTA.setPassword(OTA_PW);                                 //       set OTA password
  ArduinoOTA.onError([](ota_error_t error) {ESP.restart();});     //       restart in case of an error during OTA
  ArduinoOTA.begin();                                             //       at this point OTA is set up

  pzem.setAddress(ip);

  
}


void loop() {  
   
// with current code runs roughly 400 times per second
// start of non-blocking connection setup section
// Code Reference based on discussion Forum: https://www.esp32.com/viewtopic.php?t=3851&p=17506
  if ((WiFi.status() != WL_CONNECTED) && (conn_stat != 1)) { conn_stat = 0; }
  if ((WiFi.status() == WL_CONNECTED) && !mqttClient.connected() && (conn_stat != 3))  { conn_stat = 2; }
  if ((WiFi.status() == WL_CONNECTED) && mqttClient.connected() && (conn_stat != 5)) { conn_stat = 4;}
  switch (conn_stat) {
    case 0:                                                       // MQTT and WiFi down: start WiFi
      Serial.println("MQTT and WiFi down: start WiFi");
      WiFi.begin(WiFi_SSID.c_str(), WiFi_PW.c_str());
      conn_stat = 1;
      break;
    case 1:                                                       // WiFi starting, do nothing here
      Serial.println("WiFi starting, wait : "+ String(waitCount));
      waitCount++;
      break;
    case 2:                                                       // WiFi up, MQTT down: start MQTT
      Serial.println("WiFi up, MQTT down: start MQTT");
      mqttClient.begin(mqtt_broker.c_str(), 1883, TCP);           //   config MQTT Server, use port 8883 for secure connection
      mqttClient.onMessage(MQTTmessageReceived);
      mqttClient.connect(Hostname, mqtt_user.c_str(), mqtt_pw.c_str());
      conn_stat = 3;
      waitCount = 0;
      break;
    case 3:                                                       // WiFi up, MQTT starting, do nothing here
      Serial.println("WiFi up, MQTT starting, wait : "+ String(waitCount));
      waitCount++;
      conn_stat = 2;
      break;
    case 4:                                                       // WiFi up, MQTT up: finish MQTT configuration
      Serial.println("WiFi up, MQTT up: finish MQTT configuration");
      //mqttClient.subscribe(output_topic);

      mqttClient.subscribe("sensor/powermeter/sayhello");
      mqttClient.subscribe("sensor/powermeter/setpowerthreshold");
      mqttClient.subscribe("sensor/powermeter/setreadinginterval");
      
      
      mqttClient.publish(input_topic, Version);
      mqttClient.publish(input_topic, Status);
      conn_stat = 5;

     beepbeep(50);
                      
      break;
  }
// end of non-blocking connection setup section

// start section with tasks where WiFi/MQTT is required
  if (conn_stat == 5) {
    if (millis() - lastStatus > 60000) {                            // Start send status every 60 sec (just as an example)
      Serial.println(Status);
     // mqttClient.publish(input_topic, Status);                      //      send status to broker
      mqttClient.loop();                                            //      give control to MQTT to send message to broker
      lastStatus = millis();                                        //      remember time of last sent status message
    }
    ArduinoOTA.handle();                                            // internal household function for OTA
    mqttClient.loop();  
   
    // internal household function for MQTT

    int readinginterval = preferences.getUInt("readinginterval", 10000);
  
    if (millis() - lastPowerStatus > readinginterval) {   //every 10 sec    
     float v = pzem.voltage(ip);
       if (v < 0.0) v = 0.0;
    // String value = "Voltage: "+String(v);   
     mqttClient.publish("sensor/powermeter/voltage", String(v));

     float e = pzem.energy(ip);
     if(e < 0.0) e =0.0;
     
    // String eValue = "WattHour: "+String(e);   
     mqttClient.publish("sensor/powermeter/energy", String(e));
     
     float curr = pzem.current(ip);
     if(curr < 0.0) curr = 0.0;
     mqttClient.publish("sensor/powermeter/current", String(curr));

     float pw = pzem.power(ip);
     if(pw < 0.0) pw = 0.0;
     mqttClient.publish("sensor/powermeter/power", String(pw));

     unsigned int powerThreshold = preferences.getUInt("powerThreshold", 0);
     if(pw >= powerThreshold){
        beepbeep(1000);
     }
     mqttClient.publish("sensor/powermeter/currentpowerthreshold", String(powerThreshold));
     lastPowerStatus = millis();
    }
    
  } 
// end of section for tasks where WiFi/MQTT are required

// start section for tasks which should run regardless of WiFi/MQTT
  if (millis() - lastTask > 1000) {                                 // Print message every second (just as an example)
     
    lastTask = millis();
  }
  delay(100);
// end of section for tasks which should run regardless of WiFi/MQTT
}

