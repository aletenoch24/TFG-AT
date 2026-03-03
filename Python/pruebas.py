import sqlite3
import time
from datetime import datetime



def hora_a_minutos(hora):
    partes = hora.split(":")
    horas = int(partes[0])
    minutos = int(partes[1])
    return (horas * 60) + minutos


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

    #hora = datetime.now()
    #minutos = hora.hour * 60 + hora.minute
    hora = "19:35"
    minutos = hora_a_minutos(hora)
    final_anterior = hora_a_minutos("24:00")

    for fila in clases:
        minutos_inicio = hora_a_minutos(fila[0])
        minutos_final = hora_a_minutos(fila[1])


        if minutos_inicio <= minutos < minutos_final :
            #print(fila)
            return fila[0], fila[2], fila[3]            #Devuelve Hora inicio, Asignatura, Profesor
        elif final_anterior <= minutos < minutos_inicio :
            #print(fila)
            return fila[0], fila[2], fila[3]  

        #Variable para comprobar si estamos entre 2 clases
        final_anterior = minutos_final
        
        



    

clase_actual("H1.10") 

