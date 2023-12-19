#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <syslog.h>
#include <pthread.h>
#include "queue.h"

#if 1
#define DEBUG_LOG_PR(msg,...) printf("%s: " msg, __func__, ##__VA_ARGS__)
#define DEBUG_LOG DEBUG_LOG_PR
#else
#define DEBUG_LOG(msg,...)
#endif

#if 1
#define ERROR_LOG(msg,...) printf("ERROR %s " msg "\n" , __func__, ##__VA_ARGS__)
#else
#define ERROR_LOG(msg,...) ERROR_LOG(msg)
#endif

#define LOG_FILE "/var/tmp/aesdsocketdata"
#define NAME_LEN 50
#define SERVICE_LEN 50
#define BUF_SIZE 1024
#define TIMERSIG SIGUSR1

FILE *g_logFileWritePtr = NULL;
FILE *g_logFileReadPtr = NULL;
int g_serverSocketFd = -1;
int g_clientSocketFd = -1;
struct addrinfo g_hints;
struct addrinfo *g_servInfo = NULL;
bool g_runAsDaemon = false;
timer_t g_timerid;
bool g_timerCreated = false;

struct thread_data{
    int threadId;
    pthread_mutex_t *mutex;
    FILE *logFileWritePtr;
    FILE *logFileReadPtr;
    int  clientSocketFd;
    char *readLine;
    char clientName[NAME_LEN];
    /**
     * Set to true if the thread completed with success, false
     * if an error occurred.
     */
    bool isThreadDone;
};

// SLIST.
struct slist_data_s {
    pthread_t tid;
    struct thread_data tparms;
    SLIST_ENTRY(slist_data_s) entries;
};
typedef struct slist_data_s slist_data_t;
slist_data_t *g_datap=NULL;
SLIST_HEAD(slisthead, slist_data_s) g_head;
static int g_threadCount = 0;
static int g_threadId=0;

pthread_mutex_t g_mutex;
static bool g_mutexInitialized = false;


void sigHandlerFunction(int signum) {
    int rc;
  DEBUG_LOG("SigHandler: Caught signal %d - %s\n",signum,strsignal(signum));
  // Clean up actions on sigint or sigterm
  if(g_threadCount > 0)
  {
    while (!SLIST_EMPTY(&g_head)) {
        DEBUG_LOG("SigHandler: client thread params being freed\n");
        g_datap = SLIST_FIRST(&g_head);
        if(g_datap->tparms.isThreadDone == true)
        {
            rc = pthread_join(g_datap->tid,NULL);
            if(rc < 0)
            {
                ERROR_LOG("Error joinining thread");
                exit(EXIT_FAILURE);
            }
            g_threadCount--;
            DEBUG_LOG("SigHandler: Thread %lu is done. \n", g_datap->tid);
        }
        if(g_datap->tparms.readLine != NULL)
        {
            DEBUG_LOG("SigHandler: readline freed\n");
            free(g_datap->tparms.readLine);
        }
        if(g_datap->tparms.logFileWritePtr != NULL)
        {
            DEBUG_LOG("SigHandler: closed logfilewriter\n");
            fclose(g_datap->tparms.logFileWritePtr);
        }
        if(g_datap->tparms.logFileReadPtr != NULL)
        {
            DEBUG_LOG("SigHandler: closed logfilereader\n");
            fclose(g_datap->tparms.logFileReadPtr);
        }
        if(g_datap->tparms.clientSocketFd != -1)
        {
            DEBUG_LOG("SigHandler: closed clientsocketfd\n");
            close(g_datap->tparms.clientSocketFd);
        }

        DEBUG_LOG("SigHandler: removed thread from linkedlist\n");
        SLIST_REMOVE_HEAD(&g_head, entries);
        DEBUG_LOG("SigHandler: freed linked list mem for this thread\n");
        free(g_datap);
    }
  }
  DEBUG_LOG("SigHandler: Deleted the file\n");
  unlink(LOG_FILE);

  pthread_mutex_destroy(&g_mutex);

  if (g_timerCreated)
  {
      DEBUG_LOG("SigHandler: timer deleted\n");
      timer_delete(g_timerid);
  }

  if (g_serverSocketFd)
  {
    DEBUG_LOG("SigHandler: server socket closed\n");
    close(g_serverSocketFd);
  }

  if (g_servInfo)
  {
    DEBUG_LOG("SigHandler: serv info freed\n");
    freeaddrinfo(g_servInfo);
  }
  DEBUG_LOG("SigHandler: Good Bye!\n\n\n");
  exit(EXIT_FAILURE);
}

