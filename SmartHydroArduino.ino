#include <SPI.h>
#include <WiFiEsp.h>
#include <WiFiEspClient.h>
#include <WiFiEspServer.h>
#include <DHT.h>
#include "DFRobot_PH.h"
#include "DFRobot_EC10.h"
#include <arduino-timer.h>

//HIGH IS LOW 
//LOW IS HIGH
//WE DON'T KNOW EITHER

// WiFi network settings
char ssid[] = "SmartHydro1";       // newtork SSID (name). 8 or more characters
char password[] = "Password123";  // network password. 8 or more characters
String message = "";

#include "EC.h"
#include "pH.h"
#include "Humidity.h"
#include "Temperature.h"
Eloquent::ML::Port::RandomForestEC ForestEC;
Eloquent::ML::Port::RandomForestpH ForestPH;
Eloquent::ML::Port::RandomForestHumidity ForestHumidity;
Eloquent::ML::Port::RandomForestTemperature ForestTemperature;

WiFiEspServer server(80);
RingBuffer buf(16);

#define FLOW_PIN 2
#define LIGHT_PIN A7
#define EC_PIN A8
#define PH_PIN A9
#define DHTTYPE DHT22
#define LED_PIN 4
#define FAN_PIN 5
#define PUMP_PIN 6
#define EXTRACTOR_PIN 7  
#define DHT_PIN 8
#define PH_UP_PIN 9
#define PH_DOWN_PIN 10
#define EC_UP_PIN 11
#define EC_DOWN_PIN 12


DFRobot_PH ph;
DHT dht = DHT(DHT_PIN, DHTTYPE);

const unsigned long SIXTEEN_HR = 57600000;
const unsigned long PUMP_INTERVAL = 5000;
const unsigned long EIGHT_HR = 28800000;
const unsigned long FOUR_HR = 14400000;

const unsigned long FIVE_MIN = 300000; // FIVE_MIN
const unsigned long valueOff = 28500000; 

const  float MAX_LIGHT_READING_LUX = 1023;
const  float MINIMIM_LIGHT_PERCENTAGE = 10.0 / 100.0;
DFRobot_EC10 ec;
auto timer = timer_create_default();
float temperature;
float humidity;
float ecLevel;
float phLevel;
float lightLevel;
float flowRate;
volatile int pulseCount = 0;
unsigned long currentTime, cloopTime;

void setup() {
 Serial.begin(115200);
  Serial1.begin(9600); // for esp32
  Serial2.begin(115200);
 WiFi.init(&Serial2); 
  pinMode(PUMP_PIN, OUTPUT);
digitalWrite(PUMP_PIN, HIGH);    // initially setting it off
  timer.in(0,timeOnPump);  // THESE TIMES ARE TO SCHEDULE FOR WHEN TIME OFF WILL TAKE PLACE

  // Check for the presence of the ESP module
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi module not detected. Please check wiring and power.");
    while (true)
      ;  // Don't proceed further
  }

  Serial.print("Attempting to start AP ");
  Serial.println(ssid);

  IPAddress localIp(192, 168, 8, 14);  // create an IP address
  WiFi.configAP(localIp);              // set the IP address of the AP

  // start access point
  // channel is the number. Ranges from 1-14, defaults to 1
  // last comma is encryption type
  WiFi.beginAP(ssid, 11, password, ENC_TYPE_WPA2_PSK);

  Serial.print("Access point started");

  // Start the server
  server.begin();
  ec.begin();
  dht.begin();
  ph.begin();

  pinMode(FLOW_PIN, INPUT);

   attachInterrupt(0, incrementPulseCounter, RISING);
   sei();

