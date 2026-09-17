# Integraciones: WiFi, MQTT, Home Assistant y Telegram

## Configuración (`AppBridgeSecrets.h`)

Copia `lib/AppBridge/src/AppBridgeSecrets.example.h` a `AppBridgeSecrets.h` (ignorado por git) y define:

| Macro | Descripción | Defecto (`AppBridgeConfig.h`) |
|---|---|---|
| `APP_WIFI_SSID` / `APP_WIFI_PASSWORD` | Red WiFi 2.4 GHz | vacío |
| `APP_MQTT_HOST` / `APP_MQTT_PORT` | Broker MQTT | vacío / 1883 |
| `APP_MQTT_USERNAME` / `APP_MQTT_PASSWORD` | Credenciales del broker (opcional) | vacío |
| `APP_DEVICE_ID` | Identificador único del dispositivo (sin espacios) | `power_light_v1_banco` |
| `APP_MQTT_TOPIC_BASE` | Prefijo de tópicos | `home/power_light_v1_banco` |
| `APP_HA_DISCOVERY_PREFIX` | Prefijo de discovery de HA | `homeassistant` |
| `APP_HA_ENABLE_DISCOVERY` | 1 = publicar discovery | 1 |
| `APP_HA_DEVICE_NAME` / `_MODEL` / `_MANUFACTURER` / `_SW_VERSION` | Ficha del dispositivo en HA | `POWER LIGHT V1` / `XIAO ESP32-C6 + PZEM004T` / `DIY` / `1.0.0` |
| `APP_WIFI_ALLOW_SERIAL_INPUT` / `APP_WIFI_SERIAL_TIMEOUT_MS` | Pedir credenciales por Serial si no hay SSID | 1 / 20000 |
| `APP_TELEGRAM_ENABLE` | 1 = activar bot | 1 |
| `APP_TELEGRAM_BOT_TOKEN` / `APP_TELEGRAM_CHAT_ID` | Token de BotFather y chat autorizado | vacío |

Si faltan SSID o host MQTT, `AppBridge` se desactiva por completo (sin WiFi). Si faltan token o chat de Telegram, solo se desactiva Telegram.

Si tienes varios equipos, cambia `APP_DEVICE_ID` y `APP_MQTT_TOPIC_BASE` en cada uno; el resto puede ser idéntico.

## WiFi

- Modo estación (`WIFI_STA`), conexión no bloqueante, reintento cada 7 s si se pierde.
- Al conectar se imprime SSID, IP y RSSI por Serial. Si el SSID conectado no coincide con el configurado se avisa.
- El ESP32-C6 solo soporta 2.4 GHz.

## MQTT

Cliente: PubSubClient, `client_id = <APP_DEVICE_ID>-<8 hex del MAC>`, buffer 3072 B, reintento cada 5 s.

| Tópico | Dirección | QoS/retain | Contenido |
|---|---|---|---|
| `<base>/status` | publica | retain | `online` al conectar; `offline` por *Last Will* si el dispositivo cae. |
| `<base>/telemetry` | publica cada 5 s | retain | JSON de telemetría (abajo). |
| `<base>/alert` | publica en eventos | no retain | JSON de evento (abajo). |
| `<base>/cmd` | suscribe | — | Texto de un comando de consola (ver [comandos.md](comandos.md)). |
| `homeassistant/status` | suscribe | — | Cuando HA publica `online`, se re-publica el discovery. |
| `homeassistant/device/<APP_DEVICE_ID>/config` | publica al conectar | retain | Payload de discovery. |

### Telemetría

```json
{
  "device": "power_light_v1_banco",
  "uptime_ms": 1234567,
  "line1_ac": true,
  "pzem_valid": true,
  "pzem_v": 121.30,
  "pzem_a": 2.350,
  "pzem_w": 280.0,
  "bat_v": 12.65,
  "bat_a": -2.10,
  "bat_cap": 71
}
```

- `pzem_*` valen 0 cuando `pzem_valid` es `false`; las plantillas de HA los muestran como `unknown` en ese caso.
- `bat_a` positivo = carga, negativo = descarga (ya con dirección y deadband aplicados).
- `bat_cap` es el % estimado por voltaje (0–100).

### Alertas

```json
{ "device": "power_light_v1_banco", "uptime_ms": 1234567, "event": "LINE1_LOST" }
```

Eventos: `LINE1_LOST`, `LINE1_RESTORED`, `BAT_LOW_CRITICAL`, `BAT_LOW_RECOVERED`, `PZEM_DATA_LOST`, `PZEM_DATA_RESTORED`.

