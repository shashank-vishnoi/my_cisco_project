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


#include "ApplicationDataExt_ic.h"
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
    mpFilterId = NULL;
    msfltGroup = (tIMediaPlayerSfltFilterGroup*)malloc(sizeof(tIMediaPlayerSfltFilterGroup));
    if (sfltGroup)
    {
        memcpy(msfltGroup, sfltGroup, sizeof(tIMediaPlayerSfltFilterGroup));
    }
    mCgmiSessionHandle = NULL;
    SDVClientContext = rand() % 1000;
    pthread_mutex_init(&IMediaPlayerClientSession::AppDataAccess, NULL);

}

IMediaPlayerClientSession::~IMediaPlayerClientSession()
{
    FNLOG(DL_MSP_MPLAYER);
    free(msfltGroup);
}


eMspStatus IMediaPlayerClientSession::filterAppDataPidExt(void* pCgmiSessHandle)
{
    FNLOG(DL_MSP_MPLAYER);
    mCgmiSessionHandle = pCgmiSessHandle;
    uint16_t pid = 0;
    pid = mPid;
    tIMediaPlayerSfltFilterGroup *sfltGroup = NULL;
    sfltGroup = msfltGroup;
    LOG(DLOGL_REALLY_NOISY, "IMediaPlayerClientSession::filterAppDataPidExt  pid = %d", pid);

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
        if (mpFilterId == NULL)
        {
            pthread_mutex_lock(&IMediaPlayerClientSession::AppDataAccess);
            if (mAppData != NULL)       // shouldn't happen, but clear old data if it does
            {
                delete mAppData;
                mAppData = NULL;
            }

            mAppData = new ApplicationDataExt(maxApplicationDataSize);
            pthread_mutex_unlock(&IMediaPlayerClientSession::AppDataAccess);

            cgmi_Status retCode = CGMI_ERROR_SUCCESS;
            retCode = cgmi_CreateSectionFilter(mCgmiSessionHandle, pid, mCgmiSessionHandle, &mpFilterId);
            if (retCode != CGMI_ERROR_SUCCESS)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s:CGMI CreateSectionFilter Failed\n", __FUNCTION__);
                return kMspStatus_Error;
            }

            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:Open App Data section filter. Handle %p", __FUNCTION__, mpFilterId);
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:Setting the PID value of %d", __FUNCTION__, pid);

            tcgmi_FilterData filterdata;

            filterdata.value = NULL;
            filterdata.mask = NULL;
            filterdata.length = 0;
            filterdata.comparitor = FILTER_COMP_EQUAL;

            retCode = cgmi_SetSectionFilter(mCgmiSessionHandle, mpFilterId, &filterdata);
            if (retCode != CGMI_ERROR_SUCCESS)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: CGMI SetSectionFilter Failed\n", __FUNCTION__);
                return kMspStatus_Error;
            }

            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s Setting the Group Filter", __FUNCTION__);

            retCode = cgmi_StartSectionFilter(mCgmiSessionHandle, mpFilterId, 10, 0, 0, cgmi_QueryBufferCallback, cgmi_SectionBufferCallback);
            if (retCode != CGMI_ERROR_SUCCESS)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: CGMI StartSectionFilter Failed\n", __FUNCTION__);
                return kMspStatus_Error;
            }

            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s: start App Data section filter. handle %p", __FUNCTION__, mpFilterId);

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

    if (mpFilterId != NULL)
    {
        cgmi_Status retCode = CGMI_ERROR_SUCCESS;
        retCode = cgmi_StopSectionFilter(mCgmiSessionHandle, mpFilterId);
        if (retCode != CGMI_ERROR_SUCCESS)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: cgmi_StopSectionFilter Failed\n", __FUNCTION__);
        }

        retCode = cgmi_DestroySectionFilter(mCgmiSessionHandle, mpFilterId);
        if (retCode != CGMI_ERROR_SUCCESS)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: cgmi_DestroySectionFilter Failed\n", __FUNCTION__);
        }

        mpFilterId = NULL;
    }
    else
    {
        LOG(DLOGL_ERROR, "ExtDataFilter is alreay stopped");
    }

    return kMspStatus_Ok;
}

eMspStatus IMediaPlayerClientSession::getApplicationDataExt(uint32_t bufferSize, uint8_t * buffer, uint32_t * dataSize)
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
    else if (mAppData == NULL)
    {
        LOG(DLOGL_ERROR, "Error: (Line%d) ApplicationDataExt Instance is NULL", __LINE__);
    }
    else if ((bufferSize == 0) || (buffer == NULL)) // just requesting size with this call
    {
        *dataSize = mAppData->getTotalSize();
    }
    else
    {
        *dataSize = mAppData->getData(buffer, bufferSize);
    }

    pthread_mutex_unlock(&IMediaPlayerClientSession::AppDataAccess);
    return status;
}

uint32_t IMediaPlayerClientSession::getSDVClentContext()
{
    return SDVClientContext;
}

/****************************  Section Filter Callbacks  *******************************/
cgmi_Status IMediaPlayerClientSession::cgmi_QueryBufferCallback(void * pUserData, void * pFilterPriv, void * pFilterId, char **ppBuffer, int * pBufferSize)
{

    (void) pFilterPriv;
    (void) pFilterId;

    FNLOG(DL_MSP_MPLAYER);

    IMediaPlayerClientSession *inst = (IMediaPlayerClientSession *) pUserData;
    if (inst == NULL)
    {
        LOG(DLOGL_ERROR, "Invalid user data\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    // Check if a size of greater than zero was provided, use default if not
    if (*pBufferSize <= 0)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Making the size to Minimum \n");
        *pBufferSize = 256;
    }

    // Allocate buffer of size *pBufferSize
    *ppBuffer = (char *) malloc(*pBufferSize);
    if (NULL == *ppBuffer)
    {
        *pBufferSize = 0;
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Memory allocation failed...\n");
        return CGMI_ERROR_OUT_OF_MEMORY;
    }

    return CGMI_ERROR_SUCCESS;
}

cgmi_Status IMediaPlayerClientSession::cgmi_SectionBufferCallback(void * pUserData, void * pFilterPriv, void * pFilterId, cgmi_Status sectionStatus, char * pSection, int sectionSize)
{
    (void) sectionStatus;
    (void) pFilterPriv;

    int i = 0;

    IMediaPlayerClientSession *inst = (IMediaPlayerClientSession *) pUserData;
    if (inst == NULL)
    {
        LOG(DLOGL_ERROR, "Invalid user data\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    FNLOG(DL_MSP_MPLAYER);

    if (NULL == pSection)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "NULL section passed to cgmiSectionBufferCallback.\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Received section pFilterId: %p, sectionSize %d\n\n", pFilterId, sectionSize);
    for (i = 0; i < sectionSize; i++)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "0x%x ", (unsigned char) pSection[i]);
    }
    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "\n");

    if (sectionSize != 0 && inst)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:%d secfilt data %d %p", __FUNCTION__, __LINE__, sectionSize, pSection);

        tTableHeader *header  = (tTableHeader *) pSection;
        if (header == NULL)
        {
            LOG(DLOGL_ERROR, "%sError: Null header ", __FILE__);
            return CGMI_ERROR_BAD_PARAM;
        }

        pthread_mutex_lock(&AppDataAccess);

        if (inst->mAppData)
        {
            LOG(DLOGL_REALLY_NOISY, "New section data with version number %d", header->VersionNumber);
            inst->mAppData->mVersionNumber = header->VersionNumber;
            inst->mAppData->addData((uint8_t *)pSection, sectionSize);

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
    return CGMI_ERROR_SUCCESS;
}


