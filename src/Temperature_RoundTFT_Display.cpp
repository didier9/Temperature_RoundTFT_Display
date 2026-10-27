// temperature display using MQTT on 1.28" round TFT LCD with integrated SD card socket
// with ESP32 (not C3)
// use ESP32 Dev Module

#include <Arduino.h>
#include <SPI.h>
#include "Adafruit_GFX.h"
#include "Adafruit_GC9A01A.h"
#include <WiFi.h>
#include <MQTT.h>
#include <FreeSansBold18pt7b.h>
#include <FreeSans9pt7b.h>
#include "RobotoRegular14pt7b.h"
#include <esp_wifi.h>
// C:\Users\djuge\Documents\!Sync\Sync\Arduino\Libraries\M5GFX\src\lgfx\Fonts\GFXFF

// interesting post about getTextBounds(): https://forum.arduino.cc/t/adafruit-gfx-gettextbounds-cursor-position-vs-bounding-box-top-left/1239441

// v002 swiping up and down works
// v003 add WiFi/MQTT
// v004 Changed default font to FreeMono12pt7b
// changed project to RoundTFTDisplay
// v001 Changed font to FreeSansBold18pt7b
//      Added mqtt commands
// v002   make project topic more easily customizable
//        added trim() function
//        only repaint the screen when data changes
// v003   Removed alarm display (was messed up)
//        Added center() function to print text centered on the X axis
// v004   Add connecting info on the display
// v005   Cleaned different background colors with tftRefresh()
// v006   Cleaned up wifiConnect() and mqttConnect()
// v007   Fixed issue with publishing from inside mqtt callback
// v008   Added last byte of MAC address to version on display
// v009   Switched TFT from bit-banged software SPI to hardware SPI (remapped
//        pins, shared bus with SD card) - display refresh went from ~1s to
//        near-instant, and frees up the CPU for other time-sensitive work
#define PROJECT_ID    "RoundTFT Display"
#define VERSION       "v009"

/* ----- user settings ----- */
// WIFI_SSID, WIFI_PASSWORD, MQTT_HOST, MQTT_USER and MQTT_PASSWORD are in secrets.h
#include "secrets.h"

#define TEST            false

#define MQTT_PORT       1883

#define TEST_TOPIC_HDR      "test/"
#define PROJECT_TOPIC_HDR   "roundTFT_display/"
#define ID_TOPIC            "id"
#define CMD_TOPIC           "cmd"
#define WILL_TOPIC          "will"
#define WILL_MSG            "disconnected"
#define TEST_TOPIC          "test"

#define POOL_TEMPERATURE_TOPIC     "swcg/temperature"
#define OUTSIDE_TEMPERATURE_TOPIC  "outside/sensor5/temperature"

#define POOL_TEMP_INTERVAL    350 // 300 seconds
#define OUTSIDE_TEMP_INTERVAL 70  // 10 seconds

#define ROWNUM_OUTSIDE_LBL  2
#define ROWNUM_OUTSIDE_VAL  3
#define ROWNUM_POOL_LBL     0
#define ROWNUM_POOL_VAL     1
#define ROWNUM_VERSION      4

/* ----- end of user settings ----- */

// CS:        5
// DC:        27
// RESET:     33
// SPI_MOSI:  15
// SPI_CLK:   14
//#define RES 33
//#define DC  27
//#define CS  5

#define VBAT_PIN  35 // analog
// SD card-SCK: 14 miso:2 Mosi: 15 SS: 13 (card connected according to SDIO, but I used SPI with remap pin)
#define sd_sck    14
#define sd_miso   2
#define sd_mosi   15
#define sd_ss     13

#define tft_cs    5
#define tft_dc    27
#define tft_rst   33
#define tft_sck   sd_sck   // shares the SPI bus with the SD card (hardware SPI, remapped pins)
#define tft_mosi  sd_mosi
#define TFT_BL   32

