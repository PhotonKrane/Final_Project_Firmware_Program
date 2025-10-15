#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Adafruit_GFX.h>  // Display OLED

#include <ESP32Servo.h>  // Servos

#include <time.h>  // Horario

#include <EEPROM.h>  // Guardar user

#include <WiFiManager.h>  //Biblioteca de Conectividade

#include <ArduinoJson.h>
#include <HTTPClient.h>  // Blibliotecas de Comunicação

#define EEPROM_SIZE 32  // EEPROM endereços max. 512

#define TOUCH_EEPROM 11
#define TOUCH_WM 12

#define echo 19
#define trig 18

#define TFT_CS        26
#define TFT_RST       20 // Or set to -1 and connect to Arduino RESET pin
#define TFT_DC        21
#define TFT_MOSI      10   // SDA, HW MOSI
#define TFT_SCLK      9   // SCL, HW SCLK

#define led_push      8

#define buzzer        16

enum FloorState {
  IDLE,              // Parado, aguardando
  DISPENSING,        // Servo ativo, dispensando
  WAITING_REMOVAL,   // Aguardando retirada do medicamento
  RETURNING          // Retornando servo
};

unsigned long lastMinutes[5];
unsigned long intervalo[5];
unsigned long lastHTTPUpdate = 0;
unsigned long httpInterval = 180000;

unsigned long floorTimers[5] = {0, 0, 0, 0, 0};

int antTime = 0;  // Tempo inicial Leitura
int interval = 60000;

WiFiServer server(3333);  // Porta qualquer acima de 1024
WiFiClient client;

const char wifiSSID[20] = "MedicaBox";
const char wifiPASS[20] = "Medicamentos";

String user = "";

WiFiManager wm;

Servo floors[5];
FloorState floorStates[5] = {IDLE, IDLE, IDLE, IDLE, IDLE};

int belts[5][5];
int copyBelts[5][5];
String names[5];
String copyNames[5];

bool areEqual = true;

float dist;
float min_dis = 5;
float max_dis = 10;

int default_hall = 0;
int activeAngles[5] = {91, 92, 90, 0, 0};
int stopAngles[5] = {90, 89, 91, 0, 0};

int leds[5] = {5, 6, 7, -1, -1};
int hall[5] = {2, 3, 4, -1, -1};

const int NUM_PHYSICAL_FLOORS = 3;  // ← Número real de floors

Adafruit_ST7789 display = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST, TFT_SCLK, TFT_MOSI);

float configSensor(int ec, int tr) {
  static unsigned long lastMeasurement = 0;
  static float lastDistance = 0;
  
  if (millis() - lastMeasurement >= 100) {
    digitalWrite(tr, LOW);
    delayMicroseconds(2);
    digitalWrite(tr, HIGH);
    delayMicroseconds(10);
    digitalWrite(tr, LOW);
    
    int duration = pulseIn(ec, HIGH);
    lastDistance = duration * 0.034 / 2;
    lastMeasurement = millis();
  }
  
  return lastDistance;
}

void setup() {
  EEPROM.begin(EEPROM_SIZE);
  Serial.begin(115200);

  display.init(240, 320);
  display.fillScreen(ST77XX_WHITE); // Comando tipo o .clear
  display.setRotation(1);

  for(int i = 0; i < NUM_PHYSICAL_FLOORS; i++) {
    pinMode(leds[i], OUTPUT);
    digitalWrite(leds[i], 0); 
  }
  pinMode(led_push, OUTPUT);
  digitalWrite(led_push, 0);
  pinMode(buzzer, OUTPUT);
  digitalWrite(buzzer, 0);

  floors[0].attach(13);
  floors[1].attach(14);
  floors[2].attach(15);

  user = EEPROM.readString(0);

  setConfigBox();
  if (WiFi.status() != WL_CONNECTED) wmConnect();

  unsigned long startWait = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWait < 5000) {
    delay(100);
  }

  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Aguardando sincronização do tempo...");
  }

  readTime();
}  // Função setup (só ocorre no acionamento do ESP32, ligando ele na tomada)

