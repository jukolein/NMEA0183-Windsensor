#include <ESP8266WiFi.h> // ESP8266 library, http://arduino.esp8266.com/stable/pa...com_index.json
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager
#include <Wire.h>
#include <AS5600.h>   //https://github.com/Seeed-Studio/Seeed_Arduino_AS5600
#include <FS.h>

//******************************************************************************************************************************************************
// Global variables
//******************************************************************************************************************************************************

#define DELAY_MS 1000 //MS to wait
#define DELAY_COUNT 1 //Counter of DELAY_MS to wait, 3600 is 1 hour
#define TCP_PORT 8080 //Port to connect for to receive the NMEA sentences via TCP
#define HTTP_PORT 80  //Port to reach the HTTP webserver 
#define MAX_CLIENTS 3 //maximal number of simultaneousy connected clients

AMS_5600 ams5600;  //select correct chip

char CSSentence[30] = "" ; //string for handing the MWV-sentence over to the checksum-function
char MWVSentence[30] = "" ; //string for MWV-sentence assembly (Wind Speed and Angle)
char e[12] = "0";  //string for storing the angle
char d[5] = "0";   //string for storing the wind speed before the dot
char c[12] = "0";  //string for storing the wind speed after the dot
char g[80] = "0";  //string for storing the calculated checksum as a char array
char result[60] = "0";  //string for NMEA-assembly (dollar symbol, MWV, CS)
volatile boolean blStatusChange = false;  //boolean for storing the current state of the hall sensor
volatile long times = millis();  //used to calculate the time between two status changes
long oldtimes = millis();   //used to calculate the time between two status changes

String offset;  //string for storing the offset of the wind angle
String factor;  //string for storing the liear correction facor of the wind speed
String blIPblink;  //string for storing if the IP is to be blinked or not. "geblinkt" equals "true", "nicht geblinkt" equals "false"
String ws_unit;  //string for storing the unit of the wind speed, like knots or meters per second, only affects the graphical display

//--------------------------------------------------------
// ESP8266 WiFi and webserver variables
//--------------------------------------------------------

WiFiServer server(TCP_PORT); //define the server port
WiFiClient clients[MAX_CLIENTS]; //Array of clients

ESP8266WebServer webserver(HTTP_PORT); //define the HTTP-webserver port


//******************************************************************************************************************************************************
// HTML PAGES
//******************************************************************************************************************************************************

//--------------------------------------------------------
// correction_page - the HTM code for the page where
// the correction values are set
// located in PROGMEM, as it is going to be static
//--------------------------------------------------------

const char correction_page[] PROGMEM = R"=====(
<!DOCTYPE html>
<head>
  <meta content="text/html; charset=UTF-8" http-equiv="content-type">
</head>
<html>
<body>
<h2>Windsensor III<h2>
<h3>Korrekturdaten</h3>
<form action="/action_page">
  Vorzeichenbehafteter Offset der Windrichtung in Grad:<br>
  <input type="text" name="offset_html" value="offset_placeholder">
  <br>  <br>
  linearer Korrekturfaktor der Windgeschwindigkeit:<br>
  <input type="text" name="factor_html" value="factor_placeholder">
  <br>  <br>
  <input type="submit" value="Anwenden">
</form> 
<br>  <br>
   Aktuell wird die IP beim Start ipblink_placeholder <br>
   <input type="button" onclick="location.href='/ipblink';" value="Ändern" />
<br>  <br>
<br>  <br>
   Aktuelle Einheit der Windgeschwindigkeit: ws_unit_placeholder <br>
   <input type="button" onclick="location.href='/meters';" value="Meter pro Sekunde" />
   <input type="button" onclick="location.href='/kilometers';" value="Kilometer pro Stunde" />
   <input type="button" onclick="location.href='/knots';" value="Knoten" />
<br>  <br>
   <a href='/'> Zur Windanzeige </a>
</body>
</html>
)=====";


//--------------------------------------------------------
// graphic - the HTM code for the page where 
// the wind data is displayed graphically
// located in PROGMEM, as it is going to be static
//--------------------------------------------------------

// MANY THANKS TO https://ziegenhagel.com FOR WRITING THE CODE

