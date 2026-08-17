# ReverseVerb — reverse delay + delay + reverb (VST3, Windows)

Reverse delay en tiempo real con sync a tempo, delay ping-pong ruteable,
filtro y saturación en ambos lazos, reverb conmutable y ducking por la entrada.

## Build (Windows, Visual Studio)

Necesitas Visual Studio con "Desarrollo para escritorio con C++" (trae compilador
y CMake) y Git.

Abre el **Developer Command Prompt** de tu versión de VS y:

```bat
cd ReverseVerb
cmake -B build
cmake --build build --config Release
```

**Sin `-G`.** Desde el Developer Command Prompt, CMake detecta solo el Visual
Studio instalado. Si lo fuerzas a mano tienes que acertar con la versión
(`"Visual Studio 17 2022"`, `"Visual Studio 18 2026"`...) y es un error fácil de
cometer. Si necesitas forzarlo por algún motivo, añade también `-A x64`.

La primera configuración descarga JUCE (~500 MB, varios minutos). Las siguientes
son rápidas y reutilizan el generador del caché.

### Solo el Standalone

Para iterar rápido sin tocar la carpeta del sistema:

```bat
cmake --build build --config Release --target ReverseVerb_Standalone
```

Queda en `build\ReverseVerb_artefacts\Release\Standalone\ReverseVerb.exe`.
Es mucho mejor sitio para probar que el DAW: si el plugin peta, cierras la
ventana y ya. En un DAW puedes perder trabajo, y además algunos ponen en lista
negra los plugins que crashean.

## Cargarlo en Ableton

Compila el target del VST3:

```bat
cmake -B build
cmake --build build --config Release --target ReverseVerb_VST3
```

Queda en:

```
build\ReverseVerb_artefacts\Release\VST3\ReverseVerb.vst3
```

`COPY_PLUGIN_AFTER_BUILD` está en **FALSE** a propósito. Copiar a
`C:\Program Files\Common Files\VST3` exige permisos de administrador y hace
fallar la build entera por un paso que ni siquiera es de compilación. Para
desarrollar es mejor que el DAW lea directamente de la carpeta de build.

En Ableton:

1. **Preferencias** (`Ctrl+,`) → pestaña **Plug-Ins**
2. Activa **"Carpeta personalizada de plug-ins VST3"**
3. **Examinar** → selecciona
   `...\ReverseVerb\build\ReverseVerb_artefacts\Release\VST3`
   — la **carpeta**, no el archivo `.vst3`
4. **Volver a examinar**

A partir de ahí cada recompilación queda activa con un rescan, sin copiar nada
ni pedir permisos.

### Cierra Ableton antes de recompilar

Mientras Ableton tiene el plugin cargado mantiene el archivo abierto, y el
enlazador no podrá sobrescribirlo. El error es del tipo
`cannot open file ... ReverseVerb.vst3` y despista bastante, porque parece un
problema de compilación cuando en realidad es un bloqueo de archivo.

Basta con cerrar Ableton. Quitar el plugin de las pistas no siempre libera la
DLL.

### Mono y estéreo en Ableton

Ableton pasa siempre estéreo por la cadena de dispositivos, así que el aviso
`MONO` del visualizador no te aparecerá aunque la fuente sea una guitarra mono:
verá dos canales idénticos y el ping-pong funcionará. Ese aviso es para hosts
que sí manejan pistas mono de verdad, como Reaper o Logic, y para el Standalone
con una interfaz de una sola entrada.

## Testing

Hay dos capas, y son complementarias: una prueba el DSP, la otra prueba el
plugin como ciudadano de un DAW.

### Suite de DSP

```bat
tools\run_tests.bat            barrido rapido (~4 s)
tools\run_tests.bat --full     barrido exhaustivo (minutos)
```

Los motores (`ReverseDelay`, `StereoDelay`, `Ducker`, `OnePole`) son headers
puros sin JUCE **a propósito**, así que la suite compila y corre en segundos sin
arrastrar el framework ni instanciar un host. Es lo que la hace barata de
ejecutar, y una suite barata es una suite que se ejecuta.

