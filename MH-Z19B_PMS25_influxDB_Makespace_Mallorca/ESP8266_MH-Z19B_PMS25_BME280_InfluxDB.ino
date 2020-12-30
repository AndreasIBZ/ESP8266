// Wemos D1 mini (ESP8266), MH-Z19B, PMS5003 (ZH0x), BME280
// 07.12.2020, @Andreas_IBZ (Telegram)

// Wiring BME280 => ESP
// SDA => D2
// SCL => D1

// Wiring MH-Z19B => ESP
// RX => TX
// TX => RX

#if defined(ESP32)
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#define DEVICE "ESP32"
#elif defined(ESP8266)
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti wifiMulti;
#define DEVICE "ESP8266"
#endif

#include <InfluxDbClient.h> // https://github.com/teebr/Influx-Arduino
#include "time.h" // for ESP8266
#include <Wire.h>
#include "WiFi_credentials.h" // credenciales del WiFi
#include "InfluxDB.h" // credenciales y certificado del servidor influx
#include <SparkFunBME280.h> // Click here to get the library: http://librarymanager/All#SparkFun_BME280
#include <SoftwareSerial.h>
#include <MHZ19.h> // library for MH-Z19B: https://github.com/strange-v/MHZ19
// Definitions for the ZH07 Particle-Sensor 
#define LENG 31   //0x42 + 31 bytes equal to 32 bytes


// adapt these values to your needs
unsigned long PERIOD = 180000; // capture period in milliseconds, 60000 = 60 s = 1 min, 180000 = 180 s = 3 min
const char sensortag[] = "CODOS-001"; // every sensor uploads to a "Serie" which is prefixed with "Sensor" => "Sensor=CODOS_test" - i suggest to use a unique number/name
bool debuginfo = true; // debuginfo via serial, put to false to write CSV formatted data through serial
// Measurement variables
long val_CO2 = 0; // valor CO2 [ppm]
float val_tempC = 0.0; // valor Temperatura [° C]
float val_accCO2 = 0.0; // valor accuracy [? % ?]
long val_minCO2 = 0.0; // valor min CO2 [ppm]
long val_PM1_0 = 0; // value PM1.0 [? µg/m³ ?]
long val_PM2_5 = 0; // value PM2.5 [? µg/m³ ?]
long val_PM10;  // value PM10 [? µg/m³ ?]
long val_eCO2 = 0; // valor eCO2
long val_TVOC = 0; // valor TVOC
float BMEtempC = 0.0; // valor Temperatura
float BMEhumid = 0.0; // valor Humedad
float BMEpres = 0.0; // valor Presión
uint16_t currBaseline = 0; // current baseline holding variable
uint16_t SerialBaudrate = 57600; // Baudrate Serial Monitor on UART1 via external TTL-to-RS232-Converter
unsigned long startTime;

//// WiFi AP SSID
//#define WIFI_SSID "SSID"
//// WiFi password
//#define WIFI_PASSWORD "PASSWORD"
//// InfluxDB v2 server url, e.g. https://eu-central-1-1.aws.cloud2.influxdata.com (Use: InfluxDB UI -> Load Data -> Client Libraries)
//#define INFLUXDB_URL "server-url"
//// InfluxDB v2 server or cloud API authentication token (Use: InfluxDB UI -> Load Data -> Tokens -> <select token>)
//#define INFLUXDB_TOKEN "server token"
//// InfluxDB v2 organization id (Use: InfluxDB UI -> Settings -> Profile -> <name under tile> )
//#define INFLUXDB_ORG "org id"
//// InfluxDB v2 bucket name (Use: InfluxDB UI -> Load Data -> Buckets)
//#define INFLUXDB_BUCKET "bucket name"

// Set timezone string according to https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
//  Central Europe: "CET-1CEST,M3.5.0,M10.5.0/3"
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"
//// NTP servers the for time syncronozation.
//// For the fastest time sync find NTP servers in your area: https://www.pool.ntp.org/zone/
#define NTP_SERVER1  "pool.ntp.org"
#define NTP_SERVER2  "time.nis.gov"

// InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client;

