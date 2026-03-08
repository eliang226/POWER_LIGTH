# Home Assistant MQTT Discovery

This project now uses MQTT Device Discovery from firmware.

No manual `mqtt:` YAML entities are required.

Requirements on Home Assistant (Raspberry):

1. MQTT integration must be configured and connected to your broker.
2. MQTT Discovery must be enabled (default in Home Assistant MQTT integration).
3. Remove old manual package-based MQTT entities to avoid duplicate `unique_id`.

Firmware publishes discovery to:

- `homeassistant/device/<APP_DEVICE_ID>/config` (retained)

And runtime topics remain:

- `<APP_MQTT_TOPIC_BASE>/status`
- `<APP_MQTT_TOPIC_BASE>/telemetry`
- `<APP_MQTT_TOPIC_BASE>/alert`

## Quick validation on Raspberry (2 min)

Use these commands from the Raspberry terminal. Replace values with your broker credentials.

```bash
# 1) Check discovery payload (must be retained and non-empty)
mosquitto_sub -h <BROKER_IP> -p 1883 -u <USER> -P <PASS> \
  -t "homeassistant/device/power_light_v1_banco/config" -C 1 -v

# 2) Check device online status
mosquitto_sub -h <BROKER_IP> -p 1883 -u <USER> -P <PASS> \
  -t "home/power_light_v1_banco/status" -C 1 -v

# 3) Watch telemetry updates
mosquitto_sub -h <BROKER_IP> -p 1883 -u <USER> -P <PASS> \
  -t "home/power_light_v1_banco/telemetry" -v

# 4) Watch line alerts
mosquitto_sub -h <BROKER_IP> -p 1883 -u <USER> -P <PASS> \
  -t "home/power_light_v1_banco/alert" -v
```

Expected results:

1. Discovery topic prints one JSON config with `cmps`.
2. Status topic prints `online`.
3. Telemetry topic updates every few seconds.
4. Alert topic publishes `LINE1_LOST` / `LINE1_RESTORED` on transitions.
