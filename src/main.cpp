#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Servo.h>
#include <EEPROM.h>
#include "HUSKYLENS.h"
#include <Wire.h>

void reciveData(String recivedData);
void reciveRotationSpeed(String rotationXY);
void recivePrecision(String precisionXY);
void reciveReturnTime(String retrunTimeRecived);
void reciveModeSelect(String patrolModeRecived);
void reciveRotation(String rotationXY);
void backToDefault();
void reciveFireSignal(String fireSignal);
void manualControl();
void patrol();
int stepControl(int rotationSpeed);
void aiAutoDetect();
void saveDataToMemory(int intData, bool boolData, String variableName);

HUSKYLENS huskylens;
void printResult(HUSKYLENSResult result);

// Ustaw nazwę i hasło sieci wifi
const char *ssid = "Sentry Turret";
const char *password = "12345678";

#define fire 5

// Wartości Wieżyczki
Servo servoX;
Servo servoY;
int rotationSpeedX = 1;
int rotationSpeedY = 1;
int precisionX = 1;
int precisionY = 1;
int returnTime = 5000;
bool patrolMode = false;
String rotationX = "off";
String rotationY = "off";
bool autoMode = false;
bool patrolLeftRight = false;     //(false w prawo, true w lewo)
int maxLeftPositionX = 65;        // Max wychylenie w Lewo (Ograniczenie deski)
int maxRightPositionX = 115;      // Max wychylenie w Prawo (Ograniczenie deski)
int servoPositionX = 90;          // Pozycja serwa ustawiona na 90 (Centrum)
int servoPositionY = 180;         // Pozycja serwa ustawiona na 180 (Pozycja Dolna) Zapobiega szarpnięciu serwa w górę
bool patrolModeWasActive = false; // Tryb patrolu był włączony przed wykryciem celu
unsigned long trackingStartTime;

// Zmienne zależne od kamery AI
bool eyeOnTarget = false;    // Cel w polu strzeleckim
bool targetDetected = false; // Cel wykryty ale nie jest w polu strzeleckim
bool targetLost = false;
int ID1 = 1; // Nauczony objekt

int value = 0;        // Przechowuje przesyłaną wartość
bool cleanup = false; // czyszczenie pamięci

// Struktura do zapisywania i odczytywania danych z EEPROM ESP32
struct MyData
{
  int saved_rotation_speed_x;
  int saved_rotation_speed_y;
  int saved_precision_x;
  int saved_precision_y;
  int saved_returnTime;
  bool saved_patrolMode;
  bool saved_patrolModeWasActive;
  bool saved_autoMode;
  bool was_saved_previously;
};
MyData savedData;

// Utwórz obiekt serwera
AsyncWebServer server(80);

void reciveData(String recivedData);