SoftwareSerial SoftSer(D5,D6);

// Instantiate the MH-Z19B CO2-Sensor with UART0
MHZ19 mhz(&Serial);

// Instantiate the BME280 Environment-Sensor with its I²C-Address
BME280 myBME280;

void setup() {

  // use only-TX-Serial UART1 for Debug-Output to free UART0 for communication
  Serial1.begin(SerialBaudrate, SERIAL_8N1); // init Serial Monitor
  Serial1.setDebugOutput(true);

  // Setup wifi
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_NAME, WIFI_PASS);

  Serial1.print("Connecting to wifi");
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial1.print(".");
    delay(100);
  }
  Serial1.println();
  
  // Start I²C Communication
  Wire.begin();
  delay(200);
  
  // BME280: Set I²C Communication parameters
  myBME280.settings.commInterface = I2C_MODE;
  myBME280.settings.I2CAddress = 0x76;
  myBME280.settings.runMode = 3; //Normal mode
  myBME280.settings.tStandby = 0;
  myBME280.settings.filter = 4;
  myBME280.settings.tempOverSample = 5;
  myBME280.settings.pressOverSample = 5;
  myBME280.settings.humidOverSample = 5;
  //Calling .begin() causes the settings to be loaded
  myBME280.begin();
  delay(100); //Make sure sensor had enough time to turn on. BME280 requires 2ms to start up.
  
  // Accurate time is necessary for certificate validation and writing in batches
  // For the fastest time sync find NTP servers in your area: https://www.pool.ntp.org/zone/
  // Syncing progress and the time will be printed to Serial.
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  // Check server connection
  client.setConnectionParamsV1(INFLUX_URL, INFLUX_DATABASE, INFLUX_USER, INFLUX_PASS, ROOT_CERT);
  if (client.validateConnection()) {
    Serial1.print("Connected to InfluxDB: ");
    Serial1.println(client.getServerUrl());
  } else {
    Serial1.print("InfluxDB connection failed: ");
    Serial1.println(client.getLastErrorMessage());
  }

// MH-Z19B: Initialize
  Serial.begin(9600);
  Serial.swap(); // switch UART0 to GPIO13/GPIO15 (pins D7/D8)
  delay(1000);
  mhz.setAutoCalibration( false );  // default settings - off autocalibration
  delay(200);
//  Serial1.print( "Acuracy:" ); Serial.println(mhz.getAccuracy()? "ON" : "OFF" );
//  Serial1.println( "Detection Range: " ); Serial.println( 5000 );

  // ZH07: Initialize
  SoftSer.begin(9600);
  Serial.setTimeout(2000);    //set the Timeout to 1500ms, longer than the data transmission periodic time of the sensor  
  delay(1000);
  
  startTime = millis();

}

