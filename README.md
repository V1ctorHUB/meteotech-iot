# MeteoTech IoT

MeteoTech es una mini estación del clima hecha con una placa ESP32. Mide la temperatura, la humedad, la presión del aire y la cantidad de luz. Cada 15 segundos manda esos datos a internet (a Adafruit IO), donde se ven en gráficas desde la computadora o el teléfono. Además tiene un LED de colores que avisa en el momento cómo está el ambiente, aunque no haya internet.

Es un proyecto de clase: sirve para aprender y para ver tendencias, no para dar pronósticos oficiales.

## Cómo funciona, en pocas palabras

1. Cada 15 segundos la placa lee los tres sensores.
2. Revisa que los valores tengan sentido. Si un sensor da algo imposible (por ejemplo, porque un cable se soltó), esa lectura se descarta en vez de mandarla.
3. Muestra las lecturas en el monitor serial de Arduino.
4. Las envía a Adafruit IO, donde se guardan y se grafican.
5. Cambia el color del LED según la temperatura y la presión.

Si se cae el WiFi, la estación sigue midiendo y el LED sigue funcionando. Cada cierto tiempo intenta volver a conectarse sola y, cuando lo logra, sigue enviando datos normalmente.

## Qué significa cada color del LED

| Color | Significado |
|---|---|
| Rojo | Hace calor (más de 30 °C) o la presión del aire viene bajando. El rojo tiene prioridad sobre los demás. |
| Azul | Hace frío (menos de 20 °C). |
| Verde | La temperatura está en un rango normal (entre 20 y 30 °C). |
| Apagado | No hay una lectura de temperatura válida. |

Sobre la presión: cuando la presión del aire baja de forma constante, suele ser señal de que el tiempo va a cambiar. La placa guarda un dato de presión por minuto y compara el valor actual con el de hace 30 minutos. Si bajó 1 hPa o más, enciende el rojo. Por eso, después de prender la placa hay que esperar unos 30 minutos para que esta alerta pueda funcionar.

Para que el LED no esté cambiando de color a cada rato cuando la temperatura está justo en el límite (por ejemplo, 29.9 y 30.1 °C), se deja un margen de medio grado antes de cambiar.

## Materiales

- Placa ESP32 DevKit
- Sensor DHT11 (temperatura y humedad)
- Sensor BMP280 (presión del aire)
- Fotorresistencia o LDR (luz) con una resistencia de 10 kΩ
- LED RGB con tres resistencias de 220 Ω
- Protoboard, cables y cable USB

## Qué hace cada sensor

**DHT11.** Mide la temperatura y la humedad del aire. Es sencillo y barato. Puede fallar por unos 2 °C y un 5 % de humedad, lo cual está bien para un proyecto de clase.

**BMP280.** Mide la presión del aire. También trae su propio termómetro, pero como queda cerca de la placa (que se calienta un poco), marca algo más alto que el DHT11. Por eso la temperatura "oficial" del proyecto es la del DHT11.

**LDR (fotorresistencia).** Es una pieza que cambia según cuánta luz le llega. Junto con la resistencia de 10 kΩ forma un circuito que la placa puede leer. El resultado se muestra como un porcentaje de luz: 0 % es oscuridad total y 100 % es mucha luz. No mide luz en unidades oficiales, solo sirve para comparar "más luz" o "menos luz".

**LED RGB.** Son tres luces (roja, verde y azul) dentro de una sola pieza. La placa enciende la que corresponda según la situación.

## Conexiones

Todo se alimenta con 3.3 V de la placa y todas las tierras (GND) van juntas.

| Componente | Conexión | Pin del ESP32 |
|---|---|---|
| DHT11 | Pin de datos | GPIO 4 |
| LDR | Punto medio entre la LDR y la resistencia de 10 kΩ | GPIO 32 |
| BMP280 | SDA | GPIO 21 |
| BMP280 | SCL | GPIO 22 |
| LED RGB | Rojo (con su resistencia de 220 Ω) | GPIO 19 |
| LED RGB | Verde (con su resistencia de 220 Ω) | GPIO 18 |
| LED RGB | Azul (con su resistencia de 220 Ω) | GPIO 17 |
| LED RGB | Pata larga | GND |

Cosas a tener en cuenta:

- La LDR va en el GPIO 32 porque algunos pines del ESP32 dejan de leer bien cuando el WiFi está encendido, y este no tiene ese problema.
- Ningún pin del ESP32 debe recibir 5 V, solo 3.3 V.
- Si tu LED tiene la pata larga conectada a 3.3 V en lugar de GND (se llama "ánodo común"), cambia `LED_ANODO_COMUN` a `true` en el código.
- Si el porcentaje de luz sale al revés (más luz da un número más bajo), intercambia de lugar la LDR y la resistencia.

