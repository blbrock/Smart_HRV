\
#include <RunningAverage.h>
#include <SoftwareSerial.h>
// #include <avr/wdt.h>;
/*
  Switched to DHT library developed by Rob Tillaart
  (https://github.com/RobTillaart/Arduino/tree/master/libraries/DHTlib)
  This library reads both temperature and humidity from
  a sensor with a single poll.  This solved a problem with the outdoor reference
  sensor hanging after several hours of use. This hanging appears to be caused by
  timing issues with float polling to extract both temp and humidity readings from
  that sensor.
*/
#include <dht.h>
//#include <EEPROM.h>

dht DHT;

// Define input pins
const int dehumPin = 2; // Dehumidification relay pin
const int fanPin = 3; // Fresh air fan relay pin
const int mBathPIN = 4;  // Master bath pin
const int gBathPIN = 5;  // Guest bath pin
const int refPIN = 6;  // Outside reference pin
const int mediaRmPIN = 7; // Indoor reference in media room

byte cmd = 4;
byte last_cmd;
int RxByte;
bool autoState = true;
bool recircState = true;
bool fanOn = false;
bool dehumCall = false;
bool defrost = false;
int minTempThresh = 5;
float rft;
int debug = 0;
String dataList = "";
int samples = 0;
unsigned long startTime = 0;
unsigned long dhStartTime = 0;

RunningAverage mbhRA(5);
RunningAverage gbhRA(5);
RunningAverage mrhRA(5);
RunningAverage mbtRA(5);
RunningAverage gbtRA(5);
RunningAverage mrtRA(5);
RunningAverage rfhRA(5);
RunningAverage rftRA(5);

SoftwareSerial TxSerial(10, 11); // RX, TX

void setup() {
  Serial.begin(300);
  TxSerial.begin(9600);
  //  TxSerial.println("Serial communication initialized");

  pinMode(13, OUTPUT);
  digitalWrite(13, LOW);    // turn the LED off by making the voltage LOW to save a little energy
  pinMode(dehumPin, INPUT_PULLUP);
  pinMode(fanPin, INPUT_PULLUP);

  // explicitly start clean
  mbhRA.clear();
  gbhRA.clear();
  mrhRA.clear();
  mbtRA.clear();
  gbtRA.clear();
  mrtRA.clear();
  rfhRA.clear();
  rftRA.clear();

  int dhp = HIGH;
  int chk = DHT.read21(refPIN);
  if (chk == 0 ) {
    //rftCelsius = DHT.temperature;
    // Convert rft to Fahrenheit
    rft = round (rft * 1.8 + 32);
  }

  // Initialize startup handshake
  startup();
  cmd = 236; //Reset cmd and last_cmd after startup
  last_cmd = 236;
  // wdt_enable(WDTO_8S);
}

