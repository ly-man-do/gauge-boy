/* libctru soc service setup. 3DS build only (not part of the host tests). */
#include "net.h"

#include <3ds.h>
#include <malloc.h>
#include <stdlib.h>

#define SOC_ALIGN      0x1000
#define SOC_BUFFERSIZE 0x100000

static u32 *soc_buffer;

bool net_init(void)
{
    if (soc_buffer != NULL) {
        return true;
    }
    soc_buffer = memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (soc_buffer == NULL) {
        return false;
    }
    if (R_FAILED(socInit(soc_buffer, SOC_BUFFERSIZE))) {
        free(soc_buffer);
        soc_buffer = NULL;
        return false;
    }
    return true;
}

void net_exit(void)
{
    if (soc_buffer == NULL) {
        return;
    }
    socExit();
    free(soc_buffer);
    soc_buffer = NULL;
}