Adafruit_GC9A01A tft( tft_cs, tft_dc, tft_rst );  // hardware SPI; pins remapped via SPI.begin() in setup()

#define X_OFFSET            25    // where to start printing messages
#define DEFAULT_BG_COLOR    GC9A01A_BLUE
#define DEFAULT_TEXT_COLOR  GC9A01A_WHITE
#define ALARM_BG_COLOR      GC9A01A_RED
#define ALARM_TEXT_COLOR    GC9A01A_WHITE

#define REFRESH_FULL        1
#define REFRESH_BG_ONLY     0

int color[] = { GC9A01A_BLUE, GC9A01A_GREEN, GC9A01A_MAGENTA, GC9A01A_YELLOW, GC9A01A_WHITE };
char msg[4][18] = { PROJECT_ID, "", "", "" };
//int line[4] = { 55, 90, 125, 160 }; // for font size 12
int line[5] = { 60, 105, 150, 195, 220 };  // for font size 18 and last line font size 9

unsigned long timer, lastMillis, lastPoolTemp, lastOutsideTemp, idTimer;
uint16_t bgColor=DEFAULT_BG_COLOR;

const char ssid[] = WIFI_SSID;
const char pass[] = WIFI_PASSWORD;
WiFiClient net;

MQTTClient mqttClient;

bool wifiConnecting = false;
bool mqttConnecting = false;

char mqttID[40];

char idTopic[60] = {0};       // "id"
char cmdTopic[60] = {0};      // "cmd"
char testTopic[60] = {0};     // "test/"
char willTopic[60] = {0};     // "will"
char projectIdMsg[40] = {};

char outside_temp[10];
char pool_temp[10];

#define MAX_PAYLOAD_LEN         40
char topic[40], payload[MAX_PAYLOAD_LEN+1];
uint16_t mac;

/* ----- prototypes ----- */
void readMacAddress();
void trim( char *p );
void showAlarm( char *a );
bool wifiConnect();
bool mqttConnect();
void reSubscribe( void );
void mqttMessageReceived( String &topic, String &payload );
void updateOutsideTemperature( void );
void updatePoolTemperature( void );
void tftRefresh( bool bg );
void centerText( char *t, int y );


void readMacAddress(){
  uint8_t baseMac[6];
  esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
  if (ret == ESP_OK) {
    Serial.printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
                  baseMac[0], baseMac[1], baseMac[2],
                  baseMac[3], baseMac[4], baseMac[5]);
    mac = (uint16_t)baseMac[4] * 256 + baseMac[5];
  } else {
    Serial.println("Failed to read MAC address");
  }
}   // readMacAddress()

// trim()
// remove leading and terminating spaces, in place (pointer to beginning of string does not change)
void trim( char *p ){
  char *p0=p, *p1=p;
  while( *p1 == ' ' )
    p1++;
  do{
    *p++ = *p1;
  }while( *p1++ != 0 );
  p -= 2;
  while( *p == ' ' )
    *p-- = 0;
}   // trim()

void showAlarm( char *a ){
  bgColor = ALARM_BG_COLOR;
  tft.fillScreen( bgColor );
  tft.setTextColor( ALARM_TEXT_COLOR );
  centerText( a, 125 ); 
} // showAlarm()

bool wifiConnect(){
  Serial.print( "Connecting to wifi..." );
  tft.setFont( &RobotoRegular14pt7b );
  centerText( (char *)"Connecting...", line[2] );
  tft.setFont( &FreeSansBold18pt7b );
  int i=0;
  while( WiFi.status() != WL_CONNECTED ){
    Serial.print( "." );
    delay( 1000 );
    i++;
    if( i > 30 ){
      Serial.println( "\nNo WiFi" );
      break;
    }
  }
  if( i <= 30 ){
    Serial.println( "\nWiFi connected" );
    if( !mqttConnect() ){
      return( false );
    }else{
      return( true );
    }
  }else{
    showAlarm( (char *)"No WiFi!" );
    Serial.println( "wifiConnect() failed" );
    return( false );
  }
}   // wifiConnect()

