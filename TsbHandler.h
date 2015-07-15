#if !defined(TSBHANDLER_H)
#define TSBHANDLER_H

#include <iostream>
#include <list>

#include "MspCommon.h"
#include "sail-settingsuser-api.h"
#include "use_common.h"
#include "MSPScopedPerfCheck.h"

#if defined(DMALLOC)
#include "dmalloc.h"
#endif

#include <dlog.h>

struct tsb_info
{
    int tsb_count;  //maintaining the number of tsb's allowed.This might be limited by unified settings???...ARUN
    bool *tsb_availability; //database of their availability
    tsb_info()
    {
        tsb_count = 0;
        tsb_availability = NULL;
    }
    tsb_info(int count)
    {
        tsb_count = count;
        tsb_availability = new bool[count];
    }
    ~tsb_info()
    {
        delete [] tsb_availability;
    }
    tsb_info(const tsb_info &t)
    {
        tsb_count = t.tsb_count;
        tsb_availability = new bool[tsb_count];
        for (int i = 0; i < tsb_count; i++)
            tsb_availability[i] = t.tsb_availability[i];
    }
};


class TsbHandler
{
private:
    static TsbHandler *mInstance;
    TsbHandler(); // private for singleton
    struct tsb_info *mTsbInfo;
    int mTsbCount; //Has to be equal to number of tuners for DVR STBs

    pthread_mutex_t m_TsbHandlerMutex;

public:

    ~TsbHandler();
    static TsbHandler * getTsbHandlerInstance();
    eMspStatus getNumberOfTsbs();
    eMspStatus get_tsb(unsigned int *tsb_number);
    eMspStatus release_tsb(unsigned int *tsb_number);

    void locktsbhandlermutex();
    void unlocktsbhandlermutex();
};

#endif
