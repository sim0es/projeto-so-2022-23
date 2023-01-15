#include "fs/operations.h"
#include "logging.h"
#include "utils/tools.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

pthread_mutex_t new_box_lock = PTHREAD_MUTEX_INITIALIZER;

static int fd_in;
static char *in_pipe_path;

static int box_count = 0;
static node_t *head = NULL;

static void print_instructions() {
    fprintf(stderr, "usage: mbroker <pipename> <max_sessions>\n");
    exit(EXIT_FAILURE);
}

void safe_close(int status) {
    if (close(fd_in) < 0) {
        PANIC("Failed to close pipe on exit\n");
    }

    if (unlink(in_pipe_path) != 0) {
        PANIC("Failed to delete pipe on exit: %s\n", strerror(errno));
    }

    while (head) {
        node_t *next = head->next;
        free(head);
        head = next;
    }

    fprintf(stdout, "Successfully closing mbroker...\n");
    exit(status);
}

int subscriber() {
    ssize_t bytes_read;
    char client_path[MAX_PIPE_NAME + 1] = {0};
    char box_name[MAX_BOX_NAME + 1] = {0};

    bytes_read = read(fd_in, client_path, sizeof(char) * MAX_PIPE_NAME);
    if (bytes_read != sizeof(char) * MAX_PIPE_NAME) {
        if (bytes_read == -1) {
            WARN("Error reading from register pipe: %s", strerror(errno));
        } else {
            WARN("Unexpected number of bytes read from register pipe. \
                    Expected %lu, got %lu", sizeof(char) * MAX_PIPE_NAME, bytes_read);
        }
        return -1;
    }

    bytes_read = read(fd_in, box_name, sizeof(char) * MAX_BOX_NAME);
    if (bytes_read != sizeof(char) * MAX_BOX_NAME) {
        if (bytes_read == -1) {
            WARN("Error reading from register pipe: %s", strerror(errno));
        } else {
            WARN("Unexpected number of bytes read from register pipe. \ 
                    Expected %lu, got %lu", sizeof(char) * MAX_BOX_NAME, bytes_read);
        }
        return -1;
    }

    return 0;
}

int handle_publisher() {
    ssize_t bytes_read;
    char client_path[MAX_PIPE_NAME + 1] = {0};
    char box_name[MAX_BOX_NAME + 1] = {0};

    bytes_read = read(fd_in, client_path, sizeof(char) * MAX_PIPE_NAME);
    if (bytes_read != sizeof(char) * MAX_PIPE_NAME) {
        WARN("Error reading from register pipe");
        return -1;
    }
    bytes_read = read(fd_in, box_name, sizeof(char) * MAX_BOX_NAME);
    if (bytes_read != sizeof(char) * MAX_BOX_NAME) {
        WARN("Error reading from register pipe");
        return -1;
    }

    int client_fd = open(client_path, O_RDONLY);
    if (client_fd < 0) {
        WARN("Error opening pipe: '%s' - %s", client_path, strerror(errno));
        return -1;
    }

    box_t *box = find_box(head, box_name);
    if (box == NULL || box->n_publishers > 0) {
        close(client_fd);
        WARN("Error registering publisher");
        return -1;
    }

    box->n_publishers++;

    uint8_t pub_opcode;
    char message[MAX_PUB_MSG + 1] = {0};

    bytes_read = safe_read(client_fd, &pub_opcode, sizeof(uint8_t));
    while (bytes_read > 0) {
        if (pub_opcode != TFS_OPCODE_PUB_MSG) {
            PANIC("Invalid opcode %u\n", pub_opcode);
        }
        bytes_read = safe_read(client_fd, message, MAX_PUB_MSG);
        if (bytes_read != MAX_PUB_MSG) {
            PANIC("Error reading from pipe: '%s'", client_path);
        }
        INFO("Received message: %s", message);
        int fhandle = tfs_open(box_name, TFS_O_APPEND);
        if (fhandle == -1) {
            WARN("Can't open tfs file");
            return -1;
        }

        ssize_t bytes_written =
            tfs_write(fhandle, message, strlen(message) + 1);
        tfs_close(fhandle);
        if (bytes_written != strlen(message) + 1) {
            WARN("Error writing to tfs file");
            return -1;
        }
        memset(message, 0, MAX_PUB_MSG);
        bytes_read = safe_read(client_fd, &pub_opcode, sizeof(uint8_t));
    }

    if (bytes_read < 0) {
        WARN("Error reading from pipe: '%s'", in_pipe_path);
        return -1;
    }

    INFO("Publisher closed the session");
    box->n_publishers--;

    return 0;
}

static int new_box(char *box_name, char *error_msg) {
    int ret_status = -1;
    box_t *box;

    pthread_mutex_init(&new_box_lock, NULL);
    pthread_mutex_lock(&new_box_lock); // acquire the mutex
    
    box = malloc(sizeof(box_t));
    init_tfs_box(box, box_name);

    int fhandle = tfs_open(box_name, TFS_O_APPEND);
    if (fhandle == -1) {
        fhandle = tfs_open(box_name, TFS_O_CREAT);
        if (fhandle != -1) {
            ret_status = 0;
            if (tfs_close(fhandle) != 0) {
                snprintf(error_msg, MAX_ERROR_MSG, "Error closing box.");
            }
        } else {
            snprintf(error_msg, MAX_ERROR_MSG, "Error creating file.");
        }
    } else {
        snprintf(error_msg, MAX_ERROR_MSG, "Box name already exists.");
        if (tfs_close(fhandle) != 0) {
            snprintf(error_msg, MAX_ERROR_MSG, "Error closing box.");
        }
    }
    if (ret_status == 0) {
        append_box(&head, box);
        box_count++;
    } else {
        free(box);
    }
    pthread_mutex_unlock(&new_box_lock); // release the mutex
    pthread_mutex_destroy(&new_box_lock);
    return ret_status;
}

static int remove_box(char *box_name, char *error_msg) 
{
    delete_box(&head, box_name);
    box_count--;

    if (tfs_unlink(box_name) != 0) {
        strcpy(error_msg, "Error deleting box.");
        return -1;
    }

    return 0;
}

static void write_box(void *packet, box_t *box, uint8_t last, size_t *offset) {
    const uint8_t ret_opcode = TFS_OPCODE_ANS_LST_BOX;

    packet_cpy(packet, offset, &box->n_subscribers, sizeof(uint64_t));
    packet_cpy(packet, offset, &box->n_publishers, sizeof(uint64_t));
    packet_cpy(packet, offset, &ret_opcode, sizeof(uint8_t));
    packet_cpy(packet, offset, &box->size, sizeof(uint64_t));
    packet_cpy(packet, offset, &last, sizeof(uint8_t));
    packet_cpy(packet, offset, box->name, sizeof(char) * MAX_BOX_NAME);
}

int handle_box_wrapper(int (*handle_box_func)(char *, char *)) {
    char client_path[MAX_PIPE_NAME + 1];
    char box_name[MAX_BOX_NAME + 1];
    char error_msg[MAX_ERROR_MSG + 1] = {0};
    ssize_t bytes_read;
    int ret;

    bytes_read = safe_read(fd_in, client_path, sizeof(char) * MAX_PIPE_NAME);
    client_path[MAX_PIPE_NAME] = '\0';

    if (bytes_read != sizeof(char) * MAX_PIPE_NAME) {
        printf("Error reading from pipe %s\n", in_pipe_path);
        return -1;
    }

    bytes_read = safe_read(fd_in, box_name, sizeof(char) * MAX_BOX_NAME);
    box_name[MAX_BOX_NAME] = '\0';

    if (bytes_read != sizeof(char) * MAX_BOX_NAME) {
        printf("Error reading from pipe %s\n", in_pipe_path);
        return -1;
    }

    ret = handle_box_func(box_name, error_msg);

    if (ret < 0) {
        int client_fd = open(client_path, O_WRONLY);
        if (client_fd < 0) {
            printf("Error opening client pipe %s\n", client_path);
            return -1;
        }

        safe_write(client_fd, error_msg, sizeof(char) * MAX_ERROR_MSG);
        close(client_fd);
        return -1;
    } else {
        return 0;
    }
}

int handle_list_boxes(void) {
    char client_path[MAX_PIPE_NAME + 1];
    ssize_t bytes_read;

    bytes_read = safe_read(fd_in, client_path, sizeof(char) * MAX_PIPE_NAME);
    client_path[MAX_PIPE_NAME] = '\0';

    if (bytes_read != sizeof(char) * MAX_PIPE_NAME) {
        printf("Error reading from pipe %s\n", in_pipe_path);
        return -1;
    }

    int fd_out = open(client_path, O_WRONLY);
    if (fd_out < 0) {
        printf("Error opening pipe %s\n", client_path);
        return -1;
    }

    if (write(fd_out, &box_count, sizeof(int)) < 0) {
        printf("Error writing to pipe %s\n", client_path);
        return -1;
    }

    node_t *cur = head;
    while (cur) {
        if (write(fd_out, cur->data, sizeof(box_t)) < 0) {
            printf("Error writing to pipe %s\n", client_path);
            return -1;
        }
        cur = cur->next;
    }

    close(fd_out);
    return 0;
}

int main(int argc, char *argv[]) {
    // Check the number of arguments
    if (argc != 3) {
        print_instructions();
    }

    // Parse arguments
    in_pipe_path = argv[1];
    int max_sessions = atoi(argv[2]);

    // Create input pipe
    if (mkfifo(in_pipe_path, 0666) < 0) {
        PANIC("Failed to create pipe '%s': %s\n", in_pipe_path, strerror(errno));
    }

    // Open input pipe
    fd_in = open(in_pipe_path, O_RDONLY | O_NONBLOCK);
    if (fd_in < 0) {
        PANIC("Failed to open pipe '%s': %s\n", in_pipe_path, strerror(errno));
    }

    // Set up signal handlers for clean exit
    signal(SIGINT, safe_close);
    signal(SIGTERM, safe_close);

    // Initialize data structures
    head = NULL;

    // Main loop
    for (int i = 0; i < max_sessions; i++) {
        char client_path[MAX_PIPE_NAME + 1];
        ssize_t bytes_read;

        // Read from input pipe
        bytes_read = safe_read(fd_in, client_path, sizeof(char) * MAX_PIPE_NAME);
        client_path[MAX_PIPE_NAME] = '\0';

        if (bytes_read != sizeof(char) * MAX_PIPE_NAME) {
            printf("Error reading from pipe %s\n", in_pipe_path);
            continue;
        }

        // Open output pipe
        int fd_out = open(client_path, O_WRONLY);
        if (fd_out < 0) {
            printf("Error opening pipe %s\n", client_path);
            continue;
        }

        // Read opcode
        uint8_t opcode;
        bytes_read = safe_read(fd_in, &opcode, sizeof(uint8_t));
        if (bytes_read != sizeof(uint8_t)) {
            printf("Error reading opcode from pipe %s\n", in_pipe_path);
            close(fd_out);
            continue;
        }

        // Handle request
        switch (opcode) {
            case TFS_OPCODE_CRT_BOX:
                handle_box_wrapper(new_box);
                break;
            case TFS_OPCODE_RMV_BOX:
                handle_box_wrapper(remove_box);
                break;
            case TFS_OPCODE_LST_BOX:
                handle_list_boxes();
                break;
            case TFS_OPCODE_REG_SUB:
                subscriber();
                break;
            case TFS_OPCODE_REG_PUB:
                publisher();
                break;
            default:
                printf("Invalid opcode received: %d\n", opcode);
                break;
        }

        // Close output pipe
        close(fd_out);
    }

    safe_close(EXIT_SUCCESS);
    return 0;
}
