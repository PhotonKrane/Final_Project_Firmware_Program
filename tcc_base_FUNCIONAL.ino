#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h> // Display OLED

#include <ESP32Servo.h> // Servos

#include <time.h> // Horario

#include <EEPROM.h> // Guardar user

#include <WiFiManager.h> //Biblioteca de Conectividade

#include <ArduinoJson.h>
#include <HTTPClient.h> // Blibliotecas de Comunicação

#define EEPROM_SIZE 32 // EEPROM endereços max. 512

#define TOUCH_EEPROM 14
#define TOUCH_WM 4

unsigned long lastMinutes[5];
unsigned long intervalo[5];
bool estado[5];

int antTime = 0; // Tempo inicial Leitura
int interval = 6000;

WiFiServer server(3333); // Porta qualquer acima de 1024
WiFiClient client;

const char wifiSSID[20] = "MedicaBox";
const char wifiPASS[20] = "Medicamentos";

String user = "";

WiFiManager wm;

Servo floors[5];

int belts[5][5];
int copyBelts[5][5];
String names[5];
String copyNames[5];

bool areEqual = true;

int leds[5] = {12, 13, 5, 23, 19};

Adafruit_SSD1306 display(256,64,&Wire,-1);

float configSensor(int echo, int trig) {
// Gera um pulso de 50ms no pino de trigger
  digitalWrite(trig, 0);
  delay(20);
  digitalWrite(trig, 1);
  delay(50);
  digitalWrite(trig, 0);

// Mede o tempo de duração do pulso de eco
  int duration = pulseIn(echo, 1);

// Calcula a distância em centímetros
  float distance = duration * 0.034 / 2;

  return distance;
}

void setup() {
  EEPROM.begin(EEPROM_SIZE);
  Serial.begin(115200);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();

  pinMode(leds[4],OUTPUT);
  digitalWrite(leds[4],0);

  floors[0].attach(1);
  floors[1].attach(2);
  floors[2].attach(3);
  floors[3].attach(4);
  floors[4].attach(5);

  user = EEPROM.readString(0);

  setConfigBox();
  if (WiFi.status() != WL_CONNECTED) wmConnect();

  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Aguardando sincronização do tempo...");
  }

} // Função setup (só ocorre no acionamento do ESP32, ligando ele na tomada)

void loop() {
  if (user == "") {
    setConfigBox();
    if (WiFi.status() != WL_CONNECTED) wmConnect();
  }

  readTime();

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    int HrAtual = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60;
    //Serial.println(HrAtual);
    for (int i = 0; i < 5; i++) {       // percorre as linhas
      for (int j = 0; j < 5; j++) {     // percorre as colunas
        if (belts[i][j] != copyBelts[i][j]) {
          if(names[i] != "null"){
            calcTime(i, HrAtual);
            areEqual = false;
            break;  // sai do loop interno (colunas)
          }
        }
      }
    }
    for (int i = 0; i < 5; i++) {
      if (names[i] != "null") {
        configTimeFloors(i,HrAtual);
      }
    }
  }

  wmCancel(); // Esquecer WiFi

  eepromCancel(); // Trocar user

  if(!areEqual) {
    for (int i = 0; i < 5; i++) {
      for (int j = 0; j < 5; j++) {
          if (i < 5 && j < 5) { // segurança redundante
              copyBelts[i][j] = belts[i][j];
          }
      }
      copyNames[i] = names[i];
    }
    areEqual = true;
  } // "Se o antigo for diferente do novo..."
} // Função loop, acontece em loop né porra '-'

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
  if (hr_atual == lastMinutes[belt]) {
    Serial.print("Hora de tomar o medicamento: ");
    Serial.println(names[belt]);

    digitalWrite(leds[belt], estado[belt]);
    estado[belt] = !estado[belt];

    lastMinutes[belt] += intervalo[belt];  // avança para a próxima dose
  }
} // Configuração de horários

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
} // Dados das Esteiras

void setConfigBox() {
  createSoftOne();  
  while (user == "") {
    conectUser();
  }

  WiFi.softAPdisconnect(true);
  
  Serial.println("User recebido: " + user);
  EEPROM.writeString(0, user); // EEPROM.writeString(endereço, variável);
  EEPROM.commit(); // Grava na EEPROM
} // Configurar caixa

void createSoftOne() {
  WiFi.softAP("Envio", "Medic");  // Modo AP
  server.begin();
  Serial.println ("Nome da rede: Envio");
  Serial.println ("Senha da rede: Medicamentos");
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("Nome da rede: Envio");
  display.setCursor(0, 9);
  display.print("Senha da rede: Medic");
  display.setCursor(0, 17);
  display.print("IP: ");
  display.setCursor(25, 17);
  display.print(WiFi.softAPIP());
  display.display();
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
} // Recebimento do UID

void wmConnect() {
  wm.setConnectTimeout(10);
  bool res;

  wm.setConfigPortalBlocking(false);
  res = wm.autoConnect(wifiSSID,wifiPASS);

  if(!res) {
    Serial.println("Falha ao Conectar");

    Serial.print("Wifi: ");
    Serial.println(wifiSSID);
    Serial.print("Senha: ");
    Serial.println(wifiPASS);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());

    while(WiFi.status() != WL_CONNECTED) {
      wm.process();
    }
  }
} 
void wmCancel() {
  if(touchRead(TOUCH_WM) <= 20) {
    wm.resetSettings();
    Serial.println("Configurações Reiniciadas");
    wmConnect();
  }
}// Conexão

void eepromCancel() {
  if (touchRead(TOUCH_EEPROM) <= 20) {
    user = "";

    EEPROM.writeString(0, user);
    EEPROM.commit();
    Serial.println("EEPROM Reiniciada");
  }
} // Limpeza do User