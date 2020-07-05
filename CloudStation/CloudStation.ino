#include "ThingSpeak.h"
#include "secrets.h"
#include "Adafruit_BME680.h"
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

//Crear el objeto lcd  dirección  0x27 y 16 columnas x 2 filas
LiquidCrystal_I2C lcd(0x27,16,2); 

#define SEALEVELPRESSURE_HPA (1013.25) // VALOR MEDIO DE LA PRESION DE LA ATMOSFERA TERRESTRE
#define analogMQ7 A0     

Adafruit_BME680 bme; // I2C

float Temperatura;
int bandera=0;
float Humedad;
float Presion;
float Altitud;
float Gas;
String CalidadAire;
float Ppm;

const int  R_PIN           = 12;
const int  G_PIN           = 13;
const int  B_PIN           = 15;

unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;

#include <ESP8266WiFi.h>

char ssid[] = SECRET_SSID;   // your network SSID (name)
char pass[] = SECRET_PASS;   // your network password
int keyIndex = 0;            // your network key index number (needed only for WEP)
WiFiClient  client;

void setup() {
  Serial.begin(9600);
  delay(100);

  WiFi.mode(WIFI_STA);

  lcd.init(); 
  lcd.backlight();

  if (!bme.begin()) {
    Serial.println(F("No se pudo encontrar un sensor BME680 válido, verifique el cableado!"));
  }
  else{
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150); // 320*C POR 150 ms
  }

  pinMode(analogMQ7, INPUT);
  pinMode(R_PIN, OUTPUT);
  pinMode(G_PIN, OUTPUT);
  pinMode(B_PIN, OUTPUT);
  
  ThingSpeak.begin(client);
  
}

void loop() {

  // Connect or reconnect to WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(SECRET_SSID);
    while (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid, pass); // Connect to WPA/WPA2 network. Change this line if using open or WEP network
      Serial.print(".");
      delay(5000);
    }
    Serial.println("\nConnected.");
  }

  Temperatura = bme.temperature;
  Humedad = bme.humidity;
  Presion = bme.pressure / 100.0;
  Altitud = bme.readAltitude(SEALEVELPRESSURE_HPA);
  Gas = bme.gas_resistance / 1000.0;
  Ppm = analogRead(analogMQ7); 
  
  if(Gas <=50){
    CalidadAire = "Buena";
    analogWrite(R_PIN,0);
    analogWrite(G_PIN,255);
    analogWrite(B_PIN,0);
  }
  else if(Gas <=100 ){
    CalidadAire = "Promedio";
    analogWrite(R_PIN,255);
    analogWrite(G_PIN,255);
    analogWrite(B_PIN,0);
  }
  else if(Gas <=150){
    CalidadAire = "Un poco mala";
    analogWrite(R_PIN,255);
    analogWrite(G_PIN,100);
    analogWrite(B_PIN,0);
  }
  else if(Gas <=200){
    CalidadAire = "Mala";
    analogWrite(R_PIN,255);
    analogWrite(G_PIN,0);
    analogWrite(B_PIN,0);
  }
  else if(Gas <=300){
    CalidadAire = "Muy mala";
    analogWrite(R_PIN,247);
    analogWrite(G_PIN,0);
    analogWrite(B_PIN,255);
  }
  else if(Gas <=500){
    CalidadAire = "Extremadamente mala";
    analogWrite(R_PIN,0);
    analogWrite(G_PIN,0);
    analogWrite(B_PIN,0);
  }

  if (bandera == 0){

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Temp: ");
    lcd.print(Temperatura,2);
    lcd.print("337C");
  
    //Cursor en la primera posición de la 2° fila
    lcd.setCursor(0,1);
    lcd.print("Hume: ");
    lcd.print(Humedad,2); //1 decimal
    lcd.print(" %"); 
    bandera  =  1;
  
  }else if(bandera==1){
  
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Pres: ");
    lcd.print(Presion,2);
    lcd.print(" hPa");

    //Cursor en la primera posición de la 2° fila
    lcd.setCursor(0,1);
    lcd.print("Alti: ");
    lcd.print(Altitud,2); //1 decimal
    lcd.print(" m"); 
    bandera  = 2;
    
  }else if(bandera==2){
  
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("COV: ");
    lcd.print(CalidadAire);
    lcd.print("");

    //Cursor en la primera posición de la 2° fila
    lcd.setCursor(0,1);
    lcd.print("GAS: ");
    lcd.print(Ppm,2); //1 decimal
    lcd.print(" ppm"); 
    bandera  = 0;
      
   }

  ThingSpeak.setField(1,Temperatura);
  ThingSpeak.setField(2,Humedad);
  ThingSpeak.setField(3,Presion);
  ThingSpeak.setField(4,Altitud);
  ThingSpeak.setField(5,Gas);
  ThingSpeak.setField(6,Ppm);
  int httpCode  = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);

  if (httpCode == 200) {
    Serial.println("Datos subidos correctamente.");
  }
  else {
    Serial.println("Problema al subir la los datos. Codigo de error HTTP " + String(httpCode));
  }

  // Esperamos 20 segundos para actualiar los datos del canal
  delay(20000);
}