for (int i = 3; i < 13; i++) {    //This will initially set everything to off except the pump
  if (i != 8 ) {
    pinMode(i, OUTPUT);
    digitalWrite(i, HIGH);
  }
}
  // turning on equipment that should be on by default
  togglePin(LED_PIN);
  togglePin(FAN_PIN);
  togglePin(LIGHT_PIN);
  togglePin(EXTRACTOR_PIN);

  Serial.println("Server started");
  timer.every(5000, estimateTemperature);
  timer.every(5000, estimateHumidity);
  timer.every(SIXTEEN_HR, estimateEC);
  timer.every(SIXTEEN_HR, estimatePH);
 timer.every(1000, [](){
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    ecLevel = getEC();
    phLevel = getPH();
     flowRate = getFlowRate();
     lightLevel = getLightLevel();
});
timer.every(5000, []() {
 
  String message = "{";
  
  message += "\"PH\":\"" + String(isnan(phLevel) ? 0.0 : phLevel, 2) + "\",";        
  message += "\"Light\":\"" + String(isnan(lightLevel) ? 0.0 : lightLevel, 2) + "\",";  
  message += "\"EC\":\"" + String(isnan(ecLevel) ? 0.0 : ecLevel, 2) + "\",";        
  message += "\"FlowRate\":\"" + String(isnan(flowRate) ? 0.0 : flowRate, 2) + "\","; 
  message += "\"Humidity\":\"" + String(isnan(humidity) ? 0.0 : humidity, 2) + "\","; 
  message += "\"Temperature\":\"" + String(isnan(temperature) ? 0.0 : temperature, 2) + "\"";

  message += "}";
   Serial1.print('<');
  Serial1.print(message);
  Serial1.print('>');
      
  });
 // toggleLightOn();   MAY NEED TO CODE THIS BACK IN
}


void loop() {
  
  WiFiEspClient client = server.available();  // Check if a client has connected
 checkLightLevelBelow();
  timer.tick();
  readRemoteToggle();
  if (client) {  // If a client is available
    buf.init();
    message = "";
    while (client.connected()) {  // Loop while the client is connected
      if (client.available()) {   // Check if data is available from the client
        char c = client.read();
        //Serial.write(c); // Echo received data to Serial Monitor
        buf.push(c);
        // you got two newline characters in a row
        // that's the end of the HTTP request, so send a response
        if (buf.endsWith("\r\n\r\n")) {
         message = "{\n  \"PH\": \"" + String(phLevel, 2) + "\",\n \"Light\": \"" + String(lightLevel) + "\",\n  \"EC\": \"" + String(ecLevel, 2) + "\",\n  \"FlowRate\": \"" + String(flowRate) + "\",\n  \"Humidity\": \"" + String(humidity) + "\",\n  \"Temperature\": \"" + String(temperature) + "\"\n }";
         
          sendHttpResponse(client, message);
          break;
        }

        if (buf.endsWith("/light")) {
          togglePin(LED_PIN);
        } 

        if (buf.endsWith("/fan")) {
          togglePin(FAN_PIN);
        } 

        if (buf.endsWith("/extract")) {
          togglePin(EXTRACTOR_PIN);
        } 

        if (buf.endsWith("/pump")) {
          togglePin(PUMP_PIN);
        } 

        if (buf.endsWith("/phUp")) {
          togglePin(PH_DOWN_PIN, LOW);
          togglePin(PH_UP_PIN, HIGH);
          timer.in(PUMP_INTERVAL, disablePH);
        }

        if (buf.endsWith("/phDown")) {
          togglePin(PH_UP_PIN, LOW);
          togglePin(PH_DOWN_PIN, HIGH);
          timer.in(PUMP_INTERVAL, disablePH);
        }

        if (buf.endsWith("/ecUp")) {
          togglePin(EC_DOWN_PIN, LOW);
          togglePin(EC_UP_PIN, HIGH);
          timer.in(PUMP_INTERVAL, disableEC);
        }

        if (buf.endsWith("/ecDown")) {
          togglePin(EC_UP_PIN, LOW);
          togglePin(EC_DOWN_PIN, HIGH);
          timer.in(PUMP_INTERVAL, disableEC);
        }

        if (buf.endsWith("/ph")) {
          disablePH();
        }

        if (buf.endsWith("/ec")) {
          disableEC();
        }

        if (buf.endsWith("/components")) {
          message = "{\n  \"PHPump\": \"" + String(digitalRead(PH_UP_PIN)) + "\",\n \"Light\": \"" + String(digitalRead(LIGHT_PIN)) +  "\",\n  \"ECPump\": \"" + String(EC_UP_PIN) + "\",\n  \"WaterPump\": \"" + String(digitalRead(PUMP_PIN)) + "\",\n  \"Exctractor\": \"" + String(digitalRead(EXTRACTOR_PIN)) + "\",\n  \"Fan\": \"" + String(digitalRead(FAN_PIN)) +  "\"\n }"; 
        }
      }
    }
    Serial.println("Client disconnected");
    client.stop();  // Close the connection with the client
  }
}


  /**
  * Inverts the reading of a pin.
  */
void togglePin(int pin) {
  digitalWrite(pin, !(digitalRead(pin)));
}

