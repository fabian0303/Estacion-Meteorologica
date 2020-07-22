#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>
#include <BH1750.h>
#include <Adafruit_Sensor.h>
#include <SPI.h>
#include "FS.h"
#include "SD.h"
#include "ThingSpeak.h"
#include "secrets.h"
//#include "Adafruit_BME680.h"
#include <Adafruit_BME280.h>

#define PIN_GPIO_WIND_VANE 36
#define PIN_GPIO_WIND_SPEED 15
#define PIN_GPIO_RAINGAUGE 2



//Variables y constantes utilizadas en el seguimiento de la lluvia.
#define MS_IN_DAY           86400000
#define MS_IN_HR            360000
#define MS_FACTOR_SLEEP     2000
#define MS_FACTOR_CLOUD     20000
#define pressure_offset 0       // Air pressure calibration, adjust for your altitude

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


Adafruit_BME280 bme;              // I2C
//Adafruit_BME680 bme; // I2C
BH1750 lightMeter;

const char* ssid     = "Fabi";
const char* password = "1234567890";
const char* Timezone = "<-04>4<-03>,M9.1.6/24,M4.1.6/24";

String nameLog ;


WiFiClient  client;

unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;

//Variables viento
const int m_time = 5;      //Meassuretime in Seconds
const int calibrationFactorRainGauge = 5; //Ajuste de calibracion para lluvia segun la cantidad de tick por cubeta que lanza el sensor. Recordar que cada cubeta equivale a 0.2794 mm de lluvia caida
int wind_ct = 0;
float wind = 0.0;
String windDir = "";
float windDirAng = 0.0;

//Variables lluvia
int numDropsRainGauge = 0;
float rain = 0.0;
float rainHours = 0.0;
float rain24Hours = 0.0;

unsigned long timeD = 0;
unsigned long timeDay = 0;
unsigned long timeSleep = 0;
unsigned long timeCloud = 0;
unsigned long timeHours = 0;

// Only use pins that can support an interrupt
static String         Date_str, Time_str;
volatile unsigned int local_Unix_time = 0, next_update_due = 0;
volatile unsigned int update_duration = 60 * 60; // Time duration in seconds, so synchronise every hour
static float          bme_temp, bme_humi, bme_pres;


float lux;


//#########################################################################################
void IRAM_ATTR Timer_TImeout_ISR() {
  portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL_ISR(&timerMux);
  local_Unix_time++;
  portEXIT_CRITICAL_ISR(&timerMux);
}
//#########################################################################################

void IRAM_ATTR isr() {
  numDropsRainGauge+=1;
}


void setup()
{
  
  Serial.begin(9600);
  Wire.begin(SDA, SCL, 100000);      // (sda,scl,bus_speed)
  

   // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }


  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  display.clearDisplay();
  display.setTextSize(1);         
  display.setTextColor(SSD1306_WHITE);    
  display.display();

  //bool status = bme.begin(0x76);  // For Adafruit sensors use address 0x77, for most 3rd party types use address 0x76
  bool status = bme.begin(); 
  if (!status) Serial.println(F("No se pudo encontrar un sensor BMEE válido, verifique el cableado!")); // Check for a sensor

  if (!lightMeter.begin()) {
    Serial.println(F("No se pudo encontrar un sensor BH1750 válido, verifique el cableado!"));
  }

  if(!SD.begin()){
    Serial.println(F("Tarjeta SD no encontrada"));
    return;
  }
  uint8_t cardType = SD.cardType();

  if(cardType == CARD_NONE){
      Serial.println(F("Tarjeta SD no soportada"));
      return;
  }

  // Configuración del sensor de lluvia. 
  //Las precipitaciones son calculadas por ticks por segundo, 
  //y las marcas de tiempo de ticks son almacenedas para tener registros temporales, es decir, lluvia por hora, por día, etc.)
  digitalWrite(digitalPinToInterrupt(PIN_GPIO_RAINGAUGE), HIGH);
  attachInterrupt(digitalPinToInterrupt(PIN_GPIO_RAINGAUGE), isr, RISING);

  StartWiFi();
  Start_Time_Services();
  Setup_Interrupts_and_Initialise_Clock();

  delay(2000);
  UpdateLocalTime();  
  nameLog  = "/"+Date_str + "_" + Time_str + ".txt";
  nameLog.replace(":", "-");
  //Serial.println(nameLog);
  writeFile(SD, nameLog.c_str(), "fecha,hora,temperatura,humedad,presion,direccion,velocidad,lluvia,luminosidad\r\n");
  
  ThingSpeak.begin(client);

  delay(2000);

  setRainAcomulate();
}

