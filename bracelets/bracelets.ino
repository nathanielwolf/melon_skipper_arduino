#include <SPI.h>
#include <RFM69.h>
#include <RFM69registers.h>
#include <Adafruit_NeoPixel.h>
#include "MyTypes.h"                  // Arduino hack to make structs work properly

#define NODEID           2            // this should be unique for each node
#define NETWORKID        100          // every node should be on the same network
#define FREQUENCY        RF69_433MHZ
#define MAX_NODES        15           // max number of nodes in our network
#define PAIRED_THRESH    75           // threshold that determines nodes are paired (nearby)
#define UNPAIRED_THRESH  50           // after going below this threshold, nodes are unpaired (lost)
#define SEND_INTERVAL    2000
#define LOST_INTERVAL    20000        // if we haven't heard from a node for this long, they're lost

#define N_LEDS           16
#define LED_PIN          4

#define WIPE_FRAMES      32
short frame = 0;
unsigned long timestamp;

Node nodes[ MAX_NODES ];  // array of up to MAX_NODES

enum States{
  SHOWING_RSSI,
  SHOWING_WIPE,
  NEW_PAIR,
  IDLE
};
States state = SHOWING_WIPE;
boolean triggerDisplayRssi = false;
boolean triggerNewPair = false;

RFM69 radio;
bool promiscuousMode = false;
unsigned long lastSend = 0;
byte bestRSSI = 0;

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
  
  for( byte i=0; i<MAX_NODES; i++ ){
    nodes[i].paired = false;
  }
  
  Serial.print( "This is node: " );
  Serial.println( NODEID );
  
  ledRing.begin();
  ledRing.setBrightness( 100 );
}

void loop(){
  
  // receive a packet if there is one
  if( radio.receiveDone() ){
    triggerDisplayRssi = true;
    Serial.print('Received from [');Serial.print(radio.SENDERID);Serial.print("] ");
    //Serial.print("to [");Serial.print(radio.TARGETID, DEC);Serial.print("] ");
    //for ( byte i = 0; i < radio.DATALEN; i++ ){
    //  Serial.print( (char)radio.DATA[i] );
    //}
    
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
  
  // send a ping
  if( millis() - lastSend > SEND_INTERVAL ){
    lastSend = millis();
    
    delay(3);  // delay after reception (from sample code)
    radio.send( RF69_BROADCAST_ADDR, ")'(", 3, false );
    Serial.println( "Tx" );
    blink();
  }
  
  updateBestRssi();
  
  // state machine
  switch( state ){
    
    case SHOWING_WIPE:
      // state management
      if( frame >= WIPE_FRAMES ){
        Serial.println( "Transition from SHOWING_WIPE to IDLE " );
        frame = 0;
        state = IDLE;
        timestamp = millis();
        clearDisplay(true);
        break;
      }
      // show the wipe
      clearDisplay(false);
      for( byte i=frame/2; i<(frame/2+4); i++ )
        ledRing.setPixelColor( i, 255, 255, 255 );
      frame++;
      ledRing.show();
    break;
    
    case SHOWING_RSSI:
      if( frame > bestRSSI/6 ){  // if we've filled out the display
        if( millis() - timestamp > 3000 ){  // and the display has been on for 3s
          // clear the display and transition to idle
          Serial.println( "Transition from SHOWING_RSSI to IDLE" );
          state = IDLE;
          clearDisplay(true);
          timestamp = millis();
          break;
        }
      }
      else{
        // otherwise, keep filling out the display
        for( byte i=0; i<frame; i++ ){
          if( (i+1 > 2) && (i+1 == bestRSSI/6 ) )
            ledRing.setPixelColor( i, 255, 255, 255 );
          else
            ledRing.setPixelColor( i, 255, 50, 0 );
        }
        ledRing.show();
      }
      frame++;
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
      else if( triggerDisplayRssi && (millis()-timestamp>4000) ){
        Serial.println( "Transition from IDLE to SHOWING_RSSI" );
        Serial.print( "Best RSSI: " ); Serial.println( bestRSSI );
        frame = 0;
        state = SHOWING_RSSI;
        triggerDisplayRssi = false;
        timestamp = millis();
      }
      else if( millis()-timestamp > 10*1000 ){
        Serial.println( "Transition from IDLE to SHOWING_WIPE" );
        frame = 0;
        state = SHOWING_WIPE;
      }
    break;
    
  }
    
  delay(60);
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
      for( byte j=0; j<AVERAGING_ARRAY_SIZE; j++ )
        nodes[i].averageArray[j] = 0;
      nodes[i].averageRssi = 0;
    }
    
    // who's the closet
    if( !nodes[i].paired && (nodes[i].averageRssi > bestRSSI) ){
      bestRSSI = nodes[i].averageRssi;
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

void clearDisplay( boolean show ){
  for( byte i=0; i<N_LEDS; i++ )
    ledRing.setPixelColor( i, 0, 0, 0 );
  if( show )
    ledRing.show();
}
