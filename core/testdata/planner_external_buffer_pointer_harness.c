#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

intptr_t run_external_buffer_pointer_smoke(void **records, intptr_t N);

int main(void)
{
    void *records[5];
    for (intptr_t index = 0; index < 5; ++index)
    {
        intptr_t *box = malloc(sizeof(*box));
        if (box == NULL)
        {
            return 1;
        }
        *box = index + 1;
        records[index] = box;
    }

    printf("{\"result\": %" PRIdPTR "}\n", run_external_buffer_pointer_smoke(records, 5));
    return 0;
}
