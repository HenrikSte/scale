#include <fs.h>
#include <ArduinoJson.h>
//#include <DNSServer.h>
//#include <ESP8266WebServer.h>
#include <WiFiManager.h>         
#include <ESP8266WiFi.h>      
#include "SSD1306.h"

#include <HX711_ADC.h>



static const char cStaticIp[16] = "192.168.178.254";
static const char cStaticGw[16] = "10.0.1.1";
static const char cStaticSn[16] = "255.255.255.0";
String staticIp(cStaticIp);
String staticGateway(cStaticGw);
String staticSubnetMask(cStaticSn);

float scaleCalibrationFactor  = 758.0;
float scaleFactor = 1.0;
int   scaleDecimals = 1;    // number of decimal digits the weight is rounded to
float scaleIncrement     = 0.2;   // increment
float scaleHystheresis   = 0.05;
int   scalePort          = 21;
String UOM("g");

#define SCALE_SDA D1
#define SCALE_SCL D2

#define OLED_SDA D6
#define OLED_SCL D5				   

#define INFO_PIN D4	
#define TARE_PIN      D3
#define SETZERO_PIN   D7	

#define MAX_SRV_CLIENTS 3


WiFiServer server(scalePort); 
int todo; // scale port config
WiFiClient serverClients[MAX_SRV_CLIENTS];

SSD1306  DisplayOled(0x3c, OLED_SDA, OLED_SCL);       // set the DisplayOled address to 0x3c

//HX711 constructor (dout pin, sck pin)
HX711_ADC loadCell(SCALE_SDA, SCALE_SCL);
float zeroWeight  = 0;
float currentTare = 0;
bool shouldSaveConfigFile = false;


const char* configFileName = "/config.json";
const char* AP_SSID = "MSI";

boolean ledStatus = false;

void toggleLED()
{
  ledStatus = ! ledStatus;
  digitalWrite(LED_BUILTIN, ledStatus);
}

void resetLog()
{
  DisplayOled.setLogBuffer(5, 30);
}

void logText(const String & text, bool lineBreak=true)
{
  DisplayOled.clear();
  if (lineBreak)
  {
    DisplayOled.println(text);
    Serial.println(text);
  }
  else
  {
    DisplayOled.print(text);
    Serial.print(text);
  }

  DisplayOled.setFont(ArialMT_Plain_10);
  DisplayOled.drawLogBuffer(0, 0);
  DisplayOled.display();
}

void displayWeight(float weight)
{
  DisplayOled.clear();
  DisplayOled.setFont(ArialMT_Plain_24);
  String s(weight,scaleDecimals);
  s = "N:" + s + UOM;
  DisplayOled.drawString(0, 0, s.c_str());

  s = String(currentTare, scaleDecimals);
  s = "T:" + s + UOM;
  DisplayOled.drawString(0, 32, s.c_str());
  DisplayOled.display();
}

