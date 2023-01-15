#ifndef __TOOLS_H__
#define __TOOLS_H__

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define MAX_BOX_COUNT 16
#define MAX_BOX_NAME 32
#define MAX_PIPE_NAME 256
#define MAX_ERROR_MSG 1024
#define MAX_PUB_MSG 1024

typedef enum {
    TFS_OPCODE_REG_PUB = 1,
    TFS_OPCODE_REG_SUB = 2,
    TFS_OPCODE_ANS_CRT_BOX = 5,
    TFS_OPCODE_ANS_RMV_BOX = 6,
    TFS_OPCODE_CRT_BOX = 3,
    TFS_OPCODE_RMV_BOX = 4,
    TFS_OPCODE_LST_BOX = 7,
    TFS_OPCODE_ANS_LST_BOX = 8,
    TFS_OPCODE_PUB_MSG = 9,
    TFS_OPCODE_SUB_MSG = 10,
} tfs_opcode_t;

typedef struct {
    char name[MAX_BOX_NAME + 1];
    uint64_t size;
    uint64_t n_subscribers;
    uint64_t n_publishers;
} box_t;

typedef struct node {
    box_t *data;
    struct node *next;
} node_t;

box_t *find_box(node_t *head, char *box_name);

void packet_cpy(void *packet, size_t *offset, const void *buff, size_t len);

ssize_t safe_write(int fd, const void *buff, size_t len);

ssize_t safe_read(int fd, void *buff, size_t len);

void init_tfs_box(box_t *box, char *box_name);

void append_box(node_t **head, box_t *data);

void delete_box(node_t **head, char *box_name);

int compare_boxes(const void *b1, const void *b2);


#endif