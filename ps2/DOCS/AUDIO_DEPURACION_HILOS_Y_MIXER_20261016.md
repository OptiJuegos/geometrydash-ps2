# Geometry Dash PS2: depuracion de hilos, mixer y MP3 (2026-10-16)

Continuacion de `AUDIO_AUDITORIA_PS2SDK_MPG123.md` y
`AUDIO_CORRECCION_EOF_20261016.md`. El objetivo sigue siendo el motor original
con sus MP3 en ROMFS y los SFX WAV, no una recreacion ni archivos convertidos.

## Causa principal confirmada: prioridad del hilo principal EE

El arranque nativo de PS2SDK dejo `main()` con prioridad EE efectiva **1**.
El worker `audsrv` se creaba a prioridad **24** (`0x18`); el thread MP3
dependia de `threadCreate()` y de la prioridad relativa de `main`.

- Con `main=1`, el menu avanzaba 60 FPS mientras el primer `mpg123_read`
  podia tardar unos **50 segundos** en devolver 4.096 muestras; el audio
  entregaba aproximadamente **24-35 bloques PCM/s**, en lugar de ~86/s.
- El decoder **no estaba inherentemente lento**: un ELF PS2 independiente
  `gdps2-mp3-decode-probe.elf` abrio el mismo `menuLoop.mp3` y leyo tres
  bloques de 8.192 bytes en ~8 ms el primero y ~1 ms los siguientes.
- Una cesion `DelayThread(1000)` por fotograma ayudo al primer decode
  (~183 ms) pero seguia produciendo menos PCM/s del requerido. Un intervalo
  de 100 us dio peores resultados. Las pruebas no justifican dejar estos
  delays dentro del renderizador GS.
- Al normalizar `main` de **1 a 32** con `ChangeThreadPriority(TH_SELF, 0x20)`
  despues de `ndspInit()`, dejando mixer=24 y quitando el DelayThread,
  `audsrv` paso a entregar **256 bloques cada ~2,97-3,02 s**, en linea con
  512 frames/bloque a 44.100 Hz (256*512/44100 = 2,972 s).
- En el log `gdps2-mainprio32-no-yield.log`, la prueba sobrepaso **10.496
  bloques** y mantuvo `music=1`, `nonzero_blocks=256/256` y **0 escrituras
  parciales** en los ultimos intervalos. Los menuframes seguian cercanos a
  60/s. Es evidencia de flujo PCM continuo a cadencia real en PCSX2, no una
  certificacion subjetiva de audibilidad en una PS2 fisica.

### Cambios permanentes

1. `source/main.c` (`__PS2__`): tras arrancar `ndspInit()` exitosamente,
   bajar prioridad del hilo principal a `0x20`, verificar prioridad efectiva,
   registrar aviso en caso de fallo. En PSP/3DS no cambia nada.
2. `ps2/src/c2d_ps2.c`: retirar `DelayThread(1000)` insertado durante la
   investigacion. No se fuerzan pausas ni tiempos artificiales en los frames.
3. `ps2/src/ndsp_mixer.c`: especializacion de **un canal PCM16 estereo**
   sin otros SFX activos. Conserva float, interpolacion, soft clip y cambios
   de waveBuf equivalentes. Si hay otro canal, usa el mezclador generico.
   `test_mixer_fast_reference.ps1` verifica salida y estados **exactos**.
4. La variante Q16/Q12 de punto fijo se mantiene exclusivamente para pruebas
   con `-DiagnosticFastFixedMixer`. No forma parte del ELF normal: su ganancia
   en el motor completo fue pequena y altera ligeramente las muestras.

## Evidencia de costos de mezcla

Con el motor entero (y antes de normalizar `main`) las marcas de timer
mostraron **~9.17 s** dentro del render generico por 256 bloques activos,
**~5.96 s** con el fast path float, **~5.73 s** con fixed point; tiempo
esperando el semaforo ~0. Estos tiempos incluyen los periodos en que el
hilo fue desalojado por el sistema EE, no solo CPU computando PCM.

