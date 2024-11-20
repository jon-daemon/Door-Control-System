#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>
#include <Keypad.h>
#include "EEPROMex.h"
#include <SPI.h>
#include <MFRC522.h>

#define PIN_L 4 // PIN Length
#define PUK_L 8 // PUK Length
#define NUM_CARDS 5 // Number of RFID Cards to save

#define OPT_L 4 // Options Lenght, 1 more than actual length
char PIN[PIN_L + 1] = "0000"; // initial PIN - REPLACE AS SOON AS POSSIBLE (NO * OR #)
char PUK[PUK_L + 1] = "00000000"; // initial PUK - REPLACE AS SOON AS POSSIBLE (NO * OR #)
//KEEP options with 3 characters first and then 4 characters
//3 characters don't require PIN, 4 characters require PIN and parameters, allowed parameters 0,1-9999, not 0xxx
//Add new 3characters options before *B1A
//Add 4 characters options at the end and:
//	add min max parameters in MinMaxParams at the end,
//	save parameters in eeprom: 
//	add uint16_t address, define size in setup, read at end of readDataROM, clear at end of clearEEPROM
//add commands in /*COMMANDS for options*/
char * OPT[] = {
	"*A0", // Lights off
	"*A1", // Lights on
	"*00", // Reset Arduino
	"*B1A", // [1-9] triesAllowed
	"*B2A", // [1|0] wait_response
	"*B3A", // [1|0] showCodes
	"*A90", // [1|0] buzz
	"*C10", // [1000-9999] response_timout
	"*C20", // [1000-9999] info_screen_delay
	"*C30", // [1000-9999] active_input
	"*C40", // [1000-9999] LCD_idle_OFF
	"*DD0", // [0|1] EEPROM
	"*DBA", // [1-5] Write card mode
	"*DCD" // [1|0] enable/disbale card unlocking
};
int MinMaxParams[][2] = { // min and max parameters for options above
	{1, 9}, // "*C1D" [1-9] triesAllowed
	{0, 1}, // "*BC9" [1|0] wait_response
	{0, 1}, // "*D2B" [1|0] showCodes
	{0, 1}, // "*A90" [1|0] buzz
	{1000, 9999}, // "*B80" [1000-9999] response_timout
	{1000, 9999}, // "*D3B" [1000-9999] info_screen_delay
	{1000, 9999}, // "*D3C" [1000-9999] active_input
	{1000, 9999}, // "*D3D" [1000-9999] LCD_idle_OFF
	{0, 1}, // [0|1] EEPROM 0:clear, 1:read
	{1, NUM_CARDS}, // [1-5] Select card
	{0, 1} // [1|0] enable/disbale card unlocking
};
#define OPT_NUM (sizeof(OPT)/sizeof(char *)) //array size
#define OPT_PARAM sizeof(MinMaxParams)/sizeof(MinMaxParams[0]) // how many options require parameters
char dataPIN[PIN_L + 1]; // Data entered for PIN
char dataPUK[PUK_L + 1]; // Data entered for PUK
char optData[OPT_L + 1]; // Data entered for options

#define LCD_INTERVAL 200

const int Buzzer = 14;

LiquidCrystal_PCF8574 lcd(0x27);

const byte ROWS = 4;
const byte COLS = 4;
char hexaKeys[ROWS][COLS] = {
	{'1', '2', '3', 'A'},
	{'4', '5', '6', 'B'},
	{'7', '8', '9', 'C'},
	{'*', '0', '#', 'D'}
};
byte rowPins[ROWS] = {4, 3, 2, 17};
byte colPins[COLS] = {8, 7, 6, 5};
Keypad pressedKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

#define RST_PIN	9
#define SS_PIN	10
MFRC522 mfrc522(SS_PIN, RST_PIN);
byte knownID[NUM_CARDS][4];
byte tagID[4];
int cardEnable = 1;
int editCard = 0;
int currentCard = NUM_CARDS;
int cardMatched = 0;
int byteMatched = 0;

char *infoMessage;
char pressedKey;

int idle = 1;
int inputCodeMode = 1; // read from input
int dataIndex = 0; // index for input data
int inputCheck = 1; // 1: check for PIN/PUK 2:check for options
int unlockMode = 1; //1:unlocks door, 2:unlocks settings, 
int pinCorrect = 0; // 1 if PIN entered correctly
int newPIN = 0;
int doorOpen = 0;
int incomingMsg = 0;
int params = 10000;
int options = OPT_NUM;