bool readConfigFile() 
{
  // this opens the config file in read-mode
  File f = SPIFFS.open(configFileName, "r");
  
  if (!f) 
  {
    Serial.println("Configuration file not found");
    return false;
  } 
  else 
  {
    // we could open the file
    size_t size = f.size();
    // Allocate a buffer to store contents of the file.
    std::unique_ptr<char[]> buf(new char[size]);

    // Read and store file contents in buf
    f.readBytes(buf.get(), size);
    // Closing file
    f.close();
    // Using dynamic JSON buffer which is not the recommended memory model, but anyway
    // See https://github.com/bblanchon/ArduinoJson/wiki/Memory%20model
    DynamicJsonBuffer jsonBuffer;
    // Parse JSON string
    JsonObject& json = jsonBuffer.parseObject(buf.get());
    // Test if parsing succeeds.
    if (!json.success()) 
    {
      Serial.println("JSON parseObject() failed");
      return false;
    }
    json.printTo(Serial);

    // Parse all config file parameters, override 
    // local config variables with parsed values
    if (json.containsKey("ip")) 
    {
      staticIp = (const char*) json["ip"]; 
    }
    if (json.containsKey("gw")) 
    {
      staticGateway = (const char*) json["gw"];      
    }
    if (json.containsKey("sn")) 
    {
      staticSubnetMask = (const char*) json["sn"];      
    }

    if (json.containsKey("UOM")) 
    {
      UOM = String((const char*)json["UOM"]);      
    }

    if (json.containsKey("scaleFactor")) 
    {
      float f= json["scaleFactor"];      
      if (f!= NAN)
      {
        scaleFactor = f;      
      }
    }


    if (json.containsKey("scaleDecimals")) 
    {
      scaleDecimals =  json["scaleDecimals"];      
    }

    if (json.containsKey("scaleIncrement")) 
    {
      float f= json["scaleIncrement"];
      if (f!= NAN)
      {
        scaleIncrement = f;  
      }
    }

    if (json.containsKey("scaleHystheresis")) 
    {
      float f = json["scaleHystheresis"];
      if (f!= NAN)
      {
        scaleHystheresis = f;   
      }
    }

    if (json.containsKey("scalePort")) 
    {
      scalePort =  json["scalePort"];      
    }

  }
  Serial.println("\nConfig file was successfully parsed");
  Serial.print("IP:");
  Serial.println(staticIp);
  Serial.print("GW:");
  Serial.println(staticGateway);
  Serial.print("SN:");
  Serial.println(staticSubnetMask);

  Serial.print("scaleCalibrationFactor:");
  Serial.println(String(scaleCalibrationFactor,6));
  Serial.print("scaleFactor:");
  Serial.println(String(scaleFactor,6));
  Serial.print("scaleDecimals:");
  Serial.println(scaleDecimals);
  Serial.print("scaleIncrement:");
  Serial.println(String(scaleIncrement,6));
  Serial.print("scaleHystheresis:");
  Serial.println(String(scaleHystheresis,6));
  Serial.print("UOM:");
  Serial.println(UOM.c_str());
  Serial.print("scalePort:");
  Serial.println(scalePort);
  
  return true;
}

bool writeConfigFile() 
{
  Serial.println("Saving config file");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();

  // JSONify local configuration parameters
  json["ip"] = staticIp.c_str();
  json["gw"] = staticGateway.c_str();
  json["sn"] = staticSubnetMask.c_str();
  json["scaleCalibrationFactor"]= scaleCalibrationFactor;
  json["scaleFactor"]= scaleFactor;
  json["scaleDecimals"] = scaleDecimals;
  json["scaleIncrement"] = scaleIncrement;
  json["scaleHystheresis"] = scaleHystheresis;
  json["UOM"] = UOM.c_str();
  json["scalePort"] = scalePort;
  
  
  // Open file for writing
  File f = SPIFFS.open(configFileName, "w");
  if (!f) 
  {
    Serial.println("Failed to open config file for writing");
    return false;
  }

  json.prettyPrintTo(Serial);
  // Write data to file and close it
  json.printTo(f);
  f.close();

  Serial.println("\nConfig file was successfully saved");
  return true;
}

void wifiManagerConfigModeCallback (WiFiManager *myWiFiManager) {
  resetLog();
  logText(F("ENTERED CONFIG MODE"));
  logText(F("connect to "));
  logText(F("SSID: "), false);
  logText(myWiFiManager->getConfigPortalSSID());
  logText(F("IP: "), false);
  logText(WiFi.softAPIP().toString());
}

void wifiManagerSaveConfigCallback () 
{
  shouldSaveConfigFile = true;
}

void showDeviceInfo()
{
  resetLog();
  
  logText(F("Chip ID: "), false);
  logText(String(ESP.getChipId()));
  
  logText(F("MAC:"), false);
  logText(WiFi.macAddress());
  
  logText(F("Running at "), false);
  logText(String(ESP.getCpuFreqMHz()), false);
  logText(F("MHz"));
  
  logText(F("Code size: "), false);
  logText(String(ESP.getSketchSize()));
  
  logText(F("Free heap: "), false);
  logText(String(ESP.getFreeHeap()));
}

void showWifiInfo()
{
  resetLog();
  logText(WiFi.isConnected() ? F("WiFi connected") : F("WiFi not connected"));

  logText(F("ssid: "), false);
  logText(WiFi.SSID());

  logText(F("IP: "), false);
  logText(WiFi.localIP().toString(),false);
  logText(F(":"),false);
  logText(String(scalePort));

  logText(F("Signal: "), false);
  logText(String(WiFi.RSSI()),false);
  logText(F("db"));

  logText(F("Hostname: "), false);
  logText(String(WiFi.hostname()));
}

