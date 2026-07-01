// --- CONFIGURACIÓN ---
const ANGULOS_PERMITIDOS = [20, 37.5, 55, 72.5, 90, 107.5, 125, 142.5, 160];
const WS_PUERTO = 81;

let modoServoActual = 'automatico';
let websocket = null;
let reconectarTimeout = null;
let conexionActiva = false;
let ipActual = '';

// --- AL CARGAR LA PÁGINA ---
window.addEventListener('load', () => {
    inicializarRadarVisual();

    const inputIP = document.getElementById('inputIP');
    const ipGuardada = localStorage.getItem('esp32_ip');
    if (ipGuardada) {
        inputIP.value = ipGuardada;
    }

    inputIP.addEventListener('keydown', (evento) => {
        if (evento.key === 'Enter') {
            conectarConIP();
        }
    });
});

function inicializarRadarVisual() {
    const pantalla = document.getElementById('radarPantalla');
    pantalla.innerHTML = '';

    ANGULOS_PERMITIDOS.forEach((angulo, indice) => {
        const punto = document.createElement('div');
        punto.className = 'radar-punto';
        punto.id = `punto-radar-${indice}`;

        const rad = (angulo * Math.PI) / 180;
        const radioX = 35;
        const radioY = 65;
        const x = 50 + radioX * Math.cos(rad);
        const y = 85 - radioY * Math.sin(rad);

        punto.style.position = 'absolute';
        punto.style.left = `${x}%`;
        punto.style.top = `${y}%`;
        punto.style.width = '20px';
        punto.style.height = '20px';
        punto.style.backgroundColor = '#333';
        punto.style.borderRadius = '50%';
        punto.style.transform = 'translate(-50%, -50%)';
        punto.style.border = '2px solid #555';
        punto.style.transition = 'all 0.3s ease';
        punto.setAttribute('title', `${angulo}°`);
        pantalla.appendChild(punto);
    });

    ANGULOS_PERMITIDOS.forEach((angulo) => {
        const divAngulo = document.createElement('div');
        divAngulo.className = 'radar-angulo';

        const rad = (angulo * Math.PI) / 180;
        const radioX = 25;
        const radioY = 40;
        const x = 48 + radioX * Math.cos(rad);
        const y = 81 - radioY * Math.sin(rad);

        divAngulo.style.position = 'absolute';
        divAngulo.style.left = `${x}%`;
        divAngulo.style.top = `${y}%`;
        divAngulo.style.fontWeight = 'bold';
        divAngulo.style.fontSize = '20px';
        divAngulo.style.color = '#00ff88';
        divAngulo.textContent = `${angulo}°`;
        pantalla.appendChild(divAngulo);
    });
}

function validarIP(ip) {
    const partes = ip.trim().split('.');
    if (partes.length !== 4) return false;
    return partes.every((parte) => {
        const num = parseInt(parte, 10);
        return !isNaN(num) && num >= 0 && num <= 255 && String(num) === parte;
    });
}

function obtenerIPIngresada() {
    return document.getElementById('inputIP').value.trim();
}

function conectarConIP() {
    const ip = obtenerIPIngresada();
    const errorDiv = document.getElementById('errorIP');
    const inputIP = document.getElementById('inputIP');

    if (!validarIP(ip)) {
        errorDiv.innerText = 'IP inválida. Ejemplo: 192.168.1.45';
        inputIP.style.borderColor = '#ff4d4d';
        return;
    }

    errorDiv.innerText = '';
    inputIP.style.borderColor = '#444';
    localStorage.setItem('esp32_ip', ip);

    cerrarWebSocket(false);
    ipActual = ip;
    conexionActiva = true;
    conectarWebSocket();
}

// --- WEBSOCKET (ESP32 puerto 81) ---
function conectarWebSocket() {
    if (!conexionActiva || !ipActual) return;

    if (websocket && (websocket.readyState === WebSocket.OPEN || websocket.readyState === WebSocket.CONNECTING)) {
        return;
    }

    actualizarEstadoWS('Conectando...', '#ff9f43');

    websocket = new WebSocket(`ws://${ipActual}:${WS_PUERTO}`);

    websocket.onopen = () => {
        actualizarEstadoWS(`Conectado (${ipActual})`, '#00ff88');
        if (reconectarTimeout) {
            clearTimeout(reconectarTimeout);
            reconectarTimeout = null;
        }
    };

    websocket.onmessage = (evento) => {
        try {
            const datos = JSON.parse(evento.data);
            procesarDatosEntrantes(datos);
        } catch (error) {
            console.error('JSON inválido del ESP32:', evento.data, error);
        }
    };

    websocket.onerror = () => {
        actualizarEstadoWS('Error de conexión', '#ff4d4d');
    };

    websocket.onclose = () => {
        websocket = null;
        if (!conexionActiva) {
            actualizarEstadoWS('Desconectado', '#ff4d4d');
            return;
        }
        actualizarEstadoWS('Desconectado - reintentando...', '#ff9f43');
        reconectarTimeout = setTimeout(conectarWebSocket, 3000);
    };
}

function cerrarWebSocket(detenerReconexion) {
    if (detenerReconexion) {
        conexionActiva = false;
    }
    if (reconectarTimeout) {
        clearTimeout(reconectarTimeout);
        reconectarTimeout = null;
    }
    if (websocket) {
        websocket.onclose = null;
        websocket.close();
        websocket = null;
    }
}

function actualizarEstadoWS(texto, color) {
    const estado = document.getElementById('estadoWS');
    estado.innerText = texto;
    estado.style.color = color;
}

