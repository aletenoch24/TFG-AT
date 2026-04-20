import paho.mqtt.client as mqtt
import json
import time
from datetime import datetime, date
import sqlite3
import unicodedata

#Datos para la conexion MQTT
broker = "192.168.1.17"
puerto = 1883

# Funciones de callback para recibir mensajes MQTT
# Al iniciar el cliente se subscribe a todos los topics de baterias de las aulas
def on_connect(client, userdata, flags, reason_code, properties):
    print(f"Conectado a MQTT con código {reason_code}. Suscribiendo a baterías...")
    client.subscribe("aula/+/bateria") # El '+' es un comodín para leer de cualquier aula

def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode('utf-8')
    
    # El topic tiene el formato: aula/H1.10/bateria
    partes = topic.split("/")
    if len(partes) == 3 and partes[2] == "bateria":
        aula = partes[1]
        try:
            porcentaje = int(payload)
            
            # Guardamos/Actualizamos en la base de datos
            conn = sqlite3.connect("TFG.db")
            conn.execute("PRAGMA foreign_keys = ON;")
            cursor = conn.cursor()
            
            # Creamos la tabla si no existe
            cursor.execute('''CREATE TABLE IF NOT EXISTS Baterias 
                              (id_espacio TEXT PRIMARY KEY, 
                               porcentaje INTEGER, 
                               ultima_actualizacion DATETIME)''')
            
            # INSERT OR REPLACE sobrescribe el dato del aula si ya existía
            cursor.execute('''INSERT OR REPLACE INTO Baterias (id_espacio, porcentaje, ultima_actualizacion)
                              VALUES (?, ?, datetime('now', 'localtime'))''', (aula, porcentaje))
            conn.commit()
            conn.close()
            
            print(f"[MQTT] Batería actualizada -> {aula}: {porcentaje}%")
        except Exception as e:
            print(f"Error procesando batería: {e}")

#Crea el cliente y se conecta
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message
client.connect(broker, puerto, 60)
client.loop_start() # Inicia el hilo en segundo plano de MQTT para procesar envíos y KeepAlive

#Horario provisional
horario_semanal = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]


#Limpia los datos de la tabla incidencias si han pasado 14 desde la incidencia
def limpieza_incidencias(cursor):

    consulta_limpieza = "DELETE FROM Incidencias WHERE fecha <= date('now', '-14 days')"

    cursor.execute(consulta_limpieza)
    cursor.connection.commit()
    filas_borradas = cursor.rowcount
    if filas_borradas > 0:
        print(f"Limpieza: Se han borrado {filas_borradas} incidencias antiguas.")

#Funcion para eliminar tildes y caracteres extraños que no puedan escribirse en pantalla
def limpiar_textos(texto):
    if texto is None:
        return "-"
    #Separa la letra de la tilde (ej: "í" se convierte en "i" + "´")
    texto_normalizado = unicodedata.normalize('NFD', texto)
    # Filtra y se queda solo con las letras normales
    texto_limpio = ''.join(c for c in texto_normalizado if unicodedata.category(c) != 'Mn')
    return texto_limpio

#Funcion para construir el json a enviar
def construir_json(datos_pantalla,aula):
    print(datos_pantalla)

    #Formato json
    datos = {
        "actual": {},
        "proxima": {"hora": "--:--", "asignatura": "-"},
        "incidencias_restantes": []
    }

    if datos_pantalla["actual"] is not None:
        datos["actual"] = {
            "aula": aula,
            "asignatura": limpiar_textos(datos_pantalla["actual"]["asignatura"]), 
            "profesor": limpiar_textos(datos_pantalla["actual"]["profesor"]),
            "hora": datos_pantalla["actual"]["hora_inicio"]
        }
    else: #No hay clase
        datos["actual"] = {
            "aula": aula,
            "asignatura": "Aula Libre", 
            "profesor": "-",
            "hora": "--:--"
        }
    
    
    if datos_pantalla["proxima"] is not None:
        datos["proxima"]={"hora": datos_pantalla["proxima"]["hora_inicio"], "asignatura": limpiar_textos(datos_pantalla["proxima"]["asignatura"])}
    
    for incidencia in datos_pantalla["incidencias_restantes"]:
        datos["incidencias_restantes"].append({"hora": incidencia["hora_inicio"], "aviso": limpiar_textos(incidencia["asignatura"])})

    return json.dumps(datos)

#Funcion que pasa del formato HH:MM a los minutos, (pj: 12:00 seria igual a -> 12*60 = 720)
def hora_a_minutos(hora):
    partes = hora.split(":")
    horas = int(partes[0])
    minutos = int(partes[1])
    return (horas * 60) + minutos

#Función para obtener las aulas registradas en la base de datos
def obtener_aulas(cursor):
    consulta ="""
    SELECT DISTINCT id_espacio 
    FROM Horarios
    """

    cursor.execute(consulta)
    aulas = [fila[0] for fila in cursor.fetchall()]

    return aulas

