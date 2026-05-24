# Sistema de información en pantallas e-ink para aulas y despachos

Este repositorio contiene el código fuente desarrollado para el Trabajo de Fin de Grado en Ingeniería Informática - Ingeniería de Computadores. 

El proyecto consiste en un sistema de información automatizado de bajo consumo para la gestión de espacios universitarios, combinando microcontroladores ESP32, tecnología de tinta electrónica y un servidor centralizado con interfaz web.

La placa utilizada durante el desarrollo ha sido la Lilygo T5 V2.3.1.

## Estructura del Proyecto

El código fuente está dividido en dos bloques principales:

* **`/Firmware`**: Contiene el código en C++ desarrollado en PlatformIO para la placa LilyGO TTGO T5 V2.3.1. Gestiona la conexión Wi-Fi, la suscripción a canales MQTT, el control de la pantalla e-ink y los estados de suspensión para maximizar la eficiencia energética.
* **`/Servidor`**: Contiene el *backend* y *frontend* del sistema desarrollado en Python.
    * `servidor.py`: Script principal que procesa los horarios, integra las incidencias y publica los paquetes JSON mediante el protocolo MQTT.
    * `web.py`: Interfaz web desarrollada con Flask para la gestión de incidencias por parte del profesorado y la monitorización de baterías por parte de los administradores.
    * `TFG.db`: Base de datos SQLite.
    * Directorios `/templates` y `/static`: Archivos HTML, CSS y JS para el renderizado del panel web (Jinja2 y Bootstrap 5).

## Tecnologías y Dependencias

Para la correcta ejecución del sistema, es necesario disponer de las siguientes herramientas:

**Para el Servidor:**
* Python 3.x
* Broker MQTT local (Ej: Eclipse Mosquitto) corriendo en el puerto 1883
* Librerías de Python: `flask`, `paho-mqtt`, `bcrypt`

**Para el Firmware:**
* PlatformIO (Extensión para Visual Studio Code)
* Librerías principales (gestionadas por `platformio.ini`): `PubSubClient`, `ArduinoJson`, `Adafruit GFX Library`

## Instrucciones de Ejecución

1.  **Levantar el Broker MQTT:** Asegúrate de que Mosquitto está en ejecución en la red local.
2.  **Iniciar el Servicio MQTT:** Ejecuta el archivo `servidor.py` para comenzar el procesamiento de datos y la escucha de la telemetría de los dispositivos.
3.  **Iniciar el Servidor Web:** Ejecuta el archivo `web.py` para levantar la plataforma web en `localhost`.
4.  **Cargar el Firmware:** Abre la carpeta `/Firmware` con PlatformIO, compila y sube el código a la placa, asegurándote de haber configurado previamente las credenciales de la red Wi-Fi y la IP del servidor MQTT en el código.