### Comandos

```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t "home/power_light_v1_banco/cmd" -m "HALL ZERO"
```

La respuesta sale por el monitor serie del dispositivo; por MQTT solo se ve el efecto (por ejemplo, el cambio en `bat_a` de la siguiente telemetría).

## Home Assistant

Requisitos: integración MQTT configurada contra el mismo broker y discovery activado (por defecto). No hace falta YAML.

El firmware publica **un solo** payload de discovery en formato *device* (`cmps`), retenido. HA crea el dispositivo con estas entidades:

| Entidad | Tipo | Fuente | Detalles |
|---|---|---|---|
| Voltaje AC | `sensor` | `pzem_v` | V, `device_class: voltage`, `unknown` si `pzem_valid` = false |
| Corriente AC | `sensor` | `pzem_a` | A, `device_class: current` |
| Potencia AC | `sensor` | `pzem_w` | W, `device_class: power` |
| Voltaje Bateria | `sensor` | `bat_v` | V |
| Corriente Bateria | `sensor` | `bat_a` | A |
| Capacidad Bateria | `sensor` | `bat_cap` | %, `device_class: battery` |
| Linea 1 | `binary_sensor` | `line1_ac` | `device_class: power` (ON = hay red) |
| Evento | `sensor` | `<base>/alert` → `event` | Último código de evento |

Todas con `unique_id = <APP_DEVICE_ID>_<clave>` y disponibilidad ligada a `<base>/status`, por lo que el dispositivo aparece *no disponible* si se apaga.

Si cambias el `APP_DEVICE_ID`, borra el dispositivo antiguo en HA (o el tópico de discovery retenido) para no duplicar entidades. Validación rápida desde la terminal del broker en [home_assistant/README.md](../home_assistant/README.md).

Ideas de automatización en HA: notificar en `Evento` = `LINE1_LOST`; alerta si `Voltaje Bateria` < 11.8 V durante 10 min; gráfico de `Corriente Bateria` para ver el ciclo carga/descarga; `utility_meter` sobre `Potencia AC` para consumo diario.

## Telegram

### Crear el bot

1. En Telegram, habla con **@BotFather** → `/newbot` → guarda el **token** (`123456789:AAAA…`).
2. Obtén tu **chat id**: escribe algo a tu bot y abre en el navegador `https://api.telegram.org/bot<TOKEN>/getUpdates`; el `chat.id` aparece en la respuesta. Para un grupo el id es negativo (también soportado).
3. Pon ambos en `AppBridgeSecrets.h` y reflashea.

### Comportamiento

- Al conectar por primera vez envía "POWER LIGHT V1 conectado." con el estado (reintenta cada 30 s hasta conseguirlo).
- Sondea `getUpdates` cada 1.5 s (`limit=5`, con `offset` para no repetir). Ignora cualquier mensaje que no venga del `chat_id` configurado.
- Envía mensajes en cada evento de la tabla de [firmware.md → Alertas](firmware.md#alertas-y-eventos) y un recordatorio cada 15 min mientras la batería esté en crítico.
- Comandos del bot en [comandos.md](comandos.md#comandos-del-bot-de-telegram).

### Limitaciones

- Cada petición abre una conexión TLS nueva y **bloquea el `loop()`** durante el intercambio (hasta 3.5 s). Con WiFi débil se nota en el LCD y en la respuesta a comandos por Serial.
- Mensajes de hasta ~760 caracteres codificados; los resúmenes actuales están muy por debajo.
- Sin WiFi, los mensajes de evento **se pierden** (no hay cola).

## Seguridad

- **Credenciales**: solo en `AppBridgeSecrets.h` (ignorado por git). La plantilla `.example.h` debe llevar valores ficticios. Si alguna vez se publicó una credencial real en el repositorio, cámbiala.
- **Telegram sin validación de certificado** (`setInsecure()`): alguien en la misma LAN podría suplantar `api.telegram.org` y enviar comandos de calibración. Mitigación: `telegramClient.setCACertBundle(...)` con el bundle de Arduino-ESP32.
- **MQTT sin TLS** en el puerto 1883: normal en una LAN doméstica; no lo expongas a Internet. Usa usuario/contraseña en el broker y, si quieres restringir, ACL para que solo HA pueda publicar en `<base>/cmd`.
- El tópico `<base>/cmd` acepta comandos que cambian calibraciones persistentes; cualquiera con acceso al broker puede alterarlas.
