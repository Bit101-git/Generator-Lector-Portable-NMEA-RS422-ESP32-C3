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
struct NMEASettings {
  uint8_t baudIndex;
  uint8_t dataBits;
  uint8_t parity;    
  uint8_t stopBits;   
};
NMEASettings nmeaRxSettings = {1, 8, 0, 1};
NMEASettings nmeaTxSettings = {1, 8, 0, 1};

enum NMEAMode { NMEA_SIMULATOR, NMEA_READER, NMEA_TX_CONFIG };
NMEAMode nmeaMode = NMEA_SIMULATOR;
uint8_t nmeaConfigField = 0; 

unsigned long rxComboStart = 0, txComboStart = 0;
bool rxComboArmed = false, txComboArmed = false;
bool rxComboFired = false, txComboFired = false;
const unsigned long COMBO_HOLD_MS = 1000;

String nmeaRxLine = "";
String lastNMEALine = "Esperando NMEA...";

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

uint32_t getNMEASerialConfig(const NMEASettings &settings);
char getNMEAParityChar(const NMEASettings &settings);
void handleNMEAConfigButtons();
void applyNMEASerialSettings(const NMEASettings &settings);
void showNMEAScreen();
void showNMEATXConfigScreen();
void readNMEA();

void setup() {
  Serial.begin(4800);

  applyNMEASerialSettings(nmeaTxSettings);

  Wire.begin(8, 9);

  if (!display.begin(OLED_ADDRESS, true)) {
    Serial.println("ERROR: no se pudo iniciar OLED SH1106");
    while (true) {
      delay(100);
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 25);
  display.println("Iniciando, espere");
  display.println("un momento...");
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

  bool tempPressed = digitalRead(btnTemp) == LOW;
  bool profPressed = digitalRead(btnProf) == LOW;
  bool gpsPressed = digitalRead(btnGPS) == LOW;
  bool velPressed = digitalRead(btnVel) == LOW;
  bool rxCombo = tempPressed && profPressed && !gpsPressed && !velPressed;
  bool txCombo = gpsPressed && velPressed && !tempPressed && !profPressed;

  if (rxCombo) {
    if (!rxComboArmed) { rxComboArmed = true; rxComboFired = false; rxComboStart = currentTime; }
    if (!rxComboFired && currentTime - rxComboStart >= COMBO_HOLD_MS) {
      rxComboFired = true;
      nmeaMode = (nmeaMode == NMEA_READER) ? NMEA_SIMULATOR : NMEA_READER;
      applyNMEASerialSettings(nmeaMode == NMEA_READER ? nmeaRxSettings : nmeaTxSettings);
      nmeaRxLine = "";
      lastNMEALine = "Esperando NMEA...";
      nmeaConfigField = 0;
      holdingUp = false; holdingDown = false;
      display.clearDisplay(); display.setCursor(0, 18);
      display.println(nmeaMode == NMEA_READER ? "LECTOR NMEA RX" : "SIMULADOR NMEA");
      display.display();
    }
  } else { rxComboArmed = false; rxComboFired = false; }

  if (txCombo) {
    if (!txComboArmed) { txComboArmed = true; txComboFired = false; txComboStart = currentTime; }
    if (!txComboFired && currentTime - txComboStart >= COMBO_HOLD_MS) {
      txComboFired = true;
      nmeaMode = (nmeaMode == NMEA_TX_CONFIG) ? NMEA_SIMULATOR : NMEA_TX_CONFIG;
      applyNMEASerialSettings(nmeaMode == NMEA_READER ? nmeaRxSettings : nmeaTxSettings);
      nmeaConfigField = 0;
      holdingUp = false; holdingDown = false;
      display.clearDisplay(); display.setCursor(0, 18);
      display.println(nmeaMode == NMEA_TX_CONFIG ? "CONFIG SALIDA TX" : "SIMULADOR NMEA");
      display.display();
    }
  } else { txComboArmed = false; txComboFired = false; }

  if (rxCombo || txCombo) {
    if (nmeaMode == NMEA_READER) { readNMEA(); showNMEAScreen(); }
    else if (nmeaMode == NMEA_TX_CONFIG) showNMEATXConfigScreen();
    delay(10);
    return;
  }

  if (nmeaMode != NMEA_SIMULATOR) {
    handleNMEAConfigButtons();
    if (nmeaMode == NMEA_READER) { readNMEA(); showNMEAScreen(); }
    else showNMEATXConfigScreen();
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
  display.print("Modo:");
  if (mode == "PROFUNDIDAD") {
    if (profMode == 0) display.println("PROFUNDIDAD DBK");
    else if (profMode == 1) display.println("PROFUNDIDAD DBT");
    else if (profMode == 2) display.println("PROFUNDIDAD DBS");
  }
  else if (mode == "GPS") {
    if (gpsMode == 0) display.println("GPS");
    else if (gpsMode == 1) display.println("RUMBO MAG.");
    else if (gpsMode == 2) display.println("RUMBO VER.");
  }
  else if (mode == "VELOCIDAD") {
    if (velMode == 0) display.println("VELOCIDAD LOG.");
    else if (velMode == 1) display.println("VEL. VIENTO REL.");
    else if (velMode == 2) display.println("VEL. VIENTO VER.");
  }
  else {
    display.println(mode);
  }

  display.setCursor(0, 15);
  display.print("Valor:");
  display.print(value, 1);

  display.setCursor(0, 30);
  display.print("Unidad:");
  if (mode == "TEMPERATURA") display.print("Celsius");
  else if (mode == "GPS") {
    if (gpsMode == 0) display.println("Lat/Long");
    else display.println("Grados");
  }
  else if (mode == "PROFUNDIDAD") display.print("Metros");
  else display.print("Nudos");

  String msg = generateNMEAMessage(mode, value, profMode, gpsMode, velMode);
  NMEASerial.print(msg);  
  Serial.print(msg);      
  display.setCursor(0, 45);
  display.print(msg);

  // Verificacion de checksum NMEA
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

  static bool lastTemp = HIGH, lastVel = HIGH, lastGPS = HIGH, lastProf = HIGH;
  static bool lastUp = HIGH, lastDown = HIGH;
  bool temp = digitalRead(btnTemp), vel = digitalRead(btnVel);
  bool gps = digitalRead(btnGPS), prof = digitalRead(btnProf);
  bool up = digitalRead(btnUp), down = digitalRead(btnDown);

  if (prof == LOW && lastProf == HIGH) nmeaConfigField = 0;
  if (gps == LOW && lastGPS == HIGH) nmeaConfigField = 1;
  if (vel == LOW && lastVel == HIGH) nmeaConfigField = 2;
  if (temp == LOW && lastTemp == HIGH) nmeaConfigField = 3;

  NMEASettings &cfg = nmeaMode == NMEA_READER ? nmeaRxSettings : nmeaTxSettings;
  bool inc = up == LOW && lastUp == HIGH;
  bool dec = down == LOW && lastDown == HIGH;
  if (inc != dec) {
    switch (nmeaConfigField) {
      case 0: cfg.baudIndex = (cfg.baudIndex + (inc ? 1 : NMEA_BAUD_COUNT-1)) % NMEA_BAUD_COUNT; break;
      case 1: cfg.dataBits = cfg.dataBits == 8 ? 7 : 8; break;
      case 2: cfg.parity = (cfg.parity + (inc ? 1 : 2)) % 3; break;
      case 3: cfg.stopBits = cfg.stopBits == 1 ? 2 : 1; break;
    }
    applyNMEASerialSettings(cfg);
  }
  lastTemp=temp; lastVel=vel; lastGPS=gps; lastProf=prof;
  lastUp=up; lastDown=down;
}

uint32_t getNMEASerialConfig(const NMEASettings &cfg) {
  if (cfg.dataBits == 7) {
    if (cfg.parity == 1) return cfg.stopBits == 2 ? SERIAL_7E2 : SERIAL_7E1;
    if (cfg.parity == 2) return cfg.stopBits == 2 ? SERIAL_7O2 : SERIAL_7O1;
    return cfg.stopBits == 2 ? SERIAL_7N2 : SERIAL_7N1;
  }
  if (cfg.parity == 1) return cfg.stopBits == 2 ? SERIAL_8E2 : SERIAL_8E1;
  if (cfg.parity == 2) return cfg.stopBits == 2 ? SERIAL_8O2 : SERIAL_8O1;
  return cfg.stopBits == 2 ? SERIAL_8N2 : SERIAL_8N1;
}

char getNMEAParityChar(const NMEASettings &cfg) {
  return cfg.parity == 1 ? 'E' : (cfg.parity == 2 ? 'O' : 'N');
}

void applyNMEASerialSettings(const NMEASettings &cfg) {
  NMEASerial.end();
  delay(20);
  NMEASerial.begin(NMEA_BAUDS[cfg.baudIndex], getNMEASerialConfig(cfg), NMEA_RX_PIN, NMEA_TX_PIN);
  Serial.print("UART NMEA: ");
  Serial.print(NMEA_BAUDS[cfg.baudIndex]);
  Serial.print(' '); Serial.print(cfg.dataBits);
  Serial.print(getNMEAParityChar(cfg)); Serial.println(cfg.stopBits);
  nmeaRxLine = "";
}

void readNMEA() {
  while (NMEASerial.available()) {
    char c = (char)NMEASerial.read();
    Serial.write(c);
    if (c == '\n') {
      nmeaRxLine.trim();
      if (nmeaRxLine.length()) lastNMEALine = nmeaRxLine;
      nmeaRxLine = "";
    } else if (c != '\r') {
      if (nmeaRxLine.length() < 120) nmeaRxLine += c;
      else nmeaRxLine = "";
    }
  }
}

void showNMEAConfigHeader(const NMEASettings &cfg, const char *prefix) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(prefix);
  display.print(" >");
  if (nmeaConfigField == 0) display.println("BAUD");
  else if (nmeaConfigField == 1) display.println("DATA");
  else if (nmeaConfigField == 2) display.println("PARITY");
  else display.println("STOP");
  display.setCursor(0, 13);
  display.print(NMEA_BAUDS[cfg.baudIndex]); display.print("  ");
  display.print(cfg.dataBits); display.print(getNMEAParityChar(cfg));
  display.println(cfg.stopBits);
  display.setCursor(0, 26); display.println("UP/DN cambia");
}

void showNMEAScreen() {
  showNMEAConfigHeader(nmeaRxSettings, "RX NMEA");
  display.setCursor(0, 39); display.println("Dato:");
  display.setCursor(0, 52);
  display.print(lastNMEALine.substring(0, 21));
  display.display();
}

void showNMEATXConfigScreen() {
  showNMEAConfigHeader(nmeaTxSettings, "TX NMEA");
  display.setCursor(0, 39); display.println("GPS+VEL: salir");
  display.setCursor(0, 52); display.println("Salida GPIO21");
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
