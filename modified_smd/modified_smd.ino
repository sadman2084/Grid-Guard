#include <Wire.h>
#include <Adafruit_INA219.h>
#include <DFRobotDFPlayerMini.h>
#include <SoftwareSerial.h>

// -------------------- PIN SETUP --------------------
#define RELAY_PIN 8          
#define TOUCH_PIN A0        
#define DFPLAYER_RX 10       
#define DFPLAYER_TX 11       

#define ADC_PIN A3         
#define PWM_PIN 3           
#define HALL1_PIN A1
#define HALL2_PIN A2

// -------------------- I2C SETUP --------------------
#define SLAVE_ADDRESS 8      

// -------------------- OBJECTS -----------------------
Adafruit_INA219 ina219;
SoftwareSerial dfSerial(DFPLAYER_RX, DFPLAYER_TX);
DFRobotDFPlayerMini dfPlayer;

// -------------------- VARIABLES ---------------------
volatile bool uidReceivedFlag = false;
String uidBuffer = "";
String receivedUID = "";

// permanent UID (first time set)
String permanentUID = "";
bool uidLocked = false;     

float balance = 0.0;

unsigned long lastSendTime = 0;

// Cable measurement variables
const float Vref = 5.0;
const int R_known = 10;
const int samples = 50;

float R_base=0, C_base=0, L_base=0;
float R_now=0, C_now=0, L_now=0;

// -------------------- RECEIVE UID -------------------
void receiveUID(int len) {
  uidBuffer = "";
  while (Wire.available()) {
    uidBuffer += (char)Wire.read();
  }
  uidReceivedFlag = true;
}

// -------------------- UID HANDLING ------------------
void handleUID() {
  receivedUID = uidBuffer;
  receivedUID.trim();
  receivedUID.toLowerCase();

  Serial.print("Received UID: ");
  Serial.println(receivedUID);

  if (!uidLocked) {
    permanentUID = receivedUID;

    // Print UID 2 times with * and #
    for (int i = 0; i < 2; i++) {
      Serial.print("*");
      Serial.print(permanentUID);
      Serial.println("#");
      delay(1000);
    }

    uidLocked = true;
    Serial.println("Permanent UID Set!");
    dfPlayer.play(1);    // “WELCOME” voice
  } 
  else {
    Serial.println("UID Received but permanent already set");
  }
}

// ----------------- SETUP -----------------
void setup() {
  Serial.begin(9600);
  Wire.onRequest(sendDataToMaster);

  Wire.begin(SLAVE_ADDRESS);
  Wire.onReceive(receiveUID);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(PWM_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);   // initially OFF

  // INA219 init
  if (!ina219.begin()) Serial.println("INA219 not found!");

  // DFPlayer init
  dfSerial.begin(9600);
  if (!dfPlayer.begin(dfSerial)) Serial.println("DFPlayer not detected!");
  else {
    dfPlayer.setTimeOut(500);
    dfPlayer.volume(25);
  }

  // Take baseline
  measureCableBaseline();

  Serial.println("System Ready...");
}

// ----------------- MAIN LOOP -----------------
void loop() {

  // NEW UID RECEIVED
  if (uidReceivedFlag) {
    uidReceivedFlag = false;
    handleUID();
  }

  // If UID not set → NO POWER ALLOWED
  if (!uidLocked) {
    digitalWrite(RELAY_PIN, HIGH);
    return;
  }

  
  // Receive balance from ESP32
  if (Serial.available()) {  
    String receivedData = Serial.readStringUntil('#');
    receivedData.trim();

    if (receivedData[0] == '@') {
      String temp = receivedData;
      temp.replace("@", "");
      Serial.println("Balance from ESP32: " + String(temp));
      balance = temp.toFloat();
    }
  }

  // If balance 0 → NO POWER
  if (balance <= 0) {
    digitalWrite(RELAY_PIN, HIGH);
    return;
  }

  // ---------------- If UID locked and balance > 0 ----------------
  measureCable(R_now, C_now, L_now);

  int hall1_val = analogRead(HALL1_PIN);
  int hall2_val = analogRead(HALL2_PIN);

  Serial.print("R: "); Serial.print(R_now,3); Serial.print(" Ohm  ");
  Serial.print("C: "); Serial.print(C_now*1e6,3); Serial.print(" uF  ");
  Serial.print("L: "); Serial.print(L_now*1e6,3); Serial.print(" uH  ");
  Serial.print("H1: "); Serial.print(hall1_val); Serial.print(" H2: "); Serial.println(hall2_val);

  // Touch → OFF
  int touchValue = analogRead(TOUCH_PIN);
  if (touchValue <= 600) {
    Serial.println("Touch detected → Power OFF");
    dfPlayer.play(2);
    digitalWrite(RELAY_PIN, HIGH);
    return;
  }

  // Capacitance Over Limit
  if (C_now*1e6 > 100) {
    Serial.println("Capacitance High → Power OFF");
    digitalWrite(RELAY_PIN, HIGH);
    return;
  }

  // Normal Condition → POWER ON
  digitalWrite(RELAY_PIN, LOW);

  // INA219
  float busV = ina219.getBusVoltage_V();
  float current = ina219.getCurrent_mA() / 1000.0;
  float powerW = busV * current;

  // Send every 1 sec
  if (millis() - lastSendTime > 1000) {

    Serial.print("Voltage: "); Serial.print(busV);
    Serial.print(" V, Current: "); Serial.print(current);
    Serial.print(" A, Power: "); Serial.print(powerW);
    Serial.println(" W");

    String output = String(busV) + ";" + String(current) + ";" + String(powerW);
    for (int i = 0; i < 2; i++) {
      Serial.print("$");
      Serial.print(output);
      Serial.println("#");
      delay(1000);
    }

    lastSendTime = millis();
  }


  delay(100);
}

// ----------------- CABLE FUNCTIONS -----------------
void measureCableBaseline() {
  measureCable(R_base, C_base, L_base);
}

void measureCable(float &R, float &C, float &L) {

  float V_sum = 0;
  for(int i=0;i<samples;i++){
    V_sum += analogRead(ADC_PIN);
    delay(1);
  }

  float V_avg = V_sum / samples;
  float V = V_avg / 1023.0 * Vref;
  float Vsource = Vref;
  R = R_known * V / (Vsource - V);

  unsigned long t_start = micros();
  analogWrite(PWM_PIN, 255);
  unsigned long timeout = micros() + 2000;
  while(analogRead(ADC_PIN) < (int)(0.63*V_avg*1023/Vref) && micros() < timeout);
  unsigned long t_rise = micros() - t_start;
  C = (float)t_rise / R;

  analogWrite(PWM_PIN, 0);
  timeout = micros() + 2000;
  unsigned long t_fall_start = micros();
  while(analogRead(ADC_PIN) > (int)(0.37*V_avg*1023/Vref) && micros() < timeout);
  unsigned long t_fall = micros() - t_fall_start;
  L = (float)t_fall * R;

  C = C * 1e-6;
  L = L * 1e-6;
}

// ------------- I2C data to ESP32 -----------------
void sendDataToMaster() {
  String sendData = permanentUID + "," + String(ina219.getBusVoltage_V(),2)
                    + "," + String(ina219.getCurrent_mA()/1000.0,3)
                    + "," + String(ina219.getBusVoltage_V()*(ina219.getCurrent_mA()/1000.0),3)
                    + "," + String(digitalRead(RELAY_PIN));

  Wire.write(sendData.c_str());
}