void showConfig()
{
  resetLog();

  logText(F("cf:"), false);
  logText(String(scaleCalibrationFactor,6));
  logText(F("sf:"), false);
  logText(String(scaleFactor,6));
  logText(F("inc:"), false);
  logText(String(scaleIncrement,6));
  logText(F("hys:"), false);
  logText(String(scaleHystheresis,6));
  logText(F("dec:"), false);
  logText(String(scaleDecimals), false);
  logText(F(" uom:"), false);
  logText(UOM.c_str());

}

void initScale()
{
  loadCell.begin();
  long stabilisingtime = 2000; // tare preciscion can be improved by adding a few seconds of stabilising time
								 
  loadCell.start(stabilisingtime);
							
//  loadCell.setCalFactor(440.0); // user set calibration factor (float)
//  loadCell.setCalFactor(440.0); // user set calibration factor (float)
  loadCell.setCalFactor(scaleCalibrationFactor / scaleFactor); // user set calibration factor (float) * scale factor
  Serial.println("Startup + tare is complete");
}

void wifiStart(bool forceConfigPortal)
{
  WiFiManager wifiManager;

  String sScaleCalibrationFactor(scaleCalibrationFactor,6);
  String sScaleFactor(scaleFactor,6);
  String sScaleDecimals(scaleDecimals);
  String sScaleIncrement(scaleIncrement,6);
  String sScaleHystheresis(scaleHystheresis,6);
  String sScalePort(scalePort);

  WiFiManagerParameter custom_scaleCalibrationFactor("scaleCalibrationFactor", "Calibraton factor", sScaleCalibrationFactor.c_str(), 15);
  WiFiManagerParameter custom_scaleFactor("scaleFactor", "Scaling factor", sScaleFactor.c_str(), 15);
  WiFiManagerParameter custom_scaleDecimals("scaleDecimals", "Number of decimals", sScaleDecimals.c_str(), 2);
  WiFiManagerParameter custom_scaleIncrement("scaleIncrement", "Scale increment", sScaleIncrement.c_str(), 10);
  WiFiManagerParameter custom_scaleHystheresis("scaleHystheresis", "Scale hystheresis", sScaleHystheresis.c_str(), 15);
  WiFiManagerParameter custom_UOM("UOM", "Unit of measure", UOM.c_str(), 15);
  WiFiManagerParameter custom_scalePort("scalePort", "IP Port", sScalePort.c_str(), 4);

  wifiManager.setAPCallback(wifiManagerConfigModeCallback);
  wifiManager.setSaveConfigCallback(wifiManagerSaveConfigCallback);

   //add all your parameters here
  wifiManager.addParameter(&custom_scaleCalibrationFactor);
  wifiManager.addParameter(&custom_scaleFactor);
  wifiManager.addParameter(&custom_scaleDecimals);
  wifiManager.addParameter(&custom_scaleIncrement);
  wifiManager.addParameter(&custom_scaleHystheresis);
  wifiManager.addParameter(&custom_UOM);
  wifiManager.addParameter(&custom_scalePort);


  if (!forceConfigPortal && digitalRead(SETZERO_PIN) == LOW) // pin nicht abfragen wenn über den Pin das portal gestartet wurde
  {
      wifiManager.resetSettings();
      Serial.println("Manual wifi data reset");
  }  

  //   readConfigFile();
  //set static ip

  IPAddress _ip,_gw,_sn;
  _ip.fromString(staticIp);
  _gw.fromString(staticGateway);
  _sn.fromString(staticSubnetMask); 
  wifiManager.setSTAStaticIPConfig(_ip, _gw, _sn);

  Serial.println("\nConfig file was successfully parsed");
  Serial.print("IP:");
  Serial.println(_ip);
  Serial.print("GW:");
  Serial.println(_gw);
  Serial.print("SN:");
  Serial.println(_sn);

  String hostName = String(F("MSI")) + ESP.getChipId();
  WiFi.hostname(hostName);


  shouldSaveConfigFile = false;

  if (forceConfigPortal)
  {
    wifiManager.startConfigPortal(hostName.c_str());
  }
  else
  {
    wifiManager.autoConnect(hostName.c_str());
  }
    

  if (shouldSaveConfigFile)
  {
    staticIp          = WiFi.localIP().toString();
    staticGateway     = WiFi.gatewayIP().toString();
    staticSubnetMask  = WiFi.subnetMask().toString();
    scaleCalibrationFactor = atof(custom_scaleCalibrationFactor.getValue());
    scaleFactor       = atof(custom_scaleFactor.getValue());
    scaleDecimals     = atoi(custom_scaleDecimals.getValue());
    scaleIncrement    = atof(custom_scaleIncrement.getValue());
    scaleHystheresis  = atof(custom_scaleHystheresis.getValue());
    UOM               = custom_UOM.getValue();
    scalePort         = atoi(custom_scalePort.getValue());
    
    writeConfigFile();

    initScale();
  }
  
  server.begin();
  server.setNoDelay(true);
  Serial.println("Server started");
  showWifiInfo();       

}


