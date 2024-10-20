#pragma once

#include "wled.h"
#include "ir_codes.h"

#define BTN_PIR_DX    0
#define BTN_TOUCH_DX  1
#define BTN_PIR_SX    2
#define BTN_TOUCH_SX  3

#define STA_LAMP_OFF    0x00
#define STA_COURTESY_DX 0x01
#define STA_COURTESY_SX 0x02
#define STA_READING_DX  0x04
#define STA_READING_SX  0x08
#define STA_FULL_LAMP   0x10
#define STA_TURNING_OFF 0x20

#define WLED_DEBOUNCE_THRESHOLD      50 // only consider button input of at least 50ms as valid (debouncing)
#define WLED_LONG_PRESS            1000 // long press if button is released after held for at least 1000ms
#define WLED_DOUBLE_PRESS           350 // double press if another press within 350ms after a short press
#define WLED_LONG_REPEATED_ACTION   100 // how often a repeated action (e.g. dimming) is fired 

#define WLED_DEFAULT_BRIGHTNESS 128
#define ESP32_LED_BUILTIN 2

class BedLampUsermod : public Usermod {
  private:
    bool enabled = false; //usermod is active
    bool incontrol = false; //lamp controlled by this usermod now
    uint32_t current_status = STA_LAMP_OFF;
    unsigned long lastLoopTime = 0;
    unsigned long lastFadeTime = 0;
    unsigned long lastCommandTime = 0;
    unsigned long originalTransitionDelay = 0;
    uint8_t fade_up = 0;

    // string that are used multiple time (this will save some flash memory)
    static const char _name[];
    static const char _enabled[];

    uint32_t striplenght;

  public:
    void setup() 
    {
      pinMode(ESP32_LED_BUILTIN, OUTPUT);
      digitalWrite(ESP32_LED_BUILTIN, HIGH); //turn off the onboard LED (active low)
      striplenght = strip.getLengthTotal();
    }

    void loop() 
    {
      // if usermod is disabled or called during strip updating just exit
      // NOTE: on very long strips strip.isUpdating() may always return true so update accordingly
      if (!enabled || strip.isUpdating()) return;
  
      if (millis() - lastLoopTime > 10000) 
      {
        updateLocalTime();
        lastLoopTime = millis();
      }

      if (millis() - lastFadeTime > 30000) //restore default fade direction after 30s
        fade_up = 0;

      if (originalTransitionDelay != 0 && (millis() - lastFadeTime) >= 2 * WLED_LONG_REPEATED_ACTION) //restore default transition delay after 1s
      {
        transitionDelay = originalTransitionDelay;
        originalTransitionDelay = 0;
        strip.setTransition(transitionDelay);
      }

      if (incontrol && strip.getBrightness() == 0 && (millis() - lastCommandTime > 1000)) //when turned off by this usermod or by WLED, detach control
      {
         Serial.println("Now Handled by WLED");
         current_status = STA_LAMP_OFF;
         incontrol = false;
      }
    }

    /*
     * handleOverlayDraw() is called just before every show() (LED strip update frame) after effects have set the colors.
     * Use this to blank out some LEDs or set them to a different color regardless of the set effect mode.
     * Commonly used for custom clocks (Cronixie, 7 segment)
     */
    void handleOverlayDraw() 
    {
      if (!enabled) return;
      if (!incontrol) return;

      bool fl = TestBit(current_status, STA_FULL_LAMP);
      bool rd = TestBit(current_status, STA_READING_DX);
      bool rs = TestBit(current_status, STA_READING_SX);
      bool cd = TestBit(current_status, STA_COURTESY_DX);
      bool cs = TestBit(current_status, STA_COURTESY_SX);


      for (int i = 0; i < striplenght; i++)
      {
        uint32_t color = COLOR_WARMWHITE;

        if (i >= 0 && i < 4 && !(fl||rd||cd)) 
          color = BLACK;
        if (i >= 4 && i < 54 && !(fl||rd)) 
          color = BLACK;
        if (i >= 54 && i < 104 && !(fl||rs||cs)) 
          color = BLACK;
        if (i >= 104 && i < 108 && !(fl||rs)) 
          color = BLACK;

        strip.setPixelColor(i, color);
      }
    }

    // Enable/Disable the usermod
    inline void enable(bool enable) { enabled = enable; }

    // Get usermod enabled/disabled state
    inline bool isEnabled() { return enabled; }

