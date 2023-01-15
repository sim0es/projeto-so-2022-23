#include "logging.h"
#include "utils/tools.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

static int reg_fd;
char out_pipe_name[MAX_PIPE_NAME + 1] = {0};
char in_pipe_name[MAX_PIPE_NAME + 1] = {0};
char box_name[MAX_BOX_NAME + 1] = {0};

void print_usage_and_exit() {
    fprintf(stderr, "usage: sub <register_pipe_name> <pipe_name> <box_name>\n");
    exit(EXIT_FAILURE);
}

int create_in_pipe() {
    if (unlink(in_pipe_name) != 0 && errno != ENOENT) {
        WARN("Error unlinking pipe: '%s'", in_pipe_name);
        return -1;
    }

    if (mkfifo(in_pipe_name, 0777) < 0) {
        WARN("Error creating pipe: '%s'", in_pipe_name);
        return -1;
    }
    return 0;
}

int open_out_pipe() {
    reg_fd = open(out_pipe_name, O_WRONLY);
    if (reg_fd < 0) {
        WARN("Error opening pipe: '%s'", out_pipe_name);
        return -1;
    }
    return 0;
}

int send_register_request() {
    const uint8_t reg_opcode = TFS_OPCODE_REG_SUB;
    size_t offset = 0;
    size_t packet_len = sizeof(uint8_t) + (sizeof(char) * MAX_PIPE_NAME) +
                        (sizeof(char) * MAX_BOX_NAME);

    void *packet = malloc(packet_len);
    packet_cpy(packet, &offset, &reg_opcode, sizeof(uint8_t));
    packet_cpy(packet, &offset, in_pipe_name, sizeof(char) * MAX_PIPE_NAME);
    packet_cpy(packet, &offset, box_name, sizeof(char) * MAX_BOX_NAME);

    ssize_t bytes_written = safe_write(reg_fd, packet, packet_len);
    if (bytes_written != packet_len) {
        WARN("Error writing to pipe: '%s'", out_pipe_name);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        print_usage_and_exit();
    }

    memcpy(out_pipe_name, argv[1], MAX_PIPE_NAME);
    memcpy(in_pipe_name, argv[2], MAX_PIPE_NAME);
    memcpy(box_name, argv[3], MAX_BOX_NAME);

    if (create_in_pipe() != 0) {
        PANIC("Error creating client pipe: '%s'", in_pipe_name);
    }
    if (open_out_pipe() != 0) {
        unlink(in_pipe_name);
        PANIC("Error opening registration pipe: '%s'", out_pipe_name);
    }
    if (send_register_request() != 0) {
        close(reg_fd);
        unlink(in_pipe_name);
        PANIC("Error registering subscriber");
    }
    close(reg_fd);

    int in_fd = open(in_pipe_name, O_RDONLY);
    if (in_fd < 0) {
        unlink(in_pipe_name);
        PANIC("Error opening client pipe: '%s'", in_pipe_name);
    }

    uint8_t ret_op_code;
    char message[MAX_PUB_MSG + 1] = {0};

    ssize_t bytes_read = safe_read(in_fd, &ret_op_code, sizeof(uint8_t));
    while (bytes_read > 0) {
        if (bytes_read < 0) {
            unlink(in_pipe_name);
            PANIC("Error reading from pipe: '%s' - %s", in_pipe_name, strerror(errno));
        }
        if (ret_op_code != TFS_OPCODE_SUB_MSG) {
            PANIC("Invalid opcode %d", ret_op_code);
        }
        bytes_read = safe_read(in_fd, message, sizeof(char) * MAX_PUB_MSG);
        if (bytes_read != sizeof(char) * MAX_PUB_MSG) {
            unlink(in_pipe_name);
            PANIC("Error reading from pipe: '%s' - %s", in_pipe_name, strerror(errno));
        }
        fprintf(stdout, "%s\n", message);
        memset(message, 0, sizeof(message));
        bytes_read = safe_read(in_fd, &ret_op_code, sizeof(uint8_t));
    }

    unlink(in_pipe_name);
    exit(EXIT_SUCCESS);
}
