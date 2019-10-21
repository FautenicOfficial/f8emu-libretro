#ifndef _FAKE6502_H_
#define _FAKE6502_H_

//Includes
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

//Memory variables
uint16_t pc;
uint8_t sp, a, x, y, status;

uint32_t instructions;
uint32_t clockticks6502, clockgoal6502;
uint16_t oldpc, ea, reladdr, value, result;
uint8_t opcode, oldstatus;

uint8_t memory       [0x10000];
uint8_t gameHeader    [0x2000];

uint8_t romBankData [0x800000];
uint8_t chrBankData [0x200000];
uint8_t bgtBankData [0x100000];
uint8_t bgpBankData [0x040000];
uint8_t wramBankData[0x200000];
uint8_t sramBankData[0x080000];
uint8_t palBankData [0x002000];

//Constants
extern uint8_t * bankDataPtrs[7];
extern const int bankMasks[7];
extern const int bankOffs[7];
extern const int bankSizes[7];
extern const int bankShiftMax[7];
extern const int bankGranularity[56];
extern const int powersOfTwoLut[10];

//Functions
void reset6502();
void exec6502(uint32_t tickcount);
void step6502();
void irq6502();
void nmi6502();

#endif