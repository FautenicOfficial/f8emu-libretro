/* Fake6502 CPU emulator core v1.1 *******************
 * (c)2011 Mike Chambers (miker00lz@gmail.com)       *
 *****************************************************
 * v1.1 - Small bugfix in BIT opcode, but it was the *
 *        difference between a few games in my NES   *
 *        emulator working and being broken!         *
 *        I went through the rest carefully again    *
 *        after fixing it just to make sure I didn't *
 *        have any other typos! (Dec. 17, 2011)      *
 *                                                   *
 * v1.0 - First release (Nov. 24, 2011)              *
 *****************************************************
 * LICENSE: This source code is released into the    *
 * public domain, but if you use it please do give   *
 * credit. I put a lot of effort into writing this!  *
 *                                                   *
 *****************************************************
 * Fake6502 is a MOS Technology 6502 CPU emulation   *
 * engine in C. It was written as part of a Nintendo *
 * Entertainment System emulator I've been writing.  *
 *                                                   *
 * If you do discover an error in timing accuracy,   *
 * or operation in general please e-mail me at the   *
 * address above so that I can fix it. Thank you!    *
 *                                                   *
 *****************************************************
 * Usage:                                            *
 *                                                   *
 * Fake6502 requires you to provide two external     *
 * functions:                                        *
 *                                                   *
 * uint8_t read6502(uint16_t address)                *
 * void write6502(uint16_t address, uint8_t value)   *
 *                                                   *
 * You may optionally pass Fake6502 the pointer to a *
 * function which you want to be called after every  *
 * emulated instruction. This function should be a   *
 * void with no parameters expected to be passed to  *
 * it.                                               *
 *                                                   *
 * This can be very useful. For example, in a NES    *
 * emulator, you check the number of clock ticks     *
 * that have passed so you can know when to handle   *
 * APU events.                                       *
 *                                                   *
 * To pass Fake6502 this pointer, use the            *
 * hookexternal(void *funcptr) function provided.    *
 *                                                   *
 * To disable the hook later, pass NULL to it.       *
 *****************************************************
 * Useful functions in this emulator:                *
 *                                                   *
 * void reset6502()                                  *
 *   - Call this once before you begin execution.    *
 *                                                   *
 * void exec6502(uint32_t tickcount)                 *
 *   - Execute 6502 code up to the next specified    *
 *     count of clock ticks.                         *
 *                                                   *
 * void step6502()                                   *
 *   - Execute a single instrution.                  *
 *                                                   *
 * void irq6502()                                    *
 *   - Trigger a hardware IRQ in the 6502 core.      *
 *                                                   *
 * void nmi6502()                                    *
 *   - Trigger an NMI in the 6502 core.              *
 *                                                   *
 * void hookexternal(void *funcptr)                  *
 *   - Pass a pointer to a void function taking no   *
 *     parameters. This will cause Fake6502 to call  *
 *     that function once after each emulated        *
 *     instruction.                                  *
 *                                                   *
 *****************************************************
 * Useful variables in this emulator:                *
 *                                                   *
 * uint32_t clockticks6502                           *
 *   - A running total of the emulated cycle count.  *
 *                                                   *
 * uint32_t instructions                             *
 *   - A running total of the total emulated         *
 *     instruction count. This is not related to     *
 *     clock cycle timing.                           *
 *                                                   *
 *****************************************************/

#include "fake6502.h"

#define FLAG_CARRY     0x01
#define FLAG_ZERO      0x02
#define FLAG_INTERRUPT 0x04
#define FLAG_DECIMAL   0x08
#define FLAG_BREAK     0x10
#define FLAG_CONSTANT  0x20
#define FLAG_OVERFLOW  0x40
#define FLAG_SIGN      0x80

#define BASE_STACK     0x100

#define saveaccum(n) a = (uint8_t)((n) & 0x00FF)


//flag modifier macros
#define setcarry() status |= FLAG_CARRY
#define clearcarry() status &= (~FLAG_CARRY)
#define setzero() status |= FLAG_ZERO
#define clearzero() status &= (~FLAG_ZERO)
#define setinterrupt() status |= FLAG_INTERRUPT
#define clearinterrupt() status &= (~FLAG_INTERRUPT)
#define setdecimal() status |= FLAG_DECIMAL
#define cleardecimal() status &= (~FLAG_DECIMAL)
#define setoverflow() status |= FLAG_OVERFLOW
#define clearoverflow() status &= (~FLAG_OVERFLOW)
#define setsign() status |= FLAG_SIGN
#define clearsign() status &= (~FLAG_SIGN)