void setup()
{
  // Rozpocznij komunikację szeregową
  Serial.begin(115200);

  EEPROM.begin(sizeof(MyData)); // Inicjalizacja EEPROM z rozmiarem struktury MyData

  if (cleanup)
  {
    // Wyczyść EEPROM
    for (int i = 0; i < 512; i++)
    {
      EEPROM.write(i, 0);
    }
    EEPROM.commit(); // Zatwierdzenie zmian
  }

  EEPROM.get(0, savedData); // Odczyt danych z pamięci i zapis do struktury
  if (savedData.was_saved_previously)
  {
    rotationSpeedX = savedData.saved_rotation_speed_x;
    rotationSpeedY = savedData.saved_rotation_speed_y;
    precisionX = savedData.saved_precision_x;
    precisionY = savedData.saved_precision_y;
    returnTime = savedData.saved_returnTime;
    patrolMode = savedData.saved_patrolMode;
    patrolModeWasActive = savedData.saved_patrolModeWasActive;
    autoMode = savedData.saved_autoMode;
  }
  if (!savedData.was_saved_previously)
  { // Jeśli nie było wcześniej nic zapisane to ma zrobić pierwszy zapis
    savedData.saved_rotation_speed_x = rotationSpeedX;
    savedData.saved_rotation_speed_y = rotationSpeedY;
    savedData.saved_precision_x = precisionX;
    savedData.saved_precision_y = precisionY;
    savedData.saved_returnTime = returnTime;
    savedData.saved_patrolMode = patrolMode;
    savedData.saved_patrolModeWasActive = patrolModeWasActive;
    savedData.saved_autoMode = autoMode;
    savedData.was_saved_previously = true;
    // Oraz od razu zapisać do pamięci
    EEPROM.put(0, savedData);
    EEPROM.commit(); // Zatwierdzenie zmian
  }

  Wire.begin(A4, A5); // SDA & SDL
  while (!huskylens.begin(Wire))
  {
    Serial.println(F("Begin failed!"));
    Serial.println(F("1.Please recheck the \"Protocol Type\" in HUSKYLENS (General Settings>>Protocol Type>>I2C)"));
    Serial.println(F("2.Please recheck the connection."));
    delay(100);
  }

  pinMode(fire, OUTPUT);
  servoX.attach(10);            // SerwoX (lewo prawo) przypisane do pinu 10
  servoX.write(servoPositionX); // Pozycja serwa ustawiona na 90 (Centrum)
  servoY.attach(11);            // SerwoY (gora dol) przypisane do pinu 10
  servoY.write(servoPositionY); // Pozycja serwa ustawiona na 90 (Centrum)

  trackingStartTime = millis(); // zapisz czas startu dla funkcji śledzenia celu ai w trybie auto

  // Utwórz sieć wifi
  WiFi.softAP(ssid, password);

  // Wyświetl adres IP esp32
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/command", HTTP_GET, [](AsyncWebServerRequest *request)
            {
  if(request->hasParam("cmd")){
    String command = request->getParam("cmd")->value();
    if(command.startsWith("set_value=")){
      String receivedValue = command.substring(10); // Odebranie wartości jako String
      Serial.print("Received value: ");
      Serial.println(receivedValue);

      // Przekazanie otrzymanej wartości do funkcji reciveData
      reciveData(receivedValue);

      request->send(200, "text/plain", "OK");
    }else {
      request->send(400, "text/plain", "Bad Request");
    }
    
  } else {
    request->send(400, "text/plain", "Bad Request");
  } });

  server.on("/sendData", HTTP_GET, [](AsyncWebServerRequest *request)
            {
  Serial.println(savedData.saved_rotation_speed_x);
  String data = "";
  data += "get_rotationSpeed_x=" + String(rotationSpeedX) + "\n";
  data += "get_rotationSpeed_y=" + String(rotationSpeedY) + "\n";
  data += "get_precision_x=" + String(precisionX) + "\n";
  data += "get_precision_y=" + String(precisionY) + "\n";
  data += "get_returnTime=" + String(returnTime) + "\n";
  if(autoMode && targetDetected || autoMode && !targetDetected && patrolModeWasActive) data += "get_patrolMode=" + String(patrolModeWasActive) + "\n";
  else data += "get_patrolMode=" + String(patrolMode) + "\n";
  data += "get_autoMode=" + String(autoMode) + "\n";

  request->send(200, "text/plain", data);
  Serial.println("Polaczono"); });

  // Uruchom serwer
  server.begin();
  backToDefault();
}

void loop()
{
  if (!autoMode)
    manualControl();
  if (patrolMode)
    patrol();
  if (autoMode)
    aiAutoDetect();
  if (targetDetected && patrolMode)
  {
    patrolMode = false;
    patrolModeWasActive = true;
  }
}

