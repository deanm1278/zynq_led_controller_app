/************************************************************************/
/*																		*/
/*	demo.c	--	Zybo DMA Demo				 						*/
/*																		*/
/************************************************************************/
/*	Author: Sam Lowe											*/
/*	Copyright 2015, Digilent Inc.										*/
/************************************************************************/
/*  Module Description: 												*/
/*																		*/
/*		This file contains code for running a demonstration of the		*/
/*		DMA audio inputs and outputs on the Zybo.					*/
/*																		*/
/*																		*/
/************************************************************************/
/*  Notes:																*/
/*																		*/
/*		- The DMA max burst size needs to be set to 16 or less			*/
/*																		*/
/************************************************************************/
/*  Revision History:													*/
/* 																		*/
/*		9/6/2016(SamL): Created										*/
/*																		*/
/************************************************************************/


#include "demo.h"

#include "audio/audio.h"
#include "dma/dma.h"
#include "intc/intc.h"
#include "userio/userio.h"
#include "iic/iic.h"

/***************************** Include Files *********************************/

#include "xaxidma.h"
#include "xparameters.h"
#include "xil_exception.h"
#include "xdebug.h"
#include "xiic.h"
#include "xaxidma.h"
#include "xtime_l.h"


#ifdef XPAR_INTC_0_DEVICE_ID
 #include "xintc.h"
 #include "microblaze_sleep.h"
#else
 #include "xscugic.h"
#include "sleep.h"
#include "xil_cache.h"
#endif

/************************** Constant Definitions *****************************/
#define NUM_LED_CHANNELS 8
#define B_OFFSET 24
#define R_OFFSET 16
#define G_OFFSET 8

#define BLUE(_x) ((uint32_t)(_x & 0xFF) << B_OFFSET)
#define RED(_x) ((uint32_t)(_x & 0xFF) << R_OFFSET)
#define GREEN(_x) ((uint32_t)(_x & 0xFF) << G_OFFSET)

/**************************** Type Definitions *******************************/
typedef uint32_t Color_t;

/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/
#if (!defined(DEBUG))
extern void xil_printf(const char *format, ...);
#endif


/************************** Variable Definitions *****************************/
/*
 * Device instance definitions
 */

static XIic sIic;
static XAxiDma sAxiDma;		/* Instance of the XAxiDma */
static XAxiDma sAxiDma1;
static XGpio sUserIO;
static XScuGic sIntc;

//
// Interrupt vector table
const ivt_t ivt[] = {
	//IIC
	{XPAR_FABRIC_AXI_IIC_0_IIC2INTC_IRPT_INTR, (Xil_ExceptionHandler)XIic_InterruptHandler, &sIic},
	//DMA Stream to MemoryMap Interrupt handler
	{XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR, (Xil_ExceptionHandler)fnS2MMInterruptHandler, &sAxiDma},
	//DMA MemoryMap to Stream Interrupt handler
	{XPAR_FABRIC_AXI_DMA_1_MM2S_INTROUT_INTR, (Xil_ExceptionHandler)fnMM2SInterruptHandler, &sAxiDma1},
	//User I/O (buttons, switches, LEDs)
	{XPAR_FABRIC_AXI_GPIO_0_IP2INTC_IRPT_INTR, (Xil_ExceptionHandler)fnUserIOIsr, &sUserIO}
};