unsigned long current;
unsigned long last_activity = 0;
unsigned long last_lcd_update = 0;
unsigned long info_screen_start = 0;
unsigned long last_buzz = 0;

//settings stored in eeprom
int unlockAttempts = 0;
int PINLocked = 0;
int triesAllowed = 3; // wrong PIN inputs before locked
int wait_response = 1; // wait for response from I^2C
int showCodes = 0;
int buzzing = 1;
uint32_t response_timout = 3000; // response from I^2C timout
uint32_t info_screen_delay = 3000;
uint32_t active_input = 3000; // keypad input active
uint32_t LCD_idle_OFF = 4000; // bigger than active_input

uint16_t unlockAttempts_address;
uint16_t PINLocked_address;
uint16_t pin_address;
uint16_t triesAllowed_address;
uint16_t wait_response_address;
uint16_t showCodes_address;
uint16_t buzzing_address;
uint16_t response_timout_address;
uint16_t info_screen_delay_address;
uint16_t active_input_address;
uint16_t LCD_idle_OFF_address;
uint16_t cards_address;
uint16_t cards_enable_address;

void setup(){
	Serial.begin(9600);
	//while (!Serial); // wait on Serial to be available on Leonardo
	SPI.begin();
	mfrc522.PCD_Init();		// Init MFRC522
	delay(4);				// Optional delay. Some board do need more time after init to be ready, see Readme
	mfrc522.PCD_DumpVersionToSerial();	// Show details of PCD - MFRC522 Card Reader details
	Serial.println(F("Scan PICC to see UID, SAK, type, and data blocks..."));
	Serial.println("check for LCD..");
	Wire.begin();
	Wire.beginTransmission(0x27);
	int error = Wire.endTransmission();
	if (error != 0) Serial.println("Error: LCD not found.");
	else{
		Serial.println("LCD found");
		lcd.init();
		lcd.begin(16, 2);
		lcd.setBacklight(255);
		lcd.clear();
	}

	pinMode(Buzzer, OUTPUT);
	buzz(2, 100, 100);

	// Define the EEPROM addresses
	pin_address = EEPROM.getAddress(sizeof(char)*(PIN_L));
	unlockAttempts_address = EEPROM.getAddress(sizeof(int));
	PINLocked_address = EEPROM.getAddress(sizeof(int));
	triesAllowed_address = EEPROM.getAddress(sizeof(int));
	wait_response_address = EEPROM.getAddress(sizeof(int));
	showCodes_address = EEPROM.getAddress(sizeof(int));
	buzzing_address = EEPROM.getAddress(sizeof(int));
	response_timout_address = EEPROM.getAddress(sizeof(long));
	info_screen_delay_address = EEPROM.getAddress(sizeof(long));
	active_input_address = EEPROM.getAddress(sizeof(long));
	LCD_idle_OFF_address = EEPROM.getAddress(sizeof(long));
	cards_address = EEPROM.getAddress(sizeof(byte)*4*(NUM_CARDS));
	cards_enable_address = EEPROM.getAddress(sizeof(int));

	readDataROM();

	sendCode(2,triesAllowed-unlockAttempts);
}


void loop(){
	current = millis();
	showLCD();
	checkKeypad();
	readUID();

	if (Serial.available() > 0) {
		incomingMsg = Serial.parseInt();
		Serial.println(incomingMsg,DEC);
		if ((!doorOpen) && (inputCheck == 1) && (pinCorrect)){
			if (incomingMsg == 9999) doorOpen = 1;
		}
	}
}

void(* resetFunc) (void) = 0;

