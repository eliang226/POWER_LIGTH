# Hardware

## Lista de materiales

| # | Componente | Función | Notas |
|---|---|---|---|
| 1 | **Seeed XIAO ESP32-C6** | MCU, WiFi | RISC-V mono-núcleo 160 MHz, ~320 KB SRAM, 4 MB flash. Solo GPIO0–6 tienen ADC. |
| 2 | **PZEM-004T v3.0** (con CT de 100 A) | Medida AC de la salida del inversor | Modbus RTU a 9600 baud. |
| 3 | **QNHCK1-21** pinza Hall, variante 30/50/100/200 A | Corriente de batería con signo | +5 V, salida 2.5 V ± 2 V. Ver [sección dedicada](#sensor-de-corriente-qnhck1-21). |
| 4 | Divisor 40.2 kΩ / 10 kΩ (1 %) | Voltaje de batería | Máx. ≈ 16.5 V. |
| 5 | Divisor 3.9 kΩ / 10 kΩ | Adaptar salida 0.5–4.5 V del Hall a 3.3 V | Ver notas del ADC. |
| 6 | **H11AA1** + resistencias limitadoras | Detección de red AC ("Línea 1") | Optoacoplador con LED bidireccional. |
| 7 | LCD 16x2 con backpack I2C (PCF8574, dir. 0x27) | Pantalla local | |
| 8 | **DS1307** RTC (+ pila CR2032) | Hora para la alerta de retorno | Comparte bus I2C. |
| 9 | LED WS2812 / NeoPixel (1 píxel) | Estado de carga | |
| 10 | Buzzer activo | Alertas sonoras | Pilotado directo por GPIO; si consume > 20 mA usar transistor. |
| 11 | Fuente 5 V (desde la batería, buck) | Alimentación del XIAO, PZEM, Hall, LCD | |

## Pinout

| Pin XIAO | GPIO | Uso | Dirección | Notas |
|---|---|---|---|---|
| **A0 / D0** | 0 | H11AA1 colector (Línea 1) | Entrada, `INPUT_PULLUP`, interrupción `CHANGE` | LOW = hay AC. Oscila a 2× frecuencia de red. |
| **D1** | 1 | Salida del QNHCK1-21 (vía divisor) | ADC | Centro ≈ 1.80 V a 0 A. |
| **D2** | 2 | Divisor de batería | ADC | 12.6 V → 2.51 V. |
| **D3** | 21 | Datos WS2812 | Salida (RMT) | |
| **D4** | 22 | I2C SDA | — | LCD + DS1307 |
| **D5** | 23 | I2C SCL | — | LCD + DS1307 |
| **D6** | 16 | UART1 TX → PZEM RX | Salida | |
| **D7** | 17 | UART1 RX ← PZEM TX | Entrada | Ver nota de nivel 5 V. |
| **D8, D9** | 19, 20 | Libres | | |
| **D10** | 18 | Buzzer | Salida | HIGH = suena. |
| 5V | — | Alimentación 5 V | | Solo tiene tensión si el XIAO se alimenta por USB-C o por este pin. |
| 3V3 | — | Salida 3.3 V del regulador | | No usar para el Hall ni el PZEM. |
| GND | — | Masa común | | |

Los pines se definen al principio de `src/main.cpp` (`kNeoPixelPin`, `kPzemRxPin`, `kBatteryAdcPin`, `kHallAdcPin`, `kBuzzerPin`, `kLine1AcPin`, `kLCD_I2cAddress`).

## Esquema de conexión

```
                         +---------------------------+
   Red AC  ──► PZEM CT   |     XIAO ESP32-C6         |
   (salida  ──► PZEM V   |                           |
   inversor)   PZEM TX ──┤ D7 (RX1)     D4 (SDA) ├──┬── LCD 16x2 I2C (0x27)
               PZEM RX ◄─┤ D6 (TX1)     D5 (SCL) ├──┴── DS1307 RTC
                         |                           |
   Red AC ──[R]──H11AA1──┤ A0 (pull-up)  D3 (DATA) ├───── WS2812
              (colector) |                           |
                         |              D10        ├───── Buzzer ── GND
   Bat+ ──40.2k──┬──10k──┤ D2 (ADC)                  |
                 GND     |                           |
   QNHCK1-21 OUT ─3.9k─┬─┤ D1 (ADC)      5V  ├── +5 V (Hall, PZEM, LCD)
                     10k |               GND ├── GND común
                     GND +---------------------------+
```

## Detalle por bloque

### Medida AC: PZEM-004T v3.0

- El transformador de corriente (CT) abraza **uno** de los conductores de la salida del inversor; los cables de tensión (L/N) van a las entradas de voltaje del PZEM.
- Lado TTL: 5 V, GND, RX, TX. Está optoaislado del lado AC.
- El firmware pide todos los registros cada 2.5 s (`kPzemReadIntervalMs`). Si no hay respuesta válida durante 10 s (`kPzemDataTimeoutMs`) se declara `PZEM_DATA_LOST`.
- **Nivel lógico:** la línea TX del PZEM lleva pull-up a su VCC. El ESP32-C6 **no es tolerante a 5 V**. Opciones: alimentar el lado TTL del PZEM a 3.3 V (funciona en la práctica) o poner un divisor/zener en TX→D7.

### Voltaje de batería: divisor 40.2k / 10k

```
V_adc = V_bat × 10 / (40.2 + 10) = V_bat / 5.02
```

| V_bat | V_adc |
|---|---|
| 10.5 V (crítico) | 2.09 V |
| 12.7 V (reposo llena) | 2.53 V |
| 14.4 V (absorción) | 2.87 V |
| 15.0 V (ecualización) | 2.99 V |
| 16.5 V (máx. seguro, `kBatteryMaxSafeV`) | 3.29 V |

El firmware aplica `V_bat = V_sensada × scale + offset` con `scale` = 1.05 por defecto (compensa tolerancias) y ambos ajustables por comando (`BAT CAL`, `BAT SCALE`, `BAT OFFSET`). Usa resistencias del 1 % y un condensador de 100 nF entre D2 y GND si el cableado es largo.

### Sensor de corriente: QNHCK1-21

Transductor Hall de **lazo abierto, núcleo partido (pinza)**, uso industrial.

| Parámetro | Valor |
|---|---|
| Alimentación | +5 V DC |
| Salida a 0 A | 2.5 V |
| Salida a ±In (corriente nominal) | 2.5 V ± 2.0 V → 0.5 … 4.5 V |
| Sensibilidad | 2.0 V / In |
| Precisión / linealidad | ≤ 1 % / ≤ 1 % FS |
| Aislamiento | 2.5 kV |
| Ventana | ~21 mm |
| Temperatura | −25 … +85 °C |
| Conexión | 3 hilos: +5 V, GND, OUT (comprobar colores en la etiqueta) |

**Sensibilidad por variante y resolución en el ADC** (tras el divisor 3.9k/10k, LSB del C6 ≈ 0.8 mV):

| Variante | mV/A sensor | mV/A en ADC | A por LSB | Deadband sugerido |
|---|---|---|---|---|
| 30 A | 66.7 | 48.0 | 0.017 | 0.10 A |
| **50 A** | **40.0** | 28.8 | 0.028 | 0.10 A |
| 100 A | 20.0 | 14.4 | 0.056 | 0.25 A |
| 200 A | 10.0 | 7.2 | 0.11 | 0.50 A |

**Configuración en el firmware.** Las constantes de `src/main.cpp` conservan el nombre histórico `kAcs758*` pero describen este sensor:

```cpp
constexpr float kAcs758ZeroCurrentV     = 2.5f;    // salida a 0 A
constexpr float kAcs758SensitivityVPerA = 0.040f;  // 2.0 V / In  → 50 A
constexpr float kAcs758FullScaleCurrentA = 50.0f;
```

Los valores actuales corresponden a la **variante de 50 A**. Para otra variante basta cambiar `kAcs758SensitivityVPerA` (0.0667 / 0.020 / 0.010) y `kAcs758FullScaleCurrentA`. Alternativamente `CurrentHallMonitor` acepta `sensorSpanV = 2.0` + `fullScaleCurrentA = In` (modo *span*) que refleja la hoja de datos de forma directa; hoy está desactivado (`sensorSpanV = 0.0f` en `makeHallConfig()`).

**Elección de la variante según el inversor.** Corriente de batería ≈ P / (12 V × 0.85):

| Inversor | I de descarga aprox. | Variante recomendada |
|---|---|---|
| 300 W | 30 A | 50 A |
| 500 W | 50 A | 100 A |
| 1000 W | 100 A | 100–200 A |
| 2000 W | 200 A | 200–300 A |

Truco: pasar el cable **2 vueltas** por la ventana duplica la sensibilidad (y hay que dividir `kAcs758FullScaleCurrentA` entre 2).

**Montaje.**

- Va en el **cable positivo** de la batería. Al estar aislada galvánicamente, no importa la polaridad del cable; su GND es el del XIAO, no el de la batería.
- Solo **un** conductor dentro de la ventana; si pasan + y − juntos los campos se anulan.
- Colocarla **entre el borne + y la primera derivación** para medir la corriente neta de la batería (carga − descarga).
- La **flecha** del cuerpo marca el sentido positivo. Con la flecha apuntando hacia el borne + de la batería, carga = positivo, que es lo que espera el firmware. Si sale invertido, `CURR DIR -1`.
- Núcleo bien cerrado y cable centrado. Tras cerrarlo, `HALL ZERO` sin corriente. Alejarla del transformador del inversor y de otros cables de potencia.
- Deriva térmica: el cero puede moverse unas decenas de mV entre frío y caliente (±0.5–1 A en la variante de 50 A). Recalibrar el cero con el sensor a temperatura de trabajo.

**Rizado.** El inversor consume de la batería en pulsos a 2× la frecuencia de red (100/120 Hz). El firmware promedia 30 muestras (~8 ms, aprox. un período de rizado) y aplica un filtro EMA (α = 0.18), por lo que la lectura instantánea (`Iinst` en `HALL RAW`) salta bajo carga pero `Ifilt` es estable.

### Detección de red AC: H11AA1

```
            R1 (47k–100k, ≥1 W total, repartido en 2)
  L ──[R]──┬───► pin 1 ┐ H11AA1
           │           │  (LEDs antiparalelo)
  N ──[R]──┴───► pin 2 ┘
                       pin 5 (colector) ──► A0 (INPUT_PULLUP)
                       pin 4 (emisor)   ──► GND
```

- Con AC presente el transistor conduce en cada semiciclo → el pin cae a LOW ~120 veces/s (60 Hz) con pulsos cortos HIGH en los cruces por cero.
- Sin AC el pin queda HIGH fijo por el pull-up.
- El firmware cuenta flancos en la ISR y evalúa cada 250 ms: ≥ 6 flancos (o pin en LOW) ⇒ hay Línea 1.
- Dimensionar R para 1–3 mA por el LED. Usar resistencias de potencia adecuada a la tensión de red (120/220 V) y separación física de la parte de baja tensión.

### I2C: LCD 16x2 + DS1307

- Bus a 100 kHz por defecto en D4/D5. Ambos módulos suelen traer pull-ups incorporados.
- **Nivel del bus:** tanto el backpack del LCD como los módulos DS1307 "Tiny RTC" llevan pull-ups a **5 V**, lo que pone 5 V en SDA/SCL del ESP32-C6 (no tolerante a 5 V). Funciona en la práctica, pero lo correcto es quitar esos pull-ups (o alimentar el DS1307 a 3.3 V, que admite 3.3 V en el bus) y dejar solo pull-ups a 3.3 V de 4.7 kΩ.
- LCD en dirección `0x27` (`kLCD_I2cAddress`); si el backpack es A0-A2 distinto, cambiar a `0x3F` o la que corresponda.
- Si el DS1307 no se detecta al arrancar, el firmware sigue sin RTC y la alerta de retorno suena siempre.
- Si el RTC está parado (primera vez o pila agotada) se ajusta con la fecha/hora de compilación.

### LED WS2812

Un solo píxel en D3. El WS2812 espera datos a 5 V y el XIAO da 3.3 V; para un solo LED corto suele funcionar; si parpadea raro, alimentar el LED a ~4.3 V (diodo en serie) o añadir un level-shifter. Brillo global 40/255.

### Buzzer

Buzzer **activo** (con oscilador) en D10, HIGH = suena. Patrones en [firmware.md](firmware.md#buzzer).

## Alimentación

Todo el sistema se alimenta de la batería de 12 V a través de un convertidor buck a 5 V con capacidad ≥ 1 A (WiFi consume picos de 300–400 mA). El 5 V entra por el pin **5V** del XIAO y alimenta también PZEM, Hall, LCD y LED. El XIAO genera internamente los 3.3 V para la lógica.

Recomendado: fusible en el positivo de batería antes del buck, y condensador ≥ 470 µF en la línea de 5 V cerca del XIAO.

## Notas sobre el ADC del ESP32-C6

- 12 bits, atenuación 11/12 dB. El firmware usa `analogReadMilliVolts()`, que aplica la calibración de fábrica grabada en eFuse; es notablemente más lineal que convertir el valor crudo.
- La respuesta es fiable hasta ≈ 3.0–3.1 V; por encima se comprime. Con el divisor 3.9k/10k la pinza a +In entrega 3.24 V, ya en esa zona. Si vas a trabajar cerca de la corriente nominal, usar **R1 = 4.7 kΩ** (máx. 3.06 V). Lo mismo aplica al divisor de batería por encima de ~15.5 V.
- El ADC es sensible al ruido de WiFi. El promedio de 16–30 muestras por lectura y el EMA en los monitores existen por eso. Cables cortos y condensador de 100 nF en cada entrada ADC ayudan.
- GPIO0 (A0) se usa como entrada digital para el H11AA1; es un pin con ADC "desperdiciado". Si en el futuro necesitas otra entrada analógica, mover el H11AA1 a D8/D9.