#Función para sustituir las clases por las incidencias en sus horas en el horario del aula
def sustituir_incidencias(clases,incidencias):
    #Diccionario base con las clases normales (la clave es la hora de inicio de la clase)
    agenda = {}
    for c in clases:
        agenda[c[0]] = {
            "hora_inicio": c[0],
            "hora_fin": c[1],
            "asignatura": c[2],
            "profesor": c[3],
            "es_incidencia": False
        }

    #Recorrer las incidencias y sobrescribir si coinciden las horas
    for i in incidencias:
        hora_incidencia = i[0]
        tipo_aviso = i[1]

        #Si la hora de la incidencia coincide con alguna clase se hace la sustitucion
        if hora_incidencia in agenda:
            # Copiamos la hora_fin de la clase antes de sobrescribirla
            hora_fin_heredada = agenda[hora_incidencia]["hora_fin"]
        
            # Sobrescribimos el hueco de esa hora con los datos de la incidencia.
            agenda[hora_incidencia] = {
                "hora_inicio": hora_incidencia,
                "hora_fin": hora_fin_heredada,    
                "asignatura": tipo_aviso, # Colocamos el aviso donde iría la asignatura
                "profesor": "-",
                "es_incidencia": True
            }

    # Devolvemos el diccionario resultante de vuelta como una lista ordenada por horas
    lista_final = [agenda[hora] for hora in sorted(agenda.keys())]
    return lista_final

#Funcion para construir los datos a enviar
def obtener_horario_aula(aula,cursor):

    consultaHorarios ="""
    SELECT hora_inicio, hora_fin, asignatura, profesor 
    FROM Horarios
    WHERE id_espacio = ? AND dia = ?
    ORDER BY hora_inicio ASC
    """

    consultaIncidencias ="""
    SELECT hora, tipo_aviso 
    FROM Incidencias 
    WHERE id_espacio = ? AND fecha = ? AND activo = 1
    ORDER BY hora ASC
    """

    dia = datetime.now().strftime("%A")

    valores = (aula,dia)
    cursor.execute(consultaHorarios,valores)
    clases = cursor.fetchall()

    fecha = date.today().strftime("%Y-%m-%d")

    valores = (aula, fecha)
    cursor.execute(consultaIncidencias,valores)
    incidencias = cursor.fetchall()

    #Sustituimos las clases por sus respectivas incidencias
    horario = sustituir_incidencias(clases,incidencias)

    datos_pantalla = {
        "actual": None,
        "proxima": None,
        "incidencias_restantes": []
    }

    #Variable usada para ver si estamos entre 2 clases
    final_anterior = hora_a_minutos("00:00")

    for clase in horario:
        #Obtenemos la hora actual
        hora = datetime.now()
        minutos = hora.hour * 60 + hora.minute

        #hora2 = "10:00"
        #minutos = hora_a_minutos(hora2)
        
        minutos_inicio = hora_a_minutos(clase["hora_inicio"])   #Minutos hora_inicio
        minutos_final = hora_a_minutos(clase["hora_fin"])    #Minutos hora_final

        #La clase se esta dando
        if minutos_inicio <= minutos < minutos_final :
            datos_pantalla["actual"] = clase
            print(datos_pantalla["actual"])
        #La clase todavia no ha empezado
        elif final_anterior <= minutos < minutos_inicio :
            #print(fila)
            datos_pantalla["actual"] = clase
            print(datos_pantalla["actual"])
        #Comprobamos las siguientes clases para obtener la proxima clase y las incidencias futuras
        elif minutos_inicio > minutos:
            if clase["es_incidencia"]:
                datos_pantalla["incidencias_restantes"].append(clase)
            elif not clase["es_incidencia"] and datos_pantalla["proxima"] is None:
                datos_pantalla["proxima"] = clase

        final_anterior = minutos_final


    datos = construir_json(datos_pantalla,aula)
    return datos


if __name__ == "__main__":
    #Diccionario para no reenviar los mismos mensajes
    memoria_jsons = {}

    #Bucle principal
    while True:
        #Nos conectamos a la base de datos
        conn = sqlite3.connect("TFG.db")
        conn.execute("PRAGMA foreign_keys = ON;")
        conn.execute("PRAGMA journal_mode = WAL;") #Write-Ahead Logging
        cursor = conn.cursor()

        #Limpiamos incidencias de hace mas de dos semanas
        limpieza_incidencias(cursor)

        #Obtener todas las aulas
        aulas = obtener_aulas(cursor)
        print(aulas)

        #Obtiene el dia como un dia de la semana
        dia = datetime.now().strftime("%A")
        print("El dia es "+dia)

        #Recorremos cada aula para enviar los datos
        for aula in aulas:
            if aula not in memoria_jsons:
                memoria_jsons[aula] = json.dumps({"actual": {"aula": aula,"asignatura": "-","profesor": "-","hora": "--:--"},
                                            "proxima": {"hora": "--:--", "asignatura": "-"},
                                            "incidencias_restantes": []
                                            })
            if dia in horario_semanal:
                datos = obtener_horario_aula(aula, cursor)
                print(datos)
                mensaje_json = datos
            else:
                mensaje_json = json.dumps({"actual": {"aula": aula,"asignatura": "-","profesor": "-","hora": "--:--"},
                                            "proxima": {"hora": "--:--", "asignatura": "-"},
                                            "incidencias_restantes": []
                                            })
                print(mensaje_json)
                print("No hay clase hoy")
            #Si es el mismo mensaje no envia
            if mensaje_json!=memoria_jsons[aula] :
                #Nos conectamos al topic del aula 
                topic = "aula/"+aula
                client.publish(topic, mensaje_json, retain=True)
                print("Datos enviados")
                memoria_jsons[aula]=mensaje_json

        #Cerramos la conexion con la base de datos
        conn.close()

        #Tiempo de espera (De momento 10 segundos para pruebas)
        time.sleep(10)

    
