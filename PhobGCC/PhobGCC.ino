//This software uses bits of code from GoodDoge's Dogebawx project, which was the initial starting point: https://github.com/DogeSSBM/DogeBawx

#include <math.h>
#include <ADC.h>
#include <VREF.h>

//Uncomment the appropriate #include line for your hardware by deleting the two slashes at the beginning of the line.
//#include "teensy/Phob1_0Teensy3_2.h"          // For PhobGCC board 1.0 with Teensy 3.2
//#include "teensy/Phob1_0Teensy3_2DiodeShort.h"// For PhobGCC board 1.0 with Teensy 3.2 and the diode shorted
//#include "teensy/Phob1_1Teensy3_2.h"          // For PhobGCC board 1.1 with Teensy 3.2
//#include "teensy/Phob1_1Teensy3_2DiodeShort.h"// For PhobGCC board 1.1 with Teensy 3.2 and the diode shorted
//#include "teensy/Phob1_1Teensy4_0.h"          // For PhobGCC board 1.1 with Teensy 4.0
//#include "teensy/Phob1_1Teensy4_0DiodeShort.h"// For PhobGCC board 1.1 with Teensy 4.0 and the diode shorted
#include "teensy/Phob1_2Teensy4_0.h"          // For PhobGCC board 1.2.x with Teensy 4.0

extern "C" uint32_t set_arm_clock(uint32_t frequency);

float _dT;

void setup() {
    serialSetup();
	Serial.print("Software version 0.");
	Serial.println(SW_VERSION);
#ifdef BUILD_DEV
	Serial.println("This is not a stable version");
#endif

#ifdef TEENSY4_0
	//Force-underclock Teensy 4 to 150 MHz to lower power draw.
	//set_arm_clock(150'000'000);
	//for some reason, 150 MHz doesn't work with the new comms code. 300 seems to be enough.
	set_arm_clock(300'000'000);
#endif //TEENSY4_0

	const int numberOfNaN = readEEPROM(_controls, _gains, _normGains, _aStickParams, _cStickParams);
	Serial.print("Number of NaN in EEPROM: ");
	Serial.println(numberOfNaN);
	if(numberOfNaN > 3){//by default it seems 4 end up NaN on Teensy 4
		resetDefaults(HARD, _controls, _gains, _normGains, _aStickParams, _cStickParams);//do reset sticks
		readEEPROM(_controls, _gains, _normGains, _aStickParams, _cStickParams);
	}

	//set some of the unused values in the message response
	_btn.errS = 0;
	_btn.errL = 0;
	_btn.orig = 0;
	_btn.high = 1;

    setPinModes();

	_pinList.pinLa = _pinLa;
	_pinList.pinRa = _pinRa;
	_pinList.pinL  = _pinL;
	_pinList.pinR  = _pinR;
	_pinList.pinAx = _pinAx;
	_pinList.pinAy = _pinAy;
	_pinList.pinCx = _pinCx;
	_pinList.pinCy = _pinCy;
	_pinList.pinRX = _pinRX;
	_pinList.pinTX = _pinTX;
	_pinList.pinDr = _pinDr;
	_pinList.pinDu = _pinDu;
	_pinList.pinDl = _pinDl;
	_pinList.pinDd = _pinDd;
	_pinList.pinX  = _pinX;
	_pinList.pinY  = _pinY;
	_pinList.pinA  = _pinA;
	_pinList.pinB  = _pinB;
	_pinList.pinZ  = _pinZ;
	_pinList.pinS  = _pinS;

    ADCSetup(adc, _ADCScale, _ADCScaleFactor);

	//measure the trigger values
	initializeButtons(_btn,_controls.lTrigInitial,_controls.rTrigInitial);
	//set the origin response before the sticks have been touched
	//it will never be changed again after this

	commsSetup(_btn);
}

void loop() {
	static bool running = false;

	//check if we should be reporting values yet
	if((_btn.B || _controls.autoInit) && !running){
		Serial.println("Starting to report values");
		running=true;
	}

	static int currentCalStep = -1;//-1 means not calibrating

	//Set up persistent storage for calibration
	static float tempCalPointsX[_noOfCalibrationPoints];
	static float tempCalPointsY[_noOfCalibrationPoints];
	static WhichStick whichStick = ASTICK;
	static NotchStatus notchStatus[_noOfNotches];
	static float notchAngles[_noOfNotches];
	static float measuredNotchAngles[_noOfNotches];

	//read the controllers buttons
	readButtons(_btn, _hardware, _controls, _gains, _normGains, currentCalStep, running, tempCalPointsX, tempCalPointsY, whichStick, notchStatus, notchAngles, measuredNotchAngles, _aStickParams, _cStickParams);

	//check to see if we are calibrating
	if(currentCalStep >= 0){
		if(whichStick == ASTICK){
			if(currentCalStep >= _noOfCalibrationPoints){//adjust notch angles
				adjustNotch(currentCalStep, _dT, true, measuredNotchAngles, notchAngles, notchStatus, _btn, _hardware);
				if(_hardware.Y || _hardware.X || (_btn.B)){//only run this if the notch was adjusted
					//clean full cal points again, feeding updated angles in
					float cleanedPointsX[_noOfNotches+1];
					float cleanedPointsY[_noOfNotches+1];
					float notchPointsX[_noOfNotches+1];
					float notchPointsY[_noOfNotches+1];
					cleanCalPoints(tempCalPointsX, tempCalPointsY, notchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
					//linearize again
					linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, _aStickParams);
					//notchCalibrate again to update the affine transform
					notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, _aStickParams);
				}
			}else{//just show desired stick position
				displayNotch(currentCalStep, true, _notchAngleDefaults, _btn);
			}
			readSticks(true,false, _btn, _pinList, _hardware, _controls, _normGains, _aStickParams, _cStickParams, _dT);
		}
		else{//WHICHSTICK == CSTICK
			if(currentCalStep >= _noOfCalibrationPoints){//adjust notch angles
				adjustNotch(currentCalStep, _dT, false, measuredNotchAngles, notchAngles, notchStatus, _btn, _hardware);
				if(_hardware.Y || _hardware.X || (_btn.B)){//only run this if the notch was adjusted
					//clean full cal points again, feeding updated angles in
					float cleanedPointsX[_noOfNotches+1];
					float cleanedPointsY[_noOfNotches+1];
					float notchPointsX[_noOfNotches+1];
					float notchPointsY[_noOfNotches+1];
					cleanCalPoints(tempCalPointsX, tempCalPointsY, notchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
					//linearize again
					linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, _cStickParams);
					//notchCalibrate again to update the affine transform
					notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, _cStickParams);
				}
			}else{//just show desired stick position
				displayNotch(currentCalStep, false, _notchAngleDefaults, _btn);
			}
			readSticks(false,true, _btn, _pinList, _hardware, _controls, _normGains, _aStickParams, _cStickParams, _dT);
		}
	}
	else if(running){
		//if not calibrating read the sticks normally
		readSticks(true,true, _btn, _pinList, _hardware, _controls, _normGains, _aStickParams, _cStickParams, _dT);
	}
}


