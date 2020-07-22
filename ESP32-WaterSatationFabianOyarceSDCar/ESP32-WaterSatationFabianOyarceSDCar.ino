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

//#include <Adafruit_BME280.h>
#include "Adafruit_BME680.h"

//Adafruit_BME280 bme;              // I2C
Adafruit_BME680 bme; // I2C
BH1750 lightMeter;

// Change to your WiFi credentials and select your time zone
const char* ssid     = "Fabi";
const char* password = "1234567890";
const char* Timezone = "<-04>4<-03>,M9.1.6/24,M4.1.6/24"; 

WiFiClient  client;

unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;


//Calibration factors, extent of wind speed average, and Wind Sensor pin adjust as necessary
#define pressure_offset 0       // Air pressure calibration, adjust for your altitude
#define WS_Calibration  1.492       // Wind Speed calibration factor
#define WS_Samples      20        // Number of Wind Speed samples for an average
#define WindSensorPin   15

// Asignacion de pines GPIO usando chip ESP32 con WiFi y Bluetooth
#define RAIN_PIN     25 //D25
#define WIND_DIR_PIN 35 //D35

#define PIN_CS_SD 5 //PIN GPIO 

//Variable direccion del viento.
String windDir = "";
int windDirAng = 0;

//Variables y constantes utilizadas en el seguimiento de la lluvia.
#define MS_IN_DAY           86400000
#define MS_IN_HR            360000
#define MS_FACTOR_SLEEP     2000
#define MS_FACTOR_CLOUD     20000

//Variables lluvia caida
float rain = 0.0;
float rainHours = 0.0;
float rain24Hours = 0.0;
volatile int rainTicks = 0;

float rainCalibrationFactor = 6.5; //Ajuste de calibracion para lluvia segun la cantidad de tick por cubeta que lanza el sensor. Recordar que cada cubeta equivale a 0.2794 mm de lluvia caida

unsigned long timeD = 0;
unsigned long timeDay = 0;
unsigned long timeSleep = 0;
unsigned long timeCloud = 0;
unsigned long timeHours = 0;

// Only use pins that can support an interrupt
static String         Date_str, Time_str;
volatile unsigned int local_Unix_time = 0, next_update_due = 0;
volatile unsigned int update_duration = 60 * 60; // Time duration in seconds, so synchronise every hour
static float          bme_temp, bme_humi, bme_pres, WindSpeed;
static unsigned int   Last_Event_Time;

float WSpeedReadings[WS_Samples]; // To hold readings from the Wind Speed Sensor
int   WS_Samples_Index = 0;       // The index of the current wind speed reading
float WS_Total         = 0;       // The running wind speed total
float WS_Average       = 0;       // The wind speed average

float Lux;