void loop() {
  //dataList = "";
  if (recircState) defrost = false;
  // wdt_reset();
  // Get commands from HRV
  RxByte = read_Tx(); // Read any incoming signals from HRV unit

  // Check relay pins for status of thermostat calls
  if (autoState) {
    CheckRelays(rft);
    //    SetRelays();
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
  if (debug >= 3) {
    Debug();
    delay(2000);
  }
  // Send data to android via bluetooth
  if (debug == 0) {
    dataList = dataList + String(last_cmd) + ',' + String(defrost) + ',';
    TxSerial.println(dataList);
    dataList = "";
  }
  Serial.flush(); // flush any bytes remainining in sending buffer

  // If manual mode lasts > 4 hours, return to automatic mode
  if (!autoState) {
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
    if (debug >= 1) {
      TxSerial.print("Rx: ");
      TxSerial.println(RxByte, DEC);
    }
  }
  return RxByte;
}

// Write Serial communications
void write_Tx(byte cmd) {
  Serial.write(cmd);
  if (debug >= 1) {
    TxSerial.print("Tx: ");
    TxSerial.println(cmd);
  }
}

////////////////////////////////// Run initial handshake on system startup //////////////////////////////////////
void startup() {
  //Startup Handshake
  for (int times = 0; times < 3; times++) {
    delay(200);
    //    TxSerial.println("Startup waiting for 28...");
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
        //        TxSerial.println("Waiting for 28...");
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
        //        TxSerial.println("Waiting for 220...");
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
  float mrh;
  int dhp;
  float mbt;
  float gbt;
  float mrt;
  float rftCelsius;
  float rfh;
  float humTarget;
  float humOn; // = setpointOn;
  float humOff; // = setpointOff;
  int recirc;

  // wdt_reset();
  // Get data from sensors. Use running averages
  int chk = DHT.read22(mBathPIN);
  if (chk == 0 ) {
    mbhRA.addValue(DHT.humidity);
    mbh = mbhRA.getAverage();
    mbtRA.addValue(DHT.temperature);
    mbt = mbtRA.getAverage();
  }
  chk = DHT.read22(gBathPIN);
  if (chk == 0) {
    gbhRA.addValue(DHT.humidity);
    gbh = gbhRA.getAverage();
    gbtRA.addValue(DHT.temperature);
    gbt = gbtRA.getAverage();
  }
  // wdt_reset();
  chk = DHT.read22(mediaRmPIN);
  if (chk == 0) {
    mrhRA.addValue(DHT.humidity);
    mrh = mrhRA.getAverage();
    mrtRA.addValue(DHT.temperature);
    mrt = mrtRA.getAverage();
  }
  chk = DHT.read21(refPIN);
  if (chk == 0 ) {
    rfhRA.addValue(DHT.humidity);
    rfh = rfhRA.getAverage();
    rftRA.addValue(DHT.temperature);
    rftCelsius = rftRA.getAverage();
    //rftCelsius = DHT.temperature;
    // Convert rft to Fahrenheit
    rft = round(rftCelsius * 1.8 + 32);
  }

  // wdt_reset();
  samples++;
  if (samples == 100)
  {
    samples = 0;
    mbhRA.clear();
    gbhRA.clear();
    mrhRA.clear();
    mbtRA.clear();
    gbtRA.clear();
    mrtRA.clear();
    rfhRA.clear();
    rftRA.clear();
  }

  /* Convert indoor and outdoor relative humidity to absolute humidity to allow comparison of
      water holding capacity for setting humidity on/off setpoints.
  */
  float tempIndoor = (mbt + gbt + mrt) / 3;
  float hMax = max(mbh, max(gbh, mrh)); // highest indoor sensor
  float hMin = min(mbh, min(gbh, mrh));
  float hAvg = ((mbh + gbh + mrh) - hMax) / 2; // = average of 2 lowest indoor sensors
  float h;
  // Use maximum humidity only when humidity is raised in bathrooms (e.g. shower), otherwise use average
  if (hMax - hMin > 10) h = hMax;
  else  h = hAvg;
  h = calcAbsH(tempIndoor, h);
  float Absh = calcAbsH(tempIndoor, h);
  float AbsHumIndoor = calcAbsH(tempIndoor, hAvg);
  float AbsHumOutdoor = calcAbsH(rftCelsius, rfh);
  float minRecirc = AbsHumIndoor * 1.2;

  /*
        Calculate minimum humidity that can be achieved - indoor humidity calculated as the average
        humidity of the two lowest reading sensors. Minimum achievable recirculating humidity is
        absolute indoor humidity times 1.2. Minimum humidity achievable is minimum of minRecirc and absolute
        outdoor humidity time 2.1. Multipliers are arbitrary values designed to avoid setting humidity off setpoints
        that are unrealistically low, which results in continuous running of HRV without affecting humidity.
  */
  float minHum = min(AbsHumOutdoor * 2.1, minRecirc);
  //float minHum = min(rfhAdj + 2, minRecirc);

  if (debug == 0) {
    dataList = String(rft) + ',' + String(int(round(rfh))) + ',' + String(int(round(mbh))) + ',' + String(int(round(gbh))) + ',';
  }

  // Set target humidity based on outside temperature

    humTarget = float(((0.56 * rft) + 32) - 2.5);
 // humTarget = float(((0.4603 * rft) + 32) - 2.5);

  // Set max humidity to 60% if possible
  if (humTarget > 60 and minHum < 60) humTarget = 60;
  // set min humidity to avoid running fresh air exchange during extreme cold
  else if (humTarget < 26) humTarget = 26;

  float AbsHumTarget = calcAbsH(tempIndoor, humTarget);

  /* if minimum achievable humidity is > humTarget, adjust humidity setpoints
    to minimum humidity possible */
  if (AbsHumTarget < minHum) {
    humOff = minHum;
  }

  // Shut off HRV when humidity drops to target or minimum achievable humidity
  else {
    humOff = AbsHumTarget;
  }

  /* If it's really cold outside, adjust humOff upward slightly to prevent continuous
    run without achieving humidity shutoff.
  */
  if (rft < minTempThresh) humOff = humOff * 1.25;

  humOn = humOff + 1;


  // Check dehumidity relay to see if Nest is calling for dehumidification
  dhp = digitalRead(dehumPin);
  if (dhp == LOW) {
    digitalWrite(13, HIGH);
  }
  // Set dehumidication call on/off. Start/check timer to run dh call minimum of 15 minutes to prevent short cycling.
  if (h >= humOn or (dhp == LOW and h > minHum)) {
    dehumCall = true;
    if (dhStartTime == 0) dhStartTime = millis();
  }
  else if (millis() - dhStartTime > 900000 and (h <= humOff and (dhp == HIGH) or (dhp == LOW and minHum > h + 0.5))) {
    dehumCall = false;
    recircState = true;
    dhStartTime = 0;
  }

  // Set HRV to recirculate if possible to conserve heat
  if ((h <= minRecirc) or (calcAbsH(tempIndoor, hAvg) > humOff) or (fanOn and rft > minTempThresh) or (AbsHumOutdoor <= humOff and rft >= 50)) {
    recircState = false;
  }
  else if (h > minRecirc + 1) { // or rft < minTempThresh) {
    recircState = true;
  }

  if (autoState) {
    //    Start dehumidification if humidity is > than target setpoint or Nest is calling for dehumidify
    if (dehumCall) {

      if (recircState) cmd = 172;
      else cmd = 76;
    }
    else {
      if (fanOn and rft > minTempThresh) cmd = 140;
      else cmd = 236;
    }
  }
  if (debug >= 2) {
    TxSerial.print("Outside TEMP: ");
    TxSerial.println(rft);
    TxSerial.print("Outside RH: ");
    TxSerial.println(rfh);
    TxSerial.print("Inside Ref RH: ");
    TxSerial.println(mrh);
    TxSerial.print("Master Bath RH: ");
    TxSerial.println(mbh);
    TxSerial.print("Guest Bath RH: ");
    TxSerial.println(gbh);
    TxSerial.print("humTarget: ");
    TxSerial.println(humTarget);


    if (debug >= 3) {
      TxSerial.println();
      TxSerial.print("h: ");
      TxSerial.println(h);
      TxSerial.print("humOn: ");
      TxSerial.println(humOn);
      TxSerial.print("humOff: ");
      TxSerial.println(humOff);
      TxSerial.print("minRecirc: ");
      TxSerial.println(calcRelH(tempIndoor, minRecirc));
      TxSerial.print("AbsHumOutdoor: ");
      TxSerial.println(AbsHumOutdoor);
      TxSerial.print("AbsHumTarget: ");
      TxSerial.println(AbsHumTarget);

      if (debug >= 4) {
        TxSerial.println();
        TxSerial.println();
        TxSerial.print("Dehum Relay: ");
        TxSerial.println(dhp);
      }
    }

  }
  return rft;
}

///////////////////////////////// Check Relay Pins for Input Commands ////////////////////////////

void CheckRelays(float rft) {
  int fp = digitalRead(fanPin);
  int dhp = digitalRead(dehumPin);

  // Don't initiate fresh air exchange if outside temp is too cold
  // CheckHumidity() will handle fresh air call if dehumidification in progress
  if (fp == LOW) {
    fanOn = true;
    if (rft > minTempThresh and !dehumCall) {
      cmd = 140;
      digitalWrite(13, HIGH);
    }
  }
  else if (fanOn and fp == HIGH) {
    digitalWrite(13, LOW);
    fanOn = false;
    if (!dehumCall) {
      cmd = 236;
    }
  }
  if (fp == HIGH and dhp == HIGH) digitalWrite(13, LOW);
}

/////////////////////////// Execute Commands ///////////////////////////
void ExecCmds() {
  if (debug >= 1) {
  }
  // Don't send command if it is is zero or a repeat of last command sent
  if ((cmd != 255 and cmd != last_cmd) or (RxByte == 92 or RxByte == 220 or RxByte == 236)) {
    // if (cmd == 255) cmd = 236;
    ///// Execute HRV Requests /////
    if (cmd > 5 ) {
      write_Tx(cmd);
    }
    // 20-Minute Timer
    if (RxByte == 92) {
      /* send Ak if defrost signal (92) received. I've found no way
          to tell the difference between 92 sent from pressing a wall switch
          and one sent to signal defrost. The partial solution is to assume
          that 92 sent during dehumidification is a defrost call. A defrost call
          low speed fresh air exchange could be misinterpreted as a wall switch
          push and initiate high speed exchange timer.

      */
      if (dehumCall or last_cmd == 76 or last_cmd == 140) {
        Ak();
        if (defrost) {
          defrost = false;
          if (cmd = 76) last_cmd = 76;
        }
        else {
          defrost = true;
          last_cmd = 172;
        }
      }
      else  Timer();
      last_cmd = cmd;
    }
    // Acknowledge Keep Alive Request
    else if (RxByte > 0 and RxByte < 255) {
      // else if (RxByte == 220 or RxByte == 28 or RxByte == 92) {
      Ak();
    }
    // Auto, Off, Recirculate Modes
    else if (cmd == 12 or cmd == 172 or cmd == 236) {
      last_cmd = cmd;
      defrost = 0;

      AutoOffRecirc();
    }
    // High, Low Fresh Air Exchange Modes
    else if (cmd == 76 or cmd == 140) {
      last_cmd = cmd;
      Xchange();
    }
    else {
      TxSerial.println("Man Cmd");
      write_Tx(cmd);
    }
  }

  while (TxSerial.read() >= 0); // flush the receive buffer
  while (Serial.read() >= 0); // flush the receive buffer
}

/////////////////////////////////////// Manual Command Mode ///////////////////////////////////////
/* Get commands from TxSerial monitor and send as digital byte
  to HRV. Note that placement of TxSerial read here allows manual commands to
  override any programatic commands set earlier in the loop */

void ManCmd() {
  if (TxSerial.available()) {
    byte tx = TxSerial.parseInt();
    // Set Debug level if it has changed
    if (tx != debug and tx <= 4) {
      debug = tx;
      TxSerial.print("Debug Level - ");
      TxSerial.println(debug);
    }
    else if (tx > 5 and tx < 255) {
      cmd = tx; // Send manual command
      // Enter manual command mode
      if (autoState) {
        if (cmd == 236 and last_cmd == 236) ; // Don't toggle autoState on repeat off command
        else  {
          autoState = false;
          startTime = millis();
        }
      }
      else if (cmd == 236) autoState = true;
    }

    // Sometimes 0 values get stuck in buffer for some reason. This clears them out.
    while (TxSerial.available()) {
      TxSerial.read();
    }
  }
}

/////////////////// Auto, Off and Recirculate Modes ////////////////////
void AutoOffRecirc(void) {
  // wdt_reset();
  // Wait for acknowledgement that command was received. If not, repeat command up to 3 times
  for (int times = 0; times < 3; times++) {
    delay(200);
    RxByte = read_Tx();
    if (RxByte != 188 and RxByte != 92) {
      write_Tx(cmd);
    }
    else {
      break;
    }
  }
  // wdt_reset();
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
  Serial.flush();
}

/////////////////////////////////// Fresh Aire Exchange Modes /////////////////////////////////////
void Xchange(void) {
  // Wait for acknowledgement that command was received. If not, repeat command up to 3 times
  for (int times = 0; times < 3; times++) {
    delay(200);
    RxByte = read_Tx();
    if (RxByte != 188 and RxByte != 92) {
      write_Tx(cmd);
    }
    else if (RxByte == 188) {
      break;
    }
  }
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
      RxByte = 0;
    }
  }
}

