#ifndef MyTypes_h
#define MyTypes_h

#include <WString.h>

#define AVERAGING_ARRAY_SIZE 4

// record of contact w/ nodes
typedef struct {
  byte averageArray[ AVERAGING_ARRAY_SIZE ];
  byte averageIndex;
  byte averageRssi;
  unsigned long lastReceived;  // timestamp of last recieved packet
  boolean paired;
  int nodeHue[3];
  int brightenedHue[3];
} Node;

#endif