//flag calculation macros
#define zerocalc(n) {\
    if ((n) & 0x00FF) clearzero();\
        else setzero();\
}

#define signcalc(n) {\
    if ((n) & 0x0080) setsign();\
        else clearsign();\
}

#define carrycalc(n) {\
    if ((n) & 0xFF00) setcarry();\
        else clearcarry();\
}

#define overflowcalc(n, m, o) { /* n = result, m = accumulator, o = memory */ \
    if (((n) ^ (uint16_t)(m)) & ((n) ^ (o)) & 0x0080) setoverflow();\
        else clearoverflow();\
}


//6502 CPU registers
/*uint16_t pc;
uint8_t sp, a, x, y, status;


//helper variables
uint32_t instructions = 0; //keep track of total instructions executed
uint32_t clockticks6502 = 0, clockgoal6502 = 0;
uint16_t oldpc, ea, reladdr, value, result;
uint8_t opcode, oldstatus;

//externally supplied functions
uint8_t memory[0x10000];
uint8_t gameHeader[0x2000];
uint8_t romBankData[0x800000];
uint8_t chrBankData[0x200000];
uint8_t bgtBankData[0x100000];
uint8_t bgpBankData[0x40000];
uint8_t wramBankData[0x200000];
uint8_t sramBankData[0x80000];
uint8_t palBankData[0x2000];*/

uint8_t * bankDataPtrs[7] = {romBankData,chrBankData,bgtBankData,bgpBankData,wramBankData,palBankData};
const int bankMasks[7] = {0x8000,0xE000,0xF000,0xFC00,0xE000,0xF800,0xFFE0};
const int  bankOffs[7] = {0x8000,0x4000,0x6000,0x7000,0x2000,0x7800,0x7500};
const int bankSizes[7] = {0x8000,0x2000,0x1000,0x0400,0x2000,0x0800,0x0020};
const int bankShiftMax[7] =  {16,    14,    13,    11,    14,    12,     7};
const int bankGranularity[56] = {2,4,3,4,3,4,3,5,
                                 1,4,3,4,2,4,3,4,
                                 1,4,3,4,2,4,3,4,
                                 1,4,3,4,2,4,3,4,
                                 1,4,3,4,2,4,3,4,
                                 1,4,3,4,2,4,3,4,
                                 1,4,3,4,2,4,3,4};
const int powersOfTwoLut[10] = {0,1,2,4,8,16,32,64,128,256};

uint8_t read6502(uint16_t address) {
	return memory[address];
}
void write6502(uint16_t address, uint8_t value) {
	//Check if we can write to RAM, and if we need to write to SRAM
	if((address&0x8000)==0) {
		bool canWriteToRam = true;
		for(int i=1; i<7; i++) {
			if((address&bankMasks[i])==bankOffs[i]) {
				if(gameHeader[0x10|i]) {
					if(gameHeader[0x18|i]&0x80) {
						int a = gameHeader[0x18|i]&0x7F;
						int offset = address-bankOffs[i];
						if(a==0) {
							bankDataPtrs[i][offset] = value;
						} else {
							for(int j=1; j<=4; j++) {
								if(a==j) {
									int b = offset&(bankSizes[i]-(bankSizes[i]>>(a-1)));
									int c = offset&((bankSizes[i]-1)>>(a-1));
									bankDataPtrs[i][(memory[0x7440|(i<<3)|(b>>(bankShiftMax[i]-4))]<<(bankShiftMax[i]-a))|c] = value;
									break;
								}
							}
						}
					} else {
						canWriteToRam = false;
					}
				}
				break;
			}
		}
		if(canWriteToRam) {
			memory[address] = value;
		}
	}
	//Check if we need to switch banks
	if(((address&0xFFC0)==0x7440)&&(address!=0x7447)&&((address&0xFFF8)!=0x7478)) {
		int i = (address>>3)&7;
		int b = address&7;
		int a = gameHeader[0x18|i]&0x7F;
		int realByte = value&(powersOfTwoLut[gameHeader[0x10|i]+a]-1);
		for(int j=bankGranularity[address&0x3F]; j<=4; j++) {
			if(a==j) {
				int offset = realByte<<(bankShiftMax[i]-a);
				for(int k=0; k<(bankSizes[i]>>(a-1)); k++) {
					memory[bankOffs[i]|((bankSizes[i]>>3)*b)|k] = bankDataPtrs[i][offset|k];
				}
			}
		}
	}
}

