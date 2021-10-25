#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <syslog.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <sys/queue.h>

#define USE_AESD_CHAR_DEVICE 1

#define PORT_NUM (9000)
#ifdef USE_AESD_CHAR_DEVICE
#define OUTPUT_FILE "/dev/aesdchar"
#else
#define OUTPUT_FILE "/var/tmp/aesdsocketdata"
#endif
#define MAX_CONNECTIONS (10)
#define BASE_BUFFER_SIZE (100)


typedef struct thread_data{
    /*
     * TODO: add other values your thread will need to manage
     * into this structure, use this structure to communicate
     * between the start_thread_obtaining_mutex function and
     * your thread implementation.
     */
    pthread_t tid;
    int sock_fd;
    int client_fd;
    int outfile_fd;
    pthread_mutex_t* mutex;
    char* writeout_buff;
    int writeout_buff_size;
    char* readout_buff;
    int readout_buff_size;
    sigset_t sig_mask;
    /**
     * Set to true if the thread completed with success, false
     * if an error occurred.
     */
    int success;
} thread_data_t;


typedef struct snode_s snode_t;
struct snode_s {
    thread_data_t t_data;
    LIST_ENTRY(snode_s) entries;
};



int sock_fd;
int continue_program = 1;
pthread_mutex_t mutex;
sigset_t sig_mask;


void* ReceiveAndSendPackets(void* thread_data);


void sig_handler(int signo) {
    if ((signo == SIGINT) || (signo == SIGTERM)) {
        syslog(LOG_INFO, "Caught signal, exiting");
        if (shutdown(sock_fd, SHUT_RDWR)) {
            syslog(LOG_ERR, "shutdown");
        }
        
        continue_program = 0;
    }
}


