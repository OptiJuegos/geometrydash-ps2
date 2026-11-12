# Auditoria de audio PS2: documentacion antes de modificar

Fecha: 2026-10-16. Alcance: motor Geometry Dash original, `mpg123` para
musica MP3, NDSP compatible para efectos WAV, mezclador EE y `audsrv`/SPU2.
No se plantea convertir los MP3 ni migrar los efectos a ADPCM.

## Fuentes primarias consultadas

1. PS2SDK, cliente EE `audsrv_rpc.c`, implementacion de `audsrv_wait_audio`,
   `audsrv_play_audio`, `audsrv_set_format`, callbacks:
   https://ps2dev.github.io/ps2sdk/audsrv__rpc_8c_source.html
2. PS2SDK, servidor IOP `audsrv.c`, buffer circular y transmision a SPU2:
   https://ps2dev.github.io/ps2sdk/audsrv_8c_source.html
3. PS2SDK, ejemplo oficial `playwav.c`, inicializacion y escritura:
   https://github.com/ps2dev/ps2sdk/blob/master/ee/rpc/audsrv/samples/playwav/playwav.c
4. PS2SDK, ejemplo de notificacion para rellenado `playwav2.c`:
   https://github.com/ps2dev/ps2sdk/blob/master/ee/rpc/audsrv/samples/playwav2/playwav2.c
5. Puerto SDL para PS2SDK, `SDL_ps2audio.c`, `WaitAudio` y `PlayAudio`:
   https://github.com/ps2dev/ps2sdk-ports/blob/master/sdl/src/audio/ps2sdk/SDL_ps2audio.c
6. mpg123, inicializacion y versiones de API:
   https://mpg123.org/api/group__mpg123__init.shtml
7. mpg123, lectura: `done` son bytes, retornos especiales y formato:
   https://mpg123.org/api/group__mpg123__input.shtml
   https://mpg123.org/api/group__mpg123__error.shtml
   https://mpg123.org/api/group__mpg123__output.shtml

Las docs web de mpg123 corresponden al desarrollo actual: comprobar la
version enlazada. El header local `psdevwindows/ps2sdk/ports/include/mpg123.h`
declara `MPG123_API_VERSION 49`; en versiones desde API 46,
`mpg123_init()` es un no-op y NO se puede afirmar que omitirlo rompa
necesariamente el decodificador. Era necesario en APIs anteriores.

## Hechos documentados

- `audsrv` recibe PCM; NO decodifica MP3. La decodificacion en RAM con
  `mpg123` es necesaria para enviar PCM al SPU2, sin crear archivos WAV.
- `audsrv_wait_audio(bytes)` **debe bloquear** hasta que haya sitio para
  `bytes` en el buffer circular del IOP. Llamarlo antes de
  `audsrv_play_audio` es el patron del propio PS2SDK y SDL para PS2.
  Una espera larga se investiga, no se diagnostica automaticamente como
  error del API.
- `audsrv_play_audio` devuelve el numero de bytes aceptados, que puede
  ser menor que los pedidos. El servidor IOP limita la copia a lo que
  quede libre en el buffer. El helper `PS2_AudioSubmitAll` evita descartar
  el sufijo no aceptado de nuestro bloque de 2048 bytes.
- `audsrv_on_fillbuf` ofrece notificacion asincrona de espacio; no
  necesitamos cambiar al modelo de callbacks sin una prueba que lo avale.
- `mpg123_read(mh, out, max_bytes, &done)` devuelve `done` en **bytes**.
  Ademas de `MPG123_OK`, puede emitir `MPG123_DONE` (fin),
  `MPG123_NEW_FORMAT` (cambio de formato) o `MPG123_NEED_MORE`.
  Tener `done == 0` no es por si solo equivalente a `MPG123_DONE`.
- `mpg123_format()` selecciona formatos permitidos, no convierte
  automaticamente la salida a ese sample rate. La salida de 32 kHz de
  los MP3 del ROMFS se mezcla/remuestrea a 44,1 kHz en nuestro backend.

## Auditoria del codigo local: hechos vs hipotesis

### BUG CONFIRMADO POR CODIGO: encolado de cero muestras

En `source/mp3_player.c`, `audio_thread()` hace `read = decode_mp3()`;
cuando `read == 0 && looping`, llama a `seek_mp3(0)` pero **no hace
`continue`**; intenta `ndspChnWaveBufAdd` con `buf->nsamples = 0`.
`PS2_MixerQueue()` en `ps2/src/ndsp_mixer.c` rechaza explicitamente
`!b->nsamples`, deja `status=DONE`. Los logs anteriores registran
precisamente rechazo `frames=0`. Se debe corregir el control de flujo,
pero antes distinguir `MPG123_DONE` y `MPG123_NEW_FORMAT` en el wrapper.

### CONCURRENCIA A AUDITAR, NO COMPROBADA

`seek_mp3` reinicia el canal y hace `memset(waveBuf[])` mientras el
decodificador podria seguir usando/enviando un buffer: el bloqueo
`decoderLock` no cubre completamente el `ndspChnWaveBufAdd` que ocurre
en `audio_thread`. La hipotesis de carrera debe validarse con trazas
de generacion de buffer/reset y una prueba controlada de seek.

### TEMPORIZACION, NO CAUSA RESUELTA

En capturas previas PCSX2, algunos envios `audsrv_wait_audio` tardaron
~2,7 segundos y el mezclador registro variaciones amplias. Esto puede
ser un problema de programacion de hilos, carga emulada, reinicios de
nivel, mutex o disponibilidad de buffer. No hay pruebas que asignen el
problema exclusivamente a `audsrv` o `mpg123`.

`ps2/src/ndsp_mixer.c` se cambio de acumuladores `double` a `float`
para el R5900; las pruebas host pasaron, pero NO hay mejora de latencia
medida concluyente. La cantidad de buffers MP3 se aumento a 12 (~0,77 s
de PCM a 32 kHz) y su reserva se hizo contigua; falta demostrar su
efecto sobre cortes de audio en sesiones sin muertes.

En las pruebas de un solo PCSX2, la configuracion SDL genero pulsaciones
reales y la escena paso automaticamente por menus aunque no se habia
compilado `PS2_DIAG_AUTONAV`. La estabilidad musical en un menu o nivel
sin transiciones NO quedo validada. `PS2_DIAG_NO_INPUT` debe usarse
solo en un ELF de diagnostico, nunca en el ejecutable principal.

### Control de API VERSION

El header local declara `MPG123_API_VERSION 49`; la documentacion de
mpg123 especifica que `mpg123_init()` solo era obligatorio antes de API
46. La afirmacion anterior de que la falta de `mpg123_init()` era la
causa del silencio **no estaba justificada para esta API**. El cambio
de inicializar una vez se conserva por compatibilidad, pero **no puede
presentarse como solucion probada**.

## Protocolo de validacion siguiente (sin cambiar formatos)

1. Reproducir tono aislado de `gdps2-audio-test.elf` en una sola instancia
   PCSX2 con la salida de sonido conocida; comprobar que termina.
2. Ejecutar una prueba de menu sin entrada del mando, **solo en
   diagnostico**, y registrar `mpg123` formato, colas, estados, PCM y
   `audsrv_queued`/`audsrv_available` cuando sea seguro invocarlos.
3. Corregir el intento de encolar `nsamples=0`, comprobando resultado
   real de `mpg123_read` y fin de archivo antes de repetir el MP3.
4. Repetir Stereo Madness y un nivel pesado con controles manuales,
   guardar trazas temporales y comparar continuidad de audio y CPU EE.
5. Compilar ELF principal **sin macros diagnosticas** y ejecutar
   `test_audio.ps1`, `test_audsrv_stream.ps1` y el resto de regresiones.

**Estado de esta auditoria:** investigacion de documentacion, NO
certificacion de audio continuo ni aprobacion de nuevos cambios en la
implementacion. Se preservan MP3 y WAV originales.