    /**
     * handleButton() can be used to override default button behaviour.
     * Returning true will prevent button working in a default way.
     * Replicating button.cpp
     */
    bool handleButton(uint8_t b) {
      yield();
      // ignore certain button types as they may have other consequences
      if (!enabled
       || buttonType[b] == BTN_TYPE_NONE
       || buttonType[b] == BTN_TYPE_RESERVED
       || buttonType[b] == BTN_TYPE_PIR_SENSOR
       || buttonType[b] == BTN_TYPE_ANALOG
       || buttonType[b] == BTN_TYPE_ANALOG_INVERTED) {
        return false;
      }


      unsigned long now = millis();
      if (IsPirButton(b) || IsTouchButton(b)) // buttons handled by this usermod
      {
        // momentary button logic
        if (isButtonPressed(b))  // pressed
        {
          if (!buttonPressedBefore[b])
            OnRisingEdge(b);

          if (!buttonPressedBefore[b]) buttonPressedTime[b] = now;
          buttonPressedBefore[b] = true;

          if (now - buttonPressedTime[b] > WLED_LONG_PRESS)  //long press handling
          {
            if (!buttonLongPressed[b]) //first action
            {
              OnLongPress(b);
            }
            else //repeatable action
            { 
              OnLongPressRepeat(b);
              buttonPressedTime[b] = now - (WLED_LONG_PRESS - WLED_LONG_REPEATED_ACTION);
            }
            buttonLongPressed[b] = true;
          }

        }
        else if (!isButtonPressed(b) && buttonPressedBefore[b])  //released
        { 
          long dur = now - buttonPressedTime[b];

          // released after rising-edge short press action
          if (macroButton[b] && macroButton[b] == macroLongPress[b] && macroButton[b] == macroDoublePress[b]) {
            if (dur > WLED_DEBOUNCE_THRESHOLD) buttonPressedBefore[b] = false; // debounce, blocks button for 50 ms once it has been released
            return true;
          }

          if (dur < WLED_DEBOUNCE_THRESHOLD) {buttonPressedBefore[b] = false; return true;} // too short "press", debounce
          bool doublePress = buttonWaitTime[b]; //did we have a short press before?
          buttonWaitTime[b] = 0;

          if (!buttonLongPressed[b])  //short press
          {
            //NOTE: this interferes with double click handling in usermods so usermod needs to implement full button handling
            if (b != 1 && !macroDoublePress[b]) { //don't wait for double press on buttons without a default action if no double press macro set
              OnShortPress(b);
            } else { //double press if less than 350 ms between current press and previous short press release (buttonWaitTime!=0)
              if (doublePress) {
                OnDoublePress(b);
              } else {
                buttonWaitTime[b] = now;
              }
            }
          }
          buttonPressedBefore[b] = false;
          buttonLongPressed[b] = false;
        }

        //if 350ms elapsed since last short press release it is a short press
        if (buttonWaitTime[b] && now - buttonWaitTime[b] > WLED_DOUBLE_PRESS && !buttonPressedBefore[b]) {
          buttonWaitTime[b] = 0;
          OnShortPress(b);
        }

        return true; // button handled, don't process default actions
      }
      else
      {
        return false; //not handled by this usermod
      }
    }

    void OnRisingEdge(uint8_t b)
    {
      if (IsPirButton(b) && IsLampOffOrCourtesy() && IsSleepHours())
      {
        Serial.printf("Rising Edge %d\n", b);
        uint32_t new_status = current_status;
        if (b == BTN_PIR_DX) SetBit(new_status, STA_COURTESY_DX);
        if (b == BTN_PIR_SX) SetBit(new_status, STA_COURTESY_SX);
        SetStatus(new_status, 1);
      }
    }
    void OnShortPress(uint8_t b)
    {
      if (IsTouchButton(b))
      {
        Serial.printf("Click %d\n", b);

        if (!IsLampTotallyOff())
        {
          uint32_t new_status = current_status;
          SetBit(new_status, STA_TURNING_OFF);
          SetStatus(new_status, 0);
        }
        else
        {
          uint32_t new_status = current_status;
          if (b == BTN_TOUCH_DX) SetBit(new_status, STA_READING_DX);
          if (b == BTN_TOUCH_SX) SetBit(new_status, STA_READING_SX);
          SetStatus(new_status, 60);
        }
      }
    }
    void OnDoublePress(uint8_t b)
    {
      if (IsTouchButton(b))
      {
        Serial.printf("Double Click %d\n", b);
        SetStatus(STA_FULL_LAMP, 60);
      }
    }
    void OnLongPress(uint8_t b)
    {
      if (IsTouchButton(b))
      {
        Serial.printf("LongPress %d\n", b);
        fade_up = !fade_up; //invert direction at each change, starting with fade up
        if (IsLampTotallyOff())
          OnShortPress(b);
        else
          FadeBrightness();
      }
    }
    void OnLongPressRepeat(uint8_t b)
    {
      if (IsTouchButton(b))
      {
        Serial.printf("LongPress RPT %d\n", b);
        FadeBrightness();
      }
    }

