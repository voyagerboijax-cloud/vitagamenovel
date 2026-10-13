# Novela visual para PS Vita y PC

Un motor de novela visual: **tú escribes la historia en un archivo de texto y pones tus imágenes en carpetas; no hace falta programar.** GitHub lo convierte en un `.vpk` para la Vita y en un programa para Windows/Linux.

El ejemplo incluido es "El faro de Cabo Niebla": dos personajes, tres escenarios, elecciones, tres finales, música y un efecto de sonido. Todas las imágenes y sonidos son de relleno hechos para el ejemplo; reemplázalos por los tuyos.

## Qué hay en la carpeta

```
assets/                 <- TODO lo que tú editas está aquí
  guion.txt             la historia (texto)
  epilogo.txt           ejemplo de un segundo archivo (@incluir)
  config.txt            título y pantalla de título
  fuente.ttf            tipo de letra
  fondos/               fondos 960x544 (.png o .jpg)
  personajes/           personajes con fondo transparente (.png)
  musica/               música (.ogg, .wav o .mp3)
  sonidos/              efectos (.wav u .ogg)
sce_sys/                icono y fondo del menú de la Vita
src/                    el motor (no hace falta tocarlo)
CMakeLists.txt          nombre de la app en la Vita (ver abajo)
.github/workflows/      las recetas que compilan en GitHub
```

## Cómo se escribe la historia

Abre `assets/guion.txt` con el Bloc de notas. La parte de arriba del archivo ya explica el formato; en resumen:

```
@fondo muelle_dia                  cambia el fondo        (fondos/muelle_dia.png)
@personaje ana feliz izquierda     muestra a Ana           (personajes/ana_feliz.png)
Ana: Hola, ¿cómo estás?            un cuadro de diálogo
Narrador: Era de noche.            texto sin nombre
@opcion "Saludar" -> saludar       opciones (varias seguidas = un menú, hasta 6)
@opcion "Ignorarla" -> ignorar
@etiqueta saludar                  un lugar al que se puede saltar
@ir final                          salta a otra etiqueta
@sumar confianza 1                 memoria de la historia (números)
@si confianza >= 2 -> final_bueno  salta solo si se cumple
@musica tema   @sonido campana     música y efectos
@quitar ana    @quitar todos       oculta personajes
@incluir capitulo2.txt             mete otro archivo de texto
@fin                               termina
```

Reglas que conviene recordar:

- Una línea de texto es un cuadro de diálogo. Si es muy largo, el juego lo parte en páginas solo. Para forzar un salto de línea escribe `\n`.
- La historia empieza en `@etiqueta inicio`.
- Los nombres de archivo van **en minúsculas, sin espacios ni acentos** (`muelle_dia.png`, no `Muelle Día.png`). Para separar palabras usa guion bajo.
- Los personajes se llaman `nombre_expresion.png`. Para `@personaje lia feliz izquierda` el archivo es `personajes/lia_feliz.png`. Si no escribes expresión, se usa `normal`.
- Máximo 4 personajes a la vez.
- Guarda el archivo como texto UTF-8 (el Bloc de notas de Windows lo hace por defecto).

## Si te equivocas

Al arrancar, el juego revisa todo el guion y te muestra una pantalla roja con **el número de línea y qué pasó**, por ejemplo:

- `guion.txt, línea 45: No encuentro el fondo: falta el archivo fondos/muelle_dia.png`
- `guion.txt, línea 17: No existe la etiqueta 'final'. Crea una línea @etiqueta final`
- `guion.txt, línea 12: Falta cerrar las comillas.`

Puedes continuar de todos modos; lo que falla se salta o se dibuja como un recuadro con el nombre del archivo que falta.

## Cómo conseguir el .vpk y el programa de PC (con GitHub)

1. En tu repositorio de GitHub, sube **el contenido** de esta carpeta (no la carpeta en sí), incluida la carpeta oculta `.github`. Lo más fácil es arrastrar las carpetas y archivos a la ventana del navegador. Si ya tienes archivos de la demo anterior, se reemplazan.
2. Entra a la pestaña **Actions**. Aparecen dos recetas:
   - **Compilar VPK**: genera el archivo para la Vita. Descarga el artefacto `novela-vpk`.
   - **Compilar PC**: genera el programa para Windows (`novela-windows`) y Linux (`novela-linux`). En Windows, descomprime y abre `novela.exe`; deja la carpeta `assets` al lado.
3. Cada vez que cambies algo en GitHub, se vuelve a compilar solo.

La web de GitHub deja subir hasta 100 archivos por vez. Si tu novela crece mucho, súbela en varias tandas.

### Instalar en la Vita

Pasa el `.vpk` con VitaShell (por USB o FTP) e instálalo con **X**. Requiere una Vita con homebrew.

## Editar sin volver a compilar (solo Vita)

El juego busca primero en `ux0:data/NovelaVisual/` y luego dentro del `.vpk`. Si copias ahí `guion.txt` (o cualquier imagen o sonido, con las mismas carpetas `fondos/`, `personajes/`...), **se usa tu copia**. Dentro del juego, pulsa **SELECT** para recargar el guion y ver el cambio al instante. Es muy cómodo para escribir y probar.

Atención: si dejas un `guion.txt` viejo en esa carpeta, ganará siempre sobre el del `.vpk`. Bórralo cuando termines de probar.

En PC pasa algo parecido: edita los archivos de la carpeta `assets` junto al programa y pulsa **F5**.

## Controles

| | Vita | PC |
|---|---|---|
| Avanzar / elegir | X o tocar la pantalla | Enter, Espacio, Z o clic |
| Mover en menús | cruceta ↑ ↓ | flechas ↑ ↓ |
| Ocultar el cuadro de texto | O | X |
| Guardar partida | START | S |
| Volver al título | TRIÁNGULO | Esc |
| Recargar guion | SELECT | F5 |

Hay una sola ranura de guardado. Si cambias el guion después de guardar (añadir o quitar líneas), la partida guardada deja de valer y "Continuar" desaparece.

## Cambiar títulos, nombre e iconos

- **Título de la pantalla inicial**: `assets/config.txt`.
- **Nombre debajo del icono en la Vita**: en `CMakeLists.txt`, la línea `set(VITA_APP_NAME "...")`.
- **Icono (128x128), fondo del menú (840x500) y botón (280x158)**: archivos de `sce_sys/`. Si los reemplazas, guárdalos como PNG.
- **Tipo de letra**: reemplaza `assets/fuente.ttf` por otro archivo .ttf (se incluye la licencia de la fuente actual, DejaVu Sans).

## Estado de esta versión (lo que sí y lo que no se probó)

- **Probado en un entorno de pruebas**: la lectura y validación del guion, el recorrido de todas las ramas y finales del ejemplo, guardar/cargar, y el motor completo corriendo con una simulación de SDL (cientos de partidas aleatorias sin errores de memoria). El diseño de pantallas se revisó con capturas aproximadas.
- **No probado**: la compilación real con VitaSDK y GitHub, ni ejecutar el juego en una Vita o en un PC con SDL real. En el primer intento de compilar puede salir algún error de librerías; si pasa, copia las últimas líneas del paso **Compilar** y se corrige.
