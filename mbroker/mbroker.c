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

    // Apagar lista de caixas;

    fprintf(stdout, "Successfully closing mbroker...\n");
    exit(status);
}

static int create_box(char *box_name, char *error_msg) {
    int ret_status = -1;
    int fhandle = tfs_open(box_name, TFS_O_APPEND);
    box_t *box;

    if (fhandle == -1) {
        fhandle = tfs_open(box_name, TFS_O_CREAT);
        if (fhandle != -1) {
            ret_status = 0;
            if (tfs_close(fhandle) != 0) {
                WARN("Error closing box.");
            }
        } else {
            strcpy(error_msg, "Error creating file.");
        }
    } else {
        strcpy(error_msg, "Box name already exists.");
        if (tfs_close(fhandle) != 0) {
            WARN("Error closing box.");
        }
    }

    box = malloc(sizeof(box_t));
    init_tfs_box(box, box_name);

    append_box(&head, box);
    box_count++;

    return ret_status;
}

static int remove_box(char *box_name, char *error_msg) {
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

    packet_cpy(packet, offset, &ret_opcode, sizeof(uint8_t));
    packet_cpy(packet, offset, &last, sizeof(uint8_t));
    packet_cpy(packet, offset, box->name, sizeof(char) * MAX_BOX_NAME);
    packet_cpy(packet, offset, &box->size, sizeof(uint64_t));
    packet_cpy(packet, offset, &box->n_publishers, sizeof(uint64_t));
    packet_cpy(packet, offset, &box->n_subscribers, sizeof(uint64_t));
}

int handle_box_wrapper(int (*handle_box_func)(char *, char *)) {
    char client_path[MAX_PIPE_NAME + 1];
    char box_name[MAX_BOX_NAME + 1];
    ssize_t bytes_read;

    bytes_read = safe_read(fd_in, client_path, sizeof(char) * MAX_PIPE_NAME);
    client_path[MAX_PIPE_NAME] = '\0';

    if (bytes_read != sizeof(char) * MAX_PIPE_NAME) {
        PANIC("Error reading from pipe %s\n", in_pipe_path);
    }

    bytes_read = safe_read(fd_in, box_name, sizeof(char) * MAX_BOX_NAME);
    box_name[MAX_BOX_NAME] = '\0';

    if (bytes_read != sizeof(char) * MAX_BOX_NAME) {
        PANIC("Error reading from pipe %s\n", in_pipe_path);
    }

    uint8_t ret_op_code = TFS_OPCODE_ANS_CRT_BOX;
    int32_t ret_status = -1;
    char error_msg[MAX_ERR_MSG + 1] = {0};

    ret_status = handle_box_func(box_name, error_msg);

    int out_fd = open(client_path, O_WRONLY);
    if (out_fd < 0) {
        WARN("Failed to open client pipe: %s", client_path);
        return -1;
    }

    size_t offset = 0;
    size_t packet_len = sizeof(uint8_t) + sizeof(int32_t);
    packet_len += sizeof(char) * MAX_ERR_MSG;

    void *packet = malloc(packet_len);
    packet_cpy(packet, &offset, &ret_op_code, sizeof(uint8_t));
    packet_cpy(packet, &offset, &ret_status, sizeof(int32_t));
    packet_cpy(packet, &offset, error_msg, sizeof(char) * MAX_ERR_MSG);

    ssize_t written = safe_write(out_fd, packet, packet_len);
    close(out_fd);

    if (written != packet_len || ret_status != 0) {
        return -1;
    }

    INFO("Successfully handled creation/deletion of box '%s'", box_name);

    return 0;
}

