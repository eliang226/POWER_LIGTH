# Firmware

## Estructura del código

| Archivo | Responsabilidad |
|---|---|
| `src/main.cpp` | Aplicación: constantes y pines, `setup()`/`loop()`, detección de Línea 1, alertas (buzzer, Telegram, MQTT), pantallas LCD, consola de comandos, cliente Telegram, persistencia NVS. |
| `lib/AppBridge/` | WiFi (STA, reconexión), MQTT (PubSubClient, LWT), Home Assistant Discovery, recepción de comandos por MQTT. |
| `lib/BatteryMonitor/` | Muestreo ADC del divisor de batería, calibración scale/offset, filtro EMA, alarma de batería baja con histéresis, clasificación de etapa de carga. |
| `lib/CurrentHallMonitor/` | Muestreo ADC del sensor Hall, conversión a amperios, calibración de cero y ganancia, filtro EMA. |
| `lib/LedStatus/` | Colores y animaciones del WS2812 según etapa de carga. |

Todo el estado de la aplicación vive en variables globales `g*` de `main.cpp`; las librerías encapsulan el suyo. No se usan `String` de Arduino en las rutas calientes (buffers `char` fijos con `snprintf`) para evitar fragmentación del heap.

## Arranque (`setup()`)

1. LCD: "POWER LIGHT V1 / Iniciando..." (1 s).
2. Serial a 115200. `telegramClient.setInsecure()`.
3. Buzzer en LOW. RTC: si el DS1307 no responde, se sigue sin él; si está parado, se ajusta con `__DATE__ __TIME__`.
4. `loadRuntimeConfig()`: lee calibraciones y ajustes de NVS y los imprime.
5. `AppBridge::begin()`: si no hay SSID en `AppBridgeSecrets.h` espera 20 s por Serial la línea `WIFI:ssid,password` (no se persiste). Configura MQTT (buffer 3072 B). WiFi se conecta de forma no bloqueante en el `loop()`.
6. UART1 para PZEM, pin de Línea 1 con pull-up e interrupción.
7. `batteryMonitor.begin()` / `hallCurrentMonitor.begin()` (resolución 12 bits, atenuación 11 dB).
8. Secuencia de LED (rojo, verde, azul, 2 destellos blancos; ~3.3 s bloqueantes) y pantallas de boot.
9. Estado inicial de Línea 1 por lectura directa del pin.

## Ciclo principal (`loop()`)

Super-loop cooperativo; cada tarea decide con `millis()` si le toca ejecutarse.

| Orden | Tarea | Intervalo | Constante | Bloquea |
|---|---|---|---|---|
| 1 | `appBridge.update` – WiFi/MQTT | reintento WiFi 7 s, MQTT 5 s | `kWifiRetryMs`, `kMqttRetryMs` | Solo `connect()` MQTT: hasta ~3 s por intento si el broker no responde |
| 2 | `pollTelegramCommands` | 1.5 s | `kTelegramPollIntervalMs` | **Sí**: HTTPS completo, hasta 3.5 s |
| 3 | `maybeSendStartupTelegram` | reintento 30 s hasta lograrlo | `kTelegramStartupRetryMs` | Sí, al enviar |
| 4 | `handleSerialCommands` | continuo | | No |
| 5 | `batteryMonitor.update` | 250 ms, 16 muestras | `kBatteryReadIntervalMs` | ~4 ms |
| 6 | `hallCurrentMonitor.update` | 100 ms, 30 muestras | `kReadIntervalMs` | ~8 ms |
| 7 | `updatePzem` | 2.5 s | `kPzemReadIntervalMs` | ≤ 100 ms si el PZEM no responde |
| 8 | `updatePzemHealthAlert` | continuo | `kPzemDataTimeoutMs` 10 s | Sí, si dispara Telegram |
| 9 | `updateLine1Ac` | ventana 250 ms | `kLine1AcEvalWindowMs` | **Sí** en transición: buzzer 3.1 s + Telegram |
| 10 | `updateLowBatteryAlarm` | continuo | | Sí, si dispara Telegram |
| 11 | `appBridge.publishTelemetry` | 5 s | `kTelemetryPublishMs` | No |
| 12 | `updateLcdDashboard` | rota cada 5 s | `kLcdRotateIntervalMs` | ~15 ms al redibujar |
| 13 | `ledStatus.update` | continuo | | No |
| 14 | `printTelemetryJson` | 5 s (dump por Serial, no es JSON) | `kTelemetryPrintIntervalMs` | No |

