# Backlog — ReverseVerb

Fuente única de verdad. Lo que estaba disperso entre el README, la conversación
y este archivo, junto.

---

## 0. Bloqueado ahora mismo

| | Estado |
|---|---|
| **Suite de DSP** | ✅ Hecha. 12 comprobaciones en ~4 s (`tools\run_tests.bat`). Encontró un glide de 1 s en el arranque del delay que no se había detectado de oído. |
| **pluginval** | ✅ **SUPERADA.** Primera ejecución: 19 secciones OK, 3 fallos en restauración de booleanos. Corregido con `SnappedBoolParameter` y revalidado en verde. |
| **Cargar el VST3 en Ableton** | ✅ Compila y pluginval lo carga correctamente. Falta la prueba de oído en sesión real. |

---

## 1. Acordado, listo para implementar

Ver la evaluación técnica detallada más abajo.

| # | Funcionalidad | Coste | Riesgo |
|---|---|---|---|
| ~~1~~ | ~~**Freeze / Drone**~~ | ✅ Hecho | Deriva medida: 0.000 dB tras 18 s |
| ~~2~~ | ~~**Envelope Follower → Drive**~~ | ✅ Hecho | Por muestra, no por bloque |
| ~~3~~ | ~~**Lecturas fraccionarias**~~ | ✅ Hecho | Transparente a rate=1, verificado bit a bit |
| ~~4~~ | ~~**Wow & Flutter + micro-detune**~~ | ✅ Hecho | Destapó un bug de lectura adelantada |
| ~~5~~ | ~~**Shimmer ±12 semitonos**~~ | ✅ Hecho | Barrido rehecho: el shimmer ESTABILIZA el lazo |

---

## 2. Propuesto, sin decidir

| Funcionalidad | Nota |
|---|---|
| **Anchura estéreo en el reverse** | Parcialmente cubierta por `Detune`, que desafina L y R en sentidos opuestos. Desfasar además los granos entre canales daría más anchura, pero ya no es la prioridad que era. |
| ~~Modulación del delay~~ | ✅ Hecho. L y R en cuadratura. |
| ~~Medidores de entrada/salida~~ | ✅ Hecho. Escala −48 a +6 dB. |
| ~~Presets adicionales~~ | ✅ Hecho. 24 en total; los 6 nuevos usan tape, shimmer y drive dinámico. |
| **Más presets** | Si vuelves a añadir: **siempre al final del array**, nunca en medio. |

---

## 3. Deuda técnica

| | Nota |
|---|---|
| **Tests de DSP como suite permanente** | Los barridos de estabilidad y de clicks se han escrito y tirado en cada iteración. Convertirlos en un `tests/` con un script que se pueda ejecutar de una vez evitaría que una refactorización futura rompa en silencio algo ya verificado. Relevante sobre todo antes de tocar el lazo de feedback. |
| **Ondulación al mover `Length`** | Reenganche de 2–3 granos con leve modulación de amplitud. Inaudible en material normal, perceptible en un seno puro sostenido. |
| **Sin build de macOS/Linux** | El código no usa nada específico de Windows, pero nunca se ha compilado fuera. Solo relevante si quieres distribuirlo. |

---

# Evaluación técnica de las cuatro propuestas

## Hallazgo 1: el motor de reverse ya *es* un pitch shifter granular

Esto no lo diseñé a propósito, pero conviene saberlo antes de decidir nada.

La topología actual — dos granos con ventana, solapados al 50 %, con crossfade
de potencia constante — es exactamente la de un pitch shifter granular tipo
PSOLA. La única diferencia es que aquí el incremento de lectura es **−1** en vez
de **+r**.

Cambiar ese incremento a **−r** da pitch shifting con el crossfade que ya
existe absorbiendo las discontinuidades. Son literalmente unas 20 líneas.

**Pero** hay un límite honesto: el tamaño de grano está atado al parámetro
`Length`, y un buen pitch shifting quiere granos de 30–80 ms. Con `Length` en
1500 ms el shifting sonaría muy embarrado, con un warble lento muy audible. Para
shimmer de calidad haría falta una etapa de shifting **independiente**, con su
propio tamaño de grano.

## Hallazgo 2: lecturas fraccionarias son infraestructura compartida

`ReverseDelay::read()` usa índices enteros. Wow/flutter y pitch shifting
necesitan **los dos** interpolación fraccionaria. Hacer esa refactorización una
vez habilita ambas, y convierte el micro-detune en casi gratis.

`StereoDelay` ya lee con interpolación lineal, así que el delay convencional ya
está listo para modulación.

---

## Orden recomendado

### 1. Freeze / Drone — coste BAJO, riesgo BAJO

**El mejor ratio de los cuatro, con diferencia.** Y es la única de las cuatro
que ya estaba prevista en el diseño.

La implementación es casi trivial: si `frozen`, no escribir en el buffer **y no
avanzar `writePos`**. Cada grano nuevo captura el mismo `start`, así que cicla
las mismas L muestras invertidas indefinidamente.

Tres cosas salen gratis por construcción:

