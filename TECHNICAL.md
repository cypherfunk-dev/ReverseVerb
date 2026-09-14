# ReverseVerb — documentación técnica

Este archivo es para quien va a **compilar, testear o modificar** el plugin.
Si solo quieres usarlo, lo que buscas está en el [README](README.md). Lo que
falta por probar de oído y en un DAW está en [TESTING.md](TESTING.md).

---

## Índice

1. [Build](#build)
2. [Flujo de trabajo con el DAW](#flujo-de-trabajo-con-el-daw)
3. [Testing](#testing)
4. [ASIO](#asio)
5. [Arquitectura](#arquitectura)
6. [Motor de reverse](#motor-de-reverse)
7. [Estabilidad del lazo de realimentación](#estabilidad-del-lazo-de-realimentación)
8. [Saturador](#saturador)
9. [Drive dinámico](#drive-dinámico)
10. [Freeze](#freeze)
11. [Tape: wow, flutter y detune](#tape-wow-flutter-y-detune)
12. [Shimmer](#shimmer)
13. [Delay estéreo](#delay-estéreo)
14. [Reverb de placa](#reverb-de-placa)
15. [Configuración de buses](#configuración-de-buses)
16. [Presets y estado](#presets-y-estado)
17. [MIDI](#midi)
18. [Interfaz y coste de CPU](#interfaz-y-coste-de-cpu)
19. [Bugs encontrados y lecciones](#bugs-encontrados-y-lecciones)

---

## Build

**Requisitos:** Visual Studio con "Desarrollo para escritorio con C++" (trae
compilador y CMake) y Git. JUCE 8.0.12 se descarga solo vía `FetchContent` la
primera vez que configuras (~500 MB, varios minutos).

Desde el **Developer Command Prompt** de tu versión de VS:

```bat
cmake -B build
cmake --build build --config Release
```

**Sin `-G`.** Desde el Developer Command Prompt, CMake detecta solo el Visual
Studio instalado. Si lo fuerzas a mano tienes que acertar con la versión
(`"Visual Studio 17 2022"`, `"Visual Studio 18 2026"`...) y es un error fácil de
cometer. Si necesitas forzarlo por algún motivo, añade también `-A x64`.

### Targets

| Target | Salida |
|---|---|
| `ReverseVerb_VST3` | `build\ReverseVerb_artefacts\Release\VST3\ReverseVerb.vst3` |
| `ReverseVerb_Standalone` | `build\ReverseVerb_artefacts\Release\Standalone\ReverseVerb.exe` |
| `ReverseVerbTests` | `build\Release\ReverseVerbTests.exe` |

Para iterar rápido, compila solo el Standalone:

```bat
cmake --build build --config Release --target ReverseVerb_Standalone
```

Es mucho mejor sitio para probar que el DAW: si el plugin peta, cierras la
ventana y ya. En un DAW puedes perder trabajo, y además algunos ponen en lista
negra los plugins que crashean.

### `COPY_PLUGIN_AFTER_BUILD` está en FALSE a propósito

Copiar a `C:\Program Files\Common Files\VST3` exige permisos de administrador y
hace fallar la build entera por un paso que ni siquiera es de compilación. Para
desarrollar es mejor que el DAW lea directamente de la carpeta de build (ver
siguiente sección).

### Versión de JUCE

`CMakeLists.txt` fija `GIT_TAG 8.0.12`, la última 8.x estable. Si quieres
probar la rama 9, cambia el tag a `9.0.1`, pero salió en agosto de 2026 y puede
traer cambios de API.

---

## Flujo de trabajo con el DAW

Apunta Ableton a la carpeta de build (Preferencias → Plug-Ins → carpeta
personalizada VST3 → `...\build\ReverseVerb_artefacts\Release\VST3`). A partir
de ahí cada recompilación queda activa con un rescan, sin copiar nada ni pedir
permisos.

### Cierra Ableton antes de recompilar

Mientras Ableton tiene el plugin cargado mantiene el archivo abierto, y el
enlazador no podrá sobrescribirlo. El error es del tipo
`cannot open file ... ReverseVerb.vst3` y despista bastante, porque parece un
problema de compilación cuando en realidad es un bloqueo de archivo. Quitar el
plugin de las pistas no siempre libera la DLL.

### Ableton y el aviso MONO

Ableton pasa siempre estéreo por la cadena de dispositivos, así que el aviso
`MONO` del visualizador no aparece aunque la fuente sea mono: el plugin ve dos
canales idénticos y el ping-pong funciona. Ese aviso es para hosts que sí
manejan pistas mono de verdad (Reaper, Logic) y para el Standalone con una
interfaz de una sola entrada.

---

## Testing

Hay dos capas, y son complementarias: una prueba el DSP, la otra prueba el
plugin como ciudadano de un DAW.

### Suite de DSP

```bat
tools\run_tests.bat            barrido rapido (~4 s)
tools\run_tests.bat --full     barrido exhaustivo (minutos)
```

Los motores (`ReverseDelay`, `StereoDelay`, `Ducker`, `OnePole`, `TapeMod`,
`PitchShifter`) son headers puros sin JUCE **a propósito**, así que la suite
compila y corre en segundos sin arrastrar el framework ni instanciar un host.
Es lo que la hace barata de ejecutar, y una suite barata es una suite que se
ejecuta.

Comprobaciones (36): que no hay clicks en la costura de los granos, que la ventana
da potencia constante, que de verdad invierte (correlación cruzada contra la
entrada invertida), que mover `Length` no produce saltos, que ambos lazos de
realimentación decaen aunque se pida feedback al máximo, que el retardo es
exacto al preparar, que el ping-pong alterna canales, que el ducker atenúa y
vuelve, que el filtro da −3 dB en su corte, que la lectura fraccionaria a
`rate=1` es bit a bit idéntica a la versión entera, que Freeze no deriva, que
el clamp de modulación adelantada funciona en el caso extremo, que el ducking
atenúa igual a −20 dBFS que a 0 dBFS, que un NaN inyectado no se queda en el
lazo, que la interpolación cúbica es exacta en enteros y ≥5× mejor que la
lineal entre ellos, que el pitch shifter tiene ganancia unidad, que la placa
decae siempre / respeta el pre-delay / sale decorrelada / apila octavas sin
escaparse a siseo / suena igual a 44.1 y 96 kHz / no produce NaN, que el
saturador a 2× rebaja los aliasing sin tocar la fundamental, que soltar
Freeze no chasca y el release apaga en el tiempo pedido, que el Low Cut de
12 dB aparta más graves, y que nada produce NaN con entradas de 1e6 o 1e−30.

**Por qué existe:** los tres peores bugs del proyecto no se detectan de oído.

- Un saturador con una ganancia oculta de hasta +21 dB dentro del lazo.
- Un lazo que se autooscilaba y sostenía +10 dBFS treinta segundos después de
  cortar la señal.
- Un click de 1.30 de amplitud al mover un slider.

Los tres se manifiestan como "a veces suena raro y no sé por qué", y lo natural
es culpar a la interfaz o al DAW antes que al algoritmo. **Ejecuta la suite
antes de tocar cualquier cosa del lazo de realimentación.**

El script distingue "tests en rojo" (código de salida 1) de "el proceso murió"
(cualquier otro código), lo que ahorra mucho tiempo de diagnóstico.

### Tests a nivel de procesador

```bat
cmake --build build --config Release --target ReverseVerbHostTests
build\ReverseVerbHostTests_artefacts\Release\ReverseVerbHostTests.exe
```

`tests/host_tests.cpp`. Consola con JUCE que instancia `ReverseVerbProcessor`
de verdad (sin ventanas; `ScopedJuceInitialiser_GUI` para el
`MessageManager`, que hace falta porque el MIDI se aplica por `AsyncUpdater`).
Cubre el hueco entre la suite de DSP y pluginval: bloques de 0 / pequeños /
mayores que `samplesPerBlock` y que un bloque grande suena igual que en
trozos; bypass con cola y seca a 0 dB; estado de ida y vuelta con los 39
parámetros; los 25 presets cargan, son idempotentes y suenan finito, y no
tocan el tempo; MIDI de extremo a extremo (learn, momentáneo, toggle,
continuo, program change, tap tempo a 500 ms → 119.7 BPM, mapeos en el
estado); trim de salida exacto y neutro en bypass. 17 comprobaciones.

`tools\run_tests.bat` corre las dos suites; el CI también (en Linux bajo
`xvfb-run`, porque el inicializador de JUCE toca X11 aunque no abra nada).

### Validación del plugin con pluginval

```bat
tools\run_pluginval.bat        nivel 8 (recomendado)
tools\run_pluginval.bat 10     nivel maximo
```

Descarga [pluginval](https://github.com/Tracktion/pluginval) la primera vez
(a `tools\pluginval\`) y valida el VST3. Prueba lo que la suite de DSP no puede:
bloques de tamaño irregular, sample rates extremos, automatización desde otro
hilo, abrir y cerrar el editor cientos de veces, y llamadas fuera de orden que
un DAW real hace aunque la documentación diga que no.

Se ejecuta con `--validate-in-process` para que, si algo peta, veas un mensaje
útil y no solo un código de salida.

Merece la pena antes de confiarle una sesión con trabajo dentro. Si Ableton pone
el plugin en lista negra por un crash, recuperarlo es más molesto que este paso.

Lo que encontró en la primera pasada está en
[Bugs encontrados](#bug-3-booleanos-que-no-se-restauraban-pluginval).

### CI

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) corre en cada push y
PR, en **Windows, macOS (binario universal arm64+x86_64) y Linux**:

1. Configura y compila VST3, Standalone y tests (JUCE cacheado en
   `build/_deps` por sistema y por hash del `CMakeLists.txt`).
2. Corre la suite de DSP.
3. Descarga pluginval y valida el VST3 a nivel 8 (en Linux bajo `xvfb-run`,
   porque no hay servidor gráfico y pluginval abre el editor).
4. Sube un zip por sistema con el `.vst3` y el Standalone como artefacto.

En un tag `v*` además publica una **release** de GitHub con los tres zips.
Para sacar una versión:

```bat
git tag v0.2.0
git push origin v0.2.0
```

El código no usa nada específico de Windows, pero hasta que el CI haya pasado
en verde en los tres sistemas, las builds de macOS y Linux son "deberían
funcionar", no "funcionan".

---

## ASIO

JUCE no puede incluir el SDK de ASIO: es de Steinberg y su licencia no permite
redistribuirlo. Por defecto el Standalone solo ofrece **Windows Audio**, que
para probar este efecto sobra (la ventana son cientos de ms, la latencia del
driver es irrelevante).

Si lo quieres: descarga el SDK de steinberg.net/developers, descomprímelo, y
reconfigura apuntando a él:

```bat
cmake -B build -DASIO_SDK_DIR="C:/SDKs/asiosdk"
```

---

## Arquitectura

```
Source/
  PluginProcessor.{h,cpp}   APVTS, parámetros, ruteo, bus layout
  PluginEditor.{h,cpp}      GUI: modos Simple/Completo, visualizador polar, knobs, medidores
  Localisation.h            idioma de la GUI: inglés fuente + tabla al español
  PresetManager.{h,cpp}     25 presets de fábrica + presets de usuario en %APPDATA%
  MidiControl.{h,cpp}       CC -> parámetro (learn), Program Change -> preset, tap tempo
  Interp.h                  Hermite de 4 puntos para lecturas fraccionarias
  Saturator.h               tanh(d·x)/d con oversampling 2x, para el lazo del reverse
  ReverseDelay.h            motor de reverse (granular, 2 granos, lectura fraccionaria)
  StereoDelay.h             delay estéreo con ping-pong y modulación
  PlateReverb.h             placa de Dattorro con shimmer en el tanque
  PitchShifter.h            shifter granular para el shimmer
  TapeMod.h                 LFOs de wow y flutter, en cents
  Ducker.h                  seguidor de envolvente (ducking + drive dinámico)
tests/
  dsp_tests.cpp             suite sin JUCE (36)
  host_tests.cpp            procesador con JUCE, sin ventanas (17)
tools/
  run_tests.bat, run_pluginval.bat
```

Los headers de DSP no incluyen JUCE. Eso es lo que permite que los tests
compilen en segundos y es una restricción que conviene mantener.

### Tamaño de bloque

Los buffers de trabajo (`wetBuffer`, `parBuffer`, `driveCurve`, `modCurve`)
se reservan en `prepareToPlay` al tamaño anunciado por el host y **nunca** se
redimensionan en el hilo de audio. Dos casos límite en `processBlock`:

- **Bloque de 0 muestras** (Reaper parado, renders offline): se devuelve sin
  hacer nada. Sin el guard se leía `driveCurve[-1]`.
- **Bloque mayor que el anunciado** (FL Studio, algunos hosts al congelar
  pistas): se trocea al tamaño reservado y `processBlock` se llama a sí mismo
  por cada trozo. Antes se devolvía la señal seca sin mezclar, que con Mix al
  50 % era un salto de +6 dB y un hueco en la cola.

### Bypass con cola

`processBlockBypassed` está sobrescrito: pone un flag y llama a `processBlock`.
Con el flag activo, el efecto recibe **silencio** en vez de la entrada (así la
cola se agota sola), el ducking se desactiva, y la ganancia de la señal seca
sube a 1 mientras la húmeda conserva su nivel. Las dos ganancias van en
`SmoothedValue` separados para que la transición no salte. Sin esto, JUCE
cortaba la cola de golpe al pulsar bypass en el host.

### Suavizado a ritmo de bloque

Los feedbacks y los tres parámetros del reverb se suavizan una vez por bloque
con `blockSmooth()`, cuyo coeficiente sale del número de muestras del bloque y
una constante de tiempo en segundos (50 ms). Antes era un `0.25` fijo por
bloque, que a 64 muestras convergía en 7 ms y a 2048 en 460 ms.

No se usa `SmoothedValue` para los feedbacks porque las rutas Pre/Post/Paralelo
llaman a los motores en distinto orden y `getNextValue()` se adelantaría un
número distinto de veces según la ruta.

El reverb además se **resetea** cuando pasa de bypass (`Reverb` a 0) a activo:
si no, soltaba la cola rancia que tenía dentro de cuando se apagó.

### Cadena de señal

```
in ─► [mono→stereo dup] ─► Ducker (envolvente de la señal seca)
                          │
                          ├─► ruteo DELAY / REVERSE según `routing`
                          │       Off:          reverse
                          │       Delay→Rev:    delay → reverse
                          │       Rev→Delay:    reverse → delay
                          │       Paralelo:     0.7·reverse + 0.7·delay
                          │
                          ├─► PlateReverb (post o pre, según `revpost`)
                          ├─► ducking
                          └─► mix con la señal seca ─► out
```

En **Paralelo** las dos ramas se suman a 0.7 cada una porque son señales
decorrelacionadas y sumarlas a 1.0 dispararía el nivel.

Con `revpost = false` el reverb entra al buffer del reverse y se invierte con
él (carácter de "succión"). Es la única ruta en la que el reverb está dentro
del proceso de inversión.

---

## Motor de reverse

`ReverseDelay.h`. Dos granos solapados al 50 % que leen el buffer hacia atrás,
cada uno con ventana sin/cos de potencia constante. La costura entre granos es
un crossfade equal-power, y es lo que permite mover `Length` en caliente sin
clicks.

### Lectura fraccionaria

La posición de lectura es un float con interpolación **cúbica** (Hermite /
Catmull-Rom de 4 puntos, en `Interp.h`, compartida por los tres motores):

```
pos = start - phase * rate + modOffset
```

Con `rate=1` y `modOffset=0` la posición cae en enteros exactos y el resultado
es **bit a bit idéntico** a la versión con índices enteros — hay un test que lo
comprueba (diferencia máxima 0.000000000). Hermite en `t=0` devuelve `x0`
exacto, así que la cúbica conserva esa transparencia. Entre muestras, en
cambio, la lineal actuaba como un paso bajo que iba y venía con la fase
fraccionaria; la cúbica reduce ese error unas 25× (medido en el test sobre un
seno a 2 kHz: 0.0004 frente a 0.011). Se nota con wow, flutter, detune,
shimmer y el chorus del delay, que es cuando se lee entre muestras.

Hermite lee `i-1..i+2`. En el reverse, las 2-3 primeras muestras de cada grano
tocan posiciones aún no escritas en este ciclo, pero ahí la ventana vale
`sin(π/L) ≈ 0`. En `StereoDelay` la distancia mínima subió de 2 a 3 para que
`i+2` nunca caiga en `writePos`. En `PitchShifter` la entrada se escribe antes
de leer, así que el margen de 2 sigue bastando.

La lectura fraccionaria habilita dos cosas:

- **`modOffset`** → wow y flutter. Modular la posición de lectura es modular la
  afinación.
- **`rate`** → pitch shifting. Verificado: leyendo al doble de velocidad, un
  seno de 200 Hz sale a 400 Hz.

Esta topología —granos con ventana, solapados al 50 %, con crossfade de
potencia constante— **es** la de un pitch shifter granular tipo PSOLA. La única
diferencia es que aquí el incremento es negativo. `rate = r` desplaza la
afinación `12·log2(r)` semitonos.

La limitación honesta: el tamaño de grano está atado a `Length`, y un buen pitch
shifting quiere granos de 30–80 ms. Con `Length` en 1500 ms el shifting suena
muy embarrado. Por eso el shimmer tiene su propia etapa (`PitchShifter.h`).

### Tempo

`resolveMs` usa el BPM del host si el playhead lo ha dado **alguna vez** desde
`prepareToPlay`; si no, el parámetro `tempo` (40–300, default 120). Es
pegajoso a propósito: un host que solo informe mientras reproduce no debe
hacer que la longitud salte al manual al parar.

En JUCE solo el dispositivo de iOS expone un playhead, así que en el
Standalone de Windows `getPlayHead()` es `nullptr` y antes `Sync` quedaba
clavado a 120 sin avisar. Ahora el control Tempo solo es visible cuando no hay
host (`visHostTempo`), y el visualizador dice "host" o "manual".

`tempo` es un parámetro de **sesión**, no de sonido: `PresetManager` lo
excluye de `resetToDefaults()` y lo conserva al cargar un preset de usuario
(`isSessionParam`).

### Tamaño del buffer

`4×maxLength` (antes `3×`): con `rate` hasta 2 y la corrección de reenganche,
el span máximo llega a 3.75×L.

### Ondulación al mover Length (deuda conocida)

Reenganche de 2–3 granos con leve modulación de amplitud. Inaudible en material
normal, perceptible en un seno puro sostenido.

---

## Estabilidad del lazo de realimentación

El reverse y el delay tienen topes muy distintos: **0.60** el reverse
(`ReverseDelay::maxFeedback`), **0.95** el delay (`StereoDelay::maxFeedback`).
No es arbitrario.

En el **delay** cada muestra se lee una sola vez y sin ventana, así que la
ganancia del lazo es exactamente `fb`. Con `fb<1` es incondicionalmente
estable.

En el **reverse** no. El lazo tiene ganancia mayor que 1 por dos razones que se
suman:

1. Cada muestra escrita se lee **dos veces**, una por cada grano.
2. La ventana sin/cos es de potencia constante, no de amplitud. Para contenido
   coherente —y una autooscilación lo es— `gA+gB` promedia ~1.27.

Barrido del reverse midiendo el nivel 30 s y 60 s después de cortar la entrada,
sobre todas las combinaciones de filtro, drive y longitud:

| feedback | a los 30 s | a los 60 s | |
|---|---|---|---|
| 0.50 | 0.0068 | 0.00004 | decae |
| **0.60** | 0.0674 | 0.0051 | **decae — límite elegido** |
| 0.65 | 0.1734 | 0.0417 | −12 dB / 30 s, demasiado lento |
| 0.70 | 0.3631 | 0.2312 | se sostiene indefinidamente |
| 1.00 | — | — | se queda en 3.14 de amplitud (**+10 dBFS**) |

Ese último caso es el que revienta un tweeter: no diverge a infinito porque
`tanh` lo acota, pero "acotado" a tres veces fondo de escala no consuela a
nadie.

**El clamp está dentro de `ReverseDelay::process`, no solo en el rango del
parámetro**, porque un host puede automatizar fuera de rango o cargar un preset
de otra versión. El parámetro `feedback` va de 0 a 100 % y se normaliza a 0..1
en el procesador; por encima del tope interno el knob no hace nada más.

### El tope depende del shimmer

Ver [Shimmer](#shimmer): el tope efectivo es `0.60 + 0.10·shimmer`
(`ReverseDelay::currentMaxFeedback()`).

### Sustain infinito

Si algún día quieres sustain infinito, la forma correcta **no** es subir el
tope, sino [Freeze](#freeze), que es seguro por construcción.

---

## Saturador

La fórmula es `tanh(d·x)/d`, con `d` en [1, 8] (`kMaxDrive`). Su ganancia
máxima es exactamente 1 sea cual sea `d`, lo que la hace segura dentro del
lazo.

La primera versión era `tanh(d·x)/tanh(d)`. Parece razonable —normaliza para
que el techo quede en 1— pero su ganancia para señales pequeñas es
`d/tanh(d)`, o sea **siempre mayor que 1**: +2.3 dB con el drive al mínimo y
hasta +21 dB al máximo. Un amplificador escondido dentro del lazo de
realimentación. A oído se habría manifestado como "el plugin a veces se pone a
pitar y no sé por qué".

Drive, Low Cut y High Cut actúan *dentro* del lazo, así que solo tocan las
repeticiones, no el primer pase. Es lo que hace que la cola se oscurezca estilo
cinta.

### Oversampling del saturador

`Saturator.h`. `tanh(8·x)` sobre 4 kHz genera armónicos hasta el 11º; a
44.1 kHz el 9º (36 kHz) se pliega a 8.1 kHz y el 7º a 16.1 kHz, a −17..−21 dB
de la fundamental, y vuelven a entrar al lazo. No se puede usar
`juce::dsp::Oversampling` (trabaja por bloques y esto es un lazo por muestra),
así que es un 2× manual: punto medio por Catmull-Rom, `tanh` en las dos
muestras, y el mismo núcleo como half-band de bajada. Cuatro multiplicaciones
extra por muestra y 3 muestras de retardo en el lazo.

Medido (`testSaturatorAliasing`): alias del 9º de −32 a −64 dB, del 7º de −26
a −39 dB, fundamental sin cambio (−0.01 dB). Los dos núcleos suman 1 y no
superan la unidad, así que el techo de estabilidad no cambia (el barrido lo
confirma).

### Low Cut de 12 dB

`hpsteep`: un segundo `OnePole` en cascada en el paso alto del lazo. Con el
mix alto los graves se acumulan repetición tras repetición y 6 dB/oct no los
apartaba: los presets shoegaze subían el corte a 150–250 Hz para compensar.
Medido: −23 dB en 100 Hz en la cola con corte a 300 Hz.

---

## Drive dinámico

`Drive Env` ata el drive a la envolvente de la señal seca, que el `Ducker` ya
calcula. Se evalúa **por muestra**, no por bloque: a 512 muestras la
granularidad sería de 10 ms y el drive llegaría tarde al ataque de la
pulsación, que es justo lo que se quiere seguir.

Esto es seguro gracias a la fórmula del saturador: como `tanh(d·x)/d` tiene
ganancia máxima 1 para cualquier `d`, modular el drive no afecta a la
estabilidad del lazo. Con la fórmula original (ganancia `d/tanh(d) > 1`),
atacar fuerte habría subido la ganancia del lazo y disparado la
autooscilación: el bug habría vuelto disfrazado de característica.

Único cuidado: el drive modulado se suaviza, porque cambiar la curva de
saturación de golpe produce saltos de nivel.

---

## Freeze

Freeze **no** es "feedback al infinito". Son dos cosas distintas y esa
diferencia es la que lo hace seguro.

Subir el feedback es realimentar: la salida vuelve a entrar al buffer y se
acumula. Por eso hay un techo medido y por encima el lazo se autooscila.

Freeze es dejar de escribir **y** dejar de avanzar `writePos`. No hay
acumulación posible: cada grano nuevo captura el mismo `start` y cicla las
mismas L muestras. Medido en la suite, el nivel se sostiene con una deriva de
**0.000 dB tras 18 segundos**. Ni decae ni crece.

Sale sin clicks por construcción: los granos conservan su ventana, que sigue
naciendo y muriendo en amplitud 0, así que congelar a mitad de grano es limpio.
Los granos en vuelo terminan de leer contenido real y el siguiente ya arranca
sobre el buffer congelado.

Con Freeze activo, `Length` y `Feedback` se desactivan en la GUI porque el
buffer ya no se escribe.

### Soltar Freeze: costura y release

**La costura.** Al soltar, lo que se escribe (`input + realimentación`) no es
la continuación del material congelado, y un grano que arranque después y lea
hacia atrás cruza esa costura con la ventana a plena ganancia. Medido con un
seno de 0.5: saltos de 0.37–0.46 según la fase. Los primeros 10 ms se escriben
en crossfade desde un **espejo** del material congelado (la muestra anterior a
la costura, luego la anterior a esa…), que es continuo en la costura por
construcción. Con eso: 0.02, la derivada natural del seno.

Dos trampas al medirlo, documentadas para no repetirlas: con ruido blanco la
diferencia entre muestras consecutivas es ruido (±1.2) y el click no se ve;
y con 220 Hz y 1 s exacto de material la costura caía siempre en un cruce por
cero (11 ciclos justos en 50 ms). El test usa 223 Hz y una duración no
redonda.

**El release** (`freezerel`, 0–2 s, default 400 ms). Al soltar, el lazo se
realimenta con la ganancia *por vuelta* que hace que lo congelado caiga 60 dB
en T: `10^(−3·L/T)`, constante durante el release. La primera versión usaba
una envolvente que decaía, pero se aplica en cada vuelta y la caída resultaba
cuadrática: en medio segundo no quedaba nada. Esa ganancia puede superar el
techo de 0.60; es seguro porque es temporal y `tanh` acota. Medido: −11.7 dB a
los 0.4 s con T = 1.5 s (la curva teórica da −16), −inf a los 3 s.

---

## Tape: wow, flutter y detune

`TapeMod.h` genera un offset de lectura que se suma a `modOffset` del reverse.

| | Rango | Profundidad máxima |
|---|---|---|
| Wow | 0.5–0.8 Hz | ±45 cents |
| Flutter | 7–12 Hz | ±14 cents |
| Detune | constante | ±50 cents, **opuesto** en L y R |

Dos decisiones que no son obvias:

**Un solo modulador para todos los canales y los dos granos.** Una cinta real
tiene un único transporte. Modular cada canal (o cada grano) por separado
sonaría a chorus, no a cinta, y si se modulan los dos granos con LFOs
distintos el crossfade suma dos copias con afinaciones distintas y produce
filtrado en peine. El ensanchado estéreo lo hace `Detune`, que es otra cosa
distinta.

**La profundidad se especifica en cents, no en muestras.** La desviación de
afinación que produce modular un retardo depende de la *derivada* del
desplazamiento, no de su amplitud: un LFO lento necesita mucha más profundidad
que uno rápido para desafinar lo mismo. Especificándolo en cents, "Wow al 50 %"
desafina igual a 44.1 que a 96 kHz.

### El bug de la lectura adelantada

Ver [Bug 4](#bug-4-modulación-positiva-que-adelantaba-la-lectura).

---

## Shimmer

`PitchShifter.h`. Parte de la realimentación pasa por un shifter granular
independiente, con su propio grano de **60 ms** (por eso no depende de
`Length`); cada pasada del lazo sube (o baja) `shimpitch` semitonos.

### Hallazgo 1: la ventana del reverse no vale aquí

La primera versión usaba la misma ventana sin/cos que el motor de reverse. Es
lo correcto allí, donde los dos granos leen regiones distintas y
decorrelacionadas. En el shifter es al revés: leen el **mismo** material con un
desfase fijo, así que se peinan. Medido: con +12 semitonos sobre un seno de 220
Hz, la portadora de 440 Hz quedaba **suprimida** y solo salían dos bandas
laterales a 420 y 453 Hz. Sonaba a modulación en anillo, no a octava.

La solución es un crossfade **corto** (15 %) y de amplitud constante: la mayor
parte del grano suena una sola cabeza limpia. Con eso la portadora vuelve a
dominar y las bandas laterales bajan de 0.40 a 0.20 relativo.

### Hallazgo 2: un límite, no un bug

Queda un error de afinación de ±20–25 cents que **depende de la frecuencia de
entrada** — es la interferencia residual del crossfade, inherente al método
granular. Los shifters transparentes usan análisis de fase (phase vocoder), que
cuesta órdenes de magnitud más CPU y no cabe dentro de un lazo de
realimentación. Para shimmer no molesta, pero no sirve como armonizador afinado.

### Hallazgo 3: el shimmer estabiliza el lazo

Contraintuitivo pero medido: el pitch shifter empuja la energía hacia arriba en
cada pasada, y arriba se escapa (el paso bajo la atenúa y lo que supera Nyquist
desaparece). Sin shimmer, la energía se queda dando vueltas.

Barrido con el tope interno abierto, nivel 30 s tras cortar la señal:

| shimmer | seguro hasta |
|---|---|
| 0.00 | fb 0.60 (0.70 ya sostiene 0.139) |
| 0.50 | fb 0.80 |
| 0.80 | fb 0.80 |
| 1.00 | fb 0.70 (0.80 sostiene 0.045) |

La relación no es monótona, así que el techo usa la regla lineal conservadora
`0.60 + 0.10·shimmer`, que queda por debajo del límite medido en **todos** los
puntos. Un límite ajustado a la curva real sería más permisivo pero frágil ante
cualquier cambio futuro del lazo.

Nota: el shimmer canónico (Eventide) es pitch shift dentro del lazo del
*reverb*. No hay acceso a las tripas de `juce::Reverb`, así que aquí va en el
lazo del reverse. Funciona, pero suena distinto.

---

## Delay estéreo

`StereoDelay.h`. Lectura con interpolación lineal, tope de feedback 0.95
(incondicionalmente estable, ver arriba).

La saturación del lazo es `tanh(f)` fija (ganancia máxima 1); a diferencia
del reverse, no hay drive ajustable.

**Balance en Paralelo** (`parbal`): `cos θ / sin θ` con θ = (bal+1)·π/4. En el
centro 0.707 + 0.707, que era el 0.7 fijo de antes: dos señales
decorrelacionadas suman en potencia.

**Ping-Pong** cruza la realimentación entre canales. No afecta a la
estabilidad: el viaje L→R→L tiene ganancia `fb²`, menos que el lazo directo.

**Mod / Mod Rate** modulan el tiempo de lectura hasta 8 ms. L y R van en
**cuadratura** (90° de desfase), que es lo que abre la imagen estéreo — la
mitad de la gracia de un chorus.

---

## Ducker

`Ducker.h` calcula una envolvente de la señal seca (ataque 5 ms, release
ajustable) y la expone de dos formas:

- **`follow()`** — escala **absoluta**: `min(1, env·3)`, o sea que −9.5 dBFS
  ya es "a tope". La usa Drive Env, donde la dinámica real de la pulsación es
  lo que se quiere seguir.
- **`process()`** — escala **relativa** al pico reciente de la entrada
  (release ~2 s, suelo en −26 dBFS). La usa el ducking. Antes usaba la
  absoluta, y "Duck 100 %" atenuaba todo con una señal a −9 dBFS pero solo un
  tercio con una DI a −20 dBFS. Ahora depende de *cuándo* tocas, no de cuánto
  nivel entra. Hay un test que lo comprueba a −20 dBFS y otro que verifica
  que el ruido a −46 dBFS no dispara el ducking.

---

## NaN en los lazos

Cada lazo (reverse, delay, shifter) hace `isfinite` sobre lo único que escribe
al buffer y lo pone a 0 si no lo es. Un NaN que entre una sola vez —del host,
de un filtro degenerado— se quedaría dando vueltas para siempre, y Freeze lo
conservaría. Hay un test que inyecta un NaN y comprueba que un segundo después
la salida es finita.

---

## Reverb de placa

`PlateReverb.h`. Dattorro, *Effect Design Part 1* (JAES 1997). Sustituye a
`juce::Reverb` (Freeverb), que sin modulación interna sonaba metálico en
colas largas, no tenía pre-delay y no se podía abrir para meter un shifter.

### Estructura

`entrada → pre-delay → paso bajo (12 kHz) → 4 allpass de difusión → tanque`.
El tanque es un "8": dos mitades que se realimentan cruzadas, cada una
`allpass modulado → delay → damping → ×decay → [shimmer] → allpass → delay`.
Las salidas L/R son sumas de 7 taps con signos alternos repartidos por las
cuatro líneas y los dos allpass (los del paper): correlación L/R de la cola
medida 0.017.

Longitudes del paper (a 29 761 Hz) escaladas a la frecuencia de muestreo en
`prepare()`, taps y excursión incluidos. Test: mismo nivel a los 3 s a 44.1 y
96 kHz (1.2 dB con señal de banda limitada).

### Estéreo

El original suma L+R a mono. Aquí L entra en la mitad izquierda y R en la
derecha: con entrada mono es idéntico; con Detune o ping-pong la cola conserva
la anchura de la entrada.

### Mapeos

| Parámetro | Mapeo |
|---|---|
| `Size` | `decay = 0.25 + 0.65·size^1.6` → RT60 ≈ 1.3 s / 3.5 s / 17 s a 0 / 0.7 / 1 |
| `Damp` | frecuencia de corte 20 kHz → 1 kHz (log), no coeficiente: igual a cualquier sr |
| `Pre-Delay` | 0–200 ms, con glide de 60 ms (cambiarlo barre, no chasca) |
| `Mod` | excursión de los allpass modulados, hasta 32 muestras @29.7k, dos LFOs a 0.93 / 1.07 Hz |
| `Rev Shimmer` | mezcla del shifter en el tanque; `Shim Pitch` compartido con el reverse |

### Shimmer canónico

Un `PitchShifter` (60 ms de grano) por mitad, entre `×decay` y el segundo
allpass. Su salida pasa por **cuatro polos a 5 kHz** antes de mezclarse.
Medido con Goertzel por bandas de octava sobre la cola de un seno de 220 Hz,
2 s después de cortar:

| shimmer | 220 | 440 | 880 | 1.7k | 3.5k | 7k | 14k |
|---|---|---|---|---|---|---|---|
| 0 % | 100 % | | | | | | |
| 50 % | 4 % | 10 % | 78 % | 7 % | 1 % | | |
| 100 % | 1 % | 1 % | 1 % | 2 % | 61 % | 34 % | 0 % |

Sin el filtro, al 100 % la cola acababa como siseo a 14 kHz; con un polo a
7 kHz seguía en 12.7 kHz; con dos a 6 kHz se apilaba en la esquina (56 % en
7 kHz). Cuatro polos a 5 kHz la dejan en 3–5 kHz. Al 100 % *toda* la energía
sube cada vuelta (no queda nada en la fundamental); el sonido "acorde de
octavas" está en 30–50 %.

Nota importante para medir shifters granulares: el error de afinación (±20
cents) a 880 Hz son ±12 Hz, y un Goertzel de 1 s (1 Hz de bin) no lo ve. Hay
que integrar por banda (±7 %). El primer test con bin exacto decía que el 880
no existía.

### Nivel

`kOutGain = 0.6 × 0.85`. El 0.6 es del paper; el 0.85 iguala la energía total
con `juce::Reverb` al mismo porcentaje (medido con un burst de ruido en 9
combinaciones de size/damp con un programa temporal que enlazaba JUCE; la
placa salía un 10–20 % más fuerte). La cola queda ~1.5× más presente que en
Freeverb al mismo Size: es una placa.

### Estabilidad

Barrido size {0.7, 1} × damp {0, 0.8} × mod {0, 1} × shimmer {0, 100 % ±12,
50 % +7}: peor pico 30 s después de cortar la entrada, 0.0006 (−65 dB). Los
allpass son de módulo 1, el damping ≤ 1 y decay ≤ 0.9, así que no puede
autooscilar; el shimmer con crossfade de amplitud constante tampoco añade
ganancia.

### Integración

La placa devuelve solo señal húmeda; el procesador mezcla
`in·(1−amt) + placa·amt`. En mono se alimenta L=R y se promedia la salida.
`Size`, `Damp`, `Mod` y `Shimmer` se suavizan a ritmo de bloque; el pre-delay
lleva su propio glide. Sigue el reset al reactivar (`reverbActive`).

Parámetros nuevos: `revpre`, `revmod` (default 20 %), `revshim`. Los proyectos
guardados con Freeverb los cogen por defecto al cargar.

### Logo

`assets/logo-mark.png` (512×512 RGBA) es el vórtice solo; va incrustado en el
binario con `juce_add_binary_data(ReverseVerbAssets)` y se pinta a 40 px en la
cabecera (`BackPanel::paint`, decodificado una vez con `ImageCache`). Se
obtuvo del SVG de Canva (que en realidad envuelve PNGs sobre blanco): alfa por
distancia al blanco (`1 − min(r,g,b)/255`), des-premultiplicado, más un halo
difuminado y un núcleo cian que el fondo blanco se había comido. El script
está en el historial de la sesión, no en el repo: si el logo cambia, basta con
reemplazar el PNG. `assets/logo.png` es el banner del README (mark + Bahnschrift). El mismo PNG
es el icono del Standalone (`ICON_BIG`/`ICON_SMALL` en `juce_add_plugin`;
juceaide genera el `.ico`). El VST3 no lleva icono en Windows.

Lección: la imagen se guarda como **miembro** de `BackPanel`, decodificada con
`ImageFileFormat::loadFrom`, no como `static` apoyado en `ImageCache`. Con el
static, pluginval daba SUCCESS y el proceso nunca terminaba: el estático se
destruía después del `MessageManager` y `ImageCache` (que tiene un timer) se
quedaba esperando.

### Idioma de la interfaz

`Localisation.h`. El idioma **fuente** del código es el inglés: todo texto
visible va en inglés envuelto en `TRANS()`. Una tabla `juce::LocalisedStrings`
al español se activa en `initLocalisation()` (llamada desde el constructor del
procesador, antes de que exista el editor) si `SystemStats::getUserLanguage()`
empieza por `es`. `REVERSEVERB_LANG=en|es` lo fuerza para probar sin cambiar
el idioma de la máquina.

No se traducen: los nombres de parámetros que ve el host (los DAW no
localizan), los nombres de presets (son nombres propios y viajan en el
estado), ni los nombres de controles (Length, Feedback…: vocabulario técnico
universal). `routingNames()` devuelve inglés ("Parallel") y la GUI lo pasa por
`TRANS()` al mostrarlo.

Añadir un idioma: otra tabla y otra rama en `initLocalisation()`. Cuidado con
`)"` dentro de la tabla: el raw string usa delimitador `i18n(...)i18n`.

### GUI: Simple y Completo

Dos layouts en el mismo editor, conmutados por un segmented control en la
cabecera. **Simple**: ocho knobs de 140×146 (círculo de ~120 px, el doble
que en Completo) en dos filas de cuatro (`Length`,
`Feedback`, `Drive`, `Mix` / `Reverb`, `Size`, `Rev Shimmer`, `Duck`), la fila
de Sync/Freeze/ruteo/tempo, medidores pequeños y una **tira de resumen**
(`SummaryStrip`) con una línea por sección que lista solo lo que difiere del
default ("por defecto" si nada). Sin esa tira un modo simple engaña: cargas
*Cinta muerta*, ves ocho knobs normales y no sabes por qué suena a casete.
Un clic en la tira pasa a Completo.

- Es una preferencia de **interfaz**, no de sonido: no va en el estado del
  proyecto ni se automatiza. Se guarda en `%APPDATA%\ReverseVerb\ReverseVerb.ui`
  (archivo propio para no chocar con el `.settings` del Standalone). Default
  para un usuario nuevo: Simple.
- Conmutar no toca ningún parámetro. Lo oculto se hace `setVisible(false)`;
  los attachments siguen vivos.
- `setUiMode()` cambia la relación de aspecto del constrainer y hace un
  `setSize` al alto del modo, reajustado a la pantalla si no cabe
  (`fitForDisplay()`, el mismo cálculo que al abrir). Para el host es un
  resize normal.
- `BackPanel::simple` cambia cabeceras y el gradiente usa `getHeight()`.
- Las constantes `lay_H`/`lay_HSimple` del header son espejo de `lay::` del
  .cpp; un `static_assert` vigila que coincidan.

### GUI: layout del modo Completo

SPACE pasó a tener fila propia (6 knobs) y OUTPUT se quedó con Mix/Duck/Duck
Rel y los medidores en las tres columnas libres. El diseño mide 780×884 y la
ventana **arranca escalada** al área útil de la pantalla principal (un
portátil 1080p al 125 % tiene 816 px lógicos); la relación de aspecto es fija
y el usuario puede redimensionar desde la esquina.

---

## Configuración de buses

Se aceptan **mono→mono, estéreo→estéreo y mono→estéreo**. Este último es el
caso real de una cadena de guitarra: DI mono, ampli mono, y a partir de aquí
estéreo para que el ping-pong y la anchura del reverb tengan dónde actuar. La
entrada se duplica a los dos canales *antes* de procesar.

No se admite **estéreo→mono**: se perdería el ping-pong en la suma, y el host
sabe hacer esa mezcla mejor que el plugin.

En una pista mono el botón de Ping-Pong se deshabilita y pasa a decir
"Ping-Pong (mono)", y el visualizador muestra `MONO`. Un botón que se puede
pulsar y no hace nada es peor que un botón apagado.

---

## Presets y estado

### Índice de programa dentro del estado

Muchos hosts llaman a `setCurrentProgram` **después** de restaurar el estado
del proyecto. Si el plugin aplicase el preset sin mirar, cada vez que
reabrieras la canción te machacaría los ajustes guardados.

La solución: el índice de programa viaja dentro del estado, y
`setCurrentProgram` hace cortocircuito si el índice no ha cambiado de verdad.
Es el motivo por el que la lista de fábrica tiene que ser de tamaño fijo, y por
el que los presets de usuario viven en una lista aparte.

### Regla: los presets nuevos van SIEMPRE al final del array

El host guarda el índice dentro del proyecto. Insertar uno en medio desplazaría
todos los siguientes y las sesiones ya guardadas cargarían un preset distinto
al que el usuario eligió. Agrupar por estilo queda más bonito en el código; no
romper proyectos ajenos importa más. Por eso los tres de shoegaze están detrás
de los "Ref:" aunque conceptualmente irían antes, y los seis de tape/shimmer
al final.

### Presets de usuario

XML en `%APPDATA%\ReverseVerb\Presets\*.rvpreset`. Solo se pueden borrar los
del usuario: los de fábrica viven en el binario y el índice de programa que el
host guarda depende de que la lista no cambie.

### Marca de "modificado"

`PresetManager` escucha todos los parámetros. Cualquier cambio que no venga de
una carga de preset pone `dirty`; el editor lo lee a 4 Hz y, si está sucio,
deselecciona el combo y muestra el nombre con asterisco. También sincroniza el
nombre si el host cambia de programa desde su propio menú o se carga un
proyecto, que antes se quedaba con el texto viejo.

### Versión del estado

`getStateInformation` escribe `version="1"`. Hoy no se lee para nada (APVTS
rellena los parámetros que falten con el default); existe para el día que
cambie el rango de un parámetro y haya que migrar el valor guardado.

### `SnappedBoolParameter`

Todos los parámetros booleanos usan esta subclase en lugar de
`AudioParameterBool`. Ver [Bug 3](#bug-3-booleanos-que-no-se-restauraban-pluginval)
para el motivo.

---

## MIDI

`MidiControl.h/.cpp`. `NEEDS_MIDI_INPUT TRUE` y `acceptsMidi()` devuelve
true; no se produce MIDI.

### Nada se aplica en el hilo de audio

`setValueNotifyingHost()` avisa al host y en VST3 eso (`performEdit`) tiene
que salir del hilo de interfaz. Así que `processBlock` solo hace
`midiControl.push(midi, sampleClock)`: filtra CC y Program Change, los mete
en un `AbstractFifo` de 512 con su posición en muestras, y dispara un
`AsyncUpdater`. Toda la lógica —tabla de mapeos, learn, modos, tap tempo,
program change— corre en `handleAsyncUpdate()` en el hilo de mensajes. La
latencia extra es la del bucle de mensajes (unos ms), indistinguible con un
pedal.

Consecuencia cómoda: la tabla es un `std::map<int cc, Mapping>` que solo toca
un hilo. Sin atómicos.

El push va **antes** del troceado de bloques grandes, y los trozos reciben un
`MidiBuffer` vacío para no encolar lo mismo varias veces. `sampleClock`
avanza al final del bloque real.

### Mapeos y modos

Un destino (paramID, o `"@tap"` para el tap tempo) tiene como mucho un CC;
al aprender uno nuevo se borra el anterior. Al aprender **no** se aplica el
valor del CC que llega.

| Modo | Para | Comportamiento |
|---|---|---|
| `continuous` | float y choice | `valor / 127` normalizado |
| `momentary` | bool (default) | `>= 64` → on, si no off. Pedal de sustain = pisar-para-congelar |
| `toggle` | bool | cada flanco de subida invierte |

Cada aplicación va entre `beginChangeGesture` / `endChangeGesture` para que
el host la pueda grabar como automatización.

### Tap tempo

Se mide en **muestras** (contador del procesador + offset del evento en el
bloque). Medirlo al recibir en el hilo de mensajes metería el jitter del
bucle, que a 120 BPM es un 1–2 % de error. Media de los últimos 4 intervalos;
más de 2 s sin tap reinicia. Escribe el parámetro `tempo`, así que solo tiene
efecto sin host.

### Estado

Los mapeos van en un hijo `<MIDI><CC number target mode/></MIDI>` del XML de
estado. `setStateInformation` lo lee y lo **quita** antes de `replaceState`,
para que APVTS no lo arrastre en su árbol.

### GUI

`MidiSlider` y `MidiToggle` (en `PluginEditor.h`) interceptan el clic derecho
y llaman a `onRightClick` en vez de arrastrar/conmutar: un `Slider` sin menú
propio empieza un arrastre con el botón derecho y un `Button` dispara el clic
al soltar. El menú es `showMidiMenu(target)`; las etiquetas llevan el sufijo
` · CC n` o ` · learn...` que refresca `syncMidiLabels()` a 4 Hz.

---

## Interfaz y coste de CPU

Solo repinta el visualizador, a 30 Hz; el fondo y los controles son estáticos y
solo se redibujan al redimensionar. Una GUI de plugin que repinta mucho compite
con el hilo de audio, y en máquinas justas se manifiesta como cortes que
parecen del buffer pero no lo son.

El visualizador es diagnóstico, no decoración: el brillo de las dos partículas
es literalmente `sin(pi·fase/L)`, la ganancia real de la ventana de cada
grano. Se ven alternar y **cruzarse a media intensidad**; si algún día dejan de
hacerlo, el bug se ve antes de oírse.

Los medidores van de −48 a +6 dB, marca en 0 dBFS, tramo superior en ámbar.

---

## Bugs encontrados y lecciones

### Bug 1: ganancia oculta en el saturador

`tanh(d·x)/tanh(d)` tiene ganancia `d/tanh(d) > 1` para señales pequeñas: de
+2.3 a +21 dB dentro del lazo. Corregido a `tanh(d·x)/d`. Detalle en
[Saturador](#saturador).

### Bug 2: autooscilación del lazo del reverse

Con feedback alto, el lazo sostenía +10 dBFS 30 s después de cortar la señal.
Causa: dos lecturas por muestra y ventana de potencia constante (ganancia
efectiva ~1.27·fb). Corregido con el tope de 0.60 medido empíricamente.
Detalle en [Estabilidad](#estabilidad-del-lazo-de-realimentación).

### Bug 3: booleanos que no se restauraban (pluginval)

19 secciones pasaron a la primera —audio a 44.1/48/96 kHz con bloques de 64 a
1024, automatización, seguridad entre hilos, fuzzing de parámetros— y falló
una: tres parámetros booleanos no se restauraban al recargar el estado.

La causa es una asimetría de JUCE que no se ve de ninguna otra forma.
`AudioParameterBool` guarda internamente el valor normalizado **crudo**:

```cpp
void  setValue (float v) { value = v; ... }   // guarda 0.1268 tal cual
float getValue() const   { return value; }    // devuelve 0.1268
bool  get() const        { return value >= 0.5f; }   // devuelve false
```

APVTS, en cambio, guarda en su ValueTree el valor ya **cuantizado** (0 o 1). Al
restaurar, compara el valor cuantizado con el que ya tiene, ve que ambos son 0,
concluye que no hay cambio y no llama a `setValue`. El float crudo se queda
rancio.

**El plugin sonaba perfectamente**, porque `get()` siempre devolvía lo correcto.
Lo que fallaba era que el host leía un valor distinto del que había guardado,
lo que rompe la comparación de estados y puede confundir al undo o al A/B del
DAW.

Se arregla con una subclase de tres líneas, pero **no** cuantizando en
`setValue`: en JUCE ese método es **privado**. Sobrescribir un virtual privado
es legal en C++ (es la base del idiom NVI), pero llamar a la implementación del
padre desde la derivada no lo es — y hace falta llamarla para guardar el valor.

Lo que sí funciona es sobrescribir **`getValue()`**, que es igualmente virtual
y solo necesita `get()`, que es público:

```cpp
class SnappedBoolParameter : public juce::AudioParameterBool
{
public:
    using juce::AudioParameterBool::AudioParameterBool;
private:
    float getValue() const override { return get() ? 1.0f : 0.0f; }
};
```

Hacia fuera el parámetro reporta siempre 0 o 1 exactos, que es justo lo que
APVTS guarda. El viaje de ida y vuelta cuadra.

Dos cosas que vale la pena retener:

- La suite de DSP **no puede** encontrar esto. Es un problema de contrato con
  el host, no de algoritmo. Por eso hacen falta las dos capas.
- pluginval marcó 3 de los 4 booleanos. El cuarto (Ping-Pong) se salvó por
  azar del valor aleatorio que le tocó, pero tenía el mismo bug. **Un fallo de
  validación casi nunca está solo**: conviene buscar el patrón, no parchear los
  casos concretos que salieron en rojo.

### Bug 4: modulación positiva que adelantaba la lectura

Al implementar wow y flutter apareció algo que ninguno de los tests con
profundidades realistas detectaba: **una modulación positiva adelantaba la
lectura al punto de arranque del grano**, donde la memoria todavía no se ha
escrito en este ciclo y contiene audio de hace varios segundos.

Con +400 muestras de offset, el salto máximo era **4.1 veces el techo teórico**,
siempre en la fase < offset del grano. Con las profundidades reales de wow el
artefacto quedaba enmascarado porque la ventana vale poco en esa zona, así que
solo se habría manifestado como "a veces suena sucio".

Se arregla clampeando el offset a `phase * rate`: la modulación entra
progresivamente desde el arranque del grano, justo donde la ventana vale casi 0.

Hay un test dedicado que fuerza el caso extremo. Lección: probar solo con
valores realistas deja huecos, porque los artefactos aparecen antes en los
extremos y desde ahí se cuelan hacia el rango normal.

### Bug 5: la ventana del reverse en el shifter

Ver [Shimmer, hallazgo 1](#hallazgo-1-la-ventana-del-reverse-no-vale-aquí).
Ventana equal-power sobre material correlacionado → supresión de portadora.

---

## Deuda técnica y pendientes

Ver [BACKLOG.md](BACKLOG.md). Resumen:

- Ondulación de amplitud al mover `Length` (inaudible salvo con seno puro).
- Builds de macOS/Linux: las hace el CI, pero no se han probado de oído en
  un DAW real fuera de Windows.
- Anchura estéreo adicional en el reverse (desfasar granos entre canales),
  parcialmente cubierta por `Detune`.
