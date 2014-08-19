#include <avr/wdt.h> 
#include <SPI.h>
#include <RFM69.h>
#include <RFM69registers.h>
#include <Adafruit_NeoPixel.h>
#include "MyTypes.h"                  // Arduino hack to make structs work properly

#define NODEID           2            // this should be unique for each node
#define NETWORKID        100          // every node should be on the same network
#define FREQUENCY        RF69_433MHZ
#define MAX_NODES        12           // max number of nodes in our network
#define PAIRED_THRESH    70           // threshold that determines nodes are paired (nearby)
#define UNPAIRED_THRESH  30           // after going below this threshold, nodes are unpaired (lost)
#define SEND_INTERVAL    2000
#define LOST_INTERVAL    40000        // if we haven't heard from a node for this long, they're lost
#define BRIGHTNESS       100          // global control over LED brightness

#define N_LEDS           16
#define LED_PIN          A0
//#define LED_PIN          4

// WIPE_FRAMES 128 is the optimum number of frames to cover most wipe
// situations.
#define WIPE_FRAMES         128
#define WIPE_SPEED_MODIFIER 2       // Slowdown the wipe animation
#define WIPE_LEDS_PER_NODE  5
#define WIPE_FREQUENCY      30000   // Frequency at which to show WIPE

short frame = 0;
unsigned long timestamp;
unsigned long lastShowRssi;
byte currentBestNode;
unsigned long rssiTimestamp;
unsigned long lastWipeTimestamp;
unsigned long lastSleepMillis;

Node nodes[ MAX_NODES ];  // array of up to MAX_NODES

enum States{
  TURNING_ON,
  SHOWING_WIPE,
  SHOWING_RSSI,
  NEW_PAIR,
  UNPAIRED,
  IDLE
};
States state = TURNING_ON;
boolean triggerDisplayRssi = false;
boolean triggerNewPair = false;
boolean triggerUnpair = false;
boolean triggerRadioSleep = false;
byte pairedNodes[MAX_NODES];

RFM69 radio;
bool promiscuousMode = false;
unsigned long lastSend = 0;
short bestNodes[MAX_NODES];
byte bestRssis[MAX_NODES];
byte bestNodesCount = 0;

byte lastPairedNode;
byte lastUnpairedNode;

Adafruit_NeoPixel ledRing = Adafruit_NeoPixel( N_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800 );

const byte dim_curve[] = {
    0,   1,   1,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,   3,   3,
    3,   3,   3,   3,   3,   3,   3,   4,   4,   4,   4,   4,   4,   4,   4,   4,
    4,   4,   4,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   6,   6,   6,
    6,   6,   6,   6,   6,   7,   7,   7,   7,   7,   7,   7,   8,   8,   8,   8,
    8,   8,   9,   9,   9,   9,   9,   9,   10,  10,  10,  10,  10,  11,  11,  11,
    11,  11,  12,  12,  12,  12,  12,  13,  13,  13,  13,  14,  14,  14,  14,  15,
    15,  15,  16,  16,  16,  16,  17,  17,  17,  18,  18,  18,  19,  19,  19,  20,
    20,  20,  21,  21,  22,  22,  22,  23,  23,  24,  24,  25,  25,  25,  26,  26,
    27,  27,  28,  28,  29,  29,  30,  30,  31,  32,  32,  33,  33,  34,  35,  35,
    36,  36,  37,  38,  38,  39,  40,  40,  41,  42,  43,  43,  44,  45,  46,  47,
    48,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,
    63,  64,  65,  66,  68,  69,  70,  71,  73,  74,  75,  76,  78,  79,  81,  82,
    83,  85,  86,  88,  90,  91,  93,  94,  96,  98,  99,  101, 103, 105, 107, 109,
    110, 112, 114, 116, 118, 121, 123, 125, 127, 129, 132, 134, 136, 139, 141, 144,
    146, 149, 151, 154, 157, 159, 162, 165, 168, 171, 174, 177, 180, 183, 186, 190,
    193, 196, 200, 203, 207, 211, 214, 218, 222, 226, 230, 234, 238, 242, 248, 255,
};