void loop() {
  if (user == "") {
    setConfigBox();
    if (WiFi.status() != WL_CONNECTED) wmConnect();
  }

  unsigned long now = millis();
  
  if (now - lastHTTPUpdate >= httpInterval) {
    readTime();
    lastHTTPUpdate = now;
  }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    dist = configSensor(echo, trig);

    if (dist > max_dis) { // Gaveta muito longe, risco de queda
      digitalWrite(led_push, 0);

      display.fillScreen(ST77XX_WHITE);
      display.setCursor(0,0);
      display.setTextSize(3);
      display.setTextColor(display.color565(39, 55, 85));
      display.setTextWrap(true);
      display.print("A gaveta nao esta no lugar!");
    }

    else if(dist < min_dis) { // Medicamento na gaveta
      digitalWrite(led_push, 1);

      display.fillScreen(ST77XX_WHITE);
      display.setCursor(0,0);
      display.setTextSize(3);
      display.setTextColor(display.color565(39, 55, 85));
      display.setTextWrap(true);
      display.print("Medicamento para se retirar!");
    }

    else if(dist >= min_dis && dist <= max_dis) {
    display.fillScreen(ST77XX_WHITE);
    display.setCursor(0,0);
    display.setTextSize(3);
    display.setTextColor(display.color565(131, 173, 230));
    display.setTextWrap(true);
    display.print("Dosagens em dia");
    }

    if(dist < max_dis) {
      int HrAtual = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60;

      for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
          if (belts[i][j] != copyBelts[i][j]) {
            if (names[i] != "null") {
              calcTime(i, HrAtual);
              areEqual = false;
              break;
            }
          }
        }
      }

      for (int i = 0; i < NUM_PHYSICAL_FLOORS; i++) {
        if (names[i] != "null") {
          configTimeFloors(i, HrAtual);
        }
      }

      for (int i = 0; i < NUM_PHYSICAL_FLOORS; i++) {
        updateFloorState(i);
      }
    }
  }

  wmCancel();  // Esquecer WiFi

  eepromCancel();  // Trocar user

  if (!areEqual) {
    for (int i = 0; i < 5; i++) {
      for (int j = 0; j < 5; j++) {
        copyBelts[i][j] = belts[i][j];
      }
      copyNames[i] = names[i];
    }
    areEqual = true;
  }
}  // Função loop, acontece em loop né porra '-'

void updateFloorState(int floor) {
  switch(floorStates[floor]) {
    
    case IDLE:
      // Não faz nada, aguarda ser ativado por configTimeFloors()
      break;
    
    case DISPENSING:
      // Ativa o servo e LED
      floors[floor].write(activeAngles[floor]);
      digitalWrite(leds[floor], 1);
      
      // Mostra mensagem no display (apenas do primeiro floor ativo)
      if (floor == getPrimeiroFloorAtivo()) {
        display.fillScreen(ST77XX_WHITE);
        display.setTextSize(2);
        display.setCursor(10, 40);
        display.print("Hora de tomar: " + names[floor]);
      }
      
      // Muda para o próximo estado
      floorStates[floor] = WAITING_REMOVAL;
      break;
    
    case WAITING_REMOVAL: {
      int readSensor;
    if (hall[floor] != -1)
      readSensor = analogRead(hall[floor]);
      
      if (readSensor <= default_hall) {  // Ímã detectado = retirada
        floorStates[floor] = RETURNING;
        floorTimers[floor] = millis();  // Marca o tempo para delay
      }
      break;
    }
      
    case RETURNING:
      // Para o servo e desliga o LED
      floors[floor].write(stopAngles[floor]);
      
      digitalWrite(leds[floor], 0);
      
      // Aguarda 500ms antes de voltar ao IDLE (evita reativações)
      if (millis() - floorTimers[floor] >= 500)
        floorStates[floor] = IDLE;
      
      break;
  }
} // Realizar a função da ativação do andar

/*FUNC AUX*/

bool temFloorAtivo() {
  for (int i = 0; i < 5; i++) {
    if (floorStates[i] != IDLE) {
      return true;
    }
  }
  return false;
}

int getPrimeiroFloorAtivo() {
  for (int i = 0; i < 5; i++) {
    if (floorStates[i] != IDLE) {
      return i;
    }
  }
  return -1;  // Nenhum ativo
}

void calcTime(int belt, int hr_atual) {
  int TIHour = belts[belt][0];
  int TIMin = belts[belt][1];
  intervalo[belt] = TIHour * 3600 + TIMin * 60;

  int lastHour = belts[belt][2];
  int lastMin = belts[belt][3];

  lastMinutes[belt] = lastHour * 3600 + lastMin * 60;


  while (lastMinutes[belt] <= hr_atual) {
    lastMinutes[belt] += intervalo[belt];
  }
}
void configTimeFloors(int belt, int hr_atual) {
  // Só ativa se estiver IDLE e for a hora certa
  if (floorStates[belt] == IDLE && hr_atual == lastMinutes[belt]) {
    Serial.print("Hora de tomar o medicamento: ");
    Serial.println(names[belt]);
    
    // Ativa o floor mudando seu estado
    floorStates[belt] = DISPENSING;
    
    // Avança para a próxima dose
    lastMinutes[belt] += intervalo[belt];
  }
}  // Configuração de horários

