#ifndef _VIS_H_
#define _VIS_H_

#include "xparameters.h"
#include "xil_io.h"
#include "xiic.h"
#include "xil_printf.h"
#include "xil_cache.h"
#include "xstatus.h"

XStatus init_vis(void);
void testLeds(void);
void randomizeColors(void);

void visualizer(uint32_t *data);

#endif