void reSubscribe( void ){
  Serial.println( "Subscribing..." );
  mqttClient.subscribe( cmdTopic );
  delay( 150 );
  mqttClient.subscribe( POOL_TEMPERATURE_TOPIC );
  delay( 150 );
  mqttClient.subscribe( OUTSIDE_TEMPERATURE_TOPIC );
}   // reSubscribe()

bool mqttConnect(){
  char i=0;
  Serial.printf( "\nConnecting to MQTT broker as %s...", mqttID );
  while( !mqttClient.connect( mqttID, MQTT_USER, MQTT_PASSWORD ) && i < 10 ){
    Serial.print(".");
    delay( 1000 );
    i++;
  }
  if( i < 10 ){
    Serial.println("\nMQTT connected");
    bgColor = DEFAULT_BG_COLOR;
    tftRefresh( REFRESH_FULL );
    centerText( (char *)"????", line[ROWNUM_OUTSIDE_VAL] );
    centerText( (char *)"????", line[ROWNUM_POOL_VAL] );
    mqttClient.publish( willTopic, WILL_MSG );
    reSubscribe();
    return( true );
  }else{
    Serial.println( "mqttConnect failed" );
    showAlarm( (char *)"No MQTT!" );
    return( false );
  }
} // mqttConnect()

char toPublishTopic[40], toPublishPayload[40];
bool toPublishFlag=false;

void mqttMessageReceived( String &topicStr, String &payloadStr ){
  // Note: Do not use mqttClient in this callback to publish, subscribe or
  // unsubscribe as it may cause deadlocks when other things arrive while
  // sending and receiving acknowledgments. Instead, change a global variable (toPublishFlag),
  // or push to a queue and handle it in the loop after calling `client.loop()`.
  Serial.println( "incoming: " + topicStr + " - " + payloadStr );
  strcpy( topic, topicStr.c_str() );
  strcpy( payload, payloadStr.c_str() );
  //Serial.printf( "topic: %s, payload: %s-\n", topic, payload );

  if( strcmp( topic, cmdTopic ) == 0 ){
    //Serial.println( "Received command" );
    if( strcmp( payload, "?id" ) == 0 ){
      strcpy( toPublishTopic, idTopic );
      strcpy( toPublishPayload, PROJECT_ID );
      toPublishFlag = true;
    }else if( strcmp( payload, "?test" ) == 0 ){
      strcpy( toPublishTopic, testTopic );
      strcpy( toPublishPayload, "This is a test" );
      toPublishFlag = true;
    }
  }else if( strcmp( topic, OUTSIDE_TEMPERATURE_TOPIC ) == 0 ){
    // payload looks like this: - 23.4, 18:58:50-05-10-2025-
    //Serial.printf( "Received -%s-\n", payload );
    if( strchr( payload, ',' ) != NULL ){
      *strchr( payload, ',' ) = '\0';  // strip the time stamp
      trim( payload );
      if( strcmp( payload, outside_temp ) != 0 ){
        strcpy( outside_temp, payload );
        updateOutsideTemperature();
      }
      lastOutsideTemp = millis();
    }
  }else if( strcmp( topic, POOL_TEMPERATURE_TOPIC ) == 0 ){
    // payload looks like this: -27.06-
    //Serial.printf( "Received -%s-\n", payload );
    trim( payload );
    if( strlen( payload ) == 5 ){
      *(payload+4) = '\0';
      //if( strcmp( payload, pool_temp ) != 0 ){
        strcpy( pool_temp, payload );
        updatePoolTemperature();
      //}
    }
    lastPoolTemp = millis();
  }
}   // mqttMessageReceived()

