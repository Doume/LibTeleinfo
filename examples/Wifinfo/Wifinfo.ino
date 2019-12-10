// **********************************************************************************
// ESP8266 Teleinfo WEB Server
// **********************************************************************************
// Creative Commons Attrib Share-Alike License
// You are free to use/extend this library but please abide with the CC-BY-SA license:
// Attribution-NonCommercial-ShareAlike 4.0 International License
// http://creativecommons.org/licenses/by-nc-sa/4.0/
//
// For any explanation about teleinfo ou use , see my blog
// http://hallard.me/category/tinfo
//
// This program works with the Wifinfo board
// see schematic here https://github.com/hallard/teleinfo/tree/master/Wifinfo
//
// Written by Charles-Henri Hallard (http://hallard.me)
//
// History : V1.00 2015-06-14 - First release
//
// All text above must be included in any redistribution.
//
// Modifié par Dominique DAMBRAIN 2017-07-10 (http://www.dambrain.fr)
//       Version 1.0.5
//       Librairie LibTeleInfo : Allocation statique d'un tableau de stockage 
//           des variables (50 entrées) afin de proscrire les malloc/free
//           pour éviter les altérations des noms & valeurs
//       Modification en conséquence des séquences de scanning du tableau
//       ATTENTION : Nécessite probablement un ESP-8266 type Wemos D1,
//        car les variables globales occupent 42.284 octets
//
//       Version 1.0.5a (11/01/2018)
//       Permettre la mise à jour OTA à partir de fichiers .ino.bin (Auduino IDE 1.8.3)
//       Ajout de la gestion d'un switch (Contact sec) relié à GND et D5 (GPIO-14)
//          Décommenter le #define SENSOR dans Wifinfo.h
//          Pour être utilisable avec Domoticz, au moins l'URL du serveur et le port
//          doivent être renseignés dans la configuration HTTP Request, ainsi que 
//          l'index du switch (déclaré dans Domoticz)
//          L'état du switch (On/Off) est envoyé à Domoticz au boot, et à chaque
//            changement d'état
//       Note : Nécessité de flasher le SPIFFS pour pouvoir configurer l'IDX du switch
//              et flasher le sketch winfinfo.ino.bin via interface Web
//       Rendre possible la compilation si define SENSOR en commentaire
//              et DEFINE_DEBUG en commentaire (aucun debug, version Production...)
//
//       Version 1.0.6 (04/02/2018) Branche 'syslog' du github
//		      Ajout de la fonctionnalité 'Remote Syslog'
//		        Pour utiliser un serveur du réseau comme collecteur des messages Debug
//            Note : Nécessité de flasher le SPIFFS pour pouvoir configurer le remote syslog
//          Affichage des options de compilation sélectionnées dans l'onglet 'Système'
//            et au début du Debug + syslog éventuels
//
//        Version 1.0.7 (10/12/2019) Use new WiFi library feature
//           Reconnect WiFi automatically after incident
// **********************************************************************************
// Include Arduino header
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <Syslog.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <Ticker.h>
//#include <WebSocketsServer.h>
//#include <Hash.h>
#include <NeoPixelBus.h>
#include <LibTeleinfo.h>
#include <FS.h>
#include <SPI.h>

// Global project file
#include "Wifinfo.h"

//WiFiManager wifi(0);
ESP8266WebServer server(80);
bool ota_blink;
char optval[48];

// Teleinfo
TInfo tinfo;

// RGB Led
#ifdef RGB_LED_PIN
//NeoPixelBus rgb_led = NeoPixelBus(1, RGB_LED_PIN, NEO_RGB | NEO_KHZ800);
//NeoPixelBus<NeoGrbFeature, NeoEsp8266BitBang800KbpsMethod> rgb_led(1, RGB_LED_PIN);
NeoPixelBus<NeoRgbFeature, NeoEsp8266BitBang800KbpsMethod> rgb_led(1, RGB_LED_PIN);
#endif


// define whole brigtness level for RGBLED (50%)
uint8_t rgb_brightness = 50;
// LED Blink timers
Ticker rgb_ticker;
Ticker blu_ticker;
Ticker red_ticker;
Ticker Every_1_Sec;
Ticker Tick_emoncms;
Ticker Tick_jeedom;
Ticker Tick_httpRequest;

volatile boolean task_1_sec = false;
volatile boolean task_emoncms = false;
volatile boolean task_jeedom = false;
volatile boolean task_httpRequest = false;
volatile boolean task_updsw = false;
unsigned long seconds = 0;
char buff[132];   //To format debug strings
// sysinfo data
_sysinfo sysinfo;

