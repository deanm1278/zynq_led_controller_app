#include "vis.h"
#include <math.h>
#include "NE10.h"
#include <stdio.h>
#include <string.h>
#include "demo.h"
#include "dma/dma.h"
#include "audio/audio.h"

#define PI 	3.141592653f

#define NFFT 4096UL

#define NUM_SAMPLES NR_AUDIO_SAMPLES

#define INSAMPS (NUM_SAMPLES/2)

/************************** Constant Definitions *****************************/
#define NUM_LED_PER_CHANNEL 8
#define NUM_LED_CHANNELS 8

#define NUM_LEDS (NUM_LED_PER_CHANNEL*NUM_LED_CHANNELS)

#define B_OFFSET 24
#define R_OFFSET 16
#define G_OFFSET 8

#define BLUE(_x) ((uint32_t)(_x & 0xFF) << B_OFFSET)
#define RED(_x) ((uint32_t)(_x & 0xFF) << R_OFFSET)
#define GREEN(_x) ((uint32_t)(_x & 0xFF) << G_OFFSET)

/**************************** Type Definitions *******************************/
typedef uint32_t Color_t;

Color_t color(int r, int g, int b){
	return RED(r) | GREEN(g) | BLUE(b);
}

ne10_fft_r2c_cfg_float32_t cfg;

uint32_t ledbuf[2][NUM_LEDS];
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

static void hann(float *p, int N) {
    for (int i = 0; i < N; i++) {
        p[i] = 0.5f * (1.0f - cosf(2.0f * PI * i / (N - 1)));
    }
}

static void writeLeds(uint32_t *addr, uint32_t numpix)
{
	union ubitField uTransferVariable;

	if (Demo.u8Verbose)
	{
		xil_printf("\r\nEnter writeleds");
	}

	Xil_DCacheFlushRange((u32) addr, numpix*sizeof(uint32_t));

	uTransferVariable.l = XAxiDma_SimpleTransfer(&sAxiDma1,(u32) addr, numpix*sizeof(uint32_t), XAXIDMA_DMA_TO_DEVICE);
	if (uTransferVariable.l != XST_SUCCESS)
	{
		if (Demo.u8Verbose)
			xil_printf("\n fail @ xmit; ERROR: %d", uTransferVariable.l);
	}

	if (Demo.u8Verbose)
	{
		xil_printf("\r\nwriteleds function done");
	}
}

void testLeds(void){
	uint32_t *ptr = ledbuf[0];
	for(int i=0; i<NUM_LEDS; i++){
		*ptr++ = color(150, 0, i);
	}

	writeLeds(ledbuf[0], NUM_LEDS);
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

	__asm__ volatile("NOP");

	/* TODO: make nice leds */

	/* TODO: send leds */
}