////////////////////////////////// 30-Minute Timer Function /////////////////////////////////
void Timer(void) {
  autoState = false;
  startTime = millis();
  byte Tx;
  int times = 0;
  // wdt_reset();
  write_Tx(188);
  //  if (debug >= 1) {
  //    TxSerial.println("Starting 30 min timer");
  //  }
  //30 min = 1800000 ms
  //20 min = 1200000 ms
  while (millis() - startTime < 1200000) {
    // Report data via bluetooth while timer running

    CheckHumidity();
    dataList = dataList + String(92) + ',' + String(defrost) + ',';
    TxSerial.println(dataList);
    dataList = ""; // clear old data list

    RxByte = read_Tx();
    if (TxSerial.available()) {
      Tx = TxSerial.parseInt();
    }
    if (Tx != debug and Tx <= 4) {
      debug = Tx;
      TxSerial.print("Debug Level - ");
      TxSerial.println(debug);
    }
    else if (Tx == 220 || Tx == 236 || Tx == 255 || RxByte == 220 || RxByte == 255 || (times > 0 && RxByte == 92)) { // if wall switched pushed again, cancel timer
      //      if (debug >= 1) {
      //        TxSerial.println("Timer Cancel Request Recieved");
      //      }
      break;
    }
    else if (RxByte > 0) {
      write_Tx(188);
    }
    RxByte = 0;
    delay(2000);
  }

  //  if (debug >= 1) {
  //    TxSerial.println("Timer stopped");
  //  }

  // Shut down HRV unless timer ended by button push
  if (RxByte != 220 and RxByte != 92) StopTimer();

  // Return to auto mode and report that ventilation has stopped.
  autoState = true;
  cmd = 236;
}
////////////////////// Stop Timer ////////////////////////////////
void StopTimer(void) {
  write_Tx(252); // Had to guess at this command through trial and error.
  for (int times = 0; times < 10; times++) {
    // wdt_reset();
    RxByte = read_Tx();
    if (RxByte != 28) {
      RxByte = read_Tx();
      delay(400);
    }
    else if (RxByte == 28) {
      write_Tx(188);
      break;
    }
  }
  for (int times = 0; times < 10; times++) {
    // wdt_reset();
    RxByte = read_Tx();
    if (RxByte != 220) {
      RxByte = read_Tx();
      delay(400);
    }
    else if (RxByte == 220) {
      write_Tx(188);
      break;
    }
  }
  cmd = 236;
}

