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
const char* mqtt_server = "192.168.1.16"; 
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// Constructor de la pantalla 
GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

//Nombre del dispositivo (a cambiar para cada dispositivo)
String nombreAula = "H1.10";

RTC_DATA_ATTR char jsonMem[1024] = "{}";

// Variable para detectar si se ha recibido un mensaje para apagar el ESP.
bool mensajeRecibido = false;

// Variable para detectar si el mensaje recibido es el mismo al actualmente escrito
bool cambio = false;

//Variable que cuenta cuantas veces el ESP se "despierta"
RTC_DATA_ATTR int bootCount = 0;

RTC_DATA_ATTR int pantalla_actual = 0; // 0 = Clase normal, 1 = Incidencias

// Pin del botón
const int PIN_BOTON = 39;

// Variable para guardar el momento de la última actualización WiFi exitosa
RTC_DATA_ATTR uint64_t ultimaActualizacionWiFi = 0; 

// El intervalo en segundos en el que queremos que se actualicen los datos por WiFi
const uint64_t INTERVALO_ACTUALIZACION = 20;

// Variable para guardar el momento del último cambio de pantalla por botón
RTC_DATA_ATTR uint64_t ultimaPulsacion = 0; 
const uint64_t COOLDOWN_BOTON = 5; // Segundos de bloqueo entre pulsaciones

// Función para averiguar por qué nos hemos despertado
void imprimir_motivo_despertar() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0: 
        Serial.println(">> Desperté por el BOTÓN físico"); 
        break;
    case ESP_SLEEP_WAKEUP_TIMER: 
        Serial.println(">> Desperté por el TEMPORIZADOR (Ciclo normal)"); 
        break;
    default: 
        Serial.println(">> Desperté por un REINICIO o primera vez"); 
        break;
  }
}

//Funcion para centrar el texto en la pantalla
void centrarTexto(String texto, int y) {
  int16_t x1, y1;
  uint16_t w, h;

  display.getTextBounds(texto, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (display.width() - w) / 2;

  display.setCursor(x, y);
  display.print(texto);
}

//Función para actualizar los datos de pantalla principal
void interfaz() {

  //Convertir payload a JSON
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc,(const char*) jsonMem);
  Serial.println("Entrando en interfaz");
  Serial.println(jsonMem);

  if (error) {
    Serial.print("Fallo al leer JSON: ");
    Serial.println(error.c_str());
    return;
  }

  display.setFullWindow(); 
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    display.setRotation(1); 

    //Aula
    String aula = doc["actual"]["aula"].as<String>();
    display.setFont(&FreeSansBold12pt7b); 
    display.setTextColor(GxEPD_BLACK); 
    display.setCursor(10, 25);
    display.print(aula);

    //Hora
    String hora = doc["actual"]["hora"].as<String>();
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(180, 25);
    display.print(hora);

    //Asignatura
    String asig = doc["actual"]["asignatura"].as<String>();
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
    String prof = doc["actual"]["profesor"].as<String>();
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 100);
    display.print("Prof: " + prof);

    //Icono para indicar si hay incidencias
    JsonArray incidencias = doc["incidencias_restantes"].as<JsonArray>();
    
    if (incidencias.size() > 0) {
      display.fillCircle(230, 100, 12, GxEPD_BLACK);
      
      display.setTextColor(GxEPD_WHITE);
      display.setFont(&FreeSansBold9pt7b);
      
      display.setCursor(227, 106); 
      display.print("!");
      
      display.setTextColor(GxEPD_BLACK);

    }
    //Marco:
    display.drawRect(0, 0, 250, 122, GxEPD_BLACK);

  }while(display.nextPage());

}

