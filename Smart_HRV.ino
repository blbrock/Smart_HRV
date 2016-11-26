#include <SoftwareSerial.h>
#include <avr/wdt.h>;
/*
  Switched to DHT library developed by Rob Tillaart
  (https://github.com/RobTillaart/Arduino/tree/master/libraries/DHTlib)
  This library reads both temperature and humidity from
  a sensor with a single poll.  This will hopefully solve a
  problem with the outdoor reference sensor hanging after several hours of use.
  This hanging appears to be caused by timing issues with double polling to extract
  both temp and humidity readings from that sensor.
*/
#include <dht.h>
#include <EEPROM.h>

dht DHT;

#define mBathPIN 4   // Master bath pin
#define gBathPIN 5   // Guest bath pin
#define refPIN 6   // Outside reference pin

byte cmd = 4;
byte last_cmd;
int RxByte;
bool autoState = true;
const int dehumPin = 2;
int dehumPinState = 0;
int dhp = 1;
const int fanPin = 3;
int fanPinState = 0;
// int fp = 1;
int dhtState = 0;
int minTempThresh = 15;
//float setpointOn = 60;
//float setpointOff = 55;
//float humOn = setpointOn;
//float humOff = setpointOff;
//float hMin = setpointOff; // Intitialize minimum humidity that can be obtained
float rft;
//int humTarget = 55; // moved to local variable to free memory
byte prehumcmd = 0;
int debug = 0;
String dataList = "";

SoftwareSerial TxSerial(10, 11); // RX, TX

void setup() {

  Serial.begin(300);
  TxSerial.begin(9600);
  TxSerial.println("Serial communication initialized");

  pinMode(13, OUTPUT);
  digitalWrite(13, LOW);    // turn the LED off by making the voltage LOW to save a little energy
  pinMode(dehumPin, INPUT_PULLUP);
  pinMode(fanPin, INPUT_PULLUP);

  int chk = DHT.read21(refPIN);
  if (chk == 0 ) {
    rft = DHT.temperature;
  }
  // Convert rft to Fahrenheit
  rft = rft * 1.8 + 32;

  // Initialize startup handshake
  startup();
  cmd = 0; //Reset cmd and last_cmd after startup
  last_cmd = 0;
  wdt_enable(WDTO_8S);
}

