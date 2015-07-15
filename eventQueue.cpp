/** @file eventQueue.cpp
 *
 * @author Khamdy Xayaraj
 *
 * @date 05-20-2010
 *
 * @version 1.0
 *
 * @brief The eventQueue implementation.
 *
 */

#include <iostream>
#include <pthread.h>
#include <sys/time.h>
#include <errno.h>
#if defined(DMALLOC)
#include "dmalloc.h"
#endif

#include "eventQueue.h"
#define kTimeOut -1

using namespace std;


MSPEventQueue::MSPEventQueue()
{
    mWaitTimeSecs = 0;
    pthread_mutex_init(&mMutex, NULL);
    pthread_cond_init(&mCond, NULL);
}

MSPEventQueue::~MSPEventQueue()
{
    Event* p = NULL;
    pthread_mutex_lock(&mMutex);

    std::list<Event*>::iterator iterList = mQueue.begin();
    while (iterList != mQueue.end())
    {
        p = (Event*) *iterList;
        if (p)
        {
            delete p;
            p = NULL;
        }
        iterList = mQueue.erase(iterList);
    }

    pthread_mutex_unlock(&mMutex);
    pthread_mutex_destroy(&mMutex);
    pthread_cond_destroy(&mCond);
}



void MSPEventQueue::dispatchEvent(unsigned int eventType, void *eventData)
{
    Event* event = new Event;

    if (event)
    {
        event->eventType = eventType;
        event->eventData = eventData;

        pthread_mutex_lock(&mMutex);
        mQueue.push_back(event);
        pthread_cond_signal(&mCond);
        pthread_mutex_unlock(&mMutex);
    }
}

Event* MSPEventQueue::popEventQueue(void)
{
    Event* p = NULL;
    int rc = 0;
    struct timespec ts;
    struct timeval tp;
    pthread_mutex_lock(&mMutex);

    //   pthread_cond_wait can return from the call even though no call to signal or broadcast on the condition occurred it called as "Spurious-wakeup". 
    //   Since the return from pthread_cond_timedwait() or pthread_cond_wait() does not imply anything about the value of this predicate, 
    //   the predicate should be re-evaluated upon such return.
    while ((mQueue.size() == 0) && (rc != ETIMEDOUT))
    {
        if (mWaitTimeSecs != 0)
        {
            rc = gettimeofday(&tp, NULL);
            ts.tv_sec = tp.tv_sec;
            ts.tv_nsec = tp.tv_usec * 1000;
            ts.tv_sec = ts.tv_sec + mWaitTimeSecs;
            rc = pthread_cond_timedwait(&mCond, &mMutex, &ts);
        }
        else
        {
            rc = pthread_cond_wait(&mCond, &mMutex);
        }
    }

    if (rc == ETIMEDOUT)
    {
        p = new Event();
        if (p)
        {
            p->eventType = kTimeOut;
            p->eventData = NULL;
        }
    }
    else
    {
        p = (Event*) mQueue.front();
        mQueue.pop_front();
    }

    pthread_mutex_unlock(&mMutex);

    return (Event*) p;
}


void  MSPEventQueue::freeEvent(Event* event)
{
    if (event)
    {
        delete event;
        event = NULL;
    }
}

void MSPEventQueue::setTimeOutSecs(unsigned int waitTime)
{
    mWaitTimeSecs = waitTime;
}

void MSPEventQueue::unSetTimeOut()
{
    mWaitTimeSecs = 0;
}



void MSPEventQueue::flushQueue()
{
    Event* p = NULL;
    pthread_mutex_lock(&mMutex);

    std::list<Event*>::iterator iterList = mQueue.begin();
    while (iterList != mQueue.end())
    {
        p = (Event*) *iterList;
        if (p)
        {
            delete p;
            p = NULL;
        }
        iterList = mQueue.erase(iterList);
    }

    pthread_mutex_unlock(&mMutex);
}
