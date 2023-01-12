#include <stdint.h>

typedef struct
{
    uint8_t code;
    uint8_t return_code;
    char error_message [124];

}response_t;