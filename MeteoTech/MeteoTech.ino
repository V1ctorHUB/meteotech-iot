// MeteoTech IoT - mini estación meteorológica con ESP32.
// Lee DHT11, BMP280 y LDR, publica en Adafruit IO por MQTT y señala con LED RGB.
// Serial 115200: 'd' = modo demo del LED, 'w' = simula caída de WiFi.

// ===================== LIBRERÍAS =====================
#include <WiFi.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_BMP280.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#include "credenciales.h"

// ===================== CONFIGURACIÓN =====================
// ---------------- PINES DEL ESP32 ----------------
const int PIN_DHT = 4;
const int PIN_LDR = 32;        // ADC1: el ADC2 no funciona con el WiFi activo
const int PIN_SDA = 21;        // I2C por defecto
const int PIN_SCL = 22;
const int PIN_LED_ROJO  = 19;  // 220 ohm por color
const int PIN_LED_VERDE = 18;
const int PIN_LED_AZUL  = 17;

// true si el LED RGB es de ánodo común (se enciende con LOW)
const bool LED_ANODO_COMUN = false;

// ---------------- UMBRALES DEL LED ----------------
const float TEMP_BAJA = 20.0;          // °C. Por debajo -> AZUL
const float TEMP_ALTA = 30.0;          // °C. Por encima -> ROJO
const float HISTERESIS_C = 0.5;        // °C. Margen para que el LED no parpadee cerca de un umbral
const float CAIDA_PRESION_HPA = 1.0;   // hPa de caída en la ventana -> ROJO

// ---------------- TENDENCIA DE PRESIÓN ----------------
const int VENTANA_MINUTOS = 30;        // 1 muestra por minuto
const float SUAVIZADO = 0.2;           // peso de la lectura nueva en el promedio exponencial

// ---------------- RANGOS PLAUSIBLES (validación de lecturas) ----------------
const float TEMP_MIN_VALIDA = 0.0;     // rango del DHT11
const float TEMP_MAX_VALIDA = 50.0;
const float HUM_MIN_VALIDA = 5.0;      // el DHT11 no es fiable fuera de ~5-95 %
const float HUM_MAX_VALIDA = 95.0;
const float PRES_MIN_VALIDA = 300.0;   // rango del BMP280
const float PRES_MAX_VALIDA = 1100.0;
// un promedio en los extremos del ADC suele ser un cable desconectado
const float LDR_MIN_CRUDO = 10.0;      // <= 10  -> lectura inválida
const float LDR_MAX_CRUDO = 4085.0;    // >= 4085 -> lectura inválida

// ---------------- TIEMPOS (milisegundos) ----------------
// 4 feeds cada 15 s = 16 datos/minuto, debajo del límite de 30/min de Adafruit IO gratis.
const unsigned long PERIODO_MEDICION = 15000;   // leer sensores y publicar
const unsigned long PERIODO_MUESTRA_PRESION = 60000;  // guardar 1 presión por minuto en la ventana
const unsigned long PERIODO_REINTENTO_WIFI = 15000;   // cada cuánto reintentar WiFi caído (deja terminar cada intento)
const unsigned long PERIODO_REINTENTO_MQTT = 10000;   // cada cuánto reintentar Adafruit IO
const unsigned long PERIODO_PING_MQTT = 60000;        // cada cuánto mandar un "ping" a Adafruit IO (keepalive)
const unsigned long PERIODO_PASO_DEMO = 5000;         // cuánto dura cada escenario del modo demo

// ===================== VARIABLES GLOBALES =====================
// ---------------- ÚLTIMAS LECTURAS ----------------
float temperatura = NAN;   // °C, del DHT11
float humedad = NAN;       // %, del DHT11
float presion = NAN;       // hPa, del BMP280
float tempBMP = NAN;       // °C, del BMP280 (solo para comparar con el DHT11 en el serial)
float luz = NAN;           // %, de la LDR (0 = oscuro, 100 = máxima luz)