void loop() {
  if (debug >= 4) {
    TxSerial.print("Start Loop, cmd = ");
    TxSerial.println(cmd);
    TxSerial.print("last_cmd: ");
    TxSerial.println(last_cmd);
  }
  dataList = "";
  wdt_reset();
  // Get commands from HRV
  RxByte = read_Tx(); // Read any incoming signals from HRV unit
  // Capture unknown commands
  if (RxByte != 0 and RxByte != 220 and RxByte != 255 and RxByte != 92 and RxByte != 28 and RxByte != 188) {
    Capture();
  }

  // Check relay pins for status of thermostat calls
  if (autoState) {
    CheckRelays(rft);
    // }
    SetRelays();
  }

  // Humidity sensors take precedent over relays or HRV/Timer commands
  rft = CheckHumidity();

  /* Get commands from TxSerial monitor and send as digital byte
    to HRV. Note that placement of TxSerial read here allows manual commands to
    override any programatic commands set earlier in the loop */
  ManCmd();

  // Execute Commands
  ExecCmds();

  delay(2000);
  if (cmd <= 4) {
    Debug();
    delay(2000);
  }
  // Send data to android via bluetooth
  if (debug == 0) {
    dataList = dataList + String(last_cmd) + ',';
    TxSerial.println(dataList);
  }
  Serial.flush(); // flush any bytes remainining in sending buffer

 // If manual mode lasts > 4 hours, return to automatic mode
  if (!autoState) {
    unsigned long startTime = 0;
    startTime = millis();
    if (millis() - startTime > 14400000) {
      cmd = 236;
      autoState = true;
      startTime = 0;
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////// FUNCTIONS //////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Read Serial communications
byte read_Tx() {
  delay(100); // Wait to allow time to receive signal time delay value seems critical
  if (Serial.available() > 0) {
    RxByte = Serial.read();
    if (debug > 0) {
      TxSerial.print("Rx: ");
      TxSerial.println(RxByte, DEC);
    }
  }
  return RxByte;
}

// Write Serial communications
void write_Tx(byte cmd) {
  Serial.write(cmd);
  if (debug > 0) {
    TxSerial.print("Tx: ");
    TxSerial.println(cmd);
  }
}

////////////////////////////////// Run initial handshake on system startup //////////////////////////////////////
void startup() {
  //Startup Handshake
  for (int times = 0; times < 3; times++) {
    delay(200);
    TxSerial.println("Startup waiting for 28...");
    RxByte = read_Tx();
    if (RxByte == 28) {
      break;
    }
  }
  if (RxByte == 28) {
    write_Tx(188);
    delay(50);
    write_Tx(236);
    for (int i = 0; i < 3; i++) {
      for (int times = 0; times < 10; times++) {
        if (RxByte == 28 or RxByte == 220) {
          write_Tx(188);
          delay(100);
          write_Tx(236);
          break;
        }
      }
    }

    for (int times = 0; times < 10; times++) {
      if (RxByte == 188) {
        write_Tx(236);
        break;
      }
    }

    for (int times = 0; times < 10; times++) {
      RxByte = read_Tx();
      if (RxByte == 220) {
        write_Tx(188);
        break;
      }
    }
  }
  else {
    // If signal 28 not receive, use brute force initialization
    write_Tx(252);
    for (int times = 0; times < 10; times++) {
      RxByte = read_Tx();
      if (RxByte != 28) {
        TxSerial.println("Waiting for 28...");
        RxByte = read_Tx();
        delay(200);
      }
      else if (RxByte == 28) {
        write_Tx(188);
        break;
      }
    }
    for (int times = 0; times < 10; times++) {
      RxByte = read_Tx();
      if (RxByte != 220) {
        TxSerial.println("Waiting for 220...");
        RxByte = read_Tx();
        delay(200);
      }
      else if (RxByte == 220) {
        write_Tx(188);
        break;
      }
    }
  }

  TxSerial.println("Startup Complete");
  while (Serial.available()) {
    Serial.read();
    if (RxByte == 220) {
      write_Tx(188);
    }
  }
  Serial.flush();
  RxByte = 0;
  delay(30000);
}

///////////////////////////////// Check Humidity Sensors ///////////////////////////////////////////////
/*Checks multiple sensors for humidity and runs HRV to achieve optimal humidity. This routine automatically
   reduces humidity in bathrooms and other high humidity areas where sensors are installed. It also adjusts
   whole house humidity based on outside temperature to reduce condensation on windows during cold weather.
   Recirculation takes precedent to minimize heat loss and occurs whenever minimum indoor humidity < desired
   setpoint. If outside humidity is lower than desired setpoint and maximum indoor humidity is <= minimum indoor
   humidity plus 5%, then fresh air is exchanges until the maximum of desired setpoint or lowest humidity possible
   is reached. Fresh air will also be exchanged during a dehumidification cycle if thermostat is calling for
   exchange, outside temp is above the minimum threshold, and outdoor humidity is low enough to allow dehumidification
   to proceed.
*/
float CheckHumidity(void) {

  float mbh;
  float gbh;
  float rfh;
  //float rft;
  int humTarget;
  float humOn; // = setpointOn;
  float humOff; // = setpointOff;
  float hMin; // = setpointOff;
  int dehumCall;
  int recirc;


  int chk = DHT.read22(mBathPIN);
  if (chk == 0 ) {
    mbh = DHT.humidity;
  }

  chk = DHT.read22(gBathPIN);
  if (chk == 0) {
    gbh = DHT.humidity;
  }

  chk = DHT.read21(refPIN);
  if (chk == 0 ) {
    rfh = DHT.humidity;
  }

  rft = DHT.temperature;
  // Convert rft to Fahrenheit
  rft = rft * 1.8 + 32;

  float h = max(mbh, gbh);

  // Check dehumidity relay to see if Nest is calling for dehumidification
  dhp = digitalRead(dehumPin);
  if (dhp == 0 and h > humOff) {
    digitalWrite(13, HIGH);
    dehumPinState = 1;
  }
  else {
    dehumPinState = 0;
  }


  // Calculate minimum humidity that can be achieved
  float minRecirc = min(mbh, gbh) + 5;
  float minHum = min(rfh + 2, minRecirc);

  if (debug == 0) {
    dataList = String(rft) + ',' + String(int(round(rfh))) + ',' + String(int(round(mbh))) + ',' + String(int(round(gbh))) + ',';
  }

  // Set target humidity based on outside temperature
  if (isnan(rfh) == false) {
    if (isnan(rft) == false) {
      humTarget = float(((0.55 * rft) + 31) - 2.5);
    }

    // Set max humidity to 60% if possible
    if (humTarget > 60 and minHum < 60) {
      humTarget = 60;
    }
    /* if minimum achievable humidity is > humTarget, adjust humidity setpoints
      to minimum humidity possible */
    if (humTarget < minHum) {
      humOff = minHum;
      humOn = minHum + 5;
    }

    // Shut off HRV when humidity drops to target or minimum achievable humidity
    else {
      humOff = humTarget;
    }
    humOn = humOff + 5;

    // Set dehumidication call on/off
    if (h >= humOn or (dhp == 0 and h > humOff + 2)) {
      dehumCall = 1;
    }
    else {
      dehumCall = 0;
    }
    // Set HRV to recirculate if possible to conserve heat
    if ((h < minRecirc and humOff < minRecirc) or (fanPinState == 1 and rft < minTempThresh) or (rfh <= humOff and rft >= 50)) {
      recirc = 0;
    }
    else {
      recirc = 1;
      if (debug >= 2 and last_cmd == 172) {
        TxSerial.println("Running in recirculation mode.");
      }
    }
  }
  // Start dehumidification if humidity is > than target setpoint or Nest is calling for dehumidify
  if (dehumCall == 1 and recirc == 0) {
    if (last_cmd != 0) {
      prehumcmd = last_cmd;
    }
    else prehumcmd = 236;

    if (autoState) {
      cmd = 76;
      dhtState = 1;

      if (debug >= 2) {
        TxSerial.println("High humidity detected!");
      }
    }
  }
  /* Remove as much humidity in recirculation mode
     as possible to conserve heat.
  */
  else if (dehumCall == 1 and recirc == 1) {
    if (last_cmd != 0 and last_cmd != 76 and last_cmd != 172) {
      prehumcmd = last_cmd;
    }
    else prehumcmd = 236;

    if (autoState) {
      cmd = 172;
      dhtState = 1;

      if (debug >= 2) {
        TxSerial.print("h: ");
        TxSerial.println(h);
      }
    }
  }

  else if (h <= humOff and dhtState == 1) {
    if (autoState) {
      if (prehumcmd == 76 or prehumcmd == 172) cmd = 236;
      else cmd = prehumcmd;
      if (debug >= 4) {
        TxSerial.println("Humidity call ended");
      }
      dhtState = 0;
    }
  }
  if (debug >= 1) {
    TxSerial.print("Outside TEMP: ");
    TxSerial.println(rft);
    TxSerial.print("Outside RH: ");
    TxSerial.println(rfh);
    TxSerial.print("Master Bath RH: ");
    TxSerial.println(mbh);
    TxSerial.print("Guest Bath RH: ");
    TxSerial.println(gbh);
    if (debug >= 2) {
      TxSerial.print("humTarget: ");
      TxSerial.println(humTarget);
      TxSerial.print("humOn: ");
      TxSerial.println(humOn);
      TxSerial.print("humOff: ");
      TxSerial.println(humOff);
      if (debug >= 4) {
        TxSerial.println("End Check Humidity");
        TxSerial.print("cmd: ");
        TxSerial.println(cmd);
      }
    }
  }
  return rft;
}

///////////////////////////////// Check Relay Pins for Input Commands ////////////////////////////

void CheckRelays(float rft) {
  wdt_reset();
  if (debug >= 4) {
    TxSerial.println("Checking Relay Status...");
    TxSerial.print("last_cmd: ");
    TxSerial.println(last_cmd);
    TxSerial.print("cmd: ");
    TxSerial.println(cmd);
  }
  int fp = digitalRead(fanPin);
  dhp = digitalRead(dehumPin);

  if ( fp == 0) {
    if (rft > minTempThresh) { // don't initiate fresh air exchange if outside temp is too cold
      if (dhtState == 0) { // CheckHumidity() will handle fresh air call if dehumidification in progress
        cmd = 140;
      }
      digitalWrite(13, HIGH);
      fanPinState = 1;
    }
    else {
      if (debug >= 2) {
        TxSerial.println ("Cold temperature lockout engaged");
      }
    }
  }

  if (fp == 1 and dhp == 1) digitalWrite(13, LOW);
  if (debug >= 4) {
    TxSerial.println("End Check Relay");
    TxSerial.print("cmd: ");
    TxSerial.println(cmd);
  }
}

///////////////////////////////// Set Relays to Appropriate State //////////////////////////////////////
void SetRelays() {
  int fp = digitalRead(fanPin);
  wdt_reset();
  if (fanPinState == 1 and fp == 1) {
    if (debug >= 4) {
      TxSerial.println("Starting fan shutdown sequence...");
      TxSerial.print("Fan Pin: ");
      TxSerial.println(fp);
      TxSerial.print("last_cmd: ");
      TxSerial.println(last_cmd);
    }
    if (last_cmd != 140 and last_cmd != 0) {
      cmd = last_cmd;
    }
    else {
      cmd = 236;
    }
    digitalWrite(13, LOW);
    fanPinState = 0;
  }
}

/////////////////////////// Execute Commands ///////////////////////////
void ExecCmds() {
  // Don't send command if it is is zero or a repeat of last command sent
  if ((cmd != 0 and cmd != last_cmd) or (RxByte == 92 or RxByte == 220)) {

    if (cmd > 5) {
      write_Tx(cmd);
    }

    ///// Execute HRV Requests /////
    // Auto, Off, Recirculate Modes
    if (cmd == 12 or cmd == 172 or cmd == 236) {
      if (cmd == 236) autoState = true;
      AutoOffRecirc();
    }
    // High, Low Fresh Air Exchange Modes

    else if (cmd == 76 or cmd == 140) {
      Xchange();
    }
    // 30-Minute Timer
    else if (RxByte == 92) {
      Timer();
    }
    // Acknowledge Keep Alive Request
    else if (RxByte == 220 || RxByte == 28) {
      Ak();
    }

    // Enter debug mode
    else if (cmd <= 4) {
      if (cmd != 0) debug = cmd;
      else debug = 0;
      if (cmd >= 3)Debug();
    }

    else {
      TxSerial.println("Sending Manual Command");
      write_Tx(cmd);
      cmd = 0;
    }
  }

  // Repeat Command Handler
  else if (cmd != 0) {
    if (debug == 1) {
      TxSerial.println("Ignoring Repeat Command");
    }
    while (TxSerial.read() >= 0); // flush the receive buffer
    while (Serial.read() >= 0); // flush the receive buffer
    cmd = 0;
  }

}
/////////////////////////////////////// Manual Command Mode ///////////////////////////////////////
/* Get commands from TxSerial monitor and send as digital byte
  to HRV. Note that placement of TxSerial read here allows manual commands to
  override any programatic commands set earlier in the loop */

void ManCmd() {
  if (debug >= 4) {
    TxSerial.println("Listening for command");
  }
  if (TxSerial.available()) {
    byte tx = TxSerial.parseInt();
    // Set Debug level if it has changed
    if (tx != debug and tx <= 4) {
      debug = tx;
      TxSerial.print("Debug Level - ");
      TxSerial.println(debug);
    }
    else if (tx == 5) {
      ClearEeprom();
    }
    else if (tx > 5) cmd = tx; // Send manual command
    // Enter manual command mode
    if (cmd != 236) autoState = false;
    // Exit manual command mode
    else {
      autoState = true;
    }
    // Sometimes 0 values get stuck in buffer for some reason. This clears them out.
    if (tx == 0) {
      while (TxSerial.available()) {
        TxSerial.read();
      }
    }
  }
}


/////////////////// Auto, Off and Recirculate Modes ////////////////////
void AutoOffRecirc(void) {
  wdt_reset();
  if (debug >= 4) {
    TxSerial.println("Running AutoOffRecirc...");
  }
  // Wait for acknowledgement that command was received. If not, repeat command up to 3 times
  for (int times = 0; times < 3; times++) {
    delay(200);
    if (RxByte = read_Tx() != 188) {
      write_Tx(cmd);
    }
    else {
      break;
    }
  }
  wdt_reset();
  for (int times = 0; times < 6; times++) {
    RxByte = read_Tx();
    if (RxByte != 220) {
      RxByte = read_Tx();
      delay(700);
    }
    else if (RxByte == 220) {
      write_Tx(188);
      while (RxByte = Serial.read() >= 0); // flush the receive buffer to avoid unwanted commands sent
      break;
    }
  }
  last_cmd = cmd;
  cmd = 0;
  Serial.flush();
}

/////////////////////////////////// Fresh Aire Exchange Modes /////////////////////////////////////
void Xchange(void) {
  wdt_reset();
  if (debug >= 4) {
    TxSerial.println("Running Xchange...");
  }
  // Wait for acknowledgement that command was received. If not, repeat command up to 3 times
  for (int times = 0; times < 3; times++) {
    delay(200);
    RxByte = read_Tx();
    if (RxByte != 188) {
      write_Tx(cmd);
    }
    else if (RxByte == 188) {
      break;
    }
  }
  wdt_reset();
  for (int times = 0; times < 10; times++) {
    RxByte = read_Tx();
    if (RxByte != 92) {
      RxByte = read_Tx();
      delay(700);
    }
    else if (RxByte == 92) {
      write_Tx(188);
      while (RxByte = Serial.read() >= 0); // flush the receive buffer to avoid unwanted commands sent
      RxByte = 0;
      break;
    }
    else {
      if (debug >= 4) {
        TxSerial.println("Error! Communication not received");
      }
      RxByte = 0;
    }
  }
  wdt_reset();
  // Cleanup params for next loop
  if (cmd == 76 and dehumPinState == HIGH and last_cmd != 0 and last_cmd != 236) {
    if (debug >= 4) {
      TxSerial.print("Keeping last_cmd at ");
      TxSerial.println(last_cmd);
    }
  }
  else {
    last_cmd = cmd;
    if (debug >= 4) {
      TxSerial.print("Setting last_cmd to ");
      TxSerial.println(last_cmd);
    }
  }
  cmd = 0;
}

////////////////////////////////// 30-Minute Timer Function /////////////////////////////////
void Timer(void) {
  wdt_reset();
  write_Tx(188);
  if (debug >= 4) {
    TxSerial.println("Starting 30 min timer");
  }
  //30 min = 180000 ms
  for (int times = 0; times < 1800; times++) { //runs 200 times
    wdt_reset();
    // Report data via bluetooth while timer running
    if (debug == 0) {
      CheckHumidity();
      dataList = dataList + String(92) + ',';
      TxSerial.println(dataList);
    }
    RxByte = read_Tx();
    if (TxSerial.parseInt() == 220 || RxByte == 220 || RxByte == 255 || (times > 0 && RxByte == 92)) { // if wall switched pushed again, cancel timer
      if (debug >= 4) {
        TxSerial.println("Timer Cancel Request Recieved");
      }
      // write_Tx(188);
      break;
    }
    else if (RxByte > 0) {
      write_Tx(188);
    }
    RxByte = 0;
  }
  StopTimer();
}
/////Stop Timer
void StopTimer(void) {
  write_Tx(252);
  for (int times = 0; times < 10; times++) {
    wdt_reset();


    RxByte = read_Tx();
    if (RxByte != 28) {
      if (debug >= 4) {
        TxSerial.println("Timer is waiting for 28...");
      }
      RxByte = read_Tx();
      delay(400);
    }
    else if (RxByte == 28) {
      write_Tx(188);
      break;
    }
  }
  for (int times = 0; times < 10; times++) {
    wdt_reset();
    RxByte = read_Tx();
    if (RxByte != 220) {
      if (debug >= 4) {
        TxSerial.println("Timer is waiting for 220...");
      }
      RxByte = read_Tx();
      delay(400);
    }
    else if (RxByte == 220) {
      write_Tx(188);
      break;
    }
  }

  cmd = last_cmd; // return HRV to previous state (also puts HRV in listening mode)
  last_cmd = 0; // make sure command is not ignored as repeat
}

///////////////////////////////// Acknowledge Keep Alive Request //////////////////////////
void Ak(void) {
  if (debug >= 4) {
    TxSerial.println("Sending Acknowledgement");
  }
  write_Tx(188);
  while (Serial.read() >= 0); // flush the receive buffer
  Serial.flush();
  RxByte = 0;
}

///////////////////////////////// Capture EEPROM //////////////////////////////////////////
/* Capture unrecognized commands to eeprom to try to find command sequence for maintenance required indicator */

void Capture(void) {
  for (int address = 0 ; address < EEPROM.length() ; address++) {
    wdt_reset();
    byte val = EEPROM.read(address);
    if (val == RxByte) {
      break;
    }
    else if (val == 0) {
      EEPROM.write(address, RxByte);
      break;
      if (debug > 2) {
        TxSerial.print("!!!!!!!!!!!!!!!- Unknown Command: ");
        TxSerial.print(RxByte);
        TxSerial.println(" -!!!!!!!!!!!!!!!");
      }
    }
    write_Tx(188);
  }
}

///////////////////////////////// Capture EEPROM //////////////////////////////////////////
/* Capture unrecognized commands to eeprom to try to find command sequence for maintenance required indicator */

void readEEPROM(void) {
  TxSerial.println("%%%%%%%%%%%%%%%%% EEPROM Values %%%%%%%%%%%%%%%%%");
  for (int address = 0 ; address < EEPROM.length() ; address++) {
    wdt_reset();
    byte val = EEPROM.read(address);
    if (val > 0 and val < 255) {
      TxSerial.println(val);
    }
    else if (val == 0) {
      break;
    }
  }
  TxSerial.println("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%");
}
///////////////////////////////// Enter Debug to turn print messages on ///////////////////////
void Debug(void) {
  if (debug >= 3) {
    TxSerial.println("########### Debug Report ###############");
    TxSerial.print("Debug RxByte: ");
    TxSerial.println(RxByte);
    TxSerial.print("cmd: ");
    TxSerial.println(cmd);
    TxSerial.print("last_cmd: ");
    TxSerial.println(last_cmd);
    TxSerial.print("dhtState: ");
    TxSerial.println(dhtState);
    TxSerial.print("dehumPinState: ");
    TxSerial.println(dehumPinState);
    TxSerial.print("fanPinState: ");
    TxSerial.println(fanPinState);
    //    TxSerial.print("humOn: ");
    //    TxSerial.println(humOn);
    //    TxSerial.print("humOff: ");
    //    TxSerial.println(humOff);
    //    TxSerial.print("humTarget: ");
    //    TxSerial.println(humTarget);
    //    TxSerial.print("hMin: ");
    //    TxSerial.println(hMin);
    TxSerial.print("prehumcmd: ");
    TxSerial.println(prehumcmd);
    TxSerial.println("########################################");
    if (debug > 3) {
      readEEPROM();
    }
  }
  //cmd = 0;
}

//////////////////////////////////////////////////// Clear Eeprom /////////////////////////////////////////////
void ClearEeprom() {
  /***
    Iterate through each byte of the EEPROM storage.

    Larger AVR processors have larger EEPROM sizes, E.g:
    - Arduno Duemilanove: 512b EEPROM storage.
    - Arduino Uno:        1kb EEPROM storage.
    - Arduino Mega:       4kb EEPROM storage.

    Rather than hard-coding the length, you should use the pre-provided length function.
    This will make your code portable to all AVR processors.
  ***/
  TxSerial.print("Clearing EEPROM storage...");
  for (int i = 0 ; i < EEPROM.length() ; i++) {
    EEPROM.write(i, 0);
    wdt_reset();
  }
  TxSerial.print("Clear EEPROM completed!");
}

/////////////////////////////////////////// Interactive Mode for Debugging Only //////////////////////////////
void interactive() {
  TxSerial.print("Trying: ");
  TxSerial.println(cmd);
  write_Tx(cmd);
  if (Serial.available()) {
    byte reply = read_Tx();
    write_Tx(188);
  }
}

