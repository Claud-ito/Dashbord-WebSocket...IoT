#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsServer.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
 
//====================================================
// CONFIGURACIÓN WIFI
//====================================================
const char* ssid = "mateos";
const char* password = "123456789a";
 
//====================================================
// CONFIGURACIÓN MQTT
//====================================================
const char* mqtt_server = "e0bd276ea891479e9b59fbf642aab2be.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "IOTdispensador";
const char* mqtt_pass = "Grupoa2026";
 
const char* TOPIC_COMANDOS = "robot/esp32/comandos";
const char* TOPIC_ESTADO = "robot/esp32/estado";
const char* TOPIC_DISTANCIA = "robot/esp32/distancia";
const char* TOPIC_RADAR = "robot/esp32/radar";
 
//====================================================
// PINES HC-SR04
//====================================================
#define TRIG_PIN 5
#define ECHO_PIN 18
 
//====================================================
// PINES L298N
//====================================================
#define ENA 26
#define IN1 27
#define IN2 14
#define IN3 12
#define IN4 13
#define ENB 19
 
//====================================================
// SERVO
//====================================================
#define PIN_SERVO 2
 
//====================================================
// OBJETOS
//====================================================
WiFiClientSecure espClient;
PubSubClient mqtt(espClient);
WebSocketsServer webSocket(81);
Servo servo;
 
//====================================================
// VARIABLES GLOBALES
//====================================================
int velocidad = 180;
bool modoAuto = true;
bool robotActivo = true;
 
const int CANTIDAD_PUNTOS = 9;
float angulosRadar[CANTIDAD_PUNTOS] = {20, 37.5, 55, 72.5, 90, 107.5, 125, 142.5, 160};
float distanciasRadar[CANTIDAD_PUNTOS] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
 
bool barridoIzquierdaDerecha = true; // true: 180 -> 0, false: 0 -> 180
float anguloServoActual = 90;
String movimientoActual = "detenido";
 
unsigned long tiempoRadar = 0;
const unsigned long INTERVALO_RADAR = 600;
const unsigned long DELAY_ESTABILIZACION_SERVO = 200;

int indiceAngulo(float angulo);
bool esperarEstabilizacionServo();
void publicarAnguloVivo();
void medirDistanciaManual();
 
//====================================================
// PROTOTIPOS
//====================================================
void conectarWiFi();
void reconectarMQTT();
void callbackMQTT(char* topic, byte* payload, unsigned int length);
void eventoWebSocket(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
 
float medirDistancia();
void ejecutarBarridoServo();
String crearJSONRadar();
void publicarRadar();
void publicarEstado(String estado);
 
void controlAutomaticoConRadar();
void ejecutarComando(String comando);
String extraerValorComando(String mensaje);
String extraerValorModo(String mensaje);
int extraerVelocidad(String mensaje);
float extraerAngulo(String mensaje);
 
void avanzar();
void retroceder();
void girarDerecha();
void girarIzquierda();
void detener();
 
//====================================================
// SETUP
//====================================================
void setup() {
  Serial.begin(115200);
 
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
 
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT)  ;
 
  servo.attach(PIN_SERVO);
  servo.write(90);
  detener();
 
  conectarWiFi();
  espClient.setInsecure();
 
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(callbackMQTT);
 
  webSocket.begin();
  webSocket.onEvent(eventoWebSocket);
 
  Serial.println("Sistema iniciado con radar de 9 puntos");
}
 
//====================================================
// LOOP PRINCIPAL
//====================================================
void loop() {
  if (!mqtt.connected()) {
    reconectarMQTT();
  }
 
  mqtt.loop();
  webSocket.loop();
 
  if (millis() - tiempoRadar >= INTERVALO_RADAR) {
    tiempoRadar = millis();

    if (modoAuto) {
      ejecutarBarridoServo();
      publicarRadar();
      if (robotActivo) {
        controlAutomaticoConRadar();
      }
    } else {
      medirDistanciaManual();
      publicarRadar();
    }
  }
}
 