//a few general functions used by various other functions
void push16(uint16_t pushval) {
    write6502(BASE_STACK + sp, (pushval >> 8) & 0xFF);
    write6502(BASE_STACK + ((sp - 1) & 0xFF), pushval & 0xFF);
    sp -= 2;
}

void push8(uint8_t pushval) {
    write6502(BASE_STACK + sp--, pushval);
}

uint16_t pull16() {
    uint16_t temp16;
    temp16 = read6502(BASE_STACK + ((sp + 1) & 0xFF)) | ((uint16_t)read6502(BASE_STACK + ((sp + 2) & 0xFF)) << 8);
    sp += 2;
    return(temp16);
}

uint8_t pull8() {
    return (read6502(BASE_STACK + ++sp));
}

void reset6502() {
    pc = (uint16_t)read6502(0xFFFC) | ((uint16_t)read6502(0xFFFD) << 8);
    a = 0;
    x = 0;
    y = 0;
    sp = 0xFF;
    status |= FLAG_CONSTANT;
}


static void (*addrtable[256])();
static void (*optable[256])();
uint8_t penaltyop, penaltyaddr;

//addressing mode functions, calculates effective addresses
static void imp() { //implied
}

static void acc() { //accumulator
}

static void imm() { //immediate
    ea = pc++;
}

static void zp() { //zero-page
    ea = (uint16_t)read6502((uint16_t)pc++);
}

static void zpx() { //zero-page,X
    ea = ((uint16_t)read6502((uint16_t)pc++) + (uint16_t)x) & 0xFF; //zero-page wraparound
}

static void zpy() { //zero-page,Y
    ea = ((uint16_t)read6502((uint16_t)pc++) + (uint16_t)y) & 0xFF; //zero-page wraparound
}

static void rel() { //relative for branch ops (8-bit immediate value, sign-extended)
    reladdr = (uint16_t)read6502(pc++);
    if (reladdr & 0x80) reladdr |= 0xFF00;
}

static void abso() { //absolute
    ea = (uint16_t)read6502(pc) | ((uint16_t)read6502(pc+1) << 8);
    pc += 2;
}

static void absx() { //absolute,X
    ea = ((uint16_t)read6502(pc) | ((uint16_t)read6502(pc+1) << 8));
    uint16_t startpage = ea & 0xFF00;
    ea += (uint16_t)x;

    if (startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
        penaltyaddr = 1;
    }

    pc += 2;
}

static void absy() { //absolute,Y
    ea = ((uint16_t)read6502(pc) | ((uint16_t)read6502(pc+1) << 8));
    uint16_t startpage = ea & 0xFF00;
    ea += (uint16_t)y;

    if (startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
        penaltyaddr = 1;
    }

    pc += 2;
}

static void ind() { //indirect
    uint16_t eahelp = (uint16_t)read6502(pc) | (uint16_t)((uint16_t)read6502(pc+1) << 8);
    ea = (uint16_t)read6502(eahelp) | ((uint16_t)read6502(eahelp+1) << 8);
    pc += 2;
}

static void indx() { // (indirect,X)
    uint16_t eahelp = (uint16_t)(((uint16_t)read6502(pc++) + (uint16_t)x) & 0xFF); //zero-page wraparound for table pointer
    ea = (uint16_t)read6502(eahelp & 0x00FF) | ((uint16_t)read6502((eahelp+1) & 0x00FF) << 8);
}

static void indy() { // (indirect),Y
    uint16_t eahelp = (uint16_t)read6502(pc++);
    ea = (uint16_t)read6502(eahelp) | ((uint16_t)read6502((eahelp+1) & 0x00FF) << 8); //zero-page wraparound
    uint16_t startpage = ea & 0xFF00;
    ea += (uint16_t)y;

    if (startpage != (ea & 0xFF00)) { //one cycle penlty for page-crossing on some opcodes
        penaltyaddr = 1;
    }
}

//Additional 65C02 addressing modes
static void indzp() { //(zero-page)
    uint16_t eahelp = (uint16_t)((uint16_t)read6502(pc++)); //zero-page wraparound for table pointer
    ea = (uint16_t)read6502(eahelp & 0x00FF) | ((uint16_t)read6502((eahelp+1) & 0x00FF) << 8);
}

