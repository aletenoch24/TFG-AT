from flask import *
import sqlite3

# 1. Inicializamos la aplicación Flask
app = Flask(__name__)

app.secret_key = 'clave_secreta_para_flash'

# 2. Definimos la ruta principal (la raíz de la web)
@app.route('/', methods=['GET', 'POST'])
def index():
    mensaje_exitoso = ""
    if request.method == "POST":
        aula = request.form.get("aula")
        estado = request.form.get("estado")
        hora = request.form.get("hora")
        fecha = request.form.get("fecha")

        conn = sqlite3.connect("TFG.db")
        cursor = conn.cursor()
        cursor.execute("INSERT INTO Incidencias (id_espacio, tipo_aviso, hora, fecha, activo) VALUES (?, ?, ?, ?, ?)", (aula, estado, hora, fecha, 1))
        conn.commit()
        conn.close()

        mensaje_exitoso = f"El aula {aula} muestra: '{estado}', a las {hora}, el dia {fecha}"
        flash(mensaje_exitoso)
    
        return redirect(url_for('index'))
    
    
    return render_template('index.html')




if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)