void loop()
{

  timeD = millis();
  UpdateLocalTime();     // The variables 'Date_str' and 'Time_str' now have current date-time values

  //display.setCursor(10, 0);
  display.clearDisplay();
  //display.setTextSize(1);         
  //display.setTextColor(SSD1306_WHITE);        
  display.setCursor(0,0);             
  display.println(Date_str);
  display.setCursor(80,0);
  display.println(Time_str); 
  display.setCursor(0,15);             
  display.println(String(bme_temp, 1)+" *"+"C");
  display.setCursor(50,15);             
  display.println(String(bme_humi, 0)+"%");
  display.setCursor(80,15);             
  display.println(String(bme_pres, 0 )+ " hPa");
  display.setCursor(0,30);             
  display.println(String(wind, 1) + " kph");
  display.setCursor(80,30);             
  display.println(windDir);
  display.setCursor(0,45);             
  display.println(String(rain,4)+ " mm");
  display.setCursor(80,45);             
  display.println(String(lux,0)+ " lx");
  
  display.display();

  if (timeD >= timeSleep ) {
    
    BME280_Read_Sensor();  // The variables 'bme_temp', 'bme_humi', 'bme_pres' now have current values
  
    //Serial.println();
    //Serial.println(F("#######################################"));
    //Serial.print(F("Fecha: "));
    //Serial.println(Date_str);
    //Serial.print(F("Hora: "));                                                        
    //Serial.println(Time_str);
  
    //Cálculo de la velocidad del viento.
    calcWindSpeed();
 
    //Cálculo de la dirección del viento.
    calcWindDir();
  
    //Cálculo de lluvia caida.
    calcRainFall();

    //Serial.println();
    //Serial.println(F("##DATOS METEOROLOGICOS##"));
    
    //Serial.print(F("Temperatura: "));
    //Serial.println(String(bme_temp, 1)+"°"+"C");
    //Serial.print(F("Humedad: "));
    //Serial.println(String(bme_humi, 0)+"%");
    //Serial.print(F("Presion Atmosferica: "));
    //Serial.println(String(bme_pres, 0)+"hPa");

    getLightMeter();
  
    String dataMessage =  String(Date_str) + "," + String(Time_str) + "," + String(bme_temp) + "," + String(bme_humi) + "," + String(bme_pres) + "," + String(windDir) + "," + String(wind) + ","  + String(rain) + ","  + String(lux) + "\r\n";

    appendFile(SD, nameLog.c_str() ,dataMessage.c_str());
              
    timeSleep = timeD + (MS_FACTOR_SLEEP);
  }

  if (timeD >= timeHours ) {
    rainHours = 0;
    timeHours = timeD + (MS_IN_HR);
  }

  if(timeD >= timeDay ) {
    rain24Hours = 0;
    timeDay = timeD + (MS_IN_DAY);
  }


   if (timeD >= timeCloud ) {

    ThingSpeak.setField(1,String(bme_temp, 1));
    ThingSpeak.setField(2,String(bme_humi, 0));
    ThingSpeak.setField(3,String(bme_pres, 0));
    ThingSpeak.setField(4,String(lux, 0));
    ThingSpeak.setField(5,wind);
    ThingSpeak.setField(6,windDirAng);
    ThingSpeak.setField(7,rain);
   
    int httpCode  = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  
    if (httpCode == 200) {
      Serial.println(F("Datos subidos correctamente."));
    }
    else {
      Serial.println("Problema al subir la los datos. HTTP error code " + String(httpCode));
    }

    timeCloud = timeD + (MS_FACTOR_CLOUD);
  }
      
  
}