void initRadio() {
  radio.initialize( FREQUENCY, NODEID, NETWORKID );
  radio.promiscuous( promiscuousMode );
  radio.setHighPower();

  // set 1200 bitrate for best range
  // from here - https://lowpowerlab.com/forum/index.php/topic,562.0.html
  radio.writeReg( 0x03, 0x68 );
  radio.writeReg( 0x04, 0x2B );
  radio.writeReg( 0x05, 0x00 );
  radio.writeReg( 0x06, 0x52 );
  radio.writeReg( 0x19, 0x40 | 0x10 | 0x05 );
  radio.writeReg( 0x18, 0x00 | 0x00 | 0x01 );
}

void setup(){
  
  Serial.begin( 9600 );
  delay(10);
  
  // Configure the watchdog timer
  wdt_enable(WDTO_8S);

  initRadio();
  
  // initialize the nodes array
  for( byte i=0; i<MAX_NODES; i++ ){
    nodes[i].paired = false;
    // calculate the node colors from HSV
    getRGB( (255/MAX_NODES)*i, 70, 255, nodes[i].brightenedHue );
  }

  nodes[0].nodeHue[0]  = dim_curve[255];
  nodes[0].nodeHue[1]  = 0;
  nodes[0].nodeHue[2]  = 0;
  nodes[1].nodeHue[0]  = dim_curve[255];
  nodes[1].nodeHue[1]  = dim_curve[128];
  nodes[1].nodeHue[2]  = 0;
  nodes[2].nodeHue[0]  = dim_curve[255];
  nodes[2].nodeHue[1]  = dim_curve[255];
  nodes[2].nodeHue[2]  = 0;
  nodes[3].nodeHue[0]  = dim_curve[128];
  nodes[3].nodeHue[1]  = dim_curve[255];
  nodes[3].nodeHue[2]  = 0;
  nodes[4].nodeHue[0]  = 0;
  nodes[4].nodeHue[1]  = dim_curve[255];
  nodes[4].nodeHue[2]  = 0;
  nodes[5].nodeHue[0]  = 0;
  nodes[5].nodeHue[1]  = dim_curve[255];
  nodes[5].nodeHue[2]  = dim_curve[128];
  nodes[6].nodeHue[0]  = 0;
  nodes[6].nodeHue[1]  = dim_curve[255];
  nodes[6].nodeHue[2]  = dim_curve[255];
  nodes[7].nodeHue[0]  = 0;
  nodes[7].nodeHue[1]  = dim_curve[128];
  nodes[7].nodeHue[2]  = dim_curve[255];
  nodes[8].nodeHue[0]  = 0;
  nodes[8].nodeHue[1]  = 0;
  nodes[8].nodeHue[2]  = dim_curve[255];
  nodes[9].nodeHue[0]  = dim_curve[127];
  nodes[9].nodeHue[1]  = 0;
  nodes[9].nodeHue[2]  = dim_curve[255];
  nodes[10].nodeHue[0] = dim_curve[255];
  nodes[10].nodeHue[1] = 0;
  nodes[10].nodeHue[2] = dim_curve[255];
  nodes[11].nodeHue[0] = dim_curve[255];
  nodes[11].nodeHue[1] = 0;
  nodes[11].nodeHue[2] = dim_curve[127];
  
  Serial.print( "This is node: " );
  Serial.println( NODEID );
  
  ledRing.begin();
  ledRing.setBrightness( BRIGHTNESS );
  
}

