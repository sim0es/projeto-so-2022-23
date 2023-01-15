#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "register_t.h"
#include "response_t.h"

#define PIPE_NAME "session_pipe"
#define MESSAGE_SIZE 256

int main(int argc, char *argv[])
{
    // Check command line arguments
    if (argc < 4) 
    {
        printf("Usage: pub <register_pipe_name> <pipe_name <box_name>\n");
        exit(EXIT_FAILURE);
    }

    char *register_pipe_name = argv[1];
    char *pipe_name = argv[2];
    char *box_name = argv[3];


    // Create session pipe
    if (mkfifo(PIPE_NAME, 0666) < 0) 
    {
        perror("Error creating session pipe");
        return 1;
    }

    // Connect to server
    int server_fd = open(register_pipe_name, O_WRONLY);
    if (server_fd < 0) 
    {
        perror("Error connecting to server");
        return 1;
    }

    // Send register message to server
    register_t register_msg;

    register_msg.code = 1;
    memset(register_msg.client_named_pipe_path, 0, 256);
    strcpy(register_msg.client_named_pipe_path, pipe_name);
    memset(register_msg.box_name, 0, 32);
    strcpy(register_msg.box_name, box_name);


    if (write(server_fd, &register_msg, sizeof(register_t)) < 0) 
    {
        perror("Error sending register message to server");
        return 1;
    }

    // Open session pipe
    int session_fd = open(pipe_name, O_WRONLY);
    if (session_fd < 0) 
    {
        perror("Error opening session pipe");
        return 1;
    }

    // Wait for response from server
    response_t response;
    if (read(session_fd, &response, sizeof(response_t)) < 0)
    {
        perror("Error reading response from server");
        return 1;
    }

    if (response.code == 0)
    {
        // Successful registration
        printf("Successfully registered with server.\n");

        // Begin publishing messages
        char message[MESSAGE_SIZE];
        while (1)
        {
            printf("Enter message to publish: ");
            scanf("%s", message);
            if (write(session_fd, message, sizeof(message)) < 0)
            {
                perror("Error writing message to session pipe");
                break;
            }
        }
    }
    else
    {
        // Registration failed
        printf("Registration failed. Error code: %d\n", response.code);
    }

    // Close pipes and exit 
    close(session_fd);
    close(server_fd);
    unlink(PIPE_NAME);
    return 0;
}