///////////////////////////////// Acknowledge Keep Alive Request //////////////////////////
void Ak(void) {
  write_Tx(188);
  while (Serial.read() >= 0); // flush the receive buffer
  Serial.flush();
  RxByte = 0;
}

/////////////////////////////////// Calculate Dewpoint (fast method) ////////////////////////
//// delta max = 0.6544 wrt dewPoint()
//// 6.9 x faster than dewPoint()
//// reference: http://en.wikipedia.org/wiki/Dew_point
//float dewPointFast(float celsius, float humidity)
//{
//  float a = 17.271;
//  float b = 237.7;
//  float temp = (a * celsius) / (b + celsius) + log(humidity * 0.01);
//  // wdt_reset();
//  float Td = (b * temp) / (a - temp);
//  return Td;
//}
//
/////////////////////////////////// Calculate Humidity //////////////////////////////////////
//// reference: http://andrew.rsmas.miami.edu/bmcnoldy/Humidity.html
//
//int humidityAdj(float celsius, float dewpoint)
//{
//  // wdt_reset();
//  int humAdj = 100 * (exp((17.625 * dewpoint) / (243.04 + dewpoint)) / exp((17.625 * celsius) / (243.04 + celsius)));
//  // wdt_reset();
//  return humAdj;
//}
//