void loop(){
    
  // receive a packet if there is one
  setLed(true);
  if( radio.receiveDone() ){
    if( !nodes[ radio.SENDERID ].paired )
      triggerDisplayRssi = true;
    Serial.print("Received from ["); Serial.print(radio.SENDERID); Serial.print("] ");
    
    // make sure we don't go out of array bounds
    if( radio.SENDERID > MAX_NODES || radio.SENDERID < 0 ){
      Serial.println( "Bad sender ID" );
      return;
    }
    // store received strength
    else{
      updateNode( radio.SENDERID, &nodes[ radio.SENDERID ], map( radio.RSSI, -25, -100, 100, 0 ) );
      Serial.print("   [RX_RSSI:"); Serial.print( map( radio.RSSI, -25, -100, 100, 0 ) ); Serial.print("]");
      Serial.println();
    }
  }
  setLed(false);
  
  // send a ping; random helps keep tx's out of sync
  // only send pings when we're in IDLE (and LED's off)
  if( state==IDLE && millis() - lastSend > (SEND_INTERVAL + random(500,1000)) ){
    lastSend = millis();
    
    delay(3);  // delay after reception (from sample code)
    setLed(true);
    if( !radio.send( RF69_BROADCAST_ADDR, ")'(", 3, false ) )
      Serial.println( "Couldn't send" );
    else
      Serial.println( "Tx" );
    setLed(false);
    blink();
  }

  if ( millis() - lastSend > 10000) {
    Serial.println("Putting radio to sleep");
    radio.sleep();
    lastSleepMillis = millis();
    triggerRadioSleep = true;
  }

  if ( triggerRadioSleep && millis() - lastSleepMillis > 1000) {
    Serial.println("Waking radio up");
    triggerRadioSleep = false;
    initRadio();
  }
  
  updateBestRssi();
  
  // animation state machine
  setLed(true);
  switch( state ){
    
    case SHOWING_WIPE:
      // state management
      if( frame >= WIPE_FRAMES ){
        Serial.println( "Transition from SHOWING_WIPE to IDLE " );
        frame = 0;
        state = IDLE;
        timestamp = millis();
        clearDisplay(true);
      }
      else{
	byte pairedNodesCount = calcPairedNodes();

        // show the wipe
	byte warmup[3] = {20, 50, 100};

	short totalLedCount = (sizeof(warmup) * 2) + (pairedNodesCount * WIPE_LEDS_PER_NODE);
        byte pattern[totalLedCount][3];

	for (byte i = 0; i < sizeof(warmup); i++) {
	  pattern[i][0] = warmup[i];
	  pattern[i][1] = warmup[i];
	  pattern[i][2] = warmup[i];
	}
	for (byte i = 0; i < (pairedNodesCount * WIPE_LEDS_PER_NODE); i++) {
	  pattern[sizeof(warmup) + i][0] = nodes[pairedNodes[i/WIPE_LEDS_PER_NODE]].nodeHue[0];
	  pattern[sizeof(warmup) + i][1] = nodes[pairedNodes[i/WIPE_LEDS_PER_NODE]].nodeHue[1];
	  pattern[sizeof(warmup) + i][2] = nodes[pairedNodes[i/WIPE_LEDS_PER_NODE]].nodeHue[2];
	}
	for (byte i = 0; i < sizeof(warmup); i++) {
	  pattern[totalLedCount - sizeof(warmup) + i][0] = warmup[sizeof(warmup) - 1 - i];
	  pattern[totalLedCount - sizeof(warmup) + i][1] = warmup[sizeof(warmup) - 1 - i];
	  pattern[totalLedCount - sizeof(warmup) + i][2] = warmup[sizeof(warmup) - 1 - i];
	}
        if( frame % WIPE_SPEED_MODIFIER == 0 ){ // slow down the animation a little
          clearDisplay(false);
          short i = frame / WIPE_SPEED_MODIFIER;
          for( short j = totalLedCount - 1; j>=0 && i>=0; j--, i-- ){
            ledRing.setPixelColor( i, pattern[j][0], pattern[j][1], pattern[j][2] );
          }
        }
        frame++;
        ledRing.show();
      }
    break;
    
    case SHOWING_RSSI:

      if (currentBestNode >= bestNodesCount) {
	// clear the display and transition to idle
	Serial.println( "Transition from SHOWING_RSSI to IDLE" );
	state = IDLE;
	clearDisplay(true);
	timestamp = millis();
	lastShowRssi = millis();
      } else if( frame > bestRssis[currentBestNode] / 4 ) {  // if we've filled out the display
        if( millis() - rssiTimestamp > 2000 ){  // and the display has been on for 2s
	  currentBestNode++;
	  rssiTimestamp = millis();
	  frame = 0;
	  clearDisplay(true);
        }
      }
      else {
	short bestNode = bestNodes[currentBestNode];
	if (bestNode != -1) {
	  for ( byte i=0; i<frame; i++ ){
	    if( (i+1 > 2) && (i+1 == bestRssis[currentBestNode] / 4 ) ) {
	      ledRing.setPixelColor( i, nodes[bestNode].brightenedHue[0], nodes[bestNode].brightenedHue[1], nodes[bestNode].brightenedHue[2] );
	    } else {
	      ledRing.setPixelColor( i, nodes[bestNode].nodeHue[0], nodes[bestNode].nodeHue[1], nodes[bestNode].nodeHue[2] );
	    }
	  }
	  ledRing.show();
	}
	frame++;
      }
    break;
    
    case NEW_PAIR:
      if( millis() - timestamp > 3000 ){
        Serial.println( "Transition from NEW_PAIR to IDLE" );
        clearDisplay(true);
        timestamp = millis();
        state = IDLE;
      }
      else{

        for( byte i=0; i<N_LEDS/4; i++ ) {
          ledRing.setPixelColor( i, 255, 255, 255 );
	}
	for (byte i=N_LEDS/4*1; i < (N_LEDS / 4 * 2); i++) {
	  ledRing.setPixelColor(i, 
				nodes[lastPairedNode].nodeHue[0],
				nodes[lastPairedNode].nodeHue[1],
				nodes[lastPairedNode].nodeHue[2]);
	}
        for( byte i=N_LEDS/4*2; i<N_LEDS/4*3; i++ ) {
          ledRing.setPixelColor( i, 255, 255, 255 );
	}
	for (byte i=N_LEDS/4*3; i < (N_LEDS / 4 * 4); i++) {
	  ledRing.setPixelColor(i, 
				nodes[lastPairedNode].nodeHue[0],
				nodes[lastPairedNode].nodeHue[1],
				nodes[lastPairedNode].nodeHue[2]);
	}
        ledRing.show();
      }
    break;
  
    case UNPAIRED:
      if( millis() - timestamp > 3000 ){
        Serial.println( "Transition from UNPAIR to IDLE" );
        clearDisplay(true);
        timestamp = millis();
        state = IDLE;
      }
      else{
        for( byte i=0; i<N_LEDS/4; i++ ) {
          ledRing.setPixelColor( i, 0, 0, 0 );
	}
	for (byte i=N_LEDS/4*1; i < (N_LEDS / 4 * 2); i++) {
	  ledRing.setPixelColor(i, 
				nodes[lastUnpairedNode].nodeHue[0],
				nodes[lastUnpairedNode].nodeHue[1],
				nodes[lastUnpairedNode].nodeHue[2]);
	}
        for( byte i=N_LEDS/4*2; i<N_LEDS/4*3; i++ ) {
          ledRing.setPixelColor( i, 0, 0, 0 );
	}
	for (byte i=N_LEDS/4*3; i < (N_LEDS / 4 * 4); i++) {
	  ledRing.setPixelColor(i, 
				nodes[lastUnpairedNode].nodeHue[0],
				nodes[lastUnpairedNode].nodeHue[1],
				nodes[lastUnpairedNode].nodeHue[2]);
	}
        ledRing.show();
      }
    break;
    
    case TURNING_ON:
      if( millis() - timestamp > 3000 ){
        Serial.println( "Transition from TURNING_ON to IDLE" );
        clearDisplay(true);
        timestamp = millis();
        state = IDLE;
      }
      else{
        for( byte i=0; i<N_LEDS; i++ )
          ledRing.setPixelColor( i, nodes[NODEID].nodeHue[0], nodes[NODEID].nodeHue[1], nodes[NODEID].nodeHue[2] );
        //setLed(true); ledRing.show(); setLed(false);
        ledRing.show();
      }
    break;
  
    case IDLE:
      if( triggerNewPair && (millis()-timestamp>2000) ){
        Serial.println( "Transition from IDLE to NEW_PAIR" );
        frame = 0;
        state = NEW_PAIR;
        triggerNewPair = false;
        timestamp = millis();
      }
      else if( triggerDisplayRssi && (millis()-timestamp>2000) && (millis() - lastShowRssi > 10000)){
        Serial.println( "Transition from IDLE to SHOWING_RSSI" );
        frame = 0;
        state = SHOWING_RSSI;
	currentBestNode = 0;
        triggerDisplayRssi = false;
	rssiTimestamp = millis();
        timestamp = millis();
      }
      else if( triggerUnpair && (millis()-timestamp>2000) ){
        Serial.println( "Transition from IDLE to UNPAIR" );
        state = UNPAIRED;
        triggerUnpair = false;
        timestamp = millis();
      }
      
    break;

  }

  if( (millis() - timestamp > 2000) && (millis() - lastWipeTimestamp > WIPE_FREQUENCY)) {
    Serial.println( "Transition from IDLE to SHOWING_WIPE" );
    frame = 0;
    state = SHOWING_WIPE;
    lastWipeTimestamp = millis();
  }

  setLed(false);
    
  delay(30);
  
  // Pat the dog (watchdog timer)
  wdt_reset();
}

