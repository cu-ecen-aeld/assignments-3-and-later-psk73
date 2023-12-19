#include "queue.h"
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define LOG_FILE "/var/tmp/aesdsocketdata"
#define NAME_LEN 50
#define SERVICE_LEN 50
#define BUF_SIZE 1024
#define TIMERSIG SIGUSR1

int g_logFileFd = -1;
int g_serverSocketFd = -1;
int g_clientSocketFd = -1;
struct addrinfo g_hints;
struct addrinfo *g_servInfo = NULL;
static bool g_runAsDaemon = false;
static timer_t g_timerid;
static bool g_timerCreated = false;
static int g_threadCount = 0;
static int g_threadId = 0;
pthread_mutex_t g_mutex;
static bool g_mutexInitialized = false;

struct thread_data {
  int threadId;
  pthread_mutex_t *mutex;
  int logFileFd;
  int clientSocketFd;
  char clientName[NAME_LEN];
};

// SLIST.
struct slist_data_s {
  pthread_t tid;
  struct thread_data tparms;
  SLIST_ENTRY(slist_data_s) entries;
};
typedef struct slist_data_s slist_data_t;
slist_data_t *g_datap = NULL;
SLIST_HEAD(slisthead, slist_data_s) g_head;

//Called from signal handler or as cleanup if signum==-1
void sigHandlerFunction(int signum) {
  int rc;
  if (signum > 0) {
    printf("SigHandler: Caught signal %d - %s\n", signum, strsignal(signum));
  }
  // Clean up actions on sigint or sigterm
  if (g_threadCount > 0) {
    while (!SLIST_EMPTY(&g_head)) {
      printf("SigHandler: client thread params being freed\n");
      g_datap = SLIST_FIRST(&g_head);
      if (g_datap->tparms.clientSocketFd >= 0) {
        rc = pthread_join(g_datap->tid, NULL);
        if (rc < 0) {
          printf("Error joinining thread");
          exit(EXIT_FAILURE);
        }
        g_threadCount--;
        printf("SigHandler: Thread %lu is done. \n", g_datap->tid);
      }
      if (g_datap->tparms.clientSocketFd != -1) {
        printf("SigHandler: closed clientsocketfd\n");
        close(g_datap->tparms.clientSocketFd);
      }
      printf("SigHandler: removed thread from linkedlist\n");
      SLIST_REMOVE_HEAD(&g_head, entries);
      printf("SigHandler: freed linked list mem for this thread\n");
      free(g_datap);
    }
  }
  if (g_logFileFd) {
    close(g_logFileFd);
    printf("SigHandler: closed logfile\n");
  }
  unlink(LOG_FILE);
  printf("SigHandler: Deleted the file\n");
  pthread_mutex_destroy(&g_mutex);
  if (g_timerCreated) {
    timer_delete(g_timerid);
    printf("SigHandler: timer deleted\n");
  }
  if (g_serverSocketFd) {
    close(g_serverSocketFd);
    printf("SigHandler: server socket closed\n");
  }
  if (g_servInfo) {
    freeaddrinfo(g_servInfo);
    printf("SigHandler: serv info freed\n");
  }
  printf("SigHandler: Good Bye!\n\n\n");
  exit(EXIT_FAILURE);
}