const char graphic[] PROGMEM = R"=====(
<div class="pos_upper_right">
    <a href="/config">Einstellungen</a>
</div>
<div id="instrument" class="layer">
    <div id="wind_direction" class="layer">--- deg</div>
    <div id="wind_speed" class="layer">--- m/s</div>
    <div id="nesw" style="margin-top:-5px">
        <div class="layer" style="transform:rotate(0deg)">0</div>
        <div class="layer" style="transform:rotate(90deg)">90</div>
        <div class="layer" style="transform:rotate(180deg)"><div style="transform:rotate(180deg);">180</div></div>
        <div class="layer" style="transform:rotate(270deg)">270</div>
    </div>
    <div id="degs"> </div>
    <div id="pointer" class="layer"><div id="needle"></div></div>
    <div id="dot_on_needle" class="layer"></div>
    <div id="reflection" style="background: linear-gradient(#2223, #2220); height: 140%; width: 200%; margin-left: -50%; margin-top: 41%; opacity: 0.5; " class="layer"></div>
</div>
<script>
    // version 0.3
    // config, change to your needs
    const API_URL = "/data"
    const API_ENABLED = true
    const API_REFRESH_MS = 1000
    // code, don't touch
    let el_pointer = document.getElementById("pointer")
    let el_direction = document.getElementById("wind_direction")
    let el_speed = document.getElementById("wind_speed")
    let degs = document.getElementById("degs")
    function refresh_gui(data) {
        el_pointer.style.transform = "rotate("+data[1]+"deg)"
        el_direction.innerHTML = data[1]+"&deg;"
        el_speed.innerHTML = ((parseFloat(data[3]) * unit_manipulation
    }
    function build_gui() {
        let color
        for(let i=0;i<360; i+=10) {
            if(i%90 != 0) {
                if(i > 0 && i < 60) color="green"
                else if(i < 360 && i > 300) color="red"
                else color="gray"
                degs.innerHTML += '<div class="layer" style="transform:rotate('+i+'deg);"><span style="background-color:'+color+';"></span></div>' 
            }
        }
    }
    function refresh_data() {
        fetch(API_URL)
        .then((data) => data.text())
        .then((data) => {
            console.log("API response:",data)
            refresh_gui(data.split(","))
        })
        .catch((error) => {
            console.error("Error:", error)
        })
    }
    build_gui()
    if(API_ENABLED)
        setInterval(refresh_data,API_REFRESH_MS)
    else
        refresh_gui("$WIMWV,250.5,R,1.8,M,A*38".split(","))
</script>
<style>
    body {
        display:flex;
        justify-content:center;
        align-items:center;
        background:#222;
    }
    #degs {
        margin-top:5px;
    }
    #degs > div {
        padding-top:0px;
        color:#fff2;
        font-weight:bold;
    }
    #degs > div > span {
        height:21px;
        width:10px;
        display:inline-block;
    }
    #nesw > div {
        padding-top:11px;
        color:#ccc;
    }
    #instrument {
        position:relative;
        background:linear-gradient(#222,#555);
        border:6px solid gray;
    }
    .layer {
        font-size:30px;
        text-align:center;
        width:400px;
        height:400px;
        position:absolute;
        border-radius:50%;
    }
    #pointer {
        transition:1s;
    }
    #needle {
        background:linear-gradient(#d00,#a00);
        width:9px;
        height:32%;
        border-radius:4px;
        margin:auto;
        margin-top:-6%;
        box-shadow:5px 5px 8px #333;
    }
    #dot_on_needle {
        border-radius:50%;
        width:40px;height:40px;
        margin:-20px;
        left:50%;
        top:50%;
        background:#555;
        box-shadow:2px 2px 3px #1113, inset -3px -3px 10px #0005, inset 3px 3px 10px #fff3;
    }
    #wind_speed, #wind_direction {
        font-family:arial;
        background: #B1BAA9;
        padding:10px;
        height:auto;
        width:110px;
        left:50%;
        margin-left:-65px;
        top:23%;
        box-shadow: inset 2px 2px 3px #0007, inset -2px -2px 3px #fff3;
        border-radius:2px;
    }
    #wind_speed {
        top:63%;
        margin-top:10px;
    } .pos_upper_right {
        padding:10px;
        position:absolute;
        right:0;
        top:0;
    } .pos_upper_right a {
        color:gray;
        font-family:arial;
        text-decoration:none;
    } .pos_upper_right a:hover {
        color:white;
        text-decoration:underline;
    }
   </style>
)=====";