int readEEPROM(ControlConfig &controls, FilterGains &gains, FilterGains &normGains, StickParams &aStickParams, StickParams &cStickParams){
	int numberOfNaN = 0;

	//get the jump setting
	controls.jumpConfig = getJumpSetting();
	if(controls.jumpConfig < controls.jumpConfigMin){
		controls.jumpConfig = DEFAULTJUMP;
		numberOfNaN++;
	}
	if(controls.jumpConfig > controls.jumpConfigMax){
		controls.jumpConfig = DEFAULTJUMP;
		numberOfNaN++;
	}
	setJump(controls);

	//get the L setting
	controls.lConfig = getLSetting();
	if(controls.lConfig < controls.triggerConfigMin) {
		controls.lConfig = controls.triggerDefault;
		numberOfNaN++;
	}
	if(controls.lConfig > controls.triggerConfigMax) {
		controls.lConfig = controls.triggerDefault;
		numberOfNaN++;
	}

	//get the R setting
	controls.rConfig = getRSetting();
	if(controls.rConfig < controls.triggerConfigMin) {
		controls.rConfig = controls.triggerDefault;
		numberOfNaN++;
	}
	if(controls.rConfig > controls.triggerConfigMax) {
		controls.rConfig = controls.triggerDefault;
		numberOfNaN++;
	}

	//get the L-trigger Offset value
	controls.lTriggerOffset = getLOffsetSetting();
	if(controls.lTriggerOffset > controls.triggerMax) {
		controls.lTriggerOffset = controls.triggerMax;
		numberOfNaN++;
	} else if(controls.lTriggerOffset < controls.triggerMin) {
		controls.lTriggerOffset = controls.triggerMin;
		numberOfNaN++;
	}

	//get the R-trigger Offset value
	controls.rTriggerOffset = getROffsetSetting();
	if(controls.rTriggerOffset > controls.triggerMax) {
		controls.rTriggerOffset = controls.triggerMax;
		numberOfNaN++;
	} else if(controls.rTriggerOffset < controls.triggerMin) {
		controls.rTriggerOffset = controls.triggerMin;
		numberOfNaN++;
	}


	//get the C-stick X offset
	controls.cXOffset = getCXOffsetSetting();
	if(controls.cXOffset > controls.cMax) {
		controls.cXOffset = 0;
		numberOfNaN++;
	} else if(controls.cXOffset < controls.cMin) {
		controls.cXOffset = 0;
		numberOfNaN++;
	}

	//get the C-stick Y offset
	controls.cYOffset = getCYOffsetSetting();
	if(controls.cYOffset > controls.cMax) {
		controls.cYOffset = 0;
		numberOfNaN++;
	} else if(controls.cYOffset < controls.cMin) {
		controls.cYOffset = 0;
		numberOfNaN++;
	}

	//get the x-axis snapback correction
	controls.xSnapback = getXSnapbackSetting();
	Serial.print("the xSnapback value from eeprom is:");
	Serial.println(controls.xSnapback);
	if(controls.xSnapback < controls.snapbackMin) {
		controls.xSnapback = controls.snapbackMin;
		numberOfNaN++;
	} else if (controls.xSnapback > controls.snapbackMax) {
		controls.xSnapback = controls.snapbackMax;
		numberOfNaN++;
	}
	gains.xVelDamp = velDampFromSnapback(controls.xSnapback);
	Serial.print("the xVelDamp value from eeprom is:");
	Serial.println(gains.xVelDamp);

	//get the y-ayis snapback correction
	controls.ySnapback = getYSnapbackSetting();
	Serial.print("the ySnapback value from eeprom is:");
	Serial.println(controls.ySnapback);
	if(controls.ySnapback < controls.snapbackMin) {
		controls.ySnapback = controls.snapbackMin;
		numberOfNaN++;
	} else if (controls.ySnapback > controls.snapbackMax) {
		controls.ySnapback = controls.snapbackMax;
		numberOfNaN++;
	}
	gains.yVelDamp = velDampFromSnapback(controls.ySnapback);
	Serial.print("the yVelDamp value from eeprom is:");
	Serial.println(gains.yVelDamp);

	//get the x-axis smoothing value
	gains.xSmoothing = getXSmoothingSetting();
	Serial.print("the xSmoothing value from eeprom is:");
	Serial.println(gains.xSmoothing);
	if(std::isnan(gains.xSmoothing)){
		gains.xSmoothing = controls.smoothingMin;
		Serial.print("the xSmoothing value was adjusted to:");
		Serial.println(gains.xSmoothing);
		numberOfNaN++;
	}
	if(gains.xSmoothing > controls.smoothingMax) {
		gains.xSmoothing = controls.smoothingMax;
	} else if(gains.xSmoothing < controls.smoothingMin) {
		gains.xSmoothing = controls.smoothingMin;
	}

	//get the y-axis smoothing value
	gains.ySmoothing = getYSmoothingSetting();
	Serial.print("the ySmoothing value from eeprom is:");
	Serial.println(gains.ySmoothing);
	if(std::isnan(gains.ySmoothing)){
		gains.ySmoothing = controls.smoothingMin;
		Serial.print("the ySmoothing value was adjusted to:");
		Serial.println(gains.ySmoothing);
		numberOfNaN++;
	}
	if(gains.ySmoothing > controls.smoothingMax) {
		gains.ySmoothing = controls.smoothingMax;
	} else if(gains.ySmoothing < controls.smoothingMin) {
		gains.ySmoothing = controls.smoothingMin;
	}

	//get the c-stick x-axis smoothing value
	gains.cXSmoothing = getCxSmoothingSetting();
	Serial.print("the cXSmoothing value from eeprom is:");
	Serial.println(gains.cXSmoothing);
	if(std::isnan(gains.cXSmoothing)){
		gains.cXSmoothing = controls.smoothingMin;
		Serial.print("the cXSmoothing value was adjusted to:");
		Serial.println(gains.cXSmoothing);
		numberOfNaN++;
	}
	if(gains.cXSmoothing > controls.smoothingMax) {
		gains.cXSmoothing = controls.smoothingMax;
	} else if(gains.cXSmoothing < controls.smoothingMin) {
		gains.cXSmoothing = controls.smoothingMin;
	}

	//get the c-stick y-axis smoothing value
	gains.cYSmoothing = getCySmoothingSetting();
	Serial.print("the cYSmoothing value from eeprom is:");
	Serial.println(gains.cYSmoothing);
	if(std::isnan(gains.cYSmoothing)){
		gains.cYSmoothing = controls.smoothingMin;
		Serial.print("the cYSmoothing value was adjusted to:");
		Serial.println(gains.cYSmoothing);
		numberOfNaN++;
	}
	if(gains.cYSmoothing > controls.smoothingMax) {
		gains.cYSmoothing = controls.smoothingMax;
	} else if(gains.cYSmoothing < controls.smoothingMin) {
		gains.cYSmoothing = controls.smoothingMin;
	}

	//recompute the intermediate gains used directly by the kalman filter
	recomputeGains(gains, normGains);

	//Get the rumble value
	controls.rumble = getRumbleSetting();
	Serial.print("Rumble value before fixing: ");
	Serial.println(controls.rumble);
	if(std::isnan(controls.rumble)) {
		controls.rumble = controls.rumbleDefault;
		numberOfNaN++;
	}
	if(controls.rumble < controls.rumbleMin) {
		controls.rumble = controls.rumbleMin;
	}
	if(controls.rumble > controls.rumbleMax) {
		controls.rumble = controls.rumbleMax;
	}
	_rumblePower = calcRumblePower(controls.rumble);
	Serial.print("Rumble value: ");
	Serial.println(controls.rumble);
	Serial.print("Rumble power: ");
	Serial.println(_rumblePower);

	//Get the autoinit value
	controls.autoInit = getAutoInitSetting();
	if(controls.autoInit < 0) {
		controls.autoInit = 0;
		numberOfNaN++;
	}
	if(controls.autoInit > 1) {
		controls.autoInit = 0;
		numberOfNaN++;
	}
	Serial.print("Auto init: ");
	Serial.println(controls.autoInit);

	//get the calibration points collected during the last A stick calibration
	float tempCalPointsX[_noOfCalibrationPoints];
	float tempCalPointsY[_noOfCalibrationPoints];
	float notchAngles[_noOfNotches];
	getPointsSetting(tempCalPointsX, ASTICK, XAXIS);
	getPointsSetting(tempCalPointsY, ASTICK, YAXIS);
	getNotchAnglesSetting(notchAngles, ASTICK);

	float cleanedPointsX[_noOfNotches+1];
	float cleanedPointsY[_noOfNotches+1];
	float notchPointsX[_noOfNotches+1];
	float notchPointsY[_noOfNotches+1];
	NotchStatus notchStatus[_noOfNotches];

	cleanCalPoints(tempCalPointsX, tempCalPointsY, notchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
	Serial.println("calibration points cleaned");
	linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, aStickParams);
	Serial.println("A stick linearized");
	notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, aStickParams);

	//get the calibration points collected during the last A stick calibration
	getPointsSetting(tempCalPointsX, CSTICK, XAXIS);
	getPointsSetting(tempCalPointsY, CSTICK, YAXIS);
	getNotchAnglesSetting(notchAngles, ASTICK);
	cleanCalPoints(tempCalPointsX, tempCalPointsY, notchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
	Serial.println("calibration points cleaned");
	linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, cStickParams);
	Serial.println("C stick linearized");
	notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, cStickParams);

	return numberOfNaN;
}