La comparativa controlada **sin el resto del motor**, compilando
`ps2/tests/mixer_perf_ps2_probe.c`, confirma que no es una limitacion
intrinseca de CPU para ese trabajo:

| Variante | 256 bloques x 512 frames sin juego |
|---|---:|
| Mezclador generico | 225 ms |
| Un canal estereo float | 67 ms |
| Diagnostico Q16/Q12 | 42 ms |

El Q16/Q12 tuvo error maximo 30 unidades PCM16 y error medio 1,11 sobre
1.628 muestras de prueba; no se adopto aun. El fast path float tuvo
coincidencia exacta en seis escenarios, incluyendo loops, colas, panes
y SFX simultaneos.

## Verificaciones realizadas

- Build motor original: **95/95** fuentes, **107** objetos, **0** simbolos
  ausentes (ver `ps2/out/engine/compile_report.json`, `link_report.json`).
- Pruebas host: `test_audio.ps1`, `test_audsrv_stream.ps1`,
  `test_mp3_decode_flow.ps1`, `test_mixer_fast_reference.ps1`,
  `test_mixer_fixed_reference.ps1`, `test_romfs.ps1`, `test_keyboard.ps1`,
  `test_json.ps1`, y seis pruebas Python PASS.
- ELF normal `ps2/out/gdps2-engine-experimental.elf`: no incluye
  `GDPS2 MENUD`, `GDPS2 MP3 STAGE`, `GDPS2 AUDIO HEARTBEAT`, macros de
  autonav ni comparaciones de memoria/prioridad. En PCSX2 llega a
  `MAIN_MENU`, abre MP3 32.000 Hz, decodifica el primer buffer y registra
  la primera mezcla estereo no nula con diferencia de milisegundos.
- Con un ELF separado `gdps2-diag-toe2-audio-priority.elf` se alcanzo
  `STATE_GAME`, indice 17 (Theory of Everything 2), y se decodifico la
  musica MP3 del nivel. Es una prueba de integracion, no gameplay completo
  validado hasta el final.
- PCSX2 siempre se ejecuto en una configuracion de prueba aislada. No se
  tocaron SDKs, BIOS, formato de canciones, niveles originales ni ROMFS.

## Logs principales bajo `ps2/out/audio-acoustic/volume100`

- `gdps2-mp3-read-stage.log`: primer `mpg123_read` con espera enorme.
- `gdps2-standalone-mp3-probe.log`: mismo decodificador sin threads del juego.
- `gdps2-profile-lock-vs-render.log`: mezcla con main prioridad 1,
  render lento, sin esperas de semaforo relevantes.
- `gdps2-mixer-reference-solo.log`, `gdps2-mixer-float-solo.log`,
  `gdps2-mixer-fixed-solo.log`: benchmarks de costo intrinseco.
- `gdps2-mainprio32-no-yield.log`: validacion de cadencia real en menu.
- `gdps2-final-normal-audio.log`: ELF normal, apertura y primer PCM no nulo.
- `gdps2-toe2-audio-priority.log`: carga del nivel Theory of Everything 2.

## Limites

Las capturas de logs de PCM no certifican audibilidad ni ausencia de clicks
en el dispositivo del usuario. Falta escuchar PCSX2 o PS2 fisica, probar
transiciones, reinicios, efectos WAV en niveles largos y el cargador ROMFS
para hardware. Las excepciones `R5900 Jump to unmapped recLUT page` de dos
corridas **fuertemente instrumentadas** no se reprodujeron en las corridas
normales posteriores y no estan diagnosticadas como fallo de sonido.

No se adoptaron ajustes especulativos de prioridad del decoder, archivos WAV
de musica, ADPCM de otro juego ni cambio de API `audsrv`.
