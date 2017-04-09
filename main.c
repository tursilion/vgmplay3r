// This is NOT GPL or any other ""free"" software license. 
// If you want to create any form of derived work or otherwise use my code, 
// you MUST contact me and ask. I don't consider this a huge obstacle in
// the internet age. ;)
//
// Fourth musical animation - Tursi 2017

// uses libti99
#include "vdp.h"
#include "sound.h"
#include "ani1.c"
#include "4mat.h"
//#include "digiloo.h"

#define FIRST_TONE_CHAR 2
#define FIRST_DRUM_CHAR 178
#define FIRST_TONE_COLOR 0
#define FIRST_DRUM_COLOR 22

#define SPRITE_OFF 193

#define MAX_SPRITES 64
struct SPRITE {
	// hardware struct - order matters
	unsigned char y,x;
	unsigned char ch,col;
} spritelist[MAX_SPRITES];

struct SPRPATH {
	int path,step;
//	int xstep,ystep,zstep;
//	signed char z;
} sprpath[MAX_SPRITES];

// pointers into the song data - all include the command nibble. Assumes player uses workspace at >8322
volatile unsigned int * const pVoice = (volatile unsigned int*)0x833A;
volatile unsigned char * const pVol = (volatile unsigned char*)0x8334;
volatile unsigned char * const pDone = (volatile unsigned char*)0x8330;

void stinit(const void *pSong, const int index);
void ststop();
void stplay();

// ring buffer for delaying audio - multiple of 15 steps
// sadly, yes, 15 and not 16, which we could mask
#define DELAYTIME 60
unsigned char delayvol[4][DELAYTIME];
unsigned int delaytone[4][DELAYTIME];
int delaypos;
unsigned int status;

