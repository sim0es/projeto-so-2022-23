#include <stdint.h>

typedef struct
{
    uint8_t code;
    char client_named_pipe_path [256];
    char box_name [32];
    
}register_t;