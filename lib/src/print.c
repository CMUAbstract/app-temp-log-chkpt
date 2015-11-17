#include <libio/log.h>

#include "templog.h"
#include "print.h"

void print_log(log_t *log)
{
    unsigned i;
    BLOCK_PRINTF_BEGIN();
    BLOCK_PRINTF("compressed block: ");
    for (i = 0; i < BLOCK_SIZE; ++i) {
        BLOCK_PRINTF("%04u ", log->data[i]);
        if (i > 0 && (i + 1) % 8 == 0)
            BLOCK_PRINTF("\r\n");
    }
    BLOCK_PRINTF("rate: samples/block: %u/%u\r\n",
                 log->sample_count, BLOCK_SIZE);
    BLOCK_PRINTF_END();
}
