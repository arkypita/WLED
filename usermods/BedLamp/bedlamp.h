#pragma once

#include "wled.h"
#include "ir_codes.h"

#define BTN_PIR_DX    0
#define BTN_TOUCH_DX  1
#define BTN_PIR_SX    2
#define BTN_TOUCH_SX  3

#define STA_WLED_HANDLE 0x00
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

#define BRI_READING 128
#define BRI_COURTESY 32
#define BRI_FULL 128
#define BRI_OFF 0

#define BRI_MIN 8
#define BRI_MAX 255

#define ESP32_LED_BUILTIN 2

class BedLampUsermod : public Usermod {
  private:
    bool enabled = false; //usermod is active
    uint32_t current_status = STA_WLED_HANDLE;
    unsigned long lastLoopTime = 0; // used to make timed action in loop
    unsigned long lastFadeTime = 0; // used to restore default fade direction after x sec from last effective fade, and to blinkblink
    unsigned long lastStatusChangeTime = 0; //used to filter with a little delay to switch between WLED Handled and BEDLAMP Handled

    unsigned long originalTransitionDelay = 0;  //used when changing brightness
    uint8_t targetFadeBri = 0;

    uint8_t fade_up = 1;
    uint8_t do_blink_blink = 0;
    

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
  
      if (millis() - lastLoopTime > 1000) 
      {
        updateLocalTime();
        lastLoopTime = millis();
        // uint16_t rawlight = analogRead(32); 
        // Serial.printf("Light: %d\n", rawlight);
      }

      if (fade_up && millis() - lastFadeTime > 10000) 
      {
        fade_up = 1; //restore default fade direction after 10s
      }