#ifdef SYSLOG
WiFiUDP udpClient;
Syslog syslog(udpClient, SYSLOG_PROTO_IETF);
#endif

// count Wifi connect attempts, to check stability
int          nb_reconnect = 0;
bool	       need_reinit = false;
unsigned int nb_reinit = 0;
bool         first_info_call=true;

#ifdef SIMU
//for tests
uint8_t flags = 8;
int loop_cpt = 60000;
String name2 = "HCHC";
char * s2 = (char *)name2.c_str();
String value2 = "000060000";
char * v2 = (char *) value2.c_str();
#endif

#ifdef SENSOR
// Le contact sec devra etre connecte entre GND et D5 (GPIO-14)
const int   SensorPin = 14; 
int         reading ;  
int         SwitchState = -1;       // the current reading from the input pin
int         lastSwitchState = -1;   // the previous reading from the input pin
unsigned long lastChangeTime = 0;  // the last time the input pin was toggled
unsigned long tempo = 200;    // temps necessaire a la stabilisation du switch (0,2 seconde)
#endif

/////////////////////////// uniquement si DEBUG ou SYSLOG //////////
#ifdef MACRO
// Versions polymorphes des appels au debugging
// non liées au port Serial ou Serial1
char logbuffer[255];

#ifdef SYSLOG
char waitbuffer[255];
char *lines[50];
int in=-1;
int out=-1;
#endif

unsigned int pending = 0 ;
volatile boolean SYSLOGusable=false;
volatile boolean SYSLOGselected=false;
int plog=0;
FS* _fs;    //Pointeur objet File System

void convert(const __FlashStringHelper *ifsh)
{
  PGM_P p = reinterpret_cast<PGM_P>(ifsh);
  plog=0;
  while (1) {
    unsigned char c = pgm_read_byte(p++);
    if (c == 0) {
      logbuffer[plog]=0;
      break;
    }
    logbuffer[plog]=c;
    plog++;
  }
}

#ifdef SYSLOG
void process_line(char *msg) {
    strcat(waitbuffer,msg);
    pending=strlen(waitbuffer);
    if( waitbuffer[pending-1] == 0x0D || waitbuffer[pending-1] == 0x0A) {
      //Cette ligne est complete : l'envoyer !
      for(int i=0; i < pending-1; i++) {
        if(waitbuffer[i] <= 0x20)
          waitbuffer[i] = 0x20;
      }
      syslog.log(LOG_INFO,waitbuffer);
      delay(2*pending);
      memset(waitbuffer,0,255);
      pending=0;
      
    }
}
#endif

// Toutes les fonctions aboutissent sur la suivante :
void Myprint(char *msg) {
  
#ifdef DEBUG
  DEBUG_SERIAL.print(msg);
#endif

#ifdef SYSLOG
  if( SYSLOGusable ) {
    process_line(msg);   
  } else if ( SYSLOGselected) {
    //syslog non encore disponible
    //stocker les messages à envoyer plus tard
    in++;
    if(in >= 50) {
      //table saturée !
      in=0;
    }
    if(lines[in]) {
      //entrée occupée : l'écraser, tant pis !
      free(lines[in]);
    }
    lines[in]=(char *)malloc(strlen(msg)+2);
    memset(lines[in],0,strlen(msg+1));
    strcpy(lines[in],msg);   
  }
#endif

}

void Myprint() {
  logbuffer[0] = 0;
  Myprint(logbuffer);
}
void Myprint(String msg) {
  sprintf(logbuffer,"%s",msg.c_str());
  Myprint(logbuffer);
}
void Myprint(const __FlashStringHelper *msg) {
  convert(msg);
  Myprint(logbuffer);
}
void Myprint(long i) {
  sprintf(logbuffer,"%ld", i);
  Myprint(logbuffer);
}
void Myprint(unsigned int i) {
  sprintf(logbuffer,"%d", i);
  Myprint(logbuffer);
}
void Myprint(int i) {
  sprintf(logbuffer,"%d", i);
  Myprint(logbuffer);
}
void Myprintln() {
  sprintf(logbuffer,"\n");
  Myprint(logbuffer);
}
void Myprintln(unsigned char *msg) {
  sprintf(logbuffer,"%s\n",msg);
  Myprint(logbuffer);
}
void Myprintln(String msg) {
  sprintf(logbuffer,"%s\n",msg.c_str());
  Myprint(logbuffer);
}
void Myprintln(const __FlashStringHelper *msg) {
  convert(msg);
  logbuffer[plog]=(char)'\n';
  logbuffer[plog+1]=0;
  Myprint(logbuffer);
}
void Myprintln(long i) {
  sprintf((char *)logbuffer,"%ld\n", i);
  Myprint(logbuffer);
}
void Myprintln(unsigned int i) {
  sprintf(logbuffer,"%d\n", i);
  Myprint(logbuffer);
}
void Myprintln(int i) {
  sprintf(logbuffer,"%d\n", i);
  Myprint(logbuffer);
}

