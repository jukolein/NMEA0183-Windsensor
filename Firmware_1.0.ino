#include <ESP8266WiFi.h> // ESP8266 library, http://arduino.esp8266.com/stable/pa...com_index.json
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager
#include <Wire.h>
#include <WiFiUdp.h>
#include <AS5600.h>   //https://github.com/Seeed-Studio/Seeed_Arduino_AS5600


//********************************************************
// Global variables
//********************************************************

#define DELAY_MS 1000 // MS to wait
#define DELAY_COUNT 1 // Counter of DELAY_MS to wait, 3600 is 1 hour
#define PORT 8080 // Port to connect
#define LED 2 // Built-in LED

AMS_5600 ams5600;  //select correct chip

char CSSentence[30] = "" ; //string for sentence assembly
char MWVSentence[30] = "" ; //string for MWV for sentence assembly (Wind Speed and Angle)
char e[12] = "0";  //string for storing the angle
char d[5] = "0";   //string for storing the wind speed before the dot
char c[12] = "0";  //string for storing the wind speed after the dot
volatile boolean blStatusChange = false;  //boolean for storing the current state of the hall sensor
volatile long times = millis();  //used to calculate the time between two status changes
long oldtimes = millis();   //used to calculate the time between two status changes


//--------------------------------------------------------
// ESP8266 WiFi variables
//--------------------------------------------------------

WiFiServer server(PORT); // Server/port
IPAddress broadcastIP(255, 255, 255, 255); // use the broadcast IP to reach all possible listners
WiFiUDP Udp;  //UPD protocol, because it is easy to handle and has less overhead than TCP

//********************************************************
// Support functions
//********************************************************

//--------------------------------------------------------
// nmea_checksum - create NMEA checksum
// Expecting NMEA string that begins with '$' and ends with '*'
// $IIMDA,30.1,I,1.02,B,20.1,C,,C,,,,,,,,,,,,M*
// $IIXDR,C,70.2,F,TempAir,P,1.03,B,Barometer*
//--------------------------------------------------------

String checksum(char* CSSentence) {
  int cs = 0; //stores the generated dezimal-checksum
  char f[30] = "0";  //stores the final hex-checksum
  for (int n = 0; n < strlen(CSSentence); n++) {
    cs ^= CSSentence[n]; //calculates the checksum
  }
  sprintf(f, "%02X", cs);  //convert the checksum into hex
  return f;
}


//--------------------------------------------------------
// WinSensInterupt - measure the time between the inerrupts
// As this function runs whenever an interrupt is triggered,
// it needs to have the "ICACHE_RAM_ATTR" at its beginning,
// so it can run in RAM
//--------------------------------------------------------

ICACHE_RAM_ATTR void WinSensInterupt() {
  if (times == 0) {
    oldtimes = millis();
    blStatusChange = false;
    times = 10;

  }
  else
  {
    blStatusChange = true;
    times = millis() - oldtimes;
    oldtimes = millis();
  }
}


//--------------------------------------------------------
// Function: convertRawAngleToDegrees
// In: angle data from AMS_5600::getRawAngle
// Out: human readable degrees as float
// Description: takes the raw angle and calculates
// float value in degrees.
//--------------------------------------------------------

float convertRawAngleToDegrees(word newAngle) {
  /* Raw data reports 0 - 4095 segments, which is 0.087 of a degree */
  float retVal = newAngle * 0.087;
  int ang = retVal;
  return ang;
}


//********************************************************
// Setup
//********************************************************
void setup() {

  Serial.begin(74880);         // Start the Serial communication to send messages to the computer
  Wire.begin();  //Initiate the Wire library and join the I2C bus
  pinMode(LED, OUTPUT); // Initialize the LED pin as an output
  pinMode(2, INPUT_PULLUP);  //Initialize Pin 2 as Input and PullUp, to avoid floating states

  Serial.print("begin Setup");


  //--------------------------------------------------------
  // WiFiManager - takes care of the WiFi
  // If the ESP can not connect to a known WiFi-Network,
  // it opens an AP to connect to and set up a connection
  // therefore, no physical access is requiered to change
  // networks. Furthermore, it offers OTA FW-Updates.
  //--------------------------------------------------------

  // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  // Uncomment and run it once, if you want to erase all the stored information
  //wifiManager.resetSettings();

  // set custom ip for portal
  //wifiManager.setAPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  // fetches ssid and pass from eeprom and tries to connect
  // if it does not connect it starts an access point with the specified name
  // here  "Windsensor"
  // and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect("Windsensor");

  // if you get here you have connected to the WiFi
  Serial.println("Connected.");

  server.begin();

  //--------------------------------------------------------
  // Initialize UDP
  //--------------------------------------------------------

  Udp.begin(PORT); //Setup UDP


  //--------------------------------------------------------
  // ArduinoOTA - allows OTA-FW-flashing with the Arduino IDE
  // must be included in every flash, or OTA-capability is lost
  //--------------------------------------------------------

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  attachInterrupt(digitalPinToInterrupt(2), WinSensInterupt, FALLING);

  Serial.print("leave setup");
}

//********************************************************
// Loop
//********************************************************

void loop() {

  ArduinoOTA.handle(); //deals with the ArduinoOTA-stuff, must be kept in to preserve posibility of OTA-FW-updates

  int Z = convertRawAngleToDegrees(ams5600.getRawAngle()); //stores the value of the angle
  long WS = 0;  //stores the value of the wind speed
  byte bySpeedCorrection = 100;  //value for finetuning the speed calculation

  WS = ((33 * bySpeedCorrection) / (times));  //calculating the wind speed

  //--------------------------------------------------------
  // Assembly of the NMEA0183-sentence to calculate the checksum
  // Currently,the programming is still somewhat inefficient,
  // as the NMEA-sentence is created three times: for the
  // checksum, the UDP and the SERIAL. This will be fixed
  // in further releases.
  //
  // Beware: sprintf works on raw storage. If the chars
  // are not initialized long enough, it will cause an
  // buffer overflow
  //--------------------------------------------------------

  strcpy(MWVSentence, "WIMWV,");
  sprintf(e, "%d", Z); //e represents the angle in degrees
  strcat(MWVSentence, e);
  strcat(MWVSentence, ",R,");
  sprintf(d, "%d", WS / 10); //d represent the meters/seconds
  sprintf(c, ".%d", (WS % 10)); //c represent the decimal
  strcat(MWVSentence, d);
  strcat(MWVSentence, c);
  strcat(MWVSentence, ",M,A");

  //--------------------------------------------------------
  // Assembly and sending of the NMEA0183-sentence via UDP
  //--------------------------------------------------------

  Udp.beginPacket(broadcastIP, PORT);

  Udp.print("$WIMWV,");
  Udp.print(e);
  Udp.print(",R,");
  Udp.print(d);
  Udp.print(c);
  Udp.print(",M,A");
  Udp.print("*");
  Udp.print(checksum(MWVSentence)); // Call Checksum function

  Udp.endPacket();


  //--------------------------------------------------------
  // Assembly and sending of the NMEA0183-sentence via SERIAL
  //--------------------------------------------------------

  Serial.print("$WIMWV,");
  Serial.print(e);
  Serial.print(",R,");
  Serial.print(d);
  Serial.print(c);
  Serial.print(",M,A");
  Serial.print("*");
  Serial.print(checksum(MWVSentence)); // Call Checksum function
  Serial.print("\r");
  Serial.print("\n");
  Serial.println();

  // Wait for next reading
  for (int c = 0; c < DELAY_COUNT; c++) {
    delay(DELAY_MS);
  }
}