///////////////////////////////// Calculate Absolute Humidity /////////////////////////////
/* References: Formula derived by Peter Mander https://carnotcycle.wordpress.com/2012/08/04/how-to-convert-relative-humidity-to-absolute-humidity/
   Arduion function copied from http://arduino.ru/forum/proekty/kontrol-vlazhnosti-podvala-arduino-pro-mini
*/
float calcAbsH(float t, float h) {
  float temp;
  temp = pow(2.718281828, (17.67 * t) / (t + 243.5));
  return (6.112 * temp * h * 2.1674) / (273.15 + t);
}

///////////////////////////////// Calculate Relative Humidity /////////////////////////////
/* References: Formula derived by Peter Mander https://carnotcycle.wordpress.com/2012/08/04/how-to-convert-relative-humidity-to-absolute-humidity/
   Arduion function copied from http://arduino.ru/forum/proekty/kontrol-vlazhnosti-podvala-arduino-pro-mini
*/
float calcRelH(float t, float ah) {
  float temp;
  temp = pow(2.718281828, (17.67 * t) / (t + 243.5));
  return ((273.15 * ah) + (ah * t)) / (13.2471488 * temp);
}

///////////////////////////////// Capture EEPROM //////////////////////////////////////////
/* Capture unrecognized commands to eeprom to try to find command sequence for maintenance required indicator */

