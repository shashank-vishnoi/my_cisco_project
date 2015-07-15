#include <glib.h>
#include <iostream>
#include "eventQueue.h"
#include <stdio.h>

using namespace std;

MSPEventQueue* gEventQueue = NULL;

void* async_lengthy_func(void* data)
{
    (void) data;
    bool notExitBool = true;

    while (notExitBool)
    {
        //cout << __FUNCTION__ << ":" << __LINE__ << endl;
        Event *evt = gEventQueue->popEventQueue();
        //cout << __FUNCTION__ << ":" << __LINE__ << endl;
        cout << __FUNCTION__ << ":" << __LINE__ << ", eventType " <<  evt->eventType << endl;
        if (evt->eventType == 9999)
        {
            notExitBool = false;
        }
        gEventQueue->freeEvent(evt);
    }
    return NULL;
}

gpointer main_func(void* data)
{
    (void) data;
    GMainLoop *ml;
    ml = g_main_loop_new(NULL, TRUE);
    g_main_loop_run(ml);
    return NULL;
}

int main(void)
{
    GThread * thread1;
    GThread * thread2;

    char message1[] = "Thread 1";
    GError           *err1 = NULL ;
    if (!g_thread_supported()) g_thread_init(NULL);
    gEventQueue = new EventQueue();

    thread2 = g_thread_create((GThreadFunc)main_func, (void *)message1, false, &err1);

    for (int i = 0; i < 100000 ; ++i)
    {
        thread1 = g_thread_create((GThreadFunc)async_lengthy_func, (void *)message1, true, &err1);
        printf("Thread id %p, count %d\n", thread2, i);

        cout << "glib test" << endl;
        //g_usleep(1000000);
        cout << __FUNCTION__ << ":" << __LINE__ << endl;
        gEventQueue->dispatchEvent(5);
        gEventQueue->dispatchEvent(6);
        gEventQueue->dispatchEvent(9999);
        g_thread_join(thread1);
        delete gEventQueue;
        gEventQueue = new MSPEventQueue();
    }

    return 0;
}