Doce comprobaciones: que no hay clicks en la costura de los granos, que la
ventana da potencia constante, que de verdad invierte (correlación cruzada
contra la entrada invertida), que mover `Length` no produce saltos, que ambos
lazos de realimentación decaen aunque se pida feedback al máximo, que el
retardo es exacto al preparar, que el ping-pong alterna canales, que el ducker
atenúa y vuelve, que el filtro da −3 dB en su corte, y que nada produce NaN con
entradas de 1e6 o 1e−30.

**Por qué existe:** los tres peores bugs del proyecto no se detectan de oído.

- Un saturador con una ganancia oculta de hasta +21 dB dentro del lazo.
- Un lazo que se autooscilaba y sostenía +10 dBFS treinta segundos después de
  cortar la señal.
- Un click de 1.30 de amplitud al mover un slider.

Los tres se manifiestan como "a veces suena raro y no sé por qué", y lo natural
es culpar a la interfaz o al DAW antes que al algoritmo. Ejecuta la suite antes
de tocar cualquier cosa del lazo de realimentación.

### Validación del plugin

```bat
tools\run_pluginval.bat        nivel 8 (recomendado)
tools\run_pluginval.bat 10     nivel maximo
```

Descarga [pluginval](https://github.com/Tracktion/pluginval) la primera vez y
valida el VST3. Prueba lo que la suite de DSP no puede: bloques de tamaño
irregular, sample rates extremos, automatización desde otro hilo, abrir y cerrar
el editor cientos de veces, y llamadas fuera de orden que un DAW real hace
aunque la documentación diga que no.

Merece la pena antes de confiarle una sesión con trabajo dentro. Si Ableton pone
el plugin en lista negra por un crash, recuperarlo es más molesto que este paso.

#### El bug que encontró en la primera pasada

19 secciones pasaron a la primera —audio a 44.1/48/96 kHz con bloques de 64 a
1024, automatización, seguridad entre hilos, fuzzing de parámetros— y falló una:
tres parámetros booleanos no se restauraban al recargar el estado.

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
Lo que fallaba era que el host leía un valor distinto del que había guardado, lo
que rompe la comparación de estados y puede confundir al undo o al A/B del DAW.

Se arregla con una subclase de tres líneas, pero **no** cuantizando en
`setValue`: en JUCE ese método es **privado**. Sobrescribir un virtual privado
es legal en C++ (es la base del idiom NVI), pero llamar a la implementación del
padre desde la derivada no lo es — y hace falta llamarla para guardar el valor.

Lo que sí funciona es sobrescribir **`getValue()`**, que es igualmente virtual y
solo necesita `get()`, que es público:

```cpp
class SnappedBoolParameter : public juce::AudioParameterBool
{
public:
    using juce::AudioParameterBool::AudioParameterBool;
private:
    float getValue() const override { return get() ? 1.0f : 0.0f; }
};
```

El valor crudo interno sigue siendo el que sea, pero hacia fuera el parámetro
reporta siempre 0 o 1 exactos, que es justo lo que APVTS guarda. El viaje de ida
y vuelta cuadra.

Dos cosas que vale la pena retener:

- La suite de DSP **no puede** encontrar esto. Es un problema de contrato con el
  host, no de algoritmo. Por eso hacen falta las dos capas.
- pluginval marcó 3 de los 4 booleanos. El cuarto (Ping-Pong) se salvó por azar
  del valor aleatorio que le tocó, pero tenía el mismo bug. **Un fallo de
  validación casi nunca está solo**: conviene buscar el patrón, no parchear los
  casos concretos que salieron en rojo.

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

## Pruébalo primero sin DAW

También se compila una versión Standalone:

```
build\ReverseVerb_artefacts\Release\Standalone\ReverseVerb.exe
```

Ábrela, elige tu interfaz en Options > Audio Settings, y ya oyes el efecto. Es
mucho más rápido para iterar que abrir Ableton cada vez.

## Parámetros

**REVERSE** — el motor principal. Drive, Low Cut y High Cut actúan *dentro* de
su lazo de realimentación, así que solo tocan las repeticiones, no el primer
pase. Es lo que hace que la cola se oscurezca estilo cinta.

| | |
|---|---|
| **Length** | Ventana que se invierte, 20–2000 ms. Es también el desfase del efecto. Por debajo de ~80 ms suena granular; a 500 ms+ es el *swell* clásico. |
| **Sync** + división | Fija Length en fracciones de compás del BPM del host. Con puntillos y tresillos. |
| **Feedback** | Ver la nota sobre el techo, más abajo. |
| **Drive / Low Cut / High Cut** | Carácter de la cola. |
| **Drive Env** | Ata el drive a la fuerza con que tocas. A 0 el drive es fijo. Al subirlo, tocar suave deja la nube limpia y atacar fuerte satura y rompe la cola invertida. |
| **Freeze** | Congela el buffer: el efecto se sostiene indefinidamente y puedes seguir tocando encima sin que la señal nueva entre al congelado. Con Freeze activo, Length y Feedback se desactivan porque el buffer ya no se escribe. |

**DELAY** — delay estéreo convencional, con su propio sync y sus propios
filtros. El selector de ruteo cambia bastante el resultado:

| Ruteo | Qué hace |
|---|---|
| **Off** | Delay desactivado. |
| **Delay → Rev** | Ecos rítmicos que *luego* se invierten. Cada repetición entra al buffer y sale del revés. |
| **Rev → Delay** | El swell invertido completo se repite rítmicamente. El más "usable" de los tres. |
| **Paralelo** | Las dos ramas a la vez, sumadas a 0.7 cada una porque son señales decorrelacionadas y sumarlas a 1.0 dispararía el nivel. |

| **Mod** / **Mod Rate** | Chorus en la cola: modula el tiempo de lectura hasta 8 ms. L y R van en **cuadratura** (90° de desfase), que es lo que abre la imagen estéreo — la mitad de la gracia de un chorus. |

**Ping-Pong** cruza la realimentación entre canales: las repeticiones alternan
lado. No afecta a la estabilidad — el viaje L→R→L tiene ganancia `fb²`, menos
que el lazo directo.

**TAPE** — modulan la posición de lectura del reverse. Modular la posición de
lectura es modular la afinación: es lo mismo que hace una cinta cuando el motor
no gira perfectamente constante.

| | |
|---|---|
| **Wow** | Deriva lenta (0.5–0.8 Hz), hasta ±45 cents. Es la que hace que el sonido parezca venir de un casete gastado. |
| **Flutter** | Temblor rápido (7–12 Hz), hasta ±14 cents. Más sutil pero es lo que quita la sensación de "digital perfecto". |
| **Detune** | ±50 cents, en sentidos **opuestos** en L y R. El batido entre canales ensancha la imagen sin necesidad de reverb, y es lo que hace que una pared de sonido suene mareadora. |
| **Shimmer** | Cuánta realimentación pasa por el pitch shifter. Cada pasada del lazo sube de altura, así que se van apilando octavas. |
| **Shim Pitch** | Semitonos del desplazamiento, −12 a +12. |

Dos decisiones que igual no son obvias:

**Un solo modulador para todos los canales.** Una cinta real tiene un único
transporte. Modular cada canal por separado sonaría a chorus, no a cinta. El
ensanchado estéreo lo hace `Detune`, que es otra cosa distinta.

**La profundidad se especifica en cents, no en muestras.** La desviación de
afinación que produce modular un retardo depende de la *derivada* del
desplazamiento, no de su amplitud: un LFO lento necesita mucha más profundidad
que uno rápido para desafinar lo mismo. Especificándolo en cents, "Wow al 50 %"
desafina igual a 44.1 que a 96 kHz.

**SPACE**

| | |
|---|---|
| **Reverb / Size / Damp** | A 0 el reverb está en bypass, sin coste de CPU. |
| **Reverb después del reverse** | **On**: el swell clásico que crece hacia la nota. **Off**: el reverb entra al buffer y se invierte con él, carácter de "succión". |

**OUTPUT**

| | |
|---|---|
| **Mix** | Seco/procesado. |
| **Duck** | Atenúa el efecto mientras hay señal de entrada. Es lo que hace usable un reverse reverb en una mezcla densa: la cola crece en los huecos en vez de tapar la nota que la provoca. |
| **Duck Rel** | Con qué rapidez vuelve el efecto al parar de tocar. Ajústalo al ritmo de la frase. |

## La interfaz

La ambientación es de agujero de gusano, pero el visualizador **no es
decoración**: sigue siendo el instrumento de diagnóstico que era antes, solo
que en coordenadas polares.

| Lo que ves | Lo que es |
|---|---|
| Las dos partículas que orbitan la boca | Los dos granos. Su brillo es literalmente `sin(pi·fase/L)`, la ganancia real de su ventana. Se ven alternar y **cruzarse a media intensidad**: si algún día dejan de hacerlo, el bug se ve antes de oírse. |
| La torsión del túnel | El feedback del reverse. |
| El halo del núcleo | La cantidad de reverb. Se atenúa cuando el ducking actúa. |
| Los anillos naciendo y muriendo en transparencia 0 | El mismo truco que las ventanas de grano: nada aparece de golpe. |

Los knobs van de **cian a magenta** según su valor, así que el color informa sin
tener que leer el número.

Las cabeceras de sección mantienen nombres funcionales a propósito. La
ambientación va en los gráficos; renombrar "Feedback" a algo como "Materia
Oscura" queda muy bien en una captura y es un incordio cuando buscas un control
seis meses después.

### Freeze: por qué es seguro y el feedback no

Freeze **no** es "feedback al infinito". Son dos cosas distintas y esa
diferencia es la que lo hace seguro.

Subir el feedback es realimentar: la salida vuelve a entrar al buffer y se
acumula. Por eso hay un techo medido de 0.60, y por encima el lazo se
autooscila.

Freeze es dejar de escribir Y dejar de avanzar el puntero. No hay acumulación
posible: cada grano nuevo captura el mismo punto de arranque y cicla las mismas
L muestras. Medido en la suite de tests, el nivel se sostiene con una deriva de
**0.000 dB tras 18 segundos**. Ni decae ni crece.

Y sale sin clicks por construcción: los granos conservan su ventana, que sigue
naciendo y muriendo en amplitud 0, así que congelar a mitad de grano es limpio.
Los granos en vuelo terminan de leer contenido real y el siguiente ya arranca
sobre el buffer congelado.

### Drive dinámico

`Drive Env` se calcula **por muestra**, no por bloque. A 512 muestras la
granularidad sería de 10 ms y el drive llegaría tarde al ataque de la pulsación,
que es justo lo que se quiere seguir.

Hay una coincidencia afortunada que hace esto seguro, y viene del bug del
saturador que hubo que corregir en su día: como la fórmula es `tanh(d·x)/d`, su
ganancia máxima es 1 **sea cual sea `d`**. Modular el drive por tanto no afecta
a la estabilidad del lazo. Con la fórmula original (`tanh(d·x)/tanh(d)`, ganancia
`d/tanh(d)` mayor que 1), atacar fuerte habría subido la ganancia del lazo y
disparado la autooscilación: el bug habría vuelto disfrazado de característica.

### Lectura fraccionaria

La posición de lectura del reverse es un float con interpolación lineal:

```
pos = start - phase * rate + modOffset
```

Con `rate=1` y `modOffset=0` la posición cae en enteros exactos y el resultado
es **bit a bit idéntico** a la versión anterior con índices enteros — hay un test
que lo comprueba (diferencia máxima 0.000000000). O sea que la refactorización
no cambia el sonido por defecto; solo habilita dos cosas:

- **`modOffset`** → wow y flutter. Modular la posición de lectura es modular la
  afinación.
- **`rate`** → pitch shifting. Verificado: leyendo al doble de velocidad, un
  seno de 200 Hz sale a 400 Hz.

Sobre `rate` conviene saber que esta topología —granos con ventana, solapados al
50 %, con crossfade de potencia constante— **es** la de un pitch shifter
granular tipo PSOLA. La única diferencia con uno convencional es que aquí el
incremento es negativo. `rate = r` desplaza la afinación `12·log2(r)` semitonos,
y el crossfade que ya existía absorbe las discontinuidades.

La limitación honesta: el tamaño de grano está atado a `Length`, y un buen pitch
shifting quiere granos de 30–80 ms. Con `Length` en 1500 ms el shifting sonaría
muy embarrado. Para un shimmer decente hará falta una etapa aparte.

El buffer pasó de `3×maxLength` a `4×maxLength`: con `rate` hasta 2 y la
corrección de reenganche, el span máximo llega a 3.75×L.

### Shimmer: tres hallazgos

**El primero costó dos intentos.** La primera versión del pitch shifter usaba la
misma ventana sin/cos que el motor de reverse. Es lo correcto allí, donde los
dos granos leen regiones distintas y decorrelacionadas. Aquí es al revés: leen
el **mismo** material con un desfase fijo, así que se peinan. Medido: con +12
semitonos sobre un seno de 220 Hz, la portadora de 440 Hz quedaba **suprimida** y
solo salían dos bandas laterales a 420 y 453 Hz. Sonaba a modulación en anillo,
no a octava.

La solución es un crossfade **corto** (15 %) y de amplitud constante: la mayor
parte del grano suena una sola cabeza limpia. Con eso la portadora vuelve a
dominar y las bandas laterales bajan de 0.40 a 0.20 relativo.

**El segundo es un límite, no un bug.** Queda un error de afinación de ±20–25
cents que **depende de la frecuencia de entrada** — es la interferencia residual
del crossfade, inherente al método granular. Los shifters transparentes usan
análisis de fase (phase vocoder), que cuesta órdenes de magnitud más CPU y no
cabe dentro de un lazo de realimentación. Para shimmer no molesta, porque el
sonido shimmer ya es de por sí irreal, pero no lo uses como armonizador afinado.

**El tercero es contraintuitivo:** el shimmer hace el lazo **más estable**, no
menos. El pitch shifter empuja la energía hacia arriba en cada pasada, y arriba
se escapa: el paso bajo la atenúa y lo que supera Nyquist desaparece. Sin
shimmer, la energía se queda dando vueltas.

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

Consecuencia práctica: con Shimmer al máximo puedes subir el Feedback más que
sin él, y la cola dura más.

### Un bug que los tests realistas no veían

Al implementar wow y flutter apareció algo que ninguno de los tests con
profundidades realistas detectaba: **una modulación positiva adelantaba la
lectura al punto de arranque del grano**, donde la memoria todavía no se ha
escrito en este ciclo y contiene audio de hace varios segundos.

Con +400 muestras de offset, el salto máximo era **4.1 veces el techo teórico**,
siempre en la fase < offset del grano. Con las profundidades reales de wow el
artefacto quedaba enmascarado porque la ventana vale poco en esa zona, así que
pasaba desapercibido y solo se habría manifestado como "a veces suena sucio".

Se arregla clampeando el offset a `phase * rate`: la modulación entra
progresivamente desde el arranque del grano, justo donde la ventana vale casi 0.

Hay un test dedicado que fuerza el caso extremo. La lección que me llevo: probar
solo con valores realistas deja huecos, porque los artefactos aparecen antes en
los extremos y desde ahí se cuelan hacia el rango normal.

### Medidores

Abajo a la derecha, entrada y salida. Escala de −48 a +6 dB, con marca en 0 dBFS
y el tramo por encima en ámbar.

No es un limitador, es un aviso. Existe porque con feedback alto, shimmer y
saturación en el lazo es fácil pasarse sin notarlo: las colas largas suben
despacio y el oído se acostumbra antes de que suene claramente mal.

### Coste en CPU

Solo repinta el visualizador, a 30 Hz; el fondo y los controles son estáticos y
solo se redibujan al redimensionar. Esto importa más de lo que parece: una GUI
de plugin que repinta mucho compite con el hilo de audio, y en máquinas justas
se manifiesta como cortes que parecen del buffer pero no lo son. Si alguna vez
sospechas, cierra la ventana del plugin y comprueba si desaparecen — con la
ventana cerrada el editor no existe y no repinta nada.

## Posición en la cadena

Este plugin es **un eslabón**, no una cadena. Con un ampli virtual (NAM y
similares), compresor y EQ delante, el orden que tiene sentido es:

```
DI --> NAM / ampli --> compresor --> EQ --> ReverseVerb
```

**Después del ampli**, porque invertir una señal ya distorsionada no es lo mismo
que distorsionar una señal invertida. Quieres que el swell sea de tu tono, no
que el ampli tenga que lidiar con una cola que crece al revés.

**Después del compresor**, porque un compresor detrás bombearía contra los
swells y deshace el ducking — que precisamente está midiendo la dinámica de la
señal seca.

**El EQ sustractivo, delante**, si arrastras graves sucios. Aunque el Low Cut
del lazo ya se encarga de que la cola no se embarre al acumular repeticiones.

### Mono y estéreo

El plugin acepta **mono→mono, estéreo→estéreo y mono→estéreo**. Este último es
el caso real de una cadena de guitarra: DI mono, ampli mono, y a partir de aquí
quieres estéreo para que el ping-pong y la anchura del reverb tengan dónde
actuar. En ese caso la entrada se duplica a los dos canales *antes* de procesar.

Lo que no se admite es **estéreo→mono**: se perdería el ping-pong en la suma, y
el host sabe hacer esa mezcla mejor que el plugin.

Si acabas en una pista mono, el botón de Ping-Pong se pone en gris y pasa a
decir "Ping-Pong (mono)", y arriba a la derecha aparece un aviso `MONO`. Es
deliberado: un botón que se puede pulsar y no hace nada es peor que un botón
apagado.

### Guardar la cadena entera

Los presets de este plugin solo guardan este plugin. Para guardar NAM +
compresor + EQ + ReverseVerb como una unidad, en Ableton es un **Audio Effect
Rack**: metes todo dentro y lo guardas como preset de rack. El preset de
ReverseVerb queda dentro, así que puedes tener un rack con *Ref: Guthrie Wash*
ya seleccionado.

## Presets

**De fábrica** hay 18, en seis grupos: swells ambientales, rítmicos
sincronizados, granulares, sutiles de mezcla, cuatro de referencia y tres de
shoegaze. Aparecen tanto en el combo del plugin como en el menú nativo de
presets de tu DAW.

Los presets nuevos (18–23) están construidos alrededor de las funciones de
tape, shimmer y drive dinámico, y los existentes se revisaron para que usen
`Wow` y `Detune` donde tiene sentido — sobre todo los de shoegaze, donde el
desafinado entre canales es parte de la identidad del sonido.

Uno merece explicación: **Drone (pulsa Freeze)** no suena distinto de entrada.
Es un ajuste *preparado* para congelar: ventana larga, shimmer moderado, reverb
grande. Tocas un acorde, pulsas Freeze, y sigues tocando encima del colchón.

**Los tuyos** se guardan con el botón Guardar, como XML en:

```
%APPDATA%\ReverseVerb\Presets\*.rvpreset
```

Sobreviven a reinstalar el plugin y los puedes copiar a otro ordenador. Solo se
pueden borrar los tuyos: los de fábrica viven en el binario y el índice de
programa que el host guarda en tu proyecto depende de que la lista no cambie.

### Sobre los presets "Ref:"

Son puntos de partida inspirados en el **papel concreto** que un reverse o un
delay juega en esos discos, no emulaciones. El sonido de una banda sale del
instrumento, el amplificador, la sala, la compresión y la mezcla; este plugin es
un eslabón de esa cadena. Dicho eso, el papel sí es identificable y por ahí es
por donde empiezan:

| Preset | De dónde sale la idea |
|---|---|
| **Ref: Guthrie Wash** | Cocteau Twins, Slowdive. Guitarra lavada hasta perder el ataque: ventana larga, reverb generoso, graves recortados para que no embarre, ping-pong para abrir la imagen. |
| **Ref: Bow & Swell** | Sigur Rós y post-rock en general. Ventana de 1.6 s, reverb enorme, ducking al 75 % para que la nota respire antes de que llegue la cola. |
| **Ref: Greenwood Reverse** | Radiohead. Más corto, sincronizado y con saturación en el lazo. Es el único de fábrica con **el reverb en modo "antes"**: la cola entra al buffer y se invierte con él, que es de dónde sale ese carácter incómodo de succión. |
| **Ref: Dotted Edge** | U2. Aquí el protagonista es la sección DELAY con corchea con puntillo; el reverse queda en 120 ms como condimento, casi un efecto de ataque. |

### Shoegaze

| Preset | Idea |
|---|---|
| **Shoe: Muro** | El muro de sonido. Mix al 72 %, las dos ramas en paralelo, drive al 3. El efecto pesa más que la señal seca, que es el punto. |
| **Shoe: Souvlaki** | Slowdive: vidrioso y largo. Ventana de 1.1 s, reverb al 82 %, damping bajo para que brille. |
| **Shoe: Nowhere** | Ride: más empuje. Sincronizado a corcheas con el delay en semicorcheas, drive al 4. |

Dos cosas van **al revés** que en el resto de presets, y son deliberadas:

**El ducking va bajo** (10–25 % en vez de 50–80 %). En cualquier otro contexto
quieres que la cola se aparte de la nota; en shoegaze el desenfoque *es* la
estética y las dos cosas deben solaparse en una pasta. Un ducking alto te
dejaría un efecto pulcro, que es exactamente lo contrario.

**El Low Cut va alto** (150–250 Hz). Con el mix por encima del 60 % los graves
se acumulan repetición tras repetición y el muro se vuelve barro. Es el control
que separa "denso" de "sucio".

Aviso de calibración: el sonido shoegaze sale sobre todo de reverb, modulación y
distorsión. El *glide* de My Bloody Valentine es la palanca de trémolo, que este
plugin no puede hacer. Estos presets aportan la parte de cola invertida, que en
Cocteau Twins y Slowdive sí es un ingrediente real.

### Una trampa que evité a propósito

Muchos hosts llaman a `setCurrentProgram` **después** de restaurar el estado de
tu proyecto. Si el plugin aplicase el preset sin mirar, cada vez que reabrieras
la canción te machacaría los ajustes guardados y sonaría distinta sin
explicación aparente.

La solución: el índice de programa viaja dentro del estado, y
`setCurrentProgram` hace cortocircuito si el índice no ha cambiado de verdad.
Es el motivo por el que la lista de fábrica tiene que ser de tamaño fijo, y por
el que los presets de usuario viven en una lista aparte.

Consecuencia práctica para el futuro: **los presets nuevos se añaden siempre al
final del array**, nunca en medio. El host guarda el índice dentro de tu
proyecto, así que insertar uno en medio desplazaría todos los siguientes y las
sesiones ya guardadas cargarían un preset distinto al que elegiste. Agrupar por
estilo queda más bonito en el código; no romper proyectos ajenos importa más.
Por eso los tres de shoegaze están detrás de los "Ref:" aunque conceptualmente
irían antes.

## Dos techos de feedback distintos, y por qué

El reverse y el delay tienen topes muy distintos: **0.60** el reverse, **0.95**
el delay. No es arbitrario.

En el **delay** cada muestra se lee una sola vez y sin ventana, así que la
ganancia del lazo es exactamente `fb`. Con `fb<1` es incondicionalmente estable.

En el **reverse** no.
No es prudencia decorativa: el lazo tiene ganancia mayor que 1 por dos razones
que se suman.

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
nadie. El clamp está dentro de `ReverseDelay::process`, no solo en el rango del
parámetro, porque un host puede automatizar fuera de rango o cargar un preset
de otra versión.

Si algún día quieres sustain infinito, la forma correcta es un **modo Freeze**
—dejar de escribir entrada y ciclar el buffer— que es seguro por construcción.

## Un bug que casi se cuela

La primera versión del saturador era `tanh(d·x)/tanh(d)`. Parece razonable
—normaliza para que el techo quede en 1— pero su ganancia para señales
pequeñas es `d/tanh(d)`, o sea **siempre mayor que 1**: +2.3 dB con el drive al
mínimo y hasta +21 dB al máximo. Es decir, un amplificador escondido dentro del
lazo de realimentación.

Lo correcto es `tanh(d·x)/d`, cuya ganancia máxima es exactamente 1. A oído
esto se habría manifestado como "el plugin a veces se pone a pitar y no sé por
qué", y es muy fácil culpar a la interfaz o al DAW antes que al algoritmo.

## Siguientes pasos sugeridos

1. **Valida con pluginval** antes de confiar en él:
   `pluginval.exe --strictness-level 8 --validate ReverseVerb.vst3`
2. **Modo Freeze**: dejar de escribir entrada y ciclar el buffer. Es la forma
   segura de tener sustain infinito, y además es un efecto en sí mismo.
3. **Presets** de fábrica.
4. **Modulación** del tiempo de delay (chorus/vibrato en la cola). El motor ya
   lee con interpolación fraccionaria, así que la infraestructura está puesta.
