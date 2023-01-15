#include "logging.h"
#include "utils/tools.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char out_pipe_path[MAX_PIPE_NAME + 1] = {0};
static char in_pipe_path[MAX_PIPE_NAME + 1] = {0};

static void print_usage() {
    fprintf(stderr,
            "usage: \n"
            "   manager <register_pipe_name> <pipe_name> create <box_name>\n"
            "   manager <register_pipe_name> <pipe_name> remove <box_name>\n"
            "   manager <register_pipe_name> <pipe_name> list\n");
}

static void print_usage_and_exit() {
    print_usage();
    exit(EXIT_FAILURE);
}

int commands_to_box(char *box_name, tfs_opcode_t op_code) {
    int out_fd = open(out_pipe_path, O_WRONLY);
    if (out_fd < 0) {
        PANIC("Failed to open register pipe: %s\n", strerror(errno));
    }

    size_t packet_len = sizeof(uint8_t);
    packet_len += sizeof(char) * (MAX_PIPE_NAME + MAX_BOX_NAME);

    void *packet = malloc(packet_len);
    size_t offset = 0;
    const uint8_t code = op_code;

    packet_cpy(packet, &offset, &code, sizeof(uint8_t));
    packet_cpy(packet, &offset, in_pipe_path, sizeof(char) * MAX_PIPE_NAME);
    packet_cpy(packet, &offset, box_name, sizeof(char) * MAX_BOX_NAME);

    if (safe_write(out_fd, packet, packet_len) != packet_len) {
        PANIC("Error writing to pipe %s\n", out_pipe_path);
    }

    int in_fd = open(in_pipe_path, O_RDONLY);
    if (in_fd < 0) {
        WARN("Error opening client pipe '%s'", in_pipe_path);
        return -1;
    }

    uint8_t ret_op_code = '\0';
    int32_t ret_status;
    char error_msg[MAX_ERROR_MSG + 1];

    if (ret_op_code != TFS_OPCODE_ANS_CRT_BOX &&
        ret_op_code != TFS_OPCODE_ANS_RMV_BOX) {
        PANIC("Invalid opcode %d\n", ret_op_code);
    }

    if (ret_status != 0) {
        if (safe_read(in_fd, error_msg, sizeof(char) * MAX_ERROR_MSG) != sizeof(char) * MAX_ERROR_MSG) {
            WARN("Error reading from pipe: %s\n", in_pipe_path);
            return -1;
        }
        error_msg[MAX_ERROR_MSG] = '\0';
        WARN("[ERROR] %s\n", error_msg);
    }

    close(in_fd);
    unlink(in_pipe_path);
    close(out_fd);
    return ret_status;
}

int list_boxes() {
    int out_fd = open(out_pipe_path, O_WRONLY);
    if (out_fd < 0) {
        PANIC("Failed to open register pipe: %s\n", strerror(errno));
    }

    size_t packet_len = sizeof(uint8_t);
    packet_len += sizeof(char) * MAX_PIPE_NAME;

    void *packet = malloc(packet_len);

    size_t offset = 0;
    const uint8_t code = TFS_OPCODE_LST_BOX;

    packet_cpy(packet, &offset, &code, sizeof(uint8_t));
    packet_cpy(packet, &offset, in_pipe_path, sizeof(char) * MAX_PIPE_NAME);

    ssize_t bytes_written = safe_write(out_fd, packet, packet_len);
    free(packet);
    close(out_fd);

    if (bytes_written != packet_len) {
        PANIC("Error writing to pipe %s\n", out_pipe_path);
    }

    int in_fd = open(in_pipe_path, O_RDONLY);
    if (in_fd < 0) {
        unlink(in_pipe_path);
        WARN("Error opening client pipe '%s'", in_pipe_path);
        return -1;
    }

    uint8_t ret_op_code = '\0';
    int32_t ret_status;
    char error_msg[MAX_ERROR_MSG + 1];
    uint64_t box_count = 0;

    if (ret_op_code != TFS_OPCODE_ANS_LST_BOX) {
        PANIC("Invalid opcode %d\n", ret_op_code);
    }

    if (safe_read(in_fd, &ret_status, sizeof(int32_t)) != sizeof(int32_t)) {
        WARN("Error reading from pipe: %s\n", in_pipe_path);
        return -1;
    }

    if (ret_status != 0) {
        if (safe_read(in_fd, error_msg, sizeof(char) * MAX_ERROR_MSG) != sizeof(char) * MAX_ERROR_MSG) {
            WARN("Error reading from pipe: %s\n", in_pipe_path);
            return -1;
        }
        printf("Error: %s\n", error_msg);
        return ret_status;
    }

    if (box_count == 0) {
        printf("No boxes found\n");
        return 0;
    }

    box_t boxes[box_count];


    for (uint64_t i = 0; i < box_count; i++) {
        printf("Box name: %s\n", boxes[i].name);
        printf("Size: %lu\n", boxes[i].size);
        printf("Publishers: %lu\n", boxes[i].n_publishers);
        printf("Subscribers: %lu\n", boxes[i].n_subscribers);
    }

    close(in_fd);
    unlink(in_pipe_path);
    return 0;
}
           

int main(int argc, char *argv[]) {
    if (argc < 4) {
        print_usage_and_exit();
    }

    strncpy(out_pipe_path, argv[1], MAX_PIPE_NAME);
    strncpy(in_pipe_path, argv[2], MAX_PIPE_NAME);

    if (mkfifo(in_pipe_path, 0666) < 0) {
        PANIC("Failed to create pipe %s\n", in_pipe_path);
    }

    char *command = argv[3];
    if (strcmp(command, "create") == 0) {
        if (argc < 5) {
            print_usage_and_exit();
        }
        char *box_name = argv[4];
        int ret = commands_to_box(box_name, TFS_OPCODE_CRT_BOX);
        if (ret < 0) {
            WARN("Command failed\n");
            exit(EXIT_FAILURE);
        }
        printf("Box created successfully\n");
    } else if (strcmp(command, "remove") == 0) {
        if (argc < 5) 
        {
            print_usage_and_exit();
        }
        char *box_name = argv[4];
        int ret = commands_to_box(box_name, TFS_OPCODE_RMV_BOX);
        if (ret < 0) {
            WARN("Command failed\n");
            exit(EXIT_FAILURE);
        }
        printf("Box removed successfully\n");
    } else if (strcmp(command, "list") == 0) {
        int ret = list_boxes();
        if (ret < 0) {
            WARN("Command failed\n");
            exit(EXIT_FAILURE);
        }
    } else {
        print_usage_and_exit();
    }

    return 0;
}