void reciveData(String recivedData)
{
  if (recivedData.indexOf("set_rotation_speed_x=") == 0)
    reciveRotationSpeed(recivedData);
  else if (recivedData.indexOf("set_precision_x=") == 0)
    recivePrecision(recivedData);
  else if (recivedData.indexOf("set_return_time=") == 0)
    reciveReturnTime(recivedData);
  else if (recivedData.indexOf("set_mode=") == 0)
    reciveModeSelect(recivedData);
  else if (recivedData.indexOf("set_rotation_x=") == 0)
    reciveRotation(recivedData);
  else if (recivedData.indexOf("fire") == 0)
    reciveFireSignal(recivedData);
  else if (recivedData.indexOf("manual_closed") == 0)
    backToDefault();
  else if (recivedData.indexOf("manual_opened") == 0)
  {
    autoMode = false;
    saveDataToMemory(0, autoMode, "autoMode");
    backToDefault();
  }
  else if (recivedData.indexOf("autoMode=on") == 0)
  {
    autoMode = true;
    saveDataToMemory(0, autoMode, "autoMode");
  }
  else if (recivedData.indexOf("autoMode=off") == 0)
  {
    autoMode = false;
    saveDataToMemory(0, autoMode, "autoMode");
    backToDefault();
  }
}
void reciveRotationSpeed(String rotationXY)
{
  // Wzór set_rotation_speed_x=#_y=#
  //-----01234567890123456789012345
  int rotationSX = rotationXY.substring(21, 22).toInt();
  if (rotationSX != 0)
    rotationSpeedX = rotationSX;
  saveDataToMemory(rotationSpeedX, false, "rotationSpeedX");

  int rotationSY = rotationXY.substring(25).toInt();
  if (rotationSY != 0)
    rotationSpeedY = rotationSY;
  saveDataToMemory(rotationSpeedY, false, "rotationSpeedY");
}
void recivePrecision(String precisionXY)
{
  // Wzór set_precision_x=#_y=#
  //-----012345678901234567890
  int precisionXRecived = precisionXY.substring(16, 17).toInt();
  int precisionYRecived = precisionXY.substring(20).toInt();
  if (precisionXRecived != 0)
    precisionX = precisionXRecived;
  if (precisionYRecived != 0)
    precisionY = precisionYRecived;
  saveDataToMemory(precisionX, false, "precisionX");
  saveDataToMemory(precisionY, false, "precisionY");
}
void reciveReturnTime(String retrunTimeRecived)
{
  // Wzór set_return_time=#
  //-----01234567890123456
  int i = retrunTimeRecived.substring(16).toInt() * 1000;
  if (i != 0)
    returnTime = i;
  saveDataToMemory(returnTime, false, "returnTime");
}
void reciveModeSelect(String patrolModeRecived)
{
  // Wzór set_mode=###
  //-----012345678901
  if (patrolModeRecived.substring(9).equals("on"))
  {
    if (!autoMode)
    {
      patrolMode = true;
      patrolModeWasActive = true;
    }
    else if (autoMode && !targetDetected)
    {
      patrolMode = true;
      patrolModeWasActive = true;
    }
    else
      patrolModeWasActive = true;
  }
  else
  {
    patrolMode = false;
    patrolModeWasActive = false;
    if (!targetDetected)
      backToDefault();
  }
  saveDataToMemory(0, patrolMode, "patrolMode");
}
void reciveRotation(String rotationXY)
{
  // Wzór set_rotation_x=#_y=#

  rotationX = rotationXY.substring(rotationXY.indexOf("_x=") + 3,
                                   rotationXY.indexOf("_y="));
  rotationY = rotationXY.substring(rotationXY.indexOf("_y=") + 3);
}
void reciveFireSignal(String fireSignal)
{
  digitalWrite(fire, HIGH);
  delay(100);
  digitalWrite(fire, LOW);
}
void backToDefault()
{
  while (servoPositionX != 90 || servoPositionY != 90)
  {
    if (servoPositionX > 90)
      servoPositionX -= 1;
    else if (servoPositionX < 90)
      servoPositionX += 1;
    servoX.write(servoPositionX);

    if (servoPositionY > 90)
      servoPositionY -= 1;
    else if (servoPositionY < 90)
      servoPositionY += 1;
    servoY.write(servoPositionY);
    delay(20);
  }
}

void manualControl()
{
  if (rotationX.equals("left"))
  {
    servoPositionX -= 1;
    if (servoPositionX < maxLeftPositionX)
      servoPositionX = maxLeftPositionX;
    servoX.write(servoPositionX);
    delay(stepControl(rotationSpeedX));
  }
  if (rotationX.equals("right"))
  {
    servoPositionX += 1;
    if (servoPositionX > maxRightPositionX)
      servoPositionX = maxRightPositionX;
    servoX.write(servoPositionX);
    delay(stepControl(rotationSpeedX));
  }

  if (rotationY.equals("up"))
  {
    servoPositionY += 1;
    if (servoPositionY > 180)
      servoPositionY = 180;
    servoY.write(servoPositionY);
    delay(stepControl(rotationSpeedY));
  }
  if (rotationY.equals("down"))
  {
    servoPositionY -= 1;
    if (servoPositionY < 0)
      servoPositionY = 0;
    servoY.write(servoPositionY);
    delay(stepControl(rotationSpeedY));
  }
}

void patrol()
{

  if (servoPositionX > maxLeftPositionX && !patrolLeftRight)
  {
    servoPositionX -= 1;
    if (servoPositionX < maxLeftPositionX)
      servoPositionX = maxLeftPositionX;
    servoX.write(servoPositionX);
    delay(stepControl(rotationSpeedX));
  }
  if (servoPositionX == maxLeftPositionX)
    patrolLeftRight = true;

  if (servoPositionX < maxRightPositionX && patrolLeftRight)
  {
    servoPositionX += 1;
    if (servoPositionX > maxRightPositionX)
      servoPositionX = maxRightPositionX;
    servoX.write(servoPositionX);
    delay(stepControl(rotationSpeedX));
  }
  if (servoPositionX == maxRightPositionX)
    patrolLeftRight = false;
}

int stepControl(int rotationSpeed)
{
  int delay = 0;
  for (; rotationSpeed < 9; rotationSpeed += 1)
  {
    delay += 5;
  }
  return 20 + delay;
}

