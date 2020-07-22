#include <BH1750.h>
#include <Wire.h> 
#include "Adafruit_BME680.h"
 
#define PIN_ANEMOMETER 2            //PIN DIGITAL ARDUINO UNO -> INTERRUPT 0
#define PIN_ANEMOMETER_INTERRUPT 0
#define PIN_RAINGAUGE  3            //PIN DIGITAL ARDUINO UNO -> INTERRUPT 1
#define PIN_RAINGAUGE_INTERRUPT 1
#define PIN_VANE       A0           // PIN ANALOGO ARDUINO UNO
   
#define NUMDIRS 8 //CANTIDAD DE DIRECCIONES DEL VIENTO
#define SEALEVELPRESSURE_HPA (1013.25) // VALOR MEDIO DE LA PRESION DE LA ATMOSFERA TERRESTRE 

BH1750 lightMeter;
Adafruit_BME680 bme; // I2C

//Variables veleta direccion del viento
unsigned long adc[NUMDIRS] = {26, 45, 77, 118, 161, 196, 220, 256}; 
char *vaneDirection[NUMDIRS] = {"O","NO","N","SO","NE","S","SE","E"};
byte dirOffset=0;

//Variables lluvia caida
volatile int numDropsRainGauge = 0;   // SE INCREMENTA CON CADA INTERRUPCION
float rain = 0.0;
float rainHours = 0.0;
float rain24Hours = 0.0;

//Variables Anemometro
const int m_time = 5;
int wind_ct = 0;
float wind = 0.0;
float windMaxHours = 0.0;
float windMaxDay = 0.0;

//Variables BME680
float Temperatura;
float Humedad;
float Presion;
float Altitud;
float Gas;
String CalidadAire;
float Ppm;

unsigned long time = 0;
unsigned long timeDay = 0;
unsigned long timeHours = 0;