int main() {
	unsigned char nOldVol[4];
	unsigned int nOldVoice[4];
	int nextSprite = 0;

	// init the screen
	{
		int x = set_graphics(VDP_SPR_8x8);		// set graphics mode with 8x8 sprites
		vdpmemset(gColor, 0x10, 32);			// all colors to black on transparent
		vdpmemset(gPattern, 0, 8);				// char 0 is blank
		vdpmemset(gImage, 0, 768);				// clear screen to char 0
		vdpchar(gSprite, 0xd0);					// all sprites disabled
		VDP_SET_REGISTER(VDP_REG_COL, 0x07);	// cyan screen

		// set up the characters. each of the first 30 color sets
		// are one target (to make lighting them easy)
		// Each graphic is 6 chars, so we will skip the first 2
		// for now (to let char 0 be blank)
		// that leaves the last 16 chars (only) for text!
		// We could do some tricks to get a full character set
		// instead or use bitmap mode, but whatever. ;)
		// (we could just use the spare characters and
		// live with the occasional color flash...)
		for (int idx = 2; idx < 178; idx += 8) {
			vdpmemcpy(gPattern+(idx*8), tonehit, 6*8);
		}
		for (int idx = 178; idx < 242; idx += 8) {
			vdpmemcpy(gPattern+(idx*8), drumhit, 6*8);
		}
		// ball sprite on 1
		vdpmemcpy(gPattern+8, ballsprite, 8);

		// now draw the graphics
		// Probably need some background frames, like a scaffold
		for (int idx=0; idx<22; idx++) {
			int r=tones[idx*2];
			int c=tones[idx*2+1];
			int ch=idx*8+FIRST_TONE_CHAR;

			vdpscreenchar(VDP_SCREEN_POS(r-1,c-1), ch);
			vdpscreenchar(VDP_SCREEN_POS(r,c-1), ch+1);

			vdpscreenchar(VDP_SCREEN_POS(r-1,c), ch+2);
			vdpscreenchar(VDP_SCREEN_POS(r,c), ch+3);

			vdpscreenchar(VDP_SCREEN_POS(r-1,c+1), ch+4);
			vdpscreenchar(VDP_SCREEN_POS(r,c+1), ch+5);
		}

		for (int idx=0; idx<8; idx++) {
			int r=drums[idx*2];
			int c=drums[idx*2+1];
			int ch=idx*8+FIRST_DRUM_CHAR;

			vdpscreenchar(VDP_SCREEN_POS(r,c-1), ch);
			vdpscreenchar(VDP_SCREEN_POS(r+1,c-1), ch+1);

			vdpscreenchar(VDP_SCREEN_POS(r,c), ch+2);
			vdpscreenchar(VDP_SCREEN_POS(r+1,c), ch+3);

			vdpscreenchar(VDP_SCREEN_POS(r,c+1), ch+4);
			vdpscreenchar(VDP_SCREEN_POS(r+1,c+1), ch+5);
		}

		VDP_SET_REGISTER(VDP_REG_MODE1, x);		// enable the display
		VDP_REG1_KSCAN_MIRROR = x;				// must have a copy of VDP Reg 1 if you ever use KSCAN
	}

	for (int idx=0; idx<4; idx++) {
		nOldVol[idx]=0xff;
		nOldVoice[idx]=0;
		for (int i2=0; i2<DELAYTIME; i2++) {
			// mute the history 
			delayvol[idx][i2]=(idx*0x20) + 0x9f;
		}
	}

	for (int idx=0; idx<MAX_SPRITES; idx++) {
		spritelist[idx].y=SPRITE_OFF;
		spritelist[idx].ch=1;
	}

//	stinit(tunedigiloo,0);
	stinit(tune4mat,0);
	*pDone=1;
//	VDP_INT_HOOK = stplay;
	delaypos = 0;
	int frame = 0;

	while (*pDone) {
//		vdpwaitvint();
		VDP_WAIT_VBLANK_CRU_STATUS(status);		// waits for int and clears it

		// copy the appropriate sprite table up
		if (frame&1) {
			vdpmemcpy(gSprite, (unsigned char*)&spritelist[32], 128);
		} else {
			vdpmemcpy(gSprite, (unsigned char*)&spritelist[0], 128);
		}

		// now that the screen is set, NOW we can play
		stplay();

		// implement frame delay
		for (int idx=0; idx<4; idx++) {
			delayvol[idx][delaypos] = pVol[idx];
			// do the byte order swap now cause it's faster
			unsigned int x = pVoice[idx];
			x = (x>>8)|(x<<8);
			delaytone[idx][delaypos] = x;

			// trigger animation
			if (idx != 3) {
				// de-swizzle the note
				x=(x<<4)|((x>>8)&0x0f);
			}

			// do nothing if muted channel
			int targ = -1;
			if ((pVol[idx]&0x0f) < 0x0f) {
				if (idx == 3) {
					targ=((x>>8)&0x07)+FIRST_DRUM_COLOR;
				} else {
					targ=tonetarget[x&0x3ff] + FIRST_TONE_COLOR;
				}

				if ((pVol[idx] < nOldVol[idx]-2)||(targ!=nOldVoice[idx])) {
					// trigger new ball
					unsigned int y = nextSprite++;
					if (nextSprite >= MAX_SPRITES) nextSprite = 0;
					if (idx == 3) {
						sprpath[y].path=((x>>8)&0x07)+FIRST_DRUM_COLOR;
					} else {
						sprpath[y].path=tonetarget[x&0x3ff] + FIRST_TONE_COLOR;
					}
					sprpath[y].step=0;
					spritelist[y].y=spritetrace[idx][sprpath[idx].path][1];
					spritelist[y].x=spritetrace[idx][sprpath[idx].path][0];
					spritelist[y].col=colorchan[idx]>>4;
				}
			}

			// remember these values
			nOldVol[idx]=pVol[idx];
			nOldVoice[idx]=targ;
		}
		// next position
		++delaypos;
		if (delaypos>=DELAYTIME) delaypos=0;

		// fade out any colors
		{
			// buffer for working with color table in CPU RAM
			unsigned char colortab[32];

			vdpmemread(gColor, colortab, 32);
			for (int idx=0; idx<32; idx++) {
				colortab[idx]=colorfade[colortab[idx]>>4];
			}

			// play it out (and set any fresh colors)
			for (int idx=0; idx<3; idx++) {
				// tone is in correct order here
				unsigned int x = delaytone[idx][delaypos];
				SOUND = x>>8;
				SOUND = x&0xff;
				// and volume just is
				SOUND = delayvol[idx][delaypos];

				if ((delayvol[idx][delaypos]&0x0f) < 0x0f) {
					// de-swizzle the note
					x=(x<<4)|((x>>8)&0x0f);
					unsigned char set = tonetarget[x&0x3ff] + FIRST_TONE_COLOR;
					colortab[set] = colorchan[idx];
				}
			}

			// noise channel - similar but different
			{
				// noise is in msb here
				unsigned int x = delaytone[3][delaypos]>>8;
				SOUND = x;
				// and volume just is
				SOUND = delayvol[3][delaypos];

				if ((delayvol[3][delaypos]&0x0f) < 0x0f) {
					unsigned char set = (x&0x7)+FIRST_DRUM_COLOR;
					colortab[set] = colorchan[3];
				}
			}

			// write it back
			vdpmemcpy(gColor, colortab, 32);
		}

		// and animate the sprites
		++frame;
		frame&=0x03;
		for (int idx = frame*16; idx<frame*16+16; idx++) {
			if (spritelist[idx].y == SPRITE_OFF) continue;

			sprpath[idx].step+=2;
			if (sprpath[idx].step > 28) {
				// end it when it hits?
				spritelist[idx].y = SPRITE_OFF;
				continue;
			}

			if (spritetrace[idx][sprpath[idx].path][sprpath[idx].step+1] == 255) {
				spritelist[idx].y = SPRITE_OFF;
				continue;
			}
			spritelist[idx].y=spritetrace[idx][sprpath[idx].path][sprpath[idx].step+1];
			spritelist[idx].x=spritetrace[idx][sprpath[idx].path][sprpath[idx].step];
		}
	}

	// mute the sound chip
	SOUND=0x9F;
	SOUND=0xBF;
	SOUND=0xDF;
	SOUND=0xFF;

}