void loop() {
if ((millis() - startTime) >= PERIOD) { 

  // Check to see if data is available from MH-Z19B
  MHZ19_RESULT response = mhz.retrieveData();
  if (response == MHZ19_RESULT_OK)
  {
    if (debuginfo) {
    Serial1.print(F("MH-Z19B CO2: "));
    Serial1.print(mhz.getCO2());
    Serial1.print(F(", Min CO2: "));
    Serial1.print(mhz.getMinCO2());
    Serial1.print(F(", Temperature: "));
    Serial1.print(mhz.getTemperature());
    Serial1.print(F(", Accuracy: "));
    Serial1.println(mhz.getAccuracy());
    }
  } else {
    Serial1.print( "Error Reading MH-Z19B Module" );
    Serial1.print(F("Error, code: "));
    Serial1.println(response);
  }
  
  // Read MH-Z19B data    
    val_tempC = mhz.getTemperature();
    val_CO2 = mhz.getCO2();
    val_accCO2 = mhz.getAccuracy();
    val_minCO2 = mhz.getMinCO2(); 
    Serial.flush();
    
  // Read BME280 data
    BMEtempC = myBME280.readTempC();
    BMEhumid = myBME280.readFloatHumidity();
    BMEpres = (myBME280.readFloatPressure()) / 100; // Conversion to hPa
    if (debuginfo) {
            Serial1.print("BME280 T: ");
            Serial1.print(BMEtempC);        
            Serial1.print(", rH: ");
            Serial1.print(BMEhumid); 
            Serial1.print(", p: ");
            Serial1.println(BMEpres);
         } 

  // Get PM data from ZH07 // #################### ToDo: move to library #######################
  if(SoftSer.find(0x42)){    // start to read when detect 0x42
    unsigned char buf[LENG];
    SoftSer.readBytes(buf,LENG);
    if(buf[0] == 0x4d){
      if(checkValue(buf,LENG)){
        val_PM1_0 = transmitPM01(buf); //count PM1.0 value of the air detector module
        val_PM2_5 = transmitPM2_5(buf);//count PM2.5 value of the air detector module
        val_PM10 = transmitPM10(buf); //count PM10 value of the air detector module 
          if (debuginfo) {
            Serial1.print("ZH07 PM1.0: ");
            Serial1.print(val_PM1_0);        
            Serial1.print(", PM2.5: ");
            Serial1.print(val_PM2_5); 
            Serial1.print(", PM10: ");
            Serial1.println(val_PM10);
         }
      }           
    } 
  }
  SoftSer.flush();
  
  if ((WiFi.RSSI() == 0) && (wifiMulti.run() != WL_CONNECTED))
    Serial1.println("Wifi connection lost");
    
// initialize our variables.
    char tags[64];
    char fields[1024];
    // prefix tag of sensor with "Sensor="
    char tagbuf[32];
    const char sensor[] = "Sensor=";
    strcpy(tagbuf,sensor);
    strcat(tagbuf,sensortag);
    // write the tag with a description of the sensor
    sprintf(tags,tagbuf); 
    // write values
    sprintf(fields,"CO2[ppm]=%d,T_MH[°C]=%0.1f,accuracy[o/o]=%0.1f,CO2_min[ppm]=%d,PM1_0[µg/m³]=%d,PM2_5[µg/m³]=%d,PM10[µg/m³]=%d,eCO2[ppm]=%d,TVOC[ppb]=%d,baseline=%d,T_BME[°C]=%0.1f,p[hPa]=%0.1f,rH[o/o]=%0.1f",val_CO2,val_tempC,val_accCO2,val_minCO2,val_PM1_0,val_PM2_5,val_PM10,val_eCO2,val_TVOC,currBaseline,BMEtempC,BMEpres,BMEhumid); // escribir valores: CO2, Temperatura, accuracy y CO2_min
    // write to influx
    String data = String(INFLUX_MEASUREMENT) + "," + String(tags) + " " + String(fields);
    Serial1.println(data);
    if (!client.writeRecord(data)) {
    Serial1.print("InfluxDB write failed: ");
    Serial1.println(client.getLastErrorMessage());
  }
  
  startTime = millis();
}
}

// #################### ToDo ZH07: move to library #######################

char checkValue(unsigned char *thebuf, char leng)
{  
  char receiveflag=0;
  int receiveSum=0;
 
  for(int i=0; i<(leng-2); i++){
  receiveSum=receiveSum+thebuf[i];
  }
  receiveSum=receiveSum + 0x42;
 
  if(receiveSum == ((thebuf[leng-2]<<8)+thebuf[leng-1]))  //check the serial data 
  {
    receiveSum = 0;
    receiveflag = 1;
  }
  return receiveflag;
}
int transmitPM01(unsigned char *thebuf)
{
  int PM01Val;
  PM01Val=((thebuf[3]<<8) + thebuf[4]); //count PM1.0 value of the air detector module
  return PM01Val;
}
//transmit PM Value to PC
int transmitPM2_5(unsigned char *thebuf)
{
  int PM2_5Val;
  PM2_5Val=((thebuf[5]<<8) + thebuf[6]);//count PM2.5 value of the air detector module
  return PM2_5Val;
  }
//transmit PM Value to PC
int transmitPM10(unsigned char *thebuf)
{
  int PM10Val;
  PM10Val=((thebuf[7]<<8) + thebuf[8]); //count PM10 value of the air detector module  
  return PM10Val;
}
