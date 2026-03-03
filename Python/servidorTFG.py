import paho.mqtt.client as mqtt
import json
import time
from datetime import datetime, date
import sqlite3

#Datos para la conexion MQTT
broker = "192.168.1.14"
puerto = 1883
topic = "aula/H1.10"

#Crea el cliente
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

client.connect(broker, puerto, 60)

client.subscribe(topic)

#Horario provisional
horario_semanal = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]

#Aula, de momento vamos a poner que siempre sea la misma porque solo trabajamos con una pantalla (una clase)
aula = "H1.10"

duracion_clase = 120 #Minutos que dura una clase

#Mensaje Json que almacena el ultimo mensaje enviado para no enviar constantemente el mismo mensaje
json_memoria = json.dumps({
    "aula": "-",
    "asignatura":"-",
    "profesor": "-",
    "hora": "--:--"
})


#Limpia los datos de la tabla incidencias si han pasado 14 desde la incidencia
def limpieza_incidencias():
    conn = sqlite3.connect("TFG.db")
    cursor = conn.cursor()

    consulta_limpieza = "DELETE FROM Incidencias WHERE fecha <= date('now', '-14 days')"

    cursor.execute(consulta_limpieza)
    conn.commit()
    filas_borradas = cursor.rowcount
    if filas_borradas > 0:
        print(f"Limpieza: Se han borrado {filas_borradas} incidencias antiguas.")
        
    conn.close()

#Funcion para construir un json de los valores obtenidos del horario
def construir_json(datos):

    print(datos)

    h = datos[0]
    asig=datos[1]
    prof = datos[2]

    datos = {
        "aula": aula,
        "asignatura": asig,
        "profesor": prof,
        "hora": h
    }

    return json.dumps(datos)

#Funcion que pasa del formato de por ejemplo 12:00 a los minutos, 12*60 = 720
def hora_a_minutos(hora):
    partes = hora.split(":")
    horas = int(partes[0])
    minutos = int(partes[1])
    return (horas * 60) + minutos


#Funcion que obtiene los datos de la clase actual
def clase_actual(aula) :
    conn = sqlite3.connect("TFG.db")

    conn.execute("PRAGMA foreign_keys = ON;")

    cursor = conn.cursor()  

    dia = datetime.now().strftime("%A")

    consulta ="""
    SELECT hora_inicio, hora_fin, asignatura, profesor 
    FROM Horarios
    WHERE id_espacio = ? AND dia = ?
    ORDER BY hora_inicio ASC
    """

    valores = (aula,dia)

    cursor.execute(consulta,valores)

    clases = cursor.fetchall()

    conn.close()

    hora = datetime.now()
    minutos = hora.hour * 60 + hora.minute

    #Variables de prueba para probar distintas horas (por si estoy trabajando a las 23 y no hay clases en esa hora)
    #hora = "22:00"
    #minutos = hora_a_minutos(hora)

    #Variable para comprobar si estamos entre 2 clases
    final_anterior = hora_a_minutos("00:00")

    for fila in clases:
        minutos_inicio = hora_a_minutos(fila[0])
        minutos_final = hora_a_minutos(fila[1])

        
        if minutos_inicio <= minutos < minutos_final :
            #print(fila)
            return fila[0], fila[2], fila[3]            #Devuelve Hora inicio, Asignatura, Profesor
        elif final_anterior <= minutos < minutos_inicio :
            #print(fila)
            return fila[0], fila[2], fila[3]  

        final_anterior = minutos_final
    
    #En caso de no haber clase:
    return None

#Funcion que comprueba si hay incidencia en la clase actual
def comprobar_incidencia(datos):

    conn = sqlite3.connect("TFG.db")

    conn.execute("PRAGMA foreign_keys = ON;")

    cursor = conn.cursor()  


    consulta ="""
    SELECT hora, tipo_aviso 
    FROM Incidencias 
    WHERE id_espacio = ? AND hora = ? AND fecha = ? AND activo = 1
    """

    hora_clase = datos[0]
    fecha = date.today().strftime("%Y-%m-%d")

    valores = (aula, hora_clase, fecha)

    cursor.execute(consulta,valores)

    clases = cursor.fetchall()

    conn.close()

    

    if clases:
        clase = list(clases[0])
        clase.append("-")
        print(clase)

        return clase
    else:
        return None




limpieza_incidencias()

#Bucle principal
while True:

    #Obtiene el dia como un dia de la semana
    dia = datetime.now().strftime("%A")
    print("El dia es "+dia)

    if dia in horario_semanal:

        #Obtenemos los datos de la clase que toca
        datos = clase_actual(aula)

        clase_incidencia = comprobar_incidencia(datos)

        #Si hay clase
        if datos:
            clase_incidencia = comprobar_incidencia(datos)
            #Si hay una incidencia en la clase
            if clase_incidencia:
                mensaje_json = construir_json(clase_incidencia)
                print(mensaje_json)
            #Clase normal
            else: 
                mensaje_json = construir_json(datos)
                print(mensaje_json)
        #Si no hay clase
        else: 
            mensaje_json = construir_json(["--:--","Aula libre","-"])
            print(mensaje_json)
            print("No hay clase ahora")

    else:
        mensaje_json = construir_json(["Aula libre","-","--:--"])
        print(mensaje_json)
        print("No hay clase hoy")

    #Si es el mismo mensaje no envia
    if mensaje_json!=json_memoria :
        client.publish(topic, mensaje_json, retain=True)
        print("Datos enviados")
        json_memoria=mensaje_json

    #Tiempo de espera (De momento 10 segundos para pruebas)
    time.sleep(10)
