#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "fake6502.h"

#include <stdio.h>
#if defined(_WIN32) && !defined(_XBOX)
#include <windows.h>
#endif
#include "libretro.h"

/////////////////////////
//VARIABLES AND DEFINES//
/////////////////////////
#define VIDEO_WIDTH 256
#define VIDEO_HEIGHT 192
#define SAMPLE_RATE 48000

static uint8_t frameBuffer[VIDEO_WIDTH*VIDEO_HEIGHT*sizeof(uint32_t)];

uint16_t chanTimers[4] = {0,0,0,0};
uint8_t chanSamples[4] = {0,0,0,0};
int16_t chanVals[4] = {0,0,0,0};
static int16_t waveformLut[16*8] = {256,256,256,256,256,256,256,256,-256,-256,-256,-256,-256,-256,-256,-256,
	256,256,256,256,-256,-256,-256,-256,-256,-256,-256,-256,-256,-256,-256,-256,
	256,192,128,64,0,-64,-128,-192,-256,-192,-128,-64,0,64,128,192,
	256,192,128,64,0,-64,-128,-192,-256,-192,-128,-64,0,64,128,192,
	256,222,188,154,119,85,51,17,-17,-51,-85,-119,-154,-188,-222,-256,
	256,222,188,154,119,85,51,17,-17,-51,-85,-119,-154,-188,-222,-256,
	1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,
	1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0};
int16_t audioBuffer[800*2];

static struct retro_log_callback logging;
static retro_log_printf_t logCallback;

static retro_environment_t environmentCallback;
static retro_video_refresh_t videoCallback;
static retro_audio_sample_t audioCallback;
static retro_audio_sample_batch_t audioBatchCallback;
static retro_input_poll_t inputPollCallback;
static retro_input_state_t inputQueryCallback;

char sramPath[4096];
int sramLen;
static unsigned int portDevices[4] = {RETRO_DEVICE_JOYPAD,RETRO_DEVICE_JOYPAD,RETRO_DEVICE_JOYPAD,RETRO_DEVICE_JOYPAD};

