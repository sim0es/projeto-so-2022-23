#include "logging.h"

int main(int argc, char **argv) 
{
    if (argc < 4)
    {
        printf("Usage: sub <register_pipe_name> <pipe_name> <box_name>\n", argv[0]);
        return 1;
    }
    
    (void)argc;
    (void)argv;
    fprintf(stderr, "usage: sub <register_pipe_name> <box_name>\n");
    WARN("unimplemented"); // TODO: implement
    return -1;
}