//=======================================================
// Determina la direcciones del viento en base una lectura analogica del microcontrolador
//=======================================================
void calcWindDir() {
  int val;
  byte x, reading;
  
  val = analogRead(PIN_VANE);
  val >>=2;                        // Se mueve la lectura 2 bits (255)
  reading = val;

  // Compara el valor obtenido de la lectura analogica con los de la tabla adc para relacionarlo con una direcciones
  for (x=0; x<NUMDIRS; x++) {
    if (adc[x] >= reading)
      break;
   }
   
   x = (x + dirOffset) % 8;   // Ajusta la orientacion en una de las 8 direcciones del viento
   
   Serial.print(F("Direccion del viento: "));
   Serial.println(vaneDirection[x]);

}
//=======================================================
// Calcula la cantidad del agua caida en base a las interrupciones del sensor 
//=======================================================
void countRainGauge() {
   numDropsRainGauge++;
}
void calcRainFall() {

   Serial.println();
   Serial.println(F("##INFORMACION DE LLUVIA##"));
  
   float rain_amount = 0.2794 * numDropsRainGauge/5.5; //Obtenemos entre 5 y 6 interrupciones por cada balde
   rain = rain + rain_amount;
   rainHours = rainHours + rain_amount;
   rain24Hours = rain24Hours + rain_amount;

   Serial.print(F("Agua total caida: "));
   Serial.print(rain);
   Serial.print(F(" mm "));
   Serial.println();

   Serial.print(F("Agua caida ultima hora: "));
   Serial.print(rainHours);
   Serial.print(F(" mm/h "));
   Serial.println();

   Serial.print(F("Agua caida ultimas 24 horas: "));
   Serial.print(rain24Hours);
   Serial.print(F(" mm/24h "));
   Serial.println();
   
   numDropsRainGauge = 0;        // Reset counter  

}
//===========================================
//Calcula la velocidad del viento en base a las interrupciones del sensor 
//===========================================
void countWind() {
  wind_ct ++;
}
void getWindSpeed() {

  
  wind_ct = 0;
  attachInterrupt(PIN_ANEMOMETER_INTERRUPT, countWind, RISING);
  delay(1000 * m_time);
  detachInterrupt(PIN_ANEMOMETER_INTERRUPT);
  
  wind = (float)wind_ct / (float)m_time * 2.4;

  if (wind > windMaxDay){
     windMaxDay = wind;
  }
  
  if (wind > windMaxHours){
     windMaxHours = wind;
  }

  Serial.println(F("##INFORMACION DEL VIENTO##"));
  Serial.print(F("Velocidad del viento: "));
  Serial.print(wind);       //VELOCIDAD EN Km/h
  Serial.print(F(" km/h - "));
  Serial.print(wind / 3.6); //VELOCIDAD EN m/s
  Serial.println(F(" m/s"));

  Serial.print(F("Viento maximo ultima hora: "));
  Serial.print(windMaxHours);
  Serial.println(F(" km/h"));
    
  Serial.print(F("Viento maximo ultimas 24 horas: "));
  Serial.print(windMaxDay);
  Serial.println(F(" km/h"));
  
}
//===========================================
//Obtiene los datos del sensor BME680
//===========================================
void getDataBME680()
{
  if (! bme.performReading()) {
    Serial.println(F("No se pudo realizar la lectura :( "));
    return;
  }


  Temperatura = bme.temperature;
  Humedad = bme.humidity;
  Presion = bme.pressure / 100.0;
  Altitud = bme.readAltitude(SEALEVELPRESSURE_HPA);
  Gas = bme.gas_resistance / 1000.0;
  
  Serial.print(F("Temperatura = "));
  Serial.print(Temperatura);
  Serial.println(F(" *C"));

  Serial.print(F("Humedad = "));
  Serial.print(Humedad);
  Serial.println(F(" %"));

  Serial.print(F("Presion atmosferica = "));
  Serial.print(Presion);
  Serial.println(F(" hPa"));

  Serial.print(F("Altitud aproximada = "));
  Serial.print(Altitud);
  Serial.println(F(" m")); 

  Serial.print(F("Gas en el aire (COV) = "));
  Serial.print(Gas);
  Serial.println(F(" KOhms"));

  if(Gas <=50){
    CalidadAire = "Buena";
    Serial.println(F("Calidad de aire: Buena"));
  }
  else if(Gas <=100 ){
    CalidadAire = "Promedio";
    Serial.println(F("Calidad de aire: Promedio"));
  }
  else if(Gas <=150){
    CalidadAire = "Un poco mala";
    Serial.println(F("Calidad de aire: Un poco mala"));
  }
  else if(Gas <=200){
    CalidadAire = "Mala";
    Serial.println(F("Calidad de aire: Mala"));
  }
  else if(Gas <=300){
    CalidadAire = "Muy mala";
    Serial.println(F("Calidad de aire: Muy mala"));
  }
  else if(Gas <=500){
    CalidadAire = "Extremadamente mala";
    Serial.println(F("Calidad de aire: Extremadamente mala"));
  }
}
//=======================================================
// Obtiene los datos de luminosidad
//=======================================================
void getLightMeter(){
  float lux = lightMeter.readLightLevel(true);
  Serial.print(F("Luminosidad: "));
  Serial.print(lux);
  Serial.println(F(" Lx"));

}
//=======================================================
// Inicio
//=======================================================
void setup()
{
  Serial.begin(9600); 
  Wire.begin(); 
  
  if (!bme.begin()) {
    Serial.println(F("No se pudo encontrar un sensor BME680 válido, verifique el cableado!"));
  }
  else{
    // CONFIGURACION PARAMETROS BME680
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150); // 320*C POR 150 ms
  }

  if (!lightMeter.begin()) {
    Serial.println(F("No se pudo encontrar un sensor BH1750 válido, verifique el cableado!"));
  }

  digitalWrite(PIN_RAINGAUGE, HIGH);
  attachInterrupt(PIN_RAINGAUGE_INTERRUPT, countRainGauge, RISING);
  
}

  
void loop()
{
  
  time = millis();
  
  Serial.println();
  Serial.println(F("Estacion Meteorologica - Fabian Oyarce"));
  Serial.println(F("Actualizado cada 5 segundos"));
  Serial.println();
  
  getWindSpeed();
  calcWindDir();
  calcRainFall();
  
  Serial.println();
  Serial.println(F("##OTROS DATOS METEOROLOGICOS##"));
  getLightMeter();
  getDataBME680();

  if (time >= timeHours ) {
    rainHours = 0;
    windMaxHours = 0.0;
    timeHours = time + (3600000);
  }
 
  if (time >= timeDay ) {
    rain24Hours = 0;
    windMaxDay = 0.0;
    timeDay = time + (86400000);
  }
   
}