//Función para actualizar los datos de pantalla de incidencias
void interfaz2() {

  //Convertir payload a JSON
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc,(const char*) jsonMem);
  Serial.println("Entrando en interfaz");
  Serial.println(jsonMem);

  if (error) {
    Serial.print("Fallo al leer JSON: ");
    Serial.println(error.c_str());
    return;
  }

  display.setFullWindow(); 
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    display.setRotation(1); 

    //Proxima clase:
    String hora = doc["proxima"]["hora"].as<String>();
    String asig = doc["proxima"]["asignatura"].as<String>();
    display.setFont(&FreeSansBold9pt7b); 
    display.setTextColor(GxEPD_BLACK); 
    display.setCursor(5, 15);
    display.print("Prox.: " + hora);

    if(asig.length() > 24) {
      asig = asig.substring(0,21) + "...";
    }
    display.setCursor(5,38);
    display.print(asig);

    //Linea separadora
    display.drawLine(5, 46, 245, 46, GxEPD_BLACK);
    display.drawLine(5, 47, 245, 47, GxEPD_BLACK);


    //Incidencias
    display.setFont(&FreeMonoBold9pt7b);
    JsonArray incidencias = doc["incidencias_restantes"].as<JsonArray>();
    int y=65;
    
    if(incidencias.size() == 0) {
      display.setCursor(5, y);
      display.print("Sin incidencias hoy");
    } else {
      for (JsonObject i: incidencias) {
        String hora_incidencia = i["hora"].as<String>();
        String aviso_incidencia = i["aviso"].as<String>();

        if(aviso_incidencia.length() > 16) {
          aviso_incidencia = aviso_incidencia.substring(0,14) +"..";
        }

        display.setCursor(5, y);
        display.print(hora_incidencia + "|" + aviso_incidencia);
        y+=20;

        if(y > 115) break; //Si no cabe mas en la pantalla paramos de escribir
      }
    }
    

    //Marco:
    display.drawRect(0, 0, 250, 122, GxEPD_BLACK);

  }while(display.nextPage());
}

//Funcion que entra al recibir un dato
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensaje recibido [");
  Serial.print(topic);
  Serial.print("]: ");

  //Convertimos el json entrante en un string temporal
  String json_entrante = String((char*)payload, length);
  Serial.println();
  

  //Comparamos el texto que acaba de llegar con el que teníamos guardado
  Serial.print("JSON Entrante:");
  Serial.println(json_entrante);
  Serial.print("JSON Memoria String:");
  Serial.println(String(jsonMem));
  Serial.print("JSON Memoria:");
  Serial.println(jsonMem);
  if (json_entrante != String(jsonMem)) {
    Serial.println("HAY CAMBIOS. Actualizando memoria RTC.");
    cambio = true;
    
    //Sobrescribimos los datos en la variable RTC
    strlcpy(jsonMem, json_entrante.c_str(), sizeof(jsonMem));
    Serial.println(jsonMem);
  } else {
    Serial.println("Es idéntico a la memoria.");
    cambio = false; 
  }  


  mensajeRecibido=true;
}

