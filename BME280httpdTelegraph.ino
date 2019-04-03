// Built to run on an ESP8266 D1 mini Pro
// Lots of code deserves credit from others and all the horrible hacks are my own
// It does actually work however, not efficiently. Feel free to steal and improve.

#include <Adafruit_BME280.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include <time.h>

// LED Definition
#define LED D4

// ------- Telegram config --------
#define BOT_TOKEN "xxxxxxxxx:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // your Bot Token (Get from Botfather)
#define CHAT_ID "xxxxxxxxx" // Chat ID of where you want the message to go (You can use MyIdBot to get the chat ID)

long Bot_mtbs = 0; // mean time between scan messages
long Bot_mtba = 0; // mean time between alert messages
long Bot_lasttime = 0;   // last timed message sent
long Bot_lastalert = 0;   // last time alert has been sent
long Bot_onHour;          // Timer to send regular messages on the hour

const int TEMP_MIN = 30;
const int TEMP_MAX = 50;
long curTemp;

Adafruit_BME280 bme = Adafruit_BME280(); // I2C

// Wifi credentials
const char* ssid     = "mywlan";
const char* password = "mypassword";

// Port number
#define PORT 80

// mDNS name
//const char* MDNS_NAME = "garage-bme280";
const char* MDNS_NAME = "hotwater-bme280";

// Location of the sensor (e.g. Living Room, Kitchen, Garden, etc)
//const char* LOCATION = "Garage Sensor";
const char* LOCATION = "Hot Water Tank Sensor";

// Default theme
const char* BKGD_COLOUR = "#ffffff";
const char* LOCATION_BKGD_COLOUR = "#cccccc";
const char* LOCATION_TEXT_COLOUR = "#ffffff";
const char* MODULE_COLOUR = "#666666";
const char* RESULT_COLOUR = "#000000";

ESP8266WebServer server(PORT);

// SSL client needed for both libraries
WiFiClientSecure client;

UniversalTelegramBot bot(BOT_TOKEN, client);

String ipAddress = "";
String webString = "";   // String to display

int timezone = 0;
int dst = 0;

void setup(void) {
  Wire.begin();
  Serial.begin(115200);  // Initiate serial connection
  pinMode(LED, OUTPUT);
  delay(10);

  // Set STATION mode and disconnect any previous connection
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // Connect to WiFi network
  Serial.println("Connecting to wifi...");
  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {

    // Blink LED when connecting to wifi
    digitalWrite(LED, LOW);
    delay(250);
    digitalWrite(LED, HIGH);
    delay(250);
    Serial.print(".");
  }
  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());
  //Serial.println(WiFi.enableAP(0));

  Serial.println("BME280 Temperature, Humidity and Pressure Server");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  IPAddress ip = WiFi.localIP();
  Serial.println(ip);

  ipAddress = ip.toString();

  // Set up mDNS responder:
  if (!MDNS.begin(MDNS_NAME)) {
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");

  configTime(0, 0, "0.uk.pool.ntp.org", "1.uk.pool.ntp.org", "2.uk.pool.ntp.org");
  Serial.println("\nWaiting for time");
  while (!time(nullptr)) {
    Serial.print(".");
    delay(1000);
  }
  delay(1000);

  myCurrentTime();

  server.on("/", []() {
    webString = "<!DOCTYPE html>\n\
<html lang=\"en\">\n\
<head>\n\
<title>BME280 Temperature, Humidity and Pressure Server</title>\n\
<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />\n\
<meta http-equiv=\"refresh\" content=\"45\" />\n\
<meta name=\"viewport\" content=\"initial-scale=1, maximum-scale=1\" />\n\
<link href=\"https://fonts.googleapis.com/css\?family=PT+Sans+Narrow:400,700\" rel=\"stylesheet\" />\n\
<style>\n\
body { margin:0; padding:0; background-color:" + String(BKGD_COLOUR) + "; }\n\
.location { margin:0; padding:0; width:100%; height:22px; background-color:" + String(LOCATION_BKGD_COLOUR) + "; text-align:center; font:400 16px/22px 'PT Sans Narrow',sans-serif; color:" + String(LOCATION_TEXT_COLOUR) + "; text-transform:uppercase; letter-spacing:0.1em; margin-bottom:65px; }\n\
.module { margin:0; padding:0 0 60px 0; text-align:center; font:700 18px/0px 'PT Sans Narrow',sans-serif; color:" + String(MODULE_COLOUR) + "; text-transform:uppercase; letter-spacing:0.1em; }\n\
.result { margin:0; padding:0 0 73px 0; text-align:center; font:400 74px/0px 'PT Sans Narrow',sans-serif; color:" + String(RESULT_COLOUR) + "; letter-spacing:-0.02em; }\n\
.symbol { margin:0; padding:0 0 73px 0; text-align:center; font:400 54px/0px 'PT Sans Narrow',sans-serif; color:" + String(RESULT_COLOUR) + "; letter-spacing:-0.02em; }\n\
</style>\n\
</head>\n\
<body>\n\
<div class=\"location\">" + String(LOCATION) + "</div>\n\
<div class=\"module\">Temperature</div>\n\
<div class=\"result\">" + String(bme.readTemperature(), 1) + "<span class=\"symbol\">&deg;C</span></div><br />" + "\n\
<div class=\"module\">Relative Humidity</div>\n\
<div class=\"result\">" + String(bme.readHumidity(), 1) + "<span class=\"symbol\">%</span></div><br />" + "\n\
<div class=\"module\">Pressure</div>\n\
<div class=\"result\">" + String(bme.readPressure() / 100, 1) + "<span class=\"symbol\"> hPa</span></div>\n\
</body>\n\
</html>";
    server.send(200, "text/html", webString); // send to browser when asked

    digitalWrite(LED, LOW);
    delay(50);
    digitalWrite(LED, HIGH);
    Serial.print(".");
    sendWaterTempAlert(myCurrentTime());
  });

  server.begin();
  Serial.println("HTTP server started");

  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", PORT);

  if (!bme.begin()) {
    Serial.println("Couldn't find BME280!");
    while (1);
  }

  bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                  Adafruit_BME280::SAMPLING_X2, // temperature
                  Adafruit_BME280::SAMPLING_X4, // pressure
                  Adafruit_BME280::SAMPLING_X1, // humidity
                  Adafruit_BME280::FILTER_X16,
                  Adafruit_BME280::STANDBY_MS_1000);
}

