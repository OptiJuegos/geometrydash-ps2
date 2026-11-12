# Guardados de Geometry Dash para PS2

## Que se guarda y cuando

Se reutiliza el sistema JSON original del port 3DS:

- `config.json`: volumen, configuracion, iconos y colores seleccionados.
- `data/<hash>.d`: intentos, saltos, progreso normal/practica, estrellas y
  las tres monedas de cada nivel.
- `external_levels/` y `saved_songs/`: carpetas del motor original; la
  descarga y lectura completa de contenido externo no esta validada en PS2.

Las opciones llaman a `cfg_save()` al cambiarlas; el progreso del nivel se
persiste cuando se **abandona el nivel** mediante el flujo normal de salida.
No se escribe una partida por cada muerte ni por cada paso de fisica para
evitar tirones de I/O y desgaste de Memory Card. Una salida brusca, cuelgue
o interrupcion electrica **antes** del guardado puede perder progreso reciente.

## Deteccion de dispositivo PS2

`romfsInit()` escoge ROMFS en `host:`, `host:package/`, `mass:/GDPS2` o
`cdfs:`. El almacenamiento persistente se selecciona mas tarde,
en `cfg_init()`, despues de inicializar PAD/SIO2:

1. Si los assets vienen de host y ya hay `host:save/config.json`, se mantiene
   **host** para conservar partidas anteriores de PCSX2/ps2link.
2. Se carga `rom0:MCMAN` y `rom0:MCSERV`, se inicializa `libmc` y se consultan
   `mc0:` y `mc1:`. Solo se usan tarjetas PS2 formateadas; no se formatea
   ninguna tarjeta ni se borra/modifica una carpeta existente por fuerza.
3. Entre tarjetas utilizables, se prefiere una con guardado anterior en
   `mcN:GDPS2/save/config.json` (`mc0` primero en caso de empate).
4. De no haber guardados anteriores, se intenta crear
   `mc0:GDPS2/save/` o `mc1:GDPS2/save/`.
5. Si no hay ninguna Memory Card accesible, se mantiene el almacenamiento
   de la carpeta del juego por `host:` en PCSX2, o `mass:/GDPS2/save/`
   cuando los recursos provienen de USB. Con solo `cdfs:` y sin Memory
   Card/host utilizable, **el guardado no esta garantizado**.

Nota: `mc0:GDPS2` sigue la sintaxis usada por los ejemplos POSIX de PS2SDK;
la ruta virtual 3DS se adapta mediante `PS2_PathMap`, sin modificar el
codigo de los menus ni el formato de archivos del motor.

Se quitaron `config_save()` innecesarios del proceso de carga de los 18
niveles y de la configuracion en builds PS2. El logger del motor en PS2
tambien usa **stdout EE**, no crea `output.txt` en Memory Card.
Cuando hay un error real de escritura JSON, se imprime `GDPS2 SAVE ERROR`.

## PCSX2: distintas carpetas de guardado

La version original de desarrollo iniciada desde `ps2/out/` escribia en:

`ps2/out/save/config.json` y `ps2/out/save/data/*.d`.

La version release lanzada desde `ps2/out/release/` usa:

`ps2/out/release/save/config.json` y `ps2/out/release/save/data/*.d`.

**Son carpetas independientes.** Ejecutar `build.bat` no borra ni migra
automaticamente partidas. Si se necesita importar la partida antigua, con
el juego cerrado copiar los archivos *previo respaldo*, sin sobrescribir
archivos mas recientes de la carpeta de destino inadvertidamente.

Si el juego abre desde la nueva carpeta pero ya existe `config.json` en
`release/save/`, conserva ese destino para no elegir silenciosamente una
Memory Card y parecer que se perdio la partida anterior.

## Pruebas reproducibles

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\ps2\tools\test_romfs.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\ps2\tools\test_save_selection.ps1
```

El primer test valida rutas host/USB/disco/MC0/MC1. El segundo simula seis
casos de presencia y formato de tarjetas sin tocar datos reales; valida
conservacion del guardado host, seleccion de MC preexistente y fallbacks.

`build.bat` compila la version release sin test, autojump ni macros de
diagnostico. Se enlaza `-lmc` y usa los modulos ROM de PS2, sin cambiar
PS2SDK ni empaquetar un nuevo driver.

## Limites

- La API/libmc, rutas y enlace se comprobaron estaticamente y mediante tests
  del adaptador. Falta verificar un ciclo **escribir -> apagar -> reiniciar ->
  leer** en una tarjeta formateada real y en una imagen de Memory Card de
  PCSX2, ya que el emulador del usuario estaba en uso.
- No hay todavia `icon.sys` / icono 3D para el navegador de Memory Card.
- No existe un sistema de journaling/backups atomicos. No retirar Memory Card
  ni desconectar la consola mientras se guarda.
- No hay selector UI de ranura ni sincronizacion automatica entre tarjeta,
  host y USB. Se prefiere preservar la partida encontrada, nunca fusionar
  automaticamente distintas partidas.
- La carga automatica de drivers Mass USB sigue pendiente; los recursos
  `mass:/` deben estar montados por el entorno de ejecucion.
