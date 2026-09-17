# Referencia de comandos

## Canales

| Canal | Cómo enviar | Dónde se ve la respuesta | Límite |
|---|---|---|---|
| **Serial** | Monitor a 115200 baud, fin de línea `LF` (`monitor_eol = LF` en `platformio.ini`) | Serial | 95 caracteres |
| **MQTT** | Publicar el texto en `<APP_MQTT_TOPIC_BASE>/cmd` | **Serial** (no hay respuesta por MQTT) | 159 caracteres |
| **Telegram** | Mensaje al bot desde el chat autorizado | Telegram (resumen) y Serial (detalle) | Solo grupos `BAT`, `CURR`, `HALL`, `RTC`, `ALERT` |

Los comandos no distinguen mayúsculas/minúsculas y se recortan espacios. Los argumentos numéricos aceptan punto decimal.

Ejemplo por MQTT:

```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/power_light_v1_banco/cmd" -m "CURR STATUS"
```

## Comandos de consola (Serial / MQTT / Telegram vía `/cmd`)

### General

| Comando | Descripción |
|---|---|
| `HELP` | Lista todos los comandos por Serial. (`HALL HELP` es alias.) |

### Batería (`BAT`)

| Comando | Descripción | Persiste |
|---|---|---|
| `BAT STATUS` | Muestra RAW, mV, V_adc, V_sense, V_bat, V_filt, scale, offset, SOC y estado de rango. | — |
| `BAT CAL <voltios>` | Calibra el factor `scale` para que la lectura actual coincida con el voltaje real medido con multímetro. Ej.: `BAT CAL 12.65` | `bat_scale` |
| `BAT SCALE <factor>` | Fija el factor manualmente. Rango 0.5000–1.5000. | `bat_scale` |
| `BAT OFFSET <voltios>` | Fija el offset en V. Rango −2.000 … 2.000. | `bat_offs` |

### Corriente de batería (`CURR` / `HALL`)

| Comando | Descripción | Persiste |
|---|---|---|
| `CURR STATUS` | Corriente corregida (dir + deadband), flujo `CHG/DIS/IDLE`, dir, deadband y volcado del sensor. | — |
| `CURR ZERO` / `HALL ZERO` | Calibra el **cero** del sensor con 300 muestras. **Ejecutar sin corriente por la pinza.** | `hall_zero` |
| `CURR CAL <amps>` / `HALL GAIN <amps>` | Calibra la **ganancia** con una carga conocida circulando. Mínimo 1.0 A. Ej.: `CURR CAL 4.2` | `hall_gain` |
| `CURR DIR <1\|-1>` | Invierte el signo si carga/descarga salen al revés. | `hall_dir` |
| `CURR DEAD <amps>` | Deadband: corrientes menores se reportan como 0 A / `IDLE`. Rango 0.00–5.00. Ej.: `CURR DEAD 0.15` | `hall_dead` |
| `HALL RAW` | Volcado crudo del sensor: RAW, mV, V_adc, V_sensor, V_zero, dV, I_inst, I_filt, I_corr, I_dead, sensibilidad, ganancia. | — |
| `HALL STATUS` | Parámetros nominales del sensor, ratio del divisor, calibración actual y volcado crudo. | — |

### RTC

| Comando | Descripción |
|---|---|
| `RTC STATUS` | Fecha y hora actuales del DS1307. |
| `RTC SET YYYY-MM-DD HH:MM:SS` | Ajusta el reloj. Ej.: `RTC SET 2026-09-17 14:05:00` |

### Alertas (`ALERT`)

| Comando | Descripción | Persiste |
|---|---|---|
| `ALERT STATUS` | Muestra la hora configurada para la alerta sonora de retorno (o `SIEMPRE`). | — |
| `ALERT HOUR <0-23>` | El buzzer de **retorno de red** solo sonará si el retorno ocurre dentro de esa hora (HH:00–HH:59). Útil para no despertar de noche. La alerta de corte suena siempre. | `rest_hour` |
| `ALERT HOUR ANY` | El buzzer de retorno suena a cualquier hora. | `rest_hour` |
| `ALERT TEST LOST` | Reproduce el patrón de corte (4 pulsos). | — |
| `ALERT TEST RESTORE` | Reproduce el patrón de retorno (2 pulsos), respetando `ALERT HOUR`. | — |

## Comandos del bot de Telegram

Solo se aceptan mensajes del `chat_id` configurado. El bot elimina `/` y el sufijo `@nombre_del_bot`.

| Mensaje | Respuesta |
|---|---|
| `/estado`, `/status` | Resumen general (abajo). |
| `/bat` | Resumen de batería. |
| `/curr`, `/hall` | Resumen de corriente. |
| `/rtc` | Fecha y hora del RTC. |
| `/alert` | Estado de alertas: hora de retorno, Línea 1, batería crítica, PZEM. |
| `/help`, `/ayuda`, `/start` | Lista de comandos del bot. |
| `/cmd <comando de consola>` | Ejecuta el comando (solo grupos `BAT`, `CURR`, `HALL`, `RTC`, `ALERT`) y responde con el resumen del grupo. Ej.: `/cmd CURR ZERO`, `/cmd BAT CAL 12.60`, `/cmd RTC SET 2026-09-17 14:05:00` |
| `BAT CAL 12.6`, `CURR DEAD 0.2`, `ALERT HOUR 7`, … | Los comandos de esos grupos también funcionan sin `/cmd`. |

Formato de `/estado`:

```
POWER LIGHT V1
AC: LINE | MQTT: OK
VBat: 12.65V  IBat: -2.10A
Capacidad: 71%
AC V:121.3 A:2.35 W:280
WiFi IP: 192.168.1.50
```

Formato de `/bat`, `/curr` y `/alert`:

```
BAT                          CURR                            ALERT
VBat: 12.65V                 IBat: -2.10A DIS                Retorno: SIEMPRE
Vadc: 2.512V Sense: 12.61V   Hall: zero 2.498V gain 1.0000   Linea1: ACTIVA
Cal: x1.0500 off 0.000V      Dir: +1 dead: 0.10A             BatCrit: OFF
SOC: 71%                                                     PZEM: OK
```

Mensajes no reconocidos reciben `Comando no reconocido. Usa /help.`

## Salida periódica por Serial

Cada 5 s el firmware imprime un bloque de telemetría:

```
============ TELEMETRIA ============
t(ms): 123456
PZEM: V=121.30V | I=2.350A | P=280.0W | E=12.345kWh | F=60.0Hz | PF=0.98
BAT : V=12.65V | I=-2.10A | FLOW=DIS | STATUS=IN_RANGE
ACS758: RAW=2231 | ADC=1798mV | Vadc=1.798V | Vsens=2.499V | Vzero=2.498V | dV=0.001V | Iinst=0.03A | Ifilt=0.02A | Icorr=0.02A | Idead=0.00A | Sens=40.0mV/A | Gain=1.000
====================================
```

(La línea `ACS758:` corresponde al sensor Hall QNHCK1-21; el nombre es histórico.)

Además se registran por Serial todos los eventos de WiFi/MQTT/Telegram, las alertas y el resultado de cada comando.

## Entrada de credenciales WiFi por Serial

Si `APP_WIFI_SSID` está vacío, al arrancar el firmware espera 20 s (`APP_WIFI_SERIAL_TIMEOUT_MS`) una línea con el formato:

```
WIFI:MiRed,MiClave
```

Las credenciales recibidas así **no se guardan** en NVS; se pierden al reiniciar. Es un mecanismo de prueba en banco.