void  timerHandlerFunction(int signum)
{
    time_t t;
    struct tm *tmp;
    char prefix[]="timestamp:";
    char postfix[]="\n";
    char outstr[100];
    int rc;

    DEBUG_LOG("TimerHandler: mutex initialized= %d\n", g_mutexInitialized);
    if(!g_mutexInitialized)
        return;

    t=time(NULL);
    tmp = localtime(&t);
    if( tmp == NULL)
    {
        ERROR_LOG("Error getting localtime");
        exit(EXIT_FAILURE);
    }
    if(strftime(outstr,sizeof(outstr),"%a, %d %b %Y %T %z",tmp) == 0)
    {
        ERROR_LOG("Error strftime error");
        exit(EXIT_FAILURE);
    }
    rc = pthread_mutex_lock(&g_mutex);
    DEBUG_LOG("TimerHandler: obtained mutex lock\n");
    if(rc == 0)
    {
       // Open Log file , write time stamp and close
       FILE *logFileWritePtr = fopen(LOG_FILE, "a");
       if (!logFileWritePtr) {
           ERROR_LOG("Error opening log file");
           exit(EXIT_FAILURE);
       }
      fwrite(prefix,sizeof(char),strlen(prefix),logFileWritePtr);
      fwrite(outstr, sizeof(char),strlen(outstr),logFileWritePtr);
      fwrite(postfix,sizeof(char),strlen(postfix),logFileWritePtr);
      fflush(logFileWritePtr);
      fclose(logFileWritePtr);
      logFileWritePtr=NULL;
      rc = pthread_mutex_unlock(&g_mutex);
      if(rc != 0)
      {
           ERROR_LOG("Error mutex unlock");
           exit(EXIT_FAILURE);
      }
      DEBUG_LOG("TimerHandler: released mutex lock\n");
    }
    else
    {
        ERROR_LOG("Error timer task mutex error");
        exit(EXIT_FAILURE);
    }
    return;
}
static void timerHandlerThreadFunction(union sigval sigev_value)
{
    return timerHandlerFunction(34);
}

void* sendAndReceiveThread(void* thread_param)
{
    struct thread_data* pArgs = (struct thread_data *) thread_param;
    int rc;
    int status;
    char buf[BUF_SIZE];
    int rcvd = -1;

    rc = pthread_mutex_lock(pArgs->mutex);
    DEBUG_LOG("Thread ID %d obtaining mutex lock\n",pArgs->threadId);
    if(rc == 0)
    {
        //lock success
        DEBUG_LOG("Thread ID %d  opening the file for write\n", pArgs->threadId);
        {
            //setup
            // Open Log file
            pArgs->logFileWritePtr = fopen(LOG_FILE, "a");
            if (!pArgs->logFileWritePtr) {
              ERROR_LOG("Error opening log file");
              exit(EXIT_FAILURE);
            }
        }
        DEBUG_LOG("Thread ID %d  starting to receive\n", pArgs->threadId);
        {
            // recv
            while ((rcvd = recv(pArgs->clientSocketFd, (void *)buf, sizeof(buf), 0)) > 0) {
              fwrite(buf, sizeof(char), rcvd, pArgs->logFileWritePtr);
              fflush(pArgs->logFileWritePtr);
              // finished with recieving if end of packet
              if (buf[rcvd - 1] == '\n')
                break;
            }
            DEBUG_LOG("Thread ID %d  closing the file for write\n", pArgs->threadId);
            fclose(pArgs->logFileWritePtr);
            pArgs->logFileWritePtr = NULL;
        }
        DEBUG_LOG("Thread ID %d  sending to client\n", pArgs->threadId);
        size_t readLineLen = 0;
        ssize_t numread;
        {
            // send
            pArgs->readLine = NULL;
            DEBUG_LOG("Thread ID %d  opening the file for read\n", pArgs->threadId);
            pArgs->logFileReadPtr = fopen(LOG_FILE, "r");
            if (!pArgs->logFileReadPtr) {
              ERROR_LOG("Error opening file for reading");
              exit(EXIT_FAILURE);
            }
            while ((numread = getline(&pArgs->readLine, &readLineLen, pArgs->logFileReadPtr)) != -1) {
               DEBUG_LOG("\tnum read = %d readLineLen = %d\n", (int)numread, (int)readLineLen);
               /*DEBUG_LOG("--->%s<---\n", pArgs->readLine);*/
              status = send(pArgs->clientSocketFd, pArgs->readLine, numread, 0);
              if (status < 0) {
                ERROR_LOG("Error sending data to client");
                exit(EXIT_FAILURE);
              }
            }
        }
        {
            //cleanup
            DEBUG_LOG("Thread ID %d  closing the file for read\n", pArgs->threadId);
            fclose(pArgs->logFileReadPtr);
            pArgs->logFileReadPtr = NULL;
            if (pArgs->readLine != NULL) {
              DEBUG_LOG("\tThread ID %d free readline len=%d numread=%d\n",pArgs->threadId,(int)readLineLen,(int)numread);
              free(pArgs->readLine);
            }
            //shutdown socket
            DEBUG_LOG("Thread ID %d  closing the connection to %s\n", pArgs->threadId, pArgs->clientName);
            shutdown(pArgs->clientSocketFd, SHUT_RDWR);
            close(pArgs->clientSocketFd);
            pArgs->clientSocketFd = -1;
        }
        {
            //Unlock
            rc = pthread_mutex_unlock(pArgs->mutex);
            if(rc != 0)
            {
                DEBUG_LOG("Thread ID %d Failed to release mutex lock, code %d\n", pArgs->threadId, rc);
                ERROR_LOG("Error unlocking mutex");
                pArgs->isThreadDone = false;
                exit(EXIT_FAILURE);
            }
            else {
                DEBUG_LOG("Thread ID %d released mutex lock\n",pArgs->threadId);
                pArgs->isThreadDone = true;
            }
        }
    }
    else{
        //lock failure
        DEBUG_LOG("Thread ID %d Failed to obtain mutex lock, code %d\n", pArgs->threadId, rc);
        ERROR_LOG("Error locking mutex");
        pArgs->isThreadDone = false;
        exit(EXIT_FAILURE);
    }
    return thread_param;
}