static void timerHandlerFunction(int signum) {
  time_t t;
  struct tm *tmp;
  char prefix[] = "timestamp:";
  char postfix[] = "\n";
  char outstr[100];
  int rc;

  printf("TimerHandler: mutex initialized= %d\n", g_mutexInitialized);
  if (!g_mutexInitialized || g_logFileFd == -1)
    return;

  t = time(NULL);
  tmp = localtime(&t);
  if (tmp == NULL) {
    printf("Error getting localtime");
    exit(EXIT_FAILURE);
  }
  if (strftime(outstr, sizeof(outstr), "%a, %d %b %Y %T %z", tmp) == 0) {
    printf("Error strftime error");
    exit(EXIT_FAILURE);
  }

  printf("TimerHandler: obtained mutex lock\n");
  rc = pthread_mutex_lock(&g_mutex);
  if (rc != 0) {
    printf("Error timer task mutex error");
    exit(EXIT_FAILURE);
  }
  // seek to end of the Log file , write time stamp and close
  lseek(g_logFileFd, 0, SEEK_END);
  write(g_logFileFd, prefix, strlen(prefix));
  write(g_logFileFd, outstr, strlen(outstr));
  write(g_logFileFd, postfix, strlen(postfix));
  printf("TimerHandler: wrote time stamp to file\n");

  rc = pthread_mutex_unlock(&g_mutex);
  if (rc != 0) {
    printf("Error mutex unlock");
    exit(EXIT_FAILURE);
  }
  printf("TimerHandler: released mutex lock\n");
  return;
}

static void timerHandlerThreadFunction(union sigval sigev_value) {
  return timerHandlerFunction(34);
}

void *sendAndReceiveThread(void *thread_param) {
  struct thread_data *pArgs = (struct thread_data *)thread_param;
  int rc;
  char buf[BUF_SIZE];
  int rcvd = -1;

  printf("Thread ID %d obtaining mutex lock\n", pArgs->threadId);
  rc = pthread_mutex_lock(pArgs->mutex);
  if (rc != 0) {
    // lock failure
    printf("Thread ID %d Failed to obtain mutex lock, code %d\n",
           pArgs->threadId, rc);
    exit(EXIT_FAILURE);
  }
  // lock success
  // setup
  printf("Thread ID %d  set file location to end for writing \n",
         pArgs->threadId);
  lseek(pArgs->logFileFd, 0, SEEK_END);

  // recv
  printf("Thread ID %d starting to receive\n", pArgs->threadId);
  while ((rcvd = recv(pArgs->clientSocketFd, (void *)buf, sizeof(buf), 0)) >
         0) {
    printf("\tnum rcvd = %d \n", (int)rcvd);
    rc = write(pArgs->logFileFd, buf, rcvd);
    if(rc != rcvd)
    {
      printf("ERROR writing to file fd=%d rc=%d\n",pArgs->logFileFd,rc);
      exit(EXIT_FAILURE);
    }
    // finished with recieving if end of packet
    if (buf[rcvd - 1] == '\n')
      break;
  }

  printf("Thread ID %d  reset file location for reading \n", pArgs->threadId);
  lseek(pArgs->logFileFd, 0, SEEK_SET);

  char readBuf[1024];
  ssize_t readLen;

  // send
  printf("Thread ID %d  sending to client\n", pArgs->threadId);
  while ((readLen = read(pArgs->logFileFd, readBuf, 1024)) > 0) {
    printf("\tnum read = %d \n", (int)readLen);
    /*
    int i;
    printf("--\n")
    for(i=0;i<readLen;i++){putc(readBuf[i]);}
    printf("--\n")
    */
    rc = send(pArgs->clientSocketFd, readBuf, readLen, 0);
    if (rc < 0) {
      printf("Error sending data to client");
      exit(EXIT_FAILURE);
    }
  }
  // cleanup
  // Unlock
  rc = pthread_mutex_unlock(pArgs->mutex);
  if (rc != 0) {
    printf("Thread ID %d Failed to release mutex lock, code %d\n",
           pArgs->threadId, rc);
    exit(EXIT_FAILURE);
  }
  printf("Thread ID %d released mutex lock\n", pArgs->threadId);
  // shutdown socket
  printf("Thread ID %d  closing the connection to %s\n", pArgs->threadId,
         pArgs->clientName);
  shutdown(pArgs->clientSocketFd, SHUT_RDWR);
  close(pArgs->clientSocketFd);
  pArgs->clientSocketFd = -1;

  return thread_param;
}

