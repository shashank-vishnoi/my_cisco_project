///////////////////////////////////////////////////////////
//
//
// Implementation file for application data container class
//
// Purpose: Hide the details of application data handling from media player
//
//
/////////////////////////////////////////////////////////
//
//


#include "ApplicationDataExt.h"
#include <memory.h>


#ifdef LOG
#error  LOG already defined
#endif
#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"ApplicationClient:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

ApplicationDataExt::ApplicationDataExt(uint32_t maxSize)
{
    FNLOG(DL_MSP_MPLAYER);

    dataQueue.clear();
    maxAllowedSize = maxSize;
    mVersionNumber = -1;
    pthread_mutex_init(&accessMutex, NULL);
}

ApplicationDataExt::~ApplicationDataExt()
{

    FNLOG(DL_MSP_MPLAYER);
    pthread_mutex_lock(&accessMutex);
    dataQueue.clear();
    pthread_mutex_unlock(&accessMutex);

}
int ApplicationDataExt::addData(uint8_t *buffer, uint32_t bufferSize)
{

    FNLOG(DL_MSP_MPLAYER);

    pthread_mutex_lock(&accessMutex);
    if ((bufferSize + dataQueue.size()) > maxAllowedSize)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "%s:%d max size exceeded.  drop  old data", __FUNCTION__, __LINE__);
        for (uint32_t i = 0; i < bufferSize; i++)
        {
            dataQueue.pop_front();
        }
    }
    for (uint32_t i = 0; i < bufferSize; i++)
    {
        dataQueue.push_back(*buffer++);
    }
    pthread_mutex_unlock(&accessMutex);

    return bufferSize;
}

int ApplicationDataExt::getData(uint8_t *buffer, uint32_t bufferSize)
{
    uint32_t sizeToCopy = 0;

    FNLOG(DL_MSP_MPLAYER);

    pthread_mutex_lock(&accessMutex);
    sizeToCopy = (bufferSize < dataQueue.size()) ? bufferSize : dataQueue.size();
    for (uint32_t i = 0; i < sizeToCopy; i++)
    {
        *buffer++ = dataQueue.front();
        dataQueue.pop_front();
    }
    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:%d got %d bytes currentsize: %d", __FUNCTION__, __LINE__, sizeToCopy, dataQueue.size());
    pthread_mutex_unlock(&accessMutex);
    return sizeToCopy;
}
uint32_t ApplicationDataExt::getTotalSize()
{
    uint32_t total = 0;

    pthread_mutex_lock(&accessMutex);
    total = dataQueue.size();
    pthread_mutex_unlock(&accessMutex);

    return total;
}

pthread_mutex_t IMediaPlayerClientSession::AppDataAccess;

IMediaPlayerClientSession::IMediaPlayerClientSession(IMediaPlayerStatusCallback eventStatusCB, uint32_t pid, const tIMediaPlayerSfltFilterGroup *sfltGroup)
{
    FNLOG(DL_MSP_MPLAYER);
    ClientSessionCallback = eventStatusCB;
    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Pid value passed on in constructor is %d", pid);
    mPid = pid;
    mAppData = NULL;
    mGuard_word = 0x0A0A0A0A;
    mClientAppSfHandle = NULL;
    mClientAppSfCbIdError = NULL;
    mClientAppSfCbIdSectionData = NULL;
    mClientAppSfCbIdTimeOut = NULL;
    msfltGroup = (tIMediaPlayerSfltFilterGroup*)malloc(sizeof(tIMediaPlayerSfltFilterGroup));
    if (sfltGroup)
    {
        memcpy(msfltGroup, sfltGroup, sizeof(tIMediaPlayerSfltFilterGroup));
    }
    mSrcHandle = NULL;
    SDVClientContext = rand() % 1000;
    pthread_mutex_init(&IMediaPlayerClientSession::AppDataAccess, NULL);

}

IMediaPlayerClientSession::~IMediaPlayerClientSession()
{

    FNLOG(DL_MSP_MPLAYER);
    free(msfltGroup);
}