      if (current_status != STA_WLED_HANDLE && strip.getBrightness() == 0 && (millis() - lastStatusChangeTime > 1000)) //when turned off by this usermod or by WLED, detach control
      {
        SetStatus(STA_WLED_HANDLE, 0, 0); //set as controlled by WLED
        fade_up = 1; //restore default fade direction when turned off
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
      if (current_status == STA_WLED_HANDLE) return;

      bool fl = TestBit(current_status, STA_FULL_LAMP);
      bool rd = TestBit(current_status, STA_READING_DX);
      bool rs = TestBit(current_status, STA_READING_SX);
      bool cd = TestBit(current_status, STA_COURTESY_DX);
      bool cs = TestBit(current_status, STA_COURTESY_SX);

      if (do_blink_blink)
      {
        do_blink_blink = 1 + ((millis() - lastFadeTime) / 50); // compute the blink-blink state
        if (do_blink_blink >= 5) do_blink_blink = 0; // end of blink-blink
      } 

      for (int i = 0; i < striplenght; i++)
      {
        uint32_t color = COLOR_WARMWHITE;

        if (i >= 0 && i < 2 && !(fl||rd||cd)) 
          color = BLACK;
        if (i >= 2 && i < 54 && !(fl||rd)) 
          color = BLACK;
        if (i >= 54 && i < 106 && !(fl||rs)) 
          color = BLACK;
        if (i >= 106 && i < 108 && !(fl||rs||cs)) 
          color = BLACK;
        if (do_blink_blink % 2 == 1)
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
    bool handleButton(uint8_t b)
    {
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
          nightlightStartTime = millis(); // reset nightlight timer at each interaction with the buttons

          if (!buttonPressedBefore[b])
            OnRisingEdge(b);

          if (!buttonPressedBefore[b]) buttonPressedTime[b] = now;
          buttonPressedBefore[b] = true;

          if (now - buttonPressedTime[b] > WLED_LONG_PRESS)  //long press handling
          {
            if (!buttonLongPressed[b]) //first action
            {
              OnLongPressBegin(b);
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
          if (false /*macroButton[b] && macroButton[b] == macroLongPress[b] && macroButton[b] == macroDoublePress[b]*/) {
            if (dur > WLED_DEBOUNCE_THRESHOLD) buttonPressedBefore[b] = false; // debounce, blocks button for 50 ms once it has been released
            return true;
          }

          if (dur < WLED_DEBOUNCE_THRESHOLD) {buttonPressedBefore[b] = false; return true;} // too short "press", debounce
          bool doublePress = buttonWaitTime[b]; //did we have a short press before?
          buttonWaitTime[b] = 0;

          if (!buttonLongPressed[b])  //short press
          {
            //NOTE: this interferes with double click handling in usermods so usermod needs to implement full button handling
            if (b != 1 && false/*!macroDoublePress[b]*/) { //don't wait for double press on buttons without a default action if no double press macro set
              OnShortPress(b);
            } else { //double press if less than 350 ms between current press and previous short press release (buttonWaitTime!=0)
              if (doublePress) {
                OnDoublePress(b);
              } else {
                buttonWaitTime[b] = now;
              }
            }
          }
          else
          {
            OnLongPressEnd(b);              
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
      if (IsPirButton(b) && IsLampOffOrCourtesy() && IsSleepHours() && IsDark())
      {
        Serial.printf("Rising Edge %d\n", b);
        uint32_t new_status = current_status;
        ClearBit(new_status, STA_TURNING_OFF);
        if (b == BTN_PIR_DX) SetBit(new_status, STA_COURTESY_DX);
        if (b == BTN_PIR_SX) SetBit(new_status, STA_COURTESY_SX);
        SetStatus(new_status, BRI_COURTESY, 1);
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
          SetStatus(new_status, BRI_OFF, 0);
        }
        else
        {
          uint32_t new_status = current_status;
          ClearBit(new_status, STA_TURNING_OFF);
          if (b == BTN_TOUCH_DX) SetBit(new_status, STA_READING_DX);
          if (b == BTN_TOUCH_SX) SetBit(new_status, STA_READING_SX);
          SetStatus(new_status, BRI_READING, 60);
        }
      }
    }
    void OnDoublePress(uint8_t b)
    {
      if (IsTouchButton(b))
      {
        Serial.printf("Double Click %d\n", b);
        SetStatus(STA_FULL_LAMP, BRI_FULL, 60);
      }
    }
    void OnLongPressBegin(uint8_t b)
    {
      if (IsTouchButton(b))
      {
        Serial.printf("LongPress BGN %d\n", b);
        
        if (IsLampTotallyOff())
        {
          OnShortPress(b); //if we are off, turn on doing the same action as a click
          OnFadeBegin(); //set fast transition delay and starting brightness (using the one set by OnShortPress)
        }
        else
        {
          OnFadeBegin(); //set fast transition delay and starting brightness (using the current one)
          FadeBrightness(); //if we are already on, start dimming
        }
      }
    }
    void OnLongPressRepeat(uint8_t b)
    {
      if (IsTouchButton(b))
      {
        Serial.printf("LongPress RPT %d\n", b);

        FadeBrightness(); //keep dimming
      }
    }
    void OnLongPressEnd(uint8_t b)
    {
      if (IsTouchButton(b))
      {
        Serial.printf("LongPress END %d\n", b);

        OnFadeEnd(); //restore original transition delay and nightlight status
        fade_up = !fade_up;  //invert direction
      }
    }

    void OnFadeBegin()
    {
      targetFadeBri = bri; //the brightness we are starting from

      // set to a fast transition delay to have a smooth dimming effect
      originalTransitionDelay = transitionDelay;
      transitionDelay = WLED_LONG_REPEATED_ACTION-10;
      strip.setTransition(transitionDelay);
    }

    void OnFadeEnd()
    {
      // restore original transition delay
      transitionDelay = originalTransitionDelay;
      originalTransitionDelay = 0;
      strip.setTransition(transitionDelay);
    }

    void FadeBrightness()
    {
      byte old = targetFadeBri;

      if (fade_up) //up
        targetFadeBri = MIN(BRI_MAX, (targetFadeBri + 8) / 8 * 8 );
      else if (!fade_up) //down
        targetFadeBri = MAX(BRI_MIN, (targetFadeBri - 8) / 8 * 8);

      if (targetFadeBri != old)
      {
        lastFadeTime = millis();

        if (do_blink_blink == 0 && (targetFadeBri == BRI_MIN || targetFadeBri == BRI_MAX)) //siamo appena arrivati in fondo o in cima
          do_blink_blink = 1; //trigger the blink-blink

        bri = targetFadeBri;

        Serial.printf("Brightness change: %d -> %d\n", old, targetFadeBri);
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

    bool IsDark()
    { return true; }

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

    void SetStatus(uint32_t new_status, uint8_t brightness, uint32_t timeout)
    {
      //new_status = STA_COURTESY_DX;
      
      if (new_status != current_status)
      {
        if (new_status == STA_WLED_HANDLE)  
        {
          Serial.println("    > Now Handled by WLED");
        }
        else
        {
          Serial.println("    > Now Handled by BedLamp");

          lastStatusChangeTime = millis();

          if (TestBit(new_status, STA_TURNING_OFF)) //abbiamo la richiesta di spegnimento
          { briLast = bri; bri = 0; }
          else //siamo accesi o in accensione
          { bri = brightness; } // turn on at requested brightness

          nightlightActive = timeout > 0;
          nightlightDelayMins = timeout;
          
          stateUpdated(CALL_MODE_DIRECT_CHANGE);
        }
        
        PrintNewStatus(new_status);
        current_status = new_status;
      }
    }

    void PrintNewStatus(int new_status) 
    {
        String statusDescription = "    > New Status: ";
        bool first = true;
        
        if (TestBit(new_status, STA_COURTESY_DX)) {
            if (!first) statusDescription += ", ";
            statusDescription += "COURTESY_DX";
            first = false;
        }
        if (TestBit(new_status, STA_COURTESY_SX)) {
            if (!first) statusDescription += ", ";
            statusDescription += "COURTESY_SX";
            first = false;
        }
        if (TestBit(new_status, STA_READING_DX)) {
            if (!first) statusDescription += ", ";
            statusDescription += "READING_DX";
            first = false;
        }
        if (TestBit(new_status, STA_READING_SX)) {
            if (!first) statusDescription += ", ";
            statusDescription += "READING_SX";
            first = false;
        }
        if (TestBit(new_status, STA_FULL_LAMP)) {
            if (!first) statusDescription += ", ";
            statusDescription += "FULL_LAMP";
            first = false;
        }
        if (TestBit(new_status, STA_TURNING_OFF)) {
            if (!first) statusDescription += ", ";
            statusDescription += "TURNING_OFF";
            first = false;
        }
        
        if (statusDescription == "    > New Status: ") {
            statusDescription += "WLED_HANDLE";
        }
        
        Serial.println(statusDescription);
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