//====================================================
// WIFI
//====================================================
void conectarWiFi() {
  Serial.print("Conectando a WiFi");
  WiFi.begin(ssid, password);
 
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
 
  Serial.println();
  Serial.println("WiFi conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}
 
//====================================================
// MQTT
//====================================================
void reconectarMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Conectando MQTT...");
 
    if (mqtt.connect("RobotESP32", mqtt_user, mqtt_pass)) {
      Serial.println(" conectado");
      mqtt.subscribe(TOPIC_COMANDOS);
      publicarEstado("Robot conectado");
    } else {
      Serial.print(" error rc=");
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}
 
void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  String mensaje = "";
 
  for (unsigned int i = 0; i < length; i++) {
    mensaje += (char)payload[i];
  }
 
  Serial.print("Comando MQTT: ");
  Serial.println(mensaje);
  ejecutarComando(mensaje);
}
 
void publicarEstado(String estado) {
  mqtt.publish(TOPIC_ESTADO, estado.c_str());
}
 
//====================================================
// WEBSOCKET
//====================================================
void eventoWebSocket(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
 
    Serial.println("Cliente WebSocket conectado");
 
    String jsonRadar = crearJSONRadar();
    webSocket.sendTXT(num, jsonRadar);
  }
 
  if (type == WStype_TEXT) {
 
    String comando = String((char*)payload);
 
    Serial.print("Comando WebSocket: ");
    Serial.println(comando);
 
    ejecutarComando(comando);
  }
}
 
//====================================================
// SENSOR HC-SR04
//====================================================
float medirDistancia() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
 
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
 
  digitalWrite(TRIG_PIN, LOW);
 
  long duracion = pulseIn(ECHO_PIN, HIGH, 30000);
 
  if (duracion == 0) {
    return 0;
  }
 
  return (duracion * 0.0343) / 2;
}
 
//====================================================
// RADAR: SERVO DE 9 PUNTOS
//====================================================
bool esperarEstabilizacionServo() {
  unsigned long inicio = millis();
  while (millis() - inicio < DELAY_ESTABILIZACION_SERVO) {
    webSocket.loop();
    mqtt.loop();
    if (!modoAuto) return false;
    delay(10);
  }
  return true;
}

void ejecutarBarridoServo() {
  if (barridoIzquierdaDerecha) {
    // Izquierda -> derecha: 160° -> 20° (índices 8 -> 0)
    for (int i = CANTIDAD_PUNTOS - 1; i >= 0 && modoAuto; i--) {
      anguloServoActual = angulosRadar[i];
      servo.write((int)round(angulosRadar[i]));
      if (!esperarEstabilizacionServo()) return;
      distanciasRadar[i] = medirDistancia();
      publicarAnguloVivo();
    }
  } else {
    // Derecha -> izquierda: 20° -> 160° (índices 0 -> 8)
    for (int i = 0; i < CANTIDAD_PUNTOS && modoAuto; i++) {
      anguloServoActual = angulosRadar[i];
      servo.write((int)round(angulosRadar[i]));
      if (!esperarEstabilizacionServo()) return;
      distanciasRadar[i] = medirDistancia();
      publicarAnguloVivo();
    }
  }

  if (!modoAuto) return;

  barridoIzquierdaDerecha = !barridoIzquierdaDerecha;
  anguloServoActual = 90;
  servo.write(90);
}

int indiceAngulo(float angulo) {
  for (int i = 0; i < CANTIDAD_PUNTOS; i++) {
    if (abs(angulosRadar[i] - angulo) < 0.1) {
      return i;
    }
  }
  return -1;
}

void medirDistanciaManual() {
  int indice = indiceAngulo(anguloServoActual);
  for (int i = 0; i < CANTIDAD_PUNTOS; i++) {
    distanciasRadar[i] = 0;
  }
  if (indice >= 0) {
    distanciasRadar[indice] = medirDistancia();
  }
}

void publicarAnguloVivo() {
  String json = "{";
  json += "\"tipo\":\"angulo\",";
  json += "\"anguloServo\":" + String(anguloServoActual, 1) + ",";
  json += "\"modoAuto\":" + String(modoAuto ? "true" : "false") + ",";
  json += "\"movimiento\":\"" + movimientoActual + "\"";
  json += "}";
  webSocket.broadcastTXT(json);
}
 
