#include "queue.h"
#include <errno.h>
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

#define USE_AESD_CHAR_DEVICE

#ifdef USE_AESD_CHAR_DEVICE
#define LOG_FILE "/dev/aesdchar"
#else
#define LOG_FILE "/var/tmp/aesdsocketdata"
#endif

#define NAME_LEN 50
#define SERVICE_LEN 50
#define BUF_SIZE 1024
#define TIMERSIG SIGUSR1

#define ERROR_LOG(msg, errnum, ...)                                            \
  printf("ERROR %d:%s: " msg "\n", errnum, strerror(errnum), ##__VA_ARGS__)
#if 1
// enable debug prints
#define PRINT_LOG(msg, ...) printf("%s: " msg, __func__, ##__VA_ARGS__)
#else
// disable debug prints
#define PRINT_LOG(msg, ...)
#endif

#define checkerr(errcond, msg, ...)                                            \
  g_errno = errno;                                                             \
  if ((errcond) == true) {                                                     \
    ERROR_LOG(msg, g_errno, ##__VA_ARGS__);                                    \
    exit(EXIT_FAILURE);                                                        \
  }

static int g_errno = 0;
static int g_logFileFd = -1;
static int g_serverSocketFd = -1;
static int g_clientSocketFd = -1;
struct addrinfo g_hints;
struct addrinfo *g_servInfo = NULL;
static bool g_runAsDaemon = false;

#ifndef USE_AESD_CHAR_DEVICE
static timer_t g_timerid;
static bool g_timerCreated = false;
#endif

static int g_threadCount = 0;
static int g_threadId = 0;
static pthread_mutex_t g_mutex;
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

// Called from signal handler or as cleanup if signum==-1
void sigHandlerFunction(int signum) {
  int rc;
  if (signum > 0) {
    PRINT_LOG("SigHandler: Caught signal %d - %s\n", signum, strsignal(signum));
  }
  // Clean up actions on sigint or sigterm
  if (g_threadCount > 0) {
    while (!SLIST_EMPTY(&g_head)) {
      PRINT_LOG("SigHandler: client thread params being freed\n");
      g_datap = SLIST_FIRST(&g_head);
      if (g_datap->tparms.clientSocketFd >= 0) {
        rc = pthread_join(g_datap->tid, NULL);
        checkerr(rc != 0, " joining thread");
        g_threadCount--;
        PRINT_LOG("SigHandler: Thread %lu is done. \n", g_datap->tid);
      }
      if (g_datap->tparms.clientSocketFd != -1) {
        PRINT_LOG("SigHandler: closed clientsocketfd\n");
        close(g_datap->tparms.clientSocketFd);
      }
      PRINT_LOG("SigHandler: removed thread from linkedlist\n");
      SLIST_REMOVE_HEAD(&g_head, entries);
      PRINT_LOG("SigHandler: freed linked list mem for this thread\n");
      free(g_datap);
    }
  }

#ifndef USE_AESD_CHAR_DEVICE
  if (g_logFileFd) {
    close(g_logFileFd);
    PRINT_LOG("SigHandler: closed logfile\n");
  }
#endif
  pthread_mutex_destroy(&g_mutex);
#ifndef USE_AESD_CHAR_DEVICE
  unlink(LOG_FILE);
  PRINT_LOG("SigHandler: Deleted the file\n");
  if (g_timerCreated) {
    timer_delete(g_timerid);
    PRINT_LOG("SigHandler: timer deleted\n");
  }
#endif
  if (g_serverSocketFd) {
    close(g_serverSocketFd);
    PRINT_LOG("SigHandler: server socket closed\n");
  }
  if (g_servInfo) {
    freeaddrinfo(g_servInfo);
    PRINT_LOG("SigHandler: serv info freed\n");
  }
  PRINT_LOG("SigHandler: Good Bye!\n\n\n");
  exit(EXIT_FAILURE);
}

#ifndef USE_AESD_CHAR_DEVICE
static void timerHandlerFunction(int signum) {
  time_t t;
  struct tm *tmp;
  char prefix[] = "timestamp:";
  char postfix[] = "\n";
  char outstr[100];
  int rc;

  PRINT_LOG("TimerHandler: mutex initialized= %d\n", g_mutexInitialized);
  if (!g_mutexInitialized || g_logFileFd == -1)
    return;

  t = time(NULL);
  tmp = localtime(&t);
  checkerr(tmp == NULL, " getting localtime");

  rc = strftime(outstr, sizeof(outstr), "%a, %d %b %Y %T %z", tmp);
  checkerr(rc == 0, " strftime error");

  PRINT_LOG("TimerHandler: obtained mutex lock\n");
  rc = pthread_mutex_lock(&g_mutex);
  checkerr(rc != 0, " getting timer mutex lock");

  // seek to end of the Log file , write time stamp and close
  rc = lseek(g_logFileFd, 0, SEEK_END);
  checkerr(rc == -1, " seeking to end of file");

  rc = write(g_logFileFd, prefix, strlen(prefix));
  checkerr(rc == -1, " writing to file");

  rc = write(g_logFileFd, outstr, strlen(outstr));
  checkerr(rc == -1, " writing to file");

  // rc = write(g_logFileFd, postfix, strlen(postfix));
  rc = write(g_logFileFd, postfix, strlen(postfix));
  checkerr(rc == -1, " writing to file");

  PRINT_LOG("TimerHandler: wrote time stamp to file\n");

  rc = pthread_mutex_unlock(&g_mutex);
  checkerr(rc != 0, " mutext unlock");
  PRINT_LOG("TimerHandler: released mutex lock\n");
  return;
}

static void timerHandlerThreadFunction(union sigval sigev_value) {
  return timerHandlerFunction(34);
}
#endif

void *sendAndReceiveThread(void *thread_param) {
  struct thread_data *pArgs = (struct thread_data *)thread_param;
  int rc;
  char buf[BUF_SIZE];
  int rcvd = -1;

  PRINT_LOG("Thread ID %d obtaining mutex lock\n", pArgs->threadId);
  rc = pthread_mutex_lock(pArgs->mutex);
  checkerr(rc != 0, " locking mutex threadId=%d, code=%d", pArgs->threadId, rc);
  // lock success
#ifdef USE_AESD_CHAR_DEVICE
  pArgs->logFileFd = open(LOG_FILE, O_RDWR, 0644);
  checkerr(pArgs->logFileFd < 0, "Error opening the file \n");
  PRINT_LOG("Thread ID %d File opened now fd=%d\n", pArgs->threadId,
            pArgs->logFileFd);
#else
  // setup
  PRINT_LOG("Thread ID %d  set file location to end for writing \n",
            pArgs->threadId);
  lseek(pArgs->logFileFd, 0, SEEK_END);
#endif

  // recv
  PRINT_LOG("Thread ID %d starting to receive\n", pArgs->threadId);
  while ((rcvd = recv(pArgs->clientSocketFd, (void *)buf, sizeof(buf), 0)) >
         0) {
    PRINT_LOG("\tnum rcvd = %d \n", (int)rcvd);
    rc = write(pArgs->logFileFd, buf, rcvd);
    checkerr(rc != rcvd, " writing to file");
    // finished with recieving if end of packet
    if (buf[rcvd - 1] == '\n')
      break;
  }

#ifndef USE_AESD_CHAR_DEVICE
  PRINT_LOG("Thread ID %d  reset file location for reading \n",
            pArgs->threadId);
  lseek(pArgs->logFileFd, 0, SEEK_SET);
#endif

  char readBuf[1024];
  ssize_t readLen;

  // send
  PRINT_LOG("Thread ID %d  sending to client\n", pArgs->threadId);
  while ((readLen = read(pArgs->logFileFd, readBuf, 1024)) > 0) {
    PRINT_LOG("\tnum read = %d \n", (int)readLen);
    /*
    int i;
    PRINT_LOG("--\n")
    for(i=0;i<readLen;i++){putc(readBuf[i]);}
    PRINT_LOG("--\n")
    */
    rc = send(pArgs->clientSocketFd, readBuf, readLen, 0);
    checkerr(rc < 0, " sending data to client");
  }
  // cleanup
#ifdef USE_AESD_CHAR_DEVICE
  close(pArgs->logFileFd);
  PRINT_LOG("Thread ID %d closed file now fd=%d\n", pArgs->threadId,
            pArgs->logFileFd);
  pArgs->logFileFd = -1;
#endif
  // Unlock
  rc = pthread_mutex_unlock(pArgs->mutex);
  checkerr(rc != 0, " unlocking mutex");
  PRINT_LOG("Thread ID %d released mutex lock\n", pArgs->threadId);
  // shutdown socket
  PRINT_LOG("Thread ID %d  closing the connection to %s\n", pArgs->threadId,
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
    PRINT_LOG("Usage: %s [-d or -D]\n", argv[0]);
    PRINT_LOG("\t -d or -D: run as a daemon\n");
    exit(EXIT_FAILURE);
  }

  // run as daemon
  if (g_runAsDaemon) {
    // fork
    int pid;
    pid = fork();
    checkerr(pid == -1, "Error forking the process");

    // kill the parent
    if (pid > 0) {
      PRINT_LOG("Parent exiting as running in daemon mode!\n");
      exit(EXIT_SUCCESS);
    }
    // Let the child run as daemon
    if (pid == 0) {
      PRINT_LOG("Running as a daemon\n");
      // move on to listening and accepting
      // connections
    }
  }

  // Register signal handler
  // register handlers for SIGTERM and SIGINT
  PRINT_LOG("Main: Registering handlers for SIGTERM and SIGINT\n");
  struct sigaction act = {0};
  act.sa_handler = sigHandlerFunction;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  checkerr(sigaction(SIGTERM, &act, NULL) == -1,
           "Error registering SIGTERM sigaction");

  checkerr(sigaction(SIGINT, &act, NULL) == -1,
           "Error registering SIGINT sigaction");

  // register timer thread for SIGUSR1

#ifdef USE_AESD_CHAR_DEVICE
  PRINT_LOG(
      "Main: Skipping timestamp handlind as aesd_char_device is being used");
#else
  PRINT_LOG("Main: Registering handlers for SIGUSR1\n");
  struct sigaction timeract = {0};
  timeract.sa_handler = timerHandlerFunction;
  sigemptyset(&timeract.sa_mask);
  timeract.sa_flags = SA_SIGINFO;
  checkerr(sigaction(TIMERSIG, &timeract, NULL) == -1,
           "Error registering timer sigaction");
#endif

  // Setup socket
  memset(&g_hints, 0, sizeof(g_hints));
  g_hints.ai_family = AF_UNSPEC;
  g_hints.ai_socktype = SOCK_STREAM;
  g_hints.ai_flags = AI_PASSIVE;
  // get address to local host port 9000.
  checkerr((rc = getaddrinfo(NULL, "9000", &g_hints, &g_servInfo)) != 0,
           "Error with getaddrinfo ");

  // create socket and bind to it.
  g_serverSocketFd = socket(g_servInfo->ai_family, g_servInfo->ai_socktype,
                            g_servInfo->ai_protocol);
  checkerr(g_serverSocketFd < 0, "Error creating socket");

  // setup socket reuse option
  const int enable = 1;
  checkerr(setsockopt(g_serverSocketFd, SOL_SOCKET, SO_REUSEADDR, &enable,
                      sizeof(int)) != 0,
           "Error setsockopt(SO_REUSEADDR) failed");

  // Techinically we need loop thorugh the linked list, but we expect only one
  // address so using ony once here to bind to socket
  checkerr(
      bind(g_serverSocketFd, g_servInfo->ai_addr, g_servInfo->ai_addrlen) != 0,
      "Error binding to socket");

  if (g_servInfo) {
    freeaddrinfo(g_servInfo);
    g_servInfo = NULL;
  }

  // listen
  checkerr(listen(g_serverSocketFd, 1) != 0, "Error listening to socket");

  // Setup for thread usage

  // open file for use by threads
#ifndef USE_AESD_CHAR_DEVICE
  unlink(LOG_FILE);
  g_logFileFd = open(LOG_FILE, O_CREAT | O_APPEND | O_SYNC | O_RDWR, 0644);
  checkerr(g_logFileFd < 0, "Error opening the file \n");
  PRINT_LOG("Main: File opened now fd=%d\n", g_logFileFd);
#endif

  // create linked list for receiver threads
  SLIST_INIT(&g_head);
  g_threadCount = 0;
  g_threadId = 0;
  PRINT_LOG("Main: Linkedlist for threads initialized\n");

  // Initialize mutex for use by threads.
  rc = pthread_mutex_init(&g_mutex, NULL);
  checkerr(rc != 0, "Error obtaining mutex");
  g_mutexInitialized = true;
  PRINT_LOG("Main: Mutex intialized now\n");

#ifdef USE_AESD_CHAR_DEVICE
  PRINT_LOG("Main: Timer not used when aesd_char_dev is used\n");
#else
  // Setup Timer
  struct sigevent sev;
  struct itimerspec its;

  // create the timer
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = &timerHandlerThreadFunction;
  sev.sigev_notify_attributes = 0;
  /*sev.sigev_signo = TIMERSIG;*/
  sev.sigev_value.sival_ptr = &g_timerid;
  checkerr(timer_create(CLOCK_REALTIME, &sev, &g_timerid) != 0,
           "Error timer create");
  g_timerCreated = true;
  PRINT_LOG("Main: Timer created successfully\n");

  // start the timer
  its.it_value.tv_sec = 1;
  its.it_value.tv_nsec = 0;
  its.it_interval.tv_sec = 10;
  its.it_interval.tv_nsec = 0;
  checkerr(timer_settime(g_timerid, 0, &its, NULL) != 0,
           "Error starting the timer");
  PRINT_LOG("Main: Timer started now\n");
#endif

  // accept connections and start threads.
  for (;;) {
    // accept connections
    PRINT_LOG("Main: Waiting to accept next connection\n");
    clientAddrLen = sizeof(clientAddress);
    g_clientSocketFd = accept(g_serverSocketFd, &clientAddress, &clientAddrLen);
    checkerr(g_clientSocketFd < 0, " accepting the client socket");
    // get connected client name
    rc = getnameinfo(&clientAddress, clientAddrLen, clientName, NAME_LEN,
                     clientService, SERVICE_LEN,
                     NI_NUMERICHOST | NI_NUMERICSERV);
    checkerr(rc != 0, "Error getting client address name");
    PRINT_LOG("Accepted connection from %s - service %s\n", clientName,
              clientService);

    // populate param info for thread
    g_datap = malloc(sizeof(slist_data_t));
    g_datap->tparms.mutex = &g_mutex;
    g_datap->tparms.logFileFd = g_logFileFd;
    g_datap->tparms.clientSocketFd = g_clientSocketFd;
    strcpy(g_datap->tparms.clientName, clientName);
    g_datap->tparms.threadId = ++g_threadId;

    // create thread for processing
    PRINT_LOG("Main: Creating thread for %s\n", clientName);
    rc = pthread_create(&tid, NULL, sendAndReceiveThread,
                        (void *)(&g_datap->tparms));
    checkerr(rc != 0, "Error Failed to create thread, error code");
    g_datap->tid = tid;
    g_threadCount++;

    // save thread to linked list
    PRINT_LOG("Main: Insert thread ID: %d in linkedlist\n",
              g_datap->tparms.threadId);
    SLIST_INSERT_HEAD(&g_head, g_datap, entries);

    // Find threads that are done and join.
    while (!SLIST_EMPTY(&g_head)) {
      g_datap = SLIST_FIRST(&g_head);
      if (g_datap->tparms.clientSocketFd >= 0) {
        rc = pthread_join(g_datap->tid, NULL);
        checkerr(rc != 0, " pthread join");
        SLIST_REMOVE_HEAD(&g_head, entries);
        PRINT_LOG("Main: Thread %d is done. Removed from linked list\n",
                  g_datap->tparms.threadId);
        free(g_datap);
        g_threadCount--;
      }
    }
  }

  return EXIT_SUCCESS;
}