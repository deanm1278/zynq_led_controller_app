/************************************************************************/
/*																		*/
/*	demo.h	--	Zedboard DMA Demo				 						*/
/*																		*/
/************************************************************************/
/*	Author: Sam Lowe													*/
/*	Copyright 2015, Digilent Inc.										*/
/************************************************************************/
/*  Module Description: 												*/
/*																		*/
/*		This header file contains code for running a demonstration 		*/
/*		of the DMA audio inputs and outputs on the Zedboard.			*/
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
/*		8/23/2016(SamL): Created										*/
/*																		*/
/************************************************************************/

#ifndef MAIN_H_
#define MAIN_H_

/***************************** Include Files *********************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xil_io.h"
#include "xstatus.h"
#include "xparameters.h"
#include "xil_cache.h"


/************************** Constant Definitions *****************************/
#define RETURN_ON_FAILURE(x) if ((x) != XST_SUCCESS) return XST_FAILURE;

#define DMA_DEV_ID		XPAR_AXIDMA_0_DEVICE_ID

#define DDR_BASE_ADDR       XPAR_PS7_DDR_0_S_AXI_BASEADDR

/* total number of samples (divide by 2 for stereo samples) */
#define NR_AUDIO_SAMPLES	1024

#define RX_BD_SPACE_BASE	(XPAR_BRAM_0_BASEADDR)
#define RX_BD_SPACE_HIGH	(XPAR_BRAM_0_BASEADDR + 0x00000FF)

#define RX_INTR_ID		XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR
#define TX_INTR_ID		XPAR_FABRIC_AXI_DMA_0_MM2S_INTROUT_INTR

#define INTC		XScuGic
#define INTC_HANDLER	XScuGic_InterruptHandler

#define AUDIO_CTL_ADDR			XPAR_D_AXI_I2S_AUDIO_0_AXI_L_BASEADDR

//Audio controller registers
enum i2sRegisters {
	I2S_RESET_REG				= AUDIO_CTL_ADDR,
	I2S_TRANSFER_CONTROL_REG	= AUDIO_CTL_ADDR + 0x04,
	I2S_FIFO_CONTROL_REG      	= AUDIO_CTL_ADDR + 0x08,
	I2S_DATA_IN_REG         	= AUDIO_CTL_ADDR + 0x0c,
	I2S_DATA_OUT_REG          	= AUDIO_CTL_ADDR + 0x10,
	I2S_STATUS_REG           	= AUDIO_CTL_ADDR + 0x14,
	I2S_CLOCK_CONTROL_REG     	= AUDIO_CTL_ADDR + 0x18,
	I2S_PERIOD_COUNT_REG       	= AUDIO_CTL_ADDR + 0x1C,
	I2S_STREAM_CONTROL_REG     	= AUDIO_CTL_ADDR + 0x20
};


/**************************** Type Definitions *******************************/
typedef struct {
	u8 u8Verbose;
	u8 fUserIOEvent;
	u8 fAudioRecord;
	u8 fDmaError;
	u8 fDmaS2MMEvent;
	u8 fDmaMM2SEvent;
	char chBtn;
} sDemo_t;

/************************** Function Prototypes ******************************/

// This variable holds the demo related settings
volatile sDemo_t Demo;

#endif /* MAIN_H_ */
