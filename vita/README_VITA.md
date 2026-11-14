# UMK3 -> Vita (v2)

Cambio de enfoque: ya NO hay un CMakeLists aparte. El build de Vita reutiliza el del repo
(umk3-game + tablas del .ipa) y solo cambia plataforma + empaquetado.

1. sudo apt install ninja-build        (o deja que build.sh use Makefiles)
2. python3 apply_cmake_patch.py        (4 inserciones en ../CMakeLists.txt, copia en .orig)
3. ./build.sh [../UMK3.ipa]            -> ../build-vita/umk3.vpk  y  ./data/
4. Instalar el VPK (VitaShell) y ./deploy_data.sh IP_VITA  -> ux0:data/umk3/

Archivos: vita.cmake (backend), src/vita_plat.c (platform.h completo, WEAK),
src/vita_rgl.c (reemplaza rgl_front.c), src/vita_fs.c (rutas relativas -> ux0:data/umk3/).

`deploy_data.sh` también instala `vita.cfg`. Sin claves en `vita.cfg`, los pools de vitaGL son de 12 MB
circulares, 4 MB legacy y umbral de 8 MB (`circular_pool_mb`, `legacy_pool_mb`, `ram_threshold_mb`); se pueden ajustar allí. Si una
caída viene precedida por `vram_libre`, `ram_libre` y `phy_libre` cerca de
0 KB en `ux0:data/umk3/vita_log.txt`, eso apunta a agotamiento de pools gráficos,
no basta por sí solo para afirmar una fuga del heap de CPU.

## Cargas más rápidas (2026-11-04)

Una pelea tardaba más de 1 min 10 s en cargar. Cambios, **sin probar aún en una Vita**:

1. **`umk3.pak`**: abrir un archivo de `res/` en la tarjeta cuesta del orden de 100 ms (≈1900 archivos en una
   sola carpeta) y una pelea abre cientos. `make_pak.py` junta los 5050 archivos de solo lectura (333 MB) en
   `data/umk3.pak`; `vita_fs.c` lee su índice una vez y sirve cada `fopen("res/...","r")` con un `sceIoPread`.
   Partidas, `.mp3`, mods y lo que no esté en el paquete siguen leyéndose sueltos. `build.sh` lo genera;
   `./deploy_data.sh IP` sube **solo** el paquete (no toca partidas ni `vita.cfg`). Deja `res/` en la Vita como
   está. `pak=0` en `vita.cfg` lo desactiva. Si cambias algo en `data/res`, vuelve a ejecutar `make_pak.py`
   (el paquete gana sobre el archivo suelto del mismo nombre).
2. **Relojes al máximo**: CPU 444, bus 222, GPU 222, xbar 166 MHz (una app arranca a CPU 333). `clock=0` los deja.
3. `vita.cfg` se lee una sola vez (antes, ~20 aperturas al arrancar).
4. **Medida**: cada carga deja en `vita_log.txt` una línea `CARGA fight: N s aperturas=… del_paquete=…`; con
   `prof=1` añade cuánto fue fopen, fread, decodificación PVR/PNG y conversión a 16 bits.

## Sangre agrupada, registro más ligero (2026-11-14)

Cambios **sin probar aún en una Vita** (compilan sin avisos; el VPK se genera):

1. **Gotas de sangre agrupadas** (`src/runtime/draw_gl.c`, `facebatch=1` por defecto). Antes cada gota era una llamada de dibujo
   (~40 µs) y, para no hundirse en los combos, `facemax=200` dibujaba solo 1 de cada N gotas. Ahora, dentro de
   `MaintainParticles` (envuelta con `-Wl,--wrap`), las gotas se juntan por textura y se dibujan con un `glDrawArrays` por textura
   (~15 llamadas en vez de miles), así que `facemax` pasa a 0 por defecto y se ven **todas**. Lo único distinto: el orden entre
   gotas de texturas distintas que se tapan. `facebatch=0` vuelve a lo de antes (y `facemax` a 200). Con `prof=1`,
   `vita_log.txt` añade `gotas: llamadas_de_dibujo=… vistas=… dibujadas=…`.
2. **`vita_log.txt`** se abre una vez y queda abierto (antes: abrir/cerrar en la tarjeta cada 2 s). Pasando de 2 MB al arrancar se
   guarda como `vita_log_old.txt`. `log=0` quita la línea periódica.
3. **Solo con `diag=1`**: `missing.txt` y la vigilancia de cada fotograma (`vita_watch_frame`: `corrupt.txt`, volcado de hilos con
   L+R+Select).
4. El panel trasero se lee una vez por fotograma (antes hasta 4).

5. **Kombos que se cortan**: los avisos del juego `separate us:` (regla de la cadena), `corner trap:` (regla de la esquina) y
   `death scream:` ahora salen en `vita_log.txt` como `juego: fotograma=… …` (antes `printf` no se veía en la Vita).

6. **`kombo_slide=0`** (traído de la versión 0.2.0 del repositorio original): un kombo ya no separa al rival a mitad. Por defecto
   es 1, como el juego original; solo fuera de línea. También vale en `umk3.ini`. Las líneas `kombo push:` del registro dicen
   cuándo un golpe lo habría separado.

7. **Kombos que empiezan igual** (de la versión 0.1.17 del repositorio original): manteniendo ARRIBA se toma el segundo kombo
   con el mismo botón (Mileena: HK, HK, ↑+LK, ↑+HK; también Kabal, Sonya, Stryker y Kano).

Si algo se ve mal o va peor: `facebatch=0` en `vita.cfg` y mándame `vita_log.txt` con `prof=1`.
