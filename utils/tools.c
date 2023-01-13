#include "tools.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

void packet_cpy(void *packet, size_t *offset, const void *buff, size_t len) {
    memcpy(packet + *offset, buff, len);
    *offset += len;
}

void init_tfs_box(box_t *box, char *box_name) {
    box->n_publishers = 0;
    box->n_subscribers = 0;
    box->size = 0;
    strncpy(box->name, box_name, MAX_BOX_NAME);
}

void append_box(node_t **head, box_t *data) {
    node_t *new_node = malloc(sizeof(node_t));
    new_node->data = data;
    new_node->next = NULL;

    if (*head == NULL) {
        *head = new_node;
    } else {
        node_t *last_node = *head;

        while (last_node->next != NULL) {
            last_node = last_node->next;
        }

        last_node->next = new_node;
    }
}

void delete_box(node_t **head, char *box_name) {
    node_t *tmp;

    if (!strcmp((*head)->data->name, box_name)) {
        tmp = *head;
        *head = (*head)->next;
        free(tmp);
    } else {
        node_t *current = *head;
        while (current->next != NULL) {
            if (!strcmp(current->next->data->name, box_name)) {
                tmp = current->next;
                // node will be disconnected from the linked list.
                current->next = current->next->next;
                free(tmp);
                break;
            } else {
                current = current->next;
            }
        }
    }
}

int compare_boxes(const void *b1, const void *b2) {
    box_t *box1 = (box_t *)b1;
    box_t *box2 = (box_t *)b2;

    return strcmp(box1->name, box2->name);
}

ssize_t safe_read(int fd, void *buff, size_t len) {
    ssize_t b_read;
    do {
        b_read = read(fd, buff, len);
    } while (b_read < 0 && errno == EINTR);

    return b_read;
}

ssize_t safe_write(int fd, const void *buff, size_t len) {
    ssize_t written;
    do {
        written = write(fd, buff, len);
    } while (written < 0 && errno == EINTR);

    return written;
}