void* ReceiveAndSendPackets(void* thread_data) {
    int writeout_buff_index = 0;
    int bytes_recvd = 0, bytes_written_to_file = 0;
    int full_packet_received = 0;
    int readout_buff_index = 0;
    int read_status = 0, send_status = 0;
    thread_data_t* t  = (thread_data_t *)thread_data;

    while (1) {
        bytes_recvd = recv(t->client_fd, &(t->writeout_buff[writeout_buff_index]), 1, 0);
        if (bytes_recvd == -1) {
            perror("recv");
            syslog(LOG_ERR, "recv");
            close(t->outfile_fd);
            close(t->client_fd);
            close(t->sock_fd);
            free(t->writeout_buff);
            free(t->readout_buff);
            exit(EXIT_FAILURE);
        }
        if (bytes_recvd == 0) {
            break;
        }

        if (t->writeout_buff[writeout_buff_index] == '\n') {
            //write out packet
            if (pthread_mutex_lock(t->mutex) != 0) {
                perror("pthread_mutex_lock");
                syslog(LOG_ERR, "pthread_mutex_lock");
                close(t->outfile_fd);
                close(t->client_fd);
                close(t->sock_fd);
                free(t->writeout_buff);
                free(t->readout_buff);
                exit(EXIT_FAILURE);
            }
            bytes_written_to_file = write(t->outfile_fd, &(t->writeout_buff[0]), writeout_buff_index + 1);

            if (pthread_mutex_unlock(t->mutex) != 0) {
                perror("pthread_mutex_lock");
                syslog(LOG_ERR, "pthread_mutex_lock");
                close(t->outfile_fd);
                close(t->client_fd);
                close(t->sock_fd);
                free(t->writeout_buff);
                free(t->readout_buff);
                exit(EXIT_FAILURE);
            }

            if (bytes_written_to_file == -1) {
                syslog(LOG_ERR, "sigprocmask");
                close(t->outfile_fd);
                close(t->client_fd);
                close(t->sock_fd);
                free(t->writeout_buff);
                free(t->readout_buff);
                exit(EXIT_FAILURE);
            }

            writeout_buff_index = 0;
            full_packet_received = 1;
        }
        else {
            writeout_buff_index++;
        }

        if (writeout_buff_index == t->writeout_buff_size) {
            t->writeout_buff_size *= 2;
            t->writeout_buff = (char *)realloc(t->writeout_buff, sizeof(char) * t->writeout_buff_size);
            if (t->writeout_buff == NULL) {
                perror("realloc");
                syslog(LOG_ERR, "realloc");
                close(t->outfile_fd);
                close(t->client_fd);
                close(t->sock_fd);
                free(t->writeout_buff);
                free(t->readout_buff);
                exit(EXIT_FAILURE);
            }
        }

        if (sigprocmask(SIG_BLOCK, &sig_mask, NULL)) {
            syslog(LOG_ERR, "sigprocmask");
            close(t->outfile_fd);
            close(t->client_fd);
            close(t->sock_fd);
            free(t->writeout_buff);
            free(t->readout_buff);
            exit(EXIT_FAILURE);
        }

        if (full_packet_received) {
            //Send back out full file

            if (pthread_mutex_lock(t->mutex) != 0) {
                perror("pthread_mutex_lock");
                syslog(LOG_ERR, "pthread_mutex_lock");
                close(t->outfile_fd);
                close(t->client_fd);
                close(t->sock_fd);
                free(t->writeout_buff);
                free(t->readout_buff);
                exit(EXIT_FAILURE);
            }
#ifndef USE_AESD_CHAR_DEVICE
            lseek(t->outfile_fd, 0, SEEK_SET);
#endif
            read_status = read(t->outfile_fd, &(t->readout_buff[readout_buff_index]), 1);
            if (read_status == -1) {
                perror("read");
                syslog(LOG_ERR, "read");
                close(t->outfile_fd);
                close(t->client_fd);
                close(t->sock_fd);
                free(t->writeout_buff);
                free(t->readout_buff);
                exit(EXIT_FAILURE);
            }


            while (read_status != 0) {
                if (t->readout_buff[readout_buff_index] == '\n') {
                    send_status = send(t->client_fd, &(t->readout_buff[0]), readout_buff_index + 1, 0);
                    if (send_status == -1) {
                        perror("send");
                        syslog(LOG_ERR, "send");
                        close(t->outfile_fd);
                        close(t->client_fd);
                        close(t->sock_fd);
                        free(t->writeout_buff);
                        free(t->readout_buff);
                        exit(EXIT_FAILURE);
                    }

                    readout_buff_index = 0;
                }
                else {
                    readout_buff_index++;
                }

                if (readout_buff_index == t->readout_buff_size) {
                    t->readout_buff_size *= 2;
                    t->readout_buff = (char *)realloc(t->readout_buff, sizeof(char) * t->readout_buff_size);
                    if (t->readout_buff == NULL) {
                        perror("realloc");
                        syslog(LOG_ERR, "realloc");
                        close(t->outfile_fd);
                        close(t->client_fd);
                        close(t->sock_fd);
                        free(t->writeout_buff);
                        free(t->readout_buff);
                        exit(EXIT_FAILURE);
                    }
                }

                read_status = read(t->outfile_fd, &(t->readout_buff[readout_buff_index]), 1);
                if (read_status == -1) {
                    perror("read");
                    syslog(LOG_ERR, "read");
                    close(t->outfile_fd);
                    close(t->client_fd);
                    close(t->sock_fd);
                    free(t->writeout_buff);
                    free(t->readout_buff);
                    exit(EXIT_FAILURE);
                }

            }
#ifndef USE_AESD_CHAR_DEVICE
            lseek(t->outfile_fd, 0, SEEK_END);
#endif
            if (pthread_mutex_unlock(t->mutex) != 0) {
                perror("pthread_mutex_lock");
                syslog(LOG_ERR, "pthread_mutex_lock");
                close(t->outfile_fd);
                close(t->client_fd);
                close(t->sock_fd);
                free(t->writeout_buff);
                free(t->readout_buff);
                exit(EXIT_FAILURE);
            }
        }
        
        if (sigprocmask(SIG_UNBLOCK, &sig_mask, NULL)) {
            syslog(LOG_ERR, "sigprocmask");
            close(t->outfile_fd);
            close(t->client_fd);
            close(t->sock_fd);
            free(t->writeout_buff);
            free(t->readout_buff);
            exit(EXIT_FAILURE);
        }
        
    }
    t->success = 1;
    return thread_data;
}