void Myflush() {
  
}

#endif    //MACRO
/////////////////////////////////////////////////////////////////////////

/* ======================================================================
Function: UpdateSysinfo 
Purpose : update sysinfo variables
Input   : true if first call
          true if needed to print on serial debug
Output  : - 
Comments: -
====================================================================== */
void UpdateSysinfo(boolean first_call, boolean show_debug)
{
  char buff[64];
  int32_t adc;
  int sec = seconds;
  int min = sec / 60;
  int hr = min / 60;
  long day = hr / 24;

  sprintf_P( buff, PSTR("%ld days %02d h %02d m %02d sec"),day, hr % 24, min % 60, sec % 60);
  sysinfo.sys_uptime = buff;
}

/* ======================================================================
Function: Task_1_Sec 
Purpose : update our second ticker
Input   : -
Output  : - 
Comments: -
====================================================================== */
void Task_1_Sec()
{
  task_1_sec = true;
  seconds++;
}
/* ======================================================================
Function: Task_emoncms
Purpose : callback of emoncms ticker
Input   : 
Output  : -
Comments: Like an Interrupt, need to be short, we set flag for main loop
====================================================================== */
void Task_emoncms()
{
  task_emoncms = true;
}

/* ======================================================================
Function: Task_jeedom
Purpose : callback of jeedom ticker
Input   : 
Output  : -
Comments: Like an Interrupt, need to be short, we set flag for main loop
====================================================================== */
void Task_jeedom()
{
  task_jeedom = true;
}

/* ======================================================================
Function: Task_httpRequest
Purpose : callback of http request ticker
Input   : 
Output  : -
Comments: Like an Interrupt, need to be short, we set flag for main loop
====================================================================== */
void Task_httpRequest()
{
  task_httpRequest = true;
}

/* ======================================================================
Function: LedOff 
Purpose : callback called after led blink delay
Input   : led (defined in term of PIN)
Output  : - 
Comments: -
====================================================================== */
void LedOff(int led)
{
  #ifdef BLU_LED_PIN
  if (led==BLU_LED_PIN)
    LedBluOFF();
  #endif
  if (led==RED_LED_PIN)
    LedRedOFF();
  if (led==RGB_LED_PIN)
    LedRGBOFF();
}


// Light off the RGB LED
#ifdef RGB_LED_PIN
/* ======================================================================
Function: LedRGBON
Purpose : Light RGB Led with HSB value
Input   : Hue (0..255)
          Saturation (0..255)
          Brightness (0..255)
Output  : - 
Comments: 
====================================================================== */
void LedRGBON (uint16_t hue)
{
  if (config.config & CFG_RGB_LED) {
    // Convert to neoPixel API values
    // H (is color from 0..360) should be between 0.0 and 1.0
    // L (is brightness from 0..100) should be between 0.0 and 0.5
    RgbColor target = HslColor( hue / 360.0f, 1.0f, rgb_brightness * 0.005f );    

      // Set RGB Led
    rgb_led.SetPixelColor(0, target); 
    rgb_led.Show();
  }
}

/* ======================================================================
Function: LedRGBOFF 
Purpose : light off the RGN LED
Input   : -
Output  : - 
Comments: -
====================================================================== */
//void LedOff(int led)
void LedRGBOFF(void)
{
  if (config.config & CFG_RGB_LED) {
    rgb_led.SetPixelColor(0,RgbColor(0)); 
    rgb_led.Show();
  }
}

#endif


/* ======================================================================
Function: ADPSCallback 
Purpose : called by library when we detected a ADPS on any phased
Input   : phase number 
            0 for ADPS (monophase)
            1 for ADIR1 triphase
            2 for ADIR2 triphase
            3 for ADIR3 triphase
Output  : - 
Comments: should have been initialised in the main sketch with a
          tinfo.attachADPSCallback(ADPSCallback())
====================================================================== */
void ADPSCallback(uint8_t phase)
{
  // Monophasé
  if (phase == 0 ) {
    Debugln(F("ADPS"));
  } else {
    Debug(F("ADPS Phase "));
    Debugln('0' + phase);
  }
}

