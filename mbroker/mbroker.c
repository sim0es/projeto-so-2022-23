#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <logging.h>

#define MAX_SESSIONS 100

// global mutex for synchronization
pthread_mutex_t mutex;

// struct for holding session information
typedef struct 
{
    int id;   
    int type; // 0 = publisher, 1 = subscriber, 2 = manager
    int pipefd;
} session_t;

session_t sessions[MAX_SESSIONS];

void handle_session(int id) {
    session_t session = sessions[id];
    char buffer[1024];
    int bytes_read;

    while (1) 
    {
    // read message from client pipe
        bytes_read = read(session.pipefd, buffer, sizeof(buffer));

        // lock global mutex
        pthread_mutex_lock(&mutex);

        // handle message based on session type
        if (session.type == 0) {
        // publisher - handle message as a publish request
        } else if (session.type == 1) {
        // subscriber - handle message as a subscribe request
        } else {
        // manager - handle message as a management request
        }

        // unlock global mutex
        pthread_mutex_unlock(&mutex);
    }
}

int main(int argc, char *argv[]) 
{
    char *register_pipe_name;
    int max_sessions;
    int register_pipefd;
    int i, new_session_id;

    // check arguments
    if (argc < 3) 
    {
        printf("Usage: %s <register_pipe_name> <max_sessions>\n", argv[0]);
        return 1;
    }

    register_pipe_name = argv[1];
    max_sessions = atoi(argv[2]);

    // initialize global mutex
    pthread_mutex_init(&mutex, NULL);

    // initialize TecnicoFS
    if (tfs_init() < 0) 
    {
        printf("Error initializing TecnicoFS\n");
        return 1;
    }

    // create register pipe
    mkfifo(register_pipe_name, 0666);
    register_pipefd = open(register_pipe_name, O_RDONLY);

    // initialize sessions
    for (i = 0; i < max_sessions; i++) 
    {
        sessions[i].id = -1;
    }

    while (1) 
    {
        // wait for client to connect to register pipe
        int client_pipefd = open(register_pipe_name, O_WRONLY);

        // find an available session id
        for (i = 0; i < max_sessions; i++) 
        {
            if (sessions[i].id == -1) 
            {
                new_session_id = i;
            }
        }  
        if (i == max_sessions) 
        {
            printf("Error: no available sessions\n");
            continue;
        }

        // receive message from client to register session
        char buffer[1024];
        int bytes_read = read(client_pipefd, buffer, sizeof(buffer));

        // parse message to determine session type and pipe name
        int session_type = atoi(strtok(buffer, " "));
        char *pipe_name = strtok(NULL, " ");

        // open pipe for communication with client
        int session_pipefd = open(pipe_name, O_RDONLY);

        // store session information
        sessions[new_session_id].id = new_session_id;
        sessions[new_session_id].type = session_type;
        sessions[new_session_id].pipefd = session_pipefd;

        // create thread to handle session
        pthread_t session_thread;
        pthread_create(&session_thread, NULL, handle_session, (void *)new_session_id);
        }

    // cleanup and exit
    tfs_destroy();
    pthread_mutex_destroy(&mutex);
    close(register_pipefd);
    unlink(register_pipe_name);
    return 0;
}