//******************************************************************************************************************************************************
// Support functions for the core code and calculation
//******************************************************************************************************************************************************
//--------------------------------------------------------

// ip_blink - Blinks the IP address of the ESP with the build
// in LED, so you can connect to it without having to scan
// the network or connecting via serial.
// Syntax: It blinks ever digit seperatly, and, in order to
// display the 0, it blinks n+1 times. The dot is indicated
// by three rapid flashes.
// Example: "192." will be displayed as:
// 2 FLASHES, BREAK, 10 FLASHES, BREAK, 3 FLASHES, 3 RAPID FLASHES
//--------------------------------------------------------

void ip_blink() {
  String ip = WiFi.localIP().toString();  //sore current IP in string
  for (int i = 0; i <= ip.length(); i++) {
    if (ip.charAt(i) == '.') {  //check for dots and indicate them by three rapid flashes
      for (int i = 0; i <= 2; i++) {
        digitalWrite(LED_BUILTIN, LOW);   //turn the LED on (HIGH is the voltage level)
        delay(100);                       //wait for a second
        digitalWrite(LED_BUILTIN, HIGH);   //turn the LED on (HIGH is the voltage level)
        delay(100);                       //wait for a second
      }
    }
    else {
      int times = ip.charAt(i) - '0';  //store the digit at the current position as int
      //    Serial.print(times);
      for (int j = 0; j <= times; j++) {
        digitalWrite(LED_BUILTIN, LOW);   //turn the LED on (HIGH is the voltage level)
        delay(200);                       //wait for a second
        digitalWrite(LED_BUILTIN, HIGH);    //turn the LED off by making the voltage LOW
        delay(200);
      }
    }
    delay(1000);
  }
}

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


//******************************************************************************************************************************************************
// Support functions for the HTTP-webserver and the correction-values
//******************************************************************************************************************************************************

//--------------------------------------------------------
// readFile - return a string with the information stored 
// in the txt-file that was handed over to the function
//--------------------------------------------------------

String readFile(fs::FS &fs, const char * path){
  File file = fs.open(path, "r");  //open the named file with reading-rights
  if(!file || file.isDirectory()){  //check if there is a file to read from
    return String();
  }
  String fileContent;  //string to store the content of the file in
  while(file.available()){
    fileContent+=String((char)file.read());  //itterate through the file and store the findings in the string
  }
  file.close();
  return fileContent;
}


//--------------------------------------------------------
// writeFile - persistently store a given correction-value
// in a given txt-file
//--------------------------------------------------------

void writeFile(fs::FS &fs, const char * path, const char * message){
  File file = fs.open(path, "w");  //open the named file with writing-rights
  if(!file){  //check if there is a file to write to
    return;
  }
  if(file.print(message)){
    //if the writing was successful, the global variable with the correction-value
    //needs to be updated, so the changes take effect immediadly and don't
    //require a reboot or a permanent check in the loop()-function.
    //for simplicity, all values are updated. As the change of the values
    //should only rarely happen, that's not a big deal
    offset = readFile(SPIFFS, "/offset.txt");  //update the offset-value
    factor = readFile(SPIFFS, "/factor.txt");  //upate the factor-value
    blIPblink = readFile(SPIFFS, "/blipblink.txt");  //update the blIPblink state
    ws_unit = readFile(SPIFFS, "/ws_unit.txt");  //update the blIPblink state

  }
  file.close();
}


//--------------------------------------------------------
// handleCorrection - send the HTML-code for the  
// correction page to the client and display the current
// values
//--------------------------------------------------------

void handleCorrection() {
  String s = correction_page; //string to store the HTML page
  s.replace("offset_placeholder", offset);  //display current offset in the form
  s.replace("factor_placeholder", factor);  //display current facor in the form
  s.replace("ipblink_placeholder", blIPblink);  //display in the form if IP is going to be blinked at next boot
  s.replace("ws_unit_placeholder", ws_unit);  //display the unit of the windspeed

  webserver.send(200, "text/html", s); //Send web page
}


