from flask import *
import sqlite3
import bcrypt


# Inicializamos la aplicación Flask
app = Flask(__name__)

app.secret_key = 'clave_secreta_para_flash'

# RUTA DE FORMULARIO
@app.route('/', methods=['GET', 'POST'])
def index():

    if 'usuario' not in session:
        return redirect(url_for('login'))

    # Si es un admin y entra aquí por error, lo mandamos a su panel
    if session.get('rol') == 'admin':
        return redirect(url_for('admin'))

    mensaje_exitoso = ""
    if request.method == "POST":
        aula = request.form.get("aula")
        estado = request.form.get("estado")
        hora = request.form.get("hora")
        fecha = request.form.get("fecha")

        conn = sqlite3.connect("TFG.db")
        conn.execute("PRAGMA foreign_keys = ON;")
        conn.execute("PRAGMA journal_mode = WAL;")
        cursor = conn.cursor()
        cursor.execute("INSERT INTO Incidencias (id_espacio, tipo_aviso, hora, fecha, activo) VALUES (?, ?, ?, ?, ?)", (aula, estado, hora, fecha, 1))
        conn.commit()
        conn.close()

        mensaje_exitoso = f"El aula {aula} muestra: '{estado}', a las {hora}, el dia {fecha}"
        flash(mensaje_exitoso)
    
        return redirect(url_for('index'))
    
    
    return render_template('index.html')

# RUTA DE ADMIN (PANEL DE BATERIAS)
@app.route('/admin')
def admin():
    if 'usuario' not in session:
        return redirect(url_for('login'))
        
    # Protegemos la ruta para que un profesor no pueda entrar aquí
    if session.get('rol') != 'admin':
        flash("Acceso denegado. No tienes permisos de administrador.")
        return redirect(url_for('index'))

    baterias = [] # Por defecto, una lista vacía
    try:
        conn = sqlite3.connect("TFG.db")
        cursor = conn.cursor()
        # Obtenemos el estado de todas las pantallas
        cursor.execute("SELECT id_espacio, porcentaje, ultima_actualizacion FROM Baterias ORDER BY id_espacio ASC")
        baterias = cursor.fetchall()
        conn.close()
    except sqlite3.OperationalError:
        # Si la tabla Baterias no existe todavía mostraremos una lista vacía y un aviso.
        flash("La tabla de monitorización aún no existe. Esperando datos de las pantallas...")
    except Exception as e:
        flash(f"Error al acceder a la base de datos: {e}")

    return render_template('admin.html', baterias=baterias)

# RUTA DE LOGIN
@app.route('/login', methods=['GET', 'POST'])
def login():
    # Si el usuario ya había iniciado sesión, no le mostramos el login, lo mandamos al panel
    if 'usuario' in session:
        return redirect(url_for('index'))

    if request.method == 'POST':
        usuario_intento = request.form.get('usuario')
        # Bcrypt necesita que la contraseña esté en formato "bytes", por eso usamos .encode('utf-8')
        password_intento = request.form.get('password').encode('utf-8')

        # Conectamos a la base de datos para buscar a este profesor
        conn = sqlite3.connect('TFG.db') 
        conn.execute("PRAGMA foreign_keys = ON;")
        conn.execute("PRAGMA journal_mode = WAL;")
        cursor = conn.cursor()
        # Ahora pedimos también el rol a la base de datos
        cursor.execute("SELECT contraseña, rol FROM usuarios WHERE usuario = ?", (usuario_intento,))
        resultado = cursor.fetchone()
        conn.close()

        # Si el usuario existe, resultado no estará vacío
        if resultado:
            # Recuperamos el hash de la base de datos (y lo pasamos a bytes)
            password_hash_db = resultado[0].encode('utf-8')
            rol_db = resultado[1]
            
            # Comprobamos si coinciden
            if bcrypt.checkpw(password_intento, password_hash_db):
                # Guardamos su nombre y su rol en la sesión
                session['usuario'] = usuario_intento
                session['rol'] = rol_db
                
                if rol_db == 'admin':
                    return redirect(url_for('admin'))
                else:
                    return redirect(url_for('index'))
            else:
                flash("Contraseña incorrecta.")
        else:
            flash("El usuario no existe.")
            
        return redirect(url_for('login'))

    # Si entra por GET, le mostramos el formulario
    return render_template('login.html')


# RUTA DE REGISTRO 
@app.route('/registro', methods=['POST'])
def registro():
    usuario_nuevo = request.form.get('usuario')
    password_plana = request.form.get('password').encode('utf-8')

    # Generamos la sal y el hash irreversible
    sal = bcrypt.gensalt()
    password_hash = bcrypt.hashpw(password_plana, sal)

    try:
        conn = sqlite3.connect('TFG.db') 
        conn.execute("PRAGMA foreign_keys = ON;")
        conn.execute("PRAGMA journal_mode = WAL;")
        cursor = conn.cursor()
        # Guardamos el hash en la base de datos (.decode('utf-8') lo convierte de bytes a texto normal para SQLite)
        cursor.execute("INSERT INTO usuarios (usuario, contraseña) VALUES (?, ?)", (usuario_nuevo, password_hash.decode('utf-8')))
        conn.commit()
        conn.close()
        
        session['usuario'] = usuario_nuevo
        session['rol'] = 'profesor' # Al registrarse por la web, es profesor por defecto
        
        # Le damos una bienvenida personalizada
        flash(f"¡Bienvenido/a, {usuario_nuevo}! Tu cuenta ha sido creada y ya estás dentro.")
        
        # Lo mandamos directo al panel principal (inicio)
        return redirect(url_for('index'))
    except sqlite3.IntegrityError:
        # Esto salta si en tu base de datos pusiste que la columna 'usuario' fuera UNIQUE
        flash("Ese nombre de usuario ya está cogido.")
    except Exception as e:
        flash(f"Error en la base de datos: {e}")

    finally:
        conn.close()

    # Tras registrarse, lo mandamos de vuelta al login
    return redirect(url_for('login'))


#  RUTA DE LOGOUT 
@app.route('/logout')
def logout():
    # Borramos al usuario de la sesión
    session.pop('usuario', None)
    session.pop('rol', None)
    return redirect(url_for('login'))




if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)