## Medidas

### Batería (`BatteryMonitor`)

```
V_adc  = analogReadMilliVolts() promedio de 16 muestras
V_sens = V_adc × (40.2k + 10k) / 10k
V_bat  = V_sens × scale + offset          (scale 1.05, offset 0 por defecto)
V_filt = EMA(V_bat, α = 0.20)
```

- `status`: `BELOW_RANGE` (< 5 V), `IN_RANGE`, `OVER_RANGE` (> 16.5 V).
- Alarma de batería baja (para el LED): se activa a ≤ 11.4 V tras 6 lecturas consecutivas (1.5 s), se limpia a ≥ 11.7 V.
- **Etapa de carga** (`chargeStage()`), por voltaje filtrado:

| Etapa | Condición |
|---|---|
| `EQUALIZE` | ≥ 15.0 V |
| `BULK_OR_ABSORPTION` | ≥ 14.2 V |
| `FLOAT` | ≥ 13.2 V |
| `REST_FULL` | ≥ 12.7 V |
| `DISCHARGING` | < 12.7 V |
| `LOW_BATTERY` | alarma de batería baja activa |
| `UNKNOWN` | fuera de rango 5–16.5 V |

- **Capacidad (%)** (`estimateBatteryCapacityPercent` en `main.cpp`): lineal entre 10.5 V (0 %) y 13.5 V (100 %). Es una aproximación de plomo-ácido en reposo; mientras carga o bajo carga fuerte no es representativa. En LCD y `/estado` se limita a 99 %.

### Corriente de batería (`CurrentHallMonitor`)

```
V_adc    = analogReadMilliVolts() promedio de 30 muestras
V_sensor = V_adc / (10k / (3.9k + 10k))            = V_adc / 0.7194
V_zero   = 2.5 V + offset calibrado (HALL ZERO)
I_inst   = (V_sensor − V_zero) / 0.040 × gain      (gain 1.0 por defecto)
I_filt   = EMA(I_inst, α = 0.18)
```

Luego en `main.cpp`: `I_bat = I_filt × dir` (`dir` = ±1) y se aplica el *deadband* (|I| < 0.10 A ⇒ 0). Signo positivo = **carga**, negativo = **descarga**; `IDLE` dentro del deadband.

### PZEM

Cada 2.5 s se leen V, A, W, kWh, Hz y PF. La lectura es válida solo si los seis valores son numéricos. `isPzemDataFresh()` exige además que la última lectura válida tenga menos de 10 s.

## Detección de Línea 1

- La ISR en A0 cuenta flancos (`gLine1EdgeCountIsr`).
- Cada 250 ms: `hayAC = flancos ≥ 6 || pin == LOW || muestrasLow ≥ 3`. Se reinician los contadores.
- Transición a **sin AC**: alerta `LINE1_LOST` por MQTT, Telegram *"ALERTA: sin energia AC en LINEA 1."*, buzzer 4 pulsos, pantalla de alerta 7 s.
- Transición a **con AC**: `LINE1_RESTORED`, Telegram *"INFO: energia AC restablecida..."*, buzzer 2 pulsos **solo si** la hora del RTC coincide con `ALERT HOUR` (o está en `ANY`), pantalla de alerta 7 s.

## Alertas y eventos

| Evento | Condición | MQTT `/alert` | Telegram | Buzzer | LCD |
|---|---|---|---|---|---|
| Corte de red | Línea 1 pasa a sin AC | `LINE1_LOST` | Sí | 4 × (400 ms on / 500 ms off) | "ALERTA LINEA 1 / SIN ELECTRICIDAD" 7 s |
| Retorno de red | Línea 1 vuelve | `LINE1_RESTORED` | Sí | 2 × (400/500), filtrado por hora | "LINEA 1 ACTIVA / ENERGIA VOLVIO" 7 s |
| Batería crítica | V_filt ≤ 10.5 V | `BAT_LOW_CRITICAL` | Sí + recordatorio cada 15 min | Continuo 80 ms on / 80 ms off mientras dure | — |
| Batería recuperada | V_filt > 10.5 V tras crítica | `BAT_LOW_RECOVERED` | Sí | Se apaga | — |
| PZEM sin datos | > 10 s sin lectura válida (tras 15 s de gracia al arrancar) | `PZEM_DATA_LOST` | Sí | — | LCD muestra `V:---` |
| PZEM recuperado | Vuelven datos válidos | `PZEM_DATA_RESTORED` | Sí | — | — |
| Arranque | Primera conexión WiFi | — | "POWER LIGHT V1 conectado." + estado | — | — |