/* ======================================================================
Function: DataCallback 
Purpose : callback when we detected new or modified data received
Input   : linked list pointer on the concerned data
          value current state being TINFO_VALUE_ADDED/TINFO_VALUE_UPDATED
Output  : - 
Comments: -
====================================================================== */
void DataCallback(ValueList * me, uint8_t flags)
{

  // This is for simulating ADPS during my tests
  // ===========================================
  /*
  static uint8_t test = 0;
  // Each new/updated values
  if (++test >= 20) {
    test=0;
    uint8_t anotherflag = TINFO_FLAGS_NONE;
    ValueList * anotherme = tinfo.addCustomValue("ADPS", "46", &anotherflag);

    // Do our job (mainly debug)
    DataCallback(anotherme, anotherflag);
  }
  Debugf("%02d:",test);
  */
  // ===========================================
  
/*
  // Do whatever you want there
  Debug(me->name);
  Debug('=');
  Debug(me->value);
  
  if ( flags & TINFO_FLAGS_NOTHING ) Debug(F(" Nothing"));
  if ( flags & TINFO_FLAGS_ADDED )   Debug(F(" Added"));
  if ( flags & TINFO_FLAGS_UPDATED ) Debug(F(" Updated"));
  if ( flags & TINFO_FLAGS_EXIST )   Debug(F(" Exist"));
  if ( flags & TINFO_FLAGS_ALERT )   Debug(F(" Alert"));

  Debugln();
*/
}

/* ======================================================================
Function: NewFrame 
Purpose : callback when we received a complete teleinfo frame
Input   : linked list pointer on the concerned data
Output  : - 
Comments: -
====================================================================== */
void NewFrame(ValueList * me) 
{
  // Light the RGB LED 
  if ( config.config & CFG_RGB_LED) {
    LedRGBON(COLOR_GREEN);
    
    // led off after delay
    rgb_ticker.once_ms( (uint32_t) BLINK_LED_MS, LedOff, (int) RGB_LED_PIN);
  }
  //Debugln("NewFrame received");
}

/* ======================================================================
Function: NewFrame 
Purpose : callback when we received a complete teleinfo frame
Input   : linked list pointer on the concerned data
Output  : - 
Comments: it's called only if one data in the frame is different than
          the previous frame
====================================================================== */
void UpdatedFrame(ValueList * me)
{
  // Light the RGB LED (purple)
  if ( config.config & CFG_RGB_LED) {
    LedRGBON(COLOR_MAGENTA);

    // led off after delay
    rgb_ticker.once_ms(BLINK_LED_MS, LedOff, RGB_LED_PIN);
  }
  //Debugln("UpdatedFrame received");

/*
  // Got at least one ?
  if (me) {
    WiFiUDP myudp;
    IPAddress ip = WiFi.localIP();

    // start UDP server
    myudp.begin(1201);
    ip[3] = 255;

    // transmit broadcast package
    myudp.beginPacket(ip, 1201);

    // start of frame
    myudp.write(TINFO_STX);

    // Loop thru the node
    while (me->next) {
      me = me->next;
      // prepare line and write it
      sprintf_P( buff, PSTR("%s %s %c\n"),me->name, me->value, me->checksum );
      myudp.write( buff);
    }

    // End of frame
    myudp.write(TINFO_ETX);
    myudp.endPacket();
    myudp.flush();

  }
*/
}


/* ======================================================================
Function: ResetConfig
Purpose : Set configuration to default values
Input   : -
Output  : -
Comments: -
====================================================================== */
void ResetConfig(void) 
{
  // Start cleaning all that stuff
  memset(&config, 0, sizeof(_Config));

  // Set default Hostname
  sprintf_P(config.host, PSTR("WifInfo-%06X"), ESP.getChipId());
  strcpy_P(config.ota_auth, PSTR(DEFAULT_OTA_AUTH));
  config.ota_port = DEFAULT_OTA_PORT ;

  // Add other init default config here

  // Emoncms
  strcpy_P(config.emoncms.host, CFG_EMON_DEFAULT_HOST);
  config.emoncms.port = CFG_EMON_DEFAULT_PORT;
  strcpy_P(config.emoncms.url, CFG_EMON_DEFAULT_URL);

  // Jeedom
  strcpy_P(config.jeedom.host, CFG_JDOM_DEFAULT_HOST);
  config.jeedom.port = CFG_JDOM_DEFAULT_PORT;
  strcpy_P(config.jeedom.url, CFG_JDOM_DEFAULT_URL);
  //strcpy_P(config.jeedom.adco, CFG_JDOM_DEFAULT_ADCO);

  // HTTP Request
  strcpy_P(config.httpReq.host, CFG_HTTPREQ_DEFAULT_HOST);
  config.httpReq.port = CFG_HTTPREQ_DEFAULT_PORT;
  strcpy_P(config.httpReq.path, CFG_HTTPREQ_DEFAULT_PATH);
  
  config.config |= CFG_RGB_LED;

  // save back
  saveConfig();
}