// ---------------- TENDENCIA DE PRESIÓN ----------------
float presionSuave = NAN;  // presión filtrada con promedio móvil
bool caidaPresion = false; // true si la presión bajó más de CAIDA_PRESION_HPA en la ventana

// ---------------- MODO DEMOSTRACIÓN ----------------
bool modoDemo = false;     // se activa/desactiva enviando 'd' por el monitor serial

// ---------------- CONTADORES PARA LA PRUEBA DE 30 MINUTOS ----------------
int lecturasInvalidas = 0;
int desconexionesWiFi = 0;
int publicacionesFallidas = 0;

// ---------------- TEMPORIZADORES ----------------
unsigned long ultimaMedicion = 0;
unsigned long ultimaMuestraPresion = 0;

// ===================== SENSORES =====================
DHT dht(PIN_DHT, DHT11);

Adafruit_BMP280 bmp;
bool bmpListo = false;   // true cuando el BMP280 respondió en el bus I2C

void iniciarSensores() {
  dht.begin();

  Wire.begin(PIN_SDA, PIN_SCL);
  bmpListo = iniciarBMP280();
}

bool iniciarBMP280() {
  // 0x60 = BME280, que algunos módulos traen en lugar del BMP280
  if (bmp.begin(0x76, BMP280_CHIPID) || bmp.begin(0x77, BMP280_CHIPID) ||
      bmp.begin(0x76, 0x60) || bmp.begin(0x77, 0x60)) {
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);
    Serial.println("BMP280 encontrado.");
    return true;
  }
  Serial.println("ERROR: no se encontro el BMP280. Revisar cableado I2C (SDA 21, SCL 22, 3V3, GND).");
  return false;
}

void leerSensores() {
  leerDHT11();
  leerBMP280();
  leerLDR();
}

void leerDHT11() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  temperatura = validar(t, TEMP_MIN_VALIDA, TEMP_MAX_VALIDA, "temperatura DHT11");
  humedad = validar(h, HUM_MIN_VALIDA, HUM_MAX_VALIDA, "humedad DHT11");
}

void leerBMP280() {
  if (!bmpListo) {
    bmpListo = iniciarBMP280();
  }
  if (!bmpListo) {
    presion = NAN;
    tempBMP = NAN;
    lecturasInvalidas++;
    return;
  }

  float p = bmp.readPressure() / 100.0;
  tempBMP = bmp.readTemperature();

  presion = validar(p, PRES_MIN_VALIDA, PRES_MAX_VALIDA, "presion BMP280");
}

void leerLDR() {
  long suma = 0;
  for (int i = 0; i < 10; i++) {
    suma += analogRead(PIN_LDR);
    delay(2);
  }
  float promedio = suma / 10.0;

  if (promedio <= LDR_MIN_CRUDO || promedio >= LDR_MAX_CRUDO) {
    lecturasInvalidas++;
    Serial.printf("Lectura invalida de LDR (valor crudo %.0f): revisar cableado\n", promedio);
    luz = NAN;
    return;
  }

  luz = promedio * 100.0 / 4095.0;
}

float validar(float valor, float minimo, float maximo, const char* nombre) {
  if (isnan(valor) || valor < minimo || valor > maximo) {
    lecturasInvalidas++;
    Serial.printf("Lectura invalida de %s: %.2f\n", nombre, valor);
    return NAN;
  }
  return valor;
}

// ===================== PRESIÓN =====================
// +1: para cubrir 30 min hacen falta 31 muestras
const int TAM_VENTANA = VENTANA_MINUTOS + 1;  // casillas del arreglo (31)
float ventanaPresion[TAM_VENTANA];      // muestras de presión suavizada, 1 por minuto
int posicionVentana = 0;                // dónde se escribe la próxima muestra
int muestrasGuardadas = 0;              // cuántas muestras hay (máximo TAM_VENTANA)