Umbrales: `kCriticalLowBatteryV` (10.5 V), `kTelegramLowBatteryRepeatMs` (15 min), `kPzemDataTimeoutMs`, `kPzemFaultStartupGraceMs`.

## Pantalla LCD

Dos pantallas rotan cada 5 s. Cualquier alerta de Línea 1 las interrumpe 7 s.

```
Pantalla 1 (AC)             Pantalla 2 (batería)
┌────────────────┐          ┌────────────────┐
│AC:LINE V:121V  │          │Vbat:12.6V C:85%│
│A:2.35 W: 280   │          │IBAT:-3.2A DIS  │
└────────────────┘          └────────────────┘
Sin PZEM:  AC:OUT V:---     CHG / DIS / IDLE
           A:--.-- W:----
```

## LED de estado

| Etapa de carga | Color / animación |
|---|---|
| `LOW_BATTERY` | Rojo parpadeando (150 ms) |
| `BULK_OR_ABSORPTION` | Naranja, doble pulso cada 900 ms |
| `FLOAT` | Verde respirando (fade 0→170) |
| `EQUALIZE` | Magenta fijo |
| `REST_FULL` | Celeste fijo (0,180,255) |
| `DISCHARGING` | Azul fijo |
| `UNKNOWN` + `OVER_RANGE` | Magenta fijo |
| `UNKNOWN` (otro) | Azul tenue |

Arranque: rojo → verde → azul → apagado → 2 destellos blancos.

## Buzzer

| Patrón | Uso | Implementación |
|---|---|---|
| 4 × 400 ms on / 500 ms off | Corte de red | `playBuzzerPattern()` con `delay()` (**bloquea 3.1 s**) |
| 2 × 400 ms on / 500 ms off | Retorno de red (según `ALERT HOUR`) | Idem (**bloquea 1.3 s**) |
| 80 ms on / 80 ms off continuo | Batería crítica | No bloqueante, en `updateLowBatteryAlarm()` |

## Persistencia (NVS)

Namespace `pl_config` (`Preferences`). Se cargan al arrancar y se validan por rango; un valor fuera de rango se ignora y se usa el defecto.

| Clave | Tipo | Contenido | Comando que la escribe |
|---|---|---|---|
| `bat_scale` | float | Factor de batería (0.5–1.5) | `BAT CAL`, `BAT SCALE` |
| `bat_offs` | float | Offset de batería en V (−2…2) | `BAT OFFSET` |
| `hall_zero` | float | V del sensor a 0 A (0–5) | `HALL ZERO` / `CURR ZERO` |
| `hall_gain` | float | Ganancia (0.05–10) | `CURR CAL` / `HALL GAIN` |
| `hall_dir` | float | +1 / −1 | `CURR DIR` |
| `hall_dead` | float | Deadband en A (0–5) | `CURR DEAD` |
| `rest_hour` | uint8 | Hora de alerta de retorno (0–23) o 255 = siempre | `ALERT HOUR` |

## Consola de comandos

Un único parser (`processConsoleCommand`) recibe texto desde tres canales y lo normaliza (trim + mayúsculas):

- **Serial** (115200, terminado en `\n`, máx. 95 caracteres).
- **MQTT**: payload de `<base>/cmd` (máx. 159 caracteres). La salida va a Serial, no de vuelta a MQTT.
- **Telegram**: solo los grupos `BAT`, `CURR`, `HALL`, `RTC`, `ALERT` (directo o con `/cmd`); tras ejecutar responde con un resumen del grupo. Además tiene comandos propios (`/estado`, `/bat`, …).

Referencia completa en [comandos.md](comandos.md).

## Cliente Telegram

