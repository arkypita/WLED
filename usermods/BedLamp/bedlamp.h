#pragma once

#include "wled.h"
#include <esp_now.h>
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
#define WLED_LONG_REPEATED_ACTION    50 // how often a repeated action (e.g. dimming) is fired 

#define BRI_READING 128
#define BRI_COURTESY 20
#define BRI_FULL 128
#define BRI_OFF 0

#define BRI_MIN 16
#define BRI_MAX 255

#define ESP32_LED_BUILTIN 2


class BedLampUsermod : public Usermod {
	private:
		bool initialized = false;
		bool enabled = false; //usermod is active
		uint32_t current_status = STA_WLED_HANDLE;
		unsigned long lastLoopTime = 0; // used to make timed action in loop
		unsigned long lastFadeTime = 0; // used to restore default fade direction after x sec from last effective fade, and to blinkblink
		unsigned long lastStatusChangeTime = 0; //used to filter with a little delay to switch between WLED Handled and BEDLAMP Handled
		unsigned long lastButtonInteractionTime = 0;
		uint8_t targetFadeBri = 0;
		bool dimming_now = false;

		uint8_t fade_up = 1;
		uint8_t do_blink_blink = 0;
		

		// string that are used multiple time (this will save some flash memory)
		static const char _name[];
		static const char _enabled[];

		uint32_t striplenght;

		struct Target4Net
		{
			bool IsValid;
			bool IsMoving;

			short CurrentX;
			short CurrentY;
			short CurrentSpeed;
			float CurrentAngle;
			float CurrentDirection;

			float AverageX;
			float AverageY;
			float AverageSpeed;
			float AverageAngle;
			float AverageDirection;

			float Confidence;
		};

		struct Target4Net Targets[3];

		static BedLampUsermod* instance; // Static instance pointer for ESP-NOW callbacks
		// Static function as a trampoline
		static void onDataRecvStatic(const uint8_t *mac, const uint8_t *incomingData, int len) {
			if (instance) {
				instance->onDataRecv(mac, incomingData, len);
			}
		}