//#########################################################################################
void IRAM_ATTR MeasureWindSpeed_ISR() {
  portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL_ISR(&timerMux);
  Last_Event_Time = millis();    // Record current time for next event calculations
  portEXIT_CRITICAL_ISR(&timerMux);
}
//#########################################################################################
void IRAM_ATTR Timer_TImeout_ISR() {
  portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL_ISR(&timerMux);
  local_Unix_time++;
  portEXIT_CRITICAL_ISR(&timerMux);
}
//#########################################################################################
void setup() {
  Serial.begin(9600);
  Wire.begin(SDA, SCL, 100000);      // (sda,scl,bus_speed)
  
  
  //bool status = bme.begin(0x76);  // For Adafruit sensors use address 0x77, for most 3rd party types use address 0x76
  bool status = bme.begin(); 
  if (!status) Serial.println("Could not find a valid BME sensor, check wiring!"); // Check for a sensor

  if (!lightMeter.begin()) {
    Serial.println(F("No se pudo encontrar un sensor BH1750 válido, verifique el cableado!"));
  }


   if(!SD.begin()){
        Serial.println("Card Mount Failed");
        return;
    }
    uint8_t cardType = SD.cardType();

    if(cardType == CARD_NONE){
        Serial.println("No SD card attached");
        return;
    }

    Serial.print("SD Card Type: ");
    if(cardType == CARD_MMC){
        Serial.println("MMC");
    } else if(cardType == CARD_SD){
        Serial.println("SDSC");
    } else if(cardType == CARD_SDHC){
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);

  writeFile(SD, "/stationlog.txt", "fecha,hora,temperatura,humedad,presion,direccion,velocidad,lluvia,luminosidad\r\n");

  // Configuración del sensor de lluvia. 
  //Las precipitaciones son calculadas por ticks por segundo, 
  //y las marcas de tiempo de ticks son almacenedas para tener registros temporales, es decir, lluvia por hora, por día, etc.)
  pinMode(RAIN_PIN, INPUT); //Sensor de lluvia
  attachInterrupt(digitalPinToInterrupt(RAIN_PIN), rainTick, RISING);//Cuando se produce una interrupcion se llama al contador de ticks de lluvia
  
  StartWiFi();
  Start_Time_Services();
  Setup_Interrupts_and_Initialise_Clock();       // Now setup a timer interrupt to occur every 1-second, to keep seconds accurate
  
  for (int index = 0; index < WS_Samples; index++) { // Now clear the Wind Speed average array
    WSpeedReadings[index] = 0;
  }

  ThingSpeak.begin(client);
  
}
//#########################################################################################
void loop() {

  timeD = millis();
  UpdateLocalTime();     // The variables 'Date_str' and 'Time_str' now have current date-time values

   if (timeD >= timeSleep ) {
    
      BME280_Read_Sensor();  // The variables 'bme_temp', 'bme_humi', 'bme_pres' now have current values
    
      Serial.println();
      Serial.println(F("#######################################"));
      Serial.print(F("Fecha: "));
      Serial.println(Date_str);
      Serial.print(F("Hora: "));                                                        
      Serial.println(Time_str);
    
      Serial.println();
      Serial.println(F("##DATOS METEOROLOGICOS##"));
      
      Serial.print(F("Temperatura: "));
      Serial.println(String(bme_temp, 1)+"°"+"C");
      Serial.print(F("Humedad: "));
      Serial.println(String(bme_humi, 0)+"%");
      Serial.print(F("Presion Atmosferica: "));
      Serial.println(String(bme_pres, 0)+"hPa");

      getLightMeter();
    
      Serial.println();
      Serial.println(F("##INFORMACION DEL VIENTO##"));
      Serial.print(F("Velocidad del viento: "));
      Serial.println(String(Calculate_WindSpeed(), 1) + "km/h");
    
       //Cálculo de la dirección del viento.
      windDirCalc();
    
      //Cálculo de lluvia caida.
      rainFallCalc();

      String dataMessage =  String(Date_str) + "," + String(Time_str) + "," + String(bme_temp) + "," + String(bme_humi) + "," + String(bme_pres) + "," + String(windDir) + "," + String(WindSpeed) + ","  + String(rain) + ","  + String(Lux) + "\r\n";


      appendFile(SD, "/stationlog.txt",dataMessage.c_str());
                
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

    ThingSpeak.setField(1,bme_temp);
    ThingSpeak.setField(2,bme_humi);
    ThingSpeak.setField(3,bme_pres);
    ThingSpeak.setField(4,Lux);
    ThingSpeak.setField(5,WindSpeed);
    ThingSpeak.setField(6,windDirAng);
    ThingSpeak.setField(7,rain);
   
    int httpCode  = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  
    if (httpCode == 200) {
      Serial.println("Datos subidos correctamente.");
    }
    else {
      Serial.println("Problema al subir la los datos. HTTP error code " + String(httpCode));
    }

    timeCloud = timeD + (MS_FACTOR_CLOUD);
  }
  
  delay(500);                                                            
}
//#########################################################################################
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
//#########################################################################################
void StartWiFi() {
  /* Set the ESP to be a WiFi-client, otherwise by default, it acts as ss both a client and an access-point
      and can cause network-issues with other WiFi-devices on your WiFi-network. */
  WiFi.mode(WIFI_STA);
  Serial.print(F("\r\nConectando a: ")); Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED ) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.print("WiFi conectado a la direccion: "); Serial.println(WiFi.localIP());
}
//#########################################################################################
void Setup_Interrupts_and_Initialise_Clock() {
  hw_timer_t * timer = NULL;
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &Timer_TImeout_ISR, true);
  timerAlarmWrite(timer, 1000000, true);
  timerAlarmEnable(timer);
  pinMode(WindSensorPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(WindSensorPin), &MeasureWindSpeed_ISR, RISING);
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
//#########################################################################################
void Start_Time_Services() {
  // Now configure time services
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", Timezone, 1); // See below for other time zones
  delay(1000); // Wait for time services
}
//#########################################################################################
void BME280_Read_Sensor() {
  
  bme_temp = bme.readTemperature();
  bme_pres = bme.readPressure() / 100.0F + pressure_offset;
  bme_humi = bme.readHumidity();  
}

float Calculate_WindSpeed() {
  
  if ((millis() - Last_Event_Time) > 2) { // Ignore short time intervals to debounce switch contacts
    WindSpeed = (1.00F / (((millis() - Last_Event_Time) / 1000.00F) * 2)) * WS_Calibration; // Calculate wind speed
  }
  
  // Calculate average wind speed
  WS_Total                         = WS_Total - WSpeedReadings[WS_Samples_Index]; // Subtract the last reading:
  WSpeedReadings[WS_Samples_Index] = WindSpeed;                                   // Add the reading to the total:
  WS_Total                         = WS_Total + WSpeedReadings[WS_Samples_Index]; // Advance to the next position in the array:
  WS_Samples_Index                 = WS_Samples_Index + 1;                        // If we're at the end of the array...
  
  if (WS_Samples_Index >= WS_Samples) {                                           // ...wrap around to the beginning:
    WS_Samples_Index = 0;
  }
  
  WindSpeed = WS_Total / WS_Samples;                                              // calculate the average wind speed
  WindSpeed = WindSpeed * 1.60934;                                                // Convert to kph
  return WindSpeed;
}

