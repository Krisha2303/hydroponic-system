/*
*  DROMatic.ino
*  DROMatic OS Core
*  Devin R. Olsen - Dec 31, 2016
*  devin@devinrolsen.com
*/

#include "Core.h"
#include "Crops.h"
#include "Channels.h"
#include "Sessions.h"
#include "Menus.h"
#include "Screens.h"
#include "DatesTime.h"

int Key;
const int stepsPerRevolution = 100;
const int stepperSpeed = 800;
Stepper myStepper(stepsPerRevolution, 15, 14);
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
DS3231  rtc(SDA, SCL);

int minPPM = 1200;
int maxPPM = 1600;

JsonObject& getCoreData(JsonBuffer& b){
	tmpFile = SD.open("dromatic/core.dro");
	JsonObject& d = b.parseObject(tmpFile.readString());
	tmpFile.close();
	return d;
}

void setCoreData(JsonObject& d){
	char b[256];
	tmpFile = SD.open("dromatic/core.dro", O_WRITE | O_TRUNC);
	d.printTo(b, sizeof(b));
	tmpFile.print(b);
	tmpFile.close();
}

void coreInit(){
	if (SD.exists("dromatic")){ //has OS already been setup?
		DynamicJsonBuffer coreBuffer;
		JsonObject& coreData = getCoreData(coreBuffer);

		cropName = coreData["crop"].asString();
		if (cropName != "" && SD.exists("dromatic/" + cropName)){ //Loading up exisiting core file's crop directory
			screenName = "";
			openHomeScreen();
			tmpFile = SD.open("dromatic/" + cropName);
			getDirectoryMenus(tmpFile);
			tmpFile.close();
		}
		else{ //we have core file with crop, but no crop directory. //VERY CORNER CASE!
			startNewCrop();
		}
	}
	else { //if OS has not been setup, lets build out core OS file / directory
		SD.mkdir("dromatic");
		lcd.print(F("New Drive Found!"));
		lcd.setCursor(0, 1);
		lcd.print(F(" Please Hold... "));
		tmpFile = SD.open("dromatic/core.dro", FILE_WRITE);
		char buffer[64];
		DynamicJsonBuffer coreBuffer;
		JsonObject& settings = coreBuffer.createObject();
		settings["crop"] = "";
		settings.printTo(buffer, sizeof(buffer));
		tmpFile.print(buffer);
		tmpFile.close();

		lcd.clear();
		lcd.home();
		lcd.print(F("New Crop Setup"));
		lcd.setCursor(0, 1);
		lcd.print(F(" Please Hold... "));
		delay(1000);
		lcd.clear();
		startNewCrop();
	}
}

void setPHRange(double dir){
	String min, max;
	float minMaxDiff = 0.01;

	if ((dir == 1) ? cursorX == 3 : cursorX == 9){
		(dir == 1) ? tmpFloats[0] = tmpFloats[0] + minMaxDiff : tmpFloats[1] = tmpFloats[1] - minMaxDiff;
		if ((dir == 1) ? tmpFloats[0] > (tmpFloats[1] - minMaxDiff) : tmpFloats[1] < (tmpFloats[0] + minMaxDiff)){
			(dir == 1) ? tmpFloats[1] = tmpFloats[1] + minMaxDiff : tmpFloats[0] = tmpFloats[0] - minMaxDiff;
		}
	}
	if ((dir == 1) ? cursorX == 9 : cursorX == 3) {
		(dir == 1) ? tmpFloats[1] = tmpFloats[1] + minMaxDiff : tmpFloats[0] = tmpFloats[0] - minMaxDiff;
	}
	min = String(tmpFloats[0]);
	max = String(tmpFloats[1]);

	lcd.clear();
	lcd.print(min);
	lcd.write(byte(1));
	lcd.print(F(" "));
	lcd.print(max);
	lcd.write(byte(0));
	lcd.print(F(" PH"));
	lcd.setCursor(0, 1);
	lcd.print(F("<back>    <next>"));
	lcd.setCursor(cursorX, 0);
}

void setPPMRangeValues(int dir){
	if ((dir == 1) ? cursorX == 3 : cursorX == 8){
		(dir == 1) ? minPPM = minPPM + 10 : maxPPM = maxPPM - 10;
		if ((dir == 1) ? minPPM > (maxPPM - 50) : maxPPM < (minPPM + 50)){
			(dir == 1) ? maxPPM = maxPPM + 10 : minPPM = minPPM - 10;
		}
	}
	if ((dir == 1) ? cursorX == 8 : cursorX == 3) {
		(dir == 1) ? maxPPM = maxPPM + 10 : minPPM = minPPM - 10;
	}

	lcd.clear();
	lcd.print(String(minPPM) + F("-") + String(maxPPM) + F(" EC/PPM"));
	lcd.setCursor(0, 1);
	lcd.print(F("<back>      <ok>"));
	lcd.setCursor(cursorX, 0);
}