void actualizarPresionSuave() {
  if (isnan(presion)) return;           // lectura inválida: no se usa

  if (isnan(presionSuave)) {
    presionSuave = presion;             // primera lectura: se toma tal cual
  } else {
    presionSuave = (1 - SUAVIZADO) * presionSuave + SUAVIZADO * presion;
  }
}

void guardarMuestraPresion() {
  if (isnan(presionSuave)) return;      // todavía no hay lecturas válidas

  // lectura inválida: presionSuave guarda el valor viejo, no se mete a la ventana
  if (isnan(presion)) return;

  ventanaPresion[posicionVentana] = presionSuave;
  posicionVentana = (posicionVentana + 1) % TAM_VENTANA;  // vuelve a 0 al llegar al final
  if (muestrasGuardadas < TAM_VENTANA) {
    muestrasGuardadas++;
  }

  revisarCaidaPresion();
}

void revisarCaidaPresion() {
  if (muestrasGuardadas < TAM_VENTANA) {
    caidaPresion = false;               // ventana incompleta: aún no se puede opinar
    return;
  }
  float masAntigua = ventanaPresion[posicionVentana];
  float descenso = masAntigua - presionSuave;
  caidaPresion = (descenso >= CAIDA_PRESION_HPA);
}

int muestrasEnVentana() {
  return muestrasGuardadas;
}

// ===================== LED RGB =====================
// Prioridad: ROJO (calor o caída de presión) > AZUL (frío) > VERDE. Sin temperatura válida: apagado.
// El modo demo solo simula el LED; a Adafruit IO siempre van las mediciones reales.

bool calorActivo = false;          // true mientras el ROJO por calor está encendido
bool frioActivo = false;           // true mientras el AZUL por frío está encendido
int pasoDemo = 0;                  // escenario actual del modo demo (0 a 3)
unsigned long ultimoPasoDemo = 0;

void iniciarLED() {
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AZUL, OUTPUT);
  ponerColor(false, false, false);
}

void actualizarLED() {
  if (modoDemo) {
    mostrarDemo();
  } else {
    elegirColor(temperatura, caidaPresion);
  }
}

void elegirColor(float temp, bool caida) {
  if (!isnan(temp)) {
    if (calorActivo) {
      if (temp < TEMP_ALTA - HISTERESIS_C) calorActivo = false;
    } else {
      if (temp > TEMP_ALTA) calorActivo = true;
    }
    if (frioActivo) {
      if (temp > TEMP_BAJA + HISTERESIS_C) frioActivo = false;
    } else {
      if (temp < TEMP_BAJA) frioActivo = true;
    }
  }

  if (caida || calorActivo) {
    ponerColor(true, false, false);
  } else if (frioActivo) {
    ponerColor(false, false, true);
  } else if (!isnan(temp)) {
    ponerColor(false, true, false);
  } else {
    ponerColor(false, false, false);   // temp es NAN: sin dato, apagado
  }
}

void ponerColor(bool rojo, bool verde, bool azul) {
  digitalWrite(PIN_LED_ROJO,  LED_ANODO_COMUN ? !rojo  : rojo);
  digitalWrite(PIN_LED_VERDE, LED_ANODO_COMUN ? !verde : verde);
  digitalWrite(PIN_LED_AZUL,  LED_ANODO_COMUN ? !azul  : azul);
}

// ---------------- MODO DEMOSTRACIÓN ----------------
void revisarComandoSerial() {
  if (!Serial.available()) return;

  char comando = Serial.read();
  if (comando == 'd' || comando == 'D') {
    modoDemo = !modoDemo;
    pasoDemo = 0;
    ultimoPasoDemo = millis();
    if (modoDemo) {
      Serial.println(">> MODO DEMO ACTIVADO (solo afecta al LED; se siguen publicando datos reales)");
      anunciarPasoDemo();
    } else {
      Serial.println(">> MODO DEMO DESACTIVADO");
    }
  }
  if (comando == 'w' || comando == 'W') {
    // revisarWiFi() detecta la caída y reconecta
    Serial.println(">> PRUEBA: simulando caida de WiFi (WiFi.disconnect)");
    WiFi.disconnect();
  }
}