//////////////////
//INITIALIZATION//
//////////////////
static void fallbackLogCallback(enum retro_log_level level,const char * fmt,...) {
	(void)level;
	va_list va;
	va_start(va,fmt);
	vfprintf(stderr,fmt,va);
	va_end(va);
}
unsigned int retro_get_region() {
	return RETRO_REGION_NTSC;
}
unsigned int retro_api_version() {
	return RETRO_API_VERSION;
}
void retro_init() {
	srand(time(NULL));
	const char * dir = NULL;
	if(environmentCallback(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY,&dir)&&dir) {
		if(dir==NULL) {
			environmentCallback(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY,&dir);
		}
		strncpy(sramPath,dir,4095);
		strcat(sramPath,"/f8emu_");
		sramLen = strlen(sramPath);
	}
}
void retro_set_environment(retro_environment_t cb) {
	environmentCallback = cb;
	if(cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE,&logging)) {
		logCallback = logging.log;
	} else {
		logCallback = fallbackLogCallback;
	}
}
void retro_set_video_refresh(retro_video_refresh_t cb) {videoCallback = cb;}
void retro_set_audio_sample(retro_audio_sample_t cb) {audioCallback = cb;}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {audioBatchCallback = cb;}
void retro_set_input_poll(retro_input_poll_t cb) {inputPollCallback = cb;}
void retro_set_input_state(retro_input_state_t cb) {inputQueryCallback = cb;}
void retro_set_controller_port_device(unsigned int port,unsigned int device) {
	if(device==RETRO_DEVICE_JOYPAD || device==RETRO_DEVICE_NONE) {
		portDevices[port] = device;
	}
}
void retro_get_system_info(struct retro_system_info * info) {
	memset(info,0,sizeof(*info));
	
	info->library_name = "F8Emu LibRetro";
	info->library_version = "1.0";
	info->need_fullpath = true;
	info->valid_extensions = "f8";
}
void retro_get_system_av_info(struct retro_system_av_info * info) {
	memset(info,0,sizeof(*info));
	
	info->timing.fps = 60;
	info->timing.sample_rate = SAMPLE_RATE;
	
	info->geometry.base_width = info->geometry.max_width = VIDEO_WIDTH;
	info->geometry.base_height = info->geometry.max_height = VIDEO_HEIGHT;
	info->geometry.aspect_ratio = 1.333333f;
	
	int pixelFmt = RETRO_PIXEL_FORMAT_XRGB8888;
	
	environmentCallback(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT,&pixelFmt);
}
bool retro_load_game(const struct retro_game_info * info) {
	FILE * fpRom = fopen(info->path,"rb");
	fread(gameHeader,1,0x2000,fpRom);
	for(int i=0; i<7; i++) {
		if(gameHeader[0x10|i]&&((gameHeader[0x18|i]&0x80)==0)) {
			int bankDataSize = bankSizes[i]<<(gameHeader[0x10|i]-1);
			fread(bankDataPtrs[i],1,bankDataSize,fpRom);
		}
	}
	fclose(fpRom);
	//Load SRAM
	for(int i=0; i<4; i++) {
		sramPath[sramLen+i] = gameHeader[4+i];
	}
	sramPath[sramLen+4] = 0;
	strcat(sramPath,".srm");
	FILE * fpSram = fopen(sramPath,"rb");
	if(fpSram!=NULL) {
		for(int i=1; i<7; i++) {
			if(gameHeader[0x10|i]&&(gameHeader[0x18|i]&0x80)) {
				fread(bankDataPtrs[i],1,bankSizes[i]<<(gameHeader[0x10|i]-1),fpSram);
			}
		}
		fclose(fpSram);
	}
	//Load memory
	for(int i=0; i<7; i++) {
		if(gameHeader[0x10|i]) {
			int bankDataSize = bankSizes[i]<<(gameHeader[0x10|i]-1);
			memcpy(&memory[bankOffs[i]],&bankDataPtrs[i][bankDataSize-bankSizes[i]],bankSizes[i]);
		}
	}
	//Initialize input
	for(int i=0; i<128; i++) {
		//For convenience, programs should NOT rely on these being 0 on startup
		memory[0x7480|i] = 0;
	}
	struct retro_input_descriptor desc[] = {
		{0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP,"Up"},
		{0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_DOWN,"Down"},
		{0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT,"Left"},
		{0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT,"Right"},
		{0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START,"Start"},
		{0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A,"A"},
		{0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B,"B"},
		{0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L2,"C"},
		{0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L,"L"},
		{0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R,"R"},
		{0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X,"X"},
		{0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y,"Y"},
		{0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R2,"Z"},
		
		{1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP,"Up"},
		{1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_DOWN,"Down"},
		{1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT,"Left"},
		{1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT,"Right"},
		{1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START,"Start"},
		{1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A,"A"},
		{1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B,"B"},
		{1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L2,"C"},
		{1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L,"L"},
		{1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R,"R"},
		{1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X,"X"},
		{1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y,"Y"},
		{1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R2,"Z"},
		
		{2,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP,"Up"},
		{2,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_DOWN,"Down"},
		{2,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT,"Left"},
		{2,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT,"Right"},
		{2,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START,"Start"},
		{2,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A,"A"},
		{2,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B,"B"},
		{2,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L2,"C"},
		{2,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L,"L"},
		{2,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R,"R"},
		{2,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X,"X"},
		{2,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y,"Y"},
		{2,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R2,"Z"},
		
		{3,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP,"Up"},
		{3,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_DOWN,"Down"},
		{3,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT,"Left"},
		{3,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT,"Right"},
		{3,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START,"Start"},
		{3,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A,"A"},
		{3,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B,"B"},
		{3,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L2,"C"},
		{3,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L,"L"},
		{3,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R,"R"},
		{3,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X,"X"},
		{3,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y,"Y"},
		{3,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R2,"Z"},
		
		{0}};
	environmentCallback(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS,desc);
	//Reset game
	reset6502();
	return true;
}
void retro_reset() {
	//Reset game
	reset6502();
}
bool retro_load_game_special(unsigned game_type, const struct retro_game_info * info, size_t num_info){
	return false;
}
size_t retro_get_memory_size(unsigned int id) {
	switch(id) {
		case RETRO_MEMORY_SYSTEM_RAM: {
			return 0x10000;
		}
		case RETRO_MEMORY_VIDEO_RAM: {
			return 0x4000;
		}
	}
	return 0;
}
void * retro_get_memory_data(unsigned int id) {
	switch(id) {
		case RETRO_MEMORY_SYSTEM_RAM: {
			return (void*)memory;
		}
		case RETRO_MEMORY_VIDEO_RAM: {
			return (void*)&memory[0x4000];
		}
	}
	return NULL;
}

