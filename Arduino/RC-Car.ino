#include <TimerOne.h>     // Motor PWM Timer1 Pin9 Pin10
#include <ServoTimer2.h>  // Servo Timer2 Pin3 Pin11

#define steeringPin 11
#define motorPin2 10
#define motorPin1 9
#define rightBlinkerPin 8
#define leftBlinkerPin 7
#define frontLightPin 6
#define tailLightPin 5
#define reverseLightPin 4
#define btStatePin 2
#define lightSensorPin A0

const unsigned int accelRate = 8;           // höher -> stärkere Beschleunigung
const unsigned int decelRateConst = 16;     // geringer -> stärkeres Bremsen

const byte MAX_LEN = 8;                     // BT-Input Arraylänge (max Anzahl Zeichen pro Befehl)
const unsigned int brakeLightTrigger = 5;   // Bremswert ab dem Bremslicht an geht
const unsigned int blinkInterval = 500;     // Blinkergeschwindigkeit (millis)
const unsigned int speedInterval = 20;      // Geschwindgkeit Updateperiodenzeit (millis)

// Bluetoothverbindung
bool btState = false;
bool lastBtState = false;
bool firstCycleAfterConnect = false;

// Bluetoothinput
char input[MAX_LEN];
byte idx = 0;

ServoTimer2 servo;

bool forward = true;        // Fahrtrichtung
bool lowBeam = false;       // Abblendlicht
bool highBeam = false;      // Fernlicht
bool flash = false;         // Lichthupe
bool autoLight = false;     // automatisches Abblendlicht
bool leftBlinker = false;   // linker Blinker
bool rightBlinker = false;  // rechter Blinker
bool blinker = false;       // Hilfsvariable für Blinkermethode

int brakeVal = 0;       // LT-Wert
int targetSpeed = 0;    // RT-Wert
int steeringVal = 127;  // LX-Wert
int decelRate = 0;      // Entschleunigungswert
int currentSpeed = 0;   // tatsächliche Motorgeschwindigkeit

int lightSensorVal;

unsigned long now; // = millis()
unsigned long lastSpeedUpdate;
unsigned long lastBlinkUpdate;

void setup() {
  Serial.begin(9600);
  pinMode(steeringPin, OUTPUT);
  pinMode(motorPin2, OUTPUT);
  pinMode(motorPin1, OUTPUT);
  pinMode(rightBlinkerPin, OUTPUT);
  pinMode(leftBlinkerPin, OUTPUT);
  pinMode(frontLightPin, OUTPUT);
  pinMode(tailLightPin, OUTPUT);
  pinMode(reverseLightPin, OUTPUT);
  pinMode(btStatePin, INPUT);

  servo.attach(steeringPin);
  servo.write(1450); // links = 1200; mitte = 1450; rechts = 1700 (microseconds)

  Timer1.initialize(50); // 50 microseconds -> 20 kHz Motor-PWM
  Timer1.pwm(motorPin2, 0);
  Timer1.pwm(motorPin1, 0);
}

void loop() {
  now = millis();
  btState = digitalRead(btStatePin);

  // Bei BT-Verbindungsaufbau Warnblinker aus
  if (firstCycleAfterConnect) {
    leftBlinker = false;
    rightBlinker = false;
    firstCycleAfterConnect = false;
  }

  if (btState) { // Wenn BT verbunden
    if (!lastBtState) {
      firstCycleAfterConnect = true;
    }
    if (Serial.available()) {
    handleSerial();
    }
    if (now - lastSpeedUpdate >= speedInterval) {
      lastSpeedUpdate = now;
      updateSpeed();
    }
  } else { // Wenn BT nicht verbunden/abgebrochen -> anhalten + Warblinker an
    Timer1.setPwmDuty(motorPin2, 0);
    Timer1.setPwmDuty(motorPin1, 0);
    servo.write(1450);
    leftBlinker = true;
    rightBlinker = true;
    brakeVal = 0;
    targetSpeed = 0;
    currentSpeed = 0;
    steeringVal = 127;
  }

  // Blinker aktualisieren
  if (now - lastBlinkUpdate >= blinkInterval) {
    lastBlinkUpdate = now;
    updateBlinker();
  }

  // automatisches Abblendlicht steuern
  if (autoLight) {
    updateLight();
  }

  lastBtState = btState;
}

// BT-Inputs in Commands umwandeln
void handleSerial() {
  char c = Serial.read();
  if (c == '\n') {
    input[idx] = '\0'; // Array -> C-String
    processCommand(input);
    idx = 0;
  } else {
    if (idx < MAX_LEN-1) {
      input[idx++] = c;
    }
  }
}

// BT-Commands verarabeiten
void processCommand(const char* cmd) {
  if (strncmp(cmd, "LT", 2) == 0) { // cmd = "LTxx" = Bremse
    processLT(cmd);
    return;
  }

  if (strncmp(cmd, "RT", 2) == 0) { // cmd = "RTxx" = Gas
    processRT(cmd);
    return;
  }

  if (strncmp(cmd, "LX", 2) == 0) { // cmd = "LXxx" = Lenkung
    processLX(cmd);
    return;
  }

  if (strlen(cmd) == 1) { // cmd = "x" = digitale Buttons
    char btn = cmd[0];
    switch (btn) {
      case 'A': break; // unbelegt
      case 'B': break; // unbelegt
      case 'X': processX(); break; // Lichthupe an (X-Knopf)
      case 'Z': processZ(); break; // Lichthupe aus (X-Knopf loslassen)
      case 'Y': processY(); break; // Fahrtrichtung (Y-Knopf)
      case 'u': processu(); break; // Fernlicht (Steuerkreuz oben)
      case 'r': processr(); break; // Abblendlicht (Steuerkreuz rechts)
      case 'd': processd(); break; // Warnblinker (Steuerkreuz unten)
      case 'l': processl(); break; // Abblendlichtautomatik (Steuerkreuz links)
      case 'L': processL(); break; // Blinker links (linker Schulterknopf)
      case 'R': processR(); break; // Blinker rechts (rechter Schulterknopf)
    }
  }
  return;
}