void updateOutsideTemperature( void ){
  if( bgColor != DEFAULT_BG_COLOR )
    tftRefresh( REFRESH_FULL );
  tft.setTextSize( 1 );
  tft.setTextColor( DEFAULT_TEXT_COLOR );
  tft.setFont( &FreeSansBold18pt7b );
  tft.fillRect( X_OFFSET+20, line[ROWNUM_OUTSIDE_LBL]+10, 140, line[ROWNUM_OUTSIDE_VAL] - line[ROWNUM_OUTSIDE_LBL], bgColor );
  centerText( outside_temp, line[ROWNUM_OUTSIDE_VAL] );
  lastOutsideTemp = millis();
} // updateOutsideTemperature()

void updatePoolTemperature( void ){
  if( bgColor != DEFAULT_BG_COLOR )
    tftRefresh( REFRESH_FULL );
  tft.setTextSize( 1 );
  tft.setTextColor( DEFAULT_TEXT_COLOR );
  tft.setFont( &FreeSansBold18pt7b );
  tft.fillRect( X_OFFSET+20, line[ROWNUM_POOL_LBL]+10, 140, line[ROWNUM_POOL_VAL] - line[ROWNUM_POOL_LBL], bgColor );
  centerText( pool_temp, line[ROWNUM_POOL_VAL] );
  lastPoolTemp = millis();
} // updatePoolTemperature()

/*---------------------------------------------------------------------------
* setup()
*---------------------------------------------------------------------------*/
void setup() {
  char projectTopicHeader[40] = {0};

  Serial.begin( 115200 );
  
  SPI.begin( tft_sck, -1, tft_mosi, tft_cs );  // remap hardware SPI to these pins before tft.begin()
  tft.begin();

#if defined( TFT_BL )
  pinMode( TFT_BL, OUTPUT );
  digitalWrite( TFT_BL, HIGH ); // Backlight on
#endif // end TFT_BL

  WiFi.begin( WIFI_SSID, WIFI_PASSWORD );
  delay( 3000 );

  if( TEST ){
    strcpy( idTopic, TEST_TOPIC_HDR );
    strcpy( cmdTopic, TEST_TOPIC_HDR );
    strcpy( willTopic, TEST_TOPIC_HDR );
    strcpy( testTopic, TEST_TOPIC_HDR );
    strcpy( projectTopicHeader, TEST_TOPIC_HDR );
  }
  readMacAddress();
  Serial.printf( "%s %s %04X\n", PROJECT_ID, VERSION, mac );
  sprintf( projectTopicHeader + strlen( projectTopicHeader ), "%s%04X/", PROJECT_TOPIC_HDR, mac );

  strcat( idTopic, projectTopicHeader );  
  strcat( cmdTopic, projectTopicHeader );
  strcat( willTopic, projectTopicHeader );
  strcat( testTopic, projectTopicHeader );

  strcat( idTopic, ID_TOPIC );
  strcat( cmdTopic, CMD_TOPIC );
  strcat( willTopic, WILL_TOPIC );
  strcat( testTopic, TEST_TOPIC );
  sprintf( projectIdMsg, "%s %s", PROJECT_ID, VERSION );

  Serial.println( "\nDevice topics:" );
  Serial.println( idTopic );
  Serial.println( cmdTopic );
  Serial.println( willTopic );
  Serial.println( testTopic );

  Serial.println( "\nData topics:" );
  Serial.println( OUTSIDE_TEMPERATURE_TOPIC );
  Serial.println( POOL_TEMPERATURE_TOPIC );

  tftRefresh( REFRESH_BG_ONLY );
  tft.setFont( &RobotoRegular14pt7b );
  centerText((char *) PROJECT_ID, line[1] );

  //WiFi.begin( WIFI_SSID, WIFI_PASSWORD );
  //delay( 3000 );

  // Note: Local domain names (e.g. "Computer.local" on OSX) are not supported
  // by Arduino. You need to set the IP address directly.
  mqttClient.begin( MQTT_HOST, MQTT_PORT, net);
  mqttClient.onMessage( mqttMessageReceived );

  if( TEST ){
    centerText( (char *)"Test", 65 );
  }

  //readMacAddress();
  //sprintf( mqttID, "esp32_%d", random( 100, 999 ));
  sprintf( mqttID, "esp32_%04X", mac );

  wifiConnect();
  
  timer = millis();
  lastMillis = millis() + 60000;
  idTimer = lastMillis;
} // setup()