String crearJSONRadar() {
  String json = "{";
  json += "\"tipo\":\"radar\",";
  json += "\"anguloServo\":" + String(anguloServoActual, 1) + ",";
  json += "\"velocidadMotores\":" + String(velocidad) + ",";
  json += "\"modoAuto\":" + String(modoAuto ? "true" : "false") + ",";
  json += "\"robotActivo\":" + String(robotActivo ? "true" : "false") + ",";
  json += "\"movimiento\":\"" + movimientoActual + "\",";
 
  json += "\"angulos\":[";
  for (int i = 0; i < CANTIDAD_PUNTOS; i++) {
    json += String(angulosRadar[i], 1);
    if (i < CANTIDAD_PUNTOS - 1) json += ",";
  }
  json += "],";
 
  json += "\"distancias\":[";
  for (int i = 0; i < CANTIDAD_PUNTOS; i++) {
    json += String(distanciasRadar[i], 1);
    if (i < CANTIDAD_PUNTOS - 1) json += ",";
  }
  json += "]}";
 
  return json;
}
 
void publicarRadar() {
  String json = crearJSONRadar();
  mqtt.publish(TOPIC_RADAR, json.c_str());
  mqtt.publish(TOPIC_DISTANCIA, json.c_str());
  webSocket.broadcastTXT(json);
 
  Serial.println(json);
}
 
//====================================================
// CONTROL AUTOMÁTICO USANDO RADAR
//====================================================
void controlAutomaticoConRadar() {
  int indiceMayor = 0;
  float mayorDistancia = distanciasRadar[0];
 
  for (int i = 1; i < CANTIDAD_PUNTOS; i++) {
    if (distanciasRadar[i] > mayorDistancia) {
      mayorDistancia = distanciasRadar[i];
      indiceMayor = i;
    }
  }
 
  if (mayorDistancia == 0) {
    detener();
    publicarEstado("Sin lectura del sensor");
    return;
  }
 
  if (mayorDistancia > 20) {
    float mejorAngulo = angulosRadar[indiceMayor];
 
    if (mejorAngulo < 67.5) {
      girarDerecha();
      publicarEstado("Ruta libre a la derecha");
    } else if (mejorAngulo > 112.5) {
      girarIzquierda();
      publicarEstado("Ruta libre a la izquierda");
    } else {
      avanzar();
      publicarEstado("Ruta libre al frente");
    }
  } else {
    detener();
    publicarEstado("Obstaculo detectado en todos los puntos");
    delay(150);
    retroceder();
    delay(250);
    detener();
  }
}
 
//====================================================
// COMANDOS
//====================================================
void ejecutarComando(String mensaje) {
  mensaje.trim();
  String comando = extraerValorComando(mensaje);
  comando.toLowerCase();
 
  if (comando == "auto" || comando == "automatico") {
    modoAuto = true;
    publicarEstado("Modo automatico activado");
    publicarAnguloVivo();
  } else if (comando == "manual" || comando == "mecanico") {
    modoAuto = false;
    detener();
    publicarEstado("Modo manual activado");
    publicarAnguloVivo();
  } else if (comando == "modoservo") {
    String valor = extraerValorModo(mensaje);
    valor.toLowerCase();
    if (valor == "automatico" || valor == "auto") {
      modoAuto = true;
      publicarEstado("Modo automatico activado");
    } else if (valor == "mecanico" || valor == "manual") {
      modoAuto = false;
      detener();
      publicarEstado("Modo manual activado");
    }
    publicarAnguloVivo();
  } else if (comando == "avanzar") {
    modoAuto = false;
    avanzar();
    publicarEstado("Avanzando manual");
  } else if (comando == "retroceder") {
    modoAuto = false;
    retroceder();
    publicarEstado("Retrocediendo manual");
  } else if (comando == "derecha") {
    modoAuto = false;
    girarDerecha();
    publicarEstado("Girando derecha manual");
  } else if (comando == "izquierda") {
    modoAuto = false;
    girarIzquierda();
    publicarEstado("Girando izquierda manual");
  } else if (comando == "detener" || comando == "parar") {
    modoAuto = false;
    detener();
    publicarEstado("Robot detenido");
  } else if (comando == "on") {
    robotActivo = true;
    publicarEstado("Robot activo");
  } else if (comando == "off") {
    robotActivo = false;
    detener();
    publicarEstado("Robot apagado");
  } else if (comando == "setvelocidad") {
    velocidad = constrain(extraerVelocidad(mensaje), 0, 255);
    publicarEstado("Velocidad actualizada");
  } else if (comando == "moverservo") {
    float angulo = extraerAngulo(mensaje);
    anguloServoActual = angulo;
    servo.write((int)round(angulo));
    delay(DELAY_ESTABILIZACION_SERVO);
    medirDistanciaManual();
    publicarEstado("Servo movido manualmente");
    publicarRadar();
  }
}

