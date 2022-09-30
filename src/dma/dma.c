/*
 * dma.c
 *
 *  Created on: Jan 20, 2015
 *      Author: ROHegbeC
 */

#include "dma.h"
#include "../demo.h"
#include "../vis.h"

/*
 * Buffer and Buffer Descriptor related constant definition
 */
#define MAX_PKT_LEN		(4 * NR_AUDIO_SAMPLES)

/* The interrupt coalescing threshold and delay timer threshold
 * Valid range is 1 to 255
 *
 */
#define COALESCING_COUNT		1
#define DELAY_TIMER_COUNT		100

/************************** Variable Definitions *****************************/

uint32_t audioRxBuf[4*NR_AUDIO_SAMPLES];

extern volatile sDemo_t Demo;

uint32_t RxDone = 0;

/******************************************************************************
 * This is the Interrupt Handler from the Stream to the MemoryMap. It is called
 * when an interrupt is trigger by the DMA
 *
 * @param	Callback is a pointer to S2MM channel of the DMA engine.
 *
 * @return	none
 *
 *****************************************************************************/
void fnS2MMInterruptHandler (void *CallbackRef)
{
	u32 Status;
	int TimeOut;
	XAxiDma *AxiDmaInst = (XAxiDma *)CallbackRef;
	XAxiDma_BdRing* Instance = XAxiDma_GetRxRing(AxiDmaInst);

	// Get the interrupts
	Status = XAxiDma_BdRingGetIrq(Instance);

	// Clear all pending interrupts
	XAxiDma_BdRingAckIrq(Instance, Status);

	// No interrupt
	if(!(Status & XAXIDMA_IRQ_ALL_MASK))
	{
		return;
	}
	// Error interrupt
	else if((Status & XAXIDMA_IRQ_ERROR_MASK))
	{
		XAxiDma_BdRingDumpRegs(Instance);

		Demo.fDmaError = 1;
		XAxiDma_Reset(AxiDmaInst);

		TimeOut = 1000;
		while(TimeOut)
		{
			if(XAxiDma_ResetIsDone(AxiDmaInst))
			{
				break;
			}

			TimeOut -= 1;
		}

		return;
	}
	// Complete interrupt
	else if((Status & (XAXIDMA_IRQ_DELAY_MASK | XAXIDMA_IRQ_IOC_MASK)))
	{
		int BdCount;
		XAxiDma_Bd *BdPtr;

		/* restart the transfer */
		if(Status & XAXIDMA_IRQ_IOC_MASK){
			Xil_Out32(I2S_STREAM_CONTROL_REG, 0x00000000);
			Xil_Out32(I2S_STREAM_CONTROL_REG, 0x00000001);
		}

		/* Get finished BDs from hardware */
		BdCount = XAxiDma_BdRingFromHw(Instance, XAXIDMA_ALL_BDS, &BdPtr);
		RxDone += BdCount;

		if(BdCount > 0){
			uint32_t *addr = (uint32_t *)XAxiDma_BdGetBufAddr(BdPtr);
			Xil_DCacheFlushRange((u32) addr, NR_AUDIO_SAMPLES*sizeof(uint32_t));

			/* process the audio */
			visualizer(addr);
		}

		Demo.fDmaS2MMEvent = 1;
	}
}

/******************************************************************************
 * This is the Interrupt Handler from the MemoryMap to the Stream. It is called
 * when an interrupt is trigger by the DMA
 *
 * @param	Callback is a pointer to MM2S channel of the DMA engine.
 *
 * @return	none
 *
 *****************************************************************************/
void fnMM2SInterruptHandler (void *Callback)
{

	u32 IrqStatus;
	int TimeOut;
	XAxiDma *AxiDmaInst = (XAxiDma *)Callback;

	//Read all the pending DMA interrupts
	IrqStatus = XAxiDma_IntrGetIrq(AxiDmaInst, XAXIDMA_DMA_TO_DEVICE);
	//Acknowledge pending interrupts
	XAxiDma_IntrAckIrq(AxiDmaInst, IrqStatus, XAXIDMA_DMA_TO_DEVICE);
	//If there are no interrupts we exit the Handler
	if (!(IrqStatus & XAXIDMA_IRQ_ALL_MASK))
	{
		return;
	}

	// If error interrupt is asserted, raise error flag, reset the
	// hardware to recover from the error, and return with no further
	// processing.
	if (IrqStatus & XAXIDMA_IRQ_ERROR_MASK){
		Demo.fDmaError = 1;
		XAxiDma_Reset(AxiDmaInst);
		TimeOut = 1000;
		while (TimeOut)
		{
			if(XAxiDma_ResetIsDone(AxiDmaInst))
			{
				break;
			}
			TimeOut -= 1;
		}
		return;
	}
	if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK))
	{
		Demo.fDmaMM2SEvent = 1;
	}
}