## Librerías que se usan

Se instalan desde Arduino IDE, en *Herramientas > Gestionar librerías*:

| Librería | Para qué sirve |
|---|---|
| DHT sensor library (Adafruit) | Leer el DHT11. Al instalarla pide también *Adafruit Unified Sensor*; hay que aceptar. |
| Adafruit BMP280 Library | Leer el BMP280. Pide también *Adafruit BusIO*; hay que aceptar. |
| Adafruit MQTT Library | Enviar los datos a Adafruit IO. |

Estas dos ya vienen con la placa ESP32 y no hay que instalarlas:

| Librería | Para qué sirve |
|---|---|
| WiFi.h | Conectarse a la red WiFi. |
| Wire.h | Comunicarse con el BMP280 por los pines 21 y 22. |

## Comandos de prueba

Desde el monitor serial se pueden mandar dos letras para probar la estación sin tener que esperar a que cambie el clima:

- **d**: modo demostración. El LED pasa por azul, verde y rojo, y al final muestra el caso de frío con la presión bajando, donde gana el rojo. Cada paso dura 5 segundos. Los datos que se envían a internet siguen siendo los reales, la demostración solo cambia el LED. Mandar otra **d** lo detiene.
- **w**: simula que se cae el WiFi. Sirve para comprobar que la placa se reconecta sola sin tener que apagar el router.

## Ajustes que se pueden cambiar

Todo lo ajustable está al principio de `MeteoTech.ino`, en la sección de configuración:

| Ajuste | Valor actual | Qué controla |
|---|---|---|
| `TEMP_BAJA` | 20 °C | Por debajo de esto el LED se pone azul. |
| `TEMP_ALTA` | 30 °C | Por encima de esto el LED se pone rojo. |
| `HISTERESIS_C` | 0.5 °C | El margen para que el LED no parpadee cerca de un límite. |
| `CAIDA_PRESION_HPA` | 1 hPa | Cuánto tiene que bajar la presión para encender el rojo. |
| `VENTANA_MINUTOS` | 30 min | Contra cuánto tiempo atrás se compara la presión. |
| `PERIODO_MEDICION` | 15 s | Cada cuánto se mide y se envía. |
| `LED_ANODO_COMUN` | false | Cambiar a `true` si el LED es de ánodo común. |

## Cómo está organizado el código

Todo el programa está en un solo archivo, `MeteoTech/MeteoTech.ino`, dividido en secciones:

- **Configuración:** pines, límites de temperatura y tiempos. Es lo único que normalmente hace falta tocar.
- **Sensores:** lee el DHT11, el BMP280 y la LDR, y descarta las lecturas que no tienen sentido.
- **Presión:** suaviza las lecturas de presión para que no salten y guarda una por minuto para detectar si viene bajando.
- **LED:** decide qué color mostrar y tiene el modo demostración.
- **Conexión:** se conecta al WiFi y a Adafruit IO, se reconecta si se cae y envía los datos.
- **Setup y loop:** el arranque de la placa y el ciclo que se repite todo el tiempo.

El archivo `credenciales.h` tiene los datos privados y no se sube al repositorio. En su lugar está `credenciales.example.h`, que es la misma plantilla pero vacía.

## Pruebas que se hicieron

**Prueba en exterior** (19 de noviembre de 2026, Plaza Merliot). Se compararon las lecturas con los datos de Open-Meteo, un servicio público del clima:

| | Temperatura | Humedad | Presión |
|---|---|---|---|
| MeteoTech | 21.8 °C | 77 % | 910.0 hPa |
| Open-Meteo | 19.4 °C | 86 % | 918.2 hPa |

Las diferencias tienen explicación. La presión de Open-Meteo está calculada para un punto unos 70 metros más bajo, y eso equivale a unos 8 hPa. La humedad baja un poco porque el sensor estaba en un aire algo más caliente. La temperatura queda dentro del margen de error del DHT11.

**Prueba de funcionamiento continuo.** La estación estuvo encendida más de una hora sin reiniciarse, sin lecturas malas y sin envíos fallidos. Los registros están en la carpeta `pruebas/`.

**Prueba de desconexión.** Se cortó el WiFi cuatro veces con el comando `w`. En todas, la placa se reconectó en uno o dos segundos y siguió enviando datos normalmente.

## Limitaciones

- Los sensores son de nivel educativo y no están calibrados de forma profesional.
- La luz se mide como un porcentaje para comparar, no en unidades oficiales.
- Si no hay internet, las lecturas de ese momento no se guardan para mandarlas después.
- Al reiniciar la placa se pierde el historial de presión, y hay que esperar otros 30 minutos para que vuelva a funcionar la alerta por presión.
