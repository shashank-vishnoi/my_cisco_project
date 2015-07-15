/////////////////////////////////////////////////////////
//
//
// Header file for application data container class
//
// Purpose: Hide the details of application data handling from media player
//
//
/////////////////////////////////////////////////////////

#if !defined(APPICATION_DATA_EXT_IC_H)
#define APPICATION_DATA_EXT_IC_H

#include <stdint.h>
#include <deque>
#include <pthread.h>
#include "sail-mediaplayersession-api.h"
#include <dlog.h>
#include "MspCommon.h"
#include "MSPSource.h"
#include "pmt_ic.h"
#include "cgmiPlayerApi.h"

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

    void *mpFilterId;
#if 0
    void *mClientAppSfCbIdError;
    void *mClientAppSfCbIdSectionData;
    void *mClientAppSfCbIdTimeOut;
#endif
    IMediaPlayerStatusCallback ClientSessionCallback;
    tIMediaPlayerSfltFilterGroup *msfltGroup;
    static const int maxApplicationDataSize = 65536;
    uint32_t SDVClientContext;
    static pthread_mutex_t  AppDataAccess;
private:
    void* mCgmiSessionHandle;

public:
    eMspStatus filterAppDataPidExt(void* cgmiSessionHandle);
    eMspStatus getApplicationDataExt(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize);
    uint32_t getSDVClentContext();
private:

    /* Section filter callbacks */
    static cgmi_Status cgmi_SectionBufferCallback(void *pUserData, void *pFilterPriv, void *pFilterId, cgmi_Status sectionStatus, char *pSection, int sectionSize);
    static cgmi_Status cgmi_QueryBufferCallback(void *pUserData, void *pFilterPriv, void *pFilterId, char **ppBuffer, int *pBufferSize);

#if 0
    static void *appSecFltCallbackFunctionExt(tCpeSFltCallbackTypes type, void* userdata, void* pCallbackSpecific);
#endif
    eMspStatus stopAppDataFilterExt();
};

#endif