eMspStatus IMediaPlayerClientSession::filterAppDataPidExt(tCpeSrcHandle srcHandle)
{
    FNLOG(DL_MSP_MPLAYER);
    mSrcHandle = srcHandle;
    uint16_t pid = 0;
    pid = mPid;
    tIMediaPlayerSfltFilterGroup *sfltGroup = NULL;
    sfltGroup = msfltGroup;
    LOG(DLOGL_REALLY_NOISY, "ClientSession::filterAppDataPidExt  pid = %d", pid);

    if (mPid == INVALID_PID_VALUE)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Invalid Pid -- stop filtering on earlier PID");
        eMspStatus status = stopAppDataFilterExt();
        pthread_mutex_lock(&IMediaPlayerClientSession::AppDataAccess);
        if (mAppData)
        {
            delete mAppData;
            mAppData = NULL;
        }
        pthread_mutex_unlock(&IMediaPlayerClientSession::AppDataAccess);
        return status;
    }
    else
    {
        if (mClientAppSfHandle == NULL)
        {
            pthread_mutex_lock(&IMediaPlayerClientSession::AppDataAccess);
            if (mAppData != NULL)       // shouldn't happen, but clear old data if it does
            {
                if (mAppData)
                {
                    delete mAppData;
                    mAppData = NULL;
                }
            }
            mAppData = new ApplicationDataExt(maxApplicationDataSize);
            pthread_mutex_unlock(&IMediaPlayerClientSession::AppDataAccess);

            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Start Section Filter for App Data Pid %p", mClientAppSfHandle);
            int status = cpe_sflt_Open(mSrcHandle, kCpeSFlt_HighBandwidth, &mClientAppSfHandle);
            if (status != kCpe_NoErr)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to open App Data section filter. Err: %d", __FUNCTION__, status);
                return kMspStatus_Error;
            }

            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "DisplaySession::%s: Open App Data section filter. Handle %p", __FUNCTION__, mClientAppSfHandle);
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "DisplaySession::%s Setting the PID value of %d", __FUNCTION__, pid);
            status = cpe_sflt_Set(mClientAppSfHandle, eCpeSFltNames_PID, (void *)&pid);
            if (status != kCpe_NoErr)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to set App Data section filter. Err: %d", __FUNCTION__, status);
                return kMspStatus_Error;
            }
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "DisplaySession::%s Setting the Group Filter", __FUNCTION__);
            status = cpe_sflt_Set(mClientAppSfHandle, eCpeSFltNames_AddFltGrp, (void *)sfltGroup);
            if (status != kCpe_NoErr)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to set App Data Group Section Filter. Err: %d", __FUNCTION__, status);
                return kMspStatus_Error;
            }
            // Register for Callbacks
            cpe_sflt_RegisterCallback(eCpeSFltCallbackTypes_Error, (void *)this, (tCpeSFltCallbackFunction)appSecFltCallbackFunctionExt, &mClientAppSfCbIdError, mClientAppSfHandle);
            cpe_sflt_RegisterCallback(eCpeSFltCallbackTypes_SectionData, (void *)this, (tCpeSFltCallbackFunction)appSecFltCallbackFunctionExt, &mClientAppSfCbIdSectionData, mClientAppSfHandle);
            cpe_sflt_RegisterCallback(eCpeSFltCallbackTypes_Timeout, (void *)this, (tCpeSFltCallbackFunction)appSecFltCallbackFunctionExt, &mClientAppSfCbIdTimeOut, mClientAppSfHandle);

            status = cpe_sflt_Start(mClientAppSfHandle, 0);
            if (status != kCpe_NoErr)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to start App Datasection filter. Err: %d", __FUNCTION__, status);
                return kMspStatus_Error;
            }
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s: start App Data section filter. handle %p", __FUNCTION__, mClientAppSfHandle);
            return kMspStatus_Ok;
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "App data filter (Extension)is already Set");
            return kMspStatus_Error;
        }
    }
}

