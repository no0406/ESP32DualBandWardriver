#include "configs.h"
#include "BatteryInterface.h"
#include "Buffer.h"
#include "settings.h"
#include "display.h"
#include "GpsInterface.h"
#include "SDInterface.h"
#include "Switches.h"
#include "WiFiOps.h"
#include "utils.h"
#include "ui.h"
#include "logger.h"

Buffer buffer;
Settings settings;
GpsInterface gps;
BatteryInterface battery;
WiFiOps wifi_ops;
Utils utils;
UI ui_obj;
bool g_force_display_redraw = false;

SPIClass sharedSPI(SPI);
Display display = Display(&sharedSPI, TFT_CS, TFT_DC, TFT_RST);
SDInterface sd_obj = SDInterface(&sharedSPI, SD_CS);

Switches u_btn = Switches(U_BTN, 1000, U_PULL);
Switches d_btn = Switches(D_BTN, 1000, D_PULL);
Switches c_btn = Switches(C_BTN, 1000, C_PULL);

// Boot menu: UP/DOWN to move, SELECT to choose.
// Falls back to BOOT_MODE_DEFAULT after BOOT_MENU_TIMEOUT,
// unless any button is pressed first, which stops the countdown.
uint8_t runBootMenu() {
  const char* options[] = {"1 Start WDR now", "2 Join saved WiFi", "3 Start AP (60s)"};
  const uint16_t option_colors[] = {ST77XX_GREEN, YELLOW, CYAN};
  const uint8_t option_count = sizeof(options) / sizeof(options[0]);
  const uint8_t option_y = 16;
  const uint8_t option_spacing = 12;

  uint8_t selected = BOOT_MODE_DEFAULT;
  int last_secs = -1;
  bool redraw = true;
  bool countdown_active = true;
  uint32_t start = millis();

  display.clearScreen();
  display.tft->setRotation(3);
  display.tft->setTextSize(1);
  display.tft->setTextWrap(false);

  display.tft->setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  display.tft->setCursor(0, 0);
  display.tft->print("== Boot Menu :) ==");
  display.tft->setCursor(0, TFT_HEIGHT - SMALL_CHAR_HEIGHT);
  display.tft->print("UP/DN:Move SEL:Choose");

  Logger::log(STD_MSG, "Showing boot menu...");

  while (true) {
    uint32_t elapsed = millis() - start;

    if ((countdown_active) && (elapsed >= BOOT_MENU_TIMEOUT)) {
      Logger::log(STD_MSG, "Boot menu timed out. Using default option.");
      selected = BOOT_MODE_DEFAULT;
      break;
    }

    if (u_btn.justPressed()) {
      selected = (selected == 0) ? option_count - 1 : selected - 1;
      redraw = true;
    }

    if (d_btn.justPressed()) {
      selected = (selected + 1) % option_count;
      redraw = true;
    }

    if (c_btn.justPressed())
      break;

    // Any input stops the countdown
    if ((redraw) && (countdown_active) && (last_secs != -1)) {
      countdown_active = false;
      display.tft->fillRect(TFT_WIDTH - 18, 0, 18, SMALL_CHAR_HEIGHT, ST77XX_BLACK);
      Logger::log(STD_MSG, "Boot menu countdown stopped.");
    }

    if (redraw) {
      redraw = false;
      for (uint8_t i = 0; i < option_count; i++) {
        int y = option_y + i * option_spacing;
        display.tft->fillRect(0, y, TFT_WIDTH, SMALL_CHAR_HEIGHT, ST77XX_BLACK);
        display.tft->setCursor(0, y);
        if (i == selected) {
          display.tft->setTextColor(ST77XX_BLACK, option_colors[i]);
          display.tft->print("> ");
        }
        else {
          display.tft->setTextColor(option_colors[i], ST77XX_BLACK);
          display.tft->print("  ");
        }
        display.tft->print(options[i]);
      }
    }

    int secs = (BOOT_MENU_TIMEOUT - elapsed + 999) / 1000;
    if ((countdown_active) && (secs != last_secs)) {
      last_secs = secs;
      String secs_str = String(secs) + "s";
      display.tft->fillRect(TFT_WIDTH - 18, 0, 18, SMALL_CHAR_HEIGHT, ST77XX_BLACK);
      display.tft->setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      display.tft->setCursor(TFT_WIDTH - secs_str.length() * 6, 0);
      display.tft->print(secs_str);
    }

    delay(20);
  }

  display.tft->setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  display.clearScreen();

  Logger::log(STD_MSG, "Boot option selected: " + String(options[selected]));

  return selected;
}

void setup() {
  Serial.begin(115200);

  while (!Serial)
    delay(10);

  // Do SPI stuff first
  sharedSPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

  // Give SPI some time I guess
  delay(100);

  // Init the display before SD
  display.begin();

  // Give SD some time
  delay(100);

  // Show us IDF information
  Logger::log(STD_MSG, "ESP-IDF version is: " + String(esp_get_idf_version()));

  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);

  // Load settings
  settings.begin();

  if (settings.getSettingType(SETTING_SANITY) == "") {
    Logger::log(WARN_MSG, "Current settings format not supported. Installing new default settings...");
    settings.createDefaultSettings(SPIFFS);
  }
  else {
    Logger::log(GUD_MSG, "Current settings format supported");
  }

  // Init our buffer for writing logs
  buffer = Buffer();

  // Init SD Card
  if(!sd_obj.initSD())
    Logger::log(WARN_MSG, "SD Card NOT Supported");

  // Check for firmware updates now
  Logger::log(STD_MSG, "Checking for firmware updates...");
  sd_obj.runUpdate();

  // Enable SD debug logging if setting is on
  Logger::enableSDLog(settings.loadSetting<bool>(DEBUG_LOG_NAME));

  // Init battery
  battery.RunSetup();
  battery.battery_level = battery.getBatteryLevel();

  // Init GPS
  gps.begin();

  ui_obj.begin();

  // Init wifi and bluetooth
  wifi_ops.begin(runBootMenu());

  // Init UI
  ui_obj.begin();

  settings.printJsonSettings(settings.getSettingsString());

  Logger::log(GUD_MSG, "Initialization complete!");
}

void loop() {
  // Take current time of this loop for functions
  uint32_t currentTime = millis();

  // Refresh all functions
  wifi_ops.main(currentTime, ui_obj.stat_display_mode == SD_FILES);
  settings.main(currentTime);
  battery.main(currentTime);
  gps.main();
  sd_obj.main();
  buffer.save();
  ui_obj.main(currentTime);

  // Solo or Core modes
  if ((gps.getFixStatus()) && (sd_obj.supported) && (ui_obj.stat_display_mode != SD_FILES))
    wifi_ops.setCurrentScanMode(WIFI_WARDRIVING);
  // Nodes
  else if ((wifi_ops.run_mode == NODE_MODE) && (wifi_ops.getNodeReady())) {
    wifi_ops.setCurrentScanMode(WIFI_WARDRIVING);
    digitalWrite(LED_PIN, HIGH);
  }
  else {
    wifi_ops.setCurrentScanMode(WIFI_STANDBY);
    if (wifi_ops.run_mode == NODE_MODE)
      digitalWrite(LED_PIN, LOW);
  }
}
