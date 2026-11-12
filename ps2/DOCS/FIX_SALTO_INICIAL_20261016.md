# PS2: pulsacion de entrada al nivel reutilizada como salto

## Sintoma

El primer intento de un nivel empezaba con un salto automatico sin que el
jugador hubiese pedido uno dentro de la partida. No era un autojump
del `build.bat`; en el ELF release no estan activadas las macros de
diagnostico/autonavegacion.

## Causa y reparacion

- `source/menus/level_select.c`: `KEY_A` (CRUZ) entra al nivel.
- `source/main.c`: `jump_key_mask()` incluye `KEY_A`. La transicion entraba
  en `game_loop()` con `sync_precise_input(false)` y la pulsacion del menu
  podia seguir retenida en HID/libpad o en su buffer 240Hz.
- **Solo PS2:** al terminar de cargar el nivel, `hidScanInput()` lee el
  estado ACTUAL del mando. `GDPS2_JumpEntryHeld()` en
  `ps2/src/ps2_jump_entry.h` arma la supresion solo si `KEY_A`, `KEY_UP` o
  `KEY_TOUCH` siguen retenidos. Anteriormente el bloqueo comenzaba en
  `true` incondicionalmente y podia consumir el primer salto legitimo,
  aun cuando CRUZ ya se habia soltado durante la carga.
- Con mando libre, `sync_precise_input(false)` acepta el primer `DOWN`
  desde el primer fotograma. Con una tecla heredada aun retenida, se usa
  `sync_precise_input(true)` y se ignora solo hasta su liberacion. No hay
  temporizador ni se bloquean los respawns.
- `source/utils/precise_input.c` corrige la variante 240Hz de PS2: el
  bloqueo de una entrada retenida solo termina ante `UP`/release, no ante
  otro evento `DOWN`. Si la entrada estaba libre al hacer la transicion,
  el primer toque nuevo funciona sin ser consumido.

Este bloqueo se aplica al **ingreso del nivel**, no a cada respawn:
despues de morir, el comportamiento de mantener el boton para saltar
continua siendo el normal del juego.

## Verificacion

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\ps2\tools\test_ps2_aspect_and_jump.ps1
```

`ps2/tests/input_release_smoke.c` compila e incluye el codigo real de
`precise_input.c`, con una simulacion de HID para cubrir: boton del menu
retenido -> no salto, nuevo `DOWN` durante suppression -> no salto,
`UP` -> liberacion, siguiente `DOWN` -> salto, y primer `DOWN` con mando
sin botones retenidos -> salto valido. Incluye regresion de armado normal:
un mando inicialmente libre NO activa el bloqueo de primer fotograma.

Un smoke de host no sustituye la comprobacion de primer salto con mando
real/PCSX2, que sigue pendiente.

## Correccion adicional: PS2 mando digital bloqueaba el salto

El usuario confirmo que seguia sin poder saltar en PCSX2 despues del fix
anterior. Se hallo un defecto independiente en `ps2/src/ctru_ps2_input.c`:
el puente libpad interpretaba `pad.ljoy_h` y `pad.ljoy_v` incluso cuando el
mando reportaba modo digital (`pad.mode=0x41`). En modo digital esos bytes
pueden valer 0; se decodificaban como `KEY_CPAD_LEFT | KEY_CPAD_UP` retenidos
indefinidamente. Como `jump_key_mask()` incluye `KEY_UP`, el bloqueo de
entrada podia quedarse activado y anular incluso nuevas pulsaciones de CRUZ.

- `ps2/src/ps2_pad_decode.h` centraliza la lectura de botones: solo usa
  ejes analogicos cuando el modo libpad aporta datos de stick (0x73/0x79),
  no en digital 0x41. CRUZ y cruceta siguen funcionando en digital.
- `ps2/src/ps2_jump_entry.h` limita el bloqueo inicial a CRUZ si CRUZ
  realmente sirve para saltar. Arriba (D-pad/stick) no es la tecla que
  confirma la entrada del selector, por lo que nunca debe dejar bloqueadas
  otras entradas de salto.
- `ps2/tests/pad_decode_jump_smoke.c` ejercita el decodificador usado por
  el PS2 y comprueba la regresion de modo digital con ejes cero, presiones
  de CRUZ, cruceta, ejes analogicos validos y bloqueo/liberacion de menu.
- `test_ps2_aspect_and_jump.ps1` ejecuta este test junto a los existentes.

El release ahora imprime una sola vez el modo leido del mando en el log
de PS2 (`GDPS2 PAD: ... mode=... analog=...`), para poder confirmar
la configuracion real de PCSX2 sin spam por fotograma.

La explicacion es una causa verificable de bloqueo si PCSX2 devuelve
modo digital con ejes cero; no se afirma que el mando del usuario haya
devuelto exactamente esos bytes sin inspeccionar la ejecucion real.
