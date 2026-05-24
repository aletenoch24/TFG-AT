// Archivo: static/js/app.js
function cambiarFormulario() {
    var cajaLogin = document.getElementById('caja-login');
    var cajaRegistro = document.getElementById('caja-registro');

    if (cajaLogin.style.display !== 'none') {
        cajaLogin.style.display = 'none';
        cajaRegistro.style.display = 'block';
    } else {
        cajaLogin.style.display = 'block';
        cajaRegistro.style.display = 'none';
    }
}