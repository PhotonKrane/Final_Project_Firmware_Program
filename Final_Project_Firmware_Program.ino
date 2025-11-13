#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Adafruit_GFX.h>  // Display OLED

#include <ESP32Servo.h>  // Servos

#include <time.h>  // Horario

#include <EEPROM.h>  // Guardar user

#include <WiFiManager.h>  //Biblioteca de Conectividade

#include <ArduinoJson.h>
#include <HTTPClient.h>  // Blibliotecas de Comunicação

#define EEPROM_SIZE 128  // EEPROM endereços max. 512

#define TOUCH_EEPROM 13
#define TOUCH_WM 3

#define echo 7
#define trig 15

#define TFT_CS 45
#define TFT_RST 47  // Or set to -1 and connect to Arduino RESET pin
#define TFT_DC 48
#define TFT_MOSI 21  // SDA, HW MOSI
#define TFT_SCLK 20   // SCL, HW SCLK

#define led_push 8

#define buzzer 46

enum FloorState {
  IDLE,             // Parado, aguardando
  DISPENSING,       // Servo ativo, dispensando
  WAITING_REMOVAL,  // Aguardando retirada do medicamento
  RETURNING         // Retornando servo
};

unsigned long lastMinutes[5];
unsigned long intervalo[5];

unsigned long floorTimers[5] = { 0, 0, 0, 0, 0 };

int antTime = 0;  // Tempo inicial Leitura
int antTimeSensor = 0; // Tempo inicial sensor
int interval = 180000;

WiFiServer server(3333);  // Porta qualquer acima de 1024
WiFiClient client;

const char wifiSSID[20] = "MedicaBox";
const char wifiPASS[20] = "Medicamentos";

String user = "";

WiFiManager wm;

Servo floors[5];
FloorState floorStates[5] = { IDLE, IDLE, IDLE, IDLE, IDLE };

int belts[5][5];
int copyBelts[5][5];
String names[5];
String copyNames[5];

bool areEqual = true;

float max_dis = 2.9;

int default_hall_min = 1850;
int default_hall_max = 1910;
int activeAngles[5] = { 94, 92, 90, 0, 0 };
int stopAngles[5] = { 91, 89, 91, 0, 0 };

int leds[5] = { 36, 35, 16, -1, -1 };
int hall[5] = { 4, -1, -1, -1, -1 };

const int NUM_PHYSICAL_FLOORS = 3;

Adafruit_ST7789 display = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
int estadoDisplay = 0;

float configSensor(int ec, int tr) {
  digitalWrite(tr, LOW);
  delayMicroseconds(2);
  digitalWrite(tr, HIGH);
  delayMicroseconds(10);
  digitalWrite(tr, LOW);

  long duration = pulseIn(ec, HIGH, 5000);
  float distance = duration * 0.034 / 2;
  return distance;
}

void setup() {
  EEPROM.begin(EEPROM_SIZE);
  Serial.begin(115200);
  
  display.init(240, 320);
  display.fillScreen(ST77XX_WHITE);  // Comando tipo o .clear
  display.setRotation(1);
  display.setTextWrap(true);

  display.setTextColor(display.color565(39, 55, 85));
  display.setTextSize(4);
  display.setCursor(52, 88);
  display.print("MedicaBox");

  delay(3000);

  display.fillScreen(ST77XX_WHITE);  // Comando tipo o .clear

  display.setTextSize(3);
  display.setCursor(61, 96);
  display.print("Aguarde...");
  
  delay(3000);

  for (int i = 0; i < NUM_PHYSICAL_FLOORS; i++) {
    pinMode(leds[i], OUTPUT);
    digitalWrite(leds[i], 0);
  }
  pinMode(trig, OUTPUT);
  pinMode(echo, INPUT);
  pinMode(led_push, OUTPUT);
  digitalWrite(led_push, 0);
  pinMode(buzzer, OUTPUT);
  digitalWrite(buzzer, 0);

  floors[0].attach(10);
  floors[1].attach(-1);
  floors[2].attach(-1);

  for (int j = 0; j < 3; j++) {
    floors[j].write(stopAngles[j]);
  }

  user = EEPROM.readString(0);

  if(user == "") {
    display.fillScreen(ST77XX_WHITE);
    display.setTextSize(2);
    display.setCursor(46, 96);
    display.print("Configurando UID...");
    delay(2000);
    setConfigBox();
  }
  if (WiFi.status() != WL_CONNECTED) {
    display.fillScreen(ST77XX_WHITE);
    display.setTextSize(3);
    display.setCursor(43, 96);
    display.print("Conectando...");
    wmConnect();
  }

  display.fillScreen(ST77XX_WHITE);
  display.setCursor(22, 104);
  display.setTextSize(2);
  display.print("Sincronizando o Horario");

  unsigned long startWait = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWait < 5000) {
    delay(100);
  }

  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Aguardando sincronização do tempo...");
  }

  Serial.println("Sincronização bem sucedida...");

  display.fillScreen(ST77XX_WHITE);  // Comando tipo o .clear
  display.setTextColor(display.color565(39, 55, 85));
  display.setTextSize(3);
  display.setCursor(52, 83);
  display.print("Configuracao");
  display.setCursor(52, 109);
  display.print("Bem-Sucedida");

  delay(3000);

  display.fillScreen(ST77XX_WHITE);  // Comando tipo o .clear

  display.setTextColor(display.color565(39, 55, 85));
  display.setTextSize(3);
  display.setCursor(61, 96);
  display.print("Aguarde...");

  getBelts();
  delay(3000);
}  // Função setup (só ocorre no acionamento do ESP32, ligando ele na tomada