void resetDefaults(HardReset reset, ControlConfig &controls, FilterGains &gains, FilterGains &normGains, StickParams &aStickParams, StickParams &cStickParams){
	Serial.println("RESETTING ALL DEFAULTS");

	controls.jumpConfig = DEFAULTJUMP;
	setJump(controls);
	setJumpSetting(controls.jumpConfig);

	controls.lConfig = controls.triggerDefault;
	controls.rConfig = controls.triggerDefault;
	setLSetting(controls.lConfig);
	setRSetting(controls.rConfig);

	controls.cXOffset = 0;
	controls.cYOffset = 0;
	setCXOffsetSetting(controls.cXOffset);
	setCYOffsetSetting(controls.cYOffset);

	controls.xSnapback = controls.snapbackDefault;
	setXSnapbackSetting(controls.xSnapback);
	gains.xVelDamp = velDampFromSnapback(controls.xSnapback);
	controls.ySnapback = controls.snapbackDefault;
	setYSnapbackSetting(controls.ySnapback);
	gains.yVelDamp = velDampFromSnapback(controls.ySnapback);

	gains.xSmoothing = controls.smoothingMin;
	setXSmoothingSetting(gains.xSmoothing);
	gains.ySmoothing = controls.smoothingMin;
	setYSmoothingSetting(gains.ySmoothing);

	gains.cXSmoothing = controls.smoothingMin;
	setCxSmoothingSetting(gains.cXSmoothing);
	gains.cYSmoothing = controls.smoothingMin;
	setCySmoothingSetting(gains.cYSmoothing);
	//recompute the intermediate gains used directly by the kalman filter
	recomputeGains(gains, normGains);

	controls.lTriggerOffset = controls.triggerMin;
	controls.rTriggerOffset = controls.triggerMin;
	setLOffsetSetting(controls.lTriggerOffset);
	setROffsetSetting(controls.rTriggerOffset);

	controls.rumble = controls.rumbleDefault;
	_rumblePower = calcRumblePower(controls.rumble);
	setRumbleSetting(controls.rumble);

	//always cancel auto init on reset, even if we don't reset the sticks
	controls.autoInit = 0;
	setAutoInitSetting(controls.autoInit);

	if(reset == HARD){
		float notchAngles[_noOfNotches];
		for(int i = 0; i < _noOfNotches; i++){
			notchAngles[i] = _notchAngleDefaults[i];
		}
		setNotchAnglesSetting(notchAngles, ASTICK);
		setNotchAnglesSetting(notchAngles, CSTICK);

		setPointsSetting(_defaultCalPointsX, ASTICK, XAXIS);
		setPointsSetting(_defaultCalPointsY, ASTICK, YAXIS);
		Serial.println("A calibration points stored in EEPROM");

		float cleanedPointsX[_noOfNotches+1];
		float cleanedPointsY[_noOfNotches+1];
		float notchPointsX[_noOfNotches+1];
		float notchPointsY[_noOfNotches+1];
		NotchStatus notchStatus[_noOfNotches];

		cleanCalPoints(_defaultCalPointsX, _defaultCalPointsY, notchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
		Serial.println("A calibration points cleaned");
		linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, aStickParams);
		Serial.println("A stick linearized");
		notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, aStickParams);

		setPointsSetting(_defaultCalPointsX, CSTICK, XAXIS);
		setPointsSetting(_defaultCalPointsY, CSTICK, YAXIS);
		Serial.println("C calibration points stored in EEPROM");

		cleanCalPoints(_defaultCalPointsX, _defaultCalPointsY, notchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
		Serial.println("C calibration points cleaned");
		linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, cStickParams);
		Serial.println("C stick linearized");
		notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, cStickParams);
	}
}
void setPinModes(){
	pinMode(_pinL,INPUT_PULLUP);
	pinMode(_pinR,INPUT_PULLUP);
	pinMode(_pinDr,INPUT_PULLUP);
	pinMode(_pinDu,INPUT_PULLUP);
	pinMode(_pinDl,INPUT_PULLUP);
	pinMode(_pinDd,INPUT_PULLUP);
	pinMode(_pinX,INPUT_PULLUP);
	pinMode(_pinY,INPUT_PULLUP);

	pinMode(_pinA,INPUT_PULLUP);
	pinMode(_pinB,INPUT_PULLUP);
	pinMode(_pinZ,INPUT_PULLUP);
	pinMode(_pinS,INPUT_PULLUP);
#ifdef TEENSY4_0
#ifdef HALFDUPLEX
	pinMode(_pinRX,INPUT_PULLUP);
#else // HALFDUPLEX
	pinMode(9,    INPUT_PULLUP); //the normal RX pin doesn't work on teensy 4 with full duplex
#endif // HALFDUPLEX
	pinMode(_pinLED,   OUTPUT);
#endif // TEENSY4_0

#ifdef RUMBLE
	pinMode(_pinRumble, OUTPUT);
	pinMode(_pinBrake, OUTPUT);
#endif

	//Teensy 4 has some weird jump in the analog with default pin mode
	pinMode(_pinLa,INPUT_DISABLE);
	pinMode(_pinRa,INPUT_DISABLE);
	pinMode(_pinAx,INPUT_DISABLE);
	pinMode(_pinAy,INPUT_DISABLE);
	pinMode(_pinCx,INPUT_DISABLE);
	pinMode(_pinCy,INPUT_DISABLE);
}
void readButtons(Buttons &btn, HardwareButtons &hardware, ControlConfig &controls, FilterGains &gains, FilterGains &normGains, int &currentCalStep, bool &running, float tempCalPointsX[], float tempCalPointsY[], WhichStick &whichStick, NotchStatus notchStatus[], float notchAngles[], float measuredNotchAngles[], StickParams &aStickParams, StickParams &cStickParams){
	btn.A = !digitalRead(_pinA);
	btn.B = !digitalRead(_pinB);
	btn.X = !digitalRead(controls.pinXSwappable);
	btn.Y = !digitalRead(controls.pinYSwappable);
	btn.Z = !digitalRead(controls.pinZSwappable);
	btn.S = !digitalRead(_pinS);
	btn.Du = !digitalRead(_pinDu);
	btn.Dd = !digitalRead(_pinDd);
	btn.Dl = !digitalRead(_pinDl);
	btn.Dr = !digitalRead(_pinDr);

	hardware.L = !digitalRead(_pinL);
	hardware.R = !digitalRead(_pinR);
	hardware.Z = !digitalRead(_pinZ);
	hardware.X = !digitalRead(_pinX);
	hardware.Y = !digitalRead(_pinY);

	//We apply the triggers in readSticks so we can minimize race conditions
	// between trigger analog/digital so we don't get ADT vulnerability in mode 6

	/* Current Commands List
	* Safe Mode:  AXY+Start
	* Display Version: AZ+Du
	*
	* Soft Reset:  ABZ+Start
	* Hard Reset:  ABZ+Dd
	* Auto-Initialize: ABLR+Start
	*
	* Increase/Decrease Rumble: XY+Du/Dd
	* Show Current Rumble Setting: BXY (no A)
	*
	* Calibration
	* Analog Stick Calibration:  AXY+L
	* C-Stick Calibration:  AXY+R
	* Advance Calibration:  L or R
	* Undo Calibration:  Z
	* Skip to Notch Adjustment:  Start
	* Notch Adjustment CW/CCW:  X/Y
	* Notch Adjustment Reset:  B
	*
	* Analog Stick Configuration:
	* Increase/Decrease X-Axis Snapback Filtering:  LX+Du/Dd
	* Increase/Decrease Y-Axis Snapback Filtering:  LY+Du/Dd
	* Increase/Decrease X-Axis Delay:  LA+Du/Dd
	* Increase/Decrease Y-Axis Delay:  LB+Du/Dd
	* Show Filtering and Axis Delay:  LStart+Dd
	*
	* C-Stick Configuration
	* Increase/Decrease X-Axis Snapback Filtering:  RX+Du/Dd
	* Increase/Decrease Y-Axis Snapback Filtering:  RY+Du/Dd
	* Increase/Decrease X-Axis Offset:  RA+Du/Dd
	* Increase/Decrease Y-Axis Offset:  RB+Du/Dd
	* Show Filtering and Axis Offset:  RStart+Dd
	*
	* Swap X with Z:  XZ+Start
	* Swap Y with Z:  YZ+Start
	* Reset Z-Jump:  AXY+Z
	* Toggle Analog Slider L:  ZL+Start
	* Toggle Analog Slider R:  ZR+Start
	* Increase/Decrease L-trigger Offset:  ZL+Du/Dd
	* Increase/Decrease R-Trigger Offset:  ZR+Du/Dd
	*/

	static bool advanceCal = false;

	//check the dpad buttons to change the controller settings
	if(!controls.safeMode && (currentCalStep == -1)) {
		if(btn.A && hardware.X && hardware.Y && btn.S) { //Safe Mode Toggle
			controls.safeMode = true;
			freezeSticks(4000, btn, hardware);
		} else if (btn.A && hardware.Z && btn.Du) { //display version number
			const int versionHundreds = floor(SW_VERSION/100.0);
			const int versionOnes     = SW_VERSION-versionHundreds;
			btn.Ax = (uint8_t) _floatOrigin;
			btn.Ay = (uint8_t) _floatOrigin;
			btn.Cx = (uint8_t) _floatOrigin + versionHundreds;
			btn.Cy = (uint8_t) _floatOrigin + versionOnes;
			clearButtons(2000, btn, hardware);
		} else if (btn.A && btn.B && hardware.Z && btn.S) { //Soft Reset
			resetDefaults(SOFT, controls, gains, normGains, _aStickParams, _cStickParams);//don't reset sticks
			freezeSticks(2000, btn, hardware);
		} else if (btn.A && btn.B && hardware.Z && btn.Dd) { //Hard Reset
			resetDefaults(HARD, controls, gains, normGains, _aStickParams, _cStickParams);//do reset sticks
			freezeSticks(2000, btn, hardware);
		} else if (btn.A && btn.B && hardware.L && hardware.R && btn.S) { //Toggle Auto-Initialize
			changeAutoInit(btn, hardware, controls);
		} else if (hardware.X && hardware.Y && btn.Du) { //Increase Rumble
#ifdef RUMBLE
			changeRumble(INCREASE, btn, hardware, controls);
#else // RUMBLE
			//nothing
			freezeSticks(2000, btn, hardware);
#endif // RUMBLE
		} else if (hardware.X && hardware.Y && btn.Dd) { //Decrease Rumble
#ifdef RUMBLE
			changeRumble(DECREASE, btn, hardware, controls);
#else // RUMBLE
			//nothing
			freezeSticks(2000, btn, hardware);
#endif // RUMBLE
		} else if (hardware.X && hardware.Y && btn.B && !btn.A) { //Show current rumble setting
#ifdef RUMBLE
			showRumble(2000, btn, hardware, controls);
#else // RUMBLE
			freezeSticks(2000, btn, hardware);
#endif // RUMBLE
		} else if (btn.A && hardware.X && hardware.Y && hardware.L) { //Analog Calibration
			Serial.println("Calibrating the A stick");
			whichStick = ASTICK;
			currentCalStep ++;
			advanceCal = true;
			freezeSticks(2000, btn, hardware);
		} else if (btn.A && hardware.X && hardware.Y && hardware.R) { //C-stick Calibration
			Serial.println("Calibrating the C stick");
			whichStick = CSTICK;
			currentCalStep ++;
			advanceCal = true;
			freezeSticks(2000, btn, hardware);
		} else if(hardware.L && hardware.X && btn.Du) { //Increase Analog X-Axis Snapback Filtering
			adjustSnapback(XAXIS, INCREASE, btn, hardware, controls, gains, normGains);
		} else if(hardware.L && hardware.X && btn.Dd) { //Decrease Analog X-Axis Snapback Filtering
			adjustSnapback(XAXIS, DECREASE, btn, hardware, controls, gains, normGains);
		} else if(hardware.L && hardware.Y && btn.Du) { //Increase Analog Y-Axis Snapback Filtering
			adjustSnapback(YAXIS, INCREASE, btn, hardware, controls, gains, normGains);
		} else if(hardware.L && hardware.Y && btn.Dd) { //Decrease Analog Y-Axis Snapback Filtering
			adjustSnapback(YAXIS, DECREASE, btn, hardware, controls, gains, normGains);
		} else if(hardware.L && btn.A && btn.Du) { //Increase X-axis Delay
			adjustSmoothing(XAXIS, INCREASE, btn, hardware, controls, gains, normGains);
		} else if(hardware.L && btn.A && btn.Dd) { //Decrease X-axis Delay
			adjustSmoothing(XAXIS, DECREASE, btn, hardware, controls, gains, normGains);
		} else if(hardware.L && btn.B && btn.Du) { //Increase Y-axis Delay
			adjustSmoothing(YAXIS, INCREASE, btn, hardware, controls, gains, normGains);
		} else if(hardware.L && btn.B && btn.Dd) { //Decrease Y-axis Delay
			adjustSmoothing(YAXIS, DECREASE, btn, hardware, controls, gains, normGains);
		} else if(hardware.L && btn.S && btn.Dd) { //Show Current Analog Settings
			showAstickSettings(btn, hardware, controls, gains);
		} else if(hardware.R && hardware.X && btn.Du) { //Increase C-stick X-Axis Snapback Filtering
			adjustCstickSmoothing(XAXIS, INCREASE, btn, hardware, controls, gains, normGains);
		} else if(hardware.R && hardware.X && btn.Dd) { //Decrease C-stick X-Axis Snapback Filtering
			adjustCstickSmoothing(XAXIS, DECREASE, btn, hardware, controls, gains, normGains);
		} else if(hardware.R && hardware.Y && btn.Du) { //Increase C-stick Y-Axis Snapback Filtering
			adjustCstickSmoothing(YAXIS, INCREASE, btn, hardware, controls, gains, normGains);
		} else if(hardware.R && hardware.Y && btn.Dd) { //Decrease C-stick Y-Axis Snapback Filtering
			adjustCstickSmoothing(YAXIS, DECREASE, btn, hardware, controls, gains, normGains);
		} else if(hardware.R && btn.A && btn.Du) { //Increase C-stick X Offset
			adjustCstickOffset(XAXIS, INCREASE, btn, hardware, controls);
		} else if(hardware.R && btn.A && btn.Dd) { //Decrease C-stick X Offset
			adjustCstickOffset(XAXIS, DECREASE, btn, hardware, controls);
		} else if(hardware.R && btn.B && btn.Du) { //Increase C-stick Y Offset
			adjustCstickOffset(YAXIS, INCREASE, btn, hardware, controls);
		} else if(hardware.R && btn.B && btn.Dd) { //Decrease C-stick Y Offset
			adjustCstickOffset(YAXIS, DECREASE, btn, hardware, controls);
		} else if(hardware.R && btn.S && btn.Dd) { //Show Current C-stick SEttings
			showCstickSettings(btn, hardware, controls, gains);
		} else if(hardware.L && hardware.Z && btn.S) { //Toggle Analog L
			nextTriggerState(LTRIGGER, btn, hardware, controls);
		} else if(hardware.R && hardware.Z && btn.S) { //Toggle Analog R
			nextTriggerState(RTRIGGER, btn, hardware, controls);
		} else if(hardware.L && hardware.Z && btn.Du) { //Increase L-Trigger Offset
			adjustTriggerOffset(LTRIGGER, INCREASE, btn, hardware, controls);
		} else if(hardware.L && hardware.Z && btn.Dd) { //Decrease L-trigger Offset
			adjustTriggerOffset(LTRIGGER, DECREASE, btn, hardware, controls);
		} else if(hardware.R && hardware.Z && btn.Du) { //Increase R-trigger Offset
			adjustTriggerOffset(RTRIGGER, INCREASE, btn, hardware, controls);
		} else if(hardware.R && hardware.Z && btn.Dd) { //Decrease R-trigger Offset
			adjustTriggerOffset(RTRIGGER, DECREASE, btn, hardware, controls);
		} else if(hardware.X && hardware.Z && btn.S) { //Swap X and Z
			readJumpConfig(SWAP_XZ, controls);
			freezeSticks(2000, btn, hardware);
		} else if(hardware.Y && hardware.Z && btn.S) { //Swap Y and Z
			readJumpConfig(SWAP_YZ, controls);
			freezeSticks(2000, btn, hardware);
		} else if(btn.A && hardware.X && hardware.Y && hardware.Z) { // Reset X/Y/Z Config
			readJumpConfig(DEFAULTJUMP, controls);
			freezeSticks(2000, btn, hardware);
		}
	} else if (currentCalStep == -1) { //Safe Mode Enabled, Lock Settings, wait for safe mode command
		static float safeModeAccumulator = 0.0;
		if(btn.A && hardware.X && hardware.Y && btn.S) { //Safe Mode Toggle
			safeModeAccumulator = 0.99*safeModeAccumulator + 0.01;
		} else {
			safeModeAccumulator = 0.99*safeModeAccumulator;
		}
		if(safeModeAccumulator > 0.99){
			safeModeAccumulator = 0;
			if (!running) {//wake it up if not already running
				running = true;
			}
			controls.safeMode = false;
			freezeSticks(2000, btn, hardware);
		}
	}


	//Skip stick measurement and go to notch adjust using the start button while calibrating
	if(btn.S && (currentCalStep >= 0 && currentCalStep < 32)){
		currentCalStep = _noOfCalibrationPoints;
		//Do the same thing we would have done at step 32 had we actually collected the points, but with stored tempCalPoints
		if(whichStick == CSTICK){
			//get the calibration points collected during the last A stick calibration
			getPointsSetting(tempCalPointsX, whichStick, XAXIS);
			getPointsSetting(tempCalPointsY, whichStick, YAXIS);
			getNotchAnglesSetting(notchAngles, whichStick);
			//make temp temp cal points that are missing all tertiary notches so that we get a neutral grid
			float temptempCalPointsX[_noOfCalibrationPoints];
			float temptempCalPointsY[_noOfCalibrationPoints];
			stripCalPoints(tempCalPointsX, tempCalPointsY, temptempCalPointsX, temptempCalPointsY);
			//clean the stripped calibration points, use default angles
			float cleanedPointsX[_noOfNotches+1];
			float cleanedPointsY[_noOfNotches+1];
			float notchPointsX[_noOfNotches+1];
			float notchPointsY[_noOfNotches+1];
			cleanCalPoints(temptempCalPointsX, temptempCalPointsY, _notchAngleDefaults, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
			linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, cStickParams);
			notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, cStickParams);
			//apply the calibration to the original measured values including any tertiaries; we don't care about the angles
			cleanCalPoints(tempCalPointsX, tempCalPointsY, _notchAngleDefaults, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
			float transformedX[_noOfNotches+1];
			float transformedY[_noOfNotches+1];
			transformCalPoints(cleanedPointsX, cleanedPointsY, transformedX, transformedY, cStickParams);
			//compute the angles for those notches into measuredNotchAngles, using the default angles for the diagonals
			computeStickAngles(transformedX, transformedY, measuredNotchAngles);
			//clean full cal points again, feeding those angles in
			cleanCalPoints(tempCalPointsX, tempCalPointsY, measuredNotchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
			//clear unused notch angles
			cleanNotches(notchAngles, measuredNotchAngles, notchStatus);
			//clean full cal points again again, feeding those measured angles in for missing tertiary notches
			cleanCalPoints(tempCalPointsX, tempCalPointsY, notchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
			//linearize again
			linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, cStickParams);
			//notchCalibrate again
			notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, cStickParams);
		} else if(whichStick == ASTICK){
			//get the calibration points collected during the last A stick calibration
			getPointsSetting(tempCalPointsX, whichStick, XAXIS);
			getPointsSetting(tempCalPointsY, whichStick, YAXIS);
			getNotchAnglesSetting(notchAngles, whichStick);
			//make temp temp cal points that are missing all tertiary notches so that we get a neutral grid
			float temptempCalPointsX[_noOfCalibrationPoints];
			float temptempCalPointsY[_noOfCalibrationPoints];
			stripCalPoints(tempCalPointsX, tempCalPointsY, temptempCalPointsX, temptempCalPointsY);
			//clean the stripped calibration points, use default angles
			float cleanedPointsX[_noOfNotches+1];
			float cleanedPointsY[_noOfNotches+1];
			float notchPointsX[_noOfNotches+1];
			float notchPointsY[_noOfNotches+1];
			cleanCalPoints(temptempCalPointsX, temptempCalPointsY, _notchAngleDefaults, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
			linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, aStickParams);
			notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, aStickParams);
			//apply the calibration to the original measured values including any tertiaries; we don't care about the angles
			cleanCalPoints(tempCalPointsX, tempCalPointsY, _notchAngleDefaults, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
			float transformedX[_noOfNotches+1];
			float transformedY[_noOfNotches+1];
			transformCalPoints(cleanedPointsX, cleanedPointsY, transformedX, transformedY, aStickParams);
			//compute the angles for those notches into measuredNotchAngles, using the default angles for the diagonals
			computeStickAngles(transformedX, transformedY, measuredNotchAngles);
			//clean full cal points again, feeding those angles in
			cleanCalPoints(tempCalPointsX, tempCalPointsY, measuredNotchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
			//clear unused notch angles
			cleanNotches(notchAngles, measuredNotchAngles, notchStatus);
			//clean full cal points again again, feeding those measured angles in for missing tertiary notches
			cleanCalPoints(tempCalPointsX, tempCalPointsY, notchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
			//linearize again
			linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, aStickParams);
			//notchCalibrate again
			notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, aStickParams);
		}
	}
	//Undo Calibration using Z-button
	static bool undoCal = false;
	static bool undoCalPressed = false;
	if(hardware.Z && undoCal && !undoCalPressed) {
		undoCalPressed = true;
		if(currentCalStep % 2 == 0 && currentCalStep < 32 && currentCalStep != 0 ) {
			//If it's measuring zero, go back to the previous zero
			currentCalStep --;
			currentCalStep --;
		} else if(currentCalStep % 2 == 1 && currentCalStep < 32 && currentCalStep != 0 ) {
			//If it's measuring a notch, go back to the zero before the previous notch
			currentCalStep -= 3;
			currentCalStep = max(currentCalStep, 0);
		} else if(currentCalStep > 32) {
			//We can go directly between notches when adjusting notches
			currentCalStep --;
		}
		if(whichStick == CSTICK){
			int notchIndex = _notchAdjOrder[min(currentCalStep-_noOfCalibrationPoints, _noOfAdjNotches-1)];//limit this so it doesn't access outside the array bounds
			while((currentCalStep >= _noOfCalibrationPoints) && (notchStatus[notchIndex] == TERT_INACTIVE) && (currentCalStep < _noOfCalibrationPoints + _noOfAdjNotches)){//this non-diagonal notch was not calibrated
				//skip to the next valid notch
				currentCalStep--;
				notchIndex = _notchAdjOrder[min(currentCalStep-_noOfCalibrationPoints, _noOfAdjNotches-1)];//limit this so it doesn't access outside the array bounds
			}
		} else if(whichStick == ASTICK){
			int notchIndex = _notchAdjOrder[min(currentCalStep-_noOfCalibrationPoints, _noOfAdjNotches-1)];//limit this so it doesn't access outside the array bounds
			while((currentCalStep >= _noOfCalibrationPoints) && (notchStatus[notchIndex] == TERT_INACTIVE) && (currentCalStep < _noOfCalibrationPoints + _noOfAdjNotches)){//this non-diagonal notch was not calibrated
				//skip to the next valid notch
				currentCalStep--;
				notchIndex = _notchAdjOrder[min(currentCalStep-_noOfCalibrationPoints, _noOfAdjNotches-1)];//limit this so it doesn't access outside the array bounds
			}
		}
	} else if(!hardware.Z) {
		undoCalPressed = false;
	}

	//Advance Calibration Using L or R triggers
	static float advanceCalAccumulator = 0.0;
	if((hardware.L || hardware.R) && advanceCal){
		advanceCalAccumulator = 0.96*advanceCalAccumulator + 0.04;
	} else {
		advanceCalAccumulator = 0.96*advanceCalAccumulator;
	}

	static bool advanceCalPressed = false;
	if(advanceCalAccumulator > 0.75 && !advanceCalPressed){
		advanceCalPressed = true;
		if (whichStick == CSTICK){
			if(currentCalStep < _noOfCalibrationPoints){//still collecting points
				collectCalPoints(whichStick, currentCalStep, tempCalPointsX, tempCalPointsY, _pinList);
			}
			currentCalStep ++;
			if(currentCalStep >= 2 && currentCalStep != _noOfNotches*2) {//don't undo at the beginning of collection or notch adjust
				undoCal = true;
			} else {
				undoCal = false;
			}
			if(currentCalStep == _noOfCalibrationPoints){//done collecting points
				Serial.println("finished collecting the calibration points for the C stick");
				//make temp temp cal points that are missing all tertiary notches so that we get a neutral grid
				float temptempCalPointsX[_noOfCalibrationPoints];
				float temptempCalPointsY[_noOfCalibrationPoints];
				//Recall the previous notch angles
				getNotchAnglesSetting(notchAngles, whichStick);
				stripCalPoints(tempCalPointsX, tempCalPointsY, temptempCalPointsX, temptempCalPointsY);
				//clean the stripped calibration points, use default angles
				float cleanedPointsX[_noOfNotches+1];
				float cleanedPointsY[_noOfNotches+1];
				float notchPointsX[_noOfNotches+1];
				float notchPointsY[_noOfNotches+1];
				cleanCalPoints(temptempCalPointsX, temptempCalPointsY, _notchAngleDefaults, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
				linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, cStickParams);
				notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, cStickParams);
				//apply the calibration to the original measured values including any tertiaries; we don't care about the angles
				cleanCalPoints(tempCalPointsX, tempCalPointsY, _notchAngleDefaults, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
				float transformedX[_noOfNotches+1];
				float transformedY[_noOfNotches+1];
				transformCalPoints(cleanedPointsX, cleanedPointsY, transformedX, transformedY, cStickParams);
				//compute the angles for those notches into measuredNotchAngles, using the default angles for the diagonals
				computeStickAngles(transformedX, transformedY, measuredNotchAngles);
				//clean full cal points again, feeding those angles in
				cleanCalPoints(tempCalPointsX, tempCalPointsY, measuredNotchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
				//clear unused notch angles
				cleanNotches(notchAngles, measuredNotchAngles, notchStatus);
				//clean full cal points again again, feeding those measured angles in for missing tertiary notches
				cleanCalPoints(tempCalPointsX, tempCalPointsY, notchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
				//linearize again
				linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, cStickParams);
				//notchCalibrate again
				notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, cStickParams);
			}
			int notchIndex = _notchAdjOrder[min(currentCalStep-_noOfCalibrationPoints, _noOfAdjNotches-1)];//limit this so it doesn't access outside the array bounds
			while((currentCalStep >= _noOfCalibrationPoints) && (notchStatus[notchIndex] == TERT_INACTIVE) && (currentCalStep < _noOfCalibrationPoints + _noOfAdjNotches)){//this non-diagonal notch was not calibrated
				//skip to the next valid notch
				currentCalStep++;
				notchIndex = _notchAdjOrder[min(currentCalStep-_noOfCalibrationPoints, _noOfAdjNotches-1)];//limit this so it doesn't access outside the array bounds
			}
			if(currentCalStep >= _noOfCalibrationPoints + _noOfAdjNotches){//done adjusting notches
				Serial.println("finished adjusting notches for the C stick");
				setPointsSetting(tempCalPointsX, whichStick, XAXIS);
				setPointsSetting(tempCalPointsY, whichStick, YAXIS);
				setNotchAnglesSetting(notchAngles, whichStick);
				controls.autoInit = 0;
				setAutoInitSetting(controls.autoInit);
				Serial.println("calibration points stored in EEPROM");
				float cleanedPointsX[_noOfNotches+1];
				float cleanedPointsY[_noOfNotches+1];
				float notchPointsX[_noOfNotches+1];
				float notchPointsY[_noOfNotches+1];
				cleanCalPoints(tempCalPointsX, tempCalPointsY, notchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
				Serial.println("calibration points cleaned");
				linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, cStickParams);
				Serial.println("C stick linearized");
				notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, cStickParams);
				currentCalStep = -1;
				advanceCal = false;
			}
		}
		else if (whichStick == ASTICK){
			Serial.println("Current step:");
			Serial.println(currentCalStep);
			if(currentCalStep < _noOfCalibrationPoints){//still collecting points
				collectCalPoints(whichStick, currentCalStep, tempCalPointsX, tempCalPointsY, _pinList);
			}
			currentCalStep ++;
			if(currentCalStep >= 2 && currentCalStep != _noOfCalibrationPoints) {//don't undo at the beginning of collection or notch adjust
				undoCal = true;
			} else {
				undoCal = false;
			}
			if(currentCalStep == _noOfCalibrationPoints){//done collecting points
				//make temp temp cal points that are missing all tertiary notches so that we get a neutral grid
				float temptempCalPointsX[_noOfCalibrationPoints];
				float temptempCalPointsY[_noOfCalibrationPoints];
				stripCalPoints(tempCalPointsX, tempCalPointsY, temptempCalPointsX, temptempCalPointsY);
				//Recall the previous notch angles
				getNotchAnglesSetting(notchAngles, whichStick);
				//clean the stripped calibration points, use default angles
				float cleanedPointsX[_noOfNotches+1];
				float cleanedPointsY[_noOfNotches+1];
				float notchPointsX[_noOfNotches+1];
				float notchPointsY[_noOfNotches+1];
				cleanCalPoints(temptempCalPointsX, temptempCalPointsY, _notchAngleDefaults, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
				linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, aStickParams);
				notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, aStickParams);
				//apply the calibration to the original measured values including any tertiaries; we don't care about the angles
				cleanCalPoints(tempCalPointsX, tempCalPointsY, _notchAngleDefaults, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
				float transformedX[_noOfNotches+1];
				float transformedY[_noOfNotches+1];
				transformCalPoints(cleanedPointsX, cleanedPointsY, transformedX, transformedY, aStickParams);
				//compute the angles for those notches into measuredNotchAngles, using the default angles for the diagonals
				computeStickAngles(transformedX, transformedY, measuredNotchAngles);
				//clean full cal points again, feeding those angles in
				cleanCalPoints(tempCalPointsX, tempCalPointsY, measuredNotchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
				//clear unused notch angles
				cleanNotches(notchAngles, measuredNotchAngles, notchStatus);
				//clean full cal points again again, feeding those measured angles in for missing tertiary notches
				cleanCalPoints(tempCalPointsX, tempCalPointsY, notchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
				//linearize again
				linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, aStickParams);
				//notchCalibrate again
				notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, aStickParams);
			}
			int notchIndex = _notchAdjOrder[min(currentCalStep-_noOfCalibrationPoints, _noOfAdjNotches-1)];//limit this so it doesn't access outside the array bounds
			while((currentCalStep >= _noOfCalibrationPoints) && (notchStatus[notchIndex] == TERT_INACTIVE) && (currentCalStep < _noOfCalibrationPoints + _noOfAdjNotches)){//this non-diagonal notch was not calibrated
				//skip to the next valid notch
				currentCalStep++;
				notchIndex = _notchAdjOrder[min(currentCalStep-_noOfCalibrationPoints, _noOfAdjNotches-1)];//limit this so it doesn't access outside the array bounds
			}
			if(currentCalStep >= _noOfCalibrationPoints + _noOfAdjNotches){//done adjusting notches
				Serial.println("finished adjusting notches for the A stick");
				setPointsSetting(tempCalPointsX, whichStick, XAXIS);
				setPointsSetting(tempCalPointsY, whichStick, YAXIS);
				setNotchAnglesSetting(notchAngles, whichStick);
				controls.autoInit = 0;
				setAutoInitSetting(controls.autoInit);
				Serial.println("calibration points stored in EEPROM");
				float cleanedPointsX[_noOfNotches+1];
				float cleanedPointsY[_noOfNotches+1];
				float notchPointsX[_noOfNotches+1];
				float notchPointsY[_noOfNotches+1];
				cleanCalPoints(tempCalPointsX, tempCalPointsY, notchAngles, cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, notchStatus);
				Serial.println("calibration points cleaned");
				linearizeCal(cleanedPointsX, cleanedPointsY, cleanedPointsX, cleanedPointsY, aStickParams);
				Serial.println("A stick linearized");
				notchCalibrate(cleanedPointsX, cleanedPointsY, notchPointsX, notchPointsY, _noOfNotches, aStickParams);
				currentCalStep = -1;
				advanceCal = false;
			}
		}
	} else if(advanceCalAccumulator <= 0.25) {
		advanceCalPressed = false;
	}
}
void freezeSticks(const int time, Buttons &btn, HardwareButtons &hardware) {
	btn.Cx = (uint8_t) (255);
	btn.Cy = (uint8_t) (255);
	btn.Ax = (uint8_t) (255);
	btn.Ay = (uint8_t) (255);
	btn.La = (uint8_t) (255 + 60.0);
	btn.Ra = (uint8_t) (255 + 60.0);

	btn.A = (uint8_t) 0;
	btn.B = (uint8_t) 0;
	btn.X = (uint8_t) 0;
	btn.Y = (uint8_t) 0;
	btn.L = (uint8_t) 0;
	btn.R = (uint8_t) 0;
	btn.Z = (uint8_t) 0;
	btn.S = (uint8_t) 0;

	hardware.L = (uint8_t) 0;
	hardware.R = (uint8_t) 0;
	hardware.X = (uint8_t) 0;
	hardware.Y = (uint8_t) 0;
	hardware.Z = (uint8_t) 0;

	int startTime = millis();
	int delta = 0;
	while(delta < time){
		delta = millis() - startTime;
	}
}
//This clears all the buttons but doesn't overwrite the sticks or shoulder buttons.
void clearButtons(const int time, Buttons &btn, HardwareButtons &hardware) {
	btn.A = (uint8_t) 0;
	btn.B = (uint8_t) 0;
	btn.X = (uint8_t) 0;
	btn.Y = (uint8_t) 0;
	btn.L = (uint8_t) 0;
	btn.R = (uint8_t) 0;
	btn.Z = (uint8_t) 0;
	btn.S = (uint8_t) 0;

	hardware.L = (uint8_t) 0;
	hardware.R = (uint8_t) 0;
	hardware.X = (uint8_t) 0;
	hardware.Y = (uint8_t) 0;
	hardware.Z = (uint8_t) 0;

	int startTime = millis();
	int delta = 0;
	while(delta < time){
		delta = millis() - startTime;
	}
}
void changeRumble(const Increase increase, Buttons &btn, HardwareButtons &hardware, ControlConfig &controls) {
	Serial.println("changing rumble");
	if(increase == INCREASE) {
		controls.rumble += 1;
	} else {
		controls.rumble -= 1;
	}
	if(controls.rumble > controls.rumbleMax) {
		controls.rumble = controls.rumbleMax;
	}
	if(controls.rumble < controls.rumbleMin) {
		controls.rumble = controls.rumbleMin;
	}

	_rumblePower = calcRumblePower(controls.rumble);
	showRumble(1000, btn, hardware, controls);
}