//--------------------------------------------------------
// handleForm - store the correction-values handed over by 
// the webpage into the permanent SPIFFS-storage of the ESP
//--------------------------------------------------------

void handleForm() {
 String offset_html = webserver.arg("offset_html"); //read the value after "offset_html" of the HTTP-GET-request send by the client
 String factor_html = webserver.arg("factor_html"); //read the value after "factor_html" of the HTTP-GET-request send by the client

 writeFile(SPIFFS, "/offset.txt", offset_html.c_str());  //persistently store it in a text file in the SPIFFS-section
 writeFile(SPIFFS, "/factor.txt", factor_html.c_str());  //persistently store it in a text file in the SPIFFS-section

 String link = "<a href='/config'> Einstellungen </a>";  //create a HTML-link that brings you back to the root page
 webserver.send(200, "text/html", link); //Send that link
}


//--------------------------------------------------------
// gui - send the HTML-code (or rather mainly JS) that  
// displays the wind data on a web page
//--------------------------------------------------------

void gui() {
  String s = graphic; //string to store the HTML page

  if(ws_unit.equals("Kilometer pro Stunde")){
    s.replace("unit_manipulation", "3.6)).toFixed(1) + \"km/h\"");
  }
  else if(ws_unit.equals("Knoten")){
    s.replace("unit_manipulation", "1.944)).toFixed(1) + \"kn\"");
  }
  else {
    s.replace("unit_manipulation", "1)).toFixed(1) + \"m/s\"");
  }
  webserver.send(200, "text/html", s); //Send web page
}


//--------------------------------------------------------
// data - called by the graphic-JS-script
// sends the HTML-code that displays the wind data as 
// plain NMEA0813 text. That is then fetched by the 
// graphics-API
//--------------------------------------------------------

void data() {
  String s = result; //string to store the final NMEA0813-sentence
  webserver.send(200, "text/html", s); //Send the NMEA0813-sentence
}


//--------------------------------------------------------
// ipBlinkToggle - if called, this function toggles the 
// value that defines if the IP is going to blinked at boot
//--------------------------------------------------------

void ipBlinkToggle() {
   String blIPblink_html;  //string for storing the value that will be written
   if (blIPblink.equals("nicht geblinkt")) {  //check current state, and, if "false" (that means "nicht geblinkt" in this case), change it to "true" ("geblinkt")...
    blIPblink_html = "geblinkt";}
   else {
    blIPblink_html = "nicht geblinkt";  //... and if it is "true", make it "false"
    }
    writeFile(SPIFFS, "/blipblink.txt", blIPblink_html.c_str());  //persistently store it in a text file in the SPIFFS-section
    String link = "<a href='/config'> Einstellungen </a>";  //create a HTML-link that brings you back to the root page
 webserver.send(200, "text/html", link); //Send that link
}


//--------------------------------------------------------
// ws_unitChange - if called, this function changes the 
// value of the unit to be used for the graphical display
//--------------------------------------------------------

void ws_unitChange_meters() {
    ws_unit = "Meter pro Sekunde";
    writeFile(SPIFFS, "/ws_unit.txt", ws_unit.c_str());  //persistently store it in a text file in the SPIFFS-section
    String link = "<a href='/config'> Einstellungen </a>";  //create a HTML-link that brings you back to the root page
    webserver.send(200, "text/html", link); //Send that link
}

void ws_unitChange_kilometers() {
    ws_unit = "Kilometer pro Stunde";
    writeFile(SPIFFS, "/ws_unit.txt", ws_unit.c_str());  //persistently store it in a text file in the SPIFFS-section
    String link = "<a href='/config'> Einstellungen </a>";  //create a HTML-link that brings you back to the root page
    webserver.send(200, "text/html", link); //Send that link
}

void ws_unitChange_knots() {
    ws_unit = "Knoten";
    writeFile(SPIFFS, "/ws_unit.txt", ws_unit.c_str());  //persistently store it in a text file in the SPIFFS-section
    String link = "<a href='/config'> Einstellungen </a>";  //create a HTML-link that brings you back to the root page
    webserver.send(200, "text/html", link); //Send that link
}