void loop() {
  if (user == "") 
    setConfigBox();

  if (WiFi.status() != WL_CONNECTED) 
    wmConnect();

  readTime();

  if(Serial.available()) {
    int belt = Serial.readStringUntil('\n').toInt();

    Serial.println("Andar: " + String(belt));

    floorStates[belt-1] = DISPENSING;
  }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    unsigned long now = millis();
    float dist = 0;
    for(int i = 0; i < 20; i++) {
      dist += configSensor(echo, trig);
    }
    dist = dist/20;


    if (dist > max_dis && estadoDisplay != 1) {  // Gaveta muito longe, risco de queda
      estadoDisplay = 1;
      digitalWrite(led_push, 0);
      Serial.println("GAVETA MUITO LONGE!");
      display.fillScreen(ST77XX_WHITE);
      display.setCursor(0,0);
      display.setTextSize(3);
      display.setTextColor(display.color565(39, 55, 85));
      display.setTextWrap(true);
      display.print("A gaveta nao esta no lugar!");
    }

    else if (dist < max_dis && estadoDisplay != 2) {  // Medicamento na gaveta
      estadoDisplay = 2;
      Serial.println("AHHHHHHHHHH!");
      digitalWrite(led_push, 0);
      display.fillScreen(ST77XX_WHITE);
      display.setCursor(0,0);
      display.setTextSize(3);
      display.setTextColor(display.color565(131, 173, 230));
      display.setTextWrap(true);
      display.print("Gaveta no lugar");
    }
    
    if (dist < max_dis) {
      int HrAtual = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60;
      Serial.println("Hora Atual: " + String(HrAtual));
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

  if (!areEqual) {
    for (int i = 0; i < 5; i++) {
      for (int j = 0; j < 5; j++) {
        copyBelts[i][j] = belts[i][j];
      }
      copyNames[i] = names[i];
    }
    areEqual = true;
  }

  wmCancel();  // Esquecer WiFi

  eepromCancel();  // Trocar user
}  // Função loop, acontece em loop né porra '-'

void updateFloorState(int floor) {
  switch (floorStates[floor]) {
    case DISPENSING:
      // Ativa o servo e LED
      Serial.println("Servo Ativo");
      floors[floor].write(activeAngles[floor]);
      digitalWrite(leds[floor], 1);

      // Muda para o próximo estado
      floorTimers[floor] = millis();
      floorStates[floor] = WAITING_REMOVAL;
      break;

    case WAITING_REMOVAL:
    {
      Serial.println("Aguardando leitura");

      if (millis() - floorTimers[floor] < 1000)
        break;

      int readSensor = 0;
      if (hall[floor] != -1) {
        for (int i = 0; i < 100; i++) {
          readSensor += analogRead(hall[floor]);
        }
        readSensor = readSensor / 100;

        if (readSensor < default_hall_min || readSensor > default_hall_max) {  //   Ímã detectado = ponto diviória
          floors[floor].write(stopAngles[floor]);
          floorStates[floor] = RETURNING;
          floorTimers[floor] = millis();
          display.fillScreen(ST77XX_WHITE);
        }
      }
      Serial.print("Sensor Hall -> ");
      Serial.println(readSensor);
      break;
    }

    case RETURNING:
      Serial.println("Desligando servo");
      digitalWrite(leds[floor], 0);
      digitalWrite(led_push, 1);

      display.setTextColor(display.color565(39, 55, 85));
      display.setCursor(0, 100);
      display.setTextSize(3);
      display.print("Hora do remedio!");
      unsigned long tempoMusica = millis() - floorTimers[floor];
      if (tempoMusica < 2000) tone(buzzer, 440);
      else if (tempoMusica >= 2000 && tempoMusica < 3000) tone(buzzer, 350);
      else if (tempoMusica >= 3000 && tempoMusica < 4000) tone(buzzer, 523);
      else if (tempoMusica >= 4000 && tempoMusica < 6000) tone(buzzer, 659);
      else if (tempoMusica >= 6000 && tempoMusica < 8000) tone(buzzer, 784);
      else if (tempoMusica >= 8000 && tempoMusica < 9500) tone(buzzer, 825);
      else if (tempoMusica >= 9500 && tempoMusica < 10000) tone(buzzer, 687);
      else if (tempoMusica >= 10000) noTone(buzzer);

      if (millis() - floorTimers[floor] >= 10000) {
        digitalWrite(led_push, 0);
        noTone(buzzer); // para o som
        estadoDisplay = -1;
        floorStates[floor] = IDLE;
        Serial.println("Pronto, voltando pro IDLE");
      }
      break;
  }

}  // Realizar a função da ativação do andar

/*FUNC AUX*/

bool temFloorAtivo() {
  for (int i = 0; i < 5; i++) {
    if (floorStates[i] != IDLE) {
      return true;
    }
  }
  return false;
}

void calcTime(int belt, int hr_atual) {
  int TIHour = belts[belt][0];
  int TIMin = belts[belt][1];
  intervalo[belt] = TIHour * 3600 + TIMin * 60;

  int lastHour = belts[belt][2];
  int lastMin = belts[belt][3];

  lastMinutes[belt] = lastHour * 3600 + lastMin * 60;


  if (intervalo[belt] <= 0) return;

  if (lastMinutes[belt] <= hr_atual) {
    while (lastMinutes[belt] <= hr_atual) {
      lastMinutes[belt] += intervalo[belt];
    }
  }
  else {
    while (lastMinutes[belt] - hr_atual > intervalo[belt]) {
      lastMinutes[belt] -= intervalo[belt];
    }

    if (lastMinutes[belt] <= hr_atual)
      lastMinutes[belt] += intervalo[belt];
  }

  Serial.println("Tempo de intervalo: " + String(intervalo[belt]));
  Serial.println("Tempo para dosagem: " + String(lastMinutes[belt]));
}
void configTimeFloors(int belt, int hr_atual) {
  // Só ativa se estiver IDLE e for a hora certa
  if (floorStates[belt] == IDLE && hr_atual >= lastMinutes[belt]) {
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
    getBelts();
  }
}
void getBelts() {
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
  WiFi.disconnect(true);  // true = desliga WiFi
  WiFi.softAPdisconnect(true);  // true = apaga a rede
  WiFi.mode(WIFI_OFF);          // Desliga WiFi completamente
  delay(1000);
  WiFi.mode(WIFI_STA);          // Volta pro modo Station
  Serial.println("User recebido: " + user);
  EEPROM.writeString(0, user);  // EEPROM.writeString(endereço, variável);
  EEPROM.commit();              // Grava na EEPROM

  display.fillScreen(ST77XX_WHITE);
}  // Configurar caixa

/*
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextWrap(true);
  tft.print("Texto Haha");
*/
void createSoftOne() {
  WiFi.softAP("Envio", "Medicamentos");  // Modo AP
  server.begin();
  Serial.println("Nome da rede: Envio");
  Serial.println("Senha da rede: Medic");
  Serial.println(WiFi.softAPIP());
  Serial.println("Aguardando conexão...");

  display.fillScreen(ST77XX_WHITE);
  display.setTextSize(2);
  display.setTextColor(display.color565(39, 55, 85));
  display.setCursor(10, 10);
  display.print("Nome da rede: Envio");
  display.setCursor(10, 41);
  display.print("Senha da rede: ");
  display.setCursor(10, 72);
  display.print("Medicamentos");
  display.setCursor(10, 103);
  display.print("IP: ");
  display.setCursor(65, 103);
  display.print(WiFi.softAPIP());
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
    display.setTextColor(display.color565(39, 55, 85));
    display.setCursor(20, 10);
    display.print("Nome da Rede: ");
    display.setCursor(20, 41);
    display.print(wifiSSID);
    display.setCursor(20, 72);
    display.print("Senha da Rede: ");
    display.setCursor(20, 103);
    display.print(wifiPASS);
    display.setCursor(20, 134);
    display.print("IP da Rede: ");
    display.setCursor(20, 165);
    display.print(WiFi.softAPIP());
    while (WiFi.status() != WL_CONNECTED) {
      wm.process();
    }
    display.fillScreen(ST77XX_WHITE);
  }
}
void wmCancel() {
  if (touchRead(TOUCH_WM) / 100 >= 650) {
    estadoDisplay = -1;
    wm.resetSettings();
    Serial.println("Configurações Reiniciadas");
    display.fillScreen(ST77XX_WHITE);
    display.setCursor(10, 10);
    display.print("Rede Reiniciada");
    wmConnect();
  }
}  // Conexão

void eepromCancel() {
  if (touchRead(TOUCH_EEPROM) / 100 >= 650) {
    estadoDisplay = -1;
    user = "";

    EEPROM.writeString(0, user);
    EEPROM.commit();
    Serial.println("EEPROM Reiniciada");
    display.fillScreen(ST77XX_WHITE);
    display.setCursor(10, 10);
    display.print("Usuario Reiniciado");
    delay(3000);
  }
}  // Limpeza do User

