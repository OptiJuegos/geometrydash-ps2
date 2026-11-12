# PS2: selector 4:3 negro tras migrar las pantallas de 3DS

## Reproduccion y causa

El usuario mostro el selector de niveles con fondo totalmente negro,
solo los puntos inferiores, un contorno amarillo de hover y el cursor.
Los puntos son de `draw_dots()` y no forman parte del menu principal.

En `source/menus/level_select.c`, el orden es BOTTOM (fondo, tarjeta y
botones), seguido por `begin_top_eye(eye)` y `draw_dots()` en TOP.
`begin_top_eye()` en `source/utils/gfx.c` invoca `C2D_TargetClear(top)`.
En PS2 esta funcion estaba haciendo una limpieza opaca con
`gsKit_prim_sprite` de los 640x448 pixeles, por lo que borraba toda
la interfaz BOTTOM del mismo frame. Los puntos se dibujan despues y
se veian, al igual que el hover que se pinta en `C3D_FrameEnd()`.

Antes, las estadisticas visibles pertenecian a TOP y sobrevivian, por
eso el error quedo expuesto al trasladar los controles a BOTTOM.
El fallo tambien afecta a otros menus que dibujan BOTTOM antes de TOP.

## Correccion

`ps2/src/c2d_ps2.c`: `C3D_FrameBegin()` limpia el framebuffer GS una
sola vez por frame. `C2D_TargetClear()` no vuelve a limpiar cuando el
target virtual cambia de TOP a BOTTOM o viceversa. En PS2 ambos son
vistas del mismo framebuffer fisico, sin render targets independientes.
No se cambia el layout de niveles ni el zoom 4:3, ni el hover/mando.

`python ps2/tools/test_merged_target_clear.py` comprueba las rutas de
limpieza y los dos ordenes de dibujo utilizados (TOP->BOTTOM en el menu
principal, BOTTOM->TOP en el selector). No reemplaza la prueba visual
en PCSX2, que sigue pendiente. Conservar el renderizado de PSP/3DS.
