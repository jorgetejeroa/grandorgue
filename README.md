# Órgano Praeludium — app Android

App para manejar el **Ahlborn Praeludium III** desde la tablet: conectar por Bluetooth (WIDI Master) o por cable MIDI-USB, **grabar** lo que se toca y **reproducir** los .mid de una carpeta de la tablet.

Esta rama (`app-praeludium`) es independiente de Grand Orgue: solo contiene la app.

## Versión 0.1
- Conectar: botón **Conectar** → elegir el dispositivo o **Buscar el órgano por Bluetooth…**. La app recuerda el último y se conecta sola al abrirla.
- Carpeta: botón **Carpeta** → elegir o crear, por ejemplo, `Documentos/Órgano Praeludium`. Se puede entrar en subcarpetas.
- Piezas: un toque carga la pieza; mantener pulsado la toca ya. Si está sonando una pieza, un toque en otra la marca como siguiente.
- Reproducir / Pausa, Detener, Anterior, Siguiente, línea de tiempo (al saltar se ponen los registros de ese momento).
- Tempo (25 %–200 %), tono (−6 a +6 semitonos, solo lo que reproduce la app), volumen general del órgano.
- Paso automático (templo abierto) y repetir la lista.
- Terminar suavemente (baja el volumen general en 5 s) y Silencio (corta notas colgadas).
- **Grabar**: pulsar Grabar, poner la registración o la combinación, tocar, y pulsar Parar. Se guarda como .mid en la carpeta que se está viendo. Queda además una copia interna de la última grabación.

## Cómo conseguir el APK
Cada cambio en esta rama lo compila GitHub (pestaña **Actions**) y lo publica en **Releases** como `OrganoPraeludium-0.1.N.apk`. Desde la tablet: abrir la página de Releases del repositorio, descargar el .apk e instalarlo (la primera vez Android pide permitir instalar apps de Chrome o de Archivos). Las versiones nuevas se instalan encima.

## Datos técnicos
- Kotlin, sin librerías externas. minSdk 29 (Android 10).
- Implementación MIDI del órgano: canales 1 Recitativo, 2 Gran Órgano, 4 Pedal, 16 Común; registros por NRPN (CC99 0/64 = apagado/encendido, CC98 banco, CC6 registro); combinaciones generales por Bank Select (CC0 = 64, CC32 = 0–3) + Program Change en el canal 16; CC7 volúmenes.
