# Geometry Dash PS2: assets.pak estilo Minecraft

Formato MCPK v1 compatible con mc/platform/storage/PakArchive.cpp:
indice big-endian (FNV-1a, nombre, offset, size) y payload sin compresion.
Las claves son relativas a romfs/, por ejemplo menus/main_menu.txt.
El lector PS2 cachea el indice y abre recursos con fopencookie, sin
extraer ni almacenar todos los assets en memoria.

## Compilar y empaquetar

```bat
build_pak_release.bat
```

Este comando recompila el motor, prepara el ROMFS, empaqueta el MCPK y
genera automaticamente la ISO. Los tres archivos quedan juntos en
`ps2/out/pak-release/`:
`GeometryDashPS2.elf`, `assets.pak` y `GeometryDashPS2-PAK.iso`.
La ISO tiene `SYSTEM.CNF`, el ELF de arranque (`SLUS_666.01`) y
`ASSETS.PAK;1` sin duplicar una carpeta `ROMFS/`.
El nombre distinto de ISO permite mantener abierta la ISO normal en PCSX2.
Requiere `pycdlib` instalado en Python.

Tambien puede ejecutarse directamente
`ps2/tools/build_engine.ps1 -PackagePak -Output ps2/out/pak-release/GeometryDashPS2.elf`;
`-PackagePak` ahora incluye el ISO automatico. Para actualizar los
recursos desde fuentes, agregar `-PackageRomfs`.

```powershell
python ps2/tools/package_romfs.py
python ps2/tools/make_assets_pak.py
powershell -NoProfile -ExecutionPolicy Bypass -File ps2/tools/build_engine.ps1 -PackagePak -Output ps2/out/gdps2-pak.elf
```

El packer escribe ps2/out/package/assets.pak y verifica SHA-256 por entrada.
Tambien se admite -PackageRomfs -PackagePak en una sola compilacion.

Instalar ELF y assets.pak juntos en cualquier carpeta del pendrive, por
ejemplo USB:/Juegos/GeometryDash/. El juego prefiere el directorio de
arranque obtenido desde argv[0] y solo despues prueba los dispositivos
de reserva para preservar instalaciones antiguas. Un pak ausente o parcial
tiene fallback a los archivos sueltos. El guardado sigue separado en
Memory Card o su destino alternativo.

## Pruebas

- MCPK empaquetado: 158 archivos, 70.424.222 bytes, verificados.
- ELF final: 96/96 unidades y 112 objetos; no faltan simbolos.
- PCSX2 con solo ELF y assets.pak (sin romfs/): atlas y WAV del menu OK.
- Gameplay automatico Theory of Everything 2 OK. Ventanas estables de
  120 frames a ~16,66 ms, con un pico al entrar al nivel.
- Regresion sin pak: arranque y atlas OK con solo romfs/ suelta.
- Test separado con StereoMadness.wav omitido del pak: mpg123 abrio
  StereoMadness.mp3 directamente desde MCPK (32000 Hz, estereo) y el
  gameplay avanzo sin bloqueos.

Pendiente: PS2 fisica/USB y comparacion A/B de rendimiento. No distribuir
los ELF de autoinput/noclip, que se compilaron solo como diagnostico.

## Localizador dinamico (segunda refactorizacion)

El codigo de descubrimiento quedo aislado en ps2/src/asset_locator.c
y su cabecera. Se eliminan rutas de instalacion fijas como
mass:/GDPS2 y host:package de la lista de assets.

Secuencia: directorio de argv[0] -> directorio actual (si es un
dispositivo PS2) -> raices host:/mass:/mass0:/mass1:/cdrom0:/cdfs:
-> reintento USB (solo si el ELF arranco desde USB y no aparecieron
recursos, maximo 3 s) -> escaneo de hasta 64 nombres por carpeta
en la raiz USB o cerca del ejecutable. No se recorren directorios
opticos. El primer directorio que contenga un pak valido o
romfs/menus/main_menu.txt queda seleccionado para la sesion.