int main(int argc, char** argv) {
    int client_fd;
    int outfile_fd;
    struct addrinfo hints;
    struct addrinfo* servinfo;
    struct sockaddr peer_addr;
    int reuse_addr = 1;
    socklen_t peer_addr_len = sizeof(peer_addr);
    char* ip_addr_str;

    int is_daemon = 0;
    int dev_null_fd = 0;
    pid_t pid = 0;

    snode_t* node = NULL;


    openlog(NULL, 0, LOG_USER);

    // Set up signal handling
    if (signal(SIGINT, sig_handler) == SIG_ERR) {
        syslog(LOG_ERR, "signal");
        exit(EXIT_FAILURE);
    }

    if (signal(SIGTERM, sig_handler) == SIG_ERR) {
        syslog(LOG_ERR, "signal");
        exit(EXIT_FAILURE);
    }

    if (sigemptyset(&sig_mask) == -1) {
        syslog(LOG_ERR, "sigemptyset");
        exit(EXIT_FAILURE);
    }

    if (sigaddset(&sig_mask, SIGTERM) == -1) {
        syslog(LOG_ERR, "sigadd");
        exit(EXIT_FAILURE);
    }

    if (sigaddset(&sig_mask, SIGINT) == -1) {
        syslog(LOG_ERR, "sigadd");
        exit(EXIT_FAILURE);
    }

    // Get socket info
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    if ((getaddrinfo(NULL, "9000", &hints, &servinfo)) != 0) {
        syslog(LOG_ERR, "getaddrinfo");
        return -1;
    }

    //Create and bind socket
    sock_fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (sock_fd == -1) {
        syslog(LOG_ERR, "socket");
        return -1;
    }

    // Reuse address
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(int)) == -1) {
        syslog(LOG_ERR, "setsockopt");
        close(sock_fd);
        return -1;
    }

    if (bind(sock_fd, servinfo->ai_addr, servinfo->ai_addrlen) != 0) {
        syslog(LOG_ERR, "bind");
        close(sock_fd);
        return -1;
    }


    freeaddrinfo(servinfo);

    // Set up daemon if needed
    if (argc == 2) {
        if (strlen(argv[1]) == 2) {
            if (!strncmp("-d", argv[1], 2)) {
                is_daemon = 1;
            }
        }
    }

    if (is_daemon) {
        pid = fork();
        if (pid == -1) {
            perror("fork");
            syslog(LOG_ERR, "fork");
            close(sock_fd);
            exit(EXIT_FAILURE);
        }
        else if (pid == 0) {
            if (setsid() == -1) {
                perror("setsid");
                syslog(LOG_ERR, "setsid");
                close(sock_fd);
                exit(EXIT_FAILURE);
            }

            if (chdir("/") == -1) {
                perror("chdir");
                syslog(LOG_ERR, "chdir");
                close(sock_fd);
                exit(EXIT_FAILURE);
            }

            dev_null_fd = open("/dev/null", O_RDWR);
            dup2(dev_null_fd, STDIN_FILENO);
            dup2(dev_null_fd, STDOUT_FILENO);
            dup2(dev_null_fd, STDERR_FILENO);

            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
        }
        // Close parent
        else {
            exit(EXIT_SUCCESS);
        }
    }

    if (listen(sock_fd, MAX_CONNECTIONS) == -1) {
        syslog(LOG_ERR, "listen");
        close(sock_fd);
        return -1;
    }

    outfile_fd = open(OUTPUT_FILE, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (outfile_fd == -1) {
        perror("open");
        syslog(LOG_ERR, "Could not open %s", OUTPUT_FILE);
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    LIST_HEAD(slisthead, snode_s) head;
    LIST_INIT(&head);

    if (pthread_mutex_init(&mutex, NULL) != 0) {
        perror("pthread_mutex_init");
        syslog(LOG_ERR, "pthread_mutex_init");
        close(sock_fd);
        close(outfile_fd);
        exit(EXIT_FAILURE);
    }

    while (continue_program) {
        client_fd = accept(sock_fd, &peer_addr, &peer_addr_len);
        if (client_fd == -1) {
            if (errno == EINVAL) { //socket was shut down in signal handler
                break;
            }
            perror("accept");
            syslog(LOG_ERR, "accept");
            close(sock_fd);
            close(outfile_fd);
            return -1;
        }
        
        ip_addr_str = inet_ntoa(((struct sockaddr_in *)&peer_addr)->sin_addr);
        syslog(LOG_INFO, "Accepted connection from %s", ip_addr_str);

        node = (snode_t *)malloc(sizeof(snode_t));
        if (node == NULL) {
            perror("malloc");
            syslog(LOG_ERR, "malloc");
            close(client_fd);
            close(sock_fd);
            close(outfile_fd);
            exit(EXIT_FAILURE);
        }

        node->t_data.client_fd = client_fd;
        node->t_data.sock_fd = sock_fd;
        node->t_data.outfile_fd = outfile_fd;
        node->t_data.mutex = &mutex;
        node->t_data.success = 0;
        node->t_data.writeout_buff_size = BASE_BUFFER_SIZE;
        node->t_data.writeout_buff = malloc(sizeof(char) * BASE_BUFFER_SIZE);
        if (node->t_data.writeout_buff == NULL) {
            perror("malloc");
            syslog(LOG_ERR, "malloc");
            close(client_fd);
            close(sock_fd);
            close(outfile_fd);
            exit(EXIT_FAILURE);
        }
        node->t_data.readout_buff_size = BASE_BUFFER_SIZE;
        node->t_data.readout_buff = malloc(sizeof(char) * BASE_BUFFER_SIZE);
        if (node->t_data.readout_buff == NULL) {
            perror("malloc");
            syslog(LOG_ERR, "malloc");
            close(client_fd);
            close(sock_fd);
            close(outfile_fd);
            exit(EXIT_FAILURE);
        }

        LIST_INSERT_HEAD(&head, node, entries);
        
        if (pthread_create(&(node->t_data.tid), NULL, ReceiveAndSendPackets, &(node->t_data)) == -1) {
            perror("pthread_create");
            syslog(LOG_ERR, "pthread_create");
            close(client_fd);
            close(sock_fd);
            close(outfile_fd);
            exit(EXIT_FAILURE);
        }

        snode_t* prev = node;
        LIST_FOREACH(node, &head, entries) {
            if (node->t_data.success) {
                if (close(node->t_data.client_fd) == -1) {
                    perror("close");
                    syslog(LOG_ERR, "close");
                    close(outfile_fd);
                    close(sock_fd);
                    exit(EXIT_FAILURE);
                }
                free(node->t_data.writeout_buff);
                free(node->t_data.readout_buff);

                if (pthread_join(node->t_data.tid, NULL) != 0) {
                    perror("pthread_join");
                    syslog(LOG_ERR, "pthread_join");
                    close(outfile_fd);
                    close(sock_fd);
                    exit(EXIT_FAILURE);
                }
                LIST_REMOVE(node, entries);
                free(node);
                node = prev;
            }
            prev = node;
        }


        syslog(LOG_INFO, "Closing connection from %s", ip_addr_str);
    }

    while(!LIST_EMPTY(&head)) {
        node = LIST_FIRST(&head);
        if (!node->t_data.success) {
            if (pthread_cancel(node->t_data.tid) != 0) {
                perror("pthread_cancel");
                syslog(LOG_ERR, "pthread_cancel");
                close(outfile_fd);
                close(sock_fd);
                exit(EXIT_FAILURE);
            }
        }

        close(node->t_data.client_fd);
        free(node->t_data.writeout_buff);
        free(node->t_data.readout_buff);

        if (pthread_join(node->t_data.tid, NULL) != 0) {
            perror("pthread_join");
            syslog(LOG_ERR, "pthread_join");
            close(outfile_fd);
            close(sock_fd);
            exit(EXIT_FAILURE);
        }
        LIST_REMOVE(node, entries);
        free(node);
    }

    if (pthread_mutex_destroy(&mutex) != 0) {
        perror("pthread_mutex_destroy");
        syslog(LOG_ERR, "pthread_mutex_destroy");
        close(sock_fd);
        close(outfile_fd);
        exit(EXIT_FAILURE);
    }
    

    if (close(sock_fd) == -1) {
        perror("close");
        syslog(LOG_ERR, "close");
        exit(EXIT_FAILURE);
    }

    if (close(outfile_fd) == -1) {
        perror("close");
        syslog(LOG_ERR, "close");
        exit(EXIT_FAILURE); 

    }
#ifndef USE_AESD_CHAR_DEVICE
    if (remove(OUTPUT_FILE) == -1) {
        perror("remove");
        syslog(LOG_ERR, "remove");
        exit(EXIT_FAILURE);
    }
#endif
    closelog();

    return 0;
}
