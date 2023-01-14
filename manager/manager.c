#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "logging.h"

static void print_usage() {
    fprintf(stderr, "usage: \n"
                    "   manager <register_pipe_name> create <box_name>\n"
                    "   manager <register_pipe_name> remove <box_name>\n"
                    "   manager <register_pipe_name> list\n");
}

int main(int argc, char *argv[]) 
{
    char *register_pipe_name, *command, *box_name;
    int register_pipefd;

    // check arguments
    if (argc < 4) {
        print_usage();
        return -1;
    }

    register_pipe_name = argv[1];
    command = argv[2];
    box_name = argv[3];

    // open pipe to communicate with server
    register_pipefd = open(register_pipe_name, O_WRONLY);

    if (register_pipefd == -1) {
        ERROR("Error opening pipe %s", register_pipe_name);
        return -1;
    }

    // send command to server
    if (strcmp(command, "create") == 0) 
    {
        write(register_pipefd, "create ", 7);
        write(register_pipefd, box_name, strlen(box_name));
        write(register_pipefd, " ", 1);
    } 
    else if (strcmp(command, "remove") == 0) 
    {
        write(register_pipefd, "remove ", 7);
        write(register_pipefd, box_name, strlen(box_name));
        write(register_pipefd, " ", 1);
    } 
    else if (strcmp(command, "list") == 0) 
    {
        write(register_pipefd, "list", 4);
        write(register_pipefd, " ", 1);
    } 
    else 
    {
        print_usage();
        return -1;
    }

    // receive response from server
    char buffer[1024];
    int bytes_read = read(register_pipefd, buffer, sizeof(buffer));
    buffer[bytes_read] = '\0';

    // print response
    printf("Server response: %s\n", buffer);

    close(register_pipefd);
    return 0;
}