void mostrarDemo() {
  if (millis() - ultimoPasoDemo >= PERIODO_PASO_DEMO) {
    ultimoPasoDemo = millis();
    pasoDemo = (pasoDemo + 1) % 4;
    anunciarPasoDemo();
  }

  float tempFria = TEMP_BAJA - 5;
  float tempNormal = (TEMP_BAJA + TEMP_ALTA) / 2;
  float tempCaliente = TEMP_ALTA + 5;

  if (pasoDemo == 0) elegirColor(tempFria, false);
  if (pasoDemo == 1) elegirColor(tempNormal, false);
  if (pasoDemo == 2) elegirColor(tempCaliente, false);
  if (pasoDemo == 3) elegirColor(tempFria, true);       // ROJO: la caída de presión gana al azul
}

void anunciarPasoDemo() {
  if (pasoDemo == 0) Serial.println("DEMO 1/4: temperatura baja -> AZUL");
  if (pasoDemo == 1) Serial.println("DEMO 2/4: temperatura de referencia -> VERDE");
  if (pasoDemo == 2) Serial.println("DEMO 3/4: temperatura alta -> ROJO");
  if (pasoDemo == 3) Serial.println("DEMO 4/4: temperatura baja + caida de presion -> ROJO (prioridad)");
}

// ===================== CONEXIÓN =====================
WiFiClient clienteWiFi;

Adafruit_MQTT_Client mqtt(&clienteWiFi, AIO_SERVIDOR, AIO_PUERTO, AIO_USUARIO, AIO_KEY);

// QoS 0: publish() true = escrito en el socket, no confirmado
Adafruit_MQTT_Publish feedTemperatura(&mqtt, AIO_USUARIO "/feeds/temperatura");
Adafruit_MQTT_Publish feedHumedad(&mqtt, AIO_USUARIO "/feeds/humedad");
Adafruit_MQTT_Publish feedPresion(&mqtt, AIO_USUARIO "/feeds/presion");
Adafruit_MQTT_Publish feedLuz(&mqtt, AIO_USUARIO "/feeds/luz");

bool wifiConectado = false;          // estado en la vuelta anterior, para detectar cambios
unsigned long ultimoIntentoWiFi = 0;
unsigned long ultimoIntentoMQTT = 0;
unsigned long ultimoPingMQTT = 0;

// ---------------- WIFI ----------------
void iniciarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.begin(WIFI_SSID, WIFI_CLAVE);

  Serial.print("Conectando a WiFi");
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  wifiConectado = (WiFi.status() == WL_CONNECTED);
  if (wifiConectado) {
    Serial.print("WiFi conectado. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Sin WiFi: se sigue midiendo en modo local y se reintentara.");
  }
}

void revisarWiFi() {
  bool conectadoAhora = (WiFi.status() == WL_CONNECTED);

  if (wifiConectado && !conectadoAhora) {
    desconexionesWiFi++;
    imprimirHora();
    Serial.printf("WiFi PERDIDO (caidas: %d). El LED sigue funcionando.\n", desconexionesWiFi);
    mqtt.disconnect();   // cierra la sesión vieja para abrir una nueva al volver el WiFi
  }
  if (!wifiConectado && conectadoAhora) {
    imprimirHora();
    Serial.println("WiFi RECUPERADO. Se reanudan los envios.");
  }
  wifiConectado = conectadoAhora;

  if (!conectadoAhora && millis() - ultimoIntentoWiFi >= PERIODO_REINTENTO_WIFI) {
    ultimoIntentoWiFi = millis();
    // reconnect() puede quedar en "sta is connecting"; se reinicia el intento desde cero
    Serial.printf("Reintentando WiFi (estado %d)...\n", WiFi.status());
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_CLAVE);
  }
}