void tareScale(float weight)
{
  Serial.print("old Tare:");
  Serial.print(currentTare);
  Serial.print(" - new Tare");
  currentTare = weight + currentTare; // getweight returns net weight!
  Serial.println(currentTare);
}

float getWeight()
{
  return loadCell.getData() - currentTare;
}

void zeroScale()
{
  loadCell.tare();
  Serial.print("ZeroScale");
  currentTare=0.0;
}

float lastRoundedWeight=0;
float roundWeight(float weight, float increment)
{
  
  float returnWeight  = 0;
  float diff = weight - lastRoundedWeight; // positive diff means new Weight is bigger than displayed Weight
  float roundedWeight = round(weight/increment)*increment;
/*
*/  
  if (diff > scaleHystheresis + increment/2) // rising weight
  {
//    Serial.print("  case 1 ");
    returnWeight = roundedWeight;
  }
  else if (diff < -scaleHystheresis - increment/2) // falling weight
  {
//    Serial.print("  case 2 ");
    returnWeight = roundedWeight;
  }
  else // no significant weight change:
  {
    returnWeight = lastRoundedWeight;
  }
  /*
  Serial.print("roundWeight: weight=");
  Serial.print(weight,5);
  Serial.print("  roundedWeight= ");
  Serial.print(roundedWeight,5);
  Serial.print("  lastRoundedWeight= ");
  Serial.print(lastRoundedWeight,5);
  Serial.print("  diff= ");
  Serial.print(diff,5);
  Serial.print("  returnWeight= ");
  Serial.print(returnWeight,5);
  Serial.print("  scaleHystheresis= ");
  Serial.print(scaleHystheresis,5);

  if (returnWeight!=roundedWeight)
  {
    Serial.print("  !!!!!!!!!!!!!!!   ");
  }
  Serial.println();
*/
  lastRoundedWeight = returnWeight;
  return returnWeight;
}


String formatWeight(float weight, int digits, int decimalDigits, float increment)
{
  String weightString(weight, (unsigned char)decimalDigits); 
  
  if (digits>0)
  {
    for (unsigned int i=0 ;i< (digits-1)-weightString.length(); i++)
    {
      weightString = String(" ") + weightString;
    }
  }
  return weightString;
}

String formatWeight(float weight)
{
  return formatWeight(weight, 10, scaleDecimals, scaleIncrement);
}

void initDisplay()
{
  DisplayOled.init();
  DisplayOled.setContrast(255);
  DisplayOled.flipScreenVertically();
  resetLog();
}

void setup() 
{
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  pinMode(INFO_PIN, INPUT_PULLUP);
  pinMode(TARE_PIN, INPUT_PULLUP);
  pinMode(SETZERO_PIN, INPUT_PULLUP);

  initDisplay();

  showDeviceInfo();
  delay(2000);

  if (digitalRead(TARE_PIN)==HIGH)
  {
    // Mount the filesystem
    bool result = SPIFFS.begin();
    Serial.println("SPIFFS opened: " + result);

    if (!readConfigFile()) 
    {
      Serial.println("Failed to read configuration file, using default values");
    }

    showWifiInfo();       
    wifiStart(false);
    // Start the Server

    Serial.println(F("Connected!"));
    digitalWrite(LED_BUILTIN, LOW);
  }
  logText(F("Init scale..."));
  initScale();
  logText(F("Zero scale..."));
  zeroScale();
  logText(F("Running..."));
}


unsigned long t;