// Update node status whenever we receive a packet from it
void updateNode( byte senderId, Node* n, byte rssi ){
  n->averageArray[ n->averageIndex++ ] = rssi;
  n->averageIndex = n->averageIndex % AVERAGING_ARRAY_SIZE;
  n->averageRssi = getAverage( n->averageArray, AVERAGING_ARRAY_SIZE );
  n->lastReceived = millis();
  
  Serial.print( "Average = " );
  Serial.println( n->averageRssi );
  
  // paired or not
  if( !n->paired && (n->averageRssi > PAIRED_THRESH) ){
    n->paired = true;
    triggerNewPair = true;
    lastPairedNode = senderId;
    Serial.println( "Now paired" );
  }
  else if( n->paired && (n->averageRssi < UNPAIRED_THRESH) ){
    n->paired = false;
    triggerUnpair = true;
    Serial.println( "Now UNpaired" );
  }
}

void updateBestRssi(){
  //bestRSSI = 0;
  
  unsigned long t = millis();
  bestNodesCount = 0;
  
  for( byte i=0; i<MAX_NODES; i++ ){
  
    bestNodes[i] = -1;

    // been too long since we've heard from this node
    // (Maybe it shouldn't require being paired)
    // (Maybe just flag this by setting lastReceived to zero and checking for that)
    if( t - nodes[i].lastReceived > LOST_INTERVAL ){
      if (nodes[i].paired) {
	Serial.print( "Lost node " );
	Serial.println( i );
	lastUnpairedNode = i;
	triggerUnpair = true;
      }
      nodes[i].paired = false;
      for( byte j=0; j<AVERAGING_ARRAY_SIZE; j++ )
        nodes[i].averageArray[j] = 0;
      nodes[i].averageRssi = 0;
    }
    
    // who's the closet
    if( !nodes[i].paired && nodes[i].averageRssi > 0 ){
      bestRssis[bestNodesCount] = nodes[i].averageRssi;
      bestNodes[bestNodesCount++] = i;
    }
    
  }
}

