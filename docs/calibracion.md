# Calibración y puesta en marcha

Todos los ajustes se hacen con comandos (Serial, MQTT o Telegram; ver [comandos.md](comandos.md)) y quedan guardados en NVS, así que sobreviven a reinicios y a reflasheos del firmware (no a un `pio run -t erase`).

Material: multímetro, una carga DC conocida (o pinza amperimétrica de referencia), y acceso al monitor serie.

## Orden recomendado

1. RTC
2. Voltaje de batería
3. Cero del sensor de corriente
4. Dirección de la corriente
5. Ganancia del sensor de corriente (opcional)
6. Deadband
7. Hora de la alerta de retorno
8. Prueba de alertas

## 1. RTC

```
RTC STATUS
RTC SET 2026-09-17 14:05:00
```

Si el DS1307 no se detecta, verás `RTC: DS1307 no detectado.` al arrancar y `ALERT HOUR` no tendrá efecto (el buzzer de retorno sonará siempre). Comprueba la pila CR2032 y el bus I2C.

## 2. Voltaje de batería

1. Con el sistema en reposo (sin cargador activo, carga ligera) mide el voltaje **en los bornes de la batería** con el multímetro.
2. Envía el valor:
   ```
   BAT CAL 12.65
   ```
   El firmware recalcula `scale` para que la lectura coincida y muestra un `BAT STATUS`.
3. Comprueba a otro voltaje (por ejemplo con el cargador en absorción, ~14.4 V). Si hay desviación en los extremos, ajusta el offset:
   ```
   BAT OFFSET 0.05
   ```
   y repite `BAT CAL` en el punto medio.

Rangos válidos: `scale` 0.5–1.5, `offset` ±2 V. Si `BAT CAL` responde error, la lectura actual es inválida (divisor desconectado o fuera de rango).

## 3. Cero del sensor de corriente (imprescindible)

El QNHCK1-21 tiene una tolerancia de cero de fábrica y deriva con temperatura; el cero **debe** calibrarse en la instalación.

1. Asegúrate de que **no circula corriente** por el cable que abraza la pinza: desconecta el inversor y el cargador, o abre la pinza y ciérrala sin cable dentro.
2. Deja el sensor unos minutos a su temperatura de trabajo.
3. Ejecuta:
   ```
   HALL ZERO
   ```
   Toma 300 muestras y guarda `hall_zero`. Debe quedar cerca de 2.500 V (típico ±0.02 V).
4. Verifica con `HALL RAW`: `dV` ≈ 0.000 V e `Ifilt` ≈ 0.00 A.

Repite este paso cada vez que abras/cierres la pinza o cambies su posición.

## 4. Dirección de la corriente

El firmware espera **carga = positivo**.

1. Pon la batería en **carga** (cargador o inversor cargando) o en **descarga** clara (inversor con carga).
2. Mira `CURR STATUS`. Si con la batería cargando aparece `DIS` (o descargando aparece `CHG`):
   ```
   CURR DIR -1
   ```
   No hace falta girar la pinza.

## 5. Ganancia del sensor (opcional)

La ganancia de fábrica (`gain = 1.0`) ya da ≤ 1 % de error si la constante `kAcs758SensitivityVPerA` coincide con tu variante (ver [hardware.md](hardware.md#sensor-de-corriente-qnhck1-21)). Calibra solo si dispones de una referencia fiable.

1. Haz circular una corriente **conocida y estable**, cuanto mayor mejor (mínimo 1 A; ideal ≥ 10 % de la corriente nominal del sensor). Mídela con una pinza de referencia.
2. Ejecuta con el valor medido (siempre positivo):
   ```
   CURR CAL 4.2
   ```
3. El firmware calcula `gain = I_real / I_medida` y lo guarda. Verifica con `HALL RAW`.

Si el resultado está muy lejos de 1.0 (por ejemplo 2.0 o 4.0) lo más probable es que la constante de sensibilidad no corresponda a tu variante (100 A o 200 A); corrige la constante en `main.cpp` y vuelve a calibrar en lugar de compensar con la ganancia.

## 6. Deadband

Con el sistema en reposo observa `Ifilt` en `HALL RAW` durante un minuto y anota el ruido residual. Fija el deadband ligeramente por encima:

```
CURR DEAD 0.15
```

Orientación: 0.10 A (30/50 A), 0.25 A (100 A), 0.50 A (200 A). Un deadband demasiado alto oculta consumos pequeños; demasiado bajo hace que `CHG`/`DIS`/`IDLE` parpadeen.

## 7. Hora de la alerta de retorno

Por defecto el buzzer suena en cada retorno de red. Para limitarlo a una hora concreta (p. ej. solo si vuelve a las 7 de la mañana):

```
ALERT HOUR 7
ALERT STATUS
```

Para volver al comportamiento por defecto: `ALERT HOUR ANY`. Telegram y MQTT avisan siempre, independientemente de esta hora.

## 8. Prueba de alertas

```
ALERT TEST LOST       ← 4 pulsos
ALERT TEST RESTORE    ← 2 pulsos (según ALERT HOUR)
```

Para probar la cadena completa (LCD, MQTT, Telegram, buzzer), desconecta la entrada AC del H11AA1: en ≤ 250 ms debe aparecer la pantalla de alerta, publicarse `LINE1_LOST` y llegar el mensaje de Telegram. Reconecta y comprueba `LINE1_RESTORED`.

## Verificación final

- `BAT STATUS` → `STATUS=IN_RANGE`, voltaje coincide con el multímetro (±0.05 V).
- `CURR STATUS` → `IDLE` en reposo; signo correcto en carga y descarga.
- `RTC STATUS` → hora correcta.
- Serial cada 5 s: bloque `TELEMETRIA` con `PZEM: V=...` (no `lectura invalida`).
- Home Assistant: dispositivo *POWER LIGHT V1* con 8 entidades actualizándose cada 5 s.
- Telegram: `/estado` responde.

## Cuándo recalibrar

| Situación | Qué repetir |
|---|---|
| Abrir/cerrar o mover la pinza | `HALL ZERO` |
| Cambio grande de temperatura ambiente | `HALL ZERO` |
| Cambiar la variante del sensor | Constantes en `main.cpp` + `HALL ZERO` (+ `CURR CAL`) |
| Cambiar resistencias de un divisor | `BAT CAL` o `HALL ZERO` según el caso |
| Pila del RTC agotada | `RTC SET` |
| Borrado de flash (`erase`) | Todo |
