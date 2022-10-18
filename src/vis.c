#include "vis.h"
#include <math.h>
#include "NE10.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "demo.h"
#include "dma/dma.h"
#include "audio/audio.h"

#define PI 	3.141592653f

#define FREQ_S 24000.0f

#define NFFT 4096UL

#define NUM_SAMPLES NR_AUDIO_SAMPLES

#define INSAMPS (NUM_SAMPLES/2)

#define FRAMERATE (FREQ_S/INSAMPS)

#define MEL_SCALING 1.0f

#define FEATHER_MAX 5

const float featherTable[FEATHER_MAX] = { 4.0f, 1.0f, 0.5f, 0.2f, 0.05f };

/************************** Constant Definitions *****************************/
#define NUM_LED_PER_CHANNEL (60*5-2)
#define NUM_LED_CHANNELS 10

#define COL_OFFSET (30*5-1)

#define WIDTH (2*NUM_LED_CHANNELS)
#define HEIGHT COL_OFFSET

#define NUM_LEDS (NUM_LED_PER_CHANNEL*NUM_LED_CHANNELS)

#define B_OFFSET 24
#define R_OFFSET 16
#define G_OFFSET 8

#define BLUE(_x) ((uint32_t)(_x & 0xFF) << B_OFFSET)
#define RED(_x) ((uint32_t)(_x & 0xFF) << R_OFFSET)
#define GREEN(_x) ((uint32_t)(_x & 0xFF) << G_OFFSET)

#define BLUEC(_x) ((_x >> B_OFFSET) & 0xFF)
#define REDC(_x) ((_x >> R_OFFSET) & 0xFF)
#define GREENC(_x) ((_x >> G_OFFSET) & 0xFF)

/**************************** Type Definitions *******************************/

#define W(_x) (2.0f*PI)/(_x*FRAMERATE)

typedef uint32_t Color_t;

typedef struct _point {
	float x;
	float y;
} point;

static inline point mkpoint(float x, float y){
	point p;
	p.x = x;
	p.y = y;
	return p;
}

Color_t color(uint32_t r, uint32_t g, uint32_t b){
	/* saturate */
	r = (r < 0xFF ? r : 0xFF);
	g = (g < 0xFF ? g : 0xFF);
	b = (b < 0xFF ? b : 0xFF);

	return RED(r) | GREEN(g) | BLUE(b);
}

ne10_fft_r2c_cfg_float32_t cfg;

uint32_t pixels[NUM_LEDS];

uint32_t ledbuf[NUM_LEDS];
extern XAxiDma sAxiDma1;

float input[NUM_SAMPLES];
float w[NFFT];

float inbuf[NFFT];

__attribute__((aligned(8)))
float fftInBuf[NFFT];

__attribute__((aligned(8)))
ne10_fft_cpx_float32_t fftOutput[NFFT/2 + 1];

__attribute__((aligned(8)))
float mag[NFFT/2];

float *inptr;
float *fftinptr;

float fbands[WIDTH];

const int binstart[WIDTH + 1] = {
		11,	28,	46,	68,	91,	118,	148,	182,	221,	264,	313,	368,	430,
		500,	579,	668,	768,	881,	1008,	1152,	1314
};

int maskH[WIDTH];
int maskL[WIDTH];

float theta = 0;

float phaseInc = W(10.0f);

/*
 * 0 	1 		2 		3 		...		xn-1
 * xn	xn+1	xn+2	xn+3	...		xn+xn-1
 *
 */
__attribute__((optimize("Ofast")))
static void translate(uint32_t *dst, uint32_t *src){
	uint32_t *px0;
	uint32_t *px1;

	px0 = &dst[NUM_LED_CHANNELS*(COL_OFFSET-1)];
	px1 = &dst[NUM_LED_CHANNELS*COL_OFFSET];

	for(int j=0; j<HEIGHT; j++){
		uint32_t *psrc = src + j*WIDTH;
		for(int i=0; i<NUM_LED_CHANNELS; i++){
			px0[i] = psrc[2*i];
			px1[i] = psrc[2*i+1];
		}
		px0 -= NUM_LED_CHANNELS;
		px1 += NUM_LED_CHANNELS;
	}
}

static void hann(float *p, int N) {
    for (int i = 0; i < N; i++) {
        p[i] = 0.5f * (1.0f - cosf(2.0f * PI * i / (N - 1)));
    }
}

static void writeLeds(uint32_t *addr, uint32_t numpix)
{
	union ubitField uTransferVariable;

	//xil_printf("\r\nEnter writeleds");

	Xil_DCacheFlushRange((u32) addr, numpix*sizeof(uint32_t));

	uTransferVariable.l = XAxiDma_SimpleTransfer(&sAxiDma1,(u32) addr, numpix*sizeof(uint32_t), XAXIDMA_DMA_TO_DEVICE);
	if (uTransferVariable.l != XST_SUCCESS)
	{
		if (Demo.u8Verbose)
			xil_printf("\n fail @ xmit; ERROR: %d", uTransferVariable.l);
	}

	//xil_printf("\r\nwriteleds function done");
}

void testLeds(void){
	for(int i=0; i<32; i++){
		for(int j=0; j<WIDTH; j++){
			pixels[i*WIDTH + j] = color(150, 0, i);
		}
	}

	translate(ledbuf, pixels);
	writeLeds(ledbuf, NUM_LEDS);
}