void showRumble(const int time, Buttons &btn, HardwareButtons &hardware, ControlConfig &controls) {
	btn.Cx = (uint8_t) _intOrigin;
	btn.Cy = (uint8_t) (controls.rumble + _floatOrigin);
	clearButtons(time, btn, hardware);

	setRumbleSetting(controls.rumble);
}

//Make it so you don't need to press B.
//This is only good if the sticks are calibrated, so
// the setting auto-resets whenever you hard reset or recalibrate.
void changeAutoInit(Buttons &btn, HardwareButtons &hardware, ControlConfig &controls) {
	if(controls.autoInit == 0) {
		controls.autoInit = 1;
	} else {
		controls.autoInit = 0;
	}

	//move sticks up-right for on, down-left for off
	btn.Ax = (uint8_t) (controls.autoInit*100 - 50 + _floatOrigin);
	btn.Ay = (uint8_t) (controls.autoInit*100 - 50 + _floatOrigin);
	btn.Cx = (uint8_t) (controls.autoInit*100 - 50 + _floatOrigin);
	btn.Cy = (uint8_t) (controls.autoInit*100 - 50 + _floatOrigin);

	clearButtons(2000, btn, hardware);

	setAutoInitSetting(controls.autoInit);
}

void adjustSnapback(const WhichAxis axis, const Increase increase, Buttons &btn, HardwareButtons &hardware, ControlConfig &controls, FilterGains &gains, FilterGains &normGains){
	Serial.println("adjusting snapback filtering");
	if(axis == XAXIS && increase == INCREASE){
		controls.xSnapback = min(controls.xSnapback+1, controls.snapbackMax);
		Serial.print("x snapback filtering increased to:");
		Serial.println(controls.xSnapback);
	}
	else if(axis == XAXIS && increase == DECREASE){
		controls.xSnapback = max(controls.xSnapback-1, controls.snapbackMin);
		Serial.print("x snapback filtering decreased to:");
		Serial.println(controls.xSnapback);
	}

	if(axis == YAXIS && increase == INCREASE){
		controls.ySnapback = min(controls.ySnapback+1, controls.snapbackMax);
		Serial.print("y snapback filtering increased to:");
		Serial.println(controls.ySnapback);
	}
	else if(axis == YAXIS && increase == DECREASE){
		controls.ySnapback = max(controls.ySnapback-1, controls.snapbackMin);
		Serial.print("y snapback filtering decreased to:");
		Serial.println(controls.ySnapback);
	}

	gains.xVelDamp = velDampFromSnapback(controls.xSnapback);
	gains.yVelDamp = velDampFromSnapback(controls.ySnapback);

    //recompute the intermediate gains used directly by the kalman filter
    recomputeGains(gains, normGains);

	btn.Cx = (uint8_t) (controls.xSnapback + _floatOrigin);
	btn.Cy = (uint8_t) (controls.ySnapback + _floatOrigin);

	clearButtons(2000, btn, hardware);

	setXSnapbackSetting(controls.xSnapback);
	setYSnapbackSetting(controls.ySnapback);
}
void adjustSmoothing(const WhichAxis axis, const Increase increase, Buttons &btn, HardwareButtons &hardware, ControlConfig &controls, FilterGains &gains, FilterGains &normGains) {
	Serial.println("Adjusting Smoothing");
	if (axis == XAXIS && increase == INCREASE) {
		gains.xSmoothing = gains.xSmoothing + 0.1;
		if(gains.xSmoothing > controls.smoothingMax) {
			gains.xSmoothing = controls.smoothingMax;
		}
		setXSmoothingSetting(gains.xSmoothing);
		Serial.print("X Smoothing increased to:");
		Serial.println(gains.xSmoothing);
	} else if(axis == XAXIS && increase == DECREASE) {
		gains.xSmoothing = gains.xSmoothing - 0.1;
		if(gains.xSmoothing < controls.smoothingMin) {
			gains.xSmoothing = controls.smoothingMin;
		}
		setXSmoothingSetting(gains.xSmoothing);
		Serial.print("X Smoothing decreased to:");
		Serial.println(gains.xSmoothing);
	} else if(axis == YAXIS && increase == INCREASE) {
		gains.ySmoothing = gains.ySmoothing + 0.1;
		if (gains.ySmoothing > controls.smoothingMax) {
			gains.ySmoothing = controls.smoothingMax;
		}
		setYSmoothingSetting(gains.ySmoothing);
		Serial.print("Y Smoothing increased to:");
		Serial.println(gains.ySmoothing);
	} else if(axis == YAXIS && increase == DECREASE) {
		gains.ySmoothing = gains.ySmoothing - 0.1;
		if (gains.ySmoothing < controls.smoothingMin) {
			gains.ySmoothing = controls.smoothingMin;
		}
		setYSmoothingSetting(gains.ySmoothing);
		Serial.print("Y Smoothing decreased to:");
		Serial.println(gains.ySmoothing);
	}

	//recompute the intermediate gains used directly by the kalman filter
	recomputeGains(gains, normGains);

	btn.Cx = (uint8_t) (_floatOrigin + (gains.xSmoothing * 10));
	btn.Cy = (uint8_t) (_floatOrigin + (gains.ySmoothing * 10));

	clearButtons(2000, btn, hardware);
}
void showAstickSettings(Buttons &btn, HardwareButtons &hardware, const ControlConfig &controls, FilterGains &gains) {
	//Snapback on A-stick
	btn.Ax = (uint8_t) (controls.xSnapback + _floatOrigin);
	btn.Ay = (uint8_t) (controls.ySnapback + _floatOrigin);

	//Smoothing on C-stick
	btn.Cx = (uint8_t) (_floatOrigin + (gains.xSmoothing * 10));
	btn.Cy = (uint8_t) (_floatOrigin + (gains.ySmoothing * 10));

	clearButtons(2000, btn, hardware);
}
void adjustCstickSmoothing(const WhichAxis axis, const Increase increase, Buttons &btn, HardwareButtons &hardware, ControlConfig &controls, FilterGains &gains, FilterGains &normGains) {
	Serial.println("Adjusting C-Stick Smoothing");
	if (axis == XAXIS && increase == INCREASE) {
		gains.cXSmoothing = gains.cXSmoothing + 0.1;
		if(gains.cXSmoothing > controls.smoothingMax) {
			gains.cXSmoothing = controls.smoothingMax;
		}
		setCxSmoothingSetting(gains.cXSmoothing);
		Serial.print("C-Stick X Smoothing increased to:");
		Serial.println(gains.cXSmoothing);
	} else if(axis == XAXIS && increase == DECREASE) {
		gains.cXSmoothing = gains.cXSmoothing - 0.1;
		if(gains.cXSmoothing < controls.smoothingMin) {
			gains.cXSmoothing = controls.smoothingMin;
		}
		setCxSmoothingSetting(gains.cXSmoothing);
		Serial.print("C-Stick X Smoothing decreased to:");
		Serial.println(gains.cXSmoothing);
	} else if(axis == YAXIS && increase == INCREASE) {
		gains.cYSmoothing = gains.cYSmoothing + 0.1;
		if (gains.cYSmoothing > controls.smoothingMax) {
			gains.cYSmoothing = controls.smoothingMax;
		}
		setCySmoothingSetting(gains.cYSmoothing);
		Serial.print("C-Stick Y Smoothing increased to:");
		Serial.println(gains.cYSmoothing);
	} else if(axis == YAXIS && increase == DECREASE) {
		gains.cYSmoothing = gains.cYSmoothing - 0.1;
		if (gains.cYSmoothing < controls.smoothingMin) {
			gains.cYSmoothing = controls.smoothingMin;
		}
		setCySmoothingSetting(gains.cYSmoothing);
		Serial.print("C-Stick Y Smoothing decreased to:");
		Serial.println(gains.cYSmoothing);
	}

	//recompute the intermediate gains used directly by the kalman filter
	recomputeGains(gains, normGains);

	btn.Cx = (uint8_t) (_floatOrigin + (gains.cXSmoothing * 10));
	btn.Cy = (uint8_t) (_floatOrigin + (gains.cYSmoothing * 10));

	clearButtons(2000, btn, hardware);
}
void adjustCstickOffset(const WhichAxis axis, const Increase increase, Buttons &btn, HardwareButtons &hardware, ControlConfig &controls) {
	Serial.println("Adjusting C-stick Offset");
	if(axis == XAXIS && increase == INCREASE) {
		controls.cXOffset++;
		if(controls.cXOffset > controls.cMax) {
			controls.cXOffset = controls.cMax;
		}
		setCXOffsetSetting(controls.cXOffset);
		Serial.print("X offset increased to:");
		Serial.println(controls.cXOffset);
	} else if(axis == XAXIS && increase == DECREASE) {
		controls.cXOffset--;
		if(controls.cXOffset < controls.cMin) {
			controls.cXOffset = controls.cMin;
		}
		setCXOffsetSetting(controls.cXOffset);
		Serial.print("X offset decreased to:");
		Serial.println(controls.cXOffset);
	} else if(axis == YAXIS && increase == INCREASE) {
		controls.cYOffset++;
		if(controls.cYOffset > controls.cMax) {
			controls.cYOffset = controls.cMax;
		}
		setCYOffsetSetting(controls.cYOffset);
		Serial.print("Y offset increased to:");
		Serial.println(controls.cYOffset);
	} else if(axis == YAXIS && increase == DECREASE) {
		controls.cYOffset--;
		if(controls.cYOffset < controls.cMin) {
			controls.cYOffset = controls.cMin;
		}
		setCYOffsetSetting(controls.cYOffset);
		Serial.print("Y offset decreased to:");
		Serial.println(controls.cYOffset);
	}

	btn.Cx = (uint8_t) (_floatOrigin + controls.cXOffset);
	btn.Cy = (uint8_t) (_floatOrigin + controls.cYOffset);

	clearButtons(2000, btn, hardware);
}
void showCstickSettings(Buttons &btn, HardwareButtons &hardware, ControlConfig &controls, FilterGains &gains) {
	//Snapback/smoothing on A-stick
	btn.Ax = (uint8_t) (_floatOrigin + (gains.cXSmoothing * 10));
	btn.Ay = (uint8_t) (_floatOrigin + (gains.cYSmoothing * 10));

	//Smoothing on C-stick
	btn.Cx = (uint8_t) (_floatOrigin + controls.cXOffset);
	btn.Cy = (uint8_t) (_floatOrigin + controls.cYOffset);

	clearButtons(2000, btn, hardware);
}
void adjustTriggerOffset(const WhichTrigger trigger, const Increase increase, Buttons &btn, HardwareButtons &hardware, ControlConfig &controls) {
	if(trigger == LTRIGGER && increase == INCREASE) {
		controls.lTriggerOffset++;
		if(controls.lTriggerOffset > controls.triggerMax) {
			controls.lTriggerOffset = controls.triggerMax;
		}
	} else if(trigger == LTRIGGER && increase == DECREASE) {
		controls.lTriggerOffset--;
		if(controls.lTriggerOffset < controls.triggerMin) {
			controls.lTriggerOffset = controls.triggerMin;
		}
	} else if(trigger == RTRIGGER && increase == INCREASE) {
		controls.rTriggerOffset++;
		if(controls.rTriggerOffset > controls.triggerMax) {
			controls.rTriggerOffset = controls.triggerMax;
		}
	} else if(trigger == RTRIGGER && increase == DECREASE) {
		controls.rTriggerOffset--;
		if(controls.rTriggerOffset < controls.triggerMin) {
			controls.rTriggerOffset = controls.triggerMin;
		}
	}

	setLOffsetSetting(controls.lTriggerOffset);
	setROffsetSetting(controls.rTriggerOffset);

	if(controls.lTriggerOffset > 99) {
		btn.Ax = (uint8_t) (_floatOrigin + 100);
		btn.Cx = (uint8_t) (_floatOrigin + controls.lTriggerOffset-100);
	} else {
		btn.Cx = (uint8_t) (_floatOrigin + controls.lTriggerOffset);
	}
	if(controls.rTriggerOffset > 99) {
		btn.Ay = (uint8_t) (_floatOrigin + 100);
		btn.Cy = (uint8_t) (_floatOrigin + controls.rTriggerOffset-100);
	} else {
		btn.Cy = (uint8_t) (_floatOrigin + controls.rTriggerOffset);
	}

	clearButtons(250, btn, hardware);
}
void readJumpConfig(JumpConfig jumpConfig, ControlConfig &controls){
	Serial.print("setting jump to: ");
	if (controls.jumpConfig == jumpConfig) {
		controls.jumpConfig = DEFAULTJUMP;
		Serial.println("normal again");
	} else {
		controls.jumpConfig = jumpConfig;
		switch (jumpConfig) {
			case SWAP_XZ:
				Serial.println("X<->Z");
				break;
			case SWAP_YZ:
				Serial.println("Y<->Z");
				break;
			default:
				Serial.println("normal");
		}
	}
	setJumpSetting(controls.jumpConfig);
	setJump(controls);
}
void setJump(ControlConfig &controls){
	switch(controls.jumpConfig){
			case SWAP_XZ:
				controls.pinZSwappable = _pinX;
				controls.pinXSwappable = _pinZ;
				controls.pinYSwappable = _pinY;
				break;
			case SWAP_YZ:
				controls.pinZSwappable = _pinY;
				controls.pinXSwappable = _pinX;
				controls.pinYSwappable = _pinZ;
				break;
			default:
				controls.pinZSwappable = _pinZ;
				controls.pinXSwappable = _pinX;
				controls.pinYSwappable = _pinY;
	}
}
void nextTriggerState(WhichTrigger trigger, Buttons &btn, HardwareButtons &hardware, ControlConfig &controls) {
	if(trigger == LTRIGGER) {
		if(controls.lConfig >= controls.triggerConfigMax) {
			controls.lConfig = 0;
		} else {
			controls.lConfig = controls.lConfig + 1;
		}
	} else {
		if(controls.rConfig >= controls.triggerConfigMax) {
			controls.rConfig = 0;
		} else {
			controls.rConfig = controls.rConfig + 1;
		}
	}
	setLSetting(controls.lConfig);
	setRSetting(controls.rConfig);

	//if the modes are incompatible due to mode 5, make it show -100 on the stick that isn't mode 5
	//(user-facing mode 5)
	int lConfig = controls.lConfig;
	int rConfig = controls.rConfig;
	int triggerConflict = 0;
	if(rConfig == 4 && (lConfig == 0 || lConfig == 2 || lConfig == 3)) {
		triggerConflict = -100;
	}
	if(lConfig == 4 && (rConfig == 0 || rConfig == 2 || rConfig == 3)) {
		triggerConflict = -100;
	}
	//We want to one-index the modes for the users, so we add 1 here
	btn.Ay = (uint8_t) (_floatOrigin + triggerConflict);
	btn.Ax = (uint8_t) (_floatOrigin + controls.lConfig + 1);
	btn.Cy = (uint8_t) (_floatOrigin + triggerConflict);
	btn.Cx = (uint8_t) (_floatOrigin + controls.rConfig + 1);

	clearButtons(2000, btn, hardware);
}
void initializeButtons(Buttons &btn,int &startUpLa, int &startUpRa){
	//set the analog stick values to the chosen center value that will be reported to the console on startup
	//We choose 127 (_intOrigin) for this, and elsewhere we use an offset of 127.5 (_floatOrigin) truncated to int in order to round properly
	btn.Ax = _intOrigin;
	btn.Ay = _intOrigin;
	btn.Cx = _intOrigin;
	btn.Cy = _intOrigin;

	//read the ADC inputs for the analog triggers a few times and choose the startup value to be the maximum that was recorded
	//these values could be used as offsets to set particular trigger values
	startUpLa = 0;
	startUpRa = 0;
	for(int i = 0; i <64; i++){
		startUpLa = max(startUpLa,adc->adc0->analogRead(_pinLa)>>4);
		startUpRa = max(startUpRa,adc->adc0->analogRead(_pinRa)>>4);
	}
	//set the trigger values to this measured startup value
	btn.La = startUpLa;
	btn.Ra = startUpRa;

}