void fnResetDma(XAxiDma *AxiDma)
{
	XAxiDma_Reset(AxiDma);
	int TimeOut = 1000;
	while (TimeOut)
	{
		if(XAxiDma_ResetIsDone(AxiDma))
		{
			break;
		}
		TimeOut -= 1;
	}

	XStatus Status = fnConfigDma(AxiDma);
	if(Status != XST_SUCCESS) {
		xil_printf("DMA configuration ERROR");
	}
}

/******************************************************************************
 * Function to configure the DMA in Interrupt mode, this implies that the scatter
 * gather function is disabled. Prior to calling this function, the user must
 * make sure that the Interrupts and the Interrupt Handlers have been configured
 *
 * @return	XST_SUCCESS - if configuration was successful
 * 			XST_FAILURE - when the specification are not met
 *****************************************************************************/
XStatus fnConfigDma(XAxiDma *AxiDma)
{
	int Status;
	XAxiDma_Config *pCfgPtr;

	//Make sure the DMA hardware is present in the project
	//Ensures that the DMA hardware has been loaded
	pCfgPtr = XAxiDma_LookupConfig(XPAR_AXIDMA_0_DEVICE_ID);
	if (!pCfgPtr)
	{
		if (Demo.u8Verbose)
		{
			xil_printf("\r\nNo config found for %d", XPAR_AXIDMA_0_DEVICE_ID);
		}
		return XST_FAILURE;
	}

	//Initialize DMA
	//Reads and sets all the available information
	//about the DMA to the AxiDma variable
	Status = XAxiDma_CfgInitialize(AxiDma, pCfgPtr);
	if (Status != XST_SUCCESS)
	{
		if (Demo.u8Verbose)
		{
			xil_printf("\r\nInitialization failed %d");
		}
		return XST_FAILURE;
	}

	//Ensures that the Scatter Gather mode is not active
	if(XAxiDma_HasSg(AxiDma))
	{
		if (Demo.u8Verbose)
		{

			xil_printf("\r\nDevice configured as SG mode");
		}
#if 0
		return XST_FAILURE;
#endif
	}

	//Disable all the DMA related Interrupts
	XAxiDma_IntrDisable(AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
	XAxiDma_IntrDisable(AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

	//Enable all the DMA Interrupts
	XAxiDma_IntrEnable(AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);

	return XST_SUCCESS;
}

XStatus fnConfigDma1(XAxiDma *AxiDma){
	int Status;
	XAxiDma_Config *pCfgPtr;

	//Make sure the DMA hardware is present in the project
	//Ensures that the DMA hardware has been loaded
	pCfgPtr = XAxiDma_LookupConfig(XPAR_AXIDMA_1_DEVICE_ID);
	if (!pCfgPtr)
	{
		if (Demo.u8Verbose)
		{
			xil_printf("\r\nNo config found for %d", XPAR_AXIDMA_1_DEVICE_ID);
		}
		return XST_FAILURE;
	}

	//Initialize DMA
	//Reads and sets all the available information
	//about the DMA to the AxiDma variable
	Status = XAxiDma_CfgInitialize(AxiDma, pCfgPtr);
	if (Status != XST_SUCCESS)
	{
		if (Demo.u8Verbose)
		{
			xil_printf("\r\nInitialization failed %d");
		}
		return XST_FAILURE;
	}

	//Ensures that the Scatter Gather mode is not active
	if(XAxiDma_HasSg(AxiDma))
	{
		if (Demo.u8Verbose)
		{

			xil_printf("\r\nDevice configured as SG mode");
		}
		return XST_FAILURE;
	}

	//Disable all the DMA related Interrupts
	XAxiDma_IntrDisable(AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
	XAxiDma_IntrDisable(AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

	//Enable all the DMA Interrupts
	XAxiDma_IntrEnable(AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

	return XST_SUCCESS;
}

int RxSetup(XAxiDma * AxiDmaInstPtr)
{
	XAxiDma_BdRing *RxRingPtr;
	int Status;
	XAxiDma_Bd BdTemplate;
	XAxiDma_Bd *BdPtr;
	XAxiDma_Bd *BdCurPtr;
	int BdCount;
	int FreeBdCount;
	UINTPTR RxBufferPtr;
	int Index;

	RxRingPtr = XAxiDma_GetRxRing(AxiDmaInstPtr);

	/* Disable all RX interrupts before RxBD space setup */
	XAxiDma_BdRingIntDisable(RxRingPtr, XAXIDMA_IRQ_ALL_MASK);

	/* Setup Rx BD space */
	BdCount = XAxiDma_BdRingCntCalc(XAXIDMA_BD_MINIMUM_ALIGNMENT,
				RX_BD_SPACE_HIGH - RX_BD_SPACE_BASE + 1);

	Status = XAxiDma_BdRingCreate(RxRingPtr, RX_BD_SPACE_BASE,
					RX_BD_SPACE_BASE,
					XAXIDMA_BD_MINIMUM_ALIGNMENT, BdCount);
	if (Status != XST_SUCCESS) {
		xil_printf("Rx bd create failed with %d\r\n", Status);
		return XST_FAILURE;
	}

	/*
	 * Setup a BD template for the Rx channel. Then copy it to every RX BD.
	 */
	XAxiDma_BdClear(&BdTemplate);
	Status = XAxiDma_BdRingClone(RxRingPtr, &BdTemplate);
	if (Status != XST_SUCCESS) {
		xil_printf("Rx bd clone failed with %d\r\n", Status);
		return XST_FAILURE;
	}

	/* Attach buffers to RxBD ring so we are ready to receive packets */
	FreeBdCount = XAxiDma_BdRingGetFreeCnt(RxRingPtr);
	xil_printf("	%u free Buffer Descriptors can be used...\n\r", FreeBdCount);

	Status = XAxiDma_BdRingAlloc(RxRingPtr, FreeBdCount, &BdPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("Rx bd alloc failed with %d\r\n", Status);
		return XST_FAILURE;
	}

	BdCurPtr = BdPtr;
	RxBufferPtr = (u32)audioRxBuf;
	memset((void *)RxBufferPtr, 0, NR_AUDIO_SAMPLES*4*sizeof(uint32_t));

	for (Index = 0; Index < FreeBdCount; Index++) {

		Status = XAxiDma_BdSetBufAddr(BdCurPtr, RxBufferPtr);
		if (Status != XST_SUCCESS) {
			xil_printf("Rx set buffer addr %x on BD %x failed %d\r\n",
			(unsigned int)RxBufferPtr,
			(UINTPTR)BdCurPtr, Status);

			return XST_FAILURE;
		}

		Status = XAxiDma_BdSetLength(BdCurPtr, MAX_PKT_LEN,
					RxRingPtr->MaxTransferLen);
		if (Status != XST_SUCCESS) {
			xil_printf("Rx set length %d on BD %x failed %d\r\n",
			    MAX_PKT_LEN, (UINTPTR)BdCurPtr, Status);

			return XST_FAILURE;
		}

		/* Receive BDs do not need to set anything for the control
		 * The hardware will set the SOF/EOF bits per stream status
		 */
		XAxiDma_BdSetCtrl(BdCurPtr, 0);

		XAxiDma_BdSetId(BdCurPtr, RxBufferPtr);

		RxBufferPtr += MAX_PKT_LEN;
		BdCurPtr = (XAxiDma_Bd *)XAxiDma_BdRingNext(RxRingPtr, BdCurPtr);
	}

	/*
	 * If you would like to have multiple interrupts to happen, change
	 * the COALESCING_COUNT to be a smaller value
	 */
	Status = XAxiDma_BdRingSetCoalesce(RxRingPtr, COALESCING_COUNT,
			DELAY_TIMER_COUNT);
	if (Status != XST_SUCCESS) {
		xil_printf("Rx set coalesce failed with %d\r\n", Status);
		return XST_FAILURE;
	}

	Status = XAxiDma_BdRingToHw(RxRingPtr, FreeBdCount, BdPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("Rx ToHw failed with %d\r\n", Status);
		return XST_FAILURE;
	}

	XAxiDma_BdRingIntEnable(RxRingPtr, XAXIDMA_IRQ_ALL_MASK);
	XAxiDma_BdRingEnableCyclicDMA(RxRingPtr);
	XAxiDma_SelectCyclicMode(AxiDmaInstPtr, XAXIDMA_DEVICE_TO_DMA, 1);

	return XAxiDma_BdRingStart(RxRingPtr);
}