// Calculates which nodes are paired and sets that in
// pairedNodes. Returns the number of nodes that are paired
byte calcPairedNodes() {
  byte numPaired = 0;
  for (byte i = 0; i < MAX_NODES; i++) {
    if (nodes[i].paired) {
      pairedNodes[numPaired++] = i;
    }
  }
  return numPaired;
}

// takes an array, it's size, and returns the average
int getAverage( byte array[], byte size ){
  int total = 0;
  for( byte i=0; i<size; i++ ){
    total += array[i];
  }
  return total / size;
}

void blink(){
  pinMode( 9, OUTPUT );
  digitalWrite( 9, HIGH );
  delay(5);
  digitalWrite( 9, LOW );
}

void setLed(boolean on){
  digitalWrite( 9, (on ? HIGH : LOW) );
}

void clearDisplay( boolean show ){
  for( byte i=0; i<N_LEDS; i++ )
    ledRing.setPixelColor( i, 0, 0, 0 );
  if( show ){
    setLed(true); ledRing.show(); setLed(false);
  }
}

void getRGB(int hue, int sat, int val, int colors[3]) { 
  /* convert hue, saturation and brightness ( HSB/HSV ) to RGB
     The dim_curve is used only on brightness/value and on saturation (inverted).
     This looks the most natural.      
  */
 
  val = dim_curve[val];
  sat = 255-dim_curve[255-sat];
 
  int r;
  int g;
  int b;
  int base;
 
  if (sat == 0) { // Acromatic color (gray). Hue doesn't mind.
    colors[0]=val;
    colors[1]=val;
    colors[2]=val;  
  } else  { 
 
    base = ((255 - sat) * val)>>8;
 
    switch(hue/60) {
    case 0:
        r = val;
        g = (((val-base)*hue)/60)+base;
        b = base;
    break;
 
    case 1:
        r = (((val-base)*(60-(hue%60)))/60)+base;
        g = val;
        b = base;
    break;
 
    case 2:
        r = base;
        g = val;
        b = (((val-base)*(hue%60))/60)+base;
    break;
 
    case 3:
        r = base;
        g = (((val-base)*(60-(hue%60)))/60)+base;
        b = val;
    break;
 
    case 4:
        r = (((val-base)*(hue%60))/60)+base;
        g = base;
        b = val;
    break;
 
    case 5:
        r = val;
        g = base;
        b = (((val-base)*(60-(hue%60)))/60)+base;
    break;
    }
 
    colors[0]=r;
    colors[1]=g;
    colors[2]=b; 
  }   
}