void readUID(){

	if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
		//Serial.print(F(": Card UID:"));
		dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
		//Serial.println();

		//Serial.print(F("PICC type: "));
		MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
		//Serial.println(mfrc522.PICC_GetTypeName(piccType));

		for ( uint8_t i = 0; i < 4; i++) {
			tagID[i] = mfrc522.uid.uidByte[i];
		//	Serial.print("TagID: ");Serial.println(tagID[i],HEX);
		}

		if (editCard){
			for ( uint8_t i = 0; i < 4; i++) {
				knownID[currentCard][i] = tagID[i];
			}
			editCard = 0;

			for ( uint8_t i = 0; i < 4; i++) {
				EEPROM.writeByte(cards_address+currentCard*4+i,  knownID[currentCard][i]);
				//Serial.println(cards_address+currentCard*4+i);
			}

			showInfo("Card saved      ");
			//Serial.println("Card saved");
		}
		else if ((inputCodeMode == 1) && (idle) && (cardEnable)) {
			cardMatched = 0;
			for (uint8_t i = 0; i < NUM_CARDS; i++){
				if (!cardMatched){
					byteMatched = 1;
					while(byteMatched){
						for (uint8_t j = 0; j < 4; j++){
							if (tagID[j] != knownID[i][j]) byteMatched = 0;
						}

						if (byteMatched){
							byteMatched = 0;
							cardMatched = 1;
						}
					}
				}
			}
			if (!PINLocked){
				if (cardMatched){
					unlockAttempts = 0;
					EEPROM.writeInt(unlockAttempts_address, unlockAttempts);
					sendCode(2,triesAllowed-unlockAttempts);
					//Serial.print("write unlockAttempts ");
					//Serial.println(triesAllowed-unlockAttempts);
					showInfo("CORRECT Card    ");
					pinCorrect = 1;
					sendCode(1,1);
					if (buzzing) buzz(1, 30, 10);
				}
				else{
					++unlockAttempts;
					if (unlockAttempts > triesAllowed - 1){
						//unlockAttempts = 0;
						PINLocked = 1;
						EEPROM.writeInt(unlockAttempts_address, unlockAttempts);
						sendCode(2,triesAllowed-unlockAttempts);
						unlockAttempts = 0;
						//Serial.print("write unlockAttempts ");
						//Serial.println(triesAllowed-unlockAttempts);
						EEPROM.writeInt(PINLocked_address, PINLocked);
						showInfo("Code LOCKED     ");
					}
					else{
						EEPROM.writeInt(unlockAttempts_address, unlockAttempts);
						sendCode(2,triesAllowed-unlockAttempts);
						//Serial.print("write unlockAttempts ");
						//Serial.println(triesAllowed-unlockAttempts);
						showInfo("Unknown Card    ");
						pinCorrect = 0;
					}
					if (buzzing) buzz(3, 100, 100);
				}
			}
			last_activity = current;
		}
		else if ((idle) &&(!cardEnable)){
			idle = 0;
			showInfo("Cards Inactive");
			if (buzzing) buzz(2, 50, 100);
		}

		// Halt PICC
		mfrc522.PICC_HaltA();
		// Stop encryption on PCD
		mfrc522.PCD_StopCrypto1();
	}
}

void dump_byte_array(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
 //   Serial.print(buffer[i] < 0x10 ? " 0" : " ");
  //  Serial.print(buffer[i], HEX);
  }
}

void readDataROM(){

	unlockAttempts = EEPROM.readInt(unlockAttempts_address);
	if ((unlockAttempts < 0) || (unlockAttempts > triesAllowed)) unlockAttempts = 0;

	PINLocked = EEPROM.readInt(PINLocked_address);
	if ((PINLocked < 0) || (PINLocked > 1)) PINLocked = 0;

	triesAllowed = EEPROM.readInt(triesAllowed_address);
	if ((triesAllowed < MinMaxParams[0][0]) || (triesAllowed > MinMaxParams[0][1])) triesAllowed = 3;

	wait_response = EEPROM.readInt(wait_response_address);
	if ((wait_response < MinMaxParams[1][0]) || (wait_response > MinMaxParams[1][1])) wait_response = 1;

	showCodes = EEPROM.readInt(showCodes_address);
	if ((showCodes < MinMaxParams[2][0]) || (showCodes > MinMaxParams[2][1])) showCodes = 0;

	buzzing = EEPROM.readInt(buzzing_address);
	if ((buzzing < MinMaxParams[3][0]) || (buzzing > MinMaxParams[3][1])) buzzing = 1;

	response_timout = EEPROM.readLong(response_timout_address);
	if ((response_timout < MinMaxParams[4][0]) || (response_timout > MinMaxParams[4][1])) response_timout = 3000;

	info_screen_delay = EEPROM.readLong(info_screen_delay_address);
	if ((info_screen_delay < MinMaxParams[5][0]) || (info_screen_delay > MinMaxParams[5][1])) info_screen_delay = 3000;

	active_input = EEPROM.readLong(active_input_address);
	if ((active_input < MinMaxParams[6][0]) || (active_input > MinMaxParams[6][1])) active_input = 3000;

	LCD_idle_OFF = EEPROM.readLong(LCD_idle_OFF_address);
	if ((LCD_idle_OFF < MinMaxParams[7][0]) || (LCD_idle_OFF > MinMaxParams[7][1])) LCD_idle_OFF = 4000;

	EEPROM.readBlock<char>(pin_address, PIN, PIN_L);

	for (uint8_t i = 0; i < NUM_CARDS; i++){
		for (uint8_t j = 0; j < 4; j++){
			knownID[i][j] = EEPROM.readByte(cards_address+(i*4)+j);
			//Serial.print(i);Serial.print("-");Serial.print(j);Serial.print(":");Serial.println(knownID[i][j],HEX);
		}
	}

	cardEnable = EEPROM.readInt(cards_enable_address);
	if ((cardEnable < MinMaxParams[10][0]) || (cardEnable > MinMaxParams[10][1])) cardEnable = 1;
}