XStatus init_vis(void)
{
	if (ne10_init() != NE10_OK)
    {
        printf("Failed to initialise Ne10.\n");
        return XST_FAILURE;
    }

	cfg = ne10_fft_alloc_r2c_float32(NFFT);
	if(cfg == NULL){
		printf("Failed to allocate FFT.\n");
		return XST_FAILURE;
	}

	hann(w, NFFT);

	memset(fftInBuf, 0, sizeof(fftInBuf));
	memset(inbuf, 0, sizeof(inbuf));

	inptr = inbuf + NFFT - INSAMPS;
	fftinptr = inbuf;

	return XST_SUCCESS;
}

static inline Color_t lerp(float v, Color_t c0, Color_t c1)
{
    int r = (int)(REDC(c0) + v*((int)REDC(c1) - (int)REDC(c0)));
    int g = (int)(GREENC(c0) + v*((int)GREENC(c1) - (int)GREENC(c0)));
    int b = (int)(BLUEC(c0) + v*((int)BLUEC(c1) - (int)BLUEC(c0)));
    return color(r, g, b);
}

static inline point rotate(point p, float theta)
{
    float x = p.x * cosf(theta) - p.y*sinf(theta);
    float y = p.x * sinf(theta) + p.y*cosf(theta);
    return mkpoint(x, y);
}

__attribute__((optimize("Ofast")))
static inline float cubicInterp (float p[4], float x) {
  return p[1] + 0.5f * x*(p[2] - p[0] + x*(2.0f*p[0] - 5.0f*p[1] + 4.0f*p[2] - p[3] + x*(3.0f*(p[1] - p[2]) + p[3] - p[0])));
}

__attribute__((optimize("Og")))
void visualizer(uint32_t *data){
	/* fixed to float */
	for(int i=0; i<NUM_SAMPLES; i++){
		 uint32_t a = data[i] << 8;
		 float c;

		 __asm__ (
			 "VCVT.F32.S32 %[c], %[a], #31"
			 :[c] "=t" (c)
			 :[a] "t" (a)
			 :
		 );
		 input[i] = c;
	}

	/* sum LR */
	for(int i=0; i<INSAMPS; i++){
		input[i] = input[2*i] + input[2*i + 1];
	}

	/* add to input */
	for(int i=0; i<INSAMPS; i++){
		*inptr++ = input[i];
		if(inptr == inbuf + NFFT){ inptr = inbuf; }
	}

	/* window */
	float *ptr = fftinptr;
	for(int i=0; i<NFFT; i++){
		fftInBuf[i] = *ptr++ * w[i];
		if(ptr == inbuf + NFFT){ ptr = inbuf; }
	}

	fftinptr += INSAMPS;
	if(fftinptr >= inbuf + NFFT){ fftinptr -= NFFT; }

	/* do FFT */
	ne10_fft_r2c_1d_float32_neon((ne10_fft_cpx_float32_t*)fftOutput, fftInBuf, cfg);

	/* calculate magnitudes */
	ne10_len_vec2f(mag, (ne10_vec2f_t*)fftOutput, NFFT/2);

	memset(fbands, 0, sizeof(fbands));

	for(int i=0; i < WIDTH; i++){
		float v = 0;
		int per = binstart[i+1] - binstart[i];
		float inc = 1.0f / per;

		/* triangle */
		for(int j=0; j<per; j++){
			fbands[i] += (mag[binstart[i] + j] * v + mag[binstart[i + 1] + j] * (1.0f - v));
			v += inc;
		}

		int offset = 15 + roundf(fbands[i] * MEL_SCALING);

		/* set the mask based on the mel band */
		maskH[i] = (HEIGHT >> 1) + offset;
		maskL[i] = (HEIGHT >> 1) - offset;

		/* constrain */
		maskH[i] = (maskH[i] > HEIGHT ? HEIGHT : maskH[i]);
		maskL[i] = (maskL[i] < 0 ? 0 : maskL[i]);
	}

	/* TODO: make nice leds */
	memset(pixels, 0, sizeof(pixels));

	theta = theta + phaseInc;
	if(theta >= 2.0f*PI){ theta -= 2.0f*PI; }

	Color_t color0 = color(64, 0, 0);
	Color_t color1 = color(0, 0, 64);
	for(int i=0; i<NUM_LEDS; i++){
	    int xraw = (i % WIDTH);
	    int yraw = floorf((float)i/(float)WIDTH);

	    float x = (float)xraw/WIDTH - 0.5f;
	    float y = (float)yraw/HEIGHT - 0.5f;

	    point p = mkpoint(x, y);

	    /* rotate the gradient */
	    p = rotate(p, theta);

	    /* translate back to the active area */
	    p.x += 0.5f; p.y += 0.5f;
	    p.y = fmaxf(fminf(1.0f, p.y), 0);

	    Color_t c = lerp(p.y, color0, color1);

	    pixels[i] = c;

	    /* apply a feathery glow effect when we are outside the mask */
	    int difl = maskL[xraw] - yraw;
	    int difh = yraw - maskH[xraw];

	    if(difh >= FEATHER_MAX || difl >= FEATHER_MAX){
			pixels[i] = 0;
		}
	    else{
			int difraw = 0;
			if(difl > 0 && difl < FEATHER_MAX){
				difraw = difl;
			}
			else if(difh > 0 && difh < FEATHER_MAX){
				difraw = difh;
			}

			if(difraw > 0){
				float diff = featherTable[difraw - 1];
				pixels[i] = color(REDC(pixels[i])*diff, GREENC(pixels[i])*diff, BLUEC(pixels[i])*diff);
			}
	    }
	}


	/* send leds */
	translate(ledbuf, pixels);
	writeLeds(ledbuf, NUM_LEDS);
}