//******************************************************************************************************************************************************
// Setup
//******************************************************************************************************************************************************

void setup() {

  Serial.begin(74880);  //Start the Serial communication to send messages to the computer
  Wire.begin();  //Initiate the Wire library and join the I2C bus
  SPIFFS.begin();  //mount the SPIFFS storage
  
  pinMode(2, INPUT_PULLUP);  //Initialize Pin 2 as Input and PullUp, to avoid floating states
  pinMode(LED_BUILTIN, OUTPUT);  //Initialize the bulid in LED as an output
  digitalWrite(LED_BUILTIN, HIGH);   //turn the LED on (HIGH is the voltage level)

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


  //--------------------------------------------------------
  // softAP - creates an Access Point if a complete
  // stand alone mode of the Windsensor is wanted
  // Device is then reachable at 192.168.1.4
  //--------------------------------------------------------

  if(WiFi.localIP().toString().equals("(IP unset)")) { //check if the WiFiManager managed to connect to a network, if not, create an AP
    WiFi.softAP("Windsensor_AP", "123456789");  //create an AP with the SSID "Windsensor_AP" and the password "123456789"
  }


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

  //--------------------------------------------------------
  // make pin 2 an interrupt-pin, so whenever this pin is high,
  // an interrupt-function (here: WinSensInterrupt()) will be 
  // called. Crutial for the calculation of the windspeed.
  //--------------------------------------------------------

  attachInterrupt(digitalPinToInterrupt(2), WinSensInterupt, FALLING);  //define interrupt


  //--------------------------------------------------------
  // configuration of the HTTP-webserver
  // define how to deal with different URLs send or requested
  // by the clients
  //--------------------------------------------------------

  webserver.on("/config", handleCorrection);  //calls the function that sends the HTML code of the correction page, when the client goes to /config
  webserver.on("/action_page", handleForm);  //form action is handled here
  webserver.on("/ipblink", ipBlinkToggle);  //if the button to toggle the IPblinking is pressed, this calls the function to change it
  webserver.on("/", gui);  //calls the function that sends the HTML code to graphically display the wind data, when the client goes to the root of the webserver
  webserver.on("/data", data);  //when called, call the function that displays the current NMEA0813-sentence to be fetched by the API as plain text

  webserver.on("/meters", ws_unitChange_meters);  //when called, call the function that displays the current NMEA0813-sentence to be fetched by the API as plain text
  webserver.on("/kilometers", ws_unitChange_kilometers);  //when called, call the function that displays the current NMEA0813-sentence to be fetched by the API as plain text
  webserver.on("/knots", ws_unitChange_knots);  //when called, call the function that displays the current NMEA0813-sentence to be fetched by the API as plain text

  webserver.begin();  //now that all is set up, start the HTTP-webserver
  server.begin();  //start the TCP-server that sends the NMEA0813 on port 8080
  server.setNoDelay(true); // disable sending small packets


  //--------------------------------------------------------
  // configuration of the mDNS service
  // makes the ESP accessible at the string passed over
  // in the constructor, here: "windsensor", ".local"
  //--------------------------------------------------------
  
 if (!MDNS.begin("windsensor")) {  // Start the mDNS responder for windsensor.local
    Serial.println("Error setting up MDNS responder!");
  }
  MDNS.addService("http", "tcp", 80);   //tell the mDNS service, what to manage, here: the TCP-webserver on Port 80
  MDNS.addService("nmea-0183", "tcp", 8080);  // NMEA0183 data service for AVnav
  //An additional service for port 8080 is, as far as I can tell, not needed. At least Signal K could resolve with only this line

  
  //--------------------------------------------------------
  // processing of the stored correction-values
  //--------------------------------------------------------

  offset = readFile(SPIFFS, "/offset.txt");  //load the persistent data as variable
  factor = readFile(SPIFFS, "/factor.txt");  //load the persistent data as variable
  blIPblink = readFile(SPIFFS, "/blipblink.txt");  //update the blIPblink state
  ws_unit = readFile(SPIFFS, "/ws_unit.txt");  //update the ws_unit state


  //--------------------------------------------------------
  // initializing the stored correction-values
  // this only takes effect at the very first start
  //--------------------------------------------------------

  if (!(blIPblink.equals("geblinkt") or blIPblink.equals("nicht geblinkt"))) {  //initially set blIPblink to "geblinkt" ("true"), if it is neither "geblinkt" nor "nicht geblinkt"
    blIPblink = "geblinkt"; }

  if (!(ws_unit.equals("meters") or ws_unit.equals("kilometers") or ws_unit.equals("knots") or ws_unit.equals("bft"))) {  //initially set blIPblink to "geblinkt" ("true"), if it is neither "geblinkt" nor "nicht geblinkt"
    ws_unit = "meters"; }

  if (offset.toInt() == 0) {  //initially set the offset to 0, if it is null or 0, as both get converted to 0 by toInt()
    offset = "0"; }
  
  if (factor.toFloat() == 0) {  //initially set the linear windspeed factor to 1 if it is null or 0, as both get converted to 0 by toInt(), but a factor of 0 would result in no windspeed at all
    factor = "1"; }

  //--------------------------------------------------------
  // these lines can trigger the bliniking of the IP address
  //--------------------------------------------------------

  if (blIPblink.equals("geblinkt") and !(WiFi.localIP().toString().equals("(IP unset)"))) {  //if wanted and there is an IP to be blinked, call the function to blink it
    ip_blink();  //call the function to blink out the IP
  };

  Serial.print("leave setup");
}