    void FadeBrightness()
    {
      byte nbri = bri;

      if (fade_up) //up
        nbri = MIN(255, bri + 8);
      else if (!fade_up) //down
        nbri = MAX(10, bri - 8);

      if (nbri != bri)
      {
        lastFadeTime = millis();
        bri = nbri;
        if (originalTransitionDelay == 0)
        {
          originalTransitionDelay = transitionDelay;
          transitionDelay = WLED_LONG_REPEATED_ACTION;
          strip.setTransition(transitionDelay);
        }
        Serial.printf("Brightness: %d\n", bri);
        stateUpdated(CALL_MODE_DIRECT_CHANGE);
      }
    }

    bool IsTouchButton(uint8_t b)
    {
      return (b == BTN_TOUCH_DX || b == BTN_TOUCH_SX);
    }

    bool IsPirButton(uint8_t b)
    {
      return (b == BTN_PIR_DX || b == BTN_PIR_SX);
    }

    bool IsRisingEdge(uint8_t b)
    {
      bool oldp = buttonPressedBefore[b];
      bool curp = buttonPressedBefore[b] = isButtonPressed(b);

      return (curp && !oldp); // this is the rising edge
    }

    bool IsSleepHours()
    {
      uint8_t hr = hour(localTime);
      //uint8_t mi = minute(localTime);

      if (hr >= 22 && hr <= 24)
        return true;
      if (hr >= 0 && hr <= 7)
        return true;
      else
        return false;
    }

    bool IsLampTotallyOff()
    {
      return bri == 0;
    }
    bool IsLampOffOrCourtesy()
    {
      return IsLampTotallyOff() || current_status == STA_COURTESY_DX || current_status == STA_COURTESY_SX || current_status == (STA_COURTESY_DX | STA_COURTESY_SX);
    }

    void SetStatus(uint32_t new_status, uint32_t timeout)
    {
      if (new_status != current_status || !incontrol)
      {
        lastCommandTime = millis();

        if (!incontrol)
        {
          Serial.println("    > Now Handled by BedLamp");
          incontrol = true;
        }

        Serial.printf("    > New Status: %x\n", new_status);
        current_status = new_status;

        if (TestBit(current_status, STA_TURNING_OFF)) //abbiamo la richiesta di spegnimento
        {
          briLast = bri;
          bri = 0;
          nightlightActive = false;
          nightlightDelayMins = 0;
        }
        else //siamo accesi o in accensione
        {
          if (timeout > 0)
          {
            nightlightActive = true;
            nightlightDelayMins = timeout;
            nightlightStartTime = millis();
          }

          bri = WLED_DEFAULT_BRIGHTNESS; // turn on at default brightness
        }
        
        stateUpdated(CALL_MODE_DIRECT_CHANGE);
      }
    }

    bool TestBit(uint32_t value, uint8_t bit)
    { return (value & bit) != 0; }

    void SetBit(uint32_t &value, uint8_t bit)
    { value |= bit; }

    void ClearBit(uint32_t &value, uint8_t bit)
    { value &= ~bit; }

    void ToggleBit(uint32_t &value, uint8_t bit)
    { value ^= bit; }

    void SetBit(uint32_t &value, uint8_t bit, bool val)
    {
      if (val) SetBit(value, bit);
      else ClearBit(value, bit);
    }


    /**
       * addToConfig() (called from set.cpp) stores persistent properties to cfg.json
       */
    void addToConfig(JsonObject &root)
    {
      // we add JSON object.
      JsonObject top = root.createNestedObject(FPSTR(_name)); // usermodname
      top[FPSTR(_enabled)] = enabled;

      DEBUG_PRINT(FPSTR(_name));
      DEBUG_PRINTLN(F(" config saved."));
    }

    /**
    * readFromConfig() is called before setup() to populate properties from values stored in cfg.json
    */
    bool readFromConfig(JsonObject &root)
    {
      // we look for JSON object.
      JsonObject top = root[FPSTR(_name)];
      if (top.isNull()) {
        DEBUG_PRINT(FPSTR(_name));
        DEBUG_PRINTLN(F(": No config found. (Using defaults.)"));
        return false;
      }

      enabled         = top[FPSTR(_enabled)];

      DEBUG_PRINT(FPSTR(_name));
      DEBUG_PRINTLN(F(" config (re)loaded."));

      // use "return !top["newestParameter"].isNull();" when updating Usermod with new features
      return true;
    }

};


// add more strings here to reduce flash memory usage
const char BedLampUsermod::_name[]    PROGMEM = "BedLamp";
const char BedLampUsermod::_enabled[] PROGMEM = "enabled";