//=======================================================
// Calcula la cantidad del agua caida en base a las interrupciones del sensor 
//=======================================================
void rainTick(void)
{
  rainTicks++;
}

void rainFallCalc() {
   Serial.println();
   Serial.println(F("##INFORMACION DE LLUVIA##"));
  
   float rain_amount = (float(rainTicks)*0.2794) / rainCalibrationFactor;
   rain = rain + rain_amount;
   rainHours = rainHours + rain_amount;
   rain24Hours = rain24Hours + rain_amount;

   Serial.print(F("Lluvia caida ultima hora: "));
   Serial.print(rainHours);
   Serial.print(F(" mm/h "));
   Serial.println();

   Serial.print(F("Lluvia caida ultimas 24 horas: "));
   Serial.print(rain24Hours);
   Serial.print(F(" mm/24h "));
   Serial.println();

   Serial.print(F("Lluvia total caida: "));
   Serial.print(rain);
   Serial.print(F(" mm "));
   Serial.println();
   
   rainTicks = 0; //Se reinicia el contador
}
//===========================================
//Calcula la direccion del viento en base a una respuesta analogica que entrega la veleta conectada a una resistencia de 66k con un 3.3V
//El sensor conectado a 5V en Capaz de responder a 16 direcciones pero dado que solo suminstramos 3.3 para ahorrar energia vamos a trabajar solo con 8 direcciones
//===========================================
void windDirCalc()
{
  //Cálculo de la dirección del viento.
  int vin = analogRead(WIND_DIR_PIN);
  
  if (vin == 0 ){
    windDir="O";
    windDirAng = 248; 
  }
  else if (vin < 150){
    windDir="NO";
    windDirAng = 293; 
  }
  else if (vin < 400){
    windDir = "N";
    windDirAng = 338; 
  }
  else if (vin < 900){
    windDir = "SO";
    windDirAng = 203; 
  }
  else if (vin < 1500){
    windDir = "NE";
    windDirAng = 23; 
  }
  else if (vin < 2500){
    windDir = "S";
    windDirAng = 158; 
  }
  else if (vin < 3500){
    windDir = "SE";
    windDirAng = 113; 
  }
  else if (vin < 5000){
    windDir = "E";
    windDirAng = 68; 
  }

  Serial.print(F("Direccion del viento: "));
  Serial.println(windDir);
  
}

//=======================================================
// Obtiene los datos de luminosidad
//=======================================================
void getLightMeter(){
  Lux = lightMeter.readLightLevel(true);
  Serial.print(F("Luminosidad: "));
  Serial.print(Lux);
  Serial.println(F(" Lx"));  
}


void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("Failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels){
                listDir(fs, file.name(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

void createDir(fs::FS &fs, const char * path){
    Serial.printf("Creating Dir: %s\n", path);
    if(fs.mkdir(path)){
        Serial.println("Dir created");
    } else {
        Serial.println("mkdir failed");
    }
}

void removeDir(fs::FS &fs, const char * path){
    Serial.printf("Removing Dir: %s\n", path);
    if(fs.rmdir(path)){
        Serial.println("Dir removed");
    } else {
        Serial.println("rmdir failed");
    }
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

void renameFile(fs::FS &fs, const char * path1, const char * path2){
    Serial.printf("Renaming file %s to %s\n", path1, path2);
    if (fs.rename(path1, path2)) {
        Serial.println("File renamed");
    } else {
        Serial.println("Rename failed");
    }
}

void deleteFile(fs::FS &fs, const char * path){
    Serial.printf("Deleting file: %s\n", path);
    if(fs.remove(path)){
        Serial.println("File deleted");
    } else {
        Serial.println("Delete failed");
    }
}

void testFileIO(fs::FS &fs, const char * path){
    File file = fs.open(path);
    static uint8_t buf[512];
    size_t len = 0;
    uint32_t start = millis();
    uint32_t end = start;
    if(file){
        len = file.size();
        size_t flen = len;
        start = millis();
        while(len){
            size_t toRead = len;
            if(toRead > 512){
                toRead = 512;
            }
            file.read(buf, toRead);
            len -= toRead;
        }
        end = millis() - start;
        Serial.printf("%u bytes read for %u ms\n", flen, end);
        file.close();
    } else {
        Serial.println("Failed to open file for reading");
    }


    file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("Failed to open file for writing");
        return;
    }

    size_t i;
    start = millis();
    for(i=0; i<2048; i++){
        file.write(buf, 512);
    }
    end = millis() - start;
    Serial.printf("%u bytes written for %u ms\n", 2048 * 512, end);
    file.close();
}
