#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int btnTemp = 3;
const int btnVel = 4;
const int btnGPS = 5;
const int btnProf = 6;

const int btnUp = 7;
const int btnDown = 10;

const int NMEA_RX_PIN = 20;
const int NMEA_TX_PIN = 21;

HardwareSerial NMEASerial(1);

const uint32_t NMEA_BAUDS[] = {1200, 4800, 9600, 19200, 38400, 57600, 115200};
const uint8_t NMEA_BAUD_COUNT = sizeof(NMEA_BAUDS) / sizeof(NMEA_BAUDS[0]);
uint8_t nmeaBaudIndex = 1;

uint8_t nmeaDataBits = 8;
uint8_t nmeaParity = 0;
uint8_t nmeaStopBits = 1;

uint8_t txBaudIndex = 1;
uint8_t txDataBits = 8;
uint8_t txParity = 0;
uint8_t txStopBits = 1;
uint8_t nmeaConfigField = 0;
uint8_t txConfigField = 0;

bool nmeaReadMode = false;
bool nmeaTxConfigMode = false;
unsigned long rxComboStart = 0, txComboStart = 0;
bool rxComboTriggered = false, txComboTriggered = false;

String nmeaRxLine = "";
String lastNMEALine = "Waiting for NMEA...";

float value = 10.0;
String mode = "TEMPERATURA";
int profMode = 0;
int gpsMode = 0;
int velMode = 0;
bool lastProfButtonState = HIGH;
bool lastGPSButtonState = HIGH;
bool lastVelButtonState = HIGH;

bool lastUpButtonState = HIGH;
bool lastDownButtonState = HIGH;

unsigned long lastUpPress = 0;
unsigned long lastDownPress = 0;
bool fastUp = false;
bool fastDown = false;

unsigned long pressStartUp = 0;
unsigned long pressStartDown = 0;
bool holdingUp = false;
bool holdingDown = false;

uint32_t getNMEASerialConfig();
char getNMEAParityChar();
void handleNMEAConfigButtons();
void startNMEAReceiver();
void stopNMEAReceiver();
void restartNMEASerial();
void readNMEA();
void showNMEAScreen();
void showNMEATxConfigScreen();
void handleNMEATxConfigButtons();
void applyActiveUART();
uint32_t buildSerialConfig(uint8_t dataBits, uint8_t parity, uint8_t stopBits);
char parityChar(uint8_t parity);
void showModeNotice(const char *title);