int handle_list_boxes() {
    char client_path[MAX_PIPE_NAME + 1];

    ssize_t bytes_read =
        safe_read(fd_in, client_path, sizeof(char) * MAX_PIPE_NAME);
    client_path[MAX_PIPE_NAME] = '\0';

    if (bytes_read != sizeof(char) * MAX_PIPE_NAME) {
        PANIC("Error reading from pipe %s\n", in_pipe_path);
    }

    const uint8_t ret_opcode = TFS_OPCODE_ANS_LST_BOX;
    uint8_t last = 0;
    size_t packet_len = (2 * sizeof(uint8_t)) + (sizeof(char) * MAX_BOX_NAME) +
                        (sizeof(uint64_t) * 3);
    size_t offset = 0;
    void *packet = malloc(packet_len);

    int out_fd = open(client_path, O_WRONLY);
    if (out_fd < 0) {
        WARN("Failed to open client pipe: %s", client_path);
        return -1;
    }

    if (head == NULL) {
        last = 1;
        char null_box_name[MAX_BOX_NAME + 1] = {0};
        uint64_t null_box_size = 0, null_n_pubs = 0, null_n_subs = 0;
        packet_cpy(packet, &offset, &ret_opcode, sizeof(uint8_t));
        packet_cpy(packet, &offset, &last, sizeof(uint8_t));
        packet_cpy(packet, &offset, null_box_name, sizeof(char) * MAX_BOX_NAME);
        packet_cpy(packet, &offset, &null_box_size, sizeof(uint64_t));
        packet_cpy(packet, &offset, &null_n_pubs, sizeof(uint64_t));
        packet_cpy(packet, &offset, &null_n_subs, sizeof(uint64_t));

        ssize_t bytes_written = safe_write(out_fd, packet, packet_len);
        if (bytes_written != packet_len) {
            WARN("Error writing to pipe: '%s'", client_path);
            free(packet);
            close(out_fd);
            return -1;
        }
    } else {
        node_t *tmp = head;
        while (tmp->next != NULL) {
            write_box(packet, tmp->data, last, &offset);

            ssize_t bytes_written = safe_write(out_fd, packet, packet_len);
            if (bytes_written != packet_len) {
                WARN("Error writing to pipe: '%s'", client_path);
                free(packet);
                close(out_fd);
                return -1;
            }

            tmp = tmp->next;
            offset = 0;
        }
        last = 1;
        write_box(packet, tmp->data, last, &offset);
        ssize_t bytes_written = safe_write(out_fd, packet, packet_len);
        if (bytes_written != packet_len) {
            WARN("Error writing to pipe: '%s'", client_path);
            free(packet);
            close(out_fd);
            return -1;
        }
    }

    INFO("Successfully listed boxes");

    return 0;
}

int main(int argc, char **argv) {
    set_log_level(LOG_NORMAL);

    signal(SIGINT, safe_close);
    signal(SIGPIPE, SIG_IGN);

    if (argc < 3) {
        print_usage_and_exit();
    }

    if (tfs_init(NULL) != 0) {
        PANIC("Failed to init tfs\n");
    }

    in_pipe_path = argv[1];

    if (unlink(in_pipe_path) != 0 && errno != ENOENT) {
        PANIC("Failed to unlink pipe: %s\n", strerror(errno));
    }

    if (mkfifo(in_pipe_path, 0777) < 0) {
        PANIC("mkfifo failed: %s\n", strerror(errno));
    }

    INFO("Starting mbroker with registry pipe '%s'", in_pipe_path);

    fd_in = open(in_pipe_path, O_RDONLY);
    if (fd_in < 0) {
        unlink(in_pipe_path);
        PANIC("Failed to open server pipe\n");
    }

    while (true) {
        int tmp_fd = open(in_pipe_path, O_RDONLY);

        if (tmp_fd < 0) {
            if (errno == ENOENT) {
                return 0;
            }
            WARN("Failed to open server pipe");
            safe_close(EXIT_FAILURE);
        }

        if (close(tmp_fd) < 0) {
            WARN("Failed to close pipe");
            safe_close(EXIT_FAILURE);
        }

        ssize_t b_read;
        uint8_t op_code;

        b_read = safe_read(fd_in, &op_code, sizeof(uint8_t));

        while (b_read > 0) {
            switch (op_code) {
            case TFS_OPCODE_CRT_BOX:
                if (handle_box_wrapper(create_box) != 0) {
                    WARN("Error creating a new box");
                }
                break;

            case TFS_OPCODE_RMV_BOX:
                if (handle_box_wrapper(remove_box) != 0) {
                    WARN("Error removing a box");
                }
                break;

            case TFS_OPCODE_LST_BOX:
                if (handle_list_boxes() != 0) {
                    WARN("Error listing boxes");
                }
                break;

            default:
                break;
            }
            b_read = safe_read(fd_in, &op_code, sizeof(uint8_t));
        }

        if (b_read < 0) {
            WARN("Failed to read pipe");
            safe_close(EXIT_FAILURE);
        }
    }

    return 0;
}