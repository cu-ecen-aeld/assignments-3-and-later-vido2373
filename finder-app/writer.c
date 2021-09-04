#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include <syslog.h>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Incorrect number arguments: 2 needed\n");
        return 1; //TODO: look at return codes, put the correct one
    }
    
    int file_fd;
    int bytes_written;

    char* filepath = strdup(argv[1]);
    char* pathname = dirname(filepath);

    openlog(NULL, 0, LOG_USER);

    if (access(pathname, F_OK) != 0) {
        if (mkdir(pathname, S_IRWXU | S_IRWXG | S_IROTH | S_IWOTH) == -1) {
            syslog(LOG_ERR, "Could not create path\n");
            free(filepath);
            return 1;
        }
    }
   
   if ((file_fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1) {
        syslog(LOG_ERR,"File did not open.\n");
        free(filepath);
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
    if ((bytes_written = write(file_fd, argv[2], strlen(argv[2]))) == -1) {
        syslog(LOG_ERR, "Write failed.\n");
        free(filepath);
        return 1;
    }

    close(file_fd);
    free(filepath);

    return 0;
}