//void Capture(void) {
//  for (int address = 0 ; address < EEPROM.length() ; address++) {
//    // wdt_reset();
//    byte val = EEPROM.read(address);
//    if (val == RxByte) {
//      break;
//    }
////    else if (val == 0) {
////      EEPROM.write(address, RxByte);
////      break;
////      if (debug > 2) {
////        TxSerial.print("!!!!!!!!!!!!!!!- Unknown Command: ");
////        TxSerial.print(RxByte);
////        TxSerial.println(" -!!!!!!!!!!!!!!!");
////      }
////    }
//    write_Tx(188);
//  }
//}

///////////////////////////////// Capture EEPROM //////////////////////////////////////////
/* Capture unrecognized commands to eeprom to try to find command sequence for maintenance required indicator */

//void readEEPROM(void) {
//  TxSerial.println("%%%%%%%%%%%%%%%%% EEPROM Values %%%%%%%%%%%%%%%%%");
//  for (int address = 0 ; address < EEPROM.length() ; address++) {
//    // wdt_reset();
//    byte val = EEPROM.read(address);
//    if (val > 0 and val < 255) {
//      TxSerial.println(val);
//    }
//    else if (val == 0) {
//      break;
//    }
//  }
//  TxSerial.println("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%");
//}
///////////////////////////////// Enter Debug to turn print messages on ///////////////////////
void Debug(void) {
  if (debug >= 4) {
    TxSerial.print("Debug RxByte: ");
    TxSerial.println(RxByte);
    TxSerial.print("cmd: ");
    TxSerial.println(cmd);
    TxSerial.print("last_cmd: ");
    TxSerial.println(last_cmd);
    TxSerial.print("autoState: ");
    TxSerial.println(autoState);
    TxSerial.print("dehumCall: ");
    TxSerial.println(dehumCall);
    TxSerial.print("recircState: ");
    TxSerial.println(recircState);
    TxSerial.print("fanOn: ");
    TxSerial.println(fanOn);
  }
}

//////////////////////////////////////////////////// Clear Eeprom /////////////////////////////////////////////
//void ClearEeprom() {
//  /***
//    Iterate through each byte of the EEPROM storage.
//
//    Larger AVR processors have larger EEPROM sizes, E.g:
//    - Arduno Duemilanove: 512b EEPROM storage.
//    - Arduino Uno:        1kb EEPROM storage.
//    - Arduino Mega:       4kb EEPROM storage.
//
//    Rather than hard-coding the length, you should use the pre-provided length function.
//    This will make your code portable to all AVR processors.
//  ***/
//  TxSerial.print("Clearing EEPROM storage...");
//  for (int i = 0 ; i < EEPROM.length() ; i++) {
//    EEPROM.write(i, 0);
//    // wdt_reset();
//  }
//  TxSerial.print("Clear EEPROM completed!");
//}

///////////////////////////////////////////// Interactive Mode for Debugging Only //////////////////////////////
//void interactive() {
//  TxSerial.print("Trying: ");
//  TxSerial.println(cmd);
//  write_Tx(cmd);
//  if (Serial.available()) {
//    byte reply = read_Tx();
//    write_Tx(188);
//  }
//}