eMspStatus IMediaPlayerClientSession::stopAppDataFilterExt()
{
    FNLOG(DL_MSP_MPLAYER);

    if (mClientAppSfHandle != 0)
    {
        int cpeStatus = cpe_sflt_Stop(mClientAppSfHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Failed to stop App filter returned error code %d", cpeStatus);
        }

        cpeStatus = cpe_sflt_UnregisterCallback(mClientAppSfHandle, mClientAppSfCbIdError);
        if (cpeStatus != kCpe_NoErr)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Failed to  mClientAppSfCbIdError  returned error code %d", cpeStatus);
        }
        cpeStatus = cpe_sflt_UnregisterCallback(mClientAppSfHandle, mClientAppSfCbIdSectionData);
        if (cpeStatus != kCpe_NoErr)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Failed to mClientAppSfCbIdSectionData returned error code %d", cpeStatus);
        }
        cpeStatus = cpe_sflt_UnregisterCallback(mClientAppSfHandle, mClientAppSfCbIdTimeOut);
        if (cpeStatus != kCpe_NoErr)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Failed to mClientAppSfCbIdTimeOut returned error code %d", cpeStatus);
        }

        cpeStatus  = cpe_sflt_Close(mClientAppSfHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to close APP section filter. Err: %d", __FUNCTION__, cpeStatus);
            return kMspStatus_Error;
        }
        mClientAppSfHandle = 0;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "ExtDataFilter is alreay stopped");
    }

    return kMspStatus_Ok;
}
void *IMediaPlayerClientSession::appSecFltCallbackFunctionExt(tCpeSFltCallbackTypes type, void* userdata, void* pCallbackSpecific)
{
    IMediaPlayerClientSession *inst = (IMediaPlayerClientSession *)userdata;
    tCpeSFltBuffer *secFiltData = (tCpeSFltBuffer *)pCallbackSpecific;

    FNLOG(DL_MSP_MPLAYER);

    switch (type)
    {
    case eCpeSFltCallbackTypes_SectionData: ///< Callback returns the section data to the caller
    {
        if (secFiltData && secFiltData->length != 0 && inst)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:%d secfilt data %d %p", __FUNCTION__, __LINE__, secFiltData->length, secFiltData);

            tTableHeader *header  = (tTableHeader *)secFiltData->pBuffer;
            if (header == NULL)
            {
                LOG(DLOGL_ERROR, "%sError: Null header ", __FILE__);
                return NULL;
            }

            pthread_mutex_lock(&AppDataAccess);

            if (inst->mAppData)
            {
                LOG(DLOGL_REALLY_NOISY, "New section data with version number %d", header->VersionNumber);
                inst->mAppData->mVersionNumber = header->VersionNumber;
                inst->mAppData->addData(secFiltData->pBuffer, secFiltData->length);
                //Copy callback data to a local copy so that it can be called outside the lock
                IMediaPlayerStatusCallback localCallback = inst->ClientSessionCallback;
                pthread_mutex_unlock(&AppDataAccess); //Unlock the Mutex

                if (localCallback)
                {
                    tIMediaPlayerCallbackData cbData;
                    cbData.data[0] = '\0';
                    //LOG(DLOGL_NOISE, "signal %d status %d", sig, stat);
                    cbData.status = kMediaPlayerStatus_Ok;
                    cbData.signalType = kMediaPlayerSignal_ApplicationData;
                    localCallback(NULL, cbData, NULL, NULL);
                }
                else
                {
                    LOG(DLOGL_ERROR, "%s: (No callback to SDV) Local Instance of IMediaPlayerClientSession is NULL !!!", __FUNCTION__);
                }
            }  //End of if(inst->mAppData)
            else
            {
                LOG(DLOGL_ERROR, "(%s) Did Not get any AppData from sectionFilterCallback", __FUNCTION__);
                pthread_mutex_unlock(&AppDataAccess); //Unlock the Mutex
            }
        }
    }
    break;

    case eCpeSFltCallbackTypes_Timeout:   ///< Read operation timed out.
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s:%d Section Filter Timeout", __FUNCTION__, __LINE__);
    }
    break;

    case eCpeSFltCallbackTypes_Error:           ///< Error occured during read operation.
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s:%d Section Filter Error", __FUNCTION__, __LINE__);
        break;

    default:
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:%d Unknown event %d from source callback", __FUNCTION__, __LINE__, type);
        break;

    } //end of switch

    return NULL;
}

eMspStatus IMediaPlayerClientSession::getApplicationDataExt(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    FNLOG(DL_MSP_MPLAYER);

    eMspStatus status = kMspStatus_Ok;
    LOG(DLOGL_REALLY_NOISY, "Guard_Word = %x", mGuard_word);
    if (mGuard_word != 0x0A0A0A0A)
    {
        LOG(DLOGL_ERROR, "Oudated call for getting the data");
        return kMspStatus_StateError;
    }

    pthread_mutex_lock(&IMediaPlayerClientSession::AppDataAccess);

    if (dataSize == NULL)
    {
        LOG(DLOGL_ERROR, "dataSize pointer is NULL");
        status = kMspStatus_BadParameters;
    }
    else if ((bufferSize == 0) || (buffer == NULL)) // just requesting size with this call
    {
        if (mAppData)
        {
            *dataSize = mAppData->getTotalSize();
        }
        else
        {
            LOG(DLOGL_ERROR, "Error: (Line%d) ApplicationDataExt Instance is NULL", __LINE__);
        }
    }
    else
    {
        if (mAppData)
        {
            *dataSize = mAppData->getData(buffer, bufferSize);
        }
        else
        {
            LOG(DLOGL_ERROR, "Error: (Line%d) ApplicationDataExt Instance is NULL", __LINE__);
        }
    }
    pthread_mutex_unlock(&IMediaPlayerClientSession::AppDataAccess);
    return status;
}

uint32_t IMediaPlayerClientSession::getSDVClentContext()
{
    return SDVClientContext;
}

