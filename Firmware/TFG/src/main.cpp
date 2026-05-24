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

//Pin de lectura de batería interno de la Lilygo T5
#define BATT_PIN 35

//Datos de red
const char* ssid = "Livebox7-A6B5-WiFi7";
const char* password = "RQN64Zc75bf2";

//Datos del broker mqtt
const char* mqtt_server = "192.168.1.22"; 
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

//Constructor de la pantalla 
GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

//Nombre del dispositivo (a cambiar para cada dispositivo)
String nombreAula = "H1.10";

//Variable que almacena el ultimo bloque de datos distinto recibido
RTC_DATA_ATTR char jsonMem[1024] = "{}";

//Variable para detectar si se ha recibido un mensaje para apagar el ESP.
bool mensajeRecibido = false;

//Variable para detectar si el mensaje recibido es el mismo al actualmente escrito
bool cambio = false;

//Variable que cuenta cuantas veces el ESP se "despierta"
RTC_DATA_ATTR int bootCount = 0;

//Variable para alternar pantallas
RTC_DATA_ATTR int pantalla_actual = 0; // 0 = Clase normal, 1 = Incidencias

//Pin del botón
const int PIN_BOTON = 39;

//Variable para guardar el momento de la última actualización WiFi exitosa
RTC_DATA_ATTR uint64_t ultimaActualizacionWiFi = 0; 

//El intervalo en segundos en el que queremos que se actualicen los datos por WiFi
const uint64_t INTERVALO_ACTUALIZACION = 30 * 60;

//Variable para guardar el momento del último cambio de pantalla por botón
RTC_DATA_ATTR uint64_t ultimaPulsacion = 0; 
const uint64_t COOLDOWN_BOTON = 10; // Segundos de bloqueo entre pulsaciones

//Variables globales para guardar la lectura de batería de forma estática
float voltaje_bateria_actual = 0.0;
int porcentaje_bateria_actual = 0;

//Variable para contar cuantas veces ha fallado al conectarse al wifi
RTC_DATA_ATTR int intentos_wifi = 0;

//Función para averiguar por qué nos hemos despertado
void imprimir_motivo_despertar() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0: 
        Serial.println(" Desperté por el BOTÓN físico"); 
        break;
    case ESP_SLEEP_WAKEUP_TIMER: 
        Serial.println(" Desperté por el TEMPORIZADOR "); 
        break;
    default: 
        Serial.println("Desperté por un REINICIO o primera vez"); 
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

// Función para calcular el porcentaje de batería
void getBatteryPercentage() {
  analogSetPinAttenuation(BATT_PIN, ADC_11db); // Permite leer hasta ~4.4V

  //Lectura basura para estabilizar el circuito interno
  analogRead(BATT_PIN); 
  delay(5);

  //Tomamos varias muestras para promediar y estabilizar el valor
  long sum_raw = 0;
  const int num_muestras = 20;
  for (int i = 0; i < num_muestras; i++) {
    sum_raw += analogRead(BATT_PIN);
    delay(5); // Pequeña pausa entre lecturas
  }
  float raw_value = sum_raw / (float)num_muestras;

  Serial.print("ADC Value (Promedio): ");
  Serial.println(String(raw_value));
  
  //El divisor de voltaje corta a la mitad (*2), el 3.3V es la referencia del ADC, y 4095 es la resolución. 
  float factor_correccion = 1.134; //Multiplicamos por un factor de corrección ajustado.
  voltaje_bateria_actual = (raw_value / 4095.0) * 3.3 * 2.0 * factor_correccion; 
  
  //Pasar de voltaje a porcentaje (4.2V max, 3.2V min para LiPo)
  int percentage = (voltaje_bateria_actual - 3.2) / (4.2 - 3.2) * 100;
  Serial.print("Porcentaje: ");
  Serial.println(String(percentage));
  
  if (percentage > 100) percentage = 100;
  if (percentage < 0) percentage = 0;
  
  porcentaje_bateria_actual = percentage;
}

