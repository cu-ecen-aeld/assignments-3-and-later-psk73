#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>  //creat
#include <stdio.h> //printf
#include<errno.h>  //errno
#include<string.h> //strlen,strerror
#include<unistd.h> //write
#include<syslog.h> //openlog,syslog

int main(int argc, char **argv)
{
    int fd;
    if(argc != 3)
    {
        printf("Usage: %s <path-of-file-to-write-to> <text-string-to-write>\n",argv[0]);
        return 1;
    }
    char *fileName   = argv[1];
    char *strToWrite = argv[2];

    printf("Writing %s to %s\n",strToWrite,fileName);

    //Open syslog LOG_USER facility
    //Option LOG_PERROR used so as to print to stderr also
    openlog("assign2.log",LOG_PERROR|LOG_PID, LOG_USER);

    //Open the file and truncate, 
    fd = open(fileName, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if(fd<0)
    {
        syslog(LOG_DEBUG, "Error opening %s:  (%d: %s)",
            fileName, errno,strerror(errno));
        return 1;
    }

    //Write the string to file
    ssize_t nr;
    nr = write(fd, strToWrite, strlen(strToWrite));
    if(nr<0)
    {
        syslog(LOG_DEBUG,"Error writing  %s:  (%d: %s)",
            strToWrite, errno,strerror(errno));
        return 1;
    }

    //Close the file and return
    close(fd);
    //close sys log facility
    closelog();

    return 0;
}
