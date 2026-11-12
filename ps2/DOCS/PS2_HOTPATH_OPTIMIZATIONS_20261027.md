# Optimizaciones PS2: sprites, mixer y carga de niveles (27/10/2026)

Estas mejoras se implementaron sobre el port PS2 sin cambiar el formato
MCPK ni las reglas de colision/fisica. El codigo de PSP/3DS conserva
los caminos de ordenamiento y de separacion de texto anteriores.

## 1. Ordenamiento de sprites

- `source/graphics.c`: variante experimental para 2 a 16 sprites
  visibles, que usa `gd_stable_sort_small()` (insercion estable).
  **Desactivada por defecto desde el reporte de tirones del usuario**.
  Para probarla: `-DGDPS2_EXPERIMENTAL_SMALL_SORT`.
- Para mas de 16 sprites mantiene el radix preexistente. Ordena por los
  24 bits bajos de la clave, como el radix, y respeta el orden de
  entrada cuando hay claves iguales.
- Test: `sprite_sort_small_smoke.c`, 136.000 ordenamientos sinteticos
  comparados con un radix de referencia, incluyendo empates y bits
  altos de la clave. **PASS**.

## 2. Mixer NDSP / audsrv

- `ps2/src/ndsp_mixer.c`: variante experimental que precalcula
  `rate / PS2_NDSP_OUTPUT_RATE` de cada canal **activo** una vez por
  bloque. **Desactivada por defecto desde el reporte de tirones**.
  Para probarla: `-DPS2_MIXER_ENABLE_STEP_CACHE`.
- El mezclador generico reutiliza ese incremento de fase en las
  muestras del bloque. La ruta rapida estereo que ya existia no cambia.
- Sin flags se conserva el camino anterior, y el camino nuevo solo se
  activa de forma explicita. Si el mixer solo procesa musica estereo,
  el fast path preexistente ya precalcula el avance de fase.
- Test: `mixer_fast_reference.c` compilado en ambos modos, forzando
  el mezclador generico. Salida PCM, estados de cola y fase final
  identicos. **PASS**.

## 3. Parser de niveles (PS2)

- `source/level_loading.c`: `split_string()` delega a
  `gd_split_tokens()` solamente en PS2.
- Primero cuenta tokens no vacios, reserva una unica tabla de punteros
  y despues asigna cada token. Se evita un `realloc()` por fragmento;
  `free_string_array()` conserva el mismo contrato.
- La lectura del texto hace dos pasadas, por lo que la ganancia es
  principalmente menor cantidad de realojamientos y fragmentacion
  del heap, no un cambio en el formato de niveles.
- Test: `level_split_smoke.c` comparado contra el parser anterior,
  5.000 entradas aleatorias, cadenas vacias, delimitadores repetidos
  y un nivel sintetico de 5.000 objetos. **PASS**.

## Probar

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ps2/tools/test_hotpath_optimizations.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File ps2/tools/build_engine.ps1 -PackagePak -Output ps2/out/hotpath-pak-release/GeometryDashPS2.elf
python ps2/tests/pak_iso_smoke.py --elf ps2/out/hotpath-pak-release/GeometryDashPS2.elf --pak ps2/out/hotpath-pak-release/assets.pak --iso ps2/out/hotpath-pak-release/GeometryDashPS2-PAK.iso
```

**Pendiente:** comparar FPS y tiempos de carga en PCSX2 y PS2 fisica.
No atribuir ningun porcentaje de rendimiento a estos cambios sin un
registro A/B con la misma escena/medio. Para FPS usar los contadores
de frame timing existentes del juego (fisica, object creating, sort,
draw, audio y GS). El ahorro del ordenamiento pequeno depende de que
realmente haya <=16 sprites; el del mixer depende de la utilizacion
del camino generico y la cantidad de canales activos.

## Aislamiento de tirones: ISO de recuperacion

Despues de que el usuario noto mas tirones con la ISO
`ps2/out/hotpath-pak-release/GeometryDashPS2-PAK.iso`, se desactivaron
por defecto exclusivamente las dos rutas experimentales de render/audio.
La variante `ps2/out/hotpath-rollback-release/` mantiene el PAK
sector-alineado, el lector POSIX y el optimizador del parser de niveles,
pero vuelve a radix sort y a la division de velocidad de audio originales.
Comparar ambas ISOs en el mismo nivel y sin cambiar la configuracion de
PCSX2 ayuda a confirmar si los cambios nuevos eran la causa. No
atribuir la regresion sin trazas de FPS y de cola de audio.

Siguientes oportunidades a evaluar con trazas: ahorrar el formateo
`%.*f` del HUD cuando muestra dos decimales; reducir cambios y
subidas de texturas GS; cachear valores invariantes por bloque de
fisica y agrupar la carga de texturas sin duplicar buffers grandes.