		// Non-static member function for handling received data
		void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) 
		{
			// Serial.print("Received data from: ");
			// for (int i = 0; i < 6; i++) {
			// 	Serial.printf("%02X", mac[i]);
			// 	if (i < 5) Serial.print(":");
			// }
			
			// Serial.print(" | Len: ");
			// Serial.print(len);

			// Serial.print(" | millis: ");
			// Serial.print(millis());

			// Serial.println();

			if (len == sizeof(Targets))
			{
				memcpy(Targets, incomingData, sizeof(Targets));
				HandleTargets();
			}
		}

	public:

		void setup() 
		{
		   	instance = this; // Set the static instance pointer

			pinMode(ESP32_LED_BUILTIN, OUTPUT);
			digitalWrite(ESP32_LED_BUILTIN, HIGH); //turn off the onboard LED (active low)
			striplenght = strip.getLengthTotal();

			Serial.printf("\n\n\n");
			Serial.printf("BedLamp Usermod UP and RUNNING!\n\n");
			initialized = true;
		}

		void connected() 
		{
			Serial.println("Connected to WiFi");

			// Init ESP-NOW
			if (esp_now_init() == ESP_OK) 
			{
				// Once ESPNow is successfully Init, we will register for recv CB to
				// get recv packer info
				esp_now_register_recv_cb(BedLampUsermod::onDataRecvStatic);
				Serial.println("ESP-NOW Init Success");
			}
			else
			{Serial.println("Error initializing ESP-NOW");}
		}

		void loop() 
		{
			// if usermod is not initialized, or disabled or called during strip updating just exit
			// NOTE: on very long strips strip.isUpdating() may always return true so update accordingly
			if (!initialized || !enabled) return;
			if (strip.isUpdating()) return;
	
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

			if (current_status != STA_WLED_HANDLE && strip.getBrightness() == 0 && (millis() - lastButtonInteractionTime > 2000)) //when turned off by this usermod or by WLED, detach control
				DetachControl();
		}

		void onStateChange(uint8_t mode) 
		{
			if (!initialized || !enabled) return;
			
			// if (current_status != STA_WLED_HANDLE && mode == CALL_MODE_DIRECT_CHANGE && (millis() - lastButtonInteractionTime) > 2000) //when handled by BedLamp and we detect some changes from WLED, detach control
			//   DetachControl();
			// if (current_status != STA_WLED_HANDLE && mode == CALL_MODE_BUTTON_PRESET && (millis() - lastButtonInteractionTime) > 2000) //when handled by BedLamp and we detect a preset change (button, IR, macro) -> detach control
			//   DetachControl();
			// if (current_status != STA_WLED_HANDLE && mode == CALL_MODE_BUTTON && (millis() - lastButtonInteractionTime) > 2000) //when handled by BedLamp and we detect a button action -> detach control
			//   DetachControl();
		}

		void DetachControl()
		{
			SetStatus(STA_WLED_HANDLE, 0, 0); //set as controlled by WLED
			fade_up = 1; //restore default fade direction
		}

		/*
		* handleOverlayDraw() is called just before every show() (LED strip update frame) after effects have set the colors.
		* Use this to blank out some LEDs or set them to a different color regardless of the set effect mode.
		* Commonly used for custom clocks (Cronixie, 7 segment)
		*/
		void handleOverlayDraw() 
		{
			if (!initialized || !enabled) return;
			if (current_status == STA_WLED_HANDLE) return;

			bool fl = TestBit(current_status, STA_FULL_LAMP);
			bool rd = TestBit(current_status, STA_READING_DX);
			bool rs = TestBit(current_status, STA_READING_SX);
			bool cd = TestBit(current_status, STA_COURTESY_DX);
			bool cs = TestBit(current_status, STA_COURTESY_SX);

			if (do_blink_blink)
			{
				do_blink_blink = 1 + ((millis() - lastFadeTime) / 100); // compute the blink-blink state
				if (do_blink_blink >= 7) do_blink_blink = 0; // end of blink-blink
			}

			if (do_blink_blink % 2 == 1)
			{
				if (targetFadeBri == BRI_MIN)
					strip.setBrightness(BRI_MIN + 16);
				else
					strip.setBrightness(BRI_MAX - 32);
			}

			int bripix = (int) (bri * striplenght / 255);
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

				if (dimming_now && abs (i - bripix) < 6)
						color = BLACK;
				if (dimming_now && abs (i - bripix) < 2)
						color = RED;

				// if (do_blink_blink % 2 == 1)
				//   color = BLACK;

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
			if (!initialized || !enabled) return false;
			if (buttonType[b] == BTN_TYPE_NONE
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
					lastButtonInteractionTime = now;
					nightlightStartTime = now; // reset nightlight timer at each interaction with the buttons

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
				// Courtesy Light handling (disabled)

				// Serial.printf("Rising Edge %d\n", b);
				// uint32_t new_status = current_status;
				// ClearBit(new_status, STA_TURNING_OFF);
				// if (b == BTN_PIR_DX) SetBit(new_status, STA_COURTESY_DX);
				// if (b == BTN_PIR_SX) SetBit(new_status, STA_COURTESY_SX);
				// SetStatus(new_status, BRI_COURTESY, 1);
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
					bri = BRI_MIN; //set the starting brightness for the dimming
					OnFadeBegin(); //set fast transition delay and starting brightness (using the one set above)
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
			}
		}

		void OnFadeBegin()
		{
			dimming_now = true;
			targetFadeBri = bri; //the brightness we are starting from
			fadeTransition = modeBlending = false; //disable WLED transition
			transitionActive = false; // transiction is not active now
		}

		void OnFadeEnd()
		{
			dimming_now = false;
			fadeTransition = modeBlending = true; //enable WLED transition

			if(targetFadeBri == BRI_MIN || targetFadeBri == BRI_MAX) //we are at the end of the fade (min or max brightness)
				fade_up = !fade_up;  //invert direction
		}

		void FadeBrightness()
		{
			byte old = targetFadeBri;

			if (fade_up) //up
				targetFadeBri = MIN(BRI_MAX, (targetFadeBri + 4) / 4 * 4 );
			else if (!fade_up) //down
				targetFadeBri = MAX(BRI_MIN, (targetFadeBri - 4) / 4 * 4);

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

		// JSON Object Format

		// {
		//   "BedLamp": 
		//   {
		//     "LEFT": {"InBed":true, "Standing":false, "GoingOut":false, "GoingIn":false} ,
		//     "RIGHT": {"InBed":false, "Standing":true, "GoingOut":true, "GoingIn":false} 
		//   }
		// }


		void readFromJsonState(JsonObject& root) 
		{
			if (!initialized || !enabled) return; 

			// JsonObject BL = root[FPSTR(_name)]; // BedLamp object
			// if (!BL.isNull())  // BedLamp object detected
			// {
			// 	JsonObject LT = BL["LEFT"]; // Left Target Object
			// 	if (!LT.isNull())  // Left Target object changes detected
			// 		UpdateTarget(&LeftTarget, TargetFromJson(LT));

			// 	JsonObject RT = BL["RIGHT"]; // Right Target Object
			// 	if (!RT.isNull())  // Right Target object detected
			// 		UpdateTarget(&RightTarget, TargetFromJson(RT));
			// }
		}

		void HandleTargets()
		{
			
		}

		
};

BedLampUsermod* BedLampUsermod::instance = nullptr;

// add more strings here to reduce flash memory usage
const char BedLampUsermod::_name[]    PROGMEM = "BedLamp";
const char BedLampUsermod::_enabled[] PROGMEM = "enabled";