static void indabsx() { // (absolute,X) (for jumping)
    uint16_t eahelp = (uint16_t)read6502(pc) | (uint16_t)((uint16_t)read6502(pc+1) << 8);
    ea = (uint16_t)read6502(eahelp+x) | ((uint16_t)read6502(eahelp+x+1) << 8);
    pc += 2;
}

static uint16_t getvalue() {
    if (addrtable[opcode] == acc) return((uint16_t)a);
        else return((uint16_t)read6502(ea));
}

static uint16_t getvalue16() {
    return((uint16_t)read6502(ea) | ((uint16_t)read6502(ea+1) << 8));
}

static void putvalue(uint16_t saveval) {
    if (addrtable[opcode] == acc) a = (uint8_t)(saveval & 0x00FF);
        else write6502(ea, (saveval & 0x00FF));
}
int tobcd(uint16_t x) {
	int ones = (x&0xF);
	int tens = (x>>4)&0xF;
	return (10*tens)+ones;
}
uint16_t frombcd(int x) {
	int ones = x%10;
	int tens = x/10;
	return ones|(tens<<4);
}


//instruction handler functions
static void adc() {
    penaltyop = 1;
    value = getvalue();
    
    if (status & FLAG_DECIMAL) {
	    int realA = tobcd(a);
	    int realVal = tobcd(value);
	    int realRes = realA+realVal+(status&FLAG_CARRY);
	    result = frombcd(realRes%100);
	    if(realRes>=100) {
		    setcarry();
	    } else {
		    clearcarry();
	    }
    } else {
	result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
	    carrycalc(result);
    }
    
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);
    saveaccum(result);
}

static void and() {
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a & value;
   
    zerocalc(result);
    signcalc(result);
   
    saveaccum(result);
}

