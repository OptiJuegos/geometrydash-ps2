# Auditoría float/double PS2 — 27/10/2026

La FPU del R5900 procesa float en hardware; la aritmética double usa rutinas de software.
Esta ronda eliminó promociones a double en bibliotecas de color, efectos de muerte,
velocidad del jugador y comparación de pendientes. En PS2 el porcentaje entero
del gameplay ahora se formatea mediante enteros con redondeo al par.

La auditoría GCC con -Wdouble-promotion y mips64r5900el-ps2-elf-nm -u dejó
operaciones double aritméticas del motor únicamente en cJSON y su interfaz
source/utils/json_config.c. Las promociones por %f en printf son obligatorias
por las reglas de argumentos variádicos de C.

Verificación: 96/96 unidades compiladas, 110 objetos enlazados, 0 símbolos
faltantes. El redondeo entero se comparó en 200.102 casos sin discrepancias.

Prueba PCSX2 aislado con -DiagnosticFrameTiming -DiagnosticAutoInput
-DiagnosticNoClipLevel -DiagnosticLevelIndex 17: Theory of Everything 2,
55 segundos. 20 ventanas de 120 cuadros (2400 en total); una primera ventana
registró un pico de 279,38 ms; en las 19 restantes (2280 cuadros) no hubo
frames superiores a 20 ms. Máximo estable 18,89 ms; actualización media
2,178 ms y frame medio 16,66 ms con VSync.

Este resultado verifica ejecución, sin demostrar aceleración A/B.
No se realizó prueba en PS2 física.

Archivos de prueba: ps2/out/gdps2-float-gameplay-v3.elf,
ps2/out/ft.float-gameplay-v3.log.

## Revalidacion posterior (27/10/2026)

- `GDPS2_EXTRA_CFLAGS=-Wdouble-promotion` con `check_engine.ps1 -SyntaxOnly`:
  **96/96** archivos, **27 avisos**, todos de argumentos `float` promovidos a
  `double` al llamar funciones variadicas (`level_loading.c`: 3,
  `main.c`: 23, `menus/gameplay.c`: 1). No quedan promociones aritmeticas
  adicionales identificadas por el compilador en la ruta normal del motor.
- En los objetos de la compilacion EE, los helpers de `double` como `__muldf3`
  aparecen en `cJSON.o` y funciones de conversion/comparacion en
  `json_config.o`; `player.o` no tiene dependencias `df` (solo funciones
  `float`, como `fmodf` y `roundf`).
- `test_fast_float.ps1`: **PASS**. `pak_layout_smoke.py`: **PASS**, 176
  hashes SHA-256 y alineacion de 2048 bytes.
- ELF/PAK/ISO de release en `ps2/out/float-resume-20261027/`:
  compilacion **96/96**, enlace **112 objetos**, ninguna referencia sin
  resolver, `pak_iso_smoke.py` **PASS**. El PAK tiene el mismo SHA-256 que la
  ISO diagnóstica `ps2/out/stutter-diagnostic/`:
  `D0547AFCA3017E14608C35133BC8D20E94ECC4AFF6946091C9FD20F9BFC75467`.
- Se anadio `ps2/tools/ft_stutter_report.py` y su test para leer histogramas
  de VBlank, ventanas FT, tiempos GS y estimaciones de VRAM sin modificar
  las rutas de juego. `python ps2/tests/test_ft_stutter_report.py`: **PASS**.
  Con `--skip 1`, el log anterior `ps2/out/ft.float-gameplay-v3.log` dio
  19 ventanas / 2280 frames, 0 cuadros >20 ms y pico 18,89 ms; todavia no
  tiene `GDPS2 FT VBLANK`.

**Pendiente:** capturar la ISO `stutter-diagnostic` bajo la misma carga y
configuracion que muestra los tirones, y analizar los histogramas con:

`python ps2/tools/ft_stutter_report.py RUTA_AL_FT.LOG --skip 1 --csv ventanas.csv`

No se ejecutó la nueva ISO de release en PS2 física ni se obtuvo gameplay
de la ISO diagnóstica; no atribuir los tirones a `double`, GS o audio sin
la nueva medicion. No cerrar el PCSX2 ya abierto del usuario.
