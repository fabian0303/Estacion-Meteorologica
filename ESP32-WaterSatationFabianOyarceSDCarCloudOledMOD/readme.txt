Estación meteorologica

Componentes:

- BME280
- BH1750
- Anemometro
- Veleta
- Pluviómetro
- ESP32
- Pantalla Oled SSD1306
- Adaptador MicroSD + Micro SD 32GB

Mide:

- Humedad
- Temperatura
- Presión átmosferica
- Luminosidad
- Velocidad del viento
- Dirección del viento
- Cantida de lluvia caída

Los datos se envian a la nube mediante el protocolo mqtt haciendo uso de las API de ThingSpeak.

Los datos se guardan en la tarjeta de memoria y se genera un nuevo archivo cada vez que se inicia el esp
se utiliza como nombre para el archivo la fecha y hora de inicio.

Se hace uso de reloj consultado la hora al comienzo del sistema al servidor en la nube
Se toman datos cada 5 segunodos y cada 30 se envian a la nube.

Mejoras:

Los datos se guardan en la tarjeta de memoria y se genera un nuevo archivo cada vez que se inicia el esp
se utiliza como nombre para el archivo la fecha y hora de inicio.

Para no perder los registros de lluvia cada vez que se inicia el esp se consulta a la nube por el ultimo valor
de lluvia acomulada.

Se elimia ruido por altos de voltaje en velocidad de viento y lluvia caida