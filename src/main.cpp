#include <WiFi.h>
#include <TFT_eSPI.h> // Graphics and font library for ILI9341 driver chip
#include <Button2.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

#define BUTTON_1        35
#define BUTTON_2        0
#define TFT_BL          4  // Display backlight control pin
static int DEBUG = 0;

WiFiClient client;
HTTPClient http;

TFT_eSPI tft = TFT_eSPI();  // Invoke library
Button2 btn1(BUTTON_1);
Button2 btn2(BUTTON_2);
int tft_rotation = 3;

int fnt=4;

char ssid[] = "ART";
char pass[] = "qazwsxedc2";

char url[1000];
char httpBuffer[10000];
const char* url1 = "https://services1.arcgis.com/0MSEUqKaxRlEPj5g/arcgis/rest/services/Coronavirus_2019_nCoV_Cases/FeatureServer/1/query?where=Country_Region%20like%20'%25";
const char* url2 = "%25'&outFields=Last_Update,Confirmed,Deaths,Recovered&returnGeometry=false&outSR=4326&f=json";
static int curr_country = 0;
const char *country[] = {
  "UKRAINE",
  "US",
  "POLAND",
  "ITALY",
  "ISRAEL"
};

enum stateMachine {
  ERROR_SCREEN = 0,
  INIT_SCREEN,
  WIFI1_SCREEN,
  WIFI2_SCREEN,
  NTP1_SCREEN,
  NTP2_SCREEN,
  CURL_SCREEN,
  STAT_SCREEN,
  CHANGE_COUNTRY,
};

typedef void (*btnHandler)();

struct _sm {
  int id;
  int b1;
  int b2;
  int timeout;
  int escape;
} screen[] = {
  { .id = ERROR_SCREEN,
    .b1 = 0, 
    .b2 = 0, 
    .timeout = 0, 
    .escape = 0
  }, // error screen
  { .id = INIT_SCREEN,
    .b1 = 0, 
    .b2 = 0,  
    .timeout = 5, 
    .escape = 0
  }, // init screen
  { .id = WIFI1_SCREEN,
    .b1 = 0,
    .b2 = 0,
    .timeout = 10,
    .escape = 2
  }, // Wifi init screen
  { .id = WIFI2_SCREEN,
    .b1 = 0, 
    .b2 = 0, 
    .timeout = 10,
    .escape = NTP1_SCREEN
  }, // Wifi stats screen
  { .id = NTP1_SCREEN,
    .b1 = 0, 
    .b2 = 0, 
    .timeout = 30, 
    .escape = ERROR_SCREEN
  }, // NTP req screen
  { .id = NTP2_SCREEN,
    .b1 = 0, 
    .b2 = 0,
    .timeout = 10,
    .escape = CURL_SCREEN
  }, // NTP reply screen
  { .id = CURL_SCREEN,
    .b1 = 0, 
    .b2 = 0,
    .timeout = 100,
    .escape = CURL_SCREEN
  }, // Web req screen
  { .id = STAT_SCREEN,
    .b1 = CHANGE_COUNTRY, 
    .b2 = 0,
    .timeout = 7200,
    .escape = NTP1_SCREEN
  }, // stats print screen
  { .id = CHANGE_COUNTRY,
    .b1 = 0, 
    .b2 = 0,
    .timeout = 1,
    .escape = CURL_SCREEN
  }, // country change screen
};

static int curr_screen, next_screen;
struct tm timeinfo;
bool timeValid = false;

void initScreen()
{
  tft.init();
  tft.setRotation(tft_rotation);
}

void blankScreen(const char* msg = NULL)
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_DARKCYAN);
  tft.setCursor(0, 0);
  tft.setTextFont(fnt);
  tft.setTextSize(1);
  if (msg)
    tft.printf("%s ", msg);
  else if (curr_screen == STAT_SCREEN)
    tft.printf("%s ", country[curr_country]);
  else
    tft.printf("Screen: %d ", curr_screen);
  if (timeValid)
    tft.printf("(%02d:%02d:%02d)\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  else
    tft.printf("\n");
}

void button_init()
{
    btn1.setPressedHandler([](Button2 & b)
    {
      if (DEBUG) Serial.printf("b1 %d\n", screen[curr_screen].b1);
      if (screen[curr_screen].b1)
        next_screen = screen[curr_screen].b1;
    });

    btn2.setPressedHandler([](Button2 & b)
    {
      if (DEBUG) Serial.printf("b2 %d\n", screen[curr_screen].b2);
      if (screen[curr_screen].b2)
        next_screen = screen[curr_screen].b2;
    });
}

/*----------------------------------------------------------
   setup
  ----------------------------------------------------------*/
void setup()
{
  if (DEBUG) Serial.begin(115200);

  curr_screen = ERROR_SCREEN;
  next_screen = INIT_SCREEN;

  button_init();
  initScreen();
}

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 2*3600;
const int   daylightOffset_sec = 3600;
int Confirmed = 0;
int Deaths = 0;
int Recovered = 0;
bool NewData = false;