/* ======================================================================
Function: WifiHandleConn
Purpose : Handle Wifi connection / reconnection and OTA updates
Input   : setup true if we're called 1st Time from setup
Output  : state of the wifi status
Comments: -
====================================================================== */
int WifiHandleConn(boolean setup = false) 
{
  int ret = WiFi.status();
  char  toprint[20];
  IPAddress ad;
  
  if (setup) {
#ifdef DEBUG
    DebuglnF("========== WiFi diags start"); 
    WiFi.printDiag(DEBUG_SERIAL);
    DebuglnF("========== WiFi diags end"); 
    Debugflush();
#endif

    // no correct SSID
    if (!*config.ssid) {
      DebugF("no Wifi SSID in config, trying to get SDK ones..."); 

      // Let's see of SDK one is okay
      if ( WiFi.SSID() == "" ) {
        DebuglnF("Not found may be blank chip!"); 
      } else {
        *config.psk = '\0';

        // Copy SDK SSID
        strcpy(config.ssid, WiFi.SSID().c_str());

        // Copy SDK password if any
        if (WiFi.psk() != "")
          strcpy(config.psk, WiFi.psk().c_str());

        DebuglnF("found one!"); 

        // save back new config
        saveConfig();
      }
    }

    // correct SSID
    if (*config.ssid) {
      uint8_t timeout ;

      DebugF("Connecting to: "); 
      Debug(config.ssid);
      Debugflush();

      // Do wa have a PSK ?
      if (*config.psk) {
        // protected network
        Debug(F(" with key '"));
        Debug(config.psk);
        Debug(F("'..."));
        Debugflush();
        WiFi.begin(config.ssid, config.psk);
      } else {
        // Open network
        Debug(F("unsecure AP"));
        Debugflush();
        WiFi.begin(config.ssid);
      }

      timeout = 50; // 50 * 200 ms = 5 sec time out
      // 200 ms loop
      while ( ((ret = WiFi.status()) != WL_CONNECTED) && timeout )
      {
        // Orange LED
        LedRGBON(COLOR_ORANGE);
        delay(50);
        LedRGBOFF();
        delay(150);
        --timeout;
      }
    }


    // connected ? disable AP, client mode only
    if (ret == WL_CONNECTED)
    {
      nb_reconnect++;         // increase reconnections count
      DebuglnF("connected!");
      WiFi.mode(WIFI_STA);
      ad = WiFi.localIP();
      sprintf(toprint,"%d.%d.%d.%d", ad[0],ad[1],ad[2],ad[3]);
      DebugF("IP address   : "); Debugln(toprint);
      DebugF("MAC address  : "); Debugln(WiFi.macAddress());
#ifdef SYSLOG
    if (*config.syslog_host) {
      SYSLOGselected=true;
      // Create a new syslog instance with LOG_KERN facility
      syslog.server(config.syslog_host, config.syslog_port);
      syslog.deviceHostname(config.host);
      syslog.appName(APP_NAME);
      syslog.defaultPriority(LOG_KERN);
      memset(waitbuffer,0,255);
      pending=0;
      SYSLOGusable=true;
    } else {
      SYSLOGusable=false;
      SYSLOGselected=false;
    }
#endif
    
    // not connected ? start AP
    } else {
      char ap_ssid[32];
      DebuglnF("Error!");
      Debugflush();

      // STA+AP Mode without connected to STA, autoconnect will search
      // other frequencies while trying to connect, this is causing issue
      // to AP mode, so disconnect will avoid this

      // Disable auto retry search channel
      WiFi.disconnect(); 

      // SSID = hostname
      strcpy(ap_ssid, config.host );
      DebugF("Switching to AP ");
      Debugln(ap_ssid);
      Debugflush();

      // protected network
      if (*config.ap_psk) {
        DebugF(" with key '");
        Debug(config.ap_psk);
        DebuglnF("'");
        WiFi.softAP(ap_ssid, config.ap_psk);
      // Open network
      } else {
        DebuglnF(" with no password");
        WiFi.softAP(ap_ssid);
      }
      WiFi.mode(WIFI_AP_STA);

      DebugF("IP address   : "); Debugln(WiFi.softAPIP());
      DebugF("MAC address  : "); Debugln(WiFi.softAPmacAddress());
    }
    // Version 1.0.7 : Use auto reconnect Wifi
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
    DebuglnF("auto-reconnect armed !");
      
	  
    // Set OTA parameters
    ArduinoOTA.setPort(config.ota_port);
    ArduinoOTA.setHostname(config.host);
    ArduinoOTA.setPassword(config.ota_auth);
    ArduinoOTA.begin();

    // just in case your sketch sucks, keep update OTA Available
    // Trust me, when coding and testing it happens, this could save
    // the need to connect FTDI to reflash
    // Usefull just after 1st connexion when called from setup() before
    // launching potentially buggy main()
    for (uint8_t i=0; i<= 10; i++) {
      LedRGBON(COLOR_MAGENTA);
      delay(100);
      LedRGBOFF();
      delay(200);
      ArduinoOTA.handle();
    }

  } // if setup

  return WiFi.status();
}