/////////////
//MAIN LOOP//
/////////////
void putPixel(uint8_t color,uint8_t x,uint8_t y) {
	//Unpack RGB222 color
	uint8_t r = ((color>>4)&3)*85;
	uint8_t g = ((color>>2)&3)*85;
	uint8_t b = (color&3)*85;
	//Write to framebuffer
	frameBuffer[(((y<<8)|x)<<2)] = b;
	frameBuffer[(((y<<8)|x)<<2)|1] = g;
	frameBuffer[(((y<<8)|x)<<2)|2] = r;
}
void putPixelFromTile(uint8_t pal,uint16_t tile,uint8_t sx,uint8_t sy,int x,int y) {
	//Get base address of tile
	uint16_t addr = 0x4000|(tile<<4)|sy;
	//Get palette entry
	uint8_t entry0 = (memory[addr]>>(7-sx))&1;
	uint8_t entry1 = (memory[addr|8]>>(7-sx))&1;
	uint8_t entry = entry0|(entry1<<1);
	//If pixel is onscreen and entry num is not 0, draw pixel
	if(entry!=0 && x>=0 && x<256 && y>=0 && y<192) {
		uint8_t color = memory[0x7500|(pal<<2)|entry];
		putPixel(color,x,y);
	}
}
void drawBg(int scrlx,int scrly,int minx,int maxx,int miny,int maxy) {
	//Draw background in given range
	for(int y=miny; y<maxy; y++) {
		for(int x=minx; x<maxx; x++) {
			//Actual pixel position
			int px = (scrlx+x)&0x1FF;
			int py = (scrly+y)&0x1FF;
			//Source X/Y coordinates for graphics tile
			int sx = px&7;
			int sy = py&7;
			//Offset to graphics tile
			int gtile = ((px&0xF8)>>3)|((py&0xF8)<<2)|((px&0x100)<<2)|((py&0x100)<<3);
			//Offset to palette metatile
			int ptile = ((px&0xF0)>>4)|(py&0xF0)|(px&0x100)|((py&0x100)<<1);
			//Tile to use inside palette metatile
			int pstile = ((px>>2)&2)|((py>>1)&4);
			//Mask and shift for extracting palette from palette metatile
			int mask = 0xC0>>pstile;
			int shift = 6-pstile;
			putPixelFromTile((memory[0x7000|ptile]&mask)>>shift,memory[0x6000|gtile],sx,sy,x,y);
		}
	}
}
void drawSprites(int pri) {
	//Splitting coordinates for quadrants
	uint8_t splx = memory[0x7400];
	uint8_t sply = (memory[0x7401]<192)?memory[0x7401]:192;
	//Draw sprites of given priority
	for(uint16_t i=0; i<128; i++) {
		//Address of sprite OAM bytes
		uint16_t addr = 0x7600|(i<<2);
		//Address of sprite OMASK byte
		uint16_t addr2 = 0x7580|i;
		if((memory[addr|3]&8)==pri) {
			//Number of tiles wide/tall
			uint8_t yhm = (memory[addr|3]&0x20)>>5;
			uint8_t xhm = (memory[addr|3]&0x10)>>4;
			//Number of pixels wide/tall
			uint8_t yhm2 = yhm<<3;
			uint8_t xhm2 = xhm<<3;
			for(uint8_t yh=0; yh<=yhm; yh++) {
				for(uint8_t xh=0; xh<=xhm; xh++) {
					for(uint8_t y=0; y<8; y++) {
						for(uint8_t x=0; x<8; x++) {
							uint8_t x2 = x|(xh<<3);
							uint8_t y2 = y|(yh<<3);
							//Destination pixel on screen
							int xd = (memory[addr|3]&0x40)?(7+xhm2-x2+memory[addr]):(memory[addr]+x2);
							int yd = (memory[addr|3]&0x80)?(7+yhm2-y2+memory[addr|1]):(memory[addr|1]+y2);
							//If sprite is enabled in quadrant AND quadrant has sprites enabled, draw sprite
							if((xd< splx && yd< sply && (memory[0x7424]&0x80) && (memory[addr2]&0x08))||
							   (xd>=splx && yd< sply && (memory[0x7424]&0x40) && (memory[addr2]&0x04))||
							   (xd< splx && yd>=sply && (memory[0x7424]&0x20) && (memory[addr2]&0x02))||
							   (xd>=splx && yd>=sply && (memory[0x7424]&0x10) && (memory[addr2]&0x01))) {
								   putPixelFromTile(4|(memory[addr|3]&3),
								   256|(memory[addr|2]^xh^(yh<<4)),x,y,xd,yd);
							}
						}
					}
				}
			}
		}
	}
}
static void videoCallbackFunc() {
	//Clear background
	for(int i=0; i<VIDEO_WIDTH; i++) {
		for(int j=0; j<VIDEO_HEIGHT; j++) {
			putPixel(memory[0x7500],i,j);
		}
	}
	
	//Draw sprites that are behind the background
	drawSprites(8);
	
	//Draw background
	//Splitting coordinates for quadrants
	uint8_t splx = memory[0x7400];
	uint8_t sply = (memory[0x7401]<192)?memory[0x7401]:192;
	//Upper left quadrant, if enabled
	if(memory[0x7424]&0x08) {
		drawBg(memory[0x7408]|((memory[0x7404]&0x80)<<1),
		memory[0x7409]|((memory[0x7404]&0x40)<<2),
		0,splx,0,sply);
	}
	//Upper right quadrant, if enabled
	if(memory[0x7424]&0x04) {
		drawBg(memory[0x740A]|((memory[0x7404]&0x20)<<3),
		memory[0x740B]|((memory[0x7404]&0x10)<<4),
		splx,256,0,sply);
	}
	//Lower left quadrant, if enabled
	if(memory[0x7424]&0x02) {
		drawBg(memory[0x740C]|((memory[0x7404]&0x08)<<5),
		memory[0x740D]|((memory[0x7404]&0x04)<<6),
		0,splx,sply,192);
	}
	//Lower right quadrant, if enabled
	if(memory[0x7424]&0x01) {
		drawBg(memory[0x740E]|((memory[0x7404]&0x02)<<7),
		memory[0x740F]|((memory[0x7404]&0x01)<<8),
		splx,256,sply,192);
	}
	
	//Draw sprites that are in front of the background
	drawSprites(0);
	
	//Output framebuffer
	videoCallback(frameBuffer,VIDEO_WIDTH,VIDEO_HEIGHT,VIDEO_WIDTH*sizeof(uint32_t));
}
static void audioCallbackFunc() {
	//Render 800 samples
	for(int i=0; i<(SAMPLE_RATE/30); i+=2) {
		//Initialize sample to 0
		int16_t samp = 0;
		//Accumulate sample for each channel
		for(int n=0; n<4; n++) {
			if(memory[0x7425]&(8>>n)) {
				uint16_t freq = ((memory[0x7418|(n<<1)]&0x0F)<<8)|memory[0x7419|(n<<1)];
				int16_t amp = memory[0x7418|(n<<1)]>>4;
				int inst = (memory[0x7410]&(0xC0>>(n<<1)))>>((3-n)<<1);
				inst = (inst<<1)|((memory[0x7414]&(8>>n))>>(3-n));
				chanTimers[n] += freq;
				for(int j=0; j<2; j++) {
					if(chanTimers[n]>=3000) {
						chanTimers[n] -= 3000;
						chanSamples[n]++;
						chanSamples[n] &= 0xF;
						if((inst>>1)==3) {
							if(waveformLut[96|chanSamples[n]]) {
								chanVals[n] = ((rand()&1)<<9)-256;
							}
						} else {
							chanVals[n] = waveformLut[(inst<<4)|chanSamples[n]];
						}
					}
				}
				samp += amp*chanVals[n];
			}
		}
		//Output sample
		audioCallback(samp,samp);
		//audioBuffer[i] = samp;
		//audioBuffer[i|1] = samp;
	}
	//audioBatchCallback(audioBuffer,800);
}
static void inputCallbackFunc() {
	inputPollCallback();
	//For each input device
	for(int i=0; i<4; i++) {
		//Does standard controller exist
		if(portDevices[i]==RETRO_DEVICE_JOYPAD) {
			memory[0x7480|i] = 0;
			memory[0x7480|i] |= inputQueryCallback(i,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP)?0x80:0;
			memory[0x7480|i] |= inputQueryCallback(i,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_DOWN)?0x40:0;
			memory[0x7480|i] |= inputQueryCallback(i,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT)?0x20:0;
			memory[0x7480|i] |= inputQueryCallback(i,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)?0x10:0;
			memory[0x7480|i] |= inputQueryCallback(i,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START)?0x08:0;
			memory[0x7480|i] |= inputQueryCallback(i,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A)?0x04:0;
			memory[0x7480|i] |= inputQueryCallback(i,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)?0x02:0;
			memory[0x7480|i] |= inputQueryCallback(i,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L2)?0x01:0;
			//Does extended controller also exist
			//if(portDevices[i]==RETRO_DEVICE_JOYPAD) {
				memory[0x7484|i] = 0;
				memory[0x7484|i] |= inputQueryCallback(i,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L)?0x20:0;
				memory[0x7484|i] |= inputQueryCallback(i,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R)?0x10:0;
				memory[0x7484|i] |= inputQueryCallback(i,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X)?0x04:0;
				memory[0x7484|i] |= inputQueryCallback(i,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y)?0x02:0;
				memory[0x7484|i] |= inputQueryCallback(i,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R2)?0x01:0;
			//}
		}
	}
}
void retro_run() {
	//Run processor
	exec6502(65536);
	//Run callback functions
	videoCallbackFunc();
	audioCallbackFunc();
	inputCallbackFunc();
}