void togglePin(int pin, int toggleValue) {
  digitalWrite(pin, toggleValue);
}


bool timeOnPump(){    // allows us to use a timer on any
  Serial.println("⏰ Pump ON");         // printing the status
  digitalWrite(PUMP_PIN, LOW);  // turning on the required component
  timer.in(FIVE_MIN,timeOffPump );          // setting a timer to schedule turn off
  return false;  // Don't repeat automatically
}

bool timeOffPump() {
  Serial.println("⏰ Pump off");      // printing the status
  digitalWrite(PUMP_PIN, HIGH);   // 
  timer.in((EIGHT_HR-FIVE_MIN), timeOnPump);        // this will  be used to schedule for timer on    
  return false;  // Don't repeat automatically
}
  /**
  * Sends a http response along with a message.
  */
void sendHttpResponse(WiFiEspClient client, String message) {
    client.print(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Connection: close\r\n");

  if (message.length() > 0) {
    client.print("Content-Length:" + String(message.length()) + "\r\n\r\n");
    Serial.print(message.length());
    client.print(message);
  }
}

float getLightLevel() {
  return analogRead(LIGHT_PIN);
}

float getEC() {
  float ecVoltage = (float)analogRead(EC_PIN)/1024.0*5000.0; 
  return ec.readEC(ecVoltage, temperature);
}

float getPH() {
  float phVoltage = analogRead(PH_PIN)/1024.0*5000; 
  return ph.readPH(phVoltage, temperature);
}

void setComponent(int result, int pin, int status){
    if (result == 0) { // Below Optimal
    if (status == 1){
    digitalWrite(pin, LOW);
    //Serial.println("FAN offfffff");
    } 
  }
  else if (result == 1) { // Above Optimal
    if (status == 0){
      digitalWrite(pin, HIGH);
      //Serial.println("FAN ON");
    } 
  }
  else {
     if (status == 0){ //Optimal 
      Serial.println("Component: "+ digitalRead(pin));
      togglePin(pin);
      //Serial.println("COMPONENT OFF!!!!!!!");    
    }
  }
}

void setPump(int result, int pinUp, int pinDown, int statusUp,int statusDown){
    if (result == 0) { //Below Optimal
    if (statusUp == 1 || statusDown == 0){
    digitalWrite(pinUp, LOW);
    digitalWrite(pinDown, HIGH);
    Serial.println("pump up offfffff");
    } 
  }
  else if (result == 1) { //Above Optimal
    if (statusUp == 0 || statusDown == 1){
      digitalWrite(pinUp, HIGH);
      digitalWrite(pinDown, LOW);
      Serial.println("pump down on");
    } 
  }
  else {
     
      Serial.println("Component: " + digitalRead(pinUp));
      Serial.println("Component: " + digitalRead(pinDown));
      
      togglePin(pinUp, HIGH);
      togglePin(pinDown, HIGH);
      Serial.println("COMPONENTS OFF!");    
    
  }
}

void estimateTemperature() {
  if(temperature != NAN){
    int result = ForestTemperature.predict(&temperature);
    int fanStatus = digitalRead(FAN_PIN);
    int lightStatus = digitalRead(LIGHT_PIN);
    Serial.println(result);

    setComponent(result, FAN_PIN, fanStatus);
  }
  
}

void estimateHumidity() {
  if(humidity != NAN){
    int result = ForestHumidity.predict(&humidity);
    int extractorStatus = digitalRead(EXTRACTOR_PIN);

    setComponent(result, EXTRACTOR_PIN, extractorStatus);
  }
}

void estimatePH() {
  if(phLevel != NAN){
    int result = ForestPH.predict(&phLevel);
    int phUpStatus = digitalRead(PH_UP_PIN);
    int phDownStatus = digitalRead(PH_DOWN_PIN);

    setPump(result, PH_UP_PIN, PH_DOWN_PIN, phUpStatus, phDownStatus);
    timer.in(PUMP_INTERVAL, disablePH);
  }
}

void estimateEC() {
  if(ecLevel != NAN){
    int result = ForestEC.predict(&ecLevel);
    int ecUpStatus = digitalRead(EC_UP_PIN);
    int ecDownStatus = digitalRead(EC_DOWN_PIN);

    setPump(result, EC_UP_PIN, EC_DOWN_PIN, ecUpStatus, ecDownStatus);
    timer.in(PUMP_INTERVAL, disableEC);
  }
  
}

void estimateFactors() {
  estimatePH();
  estimateTemperature();
  estimateHumidity();
  estimateEC();
}

void disablePH() {
  digitalWrite(PH_UP_PIN, HIGH);
  digitalWrite(PH_DOWN_PIN, HIGH);
}

void disableEC() {
  digitalWrite(EC_UP_PIN, HIGH);
  digitalWrite(EC_DOWN_PIN, HIGH);
  Serial.println("EC HIT");
}

void incrementPulseCounter() {
  pulseCount++;
}

float getFlowRate() {
  currentTime = millis();

  if (currentTime >= (cloopTime + 1000)) {
    cloopTime = currentTime;
    float flowRatePerHr = (pulseCount * 60 / 7.5);
    pulseCount = 0;
    return flowRatePerHr;
  }
}

void toggleLightOn() {
  togglePin(LED_PIN, LOW);
  timer.in(EIGHT_HR, toggleLightOff);
}

void toggleLightOff() {
  togglePin(LED_PIN, HIGH);
  timer.in(FOUR_HR, toggleLightOn);
}
 bool checkLightLevelBelow() {
    float currentLightLevel = getLightLevel();
    bool lowSunlightLevel = currentLightLevel <= MAX_LIGHT_READING_LUX * MINIMIM_LIGHT_PERCENTAGE;

  if (lowSunlightLevel) {
      toggleLightOn();
    }
    else {
      toggleLightOff();
    }
   
  }

/* this will read data from the esp32 via the rx/tx wires
* 1 means toggle, 0 means the state stays as is
*/
 void readRemoteToggle() {
  static bool inFrame = false;
  static uint8_t index = 0;
  char c;
  
  while (Serial1.available()) {

    c = Serial1.read();
    Serial.println(c);
    Serial.println("We are here");
    if (c == '<') {
      Serial.println("We found <");
      inFrame = true;
      index = 0;
      continue;
    } 
    else if (c == '>') {
      Serial.println("closing off");
      serialBuf[index] = '\0';
      inFrame = false;
      
      // Only process if JSON looks complete
      if (strchr(serialBuf, '{') && strchr(serialBuf, '}')) {
        processToggle(serialBuf);
      } else {
        Serial.println("⚠️ Incomplete JSON received, ignoring");
      }

      index = 0;
      continue;
    }

    if (inFrame && index < SERIAL_BUF_SIZE - 1) {
      serialBuf[index++] = c;
    }
  }
}

void processToggle(char* data) {
  String s = String(data);
  Serial.println(data);
   if (s.indexOf("\"Light\":1") >= 0) togglePin(LED_PIN);
  
  // Fan
  if (s.indexOf("\"Fan\":1") >= 0) togglePin(FAN_PIN);
  
  // Extractor Fan
  if (s.indexOf("\"ExtractorFan\":1") >= 0) togglePin(EXTRACTOR_PIN);
  
  // Pump
  if (s.indexOf("\"Pump\":1") >= 0){
     togglePin(PUMP_PIN);
      
  }

   if (s.indexOf("\"PHUp\":1") >= 0) {
  togglePin(PH_DOWN_PIN, LOW);
  togglePin(PH_UP_PIN, HIGH);
  timer.in(PUMP_INTERVAL, disablePH);
}

if (s.indexOf("\"PHDown\":1") >= 0) {
  togglePin(PH_UP_PIN, LOW);
  togglePin(PH_DOWN_PIN, HIGH);
  timer.in(PUMP_INTERVAL, disablePH);
}

if (s.indexOf("\"ECUp\":1") >= 0) {
  togglePin(EC_DOWN_PIN, LOW);
  togglePin(EC_UP_PIN, HIGH);
  timer.in(PUMP_INTERVAL, disableEC);
}

if (s.indexOf("\"ECDown\":1") >= 0) {
  togglePin(EC_UP_PIN, LOW);
  togglePin(EC_DOWN_PIN, HIGH);
  timer.in(PUMP_INTERVAL, disableEC);
}

}


 String extractValue(String data, String key) {
  int start = data.indexOf("\"" + key + "\"");
  if (start == -1) return "";

  start = data.indexOf(":", start);
  if (start == -1) return "";

  int firstQuote = data.indexOf("\"", start + 1);
  int secondQuote = data.indexOf("\"", firstQuote + 1);
  if (firstQuote == -1 || secondQuote == -1) return "";

  return data.substring(firstQuote + 1, secondQuote);
}



