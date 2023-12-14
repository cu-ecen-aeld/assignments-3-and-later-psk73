#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // PSK-DONE: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    struct thread_data* pArgs = (struct thread_data *) thread_param;

    //sleep
    printf("Thread ID %d waiting for %d ms before obtain\n",(int)pthread_self(),pArgs->wait_to_obtain_ms);
    usleep(1000*pArgs->wait_to_obtain_ms);

    //obtain mutex
    int rc;
    rc = pthread_mutex_lock(pArgs->mutex);
    if(rc != 0)
    {
        printf("Thread ID %d Failed to obtain mutex lock, code %d\n", (int)pthread_self(), rc);
        pArgs->thread_complete_success = false;
    } else {
        printf("Thread ID %d waiting for %d ms to release\n",(int)pthread_self(),pArgs->wait_to_release_ms);
        usleep(1000*pArgs->wait_to_release_ms);
        rc = pthread_mutex_unlock(pArgs->mutex);
        if(rc != 0)
        {
            printf("Thread ID %d Failed to release mutex lock, code %d\n", (int)pthread_self(), rc);
            pArgs->thread_complete_success = false;
        }
        else {
        pArgs->thread_complete_success = true;
        }
    }

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * PSK-DONE: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data* pThreadParams=NULL;
    int rc=0;

    /*Allocate mem for args*/
    pThreadParams=(struct thread_data*)malloc(sizeof(struct thread_data));
    if(!pThreadParams)
    {
        printf("Failed to allocate memory for thread");
        return false;
    }

    /* Populate args */
    pThreadParams->wait_to_obtain_ms = wait_to_obtain_ms;
    pThreadParams->wait_to_release_ms = wait_to_release_ms;
    pThreadParams->mutex = mutex;

    /*Create the thread*/
    rc = pthread_create(thread,NULL,threadfunc,(void *)pThreadParams);
    if(rc != 0)
    {
        printf("Failed to create thread, error code %d",rc);
        return false;
    }

    return true;
}
