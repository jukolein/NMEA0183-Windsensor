#include <ESP8266WiFi.h> // ESP8266 library, http://arduino.esp8266.com/stable/pa...com_index.json
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager
#include <Wire.h>
#include <WiFiUdp.h>
#include <AS5600.h>
AMS_5600 ams5600;

//************************************************** ******
// Global variables
//************************************************** ******
#define DELAY_MS 1000 // MS to wait
#define DELAY_COUNT 1 // Counter of DELAY_MS to wait, 3600 is 1 hour
#define PORT 8080 // Port to connect
#define LED 2 // Built-in LED, on when broadcast

char CSSentence[85] = "" ; //string for sentence assembly
char MWVSentence[85] = "" ; //string for MWV for sentence assembly (Wind Speed and Angle)
char e[50] = "0";
char d[5] = "0";
char c[50] = "0";
volatile boolean blStatusChange = false; 
volatile long times = millis(); 
long oldtimes = millis(); 

//--------------------------------------------------------
// ESP8266 WiFi variables
//--------------------------------------------------------
WiFiServer server(PORT); // Server/port
IPAddress broadcastIP(255, 255, 255, 255);               // Senden an alle Netzwerkteilnehmer mit dieser Multicastadresse.
WiFiUDP Udp;
String header;



//************************************************** ******
// Support functions
//************************************************** ******

//--------------------------------------------------------
// nmea_checksum - create NMEA checksum
// Expecting NMEA string that begins with '$' and ends with '*'
// $IIMDA,30.1,I,1.02,B,20.1,C,,C,,,,,,,,,,,,M*
// $IIXDR,C,70.2,F,TempAir,P,1.03,B,Barometer*
//--------------------------------------------------------
String nmea_checksum(String s) {
char checksum = 0;

if( s[0]=='$' && s[s.length()-1]== '*') {
for(int i=1; i<s.length()-1;i++) {
checksum = checksum xor s[i];
}
}
return String(checksum,HEX);
}

String Checksum(char* CSSentence) 
{
  int cs = 0; //clear any old checksum
 char e[50] = "0";
 for (int n = 0; n < strlen(CSSentence); n++) {
    cs ^= CSSentence[n]; //calculates the checksum
  }
  sprintf(e, "%02X", cs);
  return e;
}

 
ICACHE_RAM_ATTR void WinSensInterupt() 
{ 
Serial.print("I");
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
Serial.println(times);
}}
 
void Daten() {

  int Z = 0;
  long WS = 0;
  byte bySpeedCorrection = 100;

 blStatusChange = true;
 
if (blStatusChange) { 
Serial.println(times);
    WS = ((33 * bySpeedCorrection) / (times));
     // Drehfrequenz = K * V / r. 
   // Wobei K der Koeffizient = 0,379 ist,
   // V die entgegenkommende Strömungsgeschwindigkeit ist, 
   // r der Radius von der Achse zur Mitte des Bechers ist
   // Laufradradius 40mm. Drehzahl (im Bogenmaß) = r (40 mm) * 2,64 (1 / 0,379) / t. 
   // Wir übersetzen in Umdrehungen und alles wird zehnmal erhöht, um Zehntel der Geschwindigkeit zu erreichen
   // neu: w (Winkelgeschwindigkeit) = K * V / r. 
   // Wobei K der Koeffizient = 0,379 ist, 
   // V die entgegenkommende Strömungsgeschwindigkeit ist, 
   // r der Radius von der Achse zur Mitte des Bechers ist
   // V = ​​(w * r) / K => V = ((2 * 3,14 / T) * r) / K. T-Periode. 
   // V = 0,66 / T. TK Vorperiode haben wir eine halbe Umdrehung, dann ist V = 0,33 / T.
   
  
    Serial.println(WS);
  } 
  else {
    WS = 0;
  }
     

  // Prüfsumme errechnen ******************************************
  //Assemble a sentance of the various parts so that we can calculate the proper checksum NMEA
  MWVSentence[0] = '\0';
  strcpy(MWVSentence, "PIMWV,"); //The string could be kept the same as what it came in as, but I chose to use the P (prefix for all proprietary devices) and I for instrument 
  Z =  convertRawAngleToDegrees(ams5600.getRawAngle());
//  Z = Z /11.375;        //  4095/360  von 0 bis 360 Grad
  sprintf(e, "%d", Z);
  strcat(MWVSentence, e);
  strcat(MWVSentence, ",R,");
  sprintf(d, "%d", WS/10); //d represent the meters/seconds
  sprintf(c, ".%d", (WS%10)); //c represent the deci knots

 // sprintf(c, ".%d", (WS - ((WS / 10) * 10))); //c represent the deci knots
  strcat(MWVSentence, d);
  strcat(MWVSentence, c);
  strcat(MWVSentence, ",M,A");
 //****************************************************************** 
///////////////////// NMEA to serial //////////////////////////
 // NMEA(MWVSentence);

  Udp.beginPacket(broadcastIP, PORT);
  
  Udp.print("$WIMWV,"); // Assemble the final message and send it out the serial port
  Udp.print(e);
  Udp.print(",R,");
  Udp.print(d);
  Udp.print(c);
  Udp.print(",M,A");
  Udp.print("*");
  Udp.print(Checksum(MWVSentence)); // Call Checksum function

  Udp.endPacket();

  Serial.print("$WIMWV,"); // Assemble the final message and send it out the serial port
  Serial.print(e);
  Serial.print(",R,");
  Serial.print(d);
  Serial.print(c);
  Serial.print(",M,A");
  Serial.print("*");
  Serial.print(Checksum(MWVSentence)); // Call Checksum function
  Serial.print("\r");
  Serial.print("\n");
  Serial.println();
}

/*******************************************************
/* Function: convertRawAngleToDegrees
/* In: angle data from AMS_5600::getRawAngle
/* Out: human readable degrees as float
/* Description: takes the raw angle and calculates
/* float value in degrees.
/*******************************************************/
float convertRawAngleToDegrees(word newAngle)
{
  /* Raw data reports 0 - 4095 segments, which is 0.087 of a degree */
  float retVal = newAngle * 0.087;
  int ang = retVal;
  return ang;
}


//************************************************** ******
// Setup
//************************************************** ******
void setup() {

Serial.begin(74880);         // Start the Serial communication to send messages to the computer
Wire.begin();
pinMode(LED, OUTPUT); // Initialize the LED pin as an output
pinMode(2, INPUT_PULLUP);


Serial.print("Versuche, zu verbinden");

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

  Udp.begin(PORT);

 if(ams5600.detectMagnet() == 0 ){
    while(1){
        if(ams5600.detectMagnet() == 1 ){
            Serial.print("Current Magnitude: ");
            Serial.println(ams5600.getMagnitude());
            break;
        }
        else{
            Serial.print("Can not detect magnet");
        }
        delay(1000);
    }
 }


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

//************************************************** ******
// Loop
//************************************************** ******
void loop() {

ArduinoOTA.handle();

  WiFiClient client = server.available();   // Listen for incoming clients

  if (client) {                             // If a new client connects,
    Serial.println("New Client.");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();
            

// Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons 
            // Feel free to change the background-color and font-size attributes to fit your preferences
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {background-color: #77878A;}</style></head>");
            
            // Web Page Heading
            client.println("<body><h1>ESP8266 Web Server</h1>");
// The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
 // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
  }

Daten();

digitalWrite(LED, HIGH);

// Wait for next reading
for(int c=0; c < DELAY_COUNT; c++) {
delay(DELAY_MS);
}

}