void setup() {
  Serial.begin(115200);
  delay(1000); 
  Serial.println("Arrancando sistema TFG...");

  imprimir_motivo_despertar();

  esp_sleep_wakeup_cause_t motivo = esp_sleep_get_wakeup_cause();

  //Imprimir numero de veces despertado
  ++bootCount ;                   
  Serial.println("Boot number: " + String(bootCount));  


  bool tocaActualizarWiFi = false;

  //Miramos los segundos actuales
  uint64_t tiempoActual = time(NULL);

  //Si el dispositivo no se ha despertado por el boton o ha pasado el tiempo de actualizacion, se conecta al wifi para comprobar si hay datos nuevos
  if (motivo != ESP_SLEEP_WAKEUP_EXT0 || (tiempoActual - ultimaActualizacionWiFi >= INTERVALO_ACTUALIZACION)) {
      tocaActualizarWiFi = true;
  }

  
  if (tocaActualizarWiFi) {
      Serial.println("MODO WIFI: Buscando nuevos datos MQTT...");
      
      WiFi.mode(WIFI_STA); //Nos aseguramos de que actúe como cliente, no como router
      WiFi.disconnect();   //Borramos cualquier conexión colgada del pasado
      delay(100);
      
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
        esp_sleep_enable_timer_wakeup(5 * 1000000ULL);
        display.powerOff(); 
        Serial.flush();
        esp_deep_sleep_start(); 
      }

      Serial.println("\nWiFi Conectado!");
      Serial.print("IP del ESP32: ");
      Serial.println(WiFi.localIP());

      //Configurar MQTT
      client.setServer(mqtt_server, mqtt_port);
      client.setCallback(callback);
      //Aumentamos el tamaño de los datos que puede recibir (por defecto 256 bytes)
      client.setBufferSize(1024);

      //Conectarse a MQTT 
      Serial.print("Conectando al Broker MQTT");
      int intentosMQTT = 0;
      while (!client.connected() && intentosMQTT < 5) {
        if (client.connect(nombreAula.c_str(), NULL, NULL, 0, 0, 0, 0, 1)) {
          Serial.println("¡Conectado!");
          
          // Nos suscribimos al tema de pruebas
          client.subscribe("aula/H1.10");
          Serial.println("Suscrito al tema: aula/H1.10");
          
        } else {
          Serial.print("Fallo, rc=");
          Serial.print(client.state());
          Serial.println(" intentando en 2 segundos");
          delay(2000);
          intentosMQTT++;
        }
      }

      //Si no se pudo conectar en los intentos, lo volvemos a intentar en 1 minuto para ahorrar energia 
      if (!client.connected()) {
        Serial.println("Imposible conectar al Broker. Abortando y a dormir.");
        esp_sleep_enable_timer_wakeup(60 * 1000000ULL); // Reintento en 1 minuto
        display.powerOff(); 
        Serial.flush();
        esp_deep_sleep_start();
      }

      //client.loop() (while de 5 segundos buscando datos nuevos)
      Serial.println("Esperando mensajes...");
      tiempoInicio = millis();
      while (millis() - tiempoInicio < 5000) {
        client.loop();
        //Si se ha recibido un mensaje y hay un cambio en el mensaje se dibuja.
        if(mensajeRecibido && cambio) {
          display.init(115200);
          interfaz();

          pantalla_actual = 0;
          Serial.println("Dibujado completo. Entrando en reposo...");
          break;
        }
      }
      //En caso de que no haya cambio y este en la pantalla de incidencias, se vuelve a la pantalla original
      if (!cambio && pantalla_actual == 1) {
          Serial.println("Restaurando pantalla principal por defecto...");
          display.init(115200);
          interfaz();
          pantalla_actual = 0;
      }
      
      //Actualizamos la marca de tiempo para que empiece a contar desde ahora
      ultimaActualizacionWiFi = time(NULL);
      
  } else {
      // Comprobamos si ha pasado el cooldown desde la última pulsación
      if (tiempoActual - ultimaPulsacion < COOLDOWN_BOTON) {
          Serial.println("ANTISPAM: Pulsación ignorada para proteger la batería.");
      }else {
        Serial.println("MODO OFFLINE: Cambiando de pantalla sin internet...");
      
        // Registramos el momento exacto de esta pulsación válida
        ultimaPulsacion = tiempoActual;

        pantalla_actual = !pantalla_actual; 

        //Encendemos la pantalla
        display.init(115200);
        // Decidimos qué dibujar usando los datos crudos que ya están en jsonMem
        if (pantalla_actual == 0) {
            interfaz();
        } else {
            interfaz2();
        }
      }
  }




  //Preparar el siguiente sueño
  Serial.println("Preparando para dormir...");
  display.powerOff(); // Apagamos la pantalla

  //Configuramos el botón para que pueda despertar el dispositivo
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BOTON, 0);

  //Calculamos cuanto tiempo debe dormir hasta la proxima actualizacion por wifi
  tiempoActual = time(NULL);
  
  //Calculamos el tiempo transcurrido desde la ultima conexion WiFi
  uint64_t tiempoTranscurrido = tiempoActual - ultimaActualizacionWiFi;
  uint64_t tiempoDormir;

  //Si ese tiempo es menor al intervalo establecido, calculamos cuanto tiempo debe dormir
  if (tiempoTranscurrido < INTERVALO_ACTUALIZACION) {
      // Si nos despertamos por botón a la mitad del ciclo, solo dormimos el tiempo que falte
      tiempoDormir = INTERVALO_ACTUALIZACION - tiempoTranscurrido;
  } else {
      //Si se ha pulsado el boton justo antes (2 o 3 segundos) del despertar normal, se establece el reinicio en 1 minuto
      Serial.println("Error en intervalo");
      tiempoDormir = 60; 
  }

  Serial.print("Dormiremos durante ");
  Serial.print(tiempoDormir);
  Serial.println(" segundos.");

  esp_sleep_enable_timer_wakeup(tiempoDormir * 1000000ULL); 

  Serial.println("Entrando en Deep Sleep. Zzz...");
  Serial.flush();
  esp_deep_sleep_start();

}

void loop() {
  
}