////////////////
//FINALIZATION//
////////////////
void retro_unload_game() {
	//Save SRAM
	FILE * fpSram = fopen(sramPath,"wb");
	for(int i=1; i<7; i++) {
		if(gameHeader[0x10|i]&&(gameHeader[0x18|i]&0x80)) {
			fwrite(bankDataPtrs[i],1,bankSizes[i]<<(gameHeader[0x10|i]-1),fpSram);
		}
	}
	fclose(fpSram);
}
void retro_deinit() {}

//////////
//EXTRAS//
//////////
size_t retro_serialize_size() {
	//RAM + registers/clock value
	return 0x10010;
}
bool retro_serialize(void *data_,size_t size) {
	uint8_t * data = (uint8_t*)data_;
	//Save RAM
	for(int i=0; i<0x10000; i++) {
		data[i] = memory[i];
	}
	//Save registers
	data[0x10000] = a;
	data[0x10002] = x;
	data[0x10003] = y;
	data[0x10004] = sp;
	data[0x10005] = status;
	data[0x10006] = pc&0xFF;
	data[0x10007] = pc>>8;
	data[0x10008] = clockticks6502&0xFF;
	data[0x10009] = (clockticks6502>>8)&0xFF;
	data[0x1000A] = (clockticks6502>>16)&0xFF;
	data[0x1000B] = clockticks6502>>24;
	data[0x1000C] = clockgoal6502&0xFF;
	data[0x1000D] = (clockgoal6502>>8)&0xFF;
	data[0x1000E] = (clockgoal6502>>16)&0xFF;
	data[0x1000F] = clockgoal6502>>24;
	return true;
}
bool retro_unserialize(const void *data_,size_t size) {
	uint8_t * data = (uint8_t*)data_;
	//Reload RAM
	for(int i=0; i<0x10000; i++) {
		memory[i] = data[i];
	}
	//Reload registers
	a = data[0x10000];
	x = data[0x10002];
	y = data[0x10003];
	sp = data[0x10004];
	status = data[0x10005];
	pc = data[0x10006]|(data[0x10007]<<8);
	clockticks6502 = data[0x10008]|(data[0x10009]<<8)|(data[0x1000A]<<16)|(data[0x1000B]<<24);
	clockgoal6502 = data[0x1000C]|(data[0x1000D]<<8)|(data[0x1000E]<<16)|(data[0x1000F]<<24);
	return true;
}
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {}