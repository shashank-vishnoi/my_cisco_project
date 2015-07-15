/** @file eventQueue.h
 *
 * @author Khamdy Xayaraj
 * @date 05-20-2010
 *
 * @version 1.0
 *
 * @brief The Asynchronous EventQueue header file.
 *
 * This module:
 * -- support the asynchronous event queue driven.
 * -- support immediate event dispatch
 * -- schedule of event timer in msec, and cancel of pending scheduled events.
 * Note: for the scheduled event timer to worked, the platform needs to
 *       have the GMain loop running in the GMainContext
 */

#ifndef _EVENTQUEUE_
#define _EVENTQUEUE_

#include <map>
#include <list>

struct Event
{
    unsigned int eventType ;
    void *eventData;
};

struct CallBackTimer
{
    Event* evt;
    void*   q;
};


class MSPEventQueue
{
public:
    void dispatchEvent(unsigned int eventType, void *eventData = NULL);
    void freeEvent(Event* event);
    Event* popEventQueue(void);
    void flushQueue(void);

    // Set time out in seconds
    void setTimeOutSecs(unsigned int waitTime);
    void unSetTimeOut();

    ~MSPEventQueue();
    MSPEventQueue();

private:
    std::list <Event*>    mQueue;
    pthread_mutex_t  mMutex;
    pthread_cond_t mCond;
    unsigned int mWaitTimeSecs;
};

#endif
