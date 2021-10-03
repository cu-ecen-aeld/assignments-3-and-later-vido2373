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


#define PORT_NUM (9000)
#define OUTPUT_FILE "/var/tmp/aesdsocketdata"
#define MAX_CONNECTIONS (10)
#define RECV_SIZE (100)
#define FILE_READOUT_CHUNK (100)


int sock_fd;
int client_fd;
int outfile_fd;
char* outfile_buff;
sigset_t sig_mask;


void sig_handler(int signo) {
    if ((signo == SIGINT) || (signo == SIGTERM)) {
        syslog(LOG_INFO, "Caught signal, exiting");
        close(outfile_fd);
        close(sock_fd);
        free(outfile_buff);

        if (remove(OUTPUT_FILE) == -1) {
            perror("remove");
            exit(EXIT_FAILURE);
        }

        closelog();
        exit(EXIT_SUCCESS);
    }
}


int main(int argc, char* argv[]) {
    struct addrinfo hints;
    struct addrinfo* servinfo;
    struct sockaddr peer_addr;
    int reuse_addr = 1;
    socklen_t peer_addr_len;
    char recv_buff[RECV_SIZE + 1];
    char* ip_addr_str;
    char* newline_ptr;
    int outfile_buff_size = RECV_SIZE;
    int outfile_size = 0;
    int outfile_buff_index = 0, recv_buff_index = 0;
    int bytes_recvd = 0, bytes_to_write = 0;
    char file_readout_buff[FILE_READOUT_CHUNK];
    int bytes_written_to_file = 0, bytes_read_from_file = 0, bytes_written_to_client = 0;
    int readout_read_chunk = 0, readout_write_chunk = 0;
    int newline_found = 0;

    int is_daemon = 0;
    int dev_null_fd = 0;
    pid_t pid;

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
    printf("is_daemon: %d\n", is_daemon);

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
        syslog(LOG_ERR, "open");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    outfile_buff = (char *)malloc(sizeof(char) * outfile_buff_size);
    if (outfile_buff == NULL) {
        perror("malloc");
        syslog(LOG_ERR, "malloc");
        close(outfile_fd);
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    while (1) {
        client_fd = accept(sock_fd, &peer_addr, &peer_addr_len);
        if (client_fd == -1) {
            perror("accept");
            syslog(LOG_ERR, "accept");
            return -1;
        }

        if (sigprocmask(SIG_BLOCK, &sig_mask, NULL)) {
            syslog(LOG_ERR, "sigprocmask");
            exit(EXIT_FAILURE);
        }
        
        ip_addr_str = inet_ntoa(((struct sockaddr_in *)&peer_addr)->sin_addr);
        syslog(LOG_INFO, "Accepted connection from %s", ip_addr_str);

        outfile_buff_index = 0;
        recv_buff_index = 0;

        while (1) {
            bytes_recvd = recv(client_fd, &recv_buff[recv_buff_index], RECV_SIZE, 0);
            if (bytes_recvd == 0) {
                break;
            }

            recv_buff[bytes_recvd] = '\0';
            bytes_to_write = bytes_recvd;

            newline_ptr = strchr(recv_buff, '\n');
            if (newline_ptr != NULL) {
                bytes_to_write = (int)(newline_ptr - (recv_buff + recv_buff_index)) + 1; //make sure newline is added
                newline_found = 1;
            }

            if (outfile_buff_index + bytes_to_write > outfile_buff_size) {
                outfile_buff_size *= 2;
                outfile_buff = (char *)realloc(outfile_buff, sizeof(char) * outfile_buff_size);
                if (outfile_buff == NULL) {
                    perror("realloc");
                    syslog(LOG_ERR, "realloc");
                    close(client_fd);
                    close(sock_fd);
                    close(outfile_fd);
                    exit(EXIT_FAILURE);
                }
            }
            strncpy(&outfile_buff[outfile_buff_index], &recv_buff[recv_buff_index], bytes_to_write);
            outfile_buff_index += bytes_to_write;

            if (newline_found) {
                //Write out packet
                bytes_written_to_file = write(outfile_fd, &outfile_buff[0], outfile_buff_index);
                if (bytes_written_to_file == -1) {
                    perror("write");
                    syslog(LOG_ERR, "write");
                    close(outfile_fd);
                    close(client_fd);
                    close(sock_fd);
                    free(outfile_buff);
                    exit(EXIT_FAILURE);
                }

                outfile_size += bytes_written_to_file;
                //Send full file contents
                lseek(outfile_fd, 0, SEEK_SET);
                
                while (bytes_written_to_client < outfile_size) {
                    if (bytes_written_to_client + FILE_READOUT_CHUNK > outfile_size) {
                        readout_write_chunk = outfile_size - bytes_read_from_file;
                    }
                    else {
                        readout_write_chunk = FILE_READOUT_CHUNK;
                    }

                    readout_read_chunk = read(outfile_fd, &file_readout_buff[0], readout_write_chunk);
                    if (bytes_recvd == -1) {
                        perror("read");
                        syslog(LOG_ERR, "read");
                        free(outfile_buff);
                        close(outfile_fd);
                        close(client_fd);
                        close(sock_fd);
                        exit(EXIT_FAILURE);
                    }
                    bytes_read_from_file += readout_read_chunk;

                    bytes_to_write = send(client_fd, &file_readout_buff[0], readout_read_chunk, 0);
                    if (bytes_to_write == -1) {
                        perror("send");
                        syslog(LOG_ERR, "send");
                        free(outfile_buff);
                        close(outfile_fd);
                        close(client_fd);
                        close(sock_fd);
                        exit(EXIT_FAILURE); 
                    }

                    bytes_written_to_client += readout_write_chunk;
                }
                bytes_written_to_client = 0;
                bytes_read_from_file = 0;
                readout_read_chunk = 0;
                readout_write_chunk = 0;

                lseek(outfile_fd, 0, SEEK_END);
            }
            
            recv_buff_index = 0;
            newline_found = 0;
        }

        if (close(client_fd) == -1) {
            perror("close");
            syslog(LOG_ERR, "close");
            free(outfile_buff);
            close(sock_fd);
            close(outfile_fd);
            exit(EXIT_FAILURE); 
        }

        syslog(LOG_INFO, "Closing connection from %s", ip_addr_str);

        if (sigprocmask(SIG_UNBLOCK, &sig_mask, NULL)) {
            syslog(LOG_ERR, "sigprocmask");
            exit(EXIT_FAILURE);
        }
    }

    free(outfile_buff);
    if (close(outfile_fd) == -1) {
        perror("close");
        syslog(LOG_ERR, "close");
        exit(EXIT_FAILURE); 

    }
    closelog();

    return 0;
}
