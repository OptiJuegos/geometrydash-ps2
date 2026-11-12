# PS2: correccion controlada de MP3 EOF/cola (2026-10-16)

Este documento complementa `AUDIO_AUDITORIA_PS2SDK_MPG123.md`.
El objetivo continua siendo portar el motor original de Geometry Dash, con
musica MP3 y efectos WAV originales. No se generaron canciones WAV ni se cambio
el backend a ADPCM.

## Cambios de codigo

- `source/mp3_player.c` bajo `__PS2__`: el estado de `mpg123_read()` se
  transmite junto con las muestras decodificadas. `done` del API es bytes,
  `decode_mp3()` conserva su contrato local de muestras PCM16.
- `ps2/src/mp3_decode_flow.h`: clasifica `MPG123_OK`, `MPG123_DONE`,
  `MPG123_NEW_FORMAT`, `MPG123_NEED_MORE`, valida alineacion de frames y
  limites de buffer. `DONE` con bytes aun permite encolarlos; `DONE` sin bytes
  no encola nada. `OK` con cero bytes no se interpreta como EOF.
- Un `NEW_FORMAT` obliga a consultar `mpg123_getformat()`. Solo se admite el
  formato de salida ya configurado; si cambia se detiene con un error claro
  en lugar de consumir PCM incompatible.
- Un MP3 looping reubica **solo el decoder** al comienzo con `mpg123_seek()`
  durante EOF, sin `ndspChnReset()`, preservando la cola ya reproducible.
  Se limita el numero de intentos sin progreso por buffer a cuatro.
- En PS2 `decoderLock` protege desde lectura/validacion hasta
  `ndspChnWaveBufAdd()`, para que `seek_mp3()` no limpie los `waveBuf`
  entre decodificar y encolar. PSP/3DS conservan sus rutas anteriores.
- La cola PS2 sigue en PCM16 remuestreado por el mixer; MP3 permanece en ROMFS.

## Validacion host / compilacion

- `ps2/tools/test_mp3_decode_flow.ps1`: PASS. Incluye `OK`, `DONE` con/sin
  bytes, `NEW_FORMAT`, `NEED_MORE`, error, desalineacion, saturacion de
  capacidad y simulacion de reintento tras EOF looping.
- `ps2/tools/test_audio.ps1`: PASS. Nuevo caso comprueba que `nsamples=0`
  no ingresa en la cola de `PS2_MixerQueue` incluso con puntero PCM valido.
- `ps2/tools/test_audsrv_stream.ps1`: PASS para escritura parcial / stall.
- `test_romfs.ps1`, `test_keyboard.ps1`, `test_json.ps1`: PASS.
- `python -m unittest discover -s ps2/tests -q`: seis tests OK; warnings de
  archivos abiertos en `psp/tools/mkatlas.py` preexistentes y ajenos al audio.
- `build_engine.ps1`: **95/95** unidades del motor, **107** objetos enlazados,
  **0** simbolos faltantes. La compilacion no acredita sonido audible.

## PCSX2: prueba de menu sin entradas, resultado NO concluyente

ELF de diagnostico: `ps2/out/gdps2-diag-menu-audio.elf` compilado con
`-DiagnosticAudioTrace` y `PS2_DIAG_NO_INPUT` (solo para diagnostico).
Configuracion PCSX2 aislada bajo `ps2/out/audio-acoustic/volume100/`.

Se hicieron dos ejecuciones iniciales en PCSX2 Sanity v1.0.3, una ventana
oculta y otra visible. Se cerro solo el PID creado para cada prueba.

1. `gdps2-menu-postfix-flow.log`: `GDPS2 STATE: MAIN_MENU` en ~5.6908s,
   `GDPS2 PCM STEP: before send next=289` en ~5.6958s.
2. `gdps2-menu-postfix-visible.log`: `MAIN_MENU` en ~5.4146s,
   `before send next=289` en ~5.4198s.

Ambas trazas se detuvieron despues de entrar al menu, sin
`GDPS2 PCM STEP: after send next=289` ni `GDPS2 MP3: decoder init begin`.
No hubo transicion a GAME por mando, pero **la musica ni siquiera habia
empezado a abrirse**. Los logs localizan un punto de bloqueo en o alrededor
del envio PCM 289 al driver; NO permiten atribuir la causa a mpg123, al IOP,
al mutex, a PCSX2, al render ni a `audsrv_wait_audio` aisladamente.
El bloqueo intencional de `audsrv_wait_audio` esta documentado por PS2SDK,
y no implica fallo por si solo. No se cambiaron prioridades ni delays.

Pruebas adicionales:

3. `gdps2-block289-rpc.log`: ELF `gdps2-diag-block289.elf` enlazado con
   trazas de entrada/salida a `ps2_audio_wait` y `ps2_audio_send` solo para
   bloque 289. Se detuvo despues de `before send next=289`, SIN alcanzar
   `GDPS2 AUDIO RPC: entering wait for block 289`. Por tanto, no demuestra
   que el hilo este bloqueado *dentro* de `audsrv_wait_audio`.
4. `gdps2-normal-postfix.log`: **ELF normal sin macros de diagnostico**.
   Llegó a `MAIN_MENU` (~4.8924s) y log de pad (~4.8980s), pero tampoco
   imprimio `GDPS2 MP3: decoder init begin` ni registro progreso posterior.

El punto comun es el arranque del menu, antes de que el decodificador pueda
probarse. Incluso los nuevos prints de diagnostico podrian interferir en la
planificacion; no usar estas trazas para afirmar que `audsrv` este roto.
La causa exacta sigue abierta y requiere aislar progreso del main thread
y del mixer EE sin añadir logging masivo. Todas las pruebas cerraron su
propio proceso PCSX2, sin afectar BIOS ni configuracion del usuario.

### Trabajo que queda para validar audio audible

1. Aislar progreso del main thread (`ui_load_screen`, menu init, primer
   `C3D_FrameEnd`) y del mixer EE con puntos minimos y sin multiplicar
   impresiones desde threads, para determinar la ultima llamada completada.
2. Inspeccionar si la configuracion de PCSX2 (`HostFs`, GS/vsync, velocidad
   y threading EE/IOP) impide el progreso del menu en estas corridas; comparar
   con el log normal anterior que SI abria archivos MP3.
3. Confirmar que el MP3 se abre, que hay PCM no nulo por muchos segundos y
   luego verificar continuidad audible mediante salida real de PCSX2 o PS2.
   La decodificacion correcta y los tonos PCM de prueba no bastan.
4. Repetir gameplay sin reinicios de nivel, y posteriormente PS2 fisica.

Fuentes que justifican el contrato:
- https://mpg123.org/api/group__mpg123__input.shtml
- https://ps2dev.github.io/ps2sdk/audsrv__rpc_8c_source.html
- https://ps2dev.github.io/ps2sdk/audsrv_8c_source.html

**Estado:** corregida la ruta de EOF y reforzada la sincronizacion PS2,
pruebas host y enlace aprobados. **No** certificado sonido MP3 continuo ni
corregido el bloqueo observado justo al entrar al menu.