Color_t color(int r, int g, int b){
	return RED(r) | GREEN(g) | BLUE(b);
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

/*****************************************************************************/
/**
*
* Main function
*
* This function is the main entry of the interrupt test. It does the following:
*	Initialize the interrupt controller
*	Initialize the IIC controller
*	Initialize the User I/O driver
*	Initialize the DMA engine
*	Initialize the Audio I2S controller
*	Enable the interrupts
*	Wait for a button event then start selected task
*	Wait for task to complete
*
* @param	None
*
* @return
*		- XST_SUCCESS if example finishes successfully
*		- XST_FAILURE if example fails.
*
* @note		None.
*
******************************************************************************/
int main(void)
{
	int Status;

	Demo.u8Verbose = 1;

	//Xil_DCacheDisable();

	xil_printf("\r\n--- Entering main() --- \r\n");

	//
	//Initialize the interrupt controller

	Status = fnInitInterruptController(&sIntc);
	if(Status != XST_SUCCESS) {
		xil_printf("Error initializing interrupts");
		return XST_FAILURE;
	}


	// Initialize IIC controller
	Status = fnInitIic(&sIic);
	if(Status != XST_SUCCESS) {
		xil_printf("Error initializing I2C controller");
		return XST_FAILURE;
	}

    // Initialize User I/O driver
    Status = fnInitUserIO(&sUserIO);
    if(Status != XST_SUCCESS) {
    	xil_printf("User I/O ERROR");
    	return XST_FAILURE;
    }


	//Initialize DMA
	Status = fnConfigDma(&sAxiDma);
	if(Status != XST_SUCCESS) {
		xil_printf("DMA configuration ERROR");
		return XST_FAILURE;
	}

	Status = fnConfigDma1(&sAxiDma1);
	if(Status != XST_SUCCESS) {
		xil_printf("DMA1 configuration ERROR");
		return XST_FAILURE;
	}

	//Initialize Audio I2S
	Status = fnInitAudio();
	if(Status != XST_SUCCESS) {
		xil_printf("Audio initializing ERROR");
		return XST_FAILURE;
	}

	{
		XTime  tStart, tEnd;

		XTime_GetTime(&tStart);
		do {
			XTime_GetTime(&tEnd);
		}
		while((tEnd-tStart)/(COUNTS_PER_SECOND/10) < 20);
	}
	//Initialize Audio I2S
	Status = fnInitAudio();
	if(Status != XST_SUCCESS) {
		xil_printf("Audio initializing ERROR");
		return XST_FAILURE;
	}


	// Enable all interrupts in our interrupt vector table
	// Make sure all driver instances using interrupts are initialized first
	fnEnableInterrupts(&sIntc, &ivt[0], sizeof(ivt)/sizeof(ivt[0]));

	memset((void *)MEM_BASE_ADDR, 0, NR_AUDIO_SAMPLES*2*4*20);

	xil_printf("----------------------------------------------------------\r\n");
	xil_printf("LED CONTROLLER\r\n");
	xil_printf("----------------------------------------------------------\r\n");
	xil_printf("  Controls:\r\n");
	xil_printf("  BTN1: Write some LEDs \r\n");
	xil_printf("  BTN3: Record from LINE IN\r\n");
	xil_printf("----------------------------------------------------------\r\n");

    //main loop

    while(1) {

		// Checking the DMA S2MM event flag
		if (Demo.fDmaS2MMEvent)
		{
			xil_printf("\r\nRecording Done...");

			// Reset S2MM event and record flag
			Demo.fDmaS2MMEvent = 0;
			Demo.fAudioRecord = 0;
		}

		// Checking the DMA MM2S event flag
		if (Demo.fDmaMM2SEvent)
		{
			xil_printf("\r\nsend leds Done...");
			Demo.fDmaMM2SEvent = 0;
		}

		// Checking the DMA Error event flag
		if (Demo.fDmaError)
		{
			xil_printf("\r\nDma Error...");
			xil_printf("\r\nDma Reset...");


			Demo.fDmaError = 0;
			Demo.fAudioRecord = 0;
		}

		// Checking the btn change event
		if(Demo.fUserIOEvent) {

			switch(Demo.chBtn) {
				case 'u':
				{
					/* write some data */
					int numpix = NUM_LED_CHANNELS * 256;

					uint32_t *ptr = (uint32_t *)MEM_BASE_ADDR;
					for(int i=0; i<numpix; i++){
						*ptr++ = color(150, 0, i);
					}

					writeLeds((uint32_t *)MEM_BASE_ADDR, numpix);
					break;
				}
				case 'd':
					break;
				case 'r':
					xil_printf("\r\nStart Recording...\r\n");
					fnSetLineInput();
					fnAudioRecord(&sAxiDma,NR_AUDIO_SAMPLES);
					Demo.fAudioRecord = 1;
					break;
				case 'l':
				{
					/* write some data */
					int numpix = NUM_LED_CHANNELS * 256;

					uint32_t *ptr = (uint32_t *)MEM_BASE_ADDR;
					for(int i=0; i<numpix; i++){
						*ptr++ = color(0, i, 150);
					}

					writeLeds((uint32_t *)MEM_BASE_ADDR, numpix);
					break;
				}
				default:
					break;
			}

			// Reset the user I/O flag
			Demo.chBtn = 0;
			Demo.fUserIOEvent = 0;

		}

    }

	xil_printf("\r\n--- Exiting main() --- \r\n");


	return XST_SUCCESS;

}