void clearEEPROM(){
	EEPROM.writeInt(unlockAttempts_address, 0);
	EEPROM.writeInt(PINLocked_address, 0);
	EEPROM.writeInt(triesAllowed_address, 0);
	EEPROM.writeInt(wait_response_address, 0);
	EEPROM.writeInt(showCodes_address, 0);
	EEPROM.writeInt(buzzing_address, 0);
	EEPROM.writeLong(response_timout_address, 0);
	EEPROM.writeLong(info_screen_delay_address, 0);
	EEPROM.writeLong(active_input_address, 0);
	EEPROM.writeLong(LCD_idle_OFF_address, 0);
	EEPROM.writeBlock<char>(pin_address, "0000", PIN_L);
	for (uint8_t i = 0; i < 4*NUM_CARDS; i++) {
		EEPROM.writeByte(cards_address+i, i);
	}
	EEPROM.writeInt(cards_enable_address, 1);
}

void initOptions(){
	idle = 1;
	inputCodeMode = 1;
	inputCheck = 1;
	unlockMode = 1;
	pinCorrect = 0;
	newPIN = 0;
	doorOpen = 0;
	incomingMsg = 0;
	params = 10000;
	options = OPT_NUM;
	while(dataIndex != 0){
		dataIndex--;
		if (dataIndex < (PIN_L + 1)) dataPIN[dataIndex] = 0;
		if (dataIndex < (PUK_L + 1)) dataPUK[dataIndex] = 0;
		if (dataIndex < (OPT_L + 1)) optData[dataIndex] = 0;
	}
	lcd.clear();
	lcd.setCursor(0,0);
}

void showInfo(char *info){
	infoMessage = info;
	info_screen_start = current;
	inputCodeMode = 0;
}

void buzz(int times, unsigned int onTime, unsigned int offTime){
	int i = 0;
	last_buzz = current;
	while (i < times){
		if ((unsigned long)(millis() - last_buzz) < onTime){
			digitalWrite(Buzzer, HIGH);
		}
		else{
			digitalWrite(Buzzer, LOW);
			if ((unsigned long)(millis() - last_buzz) > onTime + offTime){
				last_buzz = millis();
				i++;
			}
		}
	}
}

void sendCode(int code1, int code2){
	if (code1 == 1){
		if (code2 == 1) Serial.println("Op3nD00r");
		else if (code2 == 2) Serial.println("lightsoff");
		else if (code2 == 3) Serial.println("lightson");
	}
	else if (code1 == 2){
		Serial.println(code2);
	}
	delay(50);
}

