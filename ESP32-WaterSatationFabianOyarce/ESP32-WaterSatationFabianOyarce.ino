#include <WiFi.h>
#include <Wire.h>
#include <time.h>
#include <BH1750.h>
#include <Adafruit_Sensor.h>

//#include <Adafruit_BME280.h>
#include "Adafruit_BME680.h"

//Adafruit_BME280 bme;              // I2C
Adafruit_BME680 bme; // I2C
BH1750 lightMeter;

// Change to your WiFi credentials and select your time zone
const char* ssid     = "Fabi";
const char* password = "1234567890";
const char* Timezone = "<-04>4<-03>,M9.1.6/24,M4.1.6/24"; 

//Calibration factors, extent of wind speed average, and Wind Sensor pin adjust as necessary
#define pressure_offset 0       // Air pressure calibration, adjust for your altitude
#define WS_Calibration  2.4       // Wind Speed calibration factor
#define WS_Samples      10        // Number of Wind Speed samples for an average
#define WindSensorPin   15

// Asignacion de pines GPIO usando chip ESP32 con WiFi y Bluetooth
#define RAIN_PIN     25 //D25
#define WIND_DIR_PIN 35 //D35


//Variable direccion del viento.
String windDir = "";

//Variables y constantes utilizadas en el seguimiento de la lluvia.
#define MS_IN_DAY         86400000
#define MS_IN_HR          360000
#define MS_FACTOR_SLEEP      5000

//Variables lluvia caida
float rain = 0.0;
float rainHours = 0.0;
float rain24Hours = 0.0;
volatile int rainTicks = 0;

float rainCalibrationFactor = 6.5; //Ajuste de calibracion para lluvia segun la cantidad de tick por cubeta que lanza el sensor. Recordar que cada cubeta equivale a 0.2794 mm de lluvia caida

unsigned long timeD = 0;
unsigned long timeDay = 0;
unsigned long timeSleep = 0;
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
  
  if (vin == 0 ) windDir="O";
  else if (vin < 150) windDir="NO";
  else if (vin < 400) windDir = "N";
  else if (vin < 900) windDir = "SO";
  else if (vin < 1500) windDir = "NE";
  else if (vin < 2500) windDir = "S";
  else if (vin < 3500) windDir = "SE";
  else if (vin < 5000) windDir = "E";

  Serial.print(F("Direccion del viento: "));
  Serial.println(windDir);
  
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