function enviarComandoWS(paquete) {
    if (!websocket || websocket.readyState !== WebSocket.OPEN) {
        console.warn('WebSocket no conectado. Comando no enviado:', paquete);
        document.getElementById('errorIP').innerText = 'Conecta primero al ESP32 con su IP actual.';
        return false;
    }
    const json = JSON.stringify(paquete);
    websocket.send(json);
    console.log('Enviado al ESP32 ->', json);
    return true;
}

function procesarDatosEntrantes(datos) {
    if (datos.anguloServo !== undefined) {
        document.getElementById('anguloTexto').innerText = `${datos.anguloServo}°`;
    }

    if (datos.modoAuto !== undefined) {
        sincronizarModoUI(datos.modoAuto);
    }

    if (datos.velocidadMotores !== undefined) {
        document.getElementById('velocidadEstadoTexto').innerText = datos.velocidadMotores;
        const porcenVel = (datos.velocidadMotores / 255) * 100;
        document.getElementById('barraProgresoVel').style.width = `${porcenVel}%`;
    }

    if (datos.movimiento !== undefined) {
        const movimientoEl = document.getElementById('movimientoTexto');
        if (movimientoEl) {
            movimientoEl.innerText = datos.movimiento.toUpperCase();
        }
    }

    if (datos.tipo === 'radar' && Array.isArray(datos.distancias)) {
        datos.distancias.forEach((distancia, indice) => {
            const puntoVisual = document.getElementById(`punto-radar-${indice}`);
            if (!puntoVisual) return;

            if (distancia <= 0) {
                puntoVisual.style.backgroundColor = '#333';
                puntoVisual.style.boxShadow = 'none';
            } else if (distancia < 30) {
                puntoVisual.style.backgroundColor = '#ff4d4d';
                puntoVisual.style.boxShadow = '0 0 12px #ff4d4d';
            } else if (distancia < 70) {
                puntoVisual.style.backgroundColor = '#ff9f43';
                puntoVisual.style.boxShadow = '0 0 8px #ff9f43';
            } else {
                puntoVisual.style.backgroundColor = '#00ff88';
                puntoVisual.style.boxShadow = '0 0 5px #00ff88';
            }
        });
    }
}

function sincronizarModoUI(modoAuto) {
    const modo = modoAuto ? 'automatico' : 'mecanico';
    if (modoServoActual === modo) return;

    modoServoActual = modo;
    const btnAuto = document.getElementById('btnAutomatico');
    const btnMec = document.getElementById('btnMecanico');
    const panelManual = document.getElementById('controlManualBox');
    const inputAng = document.getElementById('inputAngulo');
    const btnEnv = document.getElementById('btnEnviarAngulo');

    if (modo === 'automatico') {
        btnAuto.classList.add('activo');
        btnMec.classList.remove('activo');
        panelManual.classList.add('disabled');
        inputAng.disabled = true;
        btnEnv.disabled = true;
    } else {
        btnMec.classList.add('activo');
        btnAuto.classList.remove('activo');
        panelManual.classList.remove('disabled');
        inputAng.disabled = false;
        btnEnv.disabled = false;
    }
}

// --- ACCIONES ENVIADAS (Cliente -> ESP32) ---

function cambiarModoServo(modo) {
    modoServoActual = modo;
    const btnAuto = document.getElementById('btnAutomatico');
    const btnMec = document.getElementById('btnMecanico');
    const panelManual = document.getElementById('controlManualBox');
    const inputAng = document.getElementById('inputAngulo');
    const btnEnv = document.getElementById('btnEnviarAngulo');

    if (modo === 'automatico') {
        btnAuto.classList.add('activo');
        btnMec.classList.remove('activo');
        panelManual.classList.add('disabled');
        inputAng.disabled = true;
        btnEnv.disabled = true;
        enviarComandoWS({ comando: 'automatico' });
    } else {
        btnMec.classList.add('activo');
        btnAuto.classList.remove('activo');
        panelManual.classList.remove('disabled');
        inputAng.disabled = false;
        btnEnv.disabled = false;
        enviarComandoWS({ comando: 'mecanico' });
    }
}

function anguloEsValido(valor) {
    return ANGULOS_PERMITIDOS.some((a) => Math.abs(a - valor) < 0.01);
}

function enviarAnguloManual() {
    const input = document.getElementById('inputAngulo');
    const errorDiv = document.getElementById('errorAngulo');
    const valorNum = parseFloat(input.value);

    if (anguloEsValido(valorNum)) {
        errorDiv.innerText = '';
        input.style.borderColor = '#444';
        enviarComandoWS({ comando: 'moverservo', angulo: valorNum });
    } else {
        errorDiv.innerText = 'Ángulo no válido. Usa: 20, 37.5, 55, 72.5, 90, 107.5, 125, 142.5, 160';
        input.style.borderColor = '#ff4d4d';
    }
}

function actualizarSliderVelocidad(valor) {
    document.getElementById('velFeedback').innerText = valor;
    enviarComandoWS({ comando: 'setvelocidad', valor: parseInt(valor, 10) });
}

function enviarComandoMotor(direccion) {
    const mapaComandos = {
        izquierda: 'izquierda',
        derecha: 'derecha',
        parar: 'detener',
        detener: 'detener',
        avanzar: 'avanzar',
        retroceder: 'retroceder'
    };
    const comando = mapaComandos[direccion] || direccion;
    enviarComandoWS({ comando });
}

function cerrarConexion() {
    cerrarWebSocket(true);
    actualizarEstadoWS('Desconectado permanentemente', '#ff4d4d');
    document.getElementById('controlManualBox').classList.add('disabled');
}