//Función para dibujar el icono de batería
void dibujarBateria(int x, int y) {
  int porcentaje = porcentaje_bateria_actual; //Usamos el valor guardado
  
  //Dibujar el marco de la pila
  display.drawRect(x, y, 20, 10, GxEPD_BLACK);
  display.fillRect(x + 20, y + 2, 2, 6, GxEPD_BLACK); //Borne positivo
  
  //Rellenar según el porcentaje (hasta 16 píxeles de ancho)
  int anchoRelleno = (porcentaje * 16) / 100;
  if (anchoRelleno > 0) {
    display.fillRect(x + 2, y + 2, anchoRelleno, 6, GxEPD_BLACK);
  }
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(x-2, y + 24);
  display.print(porcentaje);
  display.print("%");
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
    centrarTexto(aula, 25);

    //Dibujar icono de Batería en la esquina superior derecha
    dibujarBateria(10, 10);

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
        String textoCortado = asig.substring(0, 21) + " . . .";
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
    display.setCursor(5, 18);
    display.print("Prox. Clase: " + hora);

    // Dibujar icono de Batería en la esquina superior derecha
    dibujarBateria(210, 5);

    //Si la asignatura es muy larga, ponemos ...
    if(asig.length() > 22) {
      asig = asig.substring(0,19) + "...";
    }
    display.setCursor(5,42);
    display.print(asig);

    //Linea separadora
    display.drawLine(5, 48, 245, 48, GxEPD_BLACK);
    display.drawLine(5, 49, 245, 49, GxEPD_BLACK);


    //Incidencias
    display.setFont(&FreeMonoBold9pt7b);
    JsonArray incidencias = doc["incidencias_restantes"].as<JsonArray>();
    int y=65;
    
    if(incidencias.size() == 0) { //No hay incidencias
      display.setCursor(5, y);
      display.print("Sin incidencias hoy");
    } else {  //Escribimos cada incidencia
      for (JsonObject i: incidencias) {
        String hora_incidencia = i["hora"].as<String>();
        String aviso_incidencia = i["aviso"].as<String>();

        if(aviso_incidencia.length() > 16) {
          aviso_incidencia = aviso_incidencia.substring(0,14) +"..";
        }

        display.setCursor(5, y);
        display.print(hora_incidencia + "|" + aviso_incidencia);
        y+=20;

        if(y > 115) break; //Si no caben mas incidencias en la pantalla paramos de escribir
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
  if (json_entrante != String(jsonMem)) { //Si es diferente lo actualizamos y activamos la variable cambio
    Serial.println("HAY CAMBIOS. Actualizando memoria RTC.");
    cambio = true;
    
    //Sobrescribimos los datos en la variable RTC
    strlcpy(jsonMem, json_entrante.c_str(), sizeof(jsonMem));
    Serial.println(jsonMem);
  } else {  //Si es el mismo desactivamos cambio
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

  //Obtenemos el porcentaje de bateria antes de encender el Wi-Fi
  getBatteryPercentage();
  bool tocaActualizarWiFi = false;

  //Miramos los segundos actuales
  uint64_t tiempoActual = time(NULL);

  //Si el dispositivo no se ha despertado por el boton o ha pasado el tiempo de actualizacion, se conecta al wifi para comprobar si hay datos nuevos
  if (motivo != ESP_SLEEP_WAKEUP_EXT0 || (tiempoActual - ultimaActualizacionWiFi >= INTERVALO_ACTUALIZACION)) {
      tocaActualizarWiFi = true;
  }

  //Guardamos el tiempo de inicio para calcular cuánto hemos estado despiertos
  unsigned long tiempoInicioBoot = millis();
  
  if (tocaActualizarWiFi) { //Se ha despertado porque ha pasado el tiempo
      Serial.println("MODO WIFI: Buscando nuevos datos MQTT...");
      
      WiFi.mode(WIFI_STA); //Nos aseguramos de que actúe como cliente, no como router
      WiFi.disconnect();   //Borramos cualquier conexión colgada del pasado
      delay(100);
      
      // Conectar al WiFi
      unsigned long tiempoInicioWifi = millis();
      WiFi.begin(ssid, password);
      Serial.print("Conectando a WiFi");

      unsigned long tiempoInicio = millis();
      while (millis() - tiempoInicio < 10000 && WiFi.status() != WL_CONNECTED) { //Intentamos conectar durante 10 segundos
        delay(500);
        Serial.print(".");
      }

      //En caso de no conectarse en 10 segundos apagamos para no consumir mas batería.
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nFallo al conectar WiFi.");
        intentos_wifi++;
        if (intentos_wifi == 3) { //Si ha fallado 3 veces seguidas en conectarse al WiFi, lo apagamos mas tiempo
          esp_sleep_enable_timer_wakeup(60 * 30 * 1000000ULL);
        }else {
          esp_sleep_enable_timer_wakeup(10 * 1000000ULL);
        }
        display.powerOff(); 
        Serial.print("Tiempo total despierto (Fallo WiFi) ms: ");
        Serial.println(millis() - tiempoInicioBoot);
        Serial.flush();
        esp_deep_sleep_start(); 
      }
      
      intentos_wifi = 0;
      Serial.println("\nWiFi Conectado!");
      Serial.print("IP del ESP32: ");
      Serial.println(WiFi.localIP());

      //Configurar MQTT
      client.setServer(mqtt_server, mqtt_port);
      client.setCallback(callback);
      //Aumentamos el tamaño de los datos que puede recibir (por defecto 256 bytes)
      client.setBufferSize(1024);

      //Conectarse a MQTT 
      Serial.print("Conectando al Broker MQTT\n");
      int intentosMQTT = 0;
      while (!client.connected() && intentosMQTT < 5) {
        if (client.connect(nombreAula.c_str(), NULL, NULL, 0, 0, 0, 0, 1)) {
          Serial.println("¡Conectado!");
          
          //Nos suscribimos al tema del aula
          String topic_sub = "aula/" + nombreAula;
          client.subscribe(topic_sub.c_str());
          Serial.println("Suscrito al tema: " + topic_sub);

          //Publicamos el nivel de bateria para el servidor
          String topic_bateria = "aula/" + nombreAula + "/bateria";
          client.publish(topic_bateria.c_str(), String(porcentaje_bateria_actual).c_str(), true);
          Serial.println("Bateria publicada en MQTT");
        
          
          //Publicamos el voltaje para debug
          String topic_voltage = "aula/" + nombreAula + "/voltage";
          client.publish(topic_voltage.c_str(), String(voltaje_bateria_actual).c_str(), true);

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
        esp_sleep_enable_timer_wakeup(60 * 30 * 1000000ULL); // Reintento en 30 minuto
        display.powerOff(); 
        Serial.print("Tiempo total despierto (Fallo MQTT) ms: ");
        Serial.println(millis() - tiempoInicioBoot);
        Serial.flush();
        esp_deep_sleep_start();
      }

      //client.loop() (while de 5 segundos buscando datos nuevos)
      Serial.println("Esperando mensajes...");
      tiempoInicio = millis();
      while (millis() - tiempoInicio < 5000) {
        client.loop();
        // En cuanto recibimos el mensaje retenido, comprobamos si hay que dibujar y salimos del bucle
        if(mensajeRecibido) {
          if(cambio) {
          // APAGAMOS WIFI ANTES DE DIBUJAR
          Serial.print("Tiempo despierto con wifi encendido\n");
          Serial.println(millis() - tiempoInicioWifi);
          Serial.println("Apagando WiFi antes de redibujar para ahorrar batería...");
          WiFi.disconnect(true);
          WiFi.mode(WIFI_OFF);
          
            display.init(115200);
            interfaz();
            pantalla_actual = 0;
            Serial.println("Dibujado completo.");
          }
          break;
        }
      }
      //En caso de que no haya cambio y este en la pantalla de incidencias, se vuelve a la pantalla original
      if (!cambio && pantalla_actual == 1) {
          Serial.print("Tiempo despierto con wifi encendido");
          Serial.println(millis() - tiempoInicioWifi);
          Serial.println("Apagando WiFi antes de redibujar para ahorrar batería...");
          WiFi.disconnect(true);
          WiFi.mode(WIFI_OFF);
          Serial.println("Restaurando pantalla principal por defecto...");
          display.init(115200);
          interfaz();
          pantalla_actual = 0;
      }
      
      //Actualizamos la marca de tiempo para que empiece a contar desde ahora
      ultimaActualizacionWiFi = time(NULL);
      
  } else { //Se ha despertado con el boton
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
      //Si se ha pulsado el boton justo antes (2 o 3 segundos) del despertar normal, se establece el reinicio en 10 segundos
      Serial.println("Error en intervalo");
      tiempoDormir = 10; 
  }

  Serial.print("Dormiremos durante ");
  Serial.print(tiempoDormir);
  Serial.println(" segundos.");

  esp_sleep_enable_timer_wakeup(tiempoDormir * 1000000ULL); 

  Serial.print("Tiempo total despierto en este ciclo (ms): ");
  Serial.println(millis() - tiempoInicioBoot);
  Serial.println("Entrando en Deep Sleep. Zzz...");
  Serial.flush();
  esp_deep_sleep_start();

}

void loop() {
  
}