static void asl() {
    value = getvalue();
    result = value << 1;

    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void bcc() {
    if ((status & FLAG_CARRY) == 0) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bcs() {
    if ((status & FLAG_CARRY) == FLAG_CARRY) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void beq() {
    if ((status & FLAG_ZERO) == FLAG_ZERO) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bit() {
    value = getvalue();
    result = (uint16_t)a & value;
   
    zerocalc(result);
    status = (status & 0x3F) | (uint8_t)(value & 0xC0);
}

static void bmi() {
    if ((status & FLAG_SIGN) == FLAG_SIGN) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bne() {
    if ((status & FLAG_ZERO) == 0) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bpl() {
    if ((status & FLAG_SIGN) == 0) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void brk() {
    pc++;
    push16(pc); //push next instruction address onto stack
    push8(status | FLAG_BREAK); //push CPU status to stack
    setinterrupt(); //set interrupt flag
    pc = (uint16_t)read6502(0xFFFE) | ((uint16_t)read6502(0xFFFF) << 8);
}

static void bvc() {
    if ((status & FLAG_OVERFLOW) == 0) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void bvs() {
    if ((status & FLAG_OVERFLOW) == FLAG_OVERFLOW) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
    }
}

static void clc() {
    clearcarry();
}

static void cld() {
    cleardecimal();
}

static void cli() {
    clearinterrupt();
}

static void clv() {
    clearoverflow();
}

static void cmp() {
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a - value;
   
    if (a >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (a == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cpx() {
    value = getvalue();
    result = (uint16_t)x - value;
   
    if (x >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (x == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cpy() {
    value = getvalue();
    result = (uint16_t)y - value;
   
    if (y >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (y == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void dec() {
    value = getvalue();
    result = value - 1;
   
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void dex() {
    x--;
   
    zerocalc(x);
    signcalc(x);
}

static void dey() {
    y--;
   
    zerocalc(y);
    signcalc(y);
}

static void eor() {
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a ^ value;
   
    zerocalc(result);
    signcalc(result);
   
    saveaccum(result);
}

static void inc() {
    value = getvalue();
    result = value + 1;
   
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void inx() {
    x++;
   
    zerocalc(x);
    signcalc(x);
}

static void iny() {
    y++;
   
    zerocalc(y);
    signcalc(y);
}

static void jmp() {
    pc = ea;
}

static void jsr() {
    push16(pc - 1);
    pc = ea;
}

static void lda() {
    penaltyop = 1;
    value = getvalue();
    a = (uint8_t)(value & 0x00FF);
   
    zerocalc(a);
    signcalc(a);
}

static void ldx() {
    penaltyop = 1;
    value = getvalue();
    x = (uint8_t)(value & 0x00FF);
   
    zerocalc(x);
    signcalc(x);
}

static void ldy() {
    penaltyop = 1;
    value = getvalue();
    y = (uint8_t)(value & 0x00FF);
   
    zerocalc(y);
    signcalc(y);
}

static void lsr() {
    value = getvalue();
    result = value >> 1;
   
    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void nop() {
    switch (opcode) {
        case 0x1C:
        case 0x3C:
        case 0x5C:
        case 0x7C:
        case 0xDC:
        case 0xFC:
            penaltyop = 1;
            break;
    }
}

static void ora() {
    penaltyop = 1;
    value = getvalue();
    result = (uint16_t)a | value;
   
    zerocalc(result);
    signcalc(result);
   
    saveaccum(result);
}

static void pha() {
    push8(a);
}

static void php() {
    push8(status | FLAG_BREAK);
}

static void pla() {
    a = pull8();
   
    zerocalc(a);
    signcalc(a);
}

static void plp() {
    status = pull8() | FLAG_CONSTANT;
}

static void rol() {
    value = getvalue();
    result = (value << 1) | (status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void ror() {
    value = getvalue();
    result = (value >> 1) | ((status & FLAG_CARRY) << 7);
   
    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void rti() {
    status = pull8();
    value = pull16();
    pc = value;
}

static void rts() {
    value = pull16();
    pc = value + 1;
}

static void sbc() {
    penaltyop = 1;
    value = getvalue();
    uint8_t carry = (status & FLAG_CARRY)^1;

    if (status & FLAG_DECIMAL) {
	    int realA = tobcd(a);
	    int realVal = 99-tobcd(value);
	    int realRes = realA+realVal+(status&FLAG_CARRY);
	    result = frombcd(realRes%100);
	    if(realRes>=100) {
		    setcarry();
	    } else {
		    clearcarry();
	    }
    } else {
	    result = (uint16_t)a - value - carry;
	    carrycalc(result);
	    status ^= 1;
    }
   
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);
    saveaccum(result);
}

static void sec() {
    setcarry();
}

static void sed() {
    setdecimal();
}

static void sei() {
    setinterrupt();
}

static void sta() {
    putvalue(a);
}

static void stx() {
    putvalue(x);
}

static void sty() {
    putvalue(y);
}

static void tax() {
    x = a;
   
    zerocalc(x);
    signcalc(x);
}

static void tay() {
    y = a;
   
    zerocalc(y);
    signcalc(y);
}

static void tsx() {
    x = sp;
   
    zerocalc(x);
    signcalc(x);
}

static void txa() {
    a = x;
   
    zerocalc(a);
    signcalc(a);
}

static void txs() {
    sp = x;
}

static void tya() {
    a = y;
   
    zerocalc(a);
    signcalc(a);
}

//Additional 65C02 instructions
static void tsb() {
	value = getvalue();
	result = ((uint16_t)a)|value;
	uint8_t zres = ((uint16_t)a)&value;
	zerocalc(zres);
	putvalue(result);
}

static void trb() {
	value = getvalue();
	result = (~((uint16_t)a))&value;
	uint8_t zres = ((uint16_t)a)&value;
	zerocalc(zres);
	putvalue(result);
}

static void stz() {
	putvalue(0);
}

static void phx() {
	push8(x);
}

static void phy() {
	push8(y);
}

static void plx() {
	x = pull8();
}

static void ply() {
	y = pull8();
}

static void bra() {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; //check if jump crossed a page boundary
            else clockticks6502++;
}

static void (*addrtable[256])() = {
/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |     */
/* 0 */     imp, indx,  imm,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  acc,  imp, abso, abso, abso,  imp, /* 0 */
/* 1 */     rel, indy,indzp,  imp,   zp,  zpx,  zpx,  imp,  imp, absy,  acc,  imp, abso, absx, absx,  imp, /* 1 */
/* 2 */    abso, indx,  imm,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  acc,  imp, abso, abso, abso,  imp, /* 2 */
/* 3 */     rel, indy,indzp,  imp,  zpx,  zpx,  zpx,  imp,  imp, absy,  acc,  imp, absx, absx, absx,  imp, /* 3 */
/* 4 */     imp, indx,  imm,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  acc,  imp, abso, abso, abso,  imp, /* 4 */
/* 5 */     rel, indy,indzp,  imp,  zpx,  zpx,  zpx,  imp,  imp, absy,  imp,  imp, abso, absx, absx,  imp, /* 5 */
/* 6 */     imp, indx,  imm,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  acc,  imp,  ind, abso, abso,  imp, /* 6 */
/* 7 */     rel, indy,indzp,  imp,  zpx,  zpx,  zpx,  imp,  imp, absy,  imp,  imp,indabsx,absx,absx,  imp, /* 7 */
/* 8 */     rel, indx,  imm,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  imp,  imp, abso, abso, abso,  imp, /* 8 */
/* 9 */     rel, indy,indzp,  imp,  zpx,  zpx,  zpy,  imp,  imp, absy,  imp,  imp, abso, absx, absx,  imp, /* 9 */
/* A */     imm, indx,  imm,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  imp,  imp, abso, abso, abso,  imp, /* A */
/* B */     rel, indy,indzp,  imp,  zpx,  zpx,  zpy,  imp,  imp, absy,  imp,  imp, absx, absx, absy,  imp, /* B */
/* C */     imm, indx,  imm,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  imp,  imp, abso, abso, abso,  imp, /* C */
/* D */     rel, indy,indzp,  imp,  zpx,  zpx,  zpx,  imp,  imp, absy,  imp,  imp, abso, absx, absx,  imp, /* D */
/* E */     imm, indx,  imm,  imp,   zp,   zp,   zp,  imp,  imp,  imm,  imp,  imp, abso, abso, abso,  imp, /* E */
/* F */     rel, indy,indzp,  imp,  zpx,  zpx,  zpx,  imp,  imp, absy,  imp,  imp, abso, absx, absx,  imp  /* F */
};

static void (*optable[256])() = {
/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |      */
/* 0 */      brk,  ora,  nop,  nop,  tsb,  ora,  asl,  nop,  php,  ora,  asl,  nop,  tsb,  ora,  asl,  nop, /* 0 */
/* 1 */      bpl,  ora,  ora,  nop,  trb,  ora,  asl,  nop,  clc,  ora,  inc,  nop,  trb,  ora,  asl,  nop, /* 1 */
/* 2 */      jsr,  and,  nop,  nop,  bit,  and,  rol,  nop,  plp,  and,  rol,  nop,  bit,  and,  rol,  nop, /* 2 */
/* 3 */      bmi,  and,  and,  nop,  bit,  and,  rol,  nop,  sec,  and,  dec,  nop,  bit,  and,  rol,  nop, /* 3 */
/* 4 */      rti,  eor,  nop,  nop,  nop,  eor,  lsr,  nop,  pha,  eor,  lsr,  nop,  jmp,  eor,  lsr,  nop, /* 4 */
/* 5 */      bvc,  eor,  eor,  nop,  nop,  eor,  lsr,  nop,  cli,  eor,  phy,  nop,  nop,  eor,  lsr,  nop, /* 5 */
/* 6 */      rts,  adc,  nop,  nop,  stz,  adc,  ror,  nop,  pla,  adc,  ror,  nop,  jmp,  adc,  ror,  nop, /* 6 */
/* 7 */      bvs,  adc,  adc,  nop,  stz,  adc,  ror,  nop,  sei,  adc,  ply,  nop,  jmp,  adc,  ror,  nop, /* 7 */
/* 8 */      bra,  sta,  nop,  nop,  sty,  sta,  stx,  nop,  dey,  bit,  txa,  nop,  sty,  sta,  stx,  nop, /* 8 */
/* 9 */      bcc,  sta,  sta,  nop,  sty,  sta,  stx,  nop,  tya,  sta,  txs,  nop,  stz,  sta,  stz,  nop, /* 9 */
/* A */      ldy,  lda,  ldx,  nop,  ldy,  lda,  ldx,  nop,  tay,  lda,  tax,  nop,  ldy,  lda,  ldx,  nop, /* A */
/* B */      bcs,  lda,  lda,  nop,  ldy,  lda,  ldx,  nop,  clv,  lda,  tsx,  nop,  ldy,  lda,  ldx,  nop, /* B */
/* C */      cpy,  cmp,  nop,  nop,  cpy,  cmp,  dec,  nop,  iny,  cmp,  dex,  nop,  cpy,  cmp,  dec,  nop, /* C */
/* D */      bne,  cmp,  cmp,  nop,  nop,  cmp,  dec,  nop,  cld,  cmp,  phx,  nop,  nop,  cmp,  dec,  nop, /* D */
/* E */      cpx,  sbc,  nop,  nop,  cpx,  sbc,  inc,  nop,  inx,  sbc,  nop,  nop,  cpx,  sbc,  inc,  nop, /* E */
/* F */      beq,  sbc,  sbc,  nop,  nop,  sbc,  inc,  nop,  sed,  sbc,  plx,  nop,  nop,  sbc,  inc,  nop  /* F */
};

static const uint32_t ticktable[256] = {
/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |     */
/* 0 */      7,    6,    2,    1,    5,    3,    5,    1,    3,    2,    2,    1,    6,    4,    6,    1,  /* 0 */
/* 1 */      2,    5,    5,    1,    5,    4,    6,    1,    2,    4,    2,    1,    6,    4,    6,    1,  /* 1 */
/* 2 */      6,    6,    2,    1,    3,    3,    5,    1,    4,    2,    2,    1,    4,    4,    6,    1,  /* 2 */
/* 3 */      2,    5,    5,    1,    4,    4,    6,    1,    2,    4,    2,    1,    4,    4,    6,    1,  /* 3 */
/* 4 */      6,    6,    2,    1,    3,    3,    5,    1,    3,    2,    2,    1,    3,    4,    6,    1,  /* 4 */
/* 5 */      2,    5,    5,    1,    4,    4,    6,    1,    2,    4,    3,    1,    8,    4,    6,    1,  /* 5 */
/* 6 */      6,    6,    2,    1,    3,    3,    5,    1,    4,    2,    2,    1,    6,    4,    6,    1,  /* 6 */
/* 7 */      2,    5,    5,    1,    4,    4,    6,    1,    2,    4,    4,    1,    6,    4,    6,    1,  /* 7 */
/* 8 */      2,    6,    2,    1,    3,    3,    3,    1,    2,    2,    2,    1,    4,    4,    4,    1,  /* 8 */
/* 9 */      2,    6,    5,    1,    4,    4,    4,    1,    2,    5,    2,    1,    4,    5,    5,    1,  /* 9 */
/* A */      2,    6,    2,    1,    3,    3,    3,    1,    2,    2,    2,    1,    4,    4,    4,    1,  /* A */
/* B */      2,    5,    5,    1,    4,    4,    4,    1,    2,    4,    2,    1,    4,    4,    4,    1,  /* B */
/* C */      2,    6,    2,    1,    3,    3,    5,    1,    2,    2,    2,    1,    4,    4,    6,    1,  /* C */
/* D */      2,    5,    5,    1,    4,    4,    6,    1,    2,    4,    3,    1,    4,    4,    7,    1,  /* D */
/* E */      2,    6,    2,    1,    3,    3,    5,    1,    2,    2,    2,    1,    4,    4,    6,    1,  /* E */
/* F */      2,    5,    5,    1,    4,    4,    6,    1,    2,    4,    4,    1,    4,    4,    7,    1   /* F */
};


void nmi6502() {
    push16(pc);
    push8(status);
    status |= FLAG_INTERRUPT;
    pc = (uint16_t)read6502(0xFFFA) | ((uint16_t)read6502(0xFFFB) << 8);
}

void irq6502() {
    push16(pc);
    push8(status);
    status |= FLAG_INTERRUPT;
    pc = (uint16_t)read6502(0xFFFE) | ((uint16_t)read6502(0xFFFF) << 8);
}

void exec6502(uint32_t tickcount) {
    clockgoal6502 = tickcount;
   
    while (clockticks6502 < clockgoal6502) {
        opcode = read6502(pc++);
        status |= FLAG_CONSTANT;

        penaltyop = 0;
        penaltyaddr = 0;

        (*addrtable[opcode])();
        (*optable[opcode])();
        clockticks6502 += ticktable[opcode];
        if (penaltyop && penaltyaddr) clockticks6502++;

        instructions++;
    }
    
    clockticks6502 -= clockgoal6502;

}

void step6502() {
    opcode = read6502(pc++);
    status |= FLAG_CONSTANT;

    penaltyop = 0;
    penaltyaddr = 0;

    (*addrtable[opcode])();
    (*optable[opcode])();
    clockticks6502 += ticktable[opcode];
    if (penaltyop && penaltyaddr) clockticks6502++;
    clockgoal6502 = clockticks6502;

    instructions++;
}