int main(int argc, char **argv) {
  bool incorrectUsage = false;
  int rc;
  struct sockaddr clientAddress;
  unsigned int clientAddrLen;
  char clientName[NAME_LEN], clientService[SERVICE_LEN];
  pthread_t tid;

  //sleep(2);

  // process args
  /*{*/
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
  /*}*/

  // Register signal handler
  /*{*/
      //register handlers for SIGTERM and SIGINT
      struct sigaction act = {0};
      act.sa_handler = sigHandlerFunction;
      sigemptyset(&act.sa_mask);
      act.sa_flags = 0;
      if (sigaction(SIGTERM, &act, NULL) == -1) {
        ERROR_LOG("Error registering SIGTERM sigaction");
        exit(EXIT_FAILURE);
      }
      if (sigaction(SIGINT, &act, NULL) == -1) {
        ERROR_LOG("Error registering SIGINT sigaction");
        exit(EXIT_FAILURE);
      }
      struct sigaction timeract = {0};
      timeract.sa_handler = timerHandlerFunction;
      sigemptyset(&timeract.sa_mask);
      timeract.sa_flags = SA_SIGINFO;
      if (sigaction(TIMERSIG, &timeract, NULL) == -1) {
        ERROR_LOG("Error registering timer sigaction");
        exit(EXIT_FAILURE);
      }
  /*}*/

  // Setup socket
  /*{*/
      memset(&g_hints, 0, sizeof(g_hints));
      g_hints.ai_family = AF_UNSPEC;
      g_hints.ai_socktype = SOCK_STREAM;
      g_hints.ai_flags = AI_PASSIVE;
      // get address to local host port 9000.
      if ((rc = getaddrinfo(NULL, "9000", &g_hints, &g_servInfo)) != 0) {
        ERROR_LOG("Error with getaddrinfo ");
        exit(EXIT_FAILURE);
      }
      // create socket and bind to it.
      g_serverSocketFd = socket(g_servInfo->ai_family, g_servInfo->ai_socktype,
                                g_servInfo->ai_protocol);
      if (g_serverSocketFd < 0) {
        ERROR_LOG("Error creating socket");
        exit(EXIT_FAILURE);
      }
      // setup socket reuse option
      const int enable = 1;
      if (setsockopt(g_serverSocketFd, SOL_SOCKET, SO_REUSEADDR, &enable,
                     sizeof(int)) < 0) {
        ERROR_LOG("Error setsockopt(SO_REUSEADDR) failed");
        exit(EXIT_FAILURE);
      }
      // Techinically we need loop thorugh the linked list, but we expect only one
      // address so using ony once here to bind to socket
      if (bind(g_serverSocketFd, g_servInfo->ai_addr, g_servInfo->ai_addrlen) !=
          0) {
        ERROR_LOG("Error binding to socket");
        exit(EXIT_FAILURE);
      }
      if (g_servInfo)
      {
        freeaddrinfo(g_servInfo);
        g_servInfo = NULL;
      }
  /*}*/

  //run as daemon
  /*{*/
      int pid;
      if(g_runAsDaemon){
        //fork 
        pid = fork();
        if(pid == -1)
        {
          ERROR_LOG("Error forking the process");
          exit(EXIT_FAILURE);
        }
        //kill the parent
        if(pid > 0)
        {
          printf("Running as a daemon!\n");
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
  /*}*/

  // listen
  if (listen(g_serverSocketFd, 1)) {
    ERROR_LOG("Error listening to socket");
    exit(EXIT_FAILURE);
  }

  //Setup for thread usage
  /*{*/
      unlink(LOG_FILE);
      //create linked list for receiver threads
      SLIST_INIT(&g_head);
      g_threadCount = 0;
      g_threadId = 0;
      //Initialize mutex for use by threads.
      rc=pthread_mutex_init(&g_mutex,NULL);
      if(rc !=0 )
      {
          ERROR_LOG("Error obtaining mutex");
          exit(EXIT_FAILURE);
      }
      g_mutexInitialized  = true;
      DEBUG_LOG("Main: Mutex intialized now\n");
  /*}*/

  //Setup Timer
  /*{*/
      struct sigevent sev;
      struct itimerspec its;
      //create the timer
      sev.sigev_notify = SIGEV_THREAD;
      sev.sigev_notify_function = &timerHandlerThreadFunction;
      sev.sigev_notify_attributes= 0;
      /*sev.sigev_signo = TIMERSIG;*/
      sev.sigev_value.sival_ptr = &g_timerid;
      if(timer_create(CLOCK_REALTIME,&sev,&g_timerid)==-1)
      {
          ERROR_LOG("Error timer create");
          exit(EXIT_FAILURE);
      }
      DEBUG_LOG("Main: Timer created successfully\n");
      g_timerCreated = true;
      //start the timer
      its.it_value.tv_sec = 1;
      its.it_value.tv_nsec = 0;
      its.it_interval.tv_sec = 10;
      its.it_interval.tv_nsec = 0;
      if (timer_settime(g_timerid,0,&its,NULL)==-1)
      {
          ERROR_LOG("Error starting the timer");
          exit(EXIT_FAILURE);
      }
  /*}*/

  // accept connections and start threads.
  for (;;) {
    // accept connections
      DEBUG_LOG("Main: Waiting to accept next connection\n");
    /*{*/
        clientAddrLen = sizeof(clientAddress);
        g_clientSocketFd = accept(g_serverSocketFd, &clientAddress, &clientAddrLen);
        if (g_clientSocketFd < 0) {
          ERROR_LOG("Error accepting the client socket");
          exit(EXIT_FAILURE);
        }
    /*}*/
    //get connected client name
    /*{*/
        rc = getnameinfo(&clientAddress, clientAddrLen, clientName, NAME_LEN,
                             clientService, SERVICE_LEN, NI_NUMERICHOST | NI_NUMERICSERV);
        if (rc != 0) {
          ERROR_LOG("Error getting client address name");
          exit(EXIT_FAILURE);
        }
        DEBUG_LOG("Accepted connection from %s - service %s\n", clientName, clientService);
    /*}*/
    //populate param info for thread
    /*{*/
        g_datap = malloc(sizeof(slist_data_t));
        g_datap->tparms.mutex = &g_mutex;
        g_datap->tparms.logFileWritePtr=NULL;
        g_datap->tparms.logFileReadPtr =NULL;
        g_datap->tparms.clientSocketFd = g_clientSocketFd;
        strcpy(g_datap->tparms.clientName,clientName);
        g_datap->tparms.readLine = NULL;
        g_datap->tparms.isThreadDone = false;
        g_datap->tparms.threadId = ++g_threadId;
    /*}*/
    //create thread for processing
    /*{*/
        DEBUG_LOG("Creating thread for %s\n",clientName);
        /*Create the thread*/
        rc = pthread_create(&tid,NULL,sendAndReceiveThread,(void *)(&g_datap->tparms));
        if(rc != 0)
        {
            ERROR_LOG("Error Failed to create thread, error code");
            exit(EXIT_FAILURE);
        }
        g_datap->tid = tid;
        g_threadCount++;
    /*}*/
    //save thread to linked list
    /*{*/
        DEBUG_LOG("Main: Insert thread ID: %d in linkedlist\n", g_datap->tparms.threadId);
        SLIST_INSERT_HEAD(&g_head, g_datap, entries);
    /*}*/
    // Find threads that are done and join.
    while (!SLIST_EMPTY(&g_head)) {
        g_datap = SLIST_FIRST(&g_head);
        if(g_datap->tparms.isThreadDone == true)
        {
            rc = pthread_join(g_datap->tid,NULL);
            if(rc < 0)
            {
                ERROR_LOG("Error joinining thread");
                exit(EXIT_FAILURE);
            }
            SLIST_REMOVE_HEAD(&g_head, entries);
            DEBUG_LOG("Main: Thread %d is done. Removed from linked list\n", g_datap->tparms.threadId);
            free(g_datap);
            g_threadCount--;
        }
    }
  }

  return EXIT_SUCCESS;
}