void loop() 
{

  //update() should be called at least as often as HX711 sample rate; >10Hz@10SPS, >80Hz@80SPS
  //longer delay in scetch will reduce effective sample rate (be carefull with delay() in loop)
  loadCell.update();

  float rawWeight = getWeight();
  float roundedWeight = roundWeight(rawWeight, scaleIncrement );
									
  //get smoothed value from data set + current calibration factor
  if (millis() > t + 250) 
  {
      /*
      String text("Weight: ");
      text += String(weight);
      Serial.println(text);
			*/								   
      displayWeight(roundedWeight);
      t = millis();
  } 
 				 

  uint8_t i;
  if (server.hasClient())
  {
    for (i = 0; i < MAX_SRV_CLIENTS; i++)
    {
      if (!serverClients[i] || !serverClients[i].connected())
      {
        if (serverClients[i]) 
        {
          serverClients[i].stop();
        }
        serverClients[i] = server.available();
        continue;
      }
    }
    //no free spot
    WiFiClient serverClient = server.available();
    serverClient.stop();
  }

  for (i = 0; i < MAX_SRV_CLIENTS; i++)
  {
    if (serverClients[i] && serverClients[i].connected())
    {
      if (serverClients[i].available())
      {
        String recievedCommand;
        while (serverClients[i].available()) 
        {
          char c = serverClients[i].read();
          Serial.write(c);
          if (c!='\r' && c!='\n')
          {
            recievedCommand += String(c);
          }  
        }
        String response;
        if (recievedCommand.length())
        {
          if ((recievedCommand == "S") || (recievedCommand == "SI"))
          {
            //stable weight
            // weight immediately
            //S S      0.23 g
            response = "S S ";
            response += formatWeight(roundedWeight);
            response += " " + UOM;

          }
		   
          else if (recievedCommand == "T")
          {
            response = "T S ";
            //doTare= true;
            tareScale(roundedWeight);

            response += formatWeight(currentTare);
            response += " " + UOM;
            // Tare stable
          }
          else if (recievedCommand == "TA")
          {
            // return Tare 
            response = "TA A ";
            response += formatWeight(currentTare);
            response += " " + UOM;
          }
          else if (recievedCommand == "Z")
          {
            // Zero
            response = "Z A";
            zeroScale();
          }
          else if (recievedCommand.startsWith("TA"))
          {
            // SET tara
            float newTare = atof(recievedCommand.c_str() +2);
            // #todo: error cheicking. atof returns 0 if nothing valid is found.

            if (newTare != NAN)
            {
              // #todo check UOM
              currentTare = newTare;
              response = "TA A ";
              response += formatWeight(roundedWeight); // according to spec it returns the current !weight!
              response += " " + UOM;
            }
            else
            {
              response = "TA L ";
            }
          }
          else
          {
            response = "Unknown command: <" + recievedCommand + ">";
          }

          response += "\r\n";

          Serial.print(response);
          serverClients[i].write(response.c_str());
        }
      }
    }
  }

/*
  if (Serial.available())
  {
    size_t len = Serial.available();
    uint8_t sbuf[len];
    Serial.readBytes(sbuf, len);
    //bello is a broadcast to all clients
    for(i = 0; i < MAX_SRV_CLIENTS; i++)
    {
      if (serverClients[i] && serverClients[i].connected())
      {
        serverClients[i].write(sbuf, len);
        delay(1);
      }
    }
  }
*/

/*
  //receive from serial terminal
  if (Serial.available() > 0) 
  {
    float i;
    char inByte = Serial.read();
    if (inByte == 't') 
    {
      doTare=true;  
    }
  }
  */
  if (digitalRead(INFO_PIN)==LOW)                                       // INFO = WIFI INFO
  {
    showWifiInfo();
    while (digitalRead(INFO_PIN)==LOW)
    {
      yield();
      if (digitalRead(INFO_PIN)==LOW && digitalRead(SETZERO_PIN)==LOW)  // INFO + ZERO = Portal
      {
        wifiStart(true);
      }
      else if (digitalRead(INFO_PIN)==LOW && digitalRead(TARE_PIN)==LOW)  // INFO + TARE = Scale CONFIG
      {
        showConfig();
        while (digitalRead(TARE_PIN)==LOW)
        {
          yield();
        }
      }
    }
  }
  else if (digitalRead(TARE_PIN)==LOW)
  {
    tareScale(roundedWeight);
  }
  else if (digitalRead(SETZERO_PIN)==LOW)
  {
    zeroScale();
  }


  //check if last tare operation is complete
  if (loadCell.getTareStatus() == true) 
  {
    logText("Tare complete");
  }
 
}