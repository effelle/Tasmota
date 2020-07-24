/*
  xdrv_91_twatch.ino

  Copyright (C) 2020  Christian Baars & Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef ESP32
#define USE_TTGO
#define LILYGO_WATCH_2020_V1
#define LILYGO_WATCH_LVGL                   //To use LVGL, you need to enable the macro LVGL

#ifdef USE_TTGO

#define XDRV_91           91
#include <LilyGoWatch.h>
#include <lvgl/lvgl.h>

TTGOClass *ttgo;
LV_IMG_DECLARE(tm_logo_name);
LV_IMG_DECLARE(tm_logo_T);
LV_IMG_DECLARE(tm_logo_O);
LV_IMG_DECLARE(tm_wifi_on);
LV_IMG_DECLARE(tm_wifi_off);
LV_IMG_DECLARE(tm_charge);
LV_IMG_DECLARE(tm_battery);

struct {
  lv_style_t style;
  lv_obj_t *date;
  lv_obj_t *time;
  lv_obj_t *version;
  lv_obj_t *pictPower;
  lv_obj_t *battLevel;
  lv_obj_t *pictWifi;
  lv_obj_t *logo_O;
  lv_obj_t *mboxWifi;
  struct{
    uint32_t isCharging:1;
    uint32_t batteryConnected:1;
    uint32_t isVBUSPlug:1;
    uint32_t lightSleep:1;
    uint32_t timeWasSynced:1;
  } state;
  uint32_t screenTimer = 200;
  bool irq;
} TTGO;

void TTGOBtnPressed(){
  static bool _bl_on = true;
  if(_bl_on){
    TTGOlightSleep();
    _bl_on=false;
  }
  else{
    TTGOwake();
    _bl_on=true;
  }
}

void TTGOwake(){
  ttgo->startLvglTick();
  ttgo->rtc->syncToSystem();
  lv_disp_trig_activity(NULL);
  ttgo->displayWakeup();
  ttgo->openBL();
  TTGO.state.lightSleep = 0;
}

void TTGOlightSleep(){
  ttgo->closeBL();
  ttgo->stopLvglTick();
  ttgo->bma->enableStepCountInterrupt(false);
  ttgo->displaySleep();
  TTGO.state.lightSleep = 1;
}

void TTGOInitPower(void){
  pinMode(AXP202_INT, INPUT_PULLUP);
  attachInterrupt(AXP202_INT, [] {
      TTGO.irq = true;
  }, FALLING);
  ttgo->power->enableIRQ(AXP202_PEK_SHORTPRESS_IRQ| AXP202_VBUS_REMOVED_IRQ | AXP202_VBUS_CONNECT_IRQ | AXP202_CHARGING_IRQ,
                    true);
  ttgo->power->clearIRQ();
  ttgo->power->adc1Enable(AXP202_VBUS_VOL_ADC1 | AXP202_VBUS_CUR_ADC1 | AXP202_BATT_CUR_ADC1 | AXP202_BATT_VOL_ADC1, true);
  ttgo->power->setPowerOutPut(AXP202_EXTEN, AXP202_OFF);
  ttgo->power->setPowerOutPut(AXP202_DCDC2, AXP202_OFF);
  ttgo->power->setPowerOutPut(AXP202_LDO3, AXP202_OFF);
  ttgo->power->setPowerOutPut(AXP202_LDO4, AXP202_OFF);
  TTGO.state.isVBUSPlug = ttgo->power->isVBUSPlug();
}

void TTGOInitGyro(void){
  ttgo->bma->begin();
  Acfg _cfg;
  _cfg.odr = BMA4_OUTPUT_DATA_RATE_100HZ;
  _cfg.range = BMA4_ACCEL_RANGE_2G;
  _cfg.bandwidth = BMA4_ACCEL_NORMAL_AVG4;
  _cfg.perf_mode = BMA4_CONTINUOUS_MODE;
  
  ttgo->bma->accelConfig(_cfg);
  // Warning : Need to use feature, you must first enable the accelerometer
  ttgo->bma->enableAccel();
  ttgo->bma->attachInterrupt();
  pinMode(BMA423_INT1, INPUT);
  attachInterrupt(BMA423_INT1, [] {
      TTGO.irq = 1;
  }, RISING); //It must be a rising edge

  ttgo->bma->enableFeature(BMA423_STEP_CNTR, true);
  ttgo->bma->enableFeature(BMA423_TILT, true);
  // Enable BMA423 isDoubleClick feature
  ttgo->bma->enableFeature(BMA423_WAKEUP, true);

  // Reset steps
  ttgo->bma->resetStepCounter();

  // Turn on feature interrupt
  ttgo->bma->enableStepCountInterrupt();
  ttgo->bma->enableTiltInterrupt();
  // It corresponds to isDoubleClick interrupt
  ttgo->bma->enableWakeupInterrupt();
}

void TTGOwifiTouchCB(lv_obj_t * obj, lv_event_t event);
void TTGOpowerCB(lv_obj_t * obj, lv_event_t event);
static void event_handler(lv_obj_t * obj, lv_event_t e);

void TTGOwifiTouchCB(lv_obj_t * obj, lv_event_t event){
  if(event==LV_EVENT_SHORT_CLICKED){
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("Wifi Status: %d"),WifiState());
    static const char * btns[] ={"On", "Off", ""};
    static lv_style_t boxStyle;
    lv_style_copy(&boxStyle,&TTGO.style);
    lv_style_set_text_font(&boxStyle, LV_OBJ_PART_MAIN, LV_THEME_DEFAULT_FONT_SMALL);
    TTGO.mboxWifi = lv_msgbox_create(lv_scr_act(), NULL);
    lv_msgbox_set_text(TTGO.mboxWifi, WiFi.localIP().toString().c_str());
    lv_msgbox_add_btns(TTGO.mboxWifi, btns);
    lv_obj_add_style(TTGO.mboxWifi,LV_OBJ_PART_MAIN, &boxStyle);
    lv_obj_set_width(TTGO.mboxWifi, 220);
    lv_obj_set_event_cb(TTGO.mboxWifi, event_handler);
    lv_obj_align(TTGO.mboxWifi, NULL, LV_ALIGN_CENTER, 0, 0);
  }
}
static void event_handler(lv_obj_t * obj, lv_event_t e)
{
  if(e == LV_EVENT_CLICKED) {
    int _index = lv_msgbox_get_active_btn(obj);
    switch(_index){
      case 0:
        AddLog_P2(LOG_LEVEL_DEBUG, PSTR("Wifi will start"));
        Settings.flag4.network_wifi=1;
        WifiConnect();
        break;
      case 1:
        AddLog_P2(LOG_LEVEL_DEBUG, PSTR("Wifi will shutdown"));
        Settings.flag4.network_wifi=0;
        WifiShutdown(false);
        WifiSetMode(WIFI_OFF);
        break;
    }
    lv_obj_del(TTGO.mboxWifi);
  }
}


void TTGOpowerCB(lv_obj_t * obj, lv_event_t event){
  if(event==LV_EVENT_SHORT_CLICKED){
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("Power Click"));
  }
}

void TTGOInitWatchFace(void){
    ttgo->lvgl_begin();
    // Turn on the backlight
    ttgo->openBL();
    //Lower the brightness
    ttgo->bl->adjust(20);


    // style
    lv_style_init(&TTGO.style);
    lv_style_set_text_color(&TTGO.style, LV_STATE_DEFAULT, LV_COLOR_SILVER);
    lv_style_set_bg_color(&TTGO.style, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_style_set_border_color(&TTGO.style, LV_STATE_DEFAULT, LV_COLOR_BLACK);

    static lv_style_t styleSmall;
    lv_style_copy(&styleSmall,&TTGO.style);
    // lv_style_init(&styleSmall);
    lv_style_set_text_font(&styleSmall, LV_OBJ_PART_MAIN, LV_THEME_DEFAULT_FONT_SMALL);

    //background
    lv_obj_t * mainScreen = lv_scr_act();
    lv_obj_add_style(mainScreen,LV_OBJ_PART_MAIN, &TTGO.style);

    lv_obj_t *logo_name = lv_img_create(mainScreen, NULL);
    lv_img_set_src(logo_name, &tm_logo_name);
    lv_obj_align(logo_name, mainScreen, LV_ALIGN_CENTER, 0, 0);

    TTGO.logo_O = lv_img_create(mainScreen, NULL);
    lv_img_set_src(TTGO.logo_O, &tm_logo_O);
    lv_obj_align(TTGO.logo_O, logo_name, LV_ALIGN_OUT_TOP_MID, 0, -10);
    
    lv_obj_t *logo_T = lv_img_create(mainScreen, NULL);
    lv_img_set_src(logo_T, &tm_logo_T);
    lv_obj_align(logo_T, logo_name, LV_ALIGN_OUT_TOP_MID, 0, -45);

    // top items
    lv_obj_t * topLeft = lv_obj_create(mainScreen, mainScreen);
    lv_obj_set_size(topLeft, 60, 30);
    lv_obj_t * topRight = lv_obj_create(mainScreen, topLeft);
    lv_obj_set_pos(topRight, 180, 0);

    // callbacks
    lv_obj_set_event_cb(topLeft,TTGOpowerCB);
    lv_obj_set_event_cb(topRight,TTGOwifiTouchCB);


    TTGO.pictPower = lv_img_create(topLeft, NULL);
    lv_obj_align(TTGO.pictPower, NULL, LV_ALIGN_IN_TOP_LEFT, 0, 0);
    lv_img_set_src(TTGO.pictPower, &tm_charge);

    TTGO.pictWifi = lv_img_create(topRight, NULL);
    lv_obj_align(TTGO.pictWifi, NULL, LV_ALIGN_IN_TOP_LEFT, 40, 0);
    lv_img_set_src(TTGO.pictWifi, &tm_wifi_on);

    TTGO.battLevel = lv_label_create(topLeft, nullptr);
    lv_obj_add_style(TTGO.battLevel, LV_OBJ_PART_MAIN, &styleSmall);
    lv_obj_align(TTGO.battLevel,TTGO.pictPower, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    // date and time
    TTGO.date = lv_label_create(mainScreen, TTGO.battLevel);
    lv_obj_align(TTGO.date, NULL, LV_ALIGN_CENTER, -20, 35);

    TTGO.time = lv_label_create(mainScreen, nullptr);
    lv_obj_add_style(TTGO.time, LV_OBJ_PART_MAIN, &TTGO.style);
    lv_label_set_text(TTGO.time, " ");
    lv_obj_align(TTGO.time, TTGO.date, LV_ALIGN_OUT_BOTTOM_MID, -75, 5);

    // footer
    TTGO.version = lv_label_create(mainScreen, TTGO.battLevel);
    lv_label_set_text_fmt(TTGO.version, "%s by Theo Arends", my_version);
    lv_obj_align(TTGO.version, NULL, LV_ALIGN_IN_BOTTOM_RIGHT, 0,0);


    // task section
    lv_task_create([](lv_task_t *t) {
        RTC_Date _time = ttgo->rtc->getDateTime();
        lv_label_set_text_fmt(TTGO.time, "%02u:%02u:%02u",_time.hour,_time.minute, _time.second);
        lv_img_set_angle(TTGO.logo_O, ((_time.second)*60)+1800);
    }, 500, LV_TASK_PRIO_HIGH, nullptr);

    lv_task_create([](lv_task_t *t1) {
      TIME_T _tm;
      BreakTime(Rtc.local_time,_tm) ;
      lv_label_set_text_fmt(TTGO.date, "%u. %s %u", _tm.day_of_month,_tm.name_of_month,_tm.year+1970);

      if (TTGO.state.isVBUSPlug){
        lv_img_set_src(TTGO.pictPower, &tm_charge);
      }
      else{
        lv_img_set_src(TTGO.pictPower, &tm_battery);
      }
      lv_label_set_text_fmt(TTGO.battLevel, "%u%%",ttgo->power->getBattPercentage());
      if(WifiState()==-1){
        lv_img_set_src(TTGO.pictWifi, &tm_wifi_off);
      }
      else{
        lv_img_set_src(TTGO.pictWifi, &tm_wifi_on);
      }
    }, 1000, LV_TASK_PRIO_LOW, nullptr);
}

void TTGOInit(void){
      //Get watch instance and init
    ttgo = TTGOClass::getWatch();
    ttgo->begin();

    ttgo->rtc->check();

    TTGOInitPower();
    TTGOInitWatchFace();
    TTGOInitGyro();
}

void TTGOirqscheduler(){
  ttgo->power->readIRQ();
  if (ttgo->power->isVbusPlugInIRQ()) {
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("Power Plug In"));
    TTGO.state.isVBUSPlug = 1;
  }
  if (ttgo->power->isVbusRemoveIRQ()) {
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("Power Remove"));
    TTGO.state.isVBUSPlug = 0;
  }
  if (ttgo->power->isPEKShortPressIRQ()) {
    TTGOBtnPressed();
  }
  ttgo->power->clearIRQ();

  while (!ttgo->bma->readInterrupt());
  if (ttgo->bma->isStepCounter()) {
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("Step"));
  }
  if (ttgo->bma->isTilt()) {
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("Tilt"));
  }
  if (ttgo->bma->isDoubleClick()) {
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("Double Click"));
    if(TTGO.state.lightSleep) TTGOwake();
  }
}

void TTGOLoop(void){
  static uint32_t _screenTimer = 400; // 20 seconds
  static uint32_t _batteryTimer = 30; // 10 * 30 = 300  seconds
  if(TTGO.irq){
    TTGOirqscheduler();
    TTGO.irq=false;
  }

  lv_task_handler();
  if(_batteryTimer==0){
    // TODO: if(too low) deep sleep
  }

  if(_screenTimer==0){
    _screenTimer = 400;
    _batteryTimer--;
    if(!TTGO.state.lightSleep) TTGOlightSleep();
  }
  _screenTimer--;

  if(!TTGO.state.timeWasSynced){
    if(!bNetIsTimeSync){
      AddLog_P2(LOG_LEVEL_DEBUG, PSTR("set RTC from NTP"));
      TTGO.state.timeWasSynced=1;
      TIME_T _tm;
      BreakTime(Rtc.local_time,_tm) ;
      ttgo->rtc->setDateTime(_tm.year+1970,_tm.month, _tm.day_of_month, _tm.hour, _tm.minute, _tm.second);
    }
  }
}

void TTGOShow(bool json){
  if (json) {
    if (TTGO.state.isVBUSPlug) ResponseAppend_P(PSTR(",\"Bus\":{\"Voltage\":%.02f mV,\"Current\":%.02f mA}"),ttgo->power->getVbusVoltage(),ttgo->power->getVbusCurrent());
    if (ttgo->power->isBatteryConnect()) ResponseAppend_P(PSTR(",\"Bat\":{\"Voltage\":%.02f mV}"),ttgo->power->getBattVoltage());
    if (ttgo->power->isChargeing()){
      ResponseAppend_P(PSTR(",\"Bat\":{\"Current\":%.02f mA}"),ttgo->power->getVbusCurrent());
    }
    else{
      ResponseAppend_P(PSTR(",\"Bat\":{\"DischargeCurrent\":%.02f mA,\"Level\":%u}"),ttgo->power->getBattDischargeCurrent(),ttgo->power->getBattPercentage());
    }
    Accel _acc;
    if(ttgo->bma->getAccel(_acc))ResponseAppend_P(PSTR(",\"Acc\":{\"X\":%d,\"Y\":%d,\"Z\":%d"),_acc.x,_acc.y,_acc.z);

#ifdef USE_WEBSERVER
  } else {

#endif  // USE_WEBSERVER
  }
}



/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv91(uint8_t function) {
  bool result = false;

  switch (function) {
    case FUNC_INIT:
      TTGOInit();
      break;
    case FUNC_EVERY_50_MSECOND:
      TTGOLoop();
      break;
    case FUNC_JSON_APPEND:
      TTGOShow(true);
      break;
  }
  return result;
}

#endif  // USE_TTGO
#endif  // ESP32