/* ======================================================================
Function: setup
Purpose : Setup I/O and other one time startup stuff
Input   : -
Output  : - 
Comments: -
====================================================================== */
void setup() {

  boolean reset_config = true;

  // Set CPU speed to 160MHz
  system_update_cpu_freq(160);

#ifdef SYSLOG
  SYSLOGselected=true;  //Par défaut, au moins stocker les premiers msg debug
  SYSLOGusable=false;   //Tant que non connecté, ne pas émettre sur réseau
#endif
  
  memset(optval,0,48);

#ifdef SIMU
  strcat(optval,"SIMU, ");
#else
  strcat(optval, "No SIMU, ");
#endif

#ifdef DEBUG
  strcat(optval,"DEBUG, ");
#else
  strcat(optval, "No DEBUG, ");
#endif

#ifdef SYSLOG
  strcat(optval,"SYSLOG, ");
#else
  strcat(optval, "No SYSLOG, ");
#endif

#ifdef SENSOR
  strcat(optval,"SENSOR");
#else
  strcat(optval, "No SENSOR");
#endif


#ifdef DEBUG
  DEBUG_SERIAL.begin(115200);
#endif

#ifdef SYSLOG
  for(int i=0; i<50; i++)
    lines[i]=0;
  in=-1;
  out=-1;
#endif

  // Teleinfo is connected to RXD2 (GPIO13 or D7) to 
  // avoid conflict when flashing, this is why
  // we swap RXD1/TXD1 to RXD2/TXD2 
  // Note that TXD2 is not used : teleinfo is "receive only"
#ifdef DEBUG_SERIAL1
  Serial.begin(1200, SERIAL_7E1);
  Serial.swap();
  Debugln("Sortie Debug sur D4 (TXD2), entrée Teleinfo sur D7 (RXD1)");
#else
  Debugln("Sortie Debug sur Serial (TXD1), pas de Teleinfo possible");
#endif
 

  // Init the RGB Led, and set it off
  rgb_led.Begin();
  LedRGBOFF();

  Debugln(F("=============="));
  Debug(F("WifInfo V"));
  Debugln(F(WIFINFO_VERSION));
  Debug(F("Options : "));
  Debugln(optval);
  Debugln();

  // Clear our global flags
  config.config = 0;

  // Our configuration is stored into EEPROM
  //EEPROM.begin(sizeof(_Config));
  EEPROM.begin(1024);


  DebugF("Config size="); Debug(sizeof(_Config));
  DebugF(" (emoncms=");   Debug(sizeof(_emoncms));
  DebugF("  jeedom=");   Debug(sizeof(_jeedom));
  DebugF("  http request=");   Debug(sizeof(_httpRequest));
  Debugln(" )");
//  Debugflush();

  
  
  // Read Configuration from EEP
  if (readConfig()) {
      DebuglnF("Good CRC, not set! From now, we can use EEPROM config !");
  } else {
    // Reset Configuration
    ResetConfig();

    // save back
    saveConfig();

    // Indicate the error in global flags
    config.config |= CFG_BAD_CRC;

    DebuglnF("Reset to default");
  }

  // We'll drive our onboard LED
  // old TXD1, not used anymore, has been swapped
  pinMode(RED_LED_PIN, OUTPUT); 
  LedRedOFF();

  // start Wifi connect or soft AP
  WifiHandleConn(true);

  //purge previous debug message,

#ifdef SYSLOG
  if(SYSLOGselected) {
    if(in != out && in != -1) {
        //Il y a des messages en attente d'envoi
        out++;
        while( out <= in ) {
          process_line(lines[out]);
          free(lines[out]);
          lines[out]=0;
          out++;
        }
        DebuglnF("syslog buffer empty");
    }
  } else {
    DebuglnF("syslog not activated !");
  }
#endif

  // Init SPIFFS filesystem, to use web server static files
  if (! SPIFFS.begin() )
  {
    // Serious problem
    DebuglnF("SPIFFS Mount failed !");
  } else {
    DebuglnF("");
    DebuglnF("SPIFFS Mount succesfull");

    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {    
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      sprintf(buff,"FS File: %s, size: %d\n", fileName.c_str(), fileSize);
      Debug(buff);
    }
    //DebuglnF("");
  }

  // OTA callbacks
  ArduinoOTA.onStart([]() { 
    LedRGBON(COLOR_MAGENTA);
    DebuglnF("Update Started");
    ota_blink = true;
  });

  ArduinoOTA.onEnd([]() { 
    LedRGBOFF();
    DebuglnF("Update finished : restarting");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (ota_blink) {
      LedRGBON(COLOR_MAGENTA);
    } else {
      LedRGBOFF();
    }
    ota_blink = !ota_blink;
    //Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    LedRGBON(COLOR_RED);
#ifdef DEBUG
    sprintf(buff,"Update Error[%u]: ", error);
    Debug(buff);
    if (error == OTA_AUTH_ERROR) DebuglnF("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) DebuglnF("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) DebuglnF("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) DebuglnF("Receive Failed");
    else if (error == OTA_END_ERROR) DebuglnF("End Failed");
#endif
    ESP.restart(); 
  });

  // Update sysinfo variable and print them
  UpdateSysinfo(true, true);

  server.on("/", handleRoot);
  server.on("/config_form.json", handleFormConfig);
  server.on("/json", sendJSON);
  server.on("/tinfo.json", tinfoJSONTable);
  server.on("/emoncms.json", emoncmsJSONTable);
  server.on("/system.json", sysJSONTable);
  server.on("/config.json", confJSONTable);
  server.on("/spiffs.json", spiffsJSONTable);
  server.on("/wifiscan.json", wifiScanJSON);
  server.on("/factory_reset", handleFactoryReset);
  server.on("/reset", handleReset);

  // handler for the hearbeat
  server.on("/hb.htm", HTTP_GET, [&](){
      server.sendHeader("Connection", "close");
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "text/html", R"(OK)");
  });

  // handler for the /update form POST (once file upload finishes)
  server.on("/update", HTTP_POST, 
    // handler once file upload finishes
    [&]() {
      server.sendHeader("Connection", "close");
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
      ESP.restart();
    },
    // handler for upload, get's the sketch bytes, 
    // and writes them through the Update object
    [&]() {
      HTTPUpload& upload = server.upload();

      if(upload.status == UPLOAD_FILE_START) {
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        WiFiUDP::stopAll();
        sprintf(buff,"Update: %s\n", upload.filename.c_str());
        Debug(buff);
        LedRGBON(COLOR_MAGENTA);
        ota_blink = true;

        //start with max available size
        if(!Update.begin(maxSketchSpace)) 
          Update.printError(Serial1);

      } else if(upload.status == UPLOAD_FILE_WRITE) {
        if (ota_blink) {
          LedRGBON(COLOR_MAGENTA);
        } else {
          LedRGBOFF();
        }
        ota_blink = !ota_blink;
        Debug(".");
        if(Update.write(upload.buf, upload.currentSize) != upload.currentSize) 
          Update.printError(Serial1);

      } else if(upload.status == UPLOAD_FILE_END) {
        //true to set the size to the current progress
        if(Update.end(true)) {
          sprintf(buff,"Update Success: %u\nRebooting...\n", upload.totalSize);
          Debug(buff);
        } else 
          Update.printError(Serial1);

        LedRGBOFF();

      } else if(upload.status == UPLOAD_FILE_ABORTED) {
        Update.end();
        LedRGBOFF();
        DebuglnF("Update was aborted");
      }
      delay(0);
    }
    );
  
    ///////////////////////////////////////////////////////
   // All other not known 
  server.onNotFound(handleNotFound);
  
  // serves all SPIFFS Web file with 24hr max-age control
  // to avoid multiple requests to ESP
  server.serveStatic("/font", SPIFFS, "/font","max-age=86400"); 
  server.serveStatic("/js",   SPIFFS, "/js"  ,"max-age=86400"); 
  server.serveStatic("/css",  SPIFFS, "/css" ,"max-age=86400"); 
  server.begin();

  // Display configuration
  showConfig();

  Debugln(F("HTTP server started"));

  // Init teleinfo
  need_reinit=false;
  tinfo.init();

  // Attach the callback we need
  // set all as an example
  tinfo.attachADPS(ADPSCallback);
  tinfo.attachData(DataCallback);
  tinfo.attachNewFrame(NewFrame);
  tinfo.attachUpdatedFrame(UpdatedFrame);

  //webSocket.begin();
  //webSocket.onEvent(webSocketEvent);

  // Light off the RGB LED
  LedRGBOFF();

  // Update sysinfo every second
  Every_1_Sec.attach(1, Task_1_Sec);
  
  // Emoncms Update if needed
  if (config.emoncms.freq) 
    Tick_emoncms.attach(config.emoncms.freq, Task_emoncms);

  // Jeedom Update if needed
  if (config.jeedom.freq) 
    Tick_jeedom.attach(config.jeedom.freq, Task_jeedom);

  // HTTP Request Update if needed
  if (config.httpReq.freq) 
    Tick_httpRequest.attach(config.httpReq.freq, Task_httpRequest);

//To simulate Teleinfo on not connected module
#ifdef SIMU
    String name1 = "ADCO";
    String value1 = "01234546789012";
    
    char * s1 = (char *)name1.c_str();
    char * v1 = (char *)value1.c_str();
    flags = TINFO_FLAGS_ADDED;
    tinfo.addCustomValue(s1, v1, &flags); //ADCO arbitrary value
    tinfo.addCustomValue(s2, v2, &flags); //counter value
    flags = TINFO_FLAGS_NONE;
    tinfo.valuesDump();
#endif

#ifdef SENSOR
  pinMode(SensorPin, INPUT_PULLUP);
  DebuglnF("Switch sensor initialized");
  reading = digitalRead(SensorPin);
  sprintf(buff,"Initial State: %d\n", reading);
  Debug(buff);
#endif

}

