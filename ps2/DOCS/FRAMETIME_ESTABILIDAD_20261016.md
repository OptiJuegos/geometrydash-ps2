# Geometry Dash PS2: frametime y tirones con 60 FPS

## Problema informado

Al avanzar aproximadamente a la mitad de un nivel el movimiento parece dar
tirones, aunque el contador indica 60 FPS. La tasa de frames por segundo no
indica la varianza de tiempo de cada frame; hace falta registrar tiempos de
la EE y del GS, especialmente en niveles con mas objetos y animaciones.

## Cambios normales aplicados

1. **Fisica con pasos uniformes (PS2)** en `source/main.c`. La simulacion
   trabaja a 240 Hz. El codigo 3DS incrementaba `frame_skipped` cuando un
   paso fisico tardaba mas de 1/240 s; `STEPS_DT` depende de ese valor.
   En el siguiente paso podia aumentar la distancia simulada y restar una
   cantidad mayor del acumulador, visualmente como un salto. El port PSP ya
   desactiva este aumento. PS2 ahora usa tambien siempre pasos de 1/240 s
   y deja que el acumulador controle cuantos hacen falta; **no se elimina**
   logica de colision, triggers ni calculo fisico.
2. **Sprites sin giro:** `ps2/src/c2d_ps2.c` ya dispone de la primitiva GS
   `SPRITE` para quads sin rotacion/shear, pero antes calculaba `cosf` y
   `sinf` y las cuatro esquinas incluso en ese caso. La ruta optimizada
   transforma solo dos esquinas sin trigonometria, manteniendo `sprite`
   y usando el camino anterior para rotaciones y transformaciones invertidas.
   La ruta de rotacion calcula `tint_to_gs` una vez para ambos triangulos.
3. `C2Di_CalcQuad` omite coseno/seno en la ruta `angle == 0.0f`. El caso
   rotado permanece sin cambio.

Son cambios de bajo riesgo, pero **no hay todavia una medicion de mejora
real en la mitad del nivel**: la instancia de PCSX2 del usuario estaba en
ejecucion. No se interrumpio su proceso ni se lanzo una instancia competidora.

## Diagnostico opcional de tiempos, independiente del release

Para generar un ELF con telemetria cada 120 frames y entrada automatica a
Theory of Everything 2 (indice 17), desde la raiz del repositorio:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\ps2\tools\build_engine.ps1 -DiagnosticFrameTiming -DiagnosticAutoInput -DiagnosticLevelIndex 17 -Output .\ps2\out\gdps2-diag-frametime-toe2.elf
```

**Nunca usar ese ELF como release**: tiene navegacion automatica
exclusivamente para pruebas. El `build.bat` no incluye esta instrumentacion.

Si PCSX2 expone `host:` desde `ps2/out/`, los recursos estan en
`host:package/romfs/`. Abrir el ELF diagnostico y observar su consola:

Para reproducir el tiron **con controles manuales y sin autojump, noclip ni
automatizacion** utilizar un segundo ELF diagnostico:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\ps2\tools\build_engine.ps1 -DiagnosticFrameTiming -Output .\ps2\out\gdps2-diag-frametime-manual.elf
```

- `GDPS2 FT: frames=120 ...` muestra media/max de frame completo y
  tiempos de actualizacion vs render. `over20` cuenta frames por encima
  de 20 ms; `over33` los que excedieron 33,3 ms; `max_physics_steps`
  muestra acumulacion de pasos. Un promedio de ~16,7 ms no descarta picos.
- `GDPS2 FT GS: frames=120 ...` divide el final de frame en vaciado de
  geometria, envio de comandos, `gsKit_sync_flip` y mantenimiento de
  `gsKit_TexManager_nextFrame`. `binds_avg/max` cuenta solicitudes de bind,
  **no transferencias reales de VRAM**.
- Si `update_max` aumenta en mitad del nivel, perfilar mas finamente
  `handle_triggers`, `create_objects`, particulas y colisiones. Si domina
  `flip_max`, considerar transferencias de atlas, sincronizacion GS, GIF,
  VSync o carga de PCSX2; si domina `tex_max`, investigar TexManager.
- Si todo esta alrededor de 16,7 ms, pero persisten saltos, investigar
  pasos fisicos, camara, desplazamiento de objetos y exactitud del clock.

No activar trazas de cada sprite/frame en la compilacion publica. Las
mediciones con `printf` frecuentes pueden introducir el problema a medir.

## Medidas registradas en PCSX2-Sanity (diagnostico)

Se ejecuto el motor original con arranque de ToE2. El primer test sin noclip
se reiniciaba periodicamente por muerte, con efecto WAV cada ~2,37 segundos.
Los picos de actualizacion de 89-96 ms coincidieron con esa condicion: no
deben confundirse con los tirones observados por un jugador vivo a mitad
de nivel.

Un segundo ELF de diagnostico activo `state.noclip=true` **solo para medir**
zonas con mas objetos sin reinicios (NO incluido en release):

| Ventana de 120 frames | Solicitudes de textura/frame | Frames >33,3 ms | Pico de frame |
|---|---:|---:|---:|
| Tramo denso inicial sin deduplicar | 667 | 6 | 33,35 ms |
| Mismo recorrido con deduplicacion experimental | 671 | 5 | 33,35 ms |
| Tramo de menos objetos sin deduplicar | 33 | 0 | 19,95 ms |

En la zona densa la actualizacion media fue ~4,8 ms, pero el render/espera
de GS tuvo picos ~27,9 ms, compatibles con perdida puntual de vblank. El
contador de FPS puede continuar redondeando a 60 aunque haya dobles frames.

En el tramo denso, alrededor de 521 de 667 binds/frame eran solicitudes
consecutivas a la misma pagina. Se probo omitir esas repeticiones **solo**
con `-DiagnosticBindDedup` en `ps2/src/c2d_ps2.c`. La mejora observada
(6 -> 5 fotogramas lentos en la ventana) fue pequena y puede ser ruido:
**no se habilito en release**, ya que se requiere una prueba visual de
cache/eviccion y mas muestras comparables antes de alterar gsKit.

Logs aislados en `ps2/out/audio-acoustic/volume100/`:
`gdps2-frametime-toe2-isolated.log`,
`gdps2-frametime-toe2-noclip-isolated.log`,
`gdps2-frametime-texrepeat-isolated.log` y
`gdps2-frametime-dedup-isolated.log`.

La prueba con noclip explora el nivel, pero no verifica un porcentaje exacto
del recorrido; para reproducir el caso real hay que ejecutar la compilacion
de diagnostico manual y avanzar jugando normalmente hasta la mitad.

## Verificacion y limites

Los cambios se compilan usando el motor original (95 unidades de juego,
107 objetos totales) con `build.bat`, sin autojump/autonav ni tests dentro
del script release. Se genera `ps2/out/release/GeometryDashPS2.elf` y
`ps2/out/release/romfs/` con musica MP3 y efectos WAV originales.

La configuracion del emulador tambien puede aportar stutter (host rendering,
compilacion de shaders, velocidad del equipo), por lo que la telemetria de
EE/GS y la grafica de frametime del emulador deben compararse antes de
atribuirlo exclusivamente al port.
