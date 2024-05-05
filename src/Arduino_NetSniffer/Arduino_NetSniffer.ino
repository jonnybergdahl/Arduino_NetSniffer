#include <Adafruit_GFX.h>    // Core graphics library
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include "FreeSerif12pt7b.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <AsyncUDP.h>
#include "time.h"
#include <bb_spi_lcd.h>
#include <SPI.h>
#include "secrets.h"

AsyncUDP udp;
static uint8_t ucBuf[48 * 384];
#define WIDTH 320
#define HEIGHT 240
BB_SPI_LCD tft = BB_SPI_LCD(); 

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 0; //3600;
struct tm timeinfo;

void printHex(char *data, int length)
{
  int p = 0;
  while(p < length)
  {
    char ascii[17];
    int i = 0;
    for(; i < 16; i++)
    {
      Serial.printf("%02X ", data[p]);
      if(data[p] >= 32)// || data[p] < 128)
        ascii[i] = data[p];
      else
        ascii[i] = '.';
      p++;
      if(p == length)
      { i++; break;}
    }
    ascii[i] = 0;
    Serial.println(ascii);
  }
}

void printIP(char *data)
{
  for(int i = 0; i < 4; i++)
  {
    Serial.print((int)data[i]);
    if(i < 3)
      Serial.print('.');
  }
}

const int DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET = 2;
const int DHCP_PACKET_CLIENT_ADDR_OFFSET = 28;

enum State
{
  READY,
  RECEIVING,
  RECIEVED
};

volatile State state = READY;
String newMAC;
String newIP;
String newName;

const char *hexDigits = "0123456789ABCDEF";

void initTft()
{
  tft.begin(LCD_ILI9341, 0, F_CPU / 4, 15, 2, -1, 21, 12, 13, 14);

  tft.setRotation(3);
  
  tft.fillScreen(TFT_BLACK);
  
  // Set "cursor" at top left corner of display (0,0) and select font 2
  // (cursor will move to next line automatically during printing with 'tft.println'
  //  or stay on the line is there is room for the text with tft.print)
  tft.setCursor(0, 0);
  // Set the font colour to be white with a black background, set text size multiplier to 1
  tft.setFont(FONT_12x16);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);  
  //tft.setTextSize(4);
  // We can now plot text on screen using the "print" class
  tft.print("Waiting...");  
}

