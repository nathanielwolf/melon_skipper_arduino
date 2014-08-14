#include <SPI.h>
#include <RFM69.h>
#include <RFM69registers.h>
#include <Adafruit_NeoPixel.h>
#include "MyTypes.h"                  // Arduino hack to make structs work properly

#define NODEID           3            // this should be unique for each node
#define NETWORKID        100          // every node should be on the same network
#define FREQUENCY        RF69_433MHZ
#define MAX_NODES        12           // max number of nodes in our network
#define PAIRED_THRESH    75           // threshold that determines nodes are paired (nearby)
#define UNPAIRED_THRESH  50           // after going below this threshold, nodes are unpaired (lost)
#define SEND_INTERVAL    2000
#define LOST_INTERVAL    20000        // if we haven't heard from a node for this long, they're lost
#define BRIGHTNESS       100          // global control over LED brightness

#define N_LEDS           16
#define LED_PIN          A0
//#define LED_PIN          4

#define WIPE_FRAMES      64
short frame = 0;
unsigned long timestamp;

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

RFM69 radio;
bool promiscuousMode = false;
unsigned long lastSend = 0;
byte bestRSSI = 0;
byte bestNode;

Adafruit_NeoPixel ledRing = Adafruit_NeoPixel( N_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800 );

void setup(){
  
  Serial.begin( 9600 );
  delay(10);
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
  
  // initialize the nodes array
  for( byte i=0; i<MAX_NODES; i++ ){
    nodes[i].paired = false;
    // calculate the node colors from HSV
    getRGB( (255/MAX_NODES)*i, 255, 255, nodes[i].nodeHue );
    getRGB( (255/MAX_NODES)*i, 70, 255, nodes[i].brightenedHue );
  }
  
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
      updateNode( &nodes[ radio.SENDERID ], map( radio.RSSI, -25, -100, 100, 0 ) );
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
        // show the wipe
        byte pattern[6] = {20, 50, 100, 100, 50, 20};
        if( frame%2 == 0 ){ // slow down the animation a little
          clearDisplay(false);
          short i=frame/2;
          for( short j=6; j>=0 && i>=0; j--, i-- ){
            ledRing.setPixelColor( i, pattern[j], pattern[j], pattern[j] );
          }
        }
        frame++;
        ledRing.show();
      }
    break;
    
    case SHOWING_RSSI:
      if( frame > bestRSSI/6 ){  // if we've filled out the display
        if( millis() - timestamp > 3000 ){  // and the display has been on for 3s
          // clear the display and transition to idle
          Serial.println( "Transition from SHOWING_RSSI to IDLE" );
          state = IDLE;
          clearDisplay(true);
          timestamp = millis();
        }
      }
      else{
        for( byte i=0; i<frame; i++ ){
          if( (i+1 > 2) && (i+1 == bestRSSI/6 ) )
            ledRing.setPixelColor( i, nodes[bestNode].brightenedHue[0], nodes[bestNode].brightenedHue[1], nodes[bestNode].brightenedHue[2] );
          else
            ledRing.setPixelColor( i, nodes[bestNode].nodeHue[0], nodes[bestNode].nodeHue[1], nodes[bestNode].nodeHue[2] );
        }
        //setLed(true); ledRing.show(); setLed(false);
        ledRing.show();
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
        for( byte i=0; i<N_LEDS; i++ )
          ledRing.setPixelColor( i, 0, 255, 0 );
        //setLed(true); ledRing.show(); setLed(false);
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
        for( byte i=0; i<N_LEDS; i++ )
          ledRing.setPixelColor( i, 255, 0, 0 );
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
      else if( triggerDisplayRssi && (millis()-timestamp>2000) ){
        Serial.println( "Transition from IDLE to SHOWING_RSSI" );
        Serial.print( "Best RSSI: " ); Serial.println( bestRSSI );
        Serial.print( "Best Node: " ); Serial.println( bestNode );
        frame = 0;
        state = SHOWING_RSSI;
        triggerDisplayRssi = false;
        timestamp = millis();
      }
      else if( triggerUnpair && (millis()-timestamp>2000) ){
        Serial.println( "Transition from IDLE to UNPAIR" );
        state = UNPAIRED;
        triggerUnpair = false;
        timestamp = millis();
      }
      else if( millis()-timestamp > 10*1000 ){
        Serial.println( "Transition from IDLE to SHOWING_WIPE" );
        frame = 0;
        state = SHOWING_WIPE;
      }
    break;
    
  }
  setLed(false);
    
  delay(30);
}

// Update node status whenever we receive a packet from it
void updateNode( Node* n, byte rssi ){
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
    Serial.println( "Now paired" );
  }
  else if( n->paired && (n->averageRssi < UNPAIRED_THRESH) ){
    n->paired = false;
    triggerUnpair = true;
    Serial.println( "Now UNpaired" );
  }
}

void updateBestRssi(){
  bestRSSI = 0;
  
  unsigned long t = millis();
  for( byte i=0; i<MAX_NODES; i++ ){
    
    // been too long since we've heard from this node
    // (Maybe it shouldn't require being paired)
    // (Maybe just flag this by setting lastReceived to zero and checking for that)
    if( nodes[i].paired && (t-nodes[i].lastReceived > LOST_INTERVAL) ){
      Serial.print( "Lost node " );
      Serial.println( i );
      nodes[i].paired = false;
      triggerUnpair = true;
      for( byte j=0; j<AVERAGING_ARRAY_SIZE; j++ )
        nodes[i].averageArray[j] = 0;
      nodes[i].averageRssi = 0;
    }
    
    // who's the closet
    if( !nodes[i].paired && (nodes[i].averageRssi > bestRSSI) ){
      bestRSSI = nodes[i].averageRssi;
      bestNode = i;
    }
    
  }
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