- **Sin clicks.** Los granos mantienen su ventana, que sigue naciendo y muriendo
  en 0. Congelar a mitad de grano es limpio.
- **Sin riesgo de nivel.** No hay acumulación: no estamos realimentando, estamos
  dejando de escribir. El tope de feedback de 0.60 ni entra en juego.
- **Tocar encima funciona solo.** La señal seca pasa por el mix sin tocar el
  buffer congelado, que es justo lo que pides.

Estimación: ~15 líneas de DSP, un botón, y una animación del túnel
deteniéndose. Un rato.

### 2. Envelope Follower → Drive — coste BAJO, riesgo BAJO

El `Ducker` **ya calcula la envolvente de la señal seca**. Reutilizarla es
conectar dos cosas que ya existen.

Hay una propiedad del saturador que hace esto especialmente seguro, y es
consecuencia directa del bug que arreglamos en su día: como la fórmula es
`tanh(d·x)/d`, su **ganancia máxima es 1 sea cual sea `d`**. Modular el drive
por tanto **no afecta a la estabilidad del lazo**. Con la fórmula original
(`tanh(d·x)/tanh(d)`, ganancia `d/tanh(d)` > 1) esto habría sido peligroso:
tocar fuerte habría subido la ganancia del lazo y disparado la autooscilación.

Único cuidado real: suavizar el drive modulado, porque cambiar la curva de
saturación de golpe produce saltos de nivel. Un `SmoothedValue` lo resuelve.

Estimación: ~30 líneas, dos parámetros (cantidad y sensibilidad).

Matiz sobre "revolucionario": es un recurso conocido en pedales boutique y suena
muy bien, pero no es inédito. Dicho eso, la relación coste/impacto aquí es
excelente y en este plugin encaja particularmente bien, porque el drive vive
dentro del lazo y afecta solo a la cola.

### 3. Wow & Flutter — coste MEDIO, riesgo MEDIO-BAJO

Requiere primero la refactorización a lecturas fraccionarias en `ReverseDelay`.

Dos LFOs: **wow** a 0.3–2 Hz con ±10–50 cents, **flutter** a 6–12 Hz con ±3–15
cents. Las profundidades en muestras son modestas — a 48 kHz, ±30 cents de wow a
1 Hz son unas 133 muestras, y el flutter apenas 5 — así que **no comprometen el
margen del buffer**.

Decisión de diseño importante: los dos granos deben modularse con **el mismo
LFO**. Si se modulan por separado, el crossfade suma dos copias con afinaciones
distintas y produce filtrado en peine. Modularlos por separado a propósito y con
poca profundidad sería un chorus natural — buen candidato a parámetro opcional,
pero no el comportamiento por defecto.

Riesgo real: bajo. Es aditivo y no toca la estructura de granos.

### 4. Pitch shifting — coste ALTO, riesgo ALTO

Aquí hay que separar dos cosas que en la propuesta van juntas:

**Micro-detune (unos pocos cents):** casi gratis una vez hecho el punto 3. Es un
offset de afinación constante o muy lento. Recomendado, y probablemente da el
90 % del efecto "pared masiva y mareadora" que buscas.

**Shimmer de ±12 semitonos:** aquí está el trabajo de verdad, y dos problemas
serios:

- **Calidad atada a `Length`.** Ver Hallazgo 1. Un shimmer decente necesita su
  propia etapa con granos de 30–80 ms, independiente del reverse.
- **Invalida la medición de estabilidad.** Esto es lo importante. El tope de
  feedback de 0.60 salió de un barrido empírico sobre la topología *actual*. Un
  pitch shifter dentro del lazo cambia el problema por completo: las frecuencias
  migran hacia arriba en cada pasada y se acumulan en el extremo agudo, donde el
  filtro paso-bajo puede no alcanzar a controlarlas. **Habría que rehacer el
  barrido entero**, y probablemente el tope seguro sería más bajo y dependiente
  de la cantidad de shift.

Nota adicional: el shimmer clásico es pitch shift dentro del lazo del *reverb*.
No tenemos acceso a las tripas de `juce::Reverb`, así que habría que hacerlo en
nuestro propio lazo (el del reverse), lo cual funciona pero suena distinto al
shimmer canónico tipo Eventide.

---

## Resumen de la evaluación

| # | Funcionalidad | Coste | Riesgo | Depende de |
|---|---|---|---|---|
| 1 | Freeze / Drone | Bajo | Bajo | — |
| 2 | Envelope → Drive | Bajo | Bajo | — |
| 3 | Wow & Flutter | Medio | Bajo | Lecturas fraccionarias |
| 4a | Micro-detune | Bajo | Bajo | Punto 3 |
| 4b | Shimmer ±12 st | Alto | **Alto** | Etapa propia + rehacer barrido de estabilidad |

Sugerencia: **1 y 2 en una tanda** (independientes, baratas, impacto inmediato),
luego **3 + 4a juntas** porque comparten la refactorización, y dejar **4b** como
proyecto aparte con su propia campaña de pruebas.