//********************************************************
// Loop
//********************************************************

void loop() {

  ArduinoOTA.handle(); //deals with the ArduinoOTA-stuff, must be kept in to preserve posibility of OTA-FW-updates

  webserver.handleClient();  //Handle client requests of the HTTP-webserver

  MDNS.update();  //Allow mDNS processing

  //int Z = convertRawAngleToDegrees(ams5600.getRawAngle()) + offset.toInt(); //stores the value of the angle
  int Z = fmod((random(360) + offset.toInt()), 360);  //generate a random number between 0 and 359 to test the code independently of the sensor

  long WS = 0;  //stores the value of the wind speed
  byte bySpeedCorrection = 100;  //value for finetuning the speed calculation

  //WS = (((33 * bySpeedCorrection) / (times))*factor.toFloat());  //calculating the wind speed
  WS = random(30)*factor.toFloat();

  //--------------------------------------------------------
  // Assembly of the NMEA0183-sentence to calculate the checksum
  // Currently,the programming is still somewhat inefficient,
  // as the NMEA-sentence is created two times: once for the
  // checksum and TCP and once again fornthe SERIAL.
  //  This will be fixed in further releases.
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
  // Assembly and sending of the NMEA0183-sentence via TCP
  //--------------------------------------------------------
  if (server.hasClient()) {  //only send if there are clients to receive
    for (int i = 0; i < MAX_CLIENTS; i++) {

      //added check for clients[i].status==0 to reuse connections
      if ( !(clients[i] && clients[i].connected() ) || clients[i].status() == 0 ) {
        if (clients[i]) {
          clients[i].stop(); // make room for new connection
        }
        clients[i] = server.available();
        continue;
      }
    }

    // No free spot or exceeded MAX_CLENTS so reject incoming connection
    server.available().stop();
  }

  checksum(MWVSentence).toCharArray(g, 85); //calculate checksum and store it

  //final assembly of the TCP-message to be send
  strcpy(result, "$");  //start with the dollar symbol
  strcat(result, MWVSentence); //append the MWVSentence
  strcat(result, "*"); //star-seperator for the CS
  strcat(result, g);  //append the CS

  // Broadcast NMEA sentence to all clients
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i] && clients[i].connected()) {
      clients[i].println(result);  //make sure to use println and not write, at least it did not work for me
    }
  }

  //--------------------------------------------------------
  // Sending of the NMEA0183-sentence via SERIAL
  //--------------------------------------------------------
  Serial.print(result);
  Serial.print("\r");
  Serial.print("\n");
  Serial.println();

  // Wait for next reading
  for (int c = 0; c < DELAY_COUNT; c++) {
    delay(DELAY_MS);
  }

}
