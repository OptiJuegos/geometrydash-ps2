# Geometry Dash PS2: conversion de interfaces 3DS al display 4:3

## Causa

El motor 3DS contiene pantallas TOP de 400x240 y BOTTOM tactiles de
320x240. En PS2 se dibujan ambas sobre el mismo GS NTSC 640x448 y el
backend aplica el recorte logico central x40..360 (320x240). Dibujar los
dos fondos o ventanas originales suele ocultar botones y texto.

## Criterios de conversion

- Una sola composicion visible 320x240, normalmente en la capa BOTTOM (el
  backend aplica x+40). Si se necesita TOP, limitar su contenido al
  area logica x40..360 y evitar fondos que tapen botones BOTTOM.
- Conservar `action`, `tag`, `keyBinds`, controles y referencias a datos.
- Ocultar elementos 3DS con `ps2hide=1` en lugar de borrarlos cuando el
  codigo accede a ellos mediante tags. No confundir invisibilidad con
  eliminar estado del progreso o monedas.
- Cambiar la carga solo bajo `__PS2__`, sin alterar los archivos 3DS/PSP.
- No crear entrada automatica, convertir medios ni modificar guardados.

## Pantallas convertidas en este grupo

| Pantalla | Ficheros PS2 | Adaptacion |
|---|---|---|
| Tutorial (`how_to_play`) | `how_to_play_ps2.txt`, `how_to_play_top_ps2.txt` | Un panel con ilustracion, instruccion y Next/Exit juntos para las 8 paginas |
| Final de nivel (`level_complete`) | `level_complete_ps2.txt`, `level_complete_top_ps2.txt` | Ventana de recompensa compacta y botones Restart/Exit abajo; conserva 3 monedas, animaciones, estrellas, intentos y saltos |
| Detalle de nivel externo | `external_pop_up_ps2.txt`, `external_pop_up_top_ps2.txt` | Un unico fondo, cabecera/datos compactos y controles Play/Info/Exit + progreso visibles |
| Informacion de nivel online | `online_level_info_pop_up_ps2.txt`, `online_level_info_pop_up_top_ps2.txt` | Datos y boton OK juntos en BOTTOM; TOP vacio para evitar superposicion |
| Soggy | `soggy_ps2.txt`, `soggy_top_ps2.txt` | Un solo arte 320x240, con acciones Exit y Boop |

La revision de orden de capas encontro y corrigio dos excepciones:
`external_popup_draw_top()` no vuelve a dibujar la metadata del padre si
la modal `external_level_infobox` esta abierta sobre PS2; y el filtro
`endDarken` bottom de `level_complete` queda oculto (con tag conservado),
pues la pantalla bottom se dibuja DESPUES del resultado top. Una capa
oscura propia del top se dibuja antes del panel de recompensas.

El infobox online original no asigna ningun dato real a los campos de
fecha, nombre o progreso: en PS2 se muestra un mensaje de no disponibilidad
en vez de inventar valores. Se conservan sus tags con `ps2hide=1` para
permitir conectarlos mas adelante cuando exista una fuente de datos real.

Las pantallas de una sola capa BOTTOM del motor (por ejemplo ajustes,
estadisticas, cancionero, creditos y algunos modales) ya usan coordenadas
320x240 y se componen centradas a x+40. No hay necesidad de añadirles una
segunda capa. El selector de niveles, gameplay/pausa y loading ya tenian
variantes PS2 en trabajos anteriores.

## Validacion

`python ps2/tools/test_ps2_extra_menus.py`: verifica las rutas especificas
PS2, ilustraciones dentro de 320x240, tags de estado/recompensa, botones,
ausencia de la ventana TOP que tapa los controles y la coincidencia con
ROMFS empaquetado. No es una prueba de mandos reales ni de composicion GS
en PCSX2: pendiente validar con capturas del usuario cada pantalla.