String myCurrentTime() {
  time_t now = time(nullptr);

  struct tm * ptm;
  ptm = gmtime(&now);

  String myCurTime = String(ptm->tm_mday);
  myCurTime.concat(" ");
  myCurTime.concat(ptm->tm_hour);
  myCurTime.concat(":");
  myCurTime.concat(ptm->tm_min);
  myCurTime.concat(":");
  myCurTime.concat(ptm->tm_sec);

  Serial.println(myCurTime);

  return myCurTime;
}

int getSecsToHour() {
  time_t now = time(nullptr);

  struct tm * ptm;
  ptm = gmtime(&now);

  int mySecsToHour = 3600 - ((ptm->tm_min) * 60 + (ptm->tm_sec));
  return mySecsToHour;
}

int getHours() {
  time_t now = time(nullptr);

  struct tm * ptm;
  ptm = gmtime(&now);

  int myUTCHours = (ptm->tm_hour);
  Serial.print("myUTCHours -- ");
  Serial.println(myUTCHours);
  return myUTCHours;
}

void sendTelegramMessage(String myTime) {
  String message = "REGULAR UPDATE\n";
  message.concat("HWTemp: ");
  message.concat(String(bme.readTemperature(), 1));
  message.concat("\n");
  message.concat("mBar: ");
  message.concat(String(bme.readPressure() / 100, 1));
  message.concat("\n");
  message.concat("%RH: ");
  message.concat(String(bme.readHumidity(), 1));
  message.concat("\nhttp://192.168.1.22/\n");
  message.concat(myTime);

  client.setInsecure();
  if (bot.sendMessage(CHAT_ID, message, "")) {
    Serial.println("TELEGRAM successfully sent");
  }
}

void sendWaterTempAlert(String myTime) {
  if (millis() > Bot_lastalert + Bot_mtba) {
    String message = "!! HOTWATER ALERT !!\n";
    message.concat("HWTemp: ");
    message.concat(String(bme.readTemperature(), 1));
    message.concat("\n");
    message.concat("mBar: ");
    message.concat(String(bme.readPressure() / 100, 1));
    message.concat("\n");
    message.concat("%RH: ");
    message.concat(String(bme.readHumidity(), 1));
    message.concat("\nhttp://192.168.1.22/\n");
    message.concat(myTime);

    client.setInsecure();
    if (bot.sendMessage(CHAT_ID, message, "")) {
      Serial.println("TELEGRAM successfully sent");
    }
    Bot_lastalert = millis();
    Bot_mtba = 300000; // prevent the web page from sending messages every refresh (5mins min between alerts)
  }
}

void loop(void) {
  server.handleClient();

  if (time(nullptr) > Bot_lasttime + Bot_mtbs + Bot_onHour) {
    Serial.println("\nTrying to send message...");
    sendTelegramMessage(myCurrentTime());
    //Serial.print(ptm);
    Bot_lasttime = time(nullptr);
    Bot_mtbs = 0; //3600; // 1 hour in seconds
    Bot_onHour = getSecsToHour();

    switch (getHours()) {
      case 0:
        // It's 00:mm, add 4 x 3600 to the seconds to the next hour for a message at 6am UTC+1
        Bot_onHour = Bot_onHour + 4 * 3600;
        break;
      case 5:
        // It's 06:mm, add 5 x 3600 to the seconds to the next hour for a message at 12noon UTC+1
        Bot_onHour = Bot_onHour + 5 * 3600;
        break;
      case 11:
        // It's 12:mm, add 4 x 3600 to the seconds to the next hour for a message at 5pm UTC+1
        Bot_onHour = Bot_onHour + 4 * 3600;
        break;
      case 16:
        // It's 17:mm, add 3 x 3600 to the seconds to the next hour for a message at 9pm UTC+1
        Bot_onHour = Bot_onHour + 3 * 3600;
        break;
      case 20:
        // It's 21:mm, add 8 x 3600 to the seconds to the next hour for a message at 6am UTC+1
        Bot_onHour = Bot_onHour + 8 * 3600;
        break;
      default:
        // Somethings odd - or we've just restarted so don't change Bot_onHour
        break;
    }

    Serial.print("Bot_lasttime -- ");
    Serial.println(Bot_lasttime);
    Serial.print("Bot_onHour -- ");
    Serial.println(Bot_onHour);
  }

  curTemp = bme.readTemperature();
  if ((curTemp < TEMP_MIN) || (curTemp > TEMP_MAX)) {
    if (millis() > Bot_lastalert + Bot_mtba) {
      Serial.println("\nWater Temp!");
      sendWaterTempAlert(myCurrentTime());
      //Serial.print(ptm);
      Bot_lastalert = millis();
      Bot_mtba = 1200000;
    }
  }
}