void setup() {

  Serial.begin(4800);

  NMEASerial.begin(NMEA_BAUDS[txBaudIndex], buildSerialConfig(txDataBits, txParity, txStopBits), NMEA_RX_PIN, NMEA_TX_PIN);

  Wire.begin(8, 9);

  if (!display.begin(OLED_ADDRESS, true)) {
    Serial.println("ERROR: OLED SH1106 init failed");
    while (true) {
      delay(100);
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 25);
  display.println("Starting, please");
  display.println("wait...");
  display.display();
  delay(1000);

  pinMode(btnTemp, INPUT_PULLUP);
  pinMode(btnVel, INPUT_PULLUP);
  pinMode(btnGPS, INPUT_PULLUP);
  pinMode(btnProf, INPUT_PULLUP);
  pinMode(btnUp, INPUT_PULLUP);
  pinMode(btnDown, INPUT_PULLUP);

}

void loop() {
  unsigned long currentTime = millis();

  const bool tempPressed = digitalRead(btnTemp) == LOW;
  const bool profPressed = digitalRead(btnProf) == LOW;
  const bool gpsPressed = digitalRead(btnGPS) == LOW;
  const bool velPressed = digitalRead(btnVel) == LOW;
  const bool rxCombo = tempPressed && profPressed && !gpsPressed && !velPressed;
  const bool txCombo = gpsPressed && velPressed && !tempPressed && !profPressed;

  if (rxCombo) {
    if (!rxComboStart) { rxComboStart = currentTime; rxComboTriggered = false; }
    if (!rxComboTriggered && currentTime - rxComboStart >= 1000) {
      rxComboTriggered = true;
      nmeaReadMode = !nmeaReadMode;
      nmeaTxConfigMode = false;
      applyActiveUART();
      showModeNotice(nmeaReadMode ? "NMEA RX ON" : "NMEA RX OFF");
    }
  } else { rxComboStart = 0; rxComboTriggered = false; }

  if (txCombo) {
    if (!txComboStart) { txComboStart = currentTime; txComboTriggered = false; }
    if (!txComboTriggered && currentTime - txComboStart >= 1000) {
      txComboTriggered = true;
      nmeaTxConfigMode = !nmeaTxConfigMode;
      nmeaReadMode = false;
      applyActiveUART();
      showModeNotice(nmeaTxConfigMode ? "NMEA TX SETUP ON" : "NMEA TX SETUP OFF");
    }
  } else { txComboStart = 0; txComboTriggered = false; }

  if (rxCombo || txCombo) {
    if (nmeaReadMode) { readNMEA(); showNMEAScreen(); }
    else if (nmeaTxConfigMode) showNMEATxConfigScreen();
    delay(10);
    return;
  }

  if (nmeaReadMode) {
    handleNMEAConfigButtons();
    readNMEA();
    showNMEAScreen();
    delay(10);
    return;
  }

  if (nmeaTxConfigMode) {
    handleNMEATxConfigButtons();
    showNMEATxConfigScreen();
    delay(10);
    return;
  }

  if (digitalRead(btnTemp) == LOW) mode = "TEMPERATURA";

  bool velButton = digitalRead(btnVel);
  if (velButton == LOW && lastVelButtonState == HIGH) {
    velMode = (velMode + 1) % 3;
    mode = "VELOCIDAD";
    delay(150);
  }
  lastVelButtonState = velButton;

  bool gpsButton = digitalRead(btnGPS);
  if (gpsButton == LOW && lastGPSButtonState == HIGH) {
    gpsMode = (gpsMode + 1) % 3;
    mode = "GPS";
    delay(150);
  }
  lastGPSButtonState = gpsButton;

  bool profButton = digitalRead(btnProf);
  if (profButton == LOW && lastProfButtonState == HIGH) {
    profMode = (profMode + 1) % 3;
    mode = "PROFUNDIDAD";
    delay(150);
  }
  lastProfButtonState = profButton;

  if (digitalRead(btnUp) == LOW) {
    if (!holdingUp) {
      pressStartUp = currentTime;
      holdingUp = true;
      fastUp = (currentTime - lastUpPress < 400);
      lastUpPress = currentTime;
      value += fastUp ? 1.0 : 0.1;
    } else {
      if (currentTime - pressStartUp > 1500) {
        value += 1;
        delay(100);
      }
    }
    if (value > 100) value = 100;
  } else {
    holdingUp = false;
  }

  if (digitalRead(btnDown) == LOW) {
    if (!holdingDown) {
      pressStartDown = currentTime;
      holdingDown = true;
      fastDown = (currentTime - lastDownPress < 400);
      lastDownPress = currentTime;
      value -= fastDown ? 1.0 : 0.1;
    } else {
      if (currentTime - pressStartDown > 1500) {
        value -= 1;
        delay(100);
      }
    }
    if (value < 0) value = 0;
  } else {
    holdingDown = false;
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Mode:");
  if (mode == "PROFUNDIDAD") {
    if (profMode == 0) display.println("DEPTH DBK");
    else if (profMode == 1) display.println("DEPTH DBT");
    else if (profMode == 2) display.println("DEPTH DBS");
  }
  else if (mode == "GPS") {
    if (gpsMode == 0) display.println("GPS");
    else if (gpsMode == 1) display.println("MAG HEADING");
    else if (gpsMode == 2) display.println("TRUE HEADING");
  }
  else if (mode == "VELOCIDAD") {
    if (velMode == 0) display.println("LOG SPEED");
    else if (velMode == 1) display.println("REL WIND SPEED");
    else if (velMode == 2) display.println("TRUE WIND SPEED");
  }
  else {
    display.println(mode == "TEMPERATURA" ? "TEMPERATURE" : mode);
  }

  display.setCursor(0, 15);
  display.print("Value:");
  display.print(value, 1);

  display.setCursor(0, 30);
  display.print("Unit:");
  if (mode == "TEMPERATURA") display.print("Celsius");
  else if (mode == "GPS") {
    if (gpsMode == 0) display.println("Lat/Long");
    else display.println("Degrees");
  }
  else if (mode == "PROFUNDIDAD") display.print("Meters");
  else display.print("Knots");

  String msg = generateNMEAMessage(mode, value, profMode, gpsMode, velMode);
  NMEASerial.print(msg);
  Serial.print(msg);
  display.setCursor(0, 45);
  display.print(msg);

  uint8_t msgBytes[100];
  msg.getBytes(msgBytes, sizeof(msgBytes));
  uint8_t len = strlen((char*)msgBytes);

  uint8_t cs1 = msgBytes[len - 4];
  uint8_t cs2 = msgBytes[len - 3];
  bool csOK = nmeaChecksumCompare(msgBytes, len - 1, cs1, cs2);

  Serial.print("Checksum OK? ");
  Serial.println(csOK ? "YES" : "NO");

  display.display();
  delay(200);
}

void handleNMEAConfigButtons() {
  static bool lastTempState = HIGH;
  static bool lastVelState = HIGH;
  static bool lastGPSState = HIGH;
  static bool lastProfState = HIGH;
  static bool lastCfgUpState = HIGH;
  static bool lastCfgDownState = HIGH;

  bool tempState = digitalRead(btnTemp);
  bool velState = digitalRead(btnVel);
  bool gpsState = digitalRead(btnGPS);
  bool profState = digitalRead(btnProf);
  bool upState = digitalRead(btnUp);
  bool downState = digitalRead(btnDown);

  if (profState == LOW && lastProfState == HIGH) nmeaConfigField = 0;
  if (gpsState  == LOW && lastGPSState  == HIGH) nmeaConfigField = 1;
  if (velState  == LOW && lastVelState  == HIGH) nmeaConfigField = 2;
  if (tempState == LOW && lastTempState == HIGH) nmeaConfigField = 3;

  bool changed = false;

  if (upState == LOW && lastCfgUpState == HIGH) {
    switch (nmeaConfigField) {
      case 0:
        nmeaBaudIndex = (nmeaBaudIndex + 1) % NMEA_BAUD_COUNT;
        break;
      case 1:
        nmeaDataBits = (nmeaDataBits == 8) ? 7 : 8;
        break;
      case 2:
        nmeaParity = (nmeaParity + 1) % 3;
        break;
      case 3:
        nmeaStopBits = (nmeaStopBits == 1) ? 2 : 1;
        break;
    }
    changed = true;
  }

  if (downState == LOW && lastCfgDownState == HIGH) {
    switch (nmeaConfigField) {
      case 0:
        if (nmeaBaudIndex > 0) nmeaBaudIndex--;
        else nmeaBaudIndex = NMEA_BAUD_COUNT - 1;
        break;
      case 1:
        nmeaDataBits = (nmeaDataBits == 8) ? 7 : 8;
        break;
      case 2:
        if (nmeaParity == 0) nmeaParity = 2;
        else nmeaParity--;
        break;
      case 3:
        nmeaStopBits = (nmeaStopBits == 1) ? 2 : 1;
        break;
    }
    changed = true;
  }

  if (changed) {
    restartNMEASerial();
    delay(180);
  }

  lastTempState = tempState;
  lastVelState = velState;
  lastGPSState = gpsState;
  lastProfState = profState;
  lastCfgUpState = upState;
  lastCfgDownState = downState;
}

uint32_t buildSerialConfig(uint8_t dataBits, uint8_t parity, uint8_t stopBits) {
  if (dataBits == 7) {
    if (parity == 1) return stopBits == 2 ? SERIAL_7E2 : SERIAL_7E1;
    if (parity == 2) return stopBits == 2 ? SERIAL_7O2 : SERIAL_7O1;
    return stopBits == 2 ? SERIAL_7N2 : SERIAL_7N1;
  }
  if (parity == 1) return stopBits == 2 ? SERIAL_8E2 : SERIAL_8E1;
  if (parity == 2) return stopBits == 2 ? SERIAL_8O2 : SERIAL_8O1;
  return stopBits == 2 ? SERIAL_8N2 : SERIAL_8N1;
}
uint32_t getNMEASerialConfig() {
  return buildSerialConfig(nmeaDataBits, nmeaParity, nmeaStopBits);
}
char parityChar(uint8_t p) { return p == 1 ? 'E' : (p == 2 ? 'O' : 'N'); }
char getNMEAParityChar() { return parityChar(nmeaParity); }

void applyActiveUART() {
  NMEASerial.flush();
  NMEASerial.end();
  delay(20);
  if (nmeaReadMode) {
    NMEASerial.begin(NMEA_BAUDS[nmeaBaudIndex], getNMEASerialConfig(), NMEA_RX_PIN, NMEA_TX_PIN);
  } else {
    NMEASerial.begin(NMEA_BAUDS[txBaudIndex], buildSerialConfig(txDataBits, txParity, txStopBits), NMEA_RX_PIN, NMEA_TX_PIN);
  }
  nmeaRxLine = "";
  lastNMEALine = "Waiting for NMEA...";
}
void startNMEAReceiver() { applyActiveUART(); }
void stopNMEAReceiver() { nmeaRxLine = ""; applyActiveUART(); }
void restartNMEASerial() { applyActiveUART(); }

void showModeNotice(const char *title) {
  display.clearDisplay();
  display.setCursor(0, 12);
  display.println(title);
  display.setCursor(0, 29);
  if (nmeaReadMode) {
    display.print(NMEA_BAUDS[nmeaBaudIndex]); display.print(' ');
    display.print(nmeaDataBits); display.print(parityChar(nmeaParity)); display.print(nmeaStopBits);
  } else {
    display.print(NMEA_BAUDS[txBaudIndex]); display.print(' ');
    display.print(txDataBits); display.print(parityChar(txParity)); display.print(txStopBits);
  }
  display.display();
  delay(350);
}

void handleNMEATxConfigButtons() {
  static bool previous[6] = {HIGH,HIGH,HIGH,HIGH,HIGH,HIGH};
  const bool current[6] = {
    (bool)digitalRead(btnProf), (bool)digitalRead(btnGPS),
    (bool)digitalRead(btnVel), (bool)digitalRead(btnTemp),
    (bool)digitalRead(btnUp), (bool)digitalRead(btnDown)
  };
  for (int i=0; i<4; ++i) {
    if (current[i] == LOW && previous[i] == HIGH) txConfigField = i;
  }
  int direction = 0;
  if (current[4] == LOW && previous[4] == HIGH) direction = 1;
  if (current[5] == LOW && previous[5] == HIGH) direction = -1;
  if (direction) {
    switch (txConfigField) {
      case 0: txBaudIndex = (txBaudIndex + NMEA_BAUD_COUNT + direction) % NMEA_BAUD_COUNT; break;
      case 1: txDataBits = txDataBits == 8 ? 7 : 8; break;
      case 2: txParity = (txParity + 3 + direction) % 3; break;
      case 3: txStopBits = txStopBits == 1 ? 2 : 1; break;
    }
    applyActiveUART();
  }
  for (int i=0; i<6; ++i) previous[i] = current[i];
}

void showNMEATxConfigScreen() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("TX NMEA >");
  const char *names[] = {"BAUD", "DATA", "PARITY", "STOP"};
  display.println(names[txConfigField]);
  display.setCursor(0, 14);
  display.print(NMEA_BAUDS[txBaudIndex]); display.print("  ");
  display.print(txDataBits); display.print(parityChar(txParity)); display.println(txStopBits);
  display.setCursor(0, 28);
  display.println("PROF GPS VEL TEMP");
  display.setCursor(0, 40);
  display.println("UP/DN: adjust");
  display.setCursor(0, 52);
  display.println("GPS+VEL: exit");
  display.display();
}

void readNMEA() {
  while (NMEASerial.available()) {
    char c = (char)NMEASerial.read();

    Serial.write(c);

    if (c == '\n') {
      nmeaRxLine.trim();
      if (nmeaRxLine.length() > 0) {
        lastNMEALine = nmeaRxLine;
      }
      nmeaRxLine = "";
    }
    else if (c != '\r') {

      if (nmeaRxLine.length() < 120) {
        nmeaRxLine += c;
      } else {
        nmeaRxLine = "";
      }
    }
  }
}

void showNMEAScreen() {
  display.clearDisplay();

  display.setCursor(0, 0);
  display.print("RX NMEA >");
  if (nmeaConfigField == 0) display.println("BAUD");
  else if (nmeaConfigField == 1) display.println("DATA");
  else if (nmeaConfigField == 2) display.println("PARITY");
  else display.println("STOP");

  display.setCursor(0, 13);
  display.print(NMEA_BAUDS[nmeaBaudIndex]);
  display.print("  " );
  display.print(nmeaDataBits);
  display.print(getNMEAParityChar());
  display.print(nmeaStopBits);

  display.setCursor(0, 26);
  display.print("UP/DN: adjust");

  display.setCursor(0, 39);
  display.print("Data:");

  display.setCursor(0, 52);
  if (lastNMEALine.length() > 21) {
    display.print(lastNMEALine.substring(0, 21));
  } else {
    display.print(lastNMEALine);
  }

  display.display();
}

String generateNMEAMessage(String mode, float value, int profMode, int gpsMode, int velMode) {
  float feet = value * 3.28084;
  float milla_nautica = value * 0.000539957;
  float kilometros = value / 1.94384;

  String sentence;

  if (mode == "TEMPERATURA") {
    sentence = "WIMTW," + String(value, 1) + ",C";
  }
  else if (mode == "VELOCIDAD") {
    if (velMode == 0) {
      sentence = "VWVHW," + String(value, 1) + ",T," + String(value, 1) + ",M," + String(value, 1) + ",N," + String(kilometros, 1) + ",N";
    } else if (velMode == 1) {
      sentence = "WIMWD," + String(value, 1) + ",T," + String(value, 1) + ",M," + String(value, 1) + ",N," + String(kilometros, 1) + ",M";
    } else if (velMode == 2) {
      sentence = "WIMWV," + String(value, 1) + ",R," + String(value, 1) + ",N,A";
    }
  }
  else if (mode == "GPS") {
    if (gpsMode == 0) {
      sentence = "GPGLL," + String(value, 1) + ",N," + String(value, 1) + ",W," + String(value, 1) + ",A";
    } else if (gpsMode == 1) {
      sentence = "GPHDM," + String(value, 1) + ",M";
    } else if (gpsMode == 2) {
      sentence = "GPHTD," + String(value, 1) + ",T";
    }
  }
  else if (mode == "PROFUNDIDAD") {
    String tipoProf;
    if (profMode == 0) tipoProf = "SDDBK";
    else if (profMode == 1) tipoProf = "SDDBT";
    else if (profMode == 2) tipoProf = "SDDBS";

    sentence = tipoProf + "," + String(feet, 1) + ",f," + String(value, 1) + ",M," + String(milla_nautica, 1) + ",F";
  }

  return addNMEAChecksum(sentence);
}

String addNMEAChecksum(String sentence) {
  byte checksum = 0;
  const char* chars = sentence.c_str();
  while (*chars) {
    checksum ^= *chars++;
  }

  char hexChecksum[4];
  sprintf(hexChecksum, "%02X", checksum);

  return "$" + sentence + "*" + String(hexChecksum) + "\r\n";
}

bool nmeaChecksumCompare(const uint8_t packet[], const uint8_t packetEndIndex, const uint8_t receivedCSbyte1, const uint8_t receivedCSbyte2) {
  uint8_t calcChecksum = 0;

  for (uint8_t i = 1; i < packetEndIndex - 4; ++i) {
    calcChecksum ^= packet[i];
  }

  uint8_t nibble1 = (calcChecksum & 0xF0) >> 4;
  uint8_t nibble2 = calcChecksum & 0x0F;

  uint8_t translatedByte1 = (nibble1 <= 0x9) ? (nibble1 + '0') : (nibble1 - 10 + 'A');
  uint8_t translatedByte2 = (nibble2 <= 0x9) ? (nibble2 + '0') : (nibble2 - 10 + 'A');

  return (translatedByte1 == receivedCSbyte1 && translatedByte2 == receivedCSbyte2);
}