void StartWiFi() {
  /* Set the ESP to be a WiFi-client, otherwise by default, it acts as ss both a client and an access-point
      and can cause network-issues with other WiFi-devices on your WiFi-network. */

  display.clearDisplay();
  //display.setTextSize(1);         
  //display.setTextColor(SSD1306_WHITE);        
 
  
  WiFi.mode(WIFI_STA);
  Serial.print(F("\r\nConectando a: ")); Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED ) {
    delay(500);
    Serial.print(F("."));
    
    display.setCursor(0,0);             
    display.println("Conectando WiFi");
    display.display();
  }
  Serial.print("WiFi conectado a la direccion: "); Serial.println(WiFi.localIP());
}

void UpdateLocalTime() {
  
  time_t now;
  if (local_Unix_time > next_update_due) { // only get a time synchronisation from the NTP server at the update-time delay set
    time(&now);
    Serial.println("Synchronising local time, time error was: " + String(now - local_Unix_time));
    // If this displays a negative result the interrupt clock is running fast or positive running slow
    local_Unix_time = now;
    next_update_due = local_Unix_time + update_duration;
  } else now = local_Unix_time;
  
  //See http://www.cplusplus.com/reference/ctime/strftime/
  
  char hour_output[30], day_output[30];
  strftime(day_output, 30, "%d-%m-%y", localtime(&now)); // Formats date as: 24-05-17
  strftime(hour_output, 30, "%T", localtime(&now));      // Formats time as: 14:05:49
  Date_str = day_output;
  Time_str = hour_output;
}

void Start_Time_Services() {
  // Now configure time services
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", Timezone, 1); // See below for other time zones
  delay(1000); // Wait for time services
}

void Setup_Interrupts_and_Initialise_Clock() {
  hw_timer_t * timer = NULL;
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &Timer_TImeout_ISR, true);
  timerAlarmWrite(timer, 1000000, true);
  timerAlarmEnable(timer);
  //Now get current Unix time and assign the value to local Unix time counter and start the clock.
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println(F("Falla al obtener el tiempo"));
  }
  time_t now;
  time(&now);
  local_Unix_time = now + 1; // The addition of 1 counters the NTP setup time delay
  next_update_due = local_Unix_time + update_duration;
}

void BME280_Read_Sensor() {
  
  bme_temp = bme.readTemperature();
  bme_pres = bme.readPressure() / 100.0F + pressure_offset;
  bme_humi = bme.readHumidity();  
}

//Calcula la cantidad de lluvia caida en base a interrupciones, cada interrupcion equivale a 0.2794 mm de lluvia caida
//es necesario calibrar el sensor segun sea el caso para que cada interrupcion cuente como una
//Conectado a 5V con una resistencia de 10K
void calcRainFall() {

   //Serial.println(numDropsRainGauge);
   //Serial.println(F("##INFORMACION DE LLUVIA##"));
   
   float rain_amount = 0.2794 * (numDropsRainGauge/calibrationFactorRainGauge); 

   if (rain_amount <= 0.4){ //Eliminamos golpes de corriente que puedan generar mas de 0.4 mm en una interrupcion lo que deberia estar entre 0.2 y 0.4
    rain = rain + rain_amount;
   }

   //Serial.print(F("Lluvia total: "));
   //Serial.print(rain);
   //Serial.print(F(" mm "));
   //Serial.println();
      
   numDropsRainGauge = 0;        // Reset counter  
}

//Cuenta la duración de la interrupcion 
void countWind() {
  wind_ct ++;
}

