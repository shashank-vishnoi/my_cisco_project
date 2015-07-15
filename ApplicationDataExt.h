/////////////////////////////////////////////////////////
//
//
// Header file for application data container class
//
// Purpose: Hide the details of application data handling from media player
//
//
/////////////////////////////////////////////////////////

#if !defined(APPICATION_DATA_EXT_H)
#define APPICATION_DATA_EXT_H

#include <stdint.h>
#include <deque>
#include <pthread.h>
#include "sail-mediaplayersession-api.h"
#include <dlog.h>
#include <cpe_source.h>
#include <cpe_mediamgr.h>
#include <cpe_common.h>
#include <cpe_sectionfilter.h>
#include "MspCommon.h"
#include "MSPSource.h"
#include "pmt.h"



class ApplicationDataExt
{

public:
    ApplicationDataExt(uint32_t maxSize);
    ~ApplicationDataExt();

    int addData(uint8_t *buffer, uint32_t bufferSize);
    int getData(uint8_t *buffer, uint32_t bufferSize);

    uint32_t getTotalSize();
    uint32_t  mVersionNumber;

private:
    pthread_mutex_t  accessMutex;
    uint32_t maxAllowedSize;
    std::deque <uint8_t>   dataQueue;

};


class IMediaPlayerClientSession
{

public:
    IMediaPlayerClientSession(IMediaPlayerStatusCallback eventStatusCB, uint32_t pid, const tIMediaPlayerSfltFilterGroup *sfltGroup);
    ~IMediaPlayerClientSession();

    ApplicationDataExt *mAppData;
    uint32_t mPid;
    uint32_t  mGuard_word;
    void *mClientAppSfHandle;
    void *mClientAppSfCbIdError;
    void *mClientAppSfCbIdSectionData;
    void *mClientAppSfCbIdTimeOut;
    IMediaPlayerStatusCallback ClientSessionCallback;
    tIMediaPlayerSfltFilterGroup *msfltGroup;
    static const int maxApplicationDataSize = 65536;
    uint32_t SDVClientContext;
    static pthread_mutex_t  AppDataAccess;
private:
    tCpeSrcHandle mSrcHandle;

public:
    eMspStatus filterAppDataPidExt(tCpeSrcHandle srcHandle);
    eMspStatus getApplicationDataExt(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize);
    uint32_t getSDVClentContext();
private:
    static void *appSecFltCallbackFunctionExt(tCpeSFltCallbackTypes type, void* userdata, void* pCallbackSpecific);
    eMspStatus stopAppDataFilterExt();
};

#endif