/*---------------------------------------------------------------------------
* loop()
*---------------------------------------------------------------------------*/
void loop(){
    static int lasti=-1;
    static unsigned long lastResubscribe = millis();
    static unsigned long mqttReconnect = millis();
    static unsigned long wifiReconnect = millis();
 
    mqttClient.loop();

    if( WiFi.status() != WL_CONNECTED ){
        if( wifiConnecting == false ){
            wifiConnecting = true;
            wifiReconnect = millis();
        }
        if( millis() - wifiReconnect > 10000  ){ // only try every 10 seconds
            wifiReconnect = millis();
            mqttClient.disconnect();
            wifiConnect();
        }
    }else{
        wifiConnecting = false;
        if( mqttClient.connected() == false ){
            if( mqttConnecting == false ){
                mqttConnecting = true;
                mqttReconnect = millis();
            }
            if( millis() - mqttReconnect > 10000 ){  // only try every 10 seconds
                mqttReconnect = millis();
                mqttConnecting = false;
                mqttConnect();
            }
        }else {
            // everything is good!

            if( toPublishFlag ){
                mqttClient.publish( toPublishTopic, toPublishPayload );
                toPublishFlag = false;
            }

            if( millis() - lastResubscribe > 600000 ){
                reSubscribe();
                lastResubscribe = millis();
            }

            if( millis() - lastPoolTemp > POOL_TEMP_INTERVAL*1000 ){
                if( strcmp( pool_temp, "????" ) != 0 ){
                    strcpy( pool_temp, "????" );
                    updatePoolTemperature();
                }
            }
            
            if( millis() - lastOutsideTemp > OUTSIDE_TEMP_INTERVAL*1000 ){
                if( strcmp( outside_temp, "????" ) != 0 ){
                    strcpy( outside_temp, "????" );
                    updateOutsideTemperature();
                }
            }

            // publish a message roughly every 60 second.
            if( millis() - lastMillis > 60000 ){
                lastMillis = millis();
                mqttClient.publish( idTopic, projectIdMsg );
            }
        }
    }
    if( millis() - idTimer > 60000 ){
        idTimer = millis();
        Serial.printf( "%s %s %04X\n", PROJECT_ID, VERSION, mac );
    }

} // loop()


void tftRefresh( bool bg ){
  char v[20];
  tft.setRotation( 0 );
  tft.setTextSize( 1 );    // text size 1 = 14 pixels in height for FreeMono12pt7b font
  bgColor = DEFAULT_BG_COLOR;
  tft.fillScreen( bgColor );
  tft.setTextColor( DEFAULT_TEXT_COLOR );
  tft.setFont( &FreeSans9pt7b );
  sprintf( v, "%s - %04X", VERSION, mac );
  centerText( v, line[ROWNUM_VERSION] );
  tft.setFont( &FreeSansBold18pt7b );
  if( bg == REFRESH_FULL ){
    centerText( (char *)"Outside:", line[ROWNUM_OUTSIDE_LBL] );
    centerText( (char *)"Pool:", line[ROWNUM_POOL_LBL] );
  }
} // tftRefresh()

/*---------------------------------------------------------------------------
* FUNCTION: centerText()
*
*---------------------------------------------------------------------------*/
void centerText( char *t, int y ){
  int16_t  x1, y1;
  uint16_t w, h;
  tft.getTextBounds( t, 0, y, &x1, &y1, &w, &h);
  tft.setCursor( (240-w)/2, y );
  tft.print( t );
}