String extraerValorModo(String mensaje) {
  int pos = mensaje.indexOf("\"valor\"");
  if (pos < 0) return "";

  int dosPuntos = mensaje.indexOf(":", pos);
  int comilla1 = mensaje.indexOf("\"", dosPuntos + 1);
  if (comilla1 >= 0) {
    int comilla2 = mensaje.indexOf("\"", comilla1 + 1);
    if (comilla2 >= 0) {
      return mensaje.substring(comilla1 + 1, comilla2);
    }
  }

  int coma = mensaje.indexOf(",", dosPuntos + 1);
  int fin = coma > 0 ? coma : mensaje.indexOf("}", dosPuntos + 1);
  String valor = mensaje.substring(dosPuntos + 1, fin);
  valor.trim();
  valor.replace("\"", "");
  return valor;
}
 
String extraerValorComando(String mensaje) {
  if (!mensaje.startsWith("{")) {
    return mensaje;
  }
 
  int pos = mensaje.indexOf("\"comando\"");
  if (pos < 0) return mensaje;
 
  int dosPuntos = mensaje.indexOf(":", pos);
  int comilla1 = mensaje.indexOf("\"", dosPuntos + 1);
  int comilla2 = mensaje.indexOf("\"", comilla1 + 1);
 
  if (comilla1 < 0 || comilla2 < 0) return mensaje;
  return mensaje.substring(comilla1 + 1, comilla2);
}
 
int extraerVelocidad(String mensaje) {
  int pos = mensaje.indexOf("\"valor\"");
  if (pos < 0) return velocidad;
 
  int dosPuntos = mensaje.indexOf(":", pos);
  int coma = mensaje.indexOf(",", dosPuntos + 1);
  int fin = coma > 0 ? coma : mensaje.indexOf("}", dosPuntos + 1);
 
  return mensaje.substring(dosPuntos + 1, fin).toInt();
}
 
float extraerAngulo(String mensaje) {
  int pos = mensaje.indexOf("\"angulo\"");
  if (pos < 0) return anguloServoActual;
 
  int dosPuntos = mensaje.indexOf(":", pos);
  int coma = mensaje.indexOf(",", dosPuntos + 1);
  int fin = coma > 0 ? coma : mensaje.indexOf("}", dosPuntos + 1);
 
  return mensaje.substring(dosPuntos + 1, fin).toFloat();
}
 
//====================================================
// CONTROL DE MOTORES
//====================================================
void avanzar() {
  movimientoActual = "avanzar";
  analogWrite(ENA, velocidad);
  analogWrite(ENB, velocidad);
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}
 
void retroceder() {
  movimientoActual = "retroceder";
  analogWrite(ENA, velocidad);
  analogWrite(ENB, velocidad);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}
 
void girarDerecha() {
  movimientoActual = "derecha";
  analogWrite(ENA, velocidad);
  analogWrite(ENB, velocidad);
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}
 
void girarIzquierda() {
  movimientoActual = "izquierda";
  analogWrite(ENA, velocidad);
  analogWrite(ENB, velocidad);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}
 
void detener() {
  movimientoActual = "detenido";
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}
 