void do_screen()
{
  if (DEBUG) Serial.printf("Screen: %d\n", curr_screen);

  switch(curr_screen)
  {
    case ERROR_SCREEN:
      blankScreen();
      tft.setTextSize(2);
      tft.setTextColor(TFT_RED);
      tft.printf("ERROR\n");
      tft.setTextSize(1);
      tft.setTextColor(TFT_YELLOW);
      tft.printf("Press ?? to reset\n");
      break;
    case INIT_SCREEN:
      blankScreen();
      tft.setTextSize(2);
      tft.printf("HELLO\n");
      next_screen = WIFI1_SCREEN;
      break;
    case WIFI1_SCREEN:
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid, pass);
      blankScreen();
      tft.setTextSize(1);
      tft.printf("Connecting to: %s", ssid);
      next_screen = WIFI2_SCREEN;
      break;
    case WIFI2_SCREEN:
      if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        blankScreen();
        tft.setTextSize(1);
        tft.print("Connected to wifi\n");
        
        tft.printf("SSID: %s\n", ssid);
        tft.printf("IP: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
        tft.printf("RSSI: %d dbm", WiFi.RSSI());
        next_screen = NTP1_SCREEN;
      }
    break;
    case NTP1_SCREEN:
      blankScreen();
      tft.setTextSize(1);
      tft.print("Fetching time via NTP\n");
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      next_screen = NTP2_SCREEN;
      break;
    case NTP2_SCREEN:
    {
      blankScreen();
      tft.setTextSize(1);
      if(getLocalTime(&timeinfo)) {
        tft.printf("Time: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        timeValid = true;
        next_screen = CURL_SCREEN;
      }
      break;
    }
    case CURL_SCREEN:
      {
        // build the url
        strcpy(url, url1);
        strcat(url, country[curr_country]);
        strcat(url, url2);
        if (DEBUG) Serial.printf("%s\n", url);
        http.begin(url);
        int httpCode = http.GET();

        if (httpCode > 0) { //Check for the returning code

          String payload = http.getString();
          payload.toCharArray(httpBuffer, 9999);
          
          blankScreen("Loading...");
          tft.setTextSize(1);

          //if (DEBUG) Serial.println(payload);
          const size_t capacity = JSON_ARRAY_SIZE(4) + JSON_ARRAY_SIZE(73) + 73*JSON_OBJECT_SIZE(1) + 2*JSON_OBJECT_SIZE(2) + 73*JSON_OBJECT_SIZE(4) + 3*JSON_OBJECT_SIZE(6) + 2*JSON_OBJECT_SIZE(7) + 4217;
          DynamicJsonDocument doc(capacity);

          deserializeJson(doc, payload);

          Confirmed = 0;
          Deaths = 0;
          Recovered = 0;
          
          int num_rec = doc["features"].size();
          int tmp;
          if (DEBUG) Serial.println(num_rec);
          for(int rec = 0; rec < num_rec; rec++)
          {
            JsonObject features_0_attributes = doc["features"][rec]["attributes"];
            tmp = features_0_attributes["Confirmed"];;
            Confirmed += tmp;
            tmp = features_0_attributes["Deaths"];
            Deaths += tmp;
            tmp = features_0_attributes["Recovered"];
            Recovered += tmp;
          }
          NewData = true;
          next_screen = STAT_SCREEN;
        } 
        else 
        {
          tft.printf("Error on HTTP request");
        }

        http.end();

        break;
      }
    case STAT_SCREEN:
      if (NewData)
      {
        blankScreen();
        tft.setTextSize(1);
        tft.setTextColor(TFT_YELLOW);
        tft.printf("Confirmed: %d\n", Confirmed);
        tft.setTextColor(TFT_RED);
        tft.printf("Died: %d\n", Deaths);
        tft.setTextColor(TFT_GREEN);
        tft.printf("Recovered: %d\n", Recovered);
        NewData = false;
      }
    break;
    case CHANGE_COUNTRY:
      curr_country++;
      curr_country %= sizeof(country)/sizeof(country[0]);
      next_screen = CURL_SCREEN;
    break;

    default:
    break;
  }
}

/*----------------------------------------------------------
     loop
  ----------------------------------------------------------*/
static int timeout_counter = -1;
static int time_counter = -1;

void loop() 
{
  if (next_screen != curr_screen)
  {
    timeout_counter = -1;
    time_counter = -1;
    curr_screen = next_screen;
    return;
  } 
  else if (timeout_counter < 0 && screen[curr_screen].timeout > 0)
  {
    timeout_counter = screen[curr_screen].timeout;
  }
  else if (timeout_counter == 0)
  {
    timeout_counter = -1;
    next_screen = screen[curr_screen].escape;
    return;
  }
  else if (time_counter == 0 && timeout_counter > 0) {
    timeout_counter--;
    if (DEBUG) Serial.printf("timeout in: %d\n", timeout_counter);
  }

  if (time_counter > 0)
  {
    delay(10);
    time_counter--;
  }
  else
  {
    do_screen();
    time_counter = 100; // 100*10ms
  }

  btn1.loop();
  btn2.loop();
}