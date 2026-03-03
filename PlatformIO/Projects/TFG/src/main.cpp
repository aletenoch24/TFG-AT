#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <cstring>

//Definicion de pines 
#define EPD_CS   5
#define EPD_DC   17 
#define EPD_RST  16
#define EPD_BUSY 4

//Datos de red
const char* ssid = "Livebox7-A6B5-WiFi7";
const char* password = "RQN64Zc75bf2";

//Datos del broker mqtt
const char* mqtt_server = "192.168.1.18"; 
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// Constructor de la pantalla 
GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Variables para guardar los datos que llegan
String aula;
String asig;
String prof;
String hora;

//Variables de datos que no se borran
RTC_DATA_ATTR char aulaMem[10] = "AULA";
RTC_DATA_ATTR char asigMem[64] = "-";
RTC_DATA_ATTR char profMem[20] = "-";
RTC_DATA_ATTR char horaMem[10] = "--:--";


// Variable para detectar si se ha recibido un mensaje para apagar el ESP.
bool mensajeRecibido = false;

// Variable para detectar si el mensaje recibido es el mismo al actualmente escrito
bool cambio = false;

//Variable que cuenta cuantas veces el ESP se "despierta"
RTC_DATA_ATTR int bootCount = 0;

//Funcion para centrar el texto en la pantalla
void centrarTexto(String texto, int y) {
  int16_t x1, y1;
  uint16_t w, h;

  display.getTextBounds(texto, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (display.width() - w) / 2;

  display.setCursor(x, y);
  display.print(texto);
}

//Funcion para actualizar los datos de pantalla
void interfaz() {

  display.setFullWindow(); 
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    display.setRotation(1); 

    //Aula
    display.setFont(&FreeSansBold12pt7b); 
    display.setTextColor(GxEPD_BLACK); 
    centrarTexto(aula,25);

    //Hora
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(180, 25);
    display.print(hora);

    //Asignatura
    int16_t x1, y1;
    uint16_t w, h;

    display.setFont(&FreeSansBold12pt7b);
    display.getTextBounds(asig, 0, 0, &x1, &y1, &w, &h);

    int margen = 10;
    int anchoMaximo = 250 - (margen * 2);

    //Si cabe lo ponemos con la fuente grande
    if (w <= anchoMaximo) {
        // Si cabe: Usamos fuente grande
        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(margen, 70);
        display.print(asig);
    } else {    //Si no cabe
      display.setFont(&FreeSansBold9pt7b);
      display.getTextBounds(asig, 0, 0, &x1, &y1, &w, &h);

      if(w <= anchoMaximo) {  //Si cabe en pequeña
        display.setCursor(margen, 70); 
        display.print(asig);
      } else {    //Si no cabe le añadimos ...
        String textoCortado = asig.substring(0, 22) + " . . .";
        display.setCursor(margen, 70);
        display.print(textoCortado);
      }
    }

    //Profesor
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 100);
    display.print("Prof: " + prof);


    //Marco:
    display.drawRect(0, 0, 250, 122, GxEPD_BLACK);

  }while(display.nextPage());

}

//Funcion que entra al recibir un dato
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensaje recibido [");
  Serial.print(topic);
  Serial.print("]: ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  //Convertir payload a JSON
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.print("Fallo al leer JSON: ");
    Serial.println(error.c_str());
    return;
  }
  

  //Extraer datos (Si existen en el JSON)
  if(doc.containsKey("aula")) aula = doc["aula"].as<String>();
  if(doc.containsKey("asignatura")) asig = doc["asignatura"].as<String>();
  if(doc.containsKey("profesor")) prof = doc["profesor"].as<String>();
  if(doc.containsKey("hora")) hora = doc["hora"].as<String>();

  if(aula!=aulaMem || asig!=asigMem || prof!=profMem || hora!=horaMem) {
    cambio = true;
  }


  mensajeRecibido=true;
}





void setup() {

  // Iniciamos la comunicación serie para poder ver mensajes en el PC
  Serial.begin(115200);
  delay(1000); 
  Serial.println("Arrancando sistema TFG...");

  //Imprimir numero de veces despertado
  ++bootCount ;                   
  Serial.println("Boot number: " + String(bootCount)) ;  

  //Establecer cada cuanto se despierta el ESP
  esp_sleep_enable_timer_wakeup(20 * 1000000);


  // Conectar al WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");

  unsigned long tiempoInicio = millis();
  while (millis() - tiempoInicio < 10000 && WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  //En caso de no conectarse en 10 segundos apagamos para no consumir mas batería.
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFallo al conectar WiFi.");
    display.powerOff(); 
    esp_deep_sleep_start(); 
  }

  Serial.println("\nWiFi Conectado!");
  Serial.print("IP del ESP32: ");
  Serial.println(WiFi.localIP());

  // Iniciamos la pantalla
  display.init(115200); 

  // Configurar MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);


  //Conectarse a MQTT 
  Serial.print("Conectando al Broker MQTT");
  while (!client.connected()) {
    if (client.connect("ESP32_Aula_Test", NULL, NULL, 0, 0, 0, 0, 1)) {
      Serial.println("¡Conectado!");
      
      // Nos suscribimos al tema de pruebas
      client.subscribe("aula/H1.10");
      Serial.println("Suscrito al tema: aula/H1.10");
      
      // Enviamos un saludo desde el ESP32 al PC
      client.publish("test/saludo", "Hola PC, soy el ESP32");
    } else {
      Serial.print("Fallo, rc=");
      Serial.print(client.state());
      Serial.println(" intentando en 5 segundos");
      delay(1000);
    }
  }


  //Bucle de 5 segundos para recibir el mensaje por MQTT 
  Serial.println("Esperando mensajes...");
  tiempoInicio = millis();
  while (millis() - tiempoInicio < 5000) {
    client.loop();

    //Si se ha recibido un mensaje y hay un cambio en el mensaje se dibuja.
    if(mensajeRecibido && cambio) {
      interfaz();

      //Almacenamos los nuevos valores en memoria
      std::strcpy(aulaMem, aula.c_str());
      std::strcpy(horaMem, hora.c_str());
      std::strcpy(asigMem, asig.c_str());
      std::strcpy(profMem, prof.c_str());

      Serial.println("Dibujado completo. Entrando en reposo...");
      break;
    }

  }

  Serial.println("Entrando en reposo");
  display.powerOff();
  esp_deep_sleep_start();

}

void loop() {
  
}

//Modelo Json
/*
{
  "aula": "H1.10",
  "asignatura": "Sist. Empotrados",
  "profesor": "A. Marin",
  "hora": "10:30"
}
*/
