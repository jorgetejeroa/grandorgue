# Órgano SL 300 — app Android

App para manejar el **Ahlborn SL 300** de la parroquia de San Adrián desde la tablet. Es la misma app que «Órgano Praeludium» 0.2 (rama `app-praeludium`), adaptada a los canales, registros y combinaciones del SL 300. Las dos apps se pueden tener instaladas a la vez.

Esta rama (`app-sl300`) es independiente de Grand Orgue: solo contiene la app.

## Qué hace
- **Conexión** por cable MIDI-USB o Bluetooth (WIDI). Recuerda el último dispositivo y se reconecta sola; si se corta, pausa, avisa y reintenta.
- **Carpeta** propia en la tablet, con subcarpetas.
- **Piezas:** un toque carga, mantener pulsado toca ya; tocar otra mientras suena la deja como siguiente.
- Reproducir / Pausa, Detener, Anterior, Siguiente, línea de tiempo (al saltar se ponen los registros y volúmenes de ese momento).
- **Tempo** 25–200 % y **tono** ±6, recordados por pieza; guardar con ese tempo y tono (copia del original en «Originales»).
- **Volumen** (CC7 de las cuatro secciones), **Terminar suavemente**, **Silencio**.
- **Paso automático**, **repetir lista**, **marcadores** y **repetir un tramo**, **listas** Misa / Libre (`.golist`).
- **Combinaciones 1–5** desde la tablet.
- **Registros que suenan**, por su nombre, en cuatro columnas: Pedal, Positivo, Grand'Organo, Recitativo.
- Al terminar o detener, **apagar los registros que puso la pieza** (casilla).
- **Grabar** lo que se toca en el órgano a un .mid.
- **Letras en la tele** (`<pieza>.letras`), igual que en la app del Praeludium.

**El traspositor del órgano tiene que estar a 0** para reproducir y para grabar.

## Cómo conseguir el APK
Cada cambio en esta rama lo compila GitHub (pestaña **Actions**) y lo publica en **Releases** como `OrganoSL300-0.1.N.apk`. Desde la tablet: abrir la página de Releases, descargar el .apk e instalarlo. Las versiones nuevas se instalan encima.

## Implementación MIDI del SL 300
- Canales: 1 Recitativo III, 2 Grand'Organo II, 3 Positivo I, 4 Pedal, 16 común. Do grave = nota 36.
- Registros: **CC70** en el canal de la sección; valor = índice + 64 enciende, valor = índice apaga. La tabla está en `Stops.kt`.
- Trémolos: **CC92** (127 / 0) en los canales 3 (Positivo) y 1 (Recitativo).
- Combinaciones 1–5: **Program Change 1–5 en el canal 16**, sin Bank Select.

## Pendiente de comprobar en el órgano
1. Que cada índice de la tabla corresponde al registro que dice (se sacó suponiendo que se pulsaron en el orden del panel).
2. Si el CC7 de cada sección baja el volumen.
3. Si el órgano enciende la luz de un registro cuando le llega por MIDI.
4. Si existe un pistón de anular / registración manual y qué manda.