void makeNewFile(String path, JsonObject& data){
	char buffer[1024];
	tmpFile = SD.open(path, FILE_WRITE);
	data.printTo(buffer, sizeof(buffer));
	tmpFile.print(buffer);
	tmpFile.close();
	Serial.flush();
}

//Pump Functions
void turing(){
	captureDateTime();
	byte valid = 1;
	byte progressSession = 0;
	int i, j,
		setDOY, currentDOY,
		setYear, currentYear,
		setMonth, currentMonth,
		setDay, currentDay,
		setDOW, currentDOW,
		setHour, currentHour,
		setMin, currentMin,
		repeatCount, repeatedCount, repeatType,
		setAmount, setCalibration, setSize,
		id, calib, direction, totalChannels;

	StaticJsonBuffer<256> cropBuffer;
	JsonObject& cropData = getCropData(cropBuffer);
	totalChannels = cropData["totalChannels"];

	//We start by looping over channesl
	for (i = 1; i < totalChannels; i++){
		if (analogRead(0) >= 0 && analogRead(0) <= 650){
			break;
		}
		StaticJsonBuffer<256> channelBuffer;
		JsonObject& channel = getChannelData(channelBuffer, i);
		int channelSessionTotal = channel["sessionsTotal"];
		for (j = 1; j < channelSessionTotal + 1; j++){
			if (analogRead(0) >= 0 && analogRead(0) <= 650){
				break;
			}
			valid = true; //set a validation flag to true
			StaticJsonBuffer<256> sessionBuffer;
			JsonObject& session = getSessionData(sessionBuffer, i, j);
			JsonArray& sessionDate = session["date"];
			JsonArray& sessionTime = session["time"];

			if (session["expried"] == true) continue; //lets skip this session if it has already expired.

			//Capture session's set data
			setYear = sessionDate[0];
			setMonth = sessionDate[1];
			setDay = sessionDate[2];
			setDOW = sessionDate[3];
			setDOY = calculateDayOfYear(setDay, setMonth, setYear);
			setHour = sessionTime[0];
			setMin = sessionTime[1];
			setAmount = session["amount"];
			setCalibration = channel["calibration"];
			setSize = channel["size"];
			repeatCount = session["repeat"];
			repeatedCount = session["repeated"];
			repeatType = session["repeatBy"];
			id = session["id"];

			//Capture current date/time data
			currentYear = tmpInts[0];
			currentMonth = tmpInts[1];
			currentDay = tmpInts[2];
			currentDOW = tmpInts[3];
			currentDOY = calculateDayOfYear(currentDay, currentMonth, currentYear);
			currentHour = tmpInts[4];
			currentMin = tmpInts[5];
			
			lcd.home(); //move lcd cursor to 0,0

			//Validation of session date time
			if (repeatType > 0){ //if session is set to repeat, we validate uniquely per repeatType
				if (repeatType == 1){ //hourly
					if (currentHour != setHour && currentMin < setMin){
						valid = 0;
					}
				}
				if (repeatType == 2){ //daily
					if (currentDay != setDay && currentHour != setHour && currentMin < setMin){
						valid = 0;
					}
				}
				if (repeatType == 3){ //weekly
					if (currentDOW != setDOW && currentHour != setHour && currentMin < setMin){
						valid = 0;
					}
				}
				if (repeatType == 4){ //monthly
					if (currentMonth != setMonth && currentDay != setDay && currentHour != setHour && currentMin < setMin){
						valid = 0;
					}
				}
				if (repeatType == 5){ //yearly
					if (currentDOY != setDOY && currentHour != setHour && currentMin < setMin){
						valid = 0;
					}
				}
			}
			else{ //if no repeat type is set, validation is little simpler
				if (setYear < currentYear){	//year
					valid = 0;
				}

				if (setMonth < currentMonth){ //month
					valid = 0;
				}

				if (setDay < currentDay){     //day
					valid = 0;
				}

				if (setHour < currentHour){	  //hour
					valid = 0;
				}

				if (setMin < currentMin){	//min (in no right setup would you ever need a dose to repeat, nor is the physicaly possible every minute)
					valid = 0;
				}
			}

			//LET THE DOSING BEGIN!!
			
			if (valid == 1){
				//We don't have a repeating session
				if (repeatType == 0){ //repeat type is = to none (most basic type of session)
					pumpSpin(setAmount, setCalibration, setSize, i); //do pump spin
					session["expried"] = true; //set session to expired
					setSessionData(session, j, false);
				}
				//We have a repeating session
				if (repeatType > 0){
					if (repeatCount < 0){ //repeat count is = to infinite, so we only pump
						pumpSpin(setAmount, setCalibration, setSize, i); //do pump spin
						progressSession = 1;
					}

					if(repeatCount > 0){
						repeatedCount = ((repeatedCount - 1) > 0) ? repeatedCount - 1 : 0;
						session["repeated"] = repeatedCount;
						if (repeatedCount == 0){
							//lets expire session
							session["expired"] = true;
						} else {
							progressSession = 1;
						}
						setSessionData(session, j, false);
						pumpSpin(setAmount, setCalibration, setSize, i); //do pump spin
					}
				}
			}

			//If the session is of repeating type, and needs to be repeated further 
			//we progress the session further
			if (progressSession == 1){
				if (repeatType == 1){ //hourly
					setHour = (setHour + 1 > 23) ? 0 : setHour + 1; //Push to next hour

				}
				if (repeatType == 2){ //daily
					setDay = (setDay + 1 > days[setMonth]) ? 0 : setDay + 1; //Push to next day of month
				}
				if (repeatType == 4){ //monthly
					setMonth = (setMonth + 1 > 11) ? 0 : setMonth + 1; //Push to next month
				}
				if (repeatType == 5){ //yearly
					setYear = setYear + 1; //Push to next year
				}
			}
		}
	}
}