- Bot API por HTTPS (`WiFiClientSecure` con `setInsecure()`, sin validar certificado).
- Sondeo: `getUpdates?timeout=0&limit=5&offset=<último+1>` cada 1.5 s. Solo se atienden mensajes cuyo `chat.id` coincide con `APP_TELEGRAM_CHAT_ID`.
- Envío: `sendMessage` por POST `x-www-form-urlencoded`, texto URL-encoded a mano (`urlEncode`), máx. ~760 caracteres codificados.
- Cada petición abre y cierra una conexión TLS nueva (ver limitaciones).

## Constantes principales (`src/main.cpp`)

| Constante | Valor | Significado |
|---|---|---|
| `kPzemReadIntervalMs` / `kPzemDataTimeoutMs` | 2500 / 10000 | Lectura y caducidad PZEM |
| `kLcdRotateIntervalMs` / `kLine1AlertHoldMs` | 5000 / 7000 | Rotación LCD / duración pantalla de alerta |
| `kLine1AcEvalWindowMs` / `kLine1AcMinEdges` | 250 / 6 | Ventana y umbral de flancos de Línea 1 |
| `kCriticalLowBatteryV` | 10.5 | Batería crítica |
| `kDefaultHallDeadbandA` / `kMaxHallDeadbandA` | 0.10 / 5.0 | Deadband por defecto y máximo |
| `kMinHallCalibrationCurrentA` | 1.0 | Carga mínima aceptada para `CURR CAL` |
| `kTelegramPollIntervalMs` / `kTelegramHttpTimeoutMs` | 1500 / 3500 | Sondeo y timeout HTTP |
| `kTelegramLowBatteryRepeatMs` | 900000 | Recordatorio de batería crítica (15 min) |
| `kSerialCmdBufferSize` | 96 | Longitud máxima de comando |

## Limitaciones conocidas y mejoras propuestas

Ordenadas por impacto. Ninguna impide el funcionamiento actual.

1. **Telegram bloquea el núcleo.** El ESP32-C6 es mono-núcleo y cada sondeo (1.5 s) abre una conexión TLS nueva: cientos de ms bloqueados por ciclo, hasta 3.5 s si la API tarda. Mientras tanto no corren ADC, LCD ni `mqtt.loop()`. Soluciones: reutilizar la conexión (`HTTPClient` persistente + `setReuse(true)`), subir el intervalo a 5–10 s, o mover la red a una tarea FreeRTOS con *long polling*.
2. **Buzzer con `delay()`.** El patrón de corte congela todo 3.1 s justo en el evento más importante, y se solapa con el beeper de batería crítica. Extraer a una clase no bloqueante con cola de patrones.
3. **Tres rutas de comandos.** El parser escribe a Serial; Telegram y MQTT necesitan lógica aparte. Un `processConsoleCommand(cmd, Print& out)` unificaría los tres canales y permitiría responder por MQTT.
4. **SOC por voltaje.** Inútil durante la carga. Con la pinza Hall ya se puede integrar Ah (*coulomb counting*) y resincronizar en `FLOAT`.
5. **Sin histéresis en batería crítica** (10.5 V): posible oscilación de buzzer/Telegram alrededor del umbral. Disparar a 10.5 y limpiar a ~10.8 V.
6. **Estado inicial de Línea 1** por lectura instantánea de un pin que oscila: pequeña probabilidad de falsa alerta de "retorno" al arrancar. Dejar que la primera ventana de 250 ms fije el estado en silencio.
7. **`gLine1LowSamples`** cuenta iteraciones del `loop()`, cuya frecuencia varía de kHz a < 1 Hz; el umbral "≥ 3" no es estable. Los flancos ya son suficientes.
8. **LCD parpadea** al rotar por `lcd.clear()`. Escribir siempre 16 caracteres por línea sin borrar.
9. **Stack del loop task**: TLS + buffers de Telegram (~4 KB) + payload de discovery (2.6 KB) sobre 8 KB por defecto. Añadir `-D ARDUINO_LOOP_STACK_SIZE=16384` en `platformio.ini`.
10. **Seguridad**: `setInsecure()` permite suplantar `api.telegram.org` desde la LAN e inyectar comandos de calibración; usar `setCACertBundle()`. La plantilla `AppBridgeSecrets.example.h` debe contener solo *placeholders*.
11. Detalles: validadores `isValid*` duplicados entre `main.cpp` y las librerías; cada muestra ADC hace 3 conversiones cuando basta 1 (`analogReadMilliVolts`); `printTelemetryJson` no imprime JSON.
