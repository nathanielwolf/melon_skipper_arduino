#include <SPI.h>
#include <RFM69.h>
#include <RFM69registers.h>

#define NODEID     1
#define NETWORKID  100
#define FREQUENCY  RF69_433MHZ
#define MAX_NODES  20
#define PAIRED_THRESH    
#define UNPAIRED_THRESH

enum States {
  
};

// record of last received packets
typedef struct {
  short lastRSSI;
  short avgRSSI;
  unsigned long lastReceived;
} node;
node nodes[ MAX_NODES ];  // array of up to MAX_NODES

bool promiscuousMode = true;
unsigned long lastSend = 0;

RFM69 radio;

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
  
}

void loop(){
  
  if( radio.receiveDone() ){
    Serial.print('[');Serial.print(radio.SENDERID, DEC);Serial.print("] ");
    Serial.print("to [");Serial.print(radio.TARGETID, DEC);Serial.print("] ");
    for (byte i = 0; i < radio.DATALEN; i++){
      Serial.print((char)radio.DATA[i]);
    }
    Serial.print("   [RX_RSSI:");Serial.print(radio.RSSI);Serial.print("]");
    Serial.println();
    
    // store received strength
    if( radio.SENDERID > MAX_NODES || radio.SENDERID < 0 ){
      Serial.println( "Bad sender ID" );
    }
    else{
      nodes[ radio.SENDERID ].lastRSSI = map( radio.RSSI, -25, -100, 0, 100 );
      nodes[ radio.SENDERID ].lastReceived = millis();
    }
  }
  
  if( millis() - lastSend > 2000 ){
    lastSend = millis();
    
    delay(3);  // delay after reception
    if( NODEID == 1 ){
      Serial.println( "Sending to Melon" );
      radio.send( 2, "Melon!", 6, false );
    }
    else{
      Serial.println( "Sending to Skipper" );
      radio.send( 1, "Skipper!", 8, false );
    }
  }
  
}

int calculateDisplay(){
 
  int bestRSSI;
  
}