// ---------------- MQTT ----------------
void revisarMQTT() {
  if (!wifiConectado || mqtt.connected()) return;
  if (millis() - ultimoIntentoMQTT < PERIODO_REINTENTO_MQTT) return;
  ultimoIntentoMQTT = millis();

  Serial.print("Conectando a Adafruit IO... ");
  int8_t resultado = mqtt.connect();
  if (resultado == 0) {
    Serial.println("conectado.");
  } else {
    Serial.println(mqtt.connectErrorString(resultado));
    mqtt.disconnect();
  }
}

void revisarKeepalive() {
  if (!mqtt.connected()) return;
  if (millis() - ultimoPingMQTT < PERIODO_PING_MQTT) return;
  ultimoPingMQTT = millis();

  if (!mqtt.ping()) {
    imprimirHora();
    Serial.println("Ping MQTT sin respuesta: se cierra la sesion para reconectar.");
    mqtt.disconnect();   // revisarMQTT() abrirá una sesión nueva
  }
}

void publicarLecturas() {
  if (!mqtt.connected()) {
    Serial.println("        (sin conexion a Adafruit IO: esta lectura no se publica)");
    return;
  }
  publicarValor(feedTemperatura, temperatura, "temperatura");
  publicarValor(feedHumedad, humedad, "humedad");
  publicarValor(feedPresion, presion, "presion");
  publicarValor(feedLuz, luz, "luz");
}

void publicarValor(Adafruit_MQTT_Publish &feed, float valor, const char* nombre) {
  if (isnan(valor)) return;   // lectura inválida: no se manda basura al dashboard

  if (!feed.publish(valor)) {
    publicacionesFallidas++;
    Serial.printf("Fallo al publicar %s\n", nombre);
  }
}

void imprimirHora() {
  unsigned long segundos = millis() / 1000;
  Serial.printf("[%02lu:%02lu] ", segundos / 60, segundos % 60);
}

// ===================== SALIDA SERIAL =====================
void imprimirLecturas() {
  unsigned long segundos = millis() / 1000;
  Serial.printf("[%02lu:%02lu] T: %.1f C | HR: %.0f %% | P: %.1f hPa (suave %.2f) | T_BMP: %.1f C | Luz: %.0f %%\n",
                segundos / 60, segundos % 60,
                temperatura, humedad, presion, presionSuave, tempBMP, luz);
  Serial.printf("        caida presion: %s | ventana: %d/%d muestras | invalidas: %d | caidas WiFi: %d | envios fallidos: %d\n",
                caidaPresion ? "SI" : "no", muestrasEnVentana(), TAM_VENTANA,
                lecturasInvalidas, desconexionesWiFi, publicacionesFallidas);
}

// ===================== SETUP Y LOOP =====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== MeteoTech IoT ===");

  iniciarLED();
  iniciarSensores();
  iniciarWiFi();

  Serial.println("Envia 'd' por el monitor serial para activar/desactivar el modo demo.");
  Serial.println("Envia 'w' para simular una caida de WiFi (prueba de reconexion).");
}

void loop() {
  revisarComandoSerial();   // 'd' = modo demo, 'w' = simular caída de WiFi
  revisarWiFi();            // reconecta si se cayó (sin bloquear)
  revisarMQTT();            // conecta a Adafruit IO si hay WiFi
  revisarKeepalive();       // ping cada 60 s para mantener viva la sesión MQTT

  if (millis() - ultimaMedicion >= PERIODO_MEDICION) {
    ultimaMedicion = millis();
    leerSensores();
    actualizarPresionSuave();
    imprimirLecturas();
    publicarLecturas();
  }

  if (millis() - ultimaMuestraPresion >= PERIODO_MUESTRA_PRESION) {
    ultimaMuestraPresion = millis();
    guardarMuestraPresion();
  }

  actualizarLED();          // funciona siempre, haya o no Internet
}