void checkKeypad(){

	if ((!idle) && (PINLocked == 0) && (newPIN == 0)){
		if ((unsigned long)(current - last_activity) > active_input){
			idle = 1;
			last_activity = current;
			initOptions();
		}
	}

	if (inputCodeMode) {
		pressedKey = pressedKeypad.getKey();
		if (pressedKey){
			idle = 0;
			if (buzzing) buzz(1, 30, 10);

			if (!PINLocked){
				if (newPIN == 1){ // set new PIN after correct PUK
					if ((pressedKey != '*') && (pressedKey != '#')){
						dataPIN[dataIndex] = pressedKey;
						PIN[dataIndex] = dataPIN[dataIndex];
						if(++dataIndex == PIN_L){
							EEPROM.writeBlock<char>(pin_address, dataPIN, PIN_L);
							//Serial.println("write PIN");
							EEPROM.writeInt(PINLocked_address, PINLocked);
							//Serial.println("write PINLocked");
							newPIN = 0;
							showInfo("New Code is set");
							unlockAttempts = 0;
							EEPROM.writeInt(unlockAttempts_address, unlockAttempts);
							sendCode(2,triesAllowed-unlockAttempts);
						}
					}
				}
				else{
					if ((pressedKey == '*') && (!editCard)){
						if (dataIndex == 0) {inputCheck = 2; lcd.clear();}
						else initOptions();
					}

					if ((inputCheck == 1) && (pressedKey != '*') && (pressedKey != '#')){
						if (editCard){
							if (pressedKey == 'D'){
								editCard = 0;
								for ( uint8_t i = 0; i < 4; i++) {
									knownID[currentCard][i] = 0;
									EEPROM.writeByte(cards_address+currentCard*4+i,  knownID[currentCard][i]);
								}
								showInfo("Card Deleted  ");
							}
							else if (pressedKey == 'B'){
								initOptions();
								editCard = 0;
							}
						}
						else{
							dataPIN[dataIndex++] = pressedKey;
							if(dataIndex == PIN_L){
								if(!strcmp(dataPIN, PIN)){
									unlockAttempts = 0;
									EEPROM.writeInt(unlockAttempts_address, unlockAttempts);
									sendCode(2,triesAllowed-unlockAttempts);
									//Serial.print("write unlockAttempts ");
									//Serial.println(triesAllowed-unlockAttempts);
									showInfo("CORRECT Code    ");
									pinCorrect = 1;
									if (unlockMode == 1) sendCode(1,1);
									else{  // options with 4 characters (required pin)
										if (params > 9999) showInfo("No Parameters   ");
										else{
											if ((params < MinMaxParams[options - OPT_NUM + OPT_PARAM][0]) || (params > MinMaxParams[options - OPT_NUM + OPT_PARAM][1]))	showInfo("Wrong Parameters");
											else{
												int val = 0;
	/*COMMANDS for options*/					if (options < ++val + OPT_NUM - OPT_PARAM){
													triesAllowed = params;
													EEPROM.writeInt(triesAllowed_address, triesAllowed);
												//	Serial.println("triesAllowed PINLocked");
													showInfo("# Tries Allowing");
												}
												else if (options < ++val + OPT_NUM - OPT_PARAM){
													wait_response = params;
													EEPROM.writeInt(wait_response_address, wait_response);
												//	Serial.println("wait_response PINLocked");
													showInfo("# Wait Response ");
												}
												else if (options < ++val + OPT_NUM - OPT_PARAM){
													showCodes = params;
													EEPROM.writeInt(showCodes_address, showCodes);
												//	Serial.println("showCodes PINLocked");
													showInfo("# Show Codes Set");
												}
												else if (options < ++val + OPT_NUM - OPT_PARAM){
													buzzing = params;
													EEPROM.writeInt(buzzing_address, buzzing);
												//	Serial.println("buzzing PINLocked");
													showInfo("# Buzzer Set    ");
												}
												else if (options < ++val + OPT_NUM - OPT_PARAM){
													response_timout = params;
													EEPROM.writeLong(response_timout_address, response_timout);
												//	Serial.println("response_timout PINLocked");
													showInfo("# Response Time ");
												}
												else if (options < ++val + OPT_NUM - OPT_PARAM){
													info_screen_delay = params;
													EEPROM.writeLong(info_screen_delay_address, info_screen_delay);
												//	Serial.println("info_screen_delay PINLocked");
													showInfo("# Info Idle Set  ");
												}
												else if (options < ++val + OPT_NUM - OPT_PARAM){
													active_input = params;
													EEPROM.writeLong(active_input_address, active_input);
												//	Serial.println("active_input PINLocked");
													showInfo("# Input Active  ");
												}
												else if (options < ++val + OPT_NUM - OPT_PARAM){
													LCD_idle_OFF = params;
													EEPROM.writeLong(LCD_idle_OFF_address, LCD_idle_OFF);
												//	Serial.println("LCD_idle_OFF PINLocked");
													showInfo("# LCD Idle Set  ");
												}
												else if (options < ++val + OPT_NUM - OPT_PARAM){
													if (params < 1){ clearEEPROM(); showInfo("-EEPROM cleared-");}
													else { readDataROM(); showInfo("Read EEPROM...  "); }
												}
												else if (options < ++val + OPT_NUM - OPT_PARAM){
													editCard = 1;
													currentCard = params - 1;
												}
												else if (options < ++val + OPT_NUM - OPT_PARAM){
													cardEnable = params;
													EEPROM.writeInt(cards_enable_address, cardEnable);
													if (cardEnable) showInfo("# Cards Active  ");
													else showInfo("# Cards Inactive");
												}
												else showInfo("Error in Settings");
											}
										}
									}
								}
								else{
									++unlockAttempts;
									if (unlockAttempts > triesAllowed - 1){
										//unlockAttempts = 0;
										PINLocked = 1;
										EEPROM.writeInt(unlockAttempts_address, unlockAttempts);
										sendCode(2,triesAllowed-unlockAttempts);
										unlockAttempts = 0;
										//Serial.print("write unlockAttempts ");
										//Serial.println(triesAllowed-unlockAttempts);
										EEPROM.writeInt(PINLocked_address, PINLocked);
										//Serial.println("write PINLocked");
										showInfo("Code LOCKED     ");
									}
									else{
										EEPROM.writeInt(unlockAttempts_address, unlockAttempts);
										sendCode(2,triesAllowed-unlockAttempts);
										//Serial.print("write unlockAttempts ");
										//Serial.println(triesAllowed-unlockAttempts);
										showInfo("WRONG Code      ");
										pinCorrect = 0;
									}
									if (buzzing) buzz(3, 50, 200);
								}
							}
						}
					}
					else if (inputCheck == 2){ //checking options
						///
						if ((dataIndex > 2) && (pressedKey == '#')) { // accept options with 3 digits and above
							for (int i = 0; i < OPT_NUM; i++){
								if (options > OPT_NUM - 1){
									if (!strcmp(optData, OPT[i])){
										options = i;
										if (dataIndex < OPT_L){
/*COMMANDS for options*/					switch (options) { // options with 3 charaters, no parameters
												case 0:
													showInfo("Lights OFF");
													sendCode(1,2);
													break;
												case 1:
													showInfo("Lights ON");
													sendCode(1,3);
													break;
												case 2:
													resetFunc(); //call reset
													break;
												default:
													break;
											}
										}
										else { // options with 4 characters
											inputCheck = 1;
											unlockMode = 2;
											while(dataIndex != 0){
												dataIndex--;
												if (dataIndex < (OPT_L + 1)) optData[dataIndex] = 0;
											}
											//Serial.print("options: ");Serial.println(options);
										}
									}
									else{
										//Serial.println("wrong");
									}
								}
							}

							if (options > OPT_NUM - 1) showInfo("Unknown Command");
						}
						else if ((dataIndex < OPT_L) && (pressedKey != '#')){
							optData[dataIndex++] = pressedKey;
							//Serial.print("optData: ");Serial.println(optData[dataIndex - 1]);
						}
						else if ((dataIndex > (OPT_L -1)) && (dataIndex < (OPT_L + 4))){
							if ((params != 0) && (pressedKey != 'A') && (pressedKey != 'B') && (pressedKey != 'C') && (pressedKey != 'D')){
								int multiPara = 0;
								switch (pressedKey) {
									case '0': multiPara = 0; break;
									case '1': multiPara = 1; break;
									case '2': multiPara = 2; break;
									case '3': multiPara = 3; break;
									case '4': multiPara = 4; break;
									case '5': multiPara = 5; break;
									case '6': multiPara = 6; break;
									case '7': multiPara = 7; break;
									case '8': multiPara = 8; break;
									case '9': multiPara = 9; break;
								}
								if (dataIndex < (OPT_L + 1)) params = multiPara;
								else params = multiPara + params * 10;
								dataIndex++;
							}
						}
					}
				}
			}
			else{ //check for PUK
				if ((pressedKey != '*') && (pressedKey != '#')){
					dataPUK[dataIndex++] = pressedKey;
					if(dataIndex == PUK_L){
						if(!strcmp(dataPUK, PUK)){
							PINLocked = 0;
							newPIN = 1;
							showInfo("CORRECT Master");
						}
						else{
							showInfo("WRONG Master");
							if (buzzing) buzz(3, 50, 200);
						}
					}
				}
			}

			last_activity = current;
		}
	}

	else{
	}
}