void aiAutoDetect()
{

  // Obsługa algorytmu kamery ai

  HUSKYLENSResult result;
  if (!huskylens.request())
    Serial.println(F("Fail to request data from HUSKYLENS, recheck the connection!"));
  else if (!huskylens.isLearned())
    Serial.println(F("Nothing learned, press learn button on HUSKYLENS to learn one!"));
  else if (!huskylens.available())
  {
    // Serial.println(F("No object appears on the screen!"));
    targetDetected = false;
    eyeOnTarget = false;
  }
  else
  {
    Serial.println(F("###########"));
    while (huskylens.available())
    {
      HUSKYLENSResult result = huskylens.read();
      if (result.command == COMMAND_RETURN_BLOCK)
      {
        targetDetected = true;
        targetLost = false;
        // Serial.println(String()+F("Block:xCenter=")+result.xCenter+F(",yCenter=")+result.yCenter);
        // Serial.println(String() + F("Block:xCenter=") + result.xCenter + F(",yCenter=") + result.yCenter + F(",width=") + result.width + F(",height=") + result.height + F(",ID=") + result.ID);
        if (result.ID == 1)
        {
          if (result.xCenter < (160 - (result.width / 4) - (precisionX * 5)))
          {
            // w prawo
            eyeOnTarget = false;
            servoPositionX += 1;
            if (servoPositionX > maxRightPositionX)
              servoPositionX = maxRightPositionX;
            servoX.write(servoPositionX);
            delay(stepControl(rotationSpeedX));
          }
          else if (result.xCenter > (160 + (result.width / 4) + (precisionX * 5)))
          {
            // w lewo
            eyeOnTarget = false;
            servoPositionX -= 1;
            if (servoPositionX < maxLeftPositionX)
              servoPositionX = maxLeftPositionX;
            servoX.write(servoPositionX);
            delay(stepControl(rotationSpeedX));
          }
          else if (result.yCenter > (120 - (result.height / 4) - (precisionX * 5)))
          {
            // w góre
            eyeOnTarget = false;
            servoPositionY -= 1;
            if (servoPositionY < 0)
              servoPositionY = 0;
            servoY.write(servoPositionY);
            delay(stepControl(rotationSpeedY));
          }
          else if (result.yCenter < (120 + (result.height / 4) + (precisionX * 5)))
          {
            // w dół
            eyeOnTarget = false;
            servoPositionY += 1;
            if (servoPositionY > 180)
              servoPositionY = 180;
            servoY.write(servoPositionY);
            delay(stepControl(rotationSpeedY));
          }
          else
          {
            // W polu strzalu
            eyeOnTarget = true;
            digitalWrite(fire, HIGH);
            delay(100);
            digitalWrite(fire, LOW);
          }
        }
      }
      else
      {
        Serial.println("Object unknown!");
      }
    }
  }
  if (!targetDetected && !targetLost)
  {
    trackingStartTime = millis(); // resetuj czas wykrywania celu
    // Serial.println("Nie widze celu");
    targetLost = true;
  }
  // Serial.print("patrolModeWasActive: ");
  // Serial.println(patrolModeWasActive);
  if (!targetDetected && millis() - trackingStartTime >= returnTime && millis() - trackingStartTime <= (returnTime + 50))
  {
    // Jeśli minęło 5 sekund a wieżyczka nadal nie znalazła celu
    if (patrolModeWasActive)
    {
      patrolMode = true;
      patrolModeWasActive = false;
    } // Powrót do patrolowania
    else if (!patrolMode)
      backToDefault(); // Jeśli nie było włączonego patrolu, powrót do centrum
  }
}

void saveDataToMemory(int intData, bool boolData, String variableName)
{

  if (variableName.equals("rotationSpeedX"))
  {
    savedData.saved_rotation_speed_x = rotationSpeedX;
  }
  else if (variableName.equals("rotationSpeedY"))
  {
    savedData.saved_rotation_speed_y = rotationSpeedY;
  }
  else if (variableName.equals("precisionX"))
  {
    savedData.saved_precision_x = precisionX;
  }
  else if (variableName.equals("precisionY"))
  {
    savedData.saved_precision_y = precisionY;
  }
  else if (variableName.equals("returnTime"))
  {
    savedData.saved_returnTime = returnTime;
  }
  else if (variableName.equals("patrolMode"))
  {
    savedData.saved_patrolMode = patrolMode;
  }
  else if (variableName.equals("patrolModeWasActive"))
  {
    savedData.saved_patrolModeWasActive = patrolModeWasActive;
  }
  else if (variableName.equals("autoMode"))
  {
    savedData.saved_autoMode = autoMode;
  }
  Serial.println(savedData.saved_rotation_speed_x);
  EEPROM.put(0, savedData);
  EEPROM.commit(); // Zatwierdzenie zmian
}