//Calcula la velocidad del viento en base al calculo de la duracion de una interrupcion que genera el sensor cuadno este se mueve a 2.4 km/h en un segundo
//Conectado a 5V con una resistencia de 10K
void calcWindSpeed() {
  wind_ct = 0;
  float windAux = wind;
  attachInterrupt(digitalPinToInterrupt(PIN_GPIO_WIND_SPEED), countWind, RISING);
  delay(1000 * m_time);
  detachInterrupt(digitalPinToInterrupt(PIN_GPIO_WIND_SPEED));
  wind = (float)wind_ct / (float)m_time * 2.4;

  if (wind > 100){ //Eliminamos golpes de corriente que generen vientos superior a 100 km/h
    wind = windAux;
  }

  //Serial.println();
  //Serial.println(F("##INFORMACION DEL VIENTO##"));

  //Serial.print(F("Velocidad del viento: "));
  //Serial.print(wind);       //Speed in Km/h
  //Serial.print(F(" km/h - "));
  //Serial.print(wind / 3.6); //Speed in m/s
  //Serial.println(F(" m/s"));
  
}

//Calcula la direccion del viento en base a un voltage entregado por la veleta el cual se trasforma en una tension analogica en el chip
//Voltaje calculado en base a 3.3V con una resistencia de 4k7
void calcWindDir() {
  int val;

  val = analogRead(PIN_GPIO_WIND_VANE);

  if (val < 700){
    windDir = "E";
    //windDirAng = 68; 
    windDirAng = 90; 
  }
  else if(val<1400){
    windDir = "SE";
    //windDirAng = 113;
    windDirAng = 135;  
  }
  else if(val<2000){
    windDir = "S";
    //windDirAng = 158;
    windDirAng = 180; 
  }
  else if(val<2600){
    windDir = "NE"; 
    //windDirAng = 23; 
    windDirAng = 45; 
  }
  else if(val<3200){
    windDir = "SO";
    //windDirAng = 203;
    windDirAng = 225; 
  }
  else if(val<3600){
    windDir = "N";
    //windDirAng = 338;
    windDirAng = 0;  
  }
  else if(val<4000){
    windDir = "NO";
    //windDirAng = 293; 
    windDirAng = 315; 
  }
  else{
    windDir = "O";
    //windDirAng = 248; 
    windDirAng = 270; 
  }
  
  //Serial.print(F("Direccion del viento: "));
  //Serial.println(windDir);        
}

//Obtiene los datos de luminosidad
void getLightMeter(){
  lux = lightMeter.readLightLevel(true);
  //Serial.print(F("Luminosidad: "));
  //Serial.print(lux);
  //Serial.println(F(" Lx"));  
}


void readFile(fs::FS &fs, const char * path){
    Serial.printf("Reading file: %s\n", path);

    File file = fs.open(path);
    if(!file){
        Serial.println("Failed to open file for reading");
        return;
    }

    Serial.print("Read from file: ");
    while(file.available()){
        Serial.write(file.read());
    }
    file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Writing file: %s\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("Failed to open file for writing");
        return;
    }
    if(file.print(message)){
        Serial.println("File written");
    } else {
        Serial.println("Write failed");
    }
    file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message){
    
    Serial.println();
    Serial.printf("Appending to file: %s\n", path);

    File file = fs.open(path, FILE_APPEND);
    if(!file){
        Serial.println("Failed to open file for appending");
        return;
    }
    if(file.print(message)){
        Serial.println("Message appended");
    } else {
        Serial.println("Append failed");
    }
    file.close();
}

void setRainAcomulate(){
  
  String rainCloud  =  ThingSpeak.readStringField (SECRET_CH_ID,  7,  SECRET_READ_APIKEY ); 
  rain = rainCloud.toFloat();
  if (rain < 0){
    rain = 0;
  }
  int  statusCode  =  ThingSpeak.getLastReadStatus (); 
  if  (statusCode  ==  200 ) 
  { 
    Serial.print(F("Lluvia acomulada actual:" )); 
    Serial.println (rain); 
  } 
  else 
  { 
    Serial.println (F("No se puede leer el canal / Sin conexión a Internet" )); 
  } 
 
}