void RelayToggle(int channel, bool gate) {
	if (gate == true){
		switch (channel){
		case 1:
			digitalWrite(RELAY1, LOW);
			break;
		case 2:
			digitalWrite(RELAY2, LOW);
			break;
		case 3:
			digitalWrite(RELAY3, LOW);
			break;
		case 4:
			digitalWrite(RELAY4, LOW);
			break;
		case 5:
			digitalWrite(RELAY5, LOW);
			break;
		case 6:
			digitalWrite(RELAY6, LOW);
			break;
		case 7:
			digitalWrite(RELAY7, LOW);
			break;
		case 8:
			digitalWrite(RELAY8, LOW);
			break;
		case 9:
			digitalWrite(RELAY9, LOW);
			break;
		case 10:
			digitalWrite(RELAY10, LOW);
			break;
		}
	}
	if (gate == false){
		switch (channel){
		case 1:
			digitalWrite(RELAY1, HIGH);
			break;
		case 2:
			digitalWrite(RELAY2, HIGH);
			break;
		case 3:
			digitalWrite(RELAY3, HIGH);
			break;
		case 4:
			digitalWrite(RELAY4, HIGH);
			break;
		case 5:
			digitalWrite(RELAY5, HIGH);
			break;
		case 6:
			digitalWrite(RELAY6, HIGH);
			break;
		case 7:
			digitalWrite(RELAY7, HIGH);
			break;
		case 8:
			digitalWrite(RELAY8, HIGH);
			break;
		case 9:
			digitalWrite(RELAY9, HIGH);
			break;
		case 10:
			digitalWrite(RELAY10, HIGH);
			break;
		}
	}
}

void pumpSpin(int setAmount, int setCalibration, int channelSize, int channelNumber){

	RelayToggle(channelNumber, true); //turn channel gate power on

	if (channelSize > 0){ //fixed channel system (aka syrings or not)?
		vector<long int> pumpSessions;
		long int totalRotations = stepsPerRevolution * setCalibration; //7400
		long double intPart, fractionPart, pumpingRounds;
		pumpingRounds = (float)setAmount / (float)channelSize; // 2.35 (235ml)
		fractionPart = pumpingRounds - (long)pumpingRounds; // 0.35
		intPart = pumpingRounds - fractionPart; // 2.0
		fractionPart = 1 / fractionPart; //2.857142857142857

		for (int i = 0; i < intPart; i++){
			pumpSessions.push_back(totalRotations); //7400
		}
		pumpSessions.push_back(floor(totalRotations / fractionPart)); //2590

		for (int j = 0; j < pumpSessions.size(); j++){
			int turnCounts = (pumpSessions[j] / channelSize);
			int reverseCount = turnCounts;
			while (turnCounts--){
				myStepper.step(-800); //pull fluids
				lcd.clear();
				lcd.print(turnCounts);
				if (turnCounts == 0){
					while (reverseCount--){
						myStepper.step(800); //push fluids

					}
				}
			}
		}
	}
	else { //no fixed channel size but a fixed configuration size of 100ml (aka flow control system or pump)
		myStepper.step(stepsPerRevolution * (setAmount / setCalibration));
	}

	RelayToggle(channelNumber, false); //turn channel gate power on
}