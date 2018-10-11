/*
  The fancyLight Project
*/

#include <FS.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266mDNS.h>
#include <TimeLib.h>
#include <WiFiManager.h>

char ssid[40];
char password[40];

const char *ssidAP = "FancyLight";
const char *passwordAP = "password";


#define PIN 5
#define LEDS 3

int red = 0;
int grn = 0;
int blu = 0;
String fancyMode;
String sleepMode;
int starttime;
int endtime;
int rainbowSpeed = 500;


Adafruit_NeoPixel strip = Adafruit_NeoPixel(LEDS, PIN, NEO_GRB + NEO_KHZ800);

WiFiClient client;
WiFiManager wifiManager;
ESP8266WebServer server(80);

// NTP Server:
static const char ntpServerName[] = "ptbtime1.ptb.de";
const int timeZone = 2;     // Central European Time
time_t currentTime = 0;
time_t turnOnTime = 0;
time_t turnOffTime = 0;

WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets

time_t getNtpTime();
void digitalClockDisplay();
void printDigits(int digits);
void sendNTPpacket(IPAddress &address);

void saveColor() {
  File f = SPIFFS.open("/color.txt", "w");
  if (!f) {
    Serial.println("color.txt not found");
  } else {
    f.print(red);
    f.print(",");
    f.print(grn);
    f.print(",");
    f.print(blu);
    f.close();
  }
}

void changeLight() {
  if (fancyMode == "false"){
    for (int i = 0; i < LEDS; i++){
      strip.setPixelColor(i,red, grn, blu);
      strip.show();
    }
  }
}

void getColor() {
  String r = server.arg("r");
  String g = server.arg("g");
  String b = server.arg("b");
  fancyMode = server.arg("fancyMode");
  red = r.toInt();
  grn = g.toInt();
  blu = b.toInt();
  Serial.println("");
  Serial.println("New Color:");
  Serial.println("RGB: " + r + " " + g + " " + b + ", FancyMode: " + fancyMode);

  server.send(200, "text/plain", "OK");

  changeLight();
  saveColor();
}

void getColorFromFlash() {
  File f = SPIFFS.open("/color.txt" , "r");
  if (!f) {
    Serial.println("color.txt doesn´t exsist or failed to open.");
  } else {
    Serial.println("Get saved colors: (r,g,b)");
    while (f.available()) {
      red = f.parseInt();
      Serial.println(red);
      grn = f.parseInt();
      Serial.println(grn);
      blu = f.parseInt();
      Serial.println(blu);
    }
    f.close();
  }
}

void getTime() {
  String stime = server.arg("starttime");
  String etime = server.arg("endtime");
  starttime = stime.toInt();
  endtime = etime.toInt();

  File f = SPIFFS.open("/times.txt", "w");
  if (!f) {
    Serial.println("times.txt not found");
  } else {
    f.print(starttime);
    f.print(",");
    f.print(endtime);
    f.close();
  }
  
  Serial.println("");
  Serial.println("New start-(" + stime + ") and endtime(" + etime + ").");
  server.send(200, "text/plain", "OK");
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  if(WheelPos < 85) {
   return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } else if(WheelPos < 170) {
   WheelPos -= 85;
   return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else {
   WheelPos -= 170;
   return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
}

void rainbow(uint8_t wait) {
  uint16_t i, j;
 
  for(j=0; j<256; j++) {
    for(i=0; i<strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel((i+j) & 255));
    }
    strip.show();
    delay(wait);
    server.handleClient();
    if (fancyMode != "true"){
      break;
    }
  }
}

void rainbowCycle(uint8_t wait) {
  uint16_t i, j;
 
  for(j=0; j<256*5; j++) { // 5 cycles of all colors on wheel
    for(i=0; i< strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
    }
    strip.show();
    delay(wait);
  }
}

void digitalClockDisplay()
{
  // digital clock display of the time
  Serial.print(hour());
  printDigits(minute());
  printDigits(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(".");
  Serial.print(month());
  Serial.print(".");
  Serial.print(year());
  Serial.println();
}

void printDigits(int digits)
{
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void setup() {
  Serial.begin(9600);
  Serial.println("booting up...");

  
  strip.begin();
  strip.setBrightness(100);
  strip.show();

  rainbowCycle(4);
 
  WiFi.hostname("FancyLight");
  wifiManager.setMinimumSignalQuality(30);
  wifiManager.autoConnect(ssidAP, passwordAP);
  Serial.println("Mit Wlan verbunden");


  if (!MDNS.begin("fancy")) {
    Serial.println("Error setting up MDNS responder!");
    while(1) { 
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");

  Serial.println("Starting UDP");
  Udp.begin(localPort);
  
  setSyncProvider(getNtpTime);
  setSyncInterval(300);
  now();
  Serial.print("Time: ");
  digitalClockDisplay();

  SPIFFS.begin();
  server.serveStatic("/", SPIFFS, "/index.html");
  server.on("/sendColor", getColor);
  server.on("/sendTime", getTime);
  server.begin();
  for (int i = 0; i < LEDS; i++){
    strip.setPixelColor(i,0,0,0);
    strip.show();
  }

  File f = SPIFFS.open("/times.txt", "r");
  if (!f) {
    Serial.println("times.txt doesn´t exsist or failed to open.");
  } else {
    while (f.available()) {
      starttime = f.parseInt();
      Serial.println(starttime);
      endtime = f.parseInt();
      Serial.println(endtime);
    }
    f.close();
  }

  getColorFromFlash();
}

bool checkIfLightIsOn() {
  if (red != 0 || grn != 0  || blu != 0) {
    return true;
  } else {
    return false;
  }
}

void loop() {
  
  if (WiFi.status() != WL_CONNECTED) {
    wifiManager.autoConnect(ssidAP, passwordAP);
  }

  server.handleClient();
  if (fancyMode == "true"){
    rainbow(rainbowSpeed);
  }

  currentTime = now();

  if ((hour() == starttime) && !checkIfLightIsOn()) {
    // turn light on 
    Serial.println("____________________");
    digitalClockDisplay();
    Serial.println("Light turns on");
    getColorFromFlash();
    changeLight();
  } else if ((hour() == endtime) && checkIfLightIsOn()) {
    // turn light off
    Serial.println("____________________");
    digitalClockDisplay();
    Serial.println("Light turns off");
    red = 0;
    grn = 0;
    blu = 0;
    changeLight();
  }


}