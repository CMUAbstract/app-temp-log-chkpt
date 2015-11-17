#include <libio/log.h>

#include "templog.h"
#include "print.h"

void print_log(log_t *log)
{
    unsigned i;
    BLOCK_PRINTF_BEGIN();
    BLOCK_PRINTF("rate: samples/block: %u/%u\r\n",
                 log->sample_count, log->count);
    BLOCK_PRINTF("compressed block:\r\n");
    for (i = 0; i < log->count; ++i) {
        BLOCK_PRINTF("%04x ", log->data[i]);
        if (i > 0 && ((i + 1) & (8 - 1)) == 0)
            BLOCK_PRINTF("\r\n");
    }
    if ((log->count & (8 - 1)) != 0)
        BLOCK_PRINTF("\r\n");
    BLOCK_PRINTF_END();
}
