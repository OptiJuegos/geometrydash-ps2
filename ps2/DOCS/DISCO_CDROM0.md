# Arranque desde disco / ISO (cdrom0:)

Estado: compila y pasa las pruebas de host. **No probado en PCSX2 con ISO ni en
consola/OPL.**

## Como funciona
- `romfs_ps2.c` prueba `host:`, `host:package`, `mass:/GDPS2` y `cdrom0:`. Si
  `argv[0]` empieza con `cdrom`/`cdfs` (`PS2_SetLaunchPath`), el disco va primero.
- `cdrom0:` solo acepta nombres ISO9660 exactos. `disc_names.c` pasa a
  mayusculas, cambia todo fuera de `[A-Z0-9_.]` por `_` y acorta nombres de mas de
  31 caracteres con un hash FNV-1a. `ps2/tools/disc_names.py` hace lo mismo al
  escribir la ISO; `ps2/tools/test_disc_names.py` verifica que coincidan.
- La grafia que acepta el driver (`/` o `\`, con o sin `;1`) cambia segun el
  loader. Se prueban las 4 y se recuerda la que funciona (log
  `GDPS2 ROMFS: disc spelling style=N`).
- `stat()` y `access()` caen a `fopen()` en disco; `opendir()` en `romfs:` de disco
  devuelve error porque `readdir()` en `cdrom0:` no lista nada. El juego solo
  enumera carpetas de guardado, no de romfs.
- Los guardados van a Memory Card (nunca al disco).

## Crear la ISO
```
build.bat
python ps2/tools/make_iso.py            # ps2/out/release/GeometryDashPS2.iso
python ps2/tools/make_iso.py --id SLUS_123.45 --vmode PAL
```
Requiere `pip install pycdlib`. La ISO lleva `SYSTEM.CNF`, el ELF con el id del
disco y `ROMFS/`.

## A verificar en hardware
- Cual de las 4 grafias acepta OPL y el disco real (mirar el log).
- Latencia de lectura de MP3 y niveles en DVD.
- Que audsrv (IRX embebido) cargue igual sin `mass:`.