void processLT(const char* cmd) {
  brakeVal = strtol(cmd+2, NULL, 16); // Bremswert: 00-FF -> 0-255

  if (brakeVal >= brakeLightTrigger) {
    digitalWrite(tailLightPin, HIGH);
  } else if (lowBeam) {
    analogWrite(tailLightPin, 55);
  } else {
    digitalWrite(tailLightPin, LOW);
  }
  return;
}

void processRT(const char* cmd) {
  targetSpeed = strtol(cmd+2, NULL, 16); // Gaswert: 00-FF -> 0-255
  return;
}

void processLX(const char* cmd) {
  steeringVal = strtol(cmd+2, NULL, 16); // Lenkwert: 00-FF -> 0-255
  
  int steeringMicroSec = map(steeringVal, 0, 255, 1200, 1700); // 0-255 -> 1200-1700
  servo.write(steeringMicroSec);
  return;
}

void processX() {
  flash = true;

  digitalWrite(frontLightPin, HIGH);
  return;
}

void processZ() {
  flash = false;

  if (!highBeam) {
    if (lowBeam) {
      analogWrite(frontLightPin, 55);
    } else {
      digitalWrite(frontLightPin, 0);
    }
  }
  return;
}

void processY() {
  if (currentSpeed == 0) {
    forward = !forward;
  }

  if (forward) {
    digitalWrite(reverseLightPin, LOW);
  } else {
    digitalWrite(reverseLightPin, HIGH);
  }
  return;
}

void processu() {
  highBeam = !highBeam;

  if (!autoLight && lowBeam) {
    if (highBeam) {
      digitalWrite(frontLightPin, HIGH);
    } else {
      analogWrite(frontLightPin, 55);
    }
  }
  return;
}

void processr() {
  lowBeam = !lowBeam;

  if (!autoLight) {
    if (lowBeam) {
      if (highBeam) {
        digitalWrite(frontLightPin, HIGH);
      } else {
        analogWrite(frontLightPin, 55);
      }
      if (brakeVal >= brakeLightTrigger) {
        digitalWrite(tailLightPin, HIGH);
      } else {
        analogWrite(tailLightPin, 55);
      }
    } else {
      digitalWrite(frontLightPin, LOW);
      if (brakeVal >= brakeLightTrigger) {
        digitalWrite(tailLightPin, HIGH);
      } else {
        digitalWrite(tailLightPin, LOW);
      }
    }
  }
  return;
}

void processd() {
  if (leftBlinker && rightBlinker) {
    leftBlinker = false;
    rightBlinker = false;
  } else {
    leftBlinker = true;
    rightBlinker = true;
  }
  return;
}

void processl() {
  autoLight = !autoLight;

  processr();

  return;
}

void processL() {
  if (!(leftBlinker && rightBlinker)) {
    rightBlinker = false;
    leftBlinker = !leftBlinker;
  }
  return;
}

void processR() {
  if (!(leftBlinker && rightBlinker)) {
    leftBlinker = false;
    rightBlinker = !rightBlinker;
  }
  return;
}

void updateSpeed() {
  decelRate = max((brakeVal/decelRateConst), 1);
  
  if (currentSpeed < targetSpeed) {
    currentSpeed = min(currentSpeed + accelRate, targetSpeed); // beschleunigen
  } else if (currentSpeed > targetSpeed) {
    currentSpeed = max(currentSpeed - decelRate, targetSpeed); // entschleunigen
  }

  int duty = currentSpeed*4; // 0-255 -> 0-1020 (Timer1 erwartet 0 - 1023)

  // Motor ansprechen
  if (forward) {
    Timer1.setPwmDuty(motorPin2, 0);
    Timer1.setPwmDuty(motorPin1, duty);
  } else {
    Timer1.setPwmDuty(motorPin1, 0);
    Timer1.setPwmDuty(motorPin2, duty);
  }
  return;
}

void updateBlinker() {
  blinker = !blinker; // wechselt im Blinkerintervall

  // linker Blinker aktualisieren
  if (leftBlinker && blinker) {
    digitalWrite(leftBlinkerPin, HIGH);
  } else {
    digitalWrite(leftBlinkerPin, LOW);
  }

  // rechter Blinker aktualisieren
  if (rightBlinker && blinker) {
    digitalWrite(rightBlinkerPin, HIGH);
  } else {
    digitalWrite(rightBlinkerPin, LOW);
  }
  return;
}

void updateLight() {
  lightSensorVal = analogRead(lightSensorPin);
  
  if (lightSensorVal < 768) { // wenn Umgebung dunkel
    if (highBeam || flash) {
      digitalWrite(frontLightPin, HIGH);
    } else {
      analogWrite(frontLightPin, 55);
    }
    if (brakeVal >= brakeLightTrigger) {
      digitalWrite(tailLightPin, HIGH);
    } else {
      analogWrite(tailLightPin, 55);
    }
  } else { // wenn Umgebung hell
    if (!flash) {
      digitalWrite(frontLightPin, LOW);
    }
    if (brakeVal >= brakeLightTrigger) {
      digitalWrite(tailLightPin, HIGH);
    } else {
      digitalWrite(tailLightPin, LOW);
    }
  }
  return;
}