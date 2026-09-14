# TESTING — lo que hay que oír, medir y probar

Todo lo que se hizo en septiembre de 2026 está cubierto por tests automáticos
donde se puede automatizar (53 comprobaciones entre las dos suites, más
pluginval). Este documento es **lo que no**: lo que necesita oídos, un DAW
real, un pedal MIDI o una máquina que no sea esta.

Cada punto lleva: **cómo** se prueba, **qué** se espera, y **qué hacer si
falla**. Marca la casilla cuando esté hecho. Si algo suena mal, anota preset,
ajustes y qué oíste; con eso se reproduce.

Antes de empezar:

```bat
tools\run_tests.bat          → 36 + 17 en verde
tools\run_pluginval.bat 8    → VALIDACION SUPERADA
```

Si eso no está en verde, no sigas: primero se arregla.

---

## Índice

1. [Lo que cambia el sonido de todo](#1-lo-que-cambia-el-sonido-de-todo)
2. [Reverb de placa y shimmer](#2-reverb-de-placa-y-shimmer)
3. [Reverse: freeze, saturador, low cut](#3-reverse-freeze-saturador-low-cut)
4. [Duck, bypass y mezcla](#4-duck-bypass-y-mezcla)
5. [MIDI](#5-midi)
6. [Presets](#6-presets)
7. [Interfaz](#7-interfaz)
8. [Tempo y sincronía](#8-tempo-y-sincronía)
9. [Robustez en el DAW](#9-robustez-en-el-daw)
10. [Otras plataformas](#10-otras-plataformas)
11. [Medidas objetivas que faltan](#11-medidas-objetivas-que-faltan)
12. [Registro de resultados](#12-registro-de-resultados)

---

## 1. Lo que cambia el sonido de todo

Tres cambios afectan a **todos** los presets. Van primero porque si alguno
está mal, el resto de pruebas no vale.

### 1.1 El reverb es otro motor

- [ ] **Cómo:** carga *Swell clásico*, *Ref: Guthrie Wash* y *Shoe: Souvlaki*
  (los tres con reverb alto). Toca acordes sueltos y frases.
- **Esperado:** cola más larga y más lisa que antes al mismo `Size`; nada
  metálico ni "ping" de modos en la cola larga. El nivel global parecido al
  de antes (se calibró la energía total; la cola queda ~1.5× más presente).
- **Si falla:** si la cola es *demasiado* larga en un preset, bajar `Size` en
  ese preset (ver §6). Si suena metálica, subir `Mod` de 20 a 30–40 % y
  anotarlo: sería motivo para cambiar el default.

### 1.2 Interpolación cúbica

- [ ] **Cómo:** *Cinta muerta* (wow y flutter al límite) y *Shoe: Muro*
  (detune). Compara mentalmente con lo que recuerdas de antes.
- **Esperado:** wow/flutter/detune más limpios, menos "rasposos"; agudos que
  no se apagan al modular. Con todo a 0 no debe cambiar nada.
- **Si falla:** poco probable (está medido: 25× menos error). Si oyes algo
  raro, anota si es con modulación a 0 o no.

### 1.3 Duck adaptativo

- [ ] **Cómo:** *Ref: Bow & Swell* (Duck 75 %). Toca (a) con la guitarra a
  nivel normal, (b) con el volumen de la guitarra a la mitad, (c) con una
  ganancia de entrada +12 dB en la pista.
- **Esperado:** el ducking se comporta **igual** en los tres casos: la cola
  se aparta al tocar y crece en los huecos. Antes, en (b) casi no duckeaba.
- **Esperado también:** con ruido de fondo de la interfaz (sin tocar) el
  efecto NO se duckea (suelo en −26 dBFS).
- **Si falla:** si con la señal floja duckea poco, el suelo de −26 dBFS es
  alto para tu cadena: se baja en `Ducker.h` (`kFloor`). Si el ruido de fondo
  duckea, al revés.

---

## 2. Reverb de placa y shimmer

### 2.1 Pre-delay

- [ ] **Cómo:** *Placa de cristal*. `Pre-Delay` a 0, luego 60, luego 200 ms.
  Mueve el knob mientras suena una nota.
- **Esperado:** con 60–100 ms la nota se separa de su cola (más "definido").
  Al mover el knob en caliente, un ligero *sweep* de afinación (glide de
  60 ms), **nunca un click**.

### 2.2 Mod

- [ ] **Cómo:** `Size` al máximo, `Damp` 20 %, toca un acorde y deja la cola
  10 s. Repite con `Mod` a 0, 20, 50, 100 %.
- **Esperado:** a 0 la cola larga se vuelve metálica/estática; a 20 (default)
  lisa sin que se note movimiento; a 50 se empieza a oír como chorus lento;
  a 100 es un efecto en sí mismo (±15 cents), válido para ambient.
- **Decisión a tomar:** ¿20 % es el default correcto, o 30 %?

### 2.3 Shimmer del reverb (canónico)

- [ ] **Cómo:** *Placa de cristal* (Rev Shimmer 45 %, Shim Pitch +12). Toca
  una nota grave y deja 5 s. Luego prueba 25 %, 70 % y 100 %.
- **Esperado:** a 25–50 % un "acorde" de octavas apiladas sobre la cola, sin
  que la fundamental desaparezca. A 100 % todo sube cada vuelta y la cola se
  vuelve aire en 2–3 kHz, **sin siseo** (el filtro de 4 polos a 5 kHz). Con
  `Damp` alto, el shimmer se apaga antes: es lo esperado.
- [ ] **Shim Pitch −12:** octavas hacia abajo; debe sonar a órgano/sub, no a
  ruido.
- [ ] **Shim Pitch +7:** quintas apiladas; el "error granular" de ±20 cents
  aquí se nota más. Anota si es molesto: sería motivo para un shifter mejor.
- **Si falla:** si a 100 % hay siseo, el corte del filtro (5 kHz) es alto para
  tu gusto: se baja en `PlateReverb.h` (`shimLpCoef`). Si a 50 % suena
  apagado, al revés.

### 2.4 Los dos shimmers a la vez

- [ ] **Cómo:** *Catedral (shimmer)* (shimmer del reverse 75 %) y súbele `Rev
  Shimmer` a 40 %.
- **Esperado:** dos capas de octavas distintas (una sobre el swell invertido,
  otra sobre la cola). Puede ser demasiado; el punto es que no se peleen ni
  se disparen de nivel.
- **Decisión a tomar:** ¿merece un `Shim Pitch` separado para cada uno? (está
  en el BACKLOG).

### 2.5 Estéreo de la placa

- [ ] **Cómo:** pista **mono** (Reaper/Logic) o el Standalone con una entrada:
  `Reverb` 80 %, sin delay ni detune.
- **Esperado:** cola ancha aunque la entrada sea mono (los taps son
  decorrelados: 0.017 de correlación medida).
- [ ] **Con Detune** a ±30 cents en pista estéreo: la cola conserva el batido
  entre canales (L entra en una mitad del tanque y R en la otra).

### 2.6 Reverb "antes" (succión)

- [ ] **Cómo:** *Ref: Greenwood Reverse* (el único con Reverb Post apagado).
- **Esperado:** el carácter de succión sigue ahí con la placa; que no suene
  a barro con las colas más largas. Si embarra, bajar `Size` en ese preset.

---

## 3. Reverse: freeze, saturador, low cut

### 3.1 Soltar Freeze

- [ ] **Cómo:** *Drone (pulsa Freeze)*. Acorde → Freeze → 5 s → suelta.
  Repite 10 veces soltando en momentos distintos de la frase.
- **Esperado:** **ningún click** al soltar, en ninguna de las 10 (la costura
  está cubierta con crossfade; antes había saltos de 0.4). Con `Frz Rel` a
  400 ms el colchón se va con naturalidad; a 0 desaparece en una ventana; a
  2 s queda como una cola.
- [ ] **Con Length corto (100 ms) y Frz Rel 2 s:** la ganancia por vuelta se
  acerca a 1 durante 2 s. Debe apagarse igual, sin subir de nivel.
- **Si falla:** un click al soltar es un bug: anota `Length` y `Frz Rel`.

### 3.2 Saturador a 2×

- [ ] **Cómo:** `Drive` 8, `Drive Env` 0, `Feedback` 50 %, `High Cut` 20 kHz.
  Notas agudas (traste 12+ en las cuerdas finas), armónicos.
- **Esperado:** saturación limpia, sin el "brillo sucio" inarmónico que había
  antes con notas agudas. El carácter de la saturación (cuánto satura) no
  debe haber cambiado.
- [ ] **Drive Env 100 %:** atacar fuerte satura, tocar suave no. Igual que
  antes.

### 3.3 Low Cut de 12 dB

- [ ] **Cómo:** *Shoe: Muro*. Activa `LC 12 dB` y baja `Low Cut` de 250 a
  ~120 Hz.
- **Esperado:** el muro sigue denso pero con menos barro que con 6 dB a
  250 Hz. Es una alternativa, no una mejora automática: puede que prefieras
  el original.
- **Decisión a tomar:** ¿los presets shoegaze deberían usar 12 dB con corte
  más bajo? (§6).

### 3.4 Estabilidad del lazo con todo

- [ ] **Cómo:** `Feedback` 100 %, `Shimmer` (reverse) 100 %, `Drive` 8,
  `Wow` 100 %, `Flutter` 100 %, `Detune` ±50, `LC 12 dB` on. Toca fuerte 10 s
  y para. Espera 30 s.
- **Esperado:** la cola decae siempre; nunca se sostiene ni crece. Los
  medidores no pasan de 0 dBFS de forma sostenida.
- **Si falla:** es grave. Anota exactamente los ajustes; el barrido
  automático cubre las combinaciones principales, no todas.

---

## 4. Duck, bypass y mezcla

### 4.1 Bypass con cola

- [ ] **Cómo:** en Ableton, con una cola sonando, pulsa el botón de
  bypass del dispositivo. Luego vuelve a activarlo.
- **Esperado:** la seca sube a 0 dB (sin salto, rampa de 20 ms) y la cola
  **termina de sonar** en vez de cortarse. Al reactivar, sin click.
- [ ] **En Reaper** (que usa su propio bypass): lo mismo.

### 4.2 Trim de salida

- [ ] **Cómo:** `Output` a −12 dB y a +12 dB; mira los medidores.
- **Esperado:** el medidor OUT se mueve 12 dB; en bypass no actúa (la seca
  sale a 0 dB pase lo que pase).

### 4.3 Balance Rev/Dly

- [ ] **Cómo:** ruteo *Paralelo*, `Rev/Dly` de −100 a +100.
- **Esperado:** en −100 solo el reverse, en +100 solo el delay, en 0 los dos
  al nivel de antes (0.707). Sin salto de volumen al pasar por el centro.
  Con otro ruteo el control está gris.

### 4.4 Mix

- [ ] **Cómo:** automatiza `Mix` de 0 a 100 en 1 s con una cola sonando.
- **Esperado:** sin zipper ni escalones (SmoothedValue de 20 ms).

---

## 5. MIDI

Necesita un controlador. En Ableton: pista MIDI → *MIDI To* → pista de audio
→ *ReverseVerb*, pista armada. Los tests automáticos ya verifican la lógica
(learn, modos, PC, tap tempo); aquí se prueba el camino real.

- [ ] **Learn:** clic derecho en Freeze → MIDI Learn → pisa un pedal de
  sustain. La etiqueta pasa a `Freeze · CC 64`. **No** se congela al
  aprender.
- [ ] **Momentáneo:** pisar congela, soltar suelta (con `Frz Rel` 400 ms se
  oye el apagado). Latencia imperceptible (< 10 ms; va por el hilo de
  mensajes).
- [ ] **Toggle:** clic derecho → Toggle. Cada pisada invierte.
- [ ] **Pedal de expresión → Mix:** recorrido completo 0–100 %, sin saltos.
- [ ] **Program Change** desde una pedalera: cambia de preset; el combo lo
  refleja.
- [ ] **Tap tempo** en el Standalone (sin host): clic derecho en Tempo → MIDI
  Learn (tap tempo) → un botón. 4 pulsaciones → el tempo se fija; `Sync`
  sigue.
- [ ] **Guardar y reabrir el proyecto:** las asignaciones sobreviven.
- [ ] **CC en el hilo de audio a lo bestia:** mueve un knob del controlador
  como un loco 10 s. Sin cortes de audio ni cuelgues (el FIFO descarta si
  se llena; no bloquea).

---

## 6. Presets

Los 25 cargan y son idempotentes (test). Lo que falta es oírlos con la placa.

- [ ] **Uno por uno**, con la misma guitarra y nivel: ¿suena a lo que dice el
  nombre? ¿la cola es más larga de lo que el preset pretende?
- **Los sensibles:** *Ref: Guthrie Wash*, *Ref: Bow & Swell*, *Shoe: Souvlaki*
  (reverb 82 %), *Nube lenta*, *Catedral*. Son los que tienen `Size` alto y
  con la placa pueden pasar de "ambiente" a "mar".
- **Regla al reajustar:** editar el preset **en su sitio** en
  `PresetManager.cpp`; **nunca** reordenar ni insertar en medio. Si haces
  uno nuevo, al final.
- [ ] **Placa de cristal:** es el único que no existía antes; ¿está bien
  calibrado o es un punto de partida?
- [ ] Guardar un preset de usuario, cerrar el DAW, reabrir: está en la lista y
  carga igual. Borrarlo funciona.
- [ ] Cargar un **proyecto guardado antes de la placa**: los parámetros nuevos
  (`Pre-Delay` 0, `Mod` 20, `Rev Shimmer` 0, `Frz Rel` 400, `Output` 0,
  etc.) cogen sus defaults y el resto está como se guardó.

---

## 7. Interfaz

- [ ] **Idioma:** con Windows en español todo sale en español (botones,
  cabeceras, menús de clic derecho, diálogos de guardar/borrar, tira de
  resumen). Con `REVERSEVERB_LANG=en` en el entorno antes de abrir el DAW o
  el Standalone, todo en inglés. Los nombres de controles y de presets no
  cambian en ningún caso. Busca alguna cadena que se haya quedado en un
  idioma cuando el otro está activo: es un `TRANS()` que falta.
- [ ] **Modos Simple / Completo:** abre en Simple la primera vez. Conmuta
  varias veces con audio sonando: sin cortes, sin click, el sonido no cambia.
  La ventana cambia de alto y cabe en la pantalla en los dos modos. Cierra y
  reabre el plugin (y el DAW): recuerda el último modo.
- [ ] **Tira de resumen:** carga *Cinta muerta* en Simple: la línea Tape dice
  Wow y Flutter. Cambia un knob oculto desde Completo, vuelve a Simple: la
  línea lo refleja en < 1 s. Con todo por defecto dice "por defecto". Clic en
  la tira → Completo.
- [ ] **Los ocho de Simple son los correctos:** tras una semana de uso, ¿hay
  alguno que nunca tocas ahí, o alguno que echas de menos? (Candidatos:
  `Detune` en vez de `Shimmer`; el ruteo del delay sobra si nunca lo usas.)
- [ ] **Tamaño al abrir:** en tu pantalla (1536×864 lógicos) la ventana cabe
  entera al abrir, en el Standalone y en Ableton. Se puede agrandar desde la
  esquina hasta el límite de la pantalla.
- [ ] **Monitor externo** más grande: abre al tamaño de diseño (780×884).
- [ ] **Todos los controles nuevos** visibles y legibles al tamaño reducido:
  `LC 12 dB`, `Frz Rel`, `Rev/Dly`, `Pre-Delay`, `Mod`, `Shimmer` (SPACE),
  `Output`, medidores.
- [ ] **Combo de presets:** cambia de programa desde el menú del DAW → el
  combo lo refleja. Toca un knob → aparece ` *`. Carga un proyecto → nombre
  correcto sin asterisco.
- [ ] **Tempo:** en Ableton el control desaparece y arriba dice `host … BPM`.
  En el Standalone aparece y dice `manual`. (Con el audio parado en el DAW
  dirá `manual` porque no procesa: es correcto.)
- [ ] **Clic derecho** en cada knob y botón abre el menú MIDI y **no** mueve
  el control ni lo conmuta.
- [ ] **Redimensionar en caliente** con el audio sonando: sin cortes (solo
  repinta el visualizador).
- [ ] **CPU:** con la ventana abierta vs cerrada, con 4 instancias. Anota el
  % de CPU del DAW en cada caso. Si con la ventana abierta sube mucho, es el
  visualizador a 30 Hz.

---

## 8. Tempo y sincronía

- [ ] **Sync en Ableton:** `Sync` on, división 1/4. Cambia el tempo del
  proyecto de 120 a 90 mientras suena.
- **Esperado:** `Length` sigue al tempo; el visualizador muestra el nuevo
  valor; sin clicks (se aplica en la costura del grano). El delay hace un
  *sweep* de cinta al cambiar (glide de 60 ms), no un click.
- [ ] **Tempo pegajoso:** para el transporte de Ableton. `Length` **no**
  salta al tempo manual (se queda con el último del host).
- [ ] **Standalone:** `Tempo` manual a 90; `Sync` 1/8 → `Length` = 333 ms.
- [ ] **Tempo extremo:** 40 BPM con 1/1 → `Length` se clava en 2000 ms (el
  máximo), sin errores.

---

## 9. Robustez en el DAW

- [ ] **Ableton:** cargar, guardar, cerrar, reabrir 5 veces. Deshacer
  (Ctrl+Z) después de mover un knob: vuelve. Automatizar `Feedback`,
  `Reverb`, `Size`, `Freeze` (booleano) desde un clip: sin escalones
  audibles.
- [ ] **Congelar pista** (Freeze track) en Ableton: el render suena igual que
  en directo. (Manda bloques grandes: el troceado está probado, pero aquí es
  el caso real.)
- [ ] **Exportar** una pista con una cola de 15 s y `Size` al máximo: el
  render no corta la cola (tail de 20 s declarado). Con `Freeze` activo el
  host debería seguir renderizando (tail infinito): comprueba qué hace
  Ableton.
- [ ] **Cambiar la frecuencia de muestreo** del proyecto (44.1 → 48 → 96) con
  el plugin cargado: sin crash; suena igual (todo escala). Con `Freeze`
  activo al cambiar, queda en silencio hasta soltar: es conocido.
- [ ] **Tamaño de buffer** 32, 64, 256, 2048 en Ableton: sin diferencia de
  sonido (suavizados en segundos, no en bloques).
- [ ] **Pista mono** en Reaper: aviso `MONO`, ping-pong en gris, todo suena.
- [ ] **Dos instancias** en la misma pista, una en bypass: la de bypass deja
  pasar la seca limpia.
- [ ] **Abrir/cerrar el editor 50 veces** con el audio sonando: sin fugas ni
  cuelgues (pluginval lo hace cientos de veces, pero no en Ableton).

---

## 10. Otras plataformas

El CI compila y valida en macOS y Linux, pero **nadie lo ha oído** fuera de
Windows.

- [ ] **CI en verde** en los tres sistemas tras el push. Si macOS o Linux
  fallan al compilar, pegar el log: será un include que MSVC trae de rebote o
  un warning que clang trata distinto.
- [ ] **macOS:** descargar el artefacto, meter el `.vst3` en
  `~/Library/Audio/Plug-Ins/VST3`, abrir en Logic/Live/Reaper. Gatekeeper
  se quejará (no está firmado): botón derecho → Abrir. Que suene igual.
- [ ] **Linux:** artefacto → `~/.vst3`, abrir en Reaper o Bitwig. Que suene
  igual. El Standalone con ALSA/JACK.
- [ ] **Release:** `git tag v0.2.0 && git push origin v0.2.0` → aparece la
  release con tres zips. Descargar el de Windows y probar que el `.vst3`
  carga en un PC que **no** tenga Visual Studio (falta el runtime de VC++ si
  no carga: sería un cambio de CMake).

---

## 11. Medidas objetivas que faltan

Cosas que se pueden medir con un analizador (Voxengo SPAN, Plugin Doctor,
REW) y que la suite no cubre porque necesitan el plugin entero en un host.

| Medida | Cómo | Esperado |
|---|---|---|
| **Latencia real** | Plugin Doctor, impulso | 0 muestras reportadas (a propósito). El saturador mete 3 muestras dentro del lazo, no en la seca. |
| **Respuesta en frecuencia de la seca** con Mix 0 | Plugin Doctor, sweep | Plana ±0.1 dB. Cualquier desviación es un bug. |
| **Ruido de fondo** | Sin entrada, Reverb 100 %, Feedback 60 % | < −100 dBFS. Si hay ruido, un denormal se escapó. |
| **Aliasing con Drive 8** | Seno de 4 kHz, SPAN | El alias a 8.1 kHz por debajo de −60 dB (medido −64 en la suite; aquí con el lazo entero). |
| **Nivel de la placa vs Freeverb** | Si tienes una build antigua: mismo preset, mismo material, LUFS de la cola | ±1.5 dB en energía total. La cola de la placa ~1.5× más presente: esperado. |
| **RT60 de la placa** | Impulso, `Size` 0 / 0.7 / 1, REW o a mano | ~1.3 s / ~3.5 s / ~17 s. |
| **Ancho estéreo** | Correlómetro con entrada mono, Reverb 100 % | Correlación ~0 en la cola. |
| **CPU por instancia** | DAW, 44.1 kHz, buffer 256 | Anotar. No hay referencia previa; sirve para el futuro. |
| **Jitter del tap tempo** | 8 taps con metrónomo a 120 | ±1 BPM. Se mide en muestras, no en el hilo de mensajes. |

---

## 12. Registro de resultados

Copia esto por cada sesión de pruebas:

```
Fecha:
Build (commit):
DAW / versión / sample rate / buffer:
Interfaz de audio:

Secciones probadas:
Fallos (preset, ajustes, qué se oyó):
Decisiones tomadas (defaults, presets reajustados):
Pendiente:
```

Las **decisiones a tomar** que salen de este documento, para no perderlas:

1. Default de `Mod` del reverb: 20 o 30 %.
2. Corte del filtro del shimmer del reverb: 5 kHz o menos.
3. `Shim Pitch` separado para el reverb.
4. Presets shoegaze con `LC 12 dB` y corte más bajo.
5. Reajuste de `Size` en los presets sensibles.
6. Suelo del Duck (−26 dBFS) según tu cadena.
7. Los ocho controles del modo Simple.