void showLCD(){
	if ((unsigned long)(current - last_lcd_update) > LCD_INTERVAL) {
		if ((unsigned long)(current - last_activity) > LCD_idle_OFF){
			lcd.clear();
			lcd.setBacklight(0);
		}
		else{
			lcd.setBacklight(255);
			lcd.home();

			if (inputCodeMode){ // active keypad
				if (inputCheck == 1){ //checking PIN/PUK
					if (editCard){
						lcd.print("Swipe card No #");lcd.print(currentCard + 1);
						lcd.setCursor(0,1);
						lcd.print("D(elete)  B(ack)");
						last_activity = current;
					}
					else{
						if (!PINLocked){
							if (newPIN == 1){
								lcd.print("Enter new Code");
								lcd.setCursor(0,1);
								if (showCodes) lcd.print(dataPIN);
								else{ for (int i = 0; i < dataIndex; i++) {lcd.print("*");}}
								last_activity = current;
							}
							else{
								if (unlockMode == 1) lcd.print("Unlock Door ");
								else if (unlockMode == 2) lcd.print("Unclock Settings");
								lcd.setCursor(0,1);
								if (showCodes) lcd.print(dataPIN);
								else{ for (int i = 0; i < dataIndex; i++) {lcd.print("*");}}
							}
						}
						else{
							lcd.print("Enter Master");
							lcd.setCursor(0,1);
							if (showCodes) lcd.print(dataPUK);
							else{ for (int i = 0; i < dataIndex; i++) {lcd.print("*");}}
						}
					}
				}
				else if (inputCheck == 2){ /// print options
					lcd.print(optData);
					if ((dataIndex > 3) && (params < 10000)) lcd.print(params);
				}
			}
			else{ //inactive keypad
				if ((unlockMode == 1) && (pinCorrect) && (wait_response)){
					if (!doorOpen){
						if ((unsigned long)(current - info_screen_start) < response_timout){
							lcd.print("Unlocking...");
							lcd.setCursor(0,1);
							if (showCodes) lcd.print(dataPIN);
							else{ for (int i = 0; i < dataIndex; i++) {lcd.print("*");}}
							last_activity = current;
							idle = 1;
						}
						else{
							last_activity = current;
							pinCorrect = 0;
							showInfo("No response");
						}
					}
					else{
						last_activity = current;
						pinCorrect = 0;
						if (buzzing) buzz(2, 30, 100);
						showInfo("Door is Open");
					}
				}
				else if ((unsigned long)(current - info_screen_start) < info_screen_delay){
					lcd.print(infoMessage);
					if (inputCheck == 1){
						if (!PINLocked){
							if (newPIN == 1){
								lcd.setCursor(0,1);
								if (showCodes) lcd.print(dataPUK);
								else{ for (int i = 0; i < dataIndex; i++) {lcd.print("*");}}
							}
							else if (unlockAttempts > 0){
								lcd.setCursor(0,1);
								if (showCodes) lcd.print(dataPIN);
								else{ for (int i = 0; i < dataIndex; i++) {lcd.print("*");}}
								lcd.print(" ");
								lcd.print(triesAllowed-unlockAttempts);
								lcd.print("/");
								lcd.print(triesAllowed);
								lcd.print(" Left");

							}
							else{
								lcd.setCursor(0,1);
								if (showCodes) lcd.print(dataPIN);
								else{ for (int i = 0; i < dataIndex; i++) {lcd.print("*");}}
							}
						}
						else{
							if (unlockAttempts == triesAllowed){
								unlockAttempts = 0;
								lcd.setCursor(0,1);
								if (showCodes) lcd.print(dataPIN);
								else{ for (int i = 0; i < dataIndex; i++) {lcd.print("*");}}
							}
							else{
								lcd.setCursor(0,1);
								if (showCodes) lcd.print(dataPUK);
								else{ for (int i = 0; i < dataIndex; i++) {lcd.print("*");}}
							}
						}
					}
					else{
					}
				}
				else if ((!PINLocked)){
					if (newPIN == 0) initOptions();
					else{
						inputCodeMode = 1;
						while(dataIndex != 0){
							dataIndex--;
							if (dataIndex < (PUK_L + 1)) dataPUK[dataIndex] = 0;
						}
						lcd.setCursor(0,1);
						lcd.print("                ");
					}
				}
				else{
					initOptions();
					last_activity = current;
				}
			}
		}

		last_lcd_update = current;
	}
}