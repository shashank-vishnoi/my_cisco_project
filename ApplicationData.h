/////////////////////////////////////////////////////////
//
//
// Header file for application data container class
//
// Purpose: Hide the details of application data handling from media player
//
//
/////////////////////////////////////////////////////////

#if !defined(APPLICATION_DATA_H)
#define APPLICATION_DATA_H

#include <stdint.h>
#include <deque>
#include <pthread.h>

#include <dlog.h>

#include "BaseAppData.h"
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#include "DisplaySession.h"
#endif

class ApplicationData: public BaseAppData
{

public:
    ApplicationData(uint32_t maxSize);
    virtual ~ApplicationData();

    int addData(uint8_t *buffer, uint32_t bufferSize);
    int getData(uint8_t *buffer, uint32_t bufferSize);

    uint32_t getTotalSize();
    void printHex(uint8_t* buf, uint16_t len);
    static void getSectionHeader(const uint8_t *buf, tTableHeader *p_header);
    static void logTableHeaderInfo(tTableHeader *tblHdr);
private:
    pthread_mutex_t  accessMutex;
    std::deque <uint8_t>   dataQueue;

};

#endif