void printData()
{
  char buffer[40];
  if(getLocalTime(&timeinfo))
  {    
    sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", timeinfo.tm_year+1900, timeinfo.tm_mon, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  }

  Serial.println("---------------------------------------");
  Serial.print("Time stamp: ");
  Serial.println(buffer);
  Serial.print("Name        : ");
  Serial.println(newName);
  Serial.print("IP address  : ");
  Serial.println(newIP);
  Serial.print("MAC         : ");
  Serial.println(newMAC);
  Serial.println("---------------------------------------");

  tft.fillScreen(TFT_BLACK);
  
  // Set "cursor" at top left corner of display (0,0) and select font 2
  // (cursor will move to next line automatically during printing with 'tft.println'
  //  or stay on the line is there is room for the text with tft.print)
  tft.setCursor(0, 0);
  // Set the font colour to be white with a black background, set text size multiplier to 1
  tft.setTextColor(TFT_WHITE,TFT_BLACK);  
  tft.setFont(FONT_12x16);
  // We can now plot text on screen using the "print" class
  tft.println(buffer);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.println(newName);
  
  // Set the font colour to be red with black background, set to font 4
  tft.setTextColor(TFT_BLUE,TFT_BLACK);    
  tft.setFont(FONT_16x16);
  tft.println(newIP);

  // Set the font colour to be green with black background, set to font 2
  //tft.setFont(FONT_8x8);
  tft.setFreeFont(&FreeSerif12pt7b);
  tft.setTextColor(TFT_GREEN,TFT_BLACK);
  tft.println(newMAC);
}

void parsePacket(char* data, int length)
{
  if(state == RECIEVED) return;
  String tempName;
  String tempIP;
  String tempMAC;

  Serial.println("DHCP Packet");
  //printHex(data, length);
  Serial.print("MAC address: ");
  for(int i = 0; i < data[DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET]; i++)
    if(i < data[DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET] - 1)
      Serial.printf("%02X:", (int)data[DHCP_PACKET_CLIENT_ADDR_OFFSET + i]);
    else
      Serial.printf("%02X", (int)data[DHCP_PACKET_CLIENT_ADDR_OFFSET + i]);
  for(int i = 0; i < data[DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET]; i++)
  {
    tempMAC += hexDigits[(int)data[DHCP_PACKET_CLIENT_ADDR_OFFSET + i] >> 4];
    tempMAC += hexDigits[(int)data[DHCP_PACKET_CLIENT_ADDR_OFFSET + i] & 15];
    if(i < data[DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET] - 1)
      tempMAC += ":";
  }

  Serial.println();
  //parse options
  int opp = 240;
  while(opp < length)
  {
    switch(data[opp])
    {
      case 0x0C:
      {
        Serial.print("Device name: ");
        for(int i = 0; i < data[opp + 1]; i++)
        {
          Serial.print(data[opp + 2 + i]);
          tempName += data[opp + 2 + i];
        }
        Serial.println();
        break;
      }
      case 0x35:
      {
        Serial.print("Packet Type: ");
        switch(data[opp + 2])
        {
          case 0x01:
            Serial.println("Discover");
          break;
          case 0x02:
            Serial.println("Offer");
          break;
          case 0x03:
            Serial.println("Request");
            if(state == READY)
              state = RECEIVING;
          break;
          case 0x05:
            Serial.println("ACK");
          break;
          default:
            Serial.println("Unknown");
        }
        break;
      }
      case 0x32:
      {
        Serial.print("Device IP: ");
        printIP(&data[opp + 2]);
        Serial.println();
        for(int i = 0; i < 4; i++)
        {
          tempIP += (int)data[opp + 2 + i];
          if(i < 3) tempIP += '.';
        }
        break;
      }
      case 0x36:
      {
        Serial.print("Server IP: ");
        printIP(&data[opp + 2]);
        Serial.println();
        break;
      }
      case 0x37:
      {
        Serial.println("Request list: ");
        printHex(&data[opp + 2], data[opp + 1]);
        break;
      }
      case 0x39:
      {
        Serial.print("Max DHCP message size: ");
        Serial.println(((unsigned int)data[opp + 2] << 8) | (unsigned int)data[opp + 3]);
        break;
      }
      case 0xff:
      {
        Serial.println("End of options.");
        opp = length; 
        continue;
      }
      default:
      {
        Serial.print("Unknown option: ");
        Serial.print((int)data[opp]);
        Serial.print(" (length ");
        Serial.print((int)data[opp + 1]);
        Serial.println(")");
        printHex(&data[opp + 2], data[opp + 1]);
      }
    }

    opp += data[opp + 1] + 2;
  }
  if(state == RECEIVING)
  {
    newName = tempName;
    newIP = tempIP;
    newMAC = tempMAC;
    Serial.println("Stored data.");
    state = RECIEVED;
  }
  Serial.println();
}
void setupUDP()
{
  if(udp.listen(67)) 
  {
    Serial.print("UDP Listening on IP: ");
    Serial.println(WiFi.localIP());
    udp.onPacket([](AsyncUDPPacket packet) 
    {
      char *data = (char *)packet.data();
      int length = packet.length();
      parsePacket(data, length);
    });
  };
}

void setup() 
{
  Serial.begin(115200);

  initTft();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) 
  {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  while (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time, retrying");
    delay(500);
  }  
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");  
  
  setupUDP();
  Serial.println();
}

#define TFT_GREY 0x5AEB // New colour

void loop() 
{
  delay(20);
  if(state == RECIEVED)
  {
    printData();
    state = READY;
  }
}