void convert_hcl_to_rgb(float h, float c, float l, byte rgb[]){
  float redf, greenf, bluef;
  if (c == 0 ){
    redf = greenf = bluef = l;
  }
  float temp2;
  if ( l < 0.5) temp2 = l * ( 1 + c);
  else temp2 = l + c - l * c;
  float temp1 = 2.0 * l - temp2;
  float rtemp = h + 0.33333;
  float gtemp = h ;
  float btemp = h - 0.33333;
  if (rtemp > 1 ) rtemp -= 1;
  else if (rtemp < 0 ) rtemp += 1;
  if (gtemp > 1 ) gtemp -= 1;
  else if (gtemp < 0 ) gtemp += 1;
  if (btemp > 1 ) btemp -= 1;
  else if (btemp < 0 ) btemp += 1;
  
  if ( 6.0 * rtemp < 1 ) redf = temp1+ (temp2-temp1) *6.0*rtemp;
  else if ( 2.0 * rtemp < 1 ) redf = temp2;
  else if ( 3.0 * rtemp < 2 ) redf = temp1+ (temp2-temp1) *(0.6666-rtemp)*6.0;
  else redf = temp1;
  
  if ( 6.0 * gtemp < 1 ) greenf = temp1+ (temp2-temp1) *6.0*gtemp;
  else if ( 2.0 * gtemp < 1 ) greenf = temp2;
  else if ( 3.0 * gtemp < 2 ) greenf = temp1+ (temp2-temp1) *(0.6666-gtemp)*6.0;
  else greenf = temp1;
  
  if ( 6.0 * btemp < 1 ) bluef = temp1+ (temp2-temp1) *6.0*btemp;
  else if ( 2.0 * btemp < 1 ) bluef = temp2;
  else if ( 3.0 * btemp < 2 ) bluef = temp1+ (temp2-temp1) *(0.6666-btemp)*6.0;
  else bluef = temp1;
  
  Serial.print( "redf: " ); Serial.println( redf );
  Serial.print( "greenf: " ); Serial.println( greenf );
  Serial.print( "bluef: " ); Serial.println( bluef );
  Serial.println();
  rgb[0] = (byte) ( 255 * redf);
  rgb[1] = (byte) ( 255 * greenf);
  rgb[2] = (byte) ( 255 * bluef);
}