int main(int argc, char **argv) {
  bool incorrectUsage = false;
  int rc;
  struct sockaddr clientAddress;
  unsigned int clientAddrLen;
  char clientName[NAME_LEN], clientService[SERVICE_LEN];
  pthread_t tid;

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
    printf("\t -d or -D: run as a daemon\n");
    exit(EXIT_FAILURE);
  }

  // run as daemon
  if (g_runAsDaemon) {
    // fork
    int pid;
    pid = fork();
    if (pid == -1) {
      printf("Error forking the process");
      exit(EXIT_FAILURE);
    }
    // kill the parent
    if (pid > 0) {
      printf("Parent exiting as running in daemon mode!\n");
      exit(EXIT_SUCCESS);
    }
    // Let the child run as daemon
    if (pid == 0) {
      printf("Running as a daemon\n");
      // move on to listening and accepting
      // connections
    }
  }

  // Register signal handler
  // register handlers for SIGTERM and SIGINT
  printf("Main: Registering handlers for SIGTERM and SIGINT\n");
  struct sigaction act = {0};
  act.sa_handler = sigHandlerFunction;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  if (sigaction(SIGTERM, &act, NULL) == -1) {
    printf("Error registering SIGTERM sigaction");
    exit(EXIT_FAILURE);
  }
  if (sigaction(SIGINT, &act, NULL) == -1) {
    printf("Error registering SIGINT sigaction");
    exit(EXIT_FAILURE);
  }

  // register timer thread for SIGUSR1
  printf("Main: Registering handlers for SIGUSR1\n");
  struct sigaction timeract = {0};
  timeract.sa_handler = timerHandlerFunction;
  sigemptyset(&timeract.sa_mask);
  timeract.sa_flags = SA_SIGINFO;
  if (sigaction(TIMERSIG, &timeract, NULL) == -1) {
    printf("Error registering timer sigaction");
    exit(EXIT_FAILURE);
  }

  // Setup socket
  memset(&g_hints, 0, sizeof(g_hints));
  g_hints.ai_family = AF_UNSPEC;
  g_hints.ai_socktype = SOCK_STREAM;
  g_hints.ai_flags = AI_PASSIVE;
  // get address to local host port 9000.
  if ((rc = getaddrinfo(NULL, "9000", &g_hints, &g_servInfo)) != 0) {
    printf("Error with getaddrinfo ");
    exit(EXIT_FAILURE);
  }

  // create socket and bind to it.
  g_serverSocketFd = socket(g_servInfo->ai_family, g_servInfo->ai_socktype,
                            g_servInfo->ai_protocol);
  if (g_serverSocketFd < 0) {
    printf("Error creating socket");
    exit(EXIT_FAILURE);
  }

  // setup socket reuse option
  const int enable = 1;
  if (setsockopt(g_serverSocketFd, SOL_SOCKET, SO_REUSEADDR, &enable,
                 sizeof(int)) < 0) {
    printf("Error setsockopt(SO_REUSEADDR) failed");
    exit(EXIT_FAILURE);
  }
  // Techinically we need loop thorugh the linked list, but we expect only one
  // address so using ony once here to bind to socket
  if (bind(g_serverSocketFd, g_servInfo->ai_addr, g_servInfo->ai_addrlen) !=
      0) {
    printf("Error binding to socket");
    exit(EXIT_FAILURE);
  }

  if (g_servInfo) {
    freeaddrinfo(g_servInfo);
    g_servInfo = NULL;
  }

  // listen
  if (listen(g_serverSocketFd, 1)) {
    printf("Error listening to socket");
    exit(EXIT_FAILURE);
  }

  // Setup for thread usage

  // open file for use by threads
  unlink(LOG_FILE);
  g_logFileFd = open(LOG_FILE, O_CREAT|O_APPEND|O_SYNC|O_RDWR, 0644);
  if (g_logFileFd < 0) {
    printf("Error opening the file \n");
    exit(EXIT_FAILURE);
  }
  printf("Main: File opened now fd=%d\n",g_logFileFd);
  // create linked list for receiver threads
  SLIST_INIT(&g_head);
  g_threadCount = 0;
  g_threadId = 0;
  printf("Main: Linkedlist for threads initialized\n");
  // Initialize mutex for use by threads.
  rc = pthread_mutex_init(&g_mutex, NULL);
  if (rc != 0) {
    printf("Error obtaining mutex");
    exit(EXIT_FAILURE);
  }
  g_mutexInitialized = true;
  printf("Main: Mutex intialized now\n");

  // Setup Timer
  struct sigevent sev;
  struct itimerspec its;
  // create the timer
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = &timerHandlerThreadFunction;
  sev.sigev_notify_attributes = 0;
  /*sev.sigev_signo = TIMERSIG;*/
  sev.sigev_value.sival_ptr = &g_timerid;
  if (timer_create(CLOCK_REALTIME, &sev, &g_timerid) == -1) {
    printf("Error timer create");
    exit(EXIT_FAILURE);
  }
  g_timerCreated = true;
  printf("Main: Timer created successfully\n");
  // start the timer
  its.it_value.tv_sec = 1;
  its.it_value.tv_nsec = 0;
  its.it_interval.tv_sec = 10;
  its.it_interval.tv_nsec = 0;
  if (timer_settime(g_timerid, 0, &its, NULL) == -1) {
    printf("Error starting the timer");
    exit(EXIT_FAILURE);
  }
  printf("Main: Timer started now\n");

  // accept connections and start threads.
  for (;;) {
    // accept connections
    printf("Main: Waiting to accept next connection\n");
    clientAddrLen = sizeof(clientAddress);
    g_clientSocketFd = accept(g_serverSocketFd, &clientAddress, &clientAddrLen);
    if (g_clientSocketFd < 0) {
      printf("Error accepting the client socket");
      exit(EXIT_FAILURE);
    }
    // get connected client name
    rc = getnameinfo(&clientAddress, clientAddrLen, clientName, NAME_LEN,
                     clientService, SERVICE_LEN,
                     NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc != 0) {
      printf("Error getting client address name");
      exit(EXIT_FAILURE);
    }
    printf("Accepted connection from %s - service %s\n", clientName,
           clientService);
    // populate param info for thread
    g_datap = malloc(sizeof(slist_data_t));
    g_datap->tparms.mutex = &g_mutex;
    g_datap->tparms.logFileFd = g_logFileFd;
    g_datap->tparms.clientSocketFd = g_clientSocketFd;
    strcpy(g_datap->tparms.clientName, clientName);
    g_datap->tparms.threadId = ++g_threadId;
    // create thread for processing
    printf("Main: Creating thread for %s\n", clientName);
    /*Create the thread*/
    rc = pthread_create(&tid, NULL, sendAndReceiveThread,
                        (void *)(&g_datap->tparms));
    if (rc != 0) {
      printf("Error Failed to create thread, error code");
      exit(EXIT_FAILURE);
    }
    g_datap->tid = tid;
    g_threadCount++;
    // save thread to linked list
    printf("Main: Insert thread ID: %d in linkedlist\n",
           g_datap->tparms.threadId);
    SLIST_INSERT_HEAD(&g_head, g_datap, entries);

    // Find threads that are done and join.
    while (!SLIST_EMPTY(&g_head)) {
      g_datap = SLIST_FIRST(&g_head);
      if (g_datap->tparms.clientSocketFd >= 0) {
        rc = pthread_join(g_datap->tid, NULL);
        if (rc < 0) {
          printf("Error joinining thread");
          exit(EXIT_FAILURE);
        }
        SLIST_REMOVE_HEAD(&g_head, entries);
        printf("Main: Thread %d is done. Removed from linked list\n",
               g_datap->tparms.threadId);
        free(g_datap);
        g_threadCount--;
      }
    }
  }

  return EXIT_SUCCESS;
}