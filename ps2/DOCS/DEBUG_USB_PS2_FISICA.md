# Diagnostico de PS2 fisica: parpadeos cian y regreso al navegador

## Para que sirve

`build_debug.bat` genera un ELF independiente con el motor completo y
marcas de inicio persistidas a `mass:/GDPS2/debug.log`, sin macros de
autojump, autonav o noclip. Incluye `usbd.irx` y `usbhdfsd.irx` del SDK
local **solo en el ELF debug** para intentar montar el USB tras uLaunchELF.

El ELF normal `ps2/out/release/GeometryDashPS2.elf` queda separado.
El debug no elimina ni migra guardados; no escribe Memory Card para logs.
No se cambia el modo GS a ciegas antes de leer las etapas.

## Compilar

Desde la raiz del proyecto en Windows: `build_debug.bat`.
Genera `ps2/out/debug/GeometryDashPS2_DEBUG.elf` y la carpeta
`ps2/out/debug/romfs/`. No inicia PCSX2 ni ejecuta pruebas por defecto.

## Pendrive y uLaunchELF

1. Usa USB FAT32/MBR. Crea la carpeta `GDPS2` en la raiz.
2. Copia `GeometryDashPS2_DEBUG.elf` a `USB:/GDPS2/`.
   Copia también **`ps2/out/debug/audsrv.irx`** al mismo directorio
   (`USB:/GDPS2/audsrv.irx`). La carga por archivo es alternativa al
   buffer EE si el parche LOADFILE no se puede instalar en la PS2 real.
3. Copia **toda** `ps2/out/debug/romfs/` a `USB:/GDPS2/romfs/`.
   Verifica `USB:/GDPS2/romfs/menus/main_menu.txt`.
4. Inicia uLaunchELF -> `FileBrowser` -> `mass:/GDPS2/` ->
   `GeometryDashPS2_DEBUG.elf`.
5. Si vuelve al navegador, apaga la consola normalmente, conecta el USB
   a Windows y abre `USB:/GDPS2/debug.log`. Copia el archivo o al menos
   las lineas de la **ultima** `GDPS2 DEBUG SESSION`.

## Que contiene `debug.log`

- Etapas `GDPS2 BOOT 00` a `17`: antes de ROMFS, GS, inicializacion del
  mezclador, atlas, render inicial y cambio al menu.
- Estado de `usbd.irx` y `usbhdfsd.irx` (id y resultado IOP).
- Resultado de buscar ROMFS en `host:`, `mass:/GDPS2` y `cdfs:`.
- Subetapas de GFX: PAD, gsKit, DMA/GIF, inicializacion NTSC 640x448,
  texturas y backend de sprites. Las lineas de regreso indican la etapa
  completada; si una queda sin su pareja, esa region merece revision.
- Resultados C3D/C2D/audio, `svcBreak` y cambios a estado MAIN_MENU,
  LEVEL_SELECT o GAME. Cada linea se guarda con apertura/append/cierre
  para intentar sobrevivir a cuelgues posteriores.
- Primer fotograma de menu: entrada a `main_menu_loop` y regreso de
  `C3D_FrameBegin` / `C3D_FrameEnd`, util para acotar el fallo grafico.

Los registros anteriores se **conservan** (modo append). Nunca se
trunca `debug.log` existente. Se escribe poco, sobre todo al arrancar.

## Limitaciones importantes

- Esto **no** garantiza capturar una excepcion de CPU, DMA o IOP que mate
  el proceso antes de que el USB este montado. Tampoco es un volcado de
  registros MIPS ni un debugger remoto; la ultima etapa completada ayuda
  a acotar el fallo, no demuestra por si sola la causa.
- La carga dinamica USB necesita que los drivers del SDK sean compatibles
  con la consola/pendrive. Si el ELF llega a fallar montando USB, no podra
  dejar `debug.log`. Se conserva stdout para usar ps2link o PCSX2 con log.
- No reinstala/resetear el IOP para no interferir con modulos del sistema.
  La presencia del archivo no implica que los recursos hayan cargado.
- `host:debug.log` sirve como alternativa cuando se prueba el ELF en
  PCSX2/ps2link con HostFS disponible. En PS2 real la ruta prevista es
  `mass:/GDPS2/debug.log` (o `mass0:/GDPS2/debug.log`).
- Si el fichero existe pero esta vacio o muestra solo `GDPS2 BOOT 00`,
  comparte ese resultado. Una prueba en PCSX2 nunca equivale a validacion
  en la consola real.
- El logger nunca escribe datos en Memory Card. Si llegas a jugar con el
  debug, **el juego** puede guardar progreso al abandonar niveles normalmente.

## Revisar el resultado

Por ejemplo, si el ultimo mensaje es `GDPS2 BOOT 03` y no hay
`GDPS2 GFX: gsKit_init_screen returned`, puede estar fallando la ruta GS.
Si solo aparece el fallo `ROMFS unavailable`, hay que revisar el montaje
USB/ruta de recursos antes de investigar la GPU.

## Segundo log recibido: sbv_patch_enable_lmb devuelve 1

La PS2 física repitió `GDPS2 RESULT AUDIO sbv_patch_enable_lmb: 1 (FAIL)`
dos veces; no llegó a cargar LIBSD ni `audsrv.irx`.
La cabecera oficial PS2SDK especifica que **solo 0 es éxito**. En el
código de PS2SDK, el 1 significa que no reconoció la secuencia esperada
del despachador LOADFILE al intentar parchearlo. No es evidencia de
parche instalado ni equivale necesariamente a que LOADFILE esté roto.

El siguiente **ELF debug exclusivamente** intenta:

1. Registrar ese código de fallo sin interrumpir inmediatamente.
2. Cargar `audsrv.irx` del pendrive con `SifLoadModule`, usando
   `mass:/GDPS2/audsrv.irx` y luego `mass0:/GDPS2/audsrv.irx`.
3. Si ambos fallan, probar `SifExecModuleBuffer` y registrar id/error,
   dado que LOADFILE más reciente podría soportar esa operación de forma
   nativa. Esta prueba podría fallar y no garantiza estabilidad real.
4. Solo continuar a `audsrv_init()` si el módulo se carga correctamente.

**No se cambia el release normal hasta validar el resultado en hardware.**
Las sesiones anteriores de `debug.log` se mantienen.

## Hallazgo en PS2 real (primer debug.log)

El primer log recibido confirmó que USB, ROMFS, GS, C3D y C2D arrancan.
El último resultado fue `GDPS2 RESULT ndspInit: -1 (FAIL)` durante `BOOT 07`.
En la versión anterior, el juego entraba en `no_dsp_firmware()`, una pantalla
de error para **3DS**, y salía del proceso. No demuestra que el renderizador
GS sea la causa del fallo.

El nuevo ELF debug registra también resultados *dentro de* `ndspInit()`:

- Resultado exacto de `sbv_patch_enable_lmb()` (LOADFILE IOP).
- Resultado de `SifLoadModule("rom0:LIBSD")`.
- Tamaño de `audsrv.irx`, ID y resultado de `SifExecModuleBuffer()`.
- Resultado de `audsrv_init()`, frecuencia/formato, volumen.
- IDs del semáforo/hilo y arranque del worker de mezcla.

Si falla el audio, PS2 registra `GDPS2 FATAL: PS2 audsrv/SPU2 initialization
failed` y termina **sin mostrar** la pantalla de ayuda de firmware DSP de
3DS. Por ahora no se fuerza el audio a continuar sin IOP; la siguiente
prueba física debe permitir localizar el paso exacto antes de corregirlo.
