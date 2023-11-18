#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <syslog.h>

#define LOG_FILE "/var/tmp/aesdsocketdata"

FILE *g_logFileWritePtr = NULL;
FILE *g_logFileReadPtr = NULL;
int g_serverSocketFd = -1;
int g_clientSocketFd = -1;
struct addrinfo g_hints;
struct addrinfo *g_servInfo = NULL;
bool g_runAsDaemon = false;

void sigHandlerFunction(int signum) {
  // Clean up actions on sigint or sigterm
  if (g_logFileWritePtr)
    fclose(g_logFileWritePtr);

  if (g_logFileReadPtr)
    fclose(g_logFileReadPtr);

  unlink(LOG_FILE);

  if (g_serverSocketFd)
    close(g_serverSocketFd);

  if (g_clientSocketFd)
    close(g_clientSocketFd);

  if (g_servInfo)
    freeaddrinfo(g_servInfo);

  printf("Caught singal, exiting\n");
}

int main(int argc, char **argv) {
  bool incorrectUsage = false;

  // process args
  if (argc == 2) {
    if (strcmp(argv[1], "-d") || strcmp(argv[1], "-D")) {
      g_runAsDaemon = true;
    } else {
      incorrectUsage = true;
    }
  } else if (argc > 2) {
    incorrectUsage = true;
  }

  if (incorrectUsage) {
    printf("Usage: %s [-d or -D]\n", argv[0]);
    printf("\t -d or -D: run as a daemon");
    exit(EXIT_FAILURE);
  }

  // Register signal handler
  struct sigaction act = {0};
  act.sa_handler = sigHandlerFunction;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  if (sigaction(SIGTERM, &act, NULL) == -1) {
    perror("Error registering SIGTERM sigaction");
    exit(EXIT_FAILURE);
  }
  if (sigaction(SIGINT, &act, NULL) == -1) {
    perror("Error registering SIGINT sigaction");
    exit(EXIT_FAILURE);
  }

  // Setup socket
  memset(&g_hints, 0, sizeof(g_hints));
  g_hints.ai_family = AF_UNSPEC;
  g_hints.ai_socktype = SOCK_STREAM;
  g_hints.ai_flags = AI_PASSIVE;

  // get address to local host port 9000.
  int status;
  if ((status = getaddrinfo(NULL, "9000", &g_hints, &g_servInfo)) != 0) {
    perror("Error with getaddro ");
    exit(EXIT_FAILURE);
  }

  // create socket and bind to it.
  g_serverSocketFd = socket(g_servInfo->ai_family, g_servInfo->ai_socktype,
                            g_servInfo->ai_protocol);
  if (g_serverSocketFd < 0) {
    perror("Error creating socket");
    exit(EXIT_FAILURE);
  }

  // setup socket reuse option
  const int enable = 1;
  if (setsockopt(g_serverSocketFd, SOL_SOCKET, SO_REUSEADDR, &enable,
                 sizeof(int)) < 0) {
    perror("setsockopt(SO_REUSEADDR) failed");
    exit(EXIT_FAILURE);
  }

  // Techinically we need loop thorugh the linked list, but we expect only one
  // address so using ony once here to bind to socket
  if (bind(g_serverSocketFd, g_servInfo->ai_addr, g_servInfo->ai_addrlen) !=
      0) {
    perror("Error binding to socket");
    exit(EXIT_FAILURE);
  }

  if (g_servInfo)
  {
    freeaddrinfo(g_servInfo);
    g_servInfo = NULL;
  }
  
  int pid;

  if(g_runAsDaemon){
    //fork 
    pid = fork();
    if(pid == -1)
    {
      perror("Error forking the process");
      exit(EXIT_FAILURE);
    }
    //kill the parent
    if(pid > 0)
    {
      printf("Running as a daemon!");
      exit(EXIT_SUCCESS);
    }
    //Let the child run as daemon
    if(pid == 0)
    {
      printf("Running as a daemon\n");
      //move on to listening and accepting 
      //connections
    }
  }

  // listen
  if (listen(g_serverSocketFd, 1)) {
    perror("Error listening to socket");
    exit(EXIT_FAILURE);
  }

  unlink(LOG_FILE);

  // recv
  for (;;) {
    // accept connectiono
    struct sockaddr clientAddress;
    unsigned int clientAddrLen;
    clientAddrLen = sizeof(clientAddress);
    g_clientSocketFd = accept(g_serverSocketFd, &clientAddress, &clientAddrLen);
    if (g_clientSocketFd < 0) {
      perror("Error accepting the client socket");
      exit(EXIT_FAILURE);
    }

#define NAME_LEN 50
#define SERVICE_LEN 50
    char clientName[NAME_LEN], clientService[SERVICE_LEN];
    status = getnameinfo(&clientAddress, clientAddrLen, clientName, NAME_LEN,
                         clientService, SERVICE_LEN,
                         NI_NUMERICHOST | NI_NUMERICSERV);
    if (status != 0) {
      perror("Error getting client address name");
      exit(EXIT_FAILURE);
    }

    printf("Accepted connection from %s - service %s\n", clientName,
           clientService);

    // Open Log file
    g_logFileWritePtr = fopen(LOG_FILE, "a");
    if (!g_logFileWritePtr) {
      perror("Error opening log file");
      exit(EXIT_FAILURE);
    }

// recv
#define BUF_SIZE 1024
    char buf[BUF_SIZE];
    int rcvd = -1;
    while ((rcvd = recv(g_clientSocketFd, (void *)buf, sizeof(buf), 0)) > 0) {
      fwrite(buf, sizeof(char), rcvd, g_logFileWritePtr);
      fflush(g_logFileWritePtr);
      // finished with recieving if end of packet
      if (buf[rcvd - 1] == '\n')
        break;
    }
    fclose(g_logFileWritePtr);
    g_logFileWritePtr = NULL;
    // printf("DONE RECEIVING\n");

    // send
    char *readLine = NULL;
    size_t readLen = 0;
    ssize_t numread;
    g_logFileReadPtr = fopen(LOG_FILE, "r");
    if (!g_logFileReadPtr) {
      perror("Error opening file for reading");
      exit(EXIT_FAILURE);
    }
    while ((numread = getline(&readLine, &readLen, g_logFileReadPtr)) != -1) {
      // printf("num read = %d readLen = %d\n", (int)numread, (int)readLen);
      // printf("--->%s<---\n", readLine);
      status = send(g_clientSocketFd, readLine, numread, 0);
      if (status < 0) {
        perror("Error sending data to client");
        exit(EXIT_FAILURE);
      }
    }
    fclose(g_logFileReadPtr);
    g_logFileReadPtr = NULL;
    if (readLine) {
      free(readLine);
    }

    shutdown(g_clientSocketFd, SHUT_RDWR);
    close(g_clientSocketFd);
    g_clientSocketFd = -1;

    printf("Closing connection from %s\n", clientName);
  }

  return EXIT_SUCCESS;
}