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

static int fd_in;
static char *in_pipe_path;

static int box_count = 0;
static node_t *head = NULL;

static void print_usage_and_exit() {
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

// alterada
static int create_box(char *box_name, char *error_msg) 
{
    int ret_status = -1;
    box_t *box = find_box(head, box_name);

    if (box == NULL) 
    {
        box = malloc(sizeof(box_t));
        init_tfs_box(box, box_name);
        append_box(&head, box);
        box_count++;
        ret_status = 0;
    } else 
    {
        strcpy(error_msg, "Box name already exists.");
    }
    return ret_status;
}

// alterada
static int remove_box(char *box_name, char *error_msg) 
{
    box_t *box = find_box(head, box_name);
    if (box != NULL) {
        delete_box(&head, box_name);
        box_count--;
        free(box);
        return 0;
    } else {
        strcpy(error_msg, "Error deleting box.");
        return -1;
    }
}

static void write_box(void *packet, box_t *box, uint8_t last, size_t *offset) {
    const uint8_t ret_opcode = TFS_OPCODE_ANS_LST_BOX;

    packet_cpy(packet, offset, &ret_opcode, sizeof(uint8_t));
    packet_cpy(packet, offset, &last, sizeof(uint8_t));
    packet_cpy(packet, offset, box->name, sizeof(char) * MAX_BOX_NAME);
    packet_cpy(packet, offset, &box->size, sizeof(uint64_t));
    packet_cpy(packet, offset, &box->n_publishers, sizeof(uint64_t));
    packet_cpy(packet, offset, &box->n_subscribers, sizeof(uint64_t));
}

// alterada
int handle_box_wrapper(int (*handle_box_func)(char *, char *)) {
    char client_path[MAX_PIPE_NAME + 1];
    char box_name[MAX_BOX_NAME + 1];
    char error_msg[MAX_ERROR_MSG + 1] = {0};
    ssize_t bytes_read;

    bytes_read = safe_read(fd_in, client_path, sizeof(char) * MAX_PIPE_NAME);
    client_path[MAX_PIPE_NAME] = '\0';

    if (bytes_read != sizeof(char) * MAX_PIPE_NAME) {
        printf("Error reading from pipe %s\n", in_pipe_path);
        return -1;
    }

    bytes_read = safe_read(fd_in, box_name, sizeof(char) * MAX_BOX_NAME);
    box_name[MAX_BOX_NAME] = '\0';

    if (bytes_read != sizeof(char) * MAX_BOX_NAME) {
        PANIC("Error reading from pipe %s\n", in_pipe_path);
        return -1;
    }

    uint8_t ret_op_code = TFS_OPCODE_ANS_CRT_BOX;
    int ret_status = handle_box_func(box_name, error_msg);

    int client_fd = open(client_path, O_WRONLY);
    if (client_fd < 0) {
        printf("Failed to open client pipe: %s", client_path);
        return -1;
    }

    size_t offset = 0;
    size_t packet_len = sizeof(uint8_t) + sizeof(int);

    if (ret_status != 0) {
        packet_len += sizeof(char) * MAX_ERROR_MSG;
    }

    void *packet = malloc(packet_len);

    if (ret_status == 0) {
        const uint8_t ret_opcode = TFS_OPCODE_ANS_OK;
        packet = malloc(sizeof(uint8_t));
        packet_cpy(packet, &offset, &ret_opcode, sizeof(uint8_t));
    } else {
        const uint8_t ret_opcode = TFS_OPCODE_ANS_ERR;
        packet = malloc(sizeof(uint8_t) + sizeof(char) * MAX_ERR_MSG);
        offset = 0;
        packet_cpy(packet, &offset, &ret_opcode, sizeof(uint8_t));
        packet_cpy(packet, &offset, error_msg, sizeof(char) * MAX_ERR_MSG);
    }

    ssize_t bytes_written = safe_write(client_fd, packet, offset);
    if (bytes_written != offset) {
        WARN("Error writing to pipe: '%s'\n", client_path);
    }

    free(packet);
    close(client_fd);

    return ret_status;
        
    // size_t offset = 0;
    // size_t packet_len = sizeof(uint8_t) + sizeof(int);
    // packet_len += sizeof(char) * MAX_ERROR_MSG;

    // void *packet = malloc(packet_len);
    // packet_cpy(packet, &offset, &ret_op_code, sizeof(uint8_t));
    // packet_cpy(packet, &offset, &ret_status, sizeof(int32_t));
    // packet_cpy(packet, &offset, error_msg, sizeof(char) * MAX_ERROR_MSG);

    // ssize_t written = safe_write(client_fd, packet, packet_len);
    // close(client_fd);

    // if (written != packet_len || ret_status != 0) {
    //     return -1;
    // }

    // printf("Box '%s' successfully closed.\n", box_name);

    // return 0;
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
        print_usage_and_exit();
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
                handle_box_wrapper(create_box);
                break;
            case TFS_OPCODE_RMV_BOX:
                handle_box_wrapper(remove_box);
                break;
            case TFS_OPCODE_LST_BOX:
                handle_list_boxes();
                break;
            default:
                printf("Invalid opcode received: %d\n", opcode);
                break;
        }

        // Close output pipe
        close(fd_out);
    }

    safe_close(EXIT_SUCCESS);
}
