# POWER LIGHT V1

Monitor inteligente para inversores/UPS de 12 V "antiguos" (sin conectividad). Se instala junto al inversor y a la batería, mide todo lo relevante y lo publica en la red.

Corre en un **Seeed XIAO ESP32-C6** con firmware Arduino (PlatformIO).

## Qué hace

| Función | Cómo |
|---|---|
| Mide la salida AC del inversor (V, A, W, kWh, Hz, PF) | PZEM-004T v3.0 por UART/Modbus |
| Mide voltaje de batería | Divisor resistivo 40.2k/10k → ADC |
| Mide corriente de batería (carga / descarga, con signo) | Pinza Hall **QNHCK1-21** → ADC |
| Detecta corte y retorno de la red eléctrica ("Línea 1") | Optoacoplador H11AA1 + interrupción |
| Muestra el estado en sitio | LCD 16x2 I2C + LED RGB (WS2812) + buzzer |
| Avisa cuando pasa algo | Telegram, MQTT (`/alert`) y buzzer |
| Se integra en Home Assistant sin configuración manual | MQTT Discovery (formato *device*) |
| Se calibra y configura en caliente | Consola de comandos por Serial, MQTT y Telegram; persistencia en NVS |
| Mantiene la hora sin red | RTC DS1307 (para la alerta horaria de retorno) |

## Documentación

| Documento | Contenido |
|---|---|
| [docs/hardware.md](docs/hardware.md) | Lista de materiales, pinout, cableado, divisores, el sensor QNHCK1-21 y notas del ADC del ESP32-C6 |
| [docs/firmware.md](docs/firmware.md) | Arquitectura del código, ciclo del `loop()`, temporizaciones, máquinas de estado, LCD/LED/buzzer, NVS, limitaciones conocidas |
| [docs/comandos.md](docs/comandos.md) | Referencia completa de comandos (Serial, MQTT, Telegram) |
| [docs/calibracion.md](docs/calibracion.md) | Procedimiento paso a paso para calibrar batería, sensor Hall, dirección, deadband, RTC y hora de alerta |
| [docs/integraciones.md](docs/integraciones.md) | WiFi, MQTT (tópicos y payloads), Home Assistant Discovery, bot de Telegram, seguridad |
| [home_assistant/README.md](home_assistant/README.md) | Validación rápida del discovery desde la terminal del broker |

## Inicio rápido

1. **Clonar y abrir** el proyecto en VS Code con la extensión PlatformIO.
2. **Credenciales**: copiar `lib/AppBridge/src/AppBridgeSecrets.example.h` a `lib/AppBridge/src/AppBridgeSecrets.h` y rellenar WiFi, MQTT y Telegram. El archivo real está en `.gitignore`; nunca lo subas.
3. **Compilar y flashear**:
   ```
   pio run -t upload
   pio device monitor        # 115200 baud, fin de línea LF
   ```
4. **Calibrar** (ver [docs/calibracion.md](docs/calibracion.md)):
   ```
   HALL ZERO          ← sin corriente por la pinza
   BAT CAL 12.65      ← voltaje real medido con multímetro
   CURR DIR -1        ← solo si carga/descarga salen invertidas
   ```
5. **Verificar** en Home Assistant que aparece el dispositivo *POWER LIGHT V1* con sus 8 entidades, y en Telegram enviar `/estado`.

## Estructura del repositorio

```
POWER_LIGTH_V1.0/
├── platformio.ini                 Entorno seeed_xiao_esp32_c6, dependencias
├── src/main.cpp                   Aplicación: setup/loop, alertas, LCD, consola, Telegram
├── lib/
│   ├── AppBridge/                 WiFi + MQTT + Home Assistant Discovery
│   │   └── src/AppBridgeSecrets.example.h   Plantilla de credenciales
│   ├── BatteryMonitor/            Lectura y filtrado del voltaje de batería, etapa de carga
│   ├── CurrentHallMonitor/        Lectura, filtrado y calibración del sensor Hall
│   └── LedStatus/                 Colores/animaciones del LED según etapa de carga
├── docs/                          Esta documentación
└── home_assistant/README.md       Comandos mosquitto para validar el discovery
```

## Dependencias

Se resuelven automáticamente desde `platformio.ini`:

- Plataforma: fork de Seeed `platform-seeedboards` (Arduino-ESP32 3.3.x)
- `adafruit/Adafruit NeoPixel`, `adafruit/RTClib`, `mandulaj/PZEM-004T-v30`, `marcoschwartz/LiquidCrystal_I2C`, `knolleary/PubSubClient`, `bblanchon/ArduinoJson`

## Estado del proyecto

Versión 1.0 funcional en banco de pruebas. Las mejoras pendientes más importantes (Telegram y buzzer bloqueantes, SOC por integración de corriente, despachador único de comandos) están descritas en [docs/firmware.md → Limitaciones conocidas](docs/firmware.md#limitaciones-conocidas-y-mejoras-propuestas).
