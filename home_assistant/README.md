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
