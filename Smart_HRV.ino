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
const int dehumPin = 2;
int dehumPinState = 0;
int dhp = 1;
const int fanPin = 3;
int fanPinState = 0;
int fp = 1;
int dhtState = 0;
float setpointOn = 60;
float setpointOff = 55;
float humOn = setpointOn;
float humOff = setpointOff;
float hMin = setpointOff; // Intitialize minimum humidity that can be obtained
float rft;
//int humVal = 55; // moved to local variable to free memory
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
  //  if (Serial.available() >= 0) {
  RxByte = read_Tx(); // Read any incoming signals from HRV unit


  // Check relay pins for status of thermostat calls
  if (dhtState == 0) {
    CheckRelays(rft);
  }
  SetRelays();

  // Humidity sensors take precedent over relays or HRV/Timer commands
  rft = CheckHumidity();

  /* Get commands from TxSerial monitor and send as digital byte
    to HRV. Note that placement of TxSerial read here allows manual commands to
    override any programatic commands set earlier in the loop */
  if (debug >= 4) {
    TxSerial.println("Listening for command");
  }
  if (TxSerial.available()) {
    byte tx = TxSerial.parseInt();
    // Set Debug level if it has changed
    if (tx != debug and tx <= 5) {
      debug = tx;
      TxSerial.print("Debug Level - ");
      TxSerial.println(debug);
    }
    else if (tx > 5) cmd = tx; // Send manual comman
    // Sometimes 0 values get stuck in buffer for some reason. This clears them out.
    if (tx == 0) {
      while (TxSerial.available()) {
        TxSerial.read();
      }
    }
  }

  // Don't send command if it is is zero or a repeat of last command sent
  if ((cmd != 0 and cmd != last_cmd) or (RxByte == 92 or RxByte == 220)) {

    if (cmd > 5) {
      write_Tx(cmd);
    }

    ///// Execute HRV Requests /////

    // Auto, Off, Recirculate Modes
    if (cmd == 12 or cmd == 172 or cmd == 236) {
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
    else if (cmd <= 5) {
      if (cmd != 0) debug = cmd;
      else debug = 0;
      if (cmd >= 3)Debug();
    }

    // Capture unknown commands
    else if (RxByte != 0 and RxByte != 220 and RxByte != 255 and RxByte != 92 and RxByte != 28 and RxByte != 188) {
      Capture();
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

  delay(2000);
  if (cmd <= 5) {
    Debug();
    delay(2000);
  }
  // Send data to android via bluetooth
  if (debug == 0) {
    dataList = dataList + String(last_cmd) + ',';
    TxSerial.println(dataList);
  }
  Serial.flush(); // flush any bytes remainining in sending buffer
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
   Dehumidifying with fresh air exchange takes precedent and occurs whenever outside humidity < desired
   setpoint. If outside humidity is higher than desired setpoint, then air is recirculated to quickly
   equilibrate humidity in the house until the maximum of desired setpoint or lowest humidity possible is
   reached.
*/
float CheckHumidity(void) {

  float mbh;
  float gbh;
  float rfh;
  //float rft;
  int humVal;


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
  hMin = min(mbh, gbh);

  if (debug >= 1) {
    TxSerial.print("Outside TEMP: ");
    TxSerial.println(rft);
    TxSerial.print("Outside RH: ");
    TxSerial.println(rfh);
    TxSerial.print("Master Bath RH: ");
    TxSerial.println(mbh);
    TxSerial.print("Guest Bath RH: ");
    TxSerial.println(gbh);
  }
  if (debug == 0) {
    dataList = String(rft) + ',' + String(int(round(rfh))) + ',' + String(int(round(mbh))) + ',' + String(int(round(gbh))) + ',';
  }

  // Set target humidity based on outside temperature
  if (isnan(rfh) == false) {
    if (isnan(rft) == false) {
      humVal = round(float((0.55 * rft) + 31));
    }
    if (debug >= 2) {
      TxSerial.print("Target Humidity: ");
      TxSerial.println(humVal);
    }
    // Changed this to ignore setpointOff to reduce amount of time HRV runs to dehumidify bathrooms
    // humOff = min(humVal, setpointOff);
    humOff = humVal;
    /* if outside humidity is > humOff, adjust humidity setpoints
      to outside humidity plus 1% for Off and outside humidity + 6% for On */
    if (humOff < rfh ) {
      humOff = rfh + 1;
      humOn = rfh + 6;
    }
    if (debug >= 2) {
      TxSerial.print("Humidity Off: ");
      TxSerial.println(humOff);
    }
    // Set minimum hum value for recirculation
    // Changed this to ignore setpointOff to reduce amount of time HRV runs to dehumidify bathrooms
    //    hMin = max(hMin, min(humVal, setpointOff));
    hMin = max(hMin, humVal);

  }
  else {
    humOn = setpointOn;
    humOff = setpointOff;
    hMin = setpointOff;
  }


  if (h >= humOn ) { //and dhtState == 0
    if (debug >= 2) {
      TxSerial.println("High humidity detected!");
    }
    if (last_cmd != 0) {
      prehumcmd = last_cmd;
    }
    else prehumcmd = 236;

    cmd = 76;
    dhtState = 1;
  }
  /* If outside humidity too high to lower humidity to desired level, run in recirculation mode
     if possible.
  */
  else if (h >= hMin and h <= humOff and hMin < humOff) {
    if (debug >= 2) {
      TxSerial.println("High Outdoor Humidity Detected. Running in recirculation mode.");
      TxSerial.print("h: ");
      TxSerial.println(h);
    }
    if (h >= hMin + 5) { // add 5% to on value to prevent short cycling
      if (last_cmd != 0 and last_cmd != 76 and last_cmd != 172) {
        prehumcmd = last_cmd;
      }
      else prehumcmd = 236;

      cmd = 172;
      dhtState = 1;
    }
  }
  else if (h <= humOff and h <= (hMin + 5) and dhtState == 1) {
    if (prehumcmd == 76 or prehumcmd == 172) cmd = 236;
    else cmd = prehumcmd;
    if (debug >= 4) {
      TxSerial.println("Humidity call ended");
    }
    dhtState = 0;
  }
  if (debug >= 4) {
    TxSerial.println("End Check Humidity");
    TxSerial.print("cmd: ");
    TxSerial.println(cmd);
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
  fp = digitalRead(fanPin);
  dhp = digitalRead(dehumPin);

  if ( fp == 0) { // check fan state first to give precedent to dehumidify
    if (rft > 15) { // don't initiate fresh air exchange if outside temp is too cold
      cmd = 140;
      digitalWrite(13, HIGH);
      fanPinState = 1;
    }
    else {
      if (debug >= 3) {
        TxSerial.println ("Cold temperature lockout engaged");
      }
    }

  }

  if (dhp == 0) { // check dehumidify second to override fan // and last_cmd != 76
    cmd = 76;
    digitalWrite(13, HIGH);
    dehumPinState = 1;
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
  wdt_reset();
  if (fanPinState == 1 and fp == 1) {
    if (debug >= 4) {
      TxSerial.println("Starting fan shutdown sequence...");
      TxSerial.print("Fan Pin: ");
      TxSerial.println(fp);
      TxSerial.print("last_cmd: ");
      TxSerial.println(last_cmd);
    }
    if (dehumPinState != 1 and last_cmd != 0) {
      if (last_cmd != 76 and last_cmd != 140) {
        cmd = last_cmd;
      }
      else {
        cmd = 236;
      }
      digitalWrite(13, LOW);
    }
    fanPinState = 0;
  }

  if (dehumPinState == 1 and dhp == 1) {
    if (debug >= 4) {
      TxSerial.println("Starting dehum shutdown sequence...");
      TxSerial.print ("fanPinState, last_cmd = ");
      TxSerial.print(fanPinState);
      TxSerial.print(", ");
      TxSerial.println(last_cmd);
    }
    if (fanPinState == 0 and last_cmd != 0) {
      digitalWrite(13, LOW);
      if (last_cmd != 76 and last_cmd != 10) {
        cmd = last_cmd;
      }
      else {
        cmd = 236;
      }
      last_cmd = 0;
    }
    else if (fanPinState == 1 and last_cmd != 0) {
      last_cmd = 0;
      cmd = 140;
    }
    dehumPinState = 0;
  }
  if (debug >= 4) {
    TxSerial.println("End Set Relay");
    TxSerial.print("cmd: ");
    TxSerial.println(cmd);
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
//Stop Timer
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
    if (val > 0) {
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
    TxSerial.print("humOn: ");
    TxSerial.println(humOn);
    TxSerial.print("humOff: ");
    TxSerial.println(humOff);
    //    TxSerial.print("humVal: ");
    //    TxSerial.println(humVal);
    TxSerial.print("hMin: ");
    TxSerial.println(hMin);
    TxSerial.print("prehumcmd: ");
    TxSerial.println(prehumcmd);
    TxSerial.println("########################################");
    readEEPROM();
  }
  cmd = 0;
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

