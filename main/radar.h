#pragma once
#include <stdint.h>
#include "Ld2410.h"

// HLK-LD2410(B/C) presence radar on Port C (the blue Grove port, UART).
// radarBegin() opens the port; radarPoll() (call often, from one task) reads
// what arrived. The radar is "found" once valid readings come in. If the two
// signal wires are crossed the wrong way, it swaps them by itself.
void radarBegin();
void radarPoll(uint32_t nowMs);
bool radarFound();                   // readings have arrived since start
bool radarFresh(uint32_t nowMs);     // a reading in the last 2 s
const RadarReading& radarLatest();