/* ======================================================================
Function: loop
Purpose : infinite loop main code
Input   : -
Output  : - 
Comments: -
====================================================================== */
void loop()
{
  char c;

  // Do all related network stuff
  server.handleClient();
  ArduinoOTA.handle();

  //webSocket.loop();

  // Only once task per loop, let system do its own task
  if (task_1_sec) { 
    UpdateSysinfo(false, false); 
    task_1_sec = false; 
    
//To simulate Teleinfo on not connected module
#ifdef SIMU
    loop_cpt++;
    if(loop_cpt % 10)
    {
      // each 10 second, try to change HCHC value
      //Increase v2 value
      sprintf(v2, "%09d", (loop_cpt) );
      // and update ListValues
      flags = TINFO_FLAGS_UPDATED;
      tinfo.addCustomValue(s2, v2, &flags);   
    }
#endif
  } else if (task_emoncms) { 
    emoncmsPost(); 
    task_emoncms=false; 
  } else if (task_jeedom) { 
    jeedomPost();  
    task_jeedom=false;
  } else if (task_httpRequest) { 
    httpRequest();  
    task_httpRequest=false;
  } 
#ifdef SENSOR
  else if (task_updsw) { 
    UPD_switch();  
    task_updsw=false;
  }

 // read the state of the switch into a local variable:
  reading = digitalRead(SensorPin);

  // check to see if you just pressed the button
  // (i.e. the input went from LOW to HIGH), and you've waited long enough
  // since the last press to ignore any noise:

  // If the switch changed, due to noise or pressing:
  if (reading != lastSwitchState) {
    // reset the debouncing timer
    lastChangeTime = millis();
    lastSwitchState = reading;
  }

  if ((millis() - lastChangeTime) > tempo) {
    // whatever the reading is at, it's been there for longer than the tempo
    // delay, so take it as the actual current state:

    // if the switch state has changed:
    if (reading != SwitchState) {
      sprintf(buff,"Switch changed from  %d to %d\n", SwitchState, reading);
      Debug(buff);
      SwitchState = reading;
      //Notify HTTP server that switch has changed, on next loop
      task_updsw=true;
    }
  }
#endif

  if (need_reinit) {
    //Some polluted entries have been detected in Teleinfo ListValues
		need_reinit=false;
    nb_reinit++;    //account of reinit operations, for system infos
		tinfo.init();		//Clear ListValues, buffer, and wait for next STX
  } else {
	  // Handle teleinfo serial
	  if ( Serial.available() ) {
	    // Read Serial and process to tinfo
	    c = Serial.read();
	    tinfo.process(c);
    }

    //delay(10);
  }

}
