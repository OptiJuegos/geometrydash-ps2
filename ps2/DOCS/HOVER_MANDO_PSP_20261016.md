# PS2: hover de botones con mando, estilo PSP

## Objetivo

En PS2, los botones tenian bindings directos pero no seleccion visual ni
navegacion con foco como en PSP. Se adapto el mecanismo de PSP al display
unificado 4:3, sin tocar el codigo de ctru_psp.c ni cambiar otros targets.

## Implementacion

- `ps2/src/ctru_ps2_input.c`: registra hitboxes de la UI del fotograma
  anterior, elige el mas cercano y mueve el foco con cruceta, o mueve el
  cursor libremente con stick izquierdo en modo analogico. Cruz se traduce
  a `KEY_TOUCH` mientras hay cursor en los menus, para activar SOLO el
  elemento apuntado; durante gameplay normal Cruz sigue siendo `KEY_A`.
- Conserva direcciones asociadas a acciones explicitas, como las flechas
  del selector de niveles. Al seleccionar un slider, Izquierda/Derecha
  lo cambia por pasos, sin tocar la navegacion de la partida.
- Cuando el stick mueve el cursor en un menu, no emite simultaneamente
  CPAD_UP/DOWN/LEFT/RIGHT a los botones (evita cambiar de nivel al mover
  el cursor). En gameplay conserva las entradas analogicas normales.
- `source/menus/core/ui_screen.c` y `ps2/src/ps2_ui_nav.h`: registran
  botones, iconos, checkboxes, windowbuttons, colores y sliders activos;
  respetan pantallas bottom 320x240 (composicion a x+40), top 400x240,
  elementos ocultos y limites de listas desplazables.
- `source/menus/components/ui_button.c`: conserva la animacion hover
  original, pero en PS2 impide que se activen dos hitboxes superpuestos
  o el boton antiguo al cambiar de foco.
- `ps2/src/c2d_ps2.c`: contorno amarillo de foco y pequena cruz sobre
  el cursor, dibujados con coordenadas reales de 4:3 solo en menus/pausa.
- Al dejar el menu mientras Cruz sigue pulsado, se suprime SOLO esa
  pulsacion hasta soltarla. No se bloquea el siguiente salto nuevo.

## Selector de iconos: cruceta vs cambio de pagina

La version PS2 asignaba `LEFT` y `RIGHT` a las flechas `icons_left` y
`icons_right` de `romfs/menus/icon_kit_ps2.txt`. Como el navegador PS2
respeta bindings direccionales, esas dos teclas cambiaban de pagina en
vez de recorrer los 7 iconos de cada fila de la cuadricula 7x3.

Solo en PS2 se separaron los controles:

- Cruceta Izquierda/Derecha: desplaza el hover dentro de la fila; Arriba/
  Abajo cambia de fila; Cruz selecciona el icono bajo el cursor.
- L1/R1: cambia la categoria de iconos (cubo/nave/bola/etc.).
- L2/R2: pasa a la pagina anterior/siguiente de iconos dentro de la
  categoria; las flechas laterales se pueden activar tambien con Cruz.
- `ps2/src/ps2_pad_decode.h` ahora mapea PAD_L2/PAD_R2 a KEY_ZL/KEY_ZR.
- `source/menus/icon_kit.c` no vuelve a asignar KEY_ZL/KEY_ZR a cambios
  de categoria en PS2. PSP/3DS mantienen sus binds originales.

`test_ps2_aspect_and_jump.ps1` prueba los 7 iconos por fila y tres filas
en `nav_hover_smoke.c`, ademas de L1/R1/L2/R2 de libpad en
`pad_decode_jump_smoke.c`. `test_ps2_creator_icon.py` impide regresar
a los bindings `LEFT`/`RIGHT` de cambio de pagina.

## Verificacion y limites

`ps2/tools/test_ps2_aspect_and_jump.ps1` agrega
`ps2/tests/nav_hover_smoke.c`: verifica la busqueda del foco mas cercano,
la navegacion entre botones reales del menu PSP fusionado, la exclusion de
hitboxes recortadas/filas desplazadas, que cerrar un menu con Cruz no
produzca un salto fantasma y que el siguiente salto no se pierda.

El smoke comprueba el filtro de transicion, pero no prueba visualmente
las pantallas renderizadas ni la ergonomia del mando. Confirmar con
PCSX2: menu principal, selector de nivel, submenus, sliders, pausa,
salida de pausa y salto normal en gameplay. No tocar guardados.
