# Geometry Dash PS2: rendimiento en Deadlocked

## Metodo

ELF de diagnostico con `-DiagnosticFrameTiming -DiagnosticAutoInput
-DiagnosticNoClipLevel -DiagnosticLevelIndex 19` (Deadlocked), ejecutado en
PCSX2-Sanity durante 170 s con `ps2/tools/run_ft_pcsx2.ps1`. Cada ventana del
log cubre 120 frames (2 s). El ELF se coloca en `ps2/out/diag-dl/` junto a una
junction `romfs -> ps2/out/release/romfs`, porque `ps2/out/package/romfs` no
incluye Deadlocked.

`check_link.ps1` aceptaba `DiagnosticLevelIndex` solo hasta 17; ahora el
limite es 19 (hay 20 niveles principales).

El tramo pesado esta hacia los 66-76 s del nivel: 500-830 sprites por frame.

## Resultados (ventanas 1-43, sin la carga inicial)

| Variante | Frames >20 ms | Frames >33 ms | EE ocupada media | Peor ventana |
|---|---:|---:|---:|---:|
| Base | 101 | 95 | 8,64 ms | 18,08 ms |
| + lotes de triangulos | 85 | 82 | 8,55 ms | 17,19 ms |
| + culling 4:3 | 23 | 22 | 7,82 ms | 14,48 ms |
| + lista de pulsos activos | 6 | 6 | 7,61 ms | 13,61 ms |
| + angulos en float | 0 | 0 | 6,68 ms | 11,86 ms |
| + cache cos/sin | 0 | 0 | 6,58 ms | 11,56 ms |
| + VU0 en sprites rotados | 0 | 0 | 6,67 ms | 11,51 ms |
| + triggers/colores (cambio 8) | 0 | 0 | 6,51 ms | 11,03 ms |

"EE ocupada" = frame (16,67 ms) menos la espera de `gsKit_sync_flip`.

## Cambios

1. **Lotes de triangulos** (`ps2/src/c2d_ps2.c`): los sprites rotados y la
   geometria `C2Di` (trails, `C2D_DrawTriangleUV`) se agrupan por textura en
   una sola primitiva `gsKit_prim_list_triangle_goraud_texture_uv_3d`, como ya
   se hacia con los sprites sin rotacion. Solo uno de los dos lotes tiene datos
   a la vez, asi que el orden de dibujo no cambia. Binds por frame: -40 %.
2. **Culling contra el scissor** (`c2d_ps2.c`): una imagen totalmente fuera
   del area visible no entra en el lote, no genera paquete ni bind.
3. **Culling 4:3 en `create_objects`** (`source/graphics.c`, solo `__PS2__`):
   la TV muestra solo x logica 40..360; los objetos se descartan contra ese
   rango con el mismo margen de 60 unidades que el original. Un 15-20 % menos
   de sprites que crear, ordenar, colorear y dibujar.
4. **Pulsos de grupo** (`source/triggers_v2.c`): `trig2_object_color` recorria
   los 64 huecos de pulsos por cada grupo de cada sprite. Ahora
   `trig2_pulse_channels` guarda una vez por frame la lista de pulsos activos
   sobre grupos.
5. **`C3D_AngleFromDegrees` en float** (`psp/include/citro3d.h`): `M_TAU` es
   `double`, y la EE no tiene FPU de doble precision; cada objeto e hijo hacia
   una multiplicacion y una division `double` por software. `create` bajo de
   ~3,5 ms a ~2,0 ms en el tramo denso. Tambien afecta al port PSP (Allegrex
   tampoco tiene doble precision). `source/utils/gfx.c` usa `(float)M_PI` en
   los circulos.
6. **Seno/coseno por tabla** (`ps2/src/ps2_sincos.h`): 1024 pasos con
   interpolacion lineal, error < 5e-5. Lo usan `C2D_DrawImage` (sprites
   rotados) y `cos_sin_degrees` en `graphics.c` para angulos no enteros
   (sierras y decoracion que gira). En el tramo denso hay 250-400 imagenes
   rotadas por frame. Mejora pequena (EE ocupada 6,91 -> 6,64 ms con la
   instrumentacion activa).

7. **VU0 en modo macro para sprites rotados** (`c2d_ps2.c`, `corners_to_gs`):
   la vista, la pantalla inferior fusionada y la escala 4:3 se pliegan en una
   afin (`physical_affine`); con la rotacion y la posicion del sprite, la VU0
   transforma las 4 esquinas en dos cadenas FMAC y convierte X, Y, U y V a
   12.4 con cuatro `VFTOI4`. Los vertices se escriben directamente en el lote
   de triangulos, con los mismos recortes que `vertex_to_XYZ2`/`vertex_to_UV`.
   `-DPS2_NO_VU0` compila la misma logica en escalar.

   | Variante | `C2D_DrawImage`, 6 peores ventanas | Peor ventana EE |
   |---|---|---:|
   | Ruta anterior | 2,44-4,28 ms | 11,95 ms |
   | Afin plegada, escalar | 2,42-3,83 ms | 11,93 ms |
   | Afin plegada, VU0 | 2,23-3,54 ms | 11,51 ms |

   Ahorro ~0,3 ms en el tramo denso: la transformacion y las conversiones no
   eran la mayor parte del coste por imagen. Solo el hilo principal usa la
   VU0 (el de audio no), por lo que no hace falta guardar su estado entre
   hilos. Capturas en el tramo de sierras: render correcto.

8. **Ruta de update: triggers y colores**. Nueva linea de diagnostico
   `GDPS2 FT TRG`, que reparte `triggers` entre `handle_triggers`,
   `trig2_update`, `obj_anim_tick` y colores (`handle_col_triggers`,
   `calculate_lbg`, `trig2_pulse_channels`). Los colores costaban 0,78 ms
   de 1,05 ms en todo el nivel, no solo en el tramo denso.
   - **RGB<->HSV en float** (`libraries/color.c`): `convertRGBtoHSV` y
     `convertHSVtoRGB` usaban literales `double` (`255.0`, `60.0`, `1.0`),
     y todo se calculaba en `double` por software. Las llaman
     `calculate_lbg` y `col_apply_copies`, que cada frame recalcula todas
     las copias de canal con HSV (hasta 48, en dos pasadas). Tambien afecta
     al port PSP.
   - **`handle_triggers` solo encola triggers** (`source/color_channels.c`):
     las secciones guardan todos los objetos, y cada frame se encolaban y
     ordenaban con `qsort` todos los bloques no activados detras del
     jugador, para que `run_trigger` no hiciera nada con ellos. Ahora
     `runs_as_trigger` filtra por los ids que `run_trigger` trata y por
     `trig2_is_trigger` (nueva, `triggers_v2.c`).
   - **Tablas por id** (`source/graphics.c`): `frame_anim_of`,
     `skel_anim_of` y la parte 2.0 de `get_rotation_speed` buscaban
     linealmente en sus tablas por cada objeto y frame; ahora usan un indice
     por id que se construye la primera vez.

   | Variante | `triggers` | colores | `handle_triggers` | update medio | EE ocupada media | Peor ventana |
   |---|---:|---:|---:|---:|---:|---:|
   | Antes | 1,05 ms | 0,78 ms | 0,19 ms | 3,77 ms | 6,70 ms | 11,54 ms |
   | Despues | 0,22 ms | 0,11 ms | 0,04 ms | 2,99 ms | 6,51 ms | 11,03 ms |

   Capturas en tres tramos (fondo magenta, fuego/sierras, monstruos):
   colores correctos.

## Segunda ronda de mediciones

Nuevas lineas del diagnostico: `GDPS2 FT SPR` (tiempo de `draw_objects` y de
los efectos del jugador), `GDPS2 FT IMG` (tiempo dentro de `C2D_DrawImage`),
`GDPS2 FT DRAW` (imagenes, rotadas, recortadas, listas enviadas, cambios de
blending, triangulos C2Di) y `GDPS2 FT BIND` (tiempo en
`gsKit_TexManager_bind` y binds que cambiaron `tex->Vram`).

- En el tramo denso `draw_objects` cuesta 4-5 ms; `C2D_DrawImage` 3-4 ms
  para 650-820 imagenes (~5 us por imagen) y los efectos del jugador ~0,8 ms.
- `gsKit_TexManager_bind` cuesta solo 0,2-0,5 ms por frame.
- **gsKit reubica de verdad 0-3 paginas por frame** (maximo 7), no las 9-18
  que estimaba el LRU de sombra: su politica es mejor que LRU. El problema de
  VRAM en hardware real es menor de lo calculado en la primera ronda.
- `-O3 -fno-math-errno` (variable `GDPS2_EXTRA_CFLAGS`, nueva en
  `check_engine.ps1` y `check_link.ps1`): resultado mixto, media peor
  (6,57 -> 7,09 ms). Descartado; en hardware real el codigo mas grande
  tambien presiona la I-cache de 16 KB.
- El audio (mezcla float + IMA ADPCM) se estima en ~1 ms por frame.
- Comparacion visual base/actual con `PrintWindow`: mismos objetos, sierras
  y monstruos correctos.

Una captura con `PrintWindow` en el tramo final no mostro defectos
(trails de wave, sprites rotados).

## Pendiente: VRAM

El diagnostico tiene ahora un LRU de sombra (`GDPS2 FT VRAM`) y la lista de
paginas por frame (`GDPS2 FT PAGES`). En Deadlocked se usan 16-21 paginas
T8 de 512x512 (256 KB) por frame, con ~2,9 MB de VRAM libre (caben ~11). El
modelo LRU estima 9-18 subidas por frame, pero `GDPS2 FT BIND` muestra que
gsKit solo reubica 0-3 (maximo 7): ~0,25-0,75 MB/frame.

PCSX2 no cobra en tiempo esas transferencias GIF, asi que **no se ve en el
emulador**, pero en una PS2 real puede costar varios ms por frame. Opciones,
de menos a mas invasiva:

- Gestor de texturas propio con politica para acceso ciclico (fijar las
  paginas mas usadas y rotar el resto), con `gsKit_texture_send_inline`.
- Framebuffer mas pequeno (640x224 por campo, o 512x448): libera 1-2 paginas
  a costa de resolucion.
- Atlas de juego reempaquetados para que un nivel use menos paginas.

Solo se pueden validar en hardware real.