void readTime() {
  unsigned long timeNow = millis();

  if (timeNow - antTime >= interval) {
    antTime = timeNow;
    getBeltInfos(0);
    getBeltInfos(1);
    getBeltInfos(2);
    getBeltInfos(3);
    getBeltInfos(4);

    for (int i = 0; i <= 4; i++) {
      if (names[i] != "null") {
        Serial.println(i + 1);
        Serial.println(belts[i][0]);
        Serial.println(belts[i][1]);
        Serial.println(belts[i][2]);
        Serial.println(belts[i][3]);
        Serial.println(belts[i][4]);
        Serial.println(names[i]);
      } else {
        Serial.print("Sem dados na esteira ");
        Serial.println(i + 1);
      }
    }
  }
}
void getBeltInfos(int belt) {
  const String firebaseHost = "https://firestore.googleapis.com/v1/projects/medicabox-50b65/databases/(default)/documents/users/";
  const String boxes = "/box/";

  HTTPClient http;

  String esteira = String(belt + 1);
  String link = firebaseHost + user + boxes + esteira;

  //Serial.println(link);

  if (WiFi.status() == WL_CONNECTED) {
    http.begin(link);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String payload = http.getString();

      //Serial.println(payload);

      DynamicJsonDocument doc(4096);
      deserializeJson(doc, payload);

      JsonObject fields = doc["fields"].as<JsonObject>();

      belts[belt][0] = 0;
      belts[belt][1] = 0;
      belts[belt][2] = 0;
      belts[belt][3] = 0;
      belts[belt][4] = 0;
      names[belt] = "null";

      int TIHour = fields["TIHour"]["integerValue"].as<int>();
      int TIMin = fields["TIMin"]["integerValue"].as<int>();
      int lastHour = fields["lastHour"]["integerValue"].as<int>();
      int lastMinute = fields["lastMinute"]["integerValue"].as<int>();
      int bel = fields["belt"]["integerValue"].as<int>();
      String name = fields["name"]["stringValue"].as<String>();

      belts[belt][0] = TIHour;
      belts[belt][1] = TIMin;
      belts[belt][2] = lastHour;
      belts[belt][3] = lastMinute;
      belts[belt][4] = bel;
      names[belt] = name;
    }
  }
  http.end();
}  // Dados das Esteiras

void setConfigBox() {
  createSoftOne();
  while (user == "") {
    conectUser();
  }

  WiFi.softAPdisconnect(true);

  Serial.println("User recebido: " + user);
  EEPROM.writeString(0, user);  // EEPROM.writeString(endereço, variável);
  EEPROM.commit();              // Grava na EEPROM
}  // Configurar caixa

/*
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextWrap(true);
  tft.print("Texto Haha");
*/
void createSoftOne() {
  WiFi.softAP("Envio", "Medic");  // Modo AP
  server.begin();
  Serial.println("Nome da rede: Envio");
  Serial.println("Senha da rede: Medic");

  display.fillScreen(ST77XX_WHITE);
  display.setTextSize(3);
  display.setTextColor(display.color565(39, 55, 85));
  display.setCursor(0, 0);
  display.print("Nome da rede: Envio");
  display.setCursor(0, 9);
  display.print("Senha da rede: Medic");
  display.setCursor(0, 17);
  display.print("IP: ");
  display.setCursor(25, 17);
  display.print(WiFi.softAPIP());
  Serial.println(WiFi.softAPIP());
  Serial.println("Aguardando conexão...");
}
void conectUser() {
  client = server.available();
  if (client) {
    Serial.println("Cliente conectado");

    String jsonString = "";
    unsigned long timeout = millis() + 2000;

    while (client.connected() && millis() < timeout) {
      while (client.available()) {
        char c = client.read();
        jsonString += c;
      }
    }

    DynamicJsonDocument doc(4096);
    deserializeJson(doc, jsonString);

    user = doc["uid"].as<String>();

    client.stop();  // Fecha a conexão
    Serial.println("Cliente desconectado");
  }
}  // Recebimento do UID

void wmConnect() {
  wm.setConnectTimeout(10);
  bool res;

  wm.setConfigPortalBlocking(false);
  res = wm.autoConnect(wifiSSID, wifiPASS);

  if (!res) {
    Serial.println("Falha ao Conectar");

    Serial.print("Wifi: ");
    Serial.println(wifiSSID);
    Serial.print("Senha: ");
    Serial.println(wifiPASS);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());

    display.fillScreen(ST77XX_WHITE);
    display.setTextSize(3);
    display.setTextColor(display.color565(39, 55, 85));
    display.setTextWrap(true);
    
    display.setCursor(10, 20);
    display.print("Nome da Rede: ");
    display.setCursor(10, 100);
    display.print(wifiSSID);
    display.setCursor(42, 20);
    display.print("Senha da Rede: ");
    display.setCursor(42, 100);
    display.print(wifiPASS);
    display.setCursor(74, 20);
    display.print("IP da Rede: ");
    display.setCursor(74, 100);
    display.print(WiFi.softAPIP());

    while (WiFi.status() != WL_CONNECTED) {
      wm.process();
    }
  }
}
void wmCancel() {
  if (touchRead(TOUCH_WM) <= 20) {
    wm.resetSettings();
    Serial.println("Configurações Reiniciadas");
    wmConnect();
  }
}  // Conexão

void eepromCancel() {
  if (touchRead(TOUCH_EEPROM) <= 20) {
    user = "";

    EEPROM.writeString(0, user);
    EEPROM.commit();
    Serial.println("EEPROM Reiniciada");
  }
}  // Limpeza do User