"host:", "mass:" y "cdrom0:" son nombres de dispositivos de PS2,
no carpetas de instalacion. El nombre assets.pak y las claves
romfs:/... son identificadores logicos. Las rutas de guardado de
Memory Card (mc0:GDPS2, mc1:GDPS2) y los destinos de logging debug
siguen separados para preservar datos/diagnosticos existentes.

Verificacion: ps2/tools/test_romfs.ps1 incluye asset_locator_smoke.c
con rutas mass:/ arbitrarias, host:E:/... y cdrom0:/.
PCSX2 comprobo (1) pak junto al ELF y (2) descubrimiento por escaneo
de recursos sueltos en una subcarpeta vecina sin nombre predefinido.
Falta validar reintentos de montaje sobre PS2 fisica.

## Prioridad de busqueda y cache por sesion

Una vez que `romfsInit()` encuentra un MCPK valido con
`menus/main_menu.txt`, conserva el directorio elegido y el indice
del PAK en RAM. Una segunda llamada a `romfsInit()` no vuelve
a probar rutas, dispositivos ni a enumerar carpetas. Solo
`romfsExit()` libera el indice y habilita una nueva busqueda.

- **USB:** primero la carpeta del ELF; despues
  `mass:/GDPS2/assets.pak`, `mass0:/GDPS2/assets.pak` y
  `mass1:/GDPS2/assets.pak`; luego el directorio actual, dispositivos
  y como ultimo recurso una exploracion limitada.
- **Disco/ISO:** carpeta del ELF y luego la raiz
  `cdrom0:/ASSETS.PAK;1` (con variantes ISO9660). Sin busqueda USB
  cuando la consola arranca del disco.
- **PCSX2/ps2link:** conserva la preferencia del directorio del ELF
  o CWD de `host:`.

El log `GDPS2 ROMFS: location cached` informa la ruta seleccionada,
numero de pruebas de localizacion y si se monto un PAK. Las lecturas
simultaneas de texturas/audio abren flujos sobre el **mismo PAK ya
resuelto**, no vuelven a explorar dispositivos. No se precargan los
archivos de texturas ni canciones completos a la RAM de PS2.

## Read-ahead CD/DVD (registro PCSX2 del 27/10)

Un arranque de la ISO con 176 entradas MCPK reporto un solo montaje del PAK
(`root=cdrom0:, probes=1`) y alcanzo `BOOT 17`, con audio funcionando.
Pero las lecturas de recursos generaron 9905 mensajes CDRead, de los cuales
4378 repetian inmediatamente el mismo sector y tamano. Esto no es una
segunda busqueda de la instalacion: son lecturas de los contenidos.

En `ps2/src/asset_pak.c` ahora cada flujo dispone de 16 KiB de read-ahead
en RAM. Al solicitar fragmentos pequenos dentro de la ventana no repite
`fseek` ni lectura del CD; al salir de la ventana la rellena. Si falla la
reserva de memoria vuelve a la lectura anterior. No mantiene los 89 MB del
PAK completo en memoria, ni cambia el formato de archivo.

## Descriptor POSIX persistente y lectura compartida

Se sustituyo el `fopen` del PAK para cada recurso por un solo descriptor
persistente de `assets.pak` por montaje, siguiendo
`mc/platform/storage/PakFile.cpp`. El montaje prueba `open(O_RDONLY)`,
`lseek()` y `read()` contra la cabecera MCPK; si el dispositivo PS2
no soporta estas operaciones, usa un unico `FILE*` `stdio` de respaldo.
Ambos caminos conservan las mismas claves MCPK y el wrapper
`fopencookie`, por lo que los cargadores originales no cambian.

Un semaforo EE protege el par `lseek/read` de los hilos de musica y
graficos. Los archivos virtuales tienen sus propios cursores y
ventanas de 16 KiB; no se reabre el descriptor en cada lectura.
El backend usa referencias para no cerrar un descriptor compartido
mientras queden flujos abiertos, incluso si se desmonta el indice.
La tabla del indice MCPK se carga ahora en una lectura agrupada.

Prueba automatizada (Windows/MinGW):
`powershell -File ps2/tools/test_romfs.ps1` ejecuta
`asset_pak_posix_smoke.c`: comprueba 4 lectores simultaneos,
busquedas aleatorias hacia delante y atras, integridad contra
lectura nativa y flujos vivos luego de desmontar el indice.
Esta prueba no sustituye una prueba de la rama `fopencookie` de
newlib en consola. La imagen de prueba independiente se genera con:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ps2/tools/build_engine.ps1 -PackagePak -Output ps2/out/pak-posix-release/GeometryDashPS2.elf
python ps2/tests/pak_iso_smoke.py --elf ps2/out/pak-posix-release/GeometryDashPS2.elf --pak ps2/out/pak-posix-release/assets.pak --iso ps2/out/pak-posix-release/GeometryDashPS2-PAK.iso
```

En el registro, el montaje indica
`POSIX persistent descriptor` o `stdio persistent descriptor`,
facilitando verificar que controlador uso PCSX2/PS2 fisica.

## Optimizacion fisica del PAK para CD/DVD

El PAK anterior escribia cada archivo en **orden de hash**, alineado a
64 bytes. Ese orden es correcto para buscar entradas del MCPK, pero muy
malo como orden de acceso fisico desde un CD: las texturas de arranque
estaban esparcidas por todo el archivo (incluidas canciones grandes).

Ahora `make_assets_pak.py --layout boot --align 2048` mantiene la
tabla de entradas ordenada por hash (compatible con el lector MCPK v1),
pero almacena los datos en el orden real de carga del juego:
`startup_logo`, atlas de pantalla de carga, atlas de interfaz, atlas
de gameplay y efectos de sonido; canciones mas tarde. Cada entrada
empieza en un sector de 2048 bytes dentro del PAK.

`build_engine.ps1 -PackagePak` selecciona este formato optimizado por
defecto. `make_assets_pak.py` sin opciones conserva el formato fisico
anterior (orden hash, 64 bytes), para pruebas comparativas.

Comparacion verificada el 27/10/2026, con los mismos 176 recursos:

- PAK original: 88.795.486 bytes.
- PAK con layout CD: 89.012.318 bytes (+216.832 bytes).
- La suma de distancias absolutas entre las posiciones consecutivas de
  los 24 recursos de arranque baja de aproximadamente 801,50 MiB a
  0,03 MiB. **Es una metrica de localizacion, no tiempo de carga medido.**
- SHA-256 de cada entrada igual al archivo fuente; offsets sin solapes,
  tabla por hash intacta y 2048-byte alignment comprobados.
- El test POSIX de cuatro lectores concurrentes y la verificacion
  SYSTEM.CNF/ELF/PAK dentro de la ISO pasaron.

Comandos de regresion:

```powershell
python ps2/tools/make_assets_pak.py ps2/out/package/romfs ps2/out/pak-layout-test/assets.pak --layout boot --align 2048
python ps2/tests/pak_layout_smoke.py --romfs ps2/out/package/romfs --baseline ps2/out/pak-posix-release/assets.pak --optimized ps2/out/pak-layout-test/assets.pak
python ps2/tests/pak_iso_smoke.py --elf ps2/out/pak-posix-release/GeometryDashPS2.elf --pak ps2/out/pak-layout-test/assets.pak --iso ps2/out/pak-layout-test/GeometryDashPS2-PAK.iso
```

La ISO de prueba esta en `ps2/out/pak-layout-test/`. No sustituye la
ISO previamente abierta en PCSX2. La diferencia de tiempo en consola
fisica y en PCSX2 requiere comparar ambas ISOs en el mismo entorno.
Compresion por archivo (LZ4/ADPCM, etc.) no esta habilitada: exigiria
cambios compatibles en el cargador y mayor uso de CPU.

Para evitar reemplazar la ISO que estaba abierta en PCSX2, la compilacion
de prueba se guardo separadamente en `ps2/out/pak-buffered-release/`.
La compilacion, el enlace y la verificacion de ELF/PAK dentro de la nueva
ISO pasaron; queda comparar sus tiempos de carga y CDRead con la ISO
anterior, y comprobarla en la PS2 real. En ese mismo log la Memory Card 0
fue reconocida como formateada pero el guardado uso `host:save/`, tema
separado por investigar antes de asegurar guardado en CD fisico.
