/**
   \file psi.cpp
   \class psi

Implementation file for psi class
*/


///////////////////////////////////////////////////////////////////////////
//                          Includes
///////////////////////////////////////////////////////////////////////////

#include "psi.h"
#include "pmt.h"
#include "assert.h"
#include <arpa/inet.h>

#include <cpe_error.h>
#include <cpe_sectionfilter.h>
#include "cpe_common.h"
#if defined(DMALLOC)
#include "dmalloc.h"
#endif

#include <dlog.h>
#include "eventQueue.h"
#include "crc32.h"
#include "MusicAppData.h"

#include"pthread_named.h"
#include "psiUtils.h"

///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////
#define ISO_639_LANG_TAG 10

#define UNUSED_PARAM(a) (void)a;


#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_PSI, level,"Psi:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

///////////////////////////////////////////////////////////////////////////
//                      Member functions implementation
///////////////////////////////////////////////////////////////////////////

/** *********************************************************
*/

void* Psi::eventthreadFunc(void *data)
{
    // eventthreadFunc is static class method running in a pthread

    FNLOG(DL_MSP_PSI);

    Psi *inst = (Psi *)data;
    if (inst)
    {
        MSPEventQueue* eventQueue = inst->psiThreadEventQueue;
        if (eventQueue)
        {
            bool done = false;

            while (!done)
            {
                unsigned int waitTime = 0;
                if (inst->mState == kPsiWaitForPat || inst->mState == kPsiWaitForPmt)
                {
                    waitTime = 5;
                }
                eventQueue->setTimeOutSecs(waitTime);
                Event *evt = eventQueue->popEventQueue();
                eventQueue->unSetTimeOut();
                if (evt)
                {
                    LOG(DLOGL_REALLY_NOISY, "dispatch event %d", evt->eventType);
                    inst->lockMutex();
                    done = inst->dispatchEvent(evt);  // call member function in class instance to handle event
                    eventQueue->freeEvent(evt);
                    inst->unlockMutex();
                }
            }
        }
    }
    pthread_exit(NULL);
    return NULL;
}

/** *********************************************************
 */
bool Psi::dispatchEvent(Event *evt)
{
    // TODO:  Update naming to show error cases that will quit processing
    //        such as kPsiTimeOutEvent
    FNLOG(DL_MSP_PSI);
    if (evt == NULL)
    {
        LOG(DLOGL_ERROR, "Error: Null evt");
        return false;
    }
    eMspStatus status;
    bool exitThread = false;
    unsigned int psiEvent = evt->eventType;

    switch (psiEvent)
    {
    case kPsiExitEvent:
        freePsi();
        if (mPmt)
        {
            delete mPmt;
            mPmt = NULL;
        }
        exitThread = true;
        break;

    case kPsiTimeOutEvent:
        if (mState == kPsiWaitForPat)
        {
            LOG(DLOGL_REALLY_NOISY, "Time out while waiting for PAT");
        }
        if (mState == kPsiWaitForPmt)
        {
            LOG(DLOGL_REALLY_NOISY, "Time out while waiting for PMT");
        }
        callbackToClient(kPSITimeOut);
        break;

    case kPsiStartEvent:
        // mPmtPid needs to be zero (PAT PID number) when here
        // since we may be re-starting PSI on a different channel, reset it here
        mPmtPid = 0;
        LOG(DLOGL_REALLY_NOISY, "kPsiStartEvent, mPmtPid %d", mPmtPid);

        status = startSectionFilter(mPmtPid);
        if (status != kMspStatus_Ok)
        {
            callbackToClient(kPSIError);
            // exitThread = true;
        }
        mState = kPsiWaitForPat;
        break;

    case kPsiSFCallbackEvent:
        LOG(DLOGL_REALLY_NOISY, "kPsiEventSFCallback");
        status = parseSectionFilterCallBackData();
        if (status != kMspStatus_Ok)
        {
            callbackToClient(kPSIError);
            // exitThread = true;
        }
        break;

    case kPsiPATReadyEvent:
        LOG(DLOGL_REALLY_NOISY, "kPsiPATReadyEvent");
        //psiStop();
        status = startSectionFilter(mPmtPid);
        if (status != kMspStatus_Ok)
        {
            callbackToClient(kPSIError);
            // exitThread = true;
        }
        mState = kPsiWaitForPmt;
        break;

    case kPsiPMTReadyEvent:
        LOG(DLOGL_REALLY_NOISY, "kPsiPMTReadyEvent - wait for update");
        callbackToClient(kPSIReady);
        status = startSectionFilter(mPmtPid, true);
        // TODO:  check/act on status
        mState = kPsiWaitForUpdate;
        break;

    case kPsiUpdateEvent:
        LOG(DLOGL_REALLY_NOISY, "PSI is updated\n");
        callbackToClient(kPSIUpdate);
        status = startSectionFilter(mPmtPid, true);
        // TODO:  check/act on status
        mState = kPsiWaitForUpdate;
        break;
    case kPsiFileSrcPMTReady:
        LOG(DLOGL_REALLY_NOISY, "File source/HTTP source PMT ready");
        callbackToClient(kPSIReady);
        break;

    case kPsiRevUpdateEvent:
        //psiStop();
        LOG(DLOGL_REALLY_NOISY, "Change in PMT revision.But not audio/video Pids");
        status = startSectionFilter(mPmtPid, true);
        callbackToClient(kPmtRevUpdate);
        mState = kPsiWaitForUpdate;
        break;
    }

    return  exitThread;
}


void Psi::callbackToClient(ePsiCallBackState state)
{
    mCbState = state;  // store last CB state

    if (mCallbackFn)
    {
        mCallbackFn(mCbState, mCbClientContext);
        LOG(DLOGL_REALLY_NOISY, "mCbState: %d", mCbState);
    }
    else
    {
        LOG(DLOGL_NOISE, "warning null mCallbackFn");
    }
}


/** *********************************************************
 */
eMspStatus Psi::processCaMetaDataDescriptor(uint8_t *buffer, uint32_t size)
{
    LOG(DLOGL_REALLY_NOISY, "buffer: %p size: %d", buffer, size);

    if (!buffer)
    {
        LOG(DLOGL_ERROR, "Error null buffer");
        return kMspStatus_Error;
    }


    caDescriptorPtr = (uint8_t *)malloc(size);
    if (caDescriptorPtr)
    {
        memcpy(caDescriptorPtr, buffer, size);
        caDescriptorLength = size;
        return kMspStatus_Ok;
    }
    else
    {
        LOG(DLOGL_ERROR, "Error allocating %d bytes", size);
        return  kMspStatus_OutofMemory;
    }

}

/** *********************************************************
 */


eMspStatus Psi::createSaraCaMetaDataDescriptor()
{
    FNLOG(DL_MSP_PSI);

    // Populate with data as done in PkCakDvrRecordSession::getDvrMetadata

    caDescriptorLength = 4;
    caDescriptorPtr = (uint8_t *)calloc(1, caDescriptorLength);

    if (!caDescriptorPtr)
    {
        LOG(DLOGL_EMERGENCY, "error alloc %d bytes", caDescriptorLength);
        caDescriptorLength = 0;
        return kMspStatus_OutofMemory;
    }

    caDescriptorPtr[0] = 0x0e;

    return kMspStatus_Ok;
}


/** *********************************************************
 */
eMspStatus Psi::processCaMetaDataBlob(uint8_t *buffer, uint32_t size)
{
    LOG(DLOGL_REALLY_NOISY, "buffer: %p size: %d", buffer, size);

    if (!buffer)
    {
        LOG(DLOGL_ERROR, "Error null buffer");
        return kMspStatus_Error;
    }


    caMetaDataPtr = (uint8_t *)malloc(size);
    if (caMetaDataPtr)
    {
        memcpy(caMetaDataPtr, buffer, size);
        caMetaDataSize = size;
        return  kMspStatus_Ok;
    }
    else
    {
        LOG(DLOGL_ERROR, "Error allocating %d bytes", size);
        return  kMspStatus_OutofMemory;
    }
}



/** *********************************************************
 */
eMspStatus Psi::processMSPMetaData(uint8_t *buffer, uint32_t size)
{
    return getPmtObj()->populateMSPMetaData(buffer, size);
}



/** *********************************************************
 */
eMspStatus Psi::processSaraMetaData(uint8_t *buffer, uint32_t size)
{
    return getPmtObj()->populateFromSaraMetaData(buffer, size);
}

eMspStatus Psi::processCaptionServiceMetaDataDescriptor(uint8_t *buffer, uint32_t size)
{
    return getPmtObj()->populateCaptionServiceMetadata(buffer, size);
}

/** *********************************************************
 */

eMspStatus Psi::psiStart(std::string recordUrl)
{
    tCpeRecDataBase *metabuf;
    FILE *fp;
    std::string recfile;
    size_t result = 0;

    FNLOG(DL_MSP_PSI);

    if (mPmt == NULL)
    {
        LOG(DLOGL_ERROR, "Error: null mPmt");
        return kMspStatus_Error;
    }
    lockMutex();

    // Clear the previous PMT MetaData if any, since we are calling psiStart more than once in same session
    mPmt->freePmtInfo();
    mPmt->mVideoPid.clear();
    mPmt->mAudioPid.clear();
    // ToDO:Move all free to one common function
    freePsi();

    if (psiEventHandlerThread)
    {
        LOG(DLOGL_NORMAL, ":  PSI event thread already started for this session");
    }

    else                            //RF source case
    {

        pthread_attr_t attr;
        pthread_attr_init(&attr);  // sets default - thread is joinable

        //TODO: Consider if 256 KB is really needed
        const int THREAD_STACK_SIZE = 256 * 1024; // 256K

        pthread_attr_setstacksize(&attr, THREAD_STACK_SIZE);

        int threadRetvalue = pthread_create(&psiEventHandlerThread,
                                            &attr,
                                            eventthreadFunc,
                                            (void *) this);

        if (!threadRetvalue)
        {
            threadRetvalue = pthread_setname_np(psiEventHandlerThread, "MSP_PSI_Event_Handler");
            if (threadRetvalue)
            {
                LOG(DLOGL_ERROR, "Error setting threadname retVal: %d", threadRetvalue);
            }

        }
        else
        {
            LOG(DLOGL_ERROR, "pthread_create error %d\n", threadRetvalue);
            unlockMutex();
            return kMspStatus_PsiThreadError;
        }

    }

    recfile = recordUrl.substr(strlen("avfs://"));

    const char* recFilename =  recfile.c_str();

    LOG(DLOGL_NOISE, "open %s", recFilename);

    fp = fopen(recFilename, "rb");
    if (!fp)
    {
        LOG(DLOGL_ERROR, "error opening %s", recFilename);
        unlockMutex();
        return kMspStatus_Error;
    }

    // obtain file size:
    fseek(fp, 0, SEEK_END);

    int metasize = ftell(fp);
    if (metasize <= 0)
    {
        LOG(DLOGL_ERROR, "error: file: %s size: %d", recFilename, metasize);
        fclose(fp);
        fp = NULL;
        unlockMutex();
        return kMspStatus_Error;
    }

    // TODO: check this - why adding 1024 bytes?
    metabuf = (tCpeRecDataBase *)malloc(metasize + 1024);
    if (!metabuf)
    {
        LOG(DLOGL_ERROR, "error allocating %d bytes", metasize + 1024);
        fclose(fp);
        fp = NULL;
        unlockMutex();
        return kMspStatus_OutofMemory;
    }

    rewind(fp);

    result = fread(metabuf, 1, (unsigned int)metasize, fp);
    if (result != (unsigned int)metasize)
    {
        LOG(DLOGL_ERROR, "error fread %s read %d bytes - expected %d", recFilename, result, metasize);
        free(metabuf);
        metabuf = NULL;
        fclose(fp);
        fp = NULL;
        unlockMutex();
        return kMspStatus_Error;
    }

    fclose(fp);
    fp = NULL;

    LOG(DLOGL_REALLY_NOISY, "dbHdr.dbCounts: %d", metabuf->dbHdr.dbCounts);

    if (metabuf->dbHdr.dbCounts > kCpeRec_DataBaseEntries)
    {
        LOG(DLOGL_ERROR, "Error dbCounts: %d > kCpeRec_DataBaseEntries", metabuf->dbHdr.dbCounts);
        free(metabuf);
        metabuf = NULL;
        unlockMutex();
        return kMspStatus_Error;
    }

    bool createSaraCaDescriptor = false;

    for (int i = 0; i < metabuf->dbHdr.dbCounts; i++)
    {
        uint32_t offset         =  metabuf->dbEntry[i].offset;
        uint8_t *sectionAddress = (uint8_t *)(metabuf) + offset;
        uint32_t tag            =   metabuf->dbEntry[i].tag;
        uint32_t metadataSize   =  metabuf->dbEntry[i].size;

        LOG(DLOGL_REALLY_NOISY, "[%d] tag: 0x%x  offset: 0x%x  sectionAddress: %p  metadataSize: %d",
            i,  tag, offset, sectionAddress, metadataSize);

        switch (tag)
        {
        case 0x1FAB:
            processMSPMetaData(sectionAddress, metadataSize);
            break;

        case 0x102:   // old SARA tag
            processSaraMetaData(sectionAddress, metadataSize);
            break;

        case 0x1FAC:
            processCaMetaDataBlob(sectionAddress, metadataSize);
            break;

        case 0x101:  // old SARA tag
            processCaMetaDataBlob(sectionAddress, metadataSize);
            createSaraCaDescriptor = true;
            break;

        case 0x1FAD:
            processCaMetaDataDescriptor(sectionAddress, metadataSize);
            break;

        case 0x1FAE:
            processCaptionServiceMetaDataDescriptor(sectionAddress, metadataSize);
            break;

        default:
            LOG(DLOGL_ERROR, "warning: unknown metadata tag: 0x%x offset: 0x%x", tag, offset);
        }
    }

    if (createSaraCaDescriptor)
    {
        createSaraCaMetaDataDescriptor();
    }

    free(metabuf);
    metabuf = NULL;

    LOG(DLOGL_NOISE, "Queueing File source Ready event");
    queueEvent(kPsiFileSrcPMTReady);


    unlockMutex();

    return kMspStatus_Ok;
}

/** *********************************************************
 */

eMspStatus Psi::psiStart(const MSPSource *aSource)
{
    eMspStatus status = kMspStatus_Ok;

    FNLOG(DL_MSP_PSI);
    if (mPmt == NULL)
    {
        LOG(DLOGL_ERROR, "Error: null mPmt");
        return kMspStatus_Error;
    }
    lockMutex();

    // Clear the previous PMT MetaData if any, since we are calling psiStart more than once in same session
    mPmt->freePmtInfo();
    mPmt->mVideoPid.clear();
    mPmt->mAudioPid.clear();
    // ToDO:Move all free to one common function
    freePsi();


    if (!aSource)
    {
        LOG(DLOGL_ERROR, "Warning: null source");
        unlockMutex();
        return kMspStatus_Error;
    }

    if (psiEventHandlerThread)
    {
        LOG(DLOGL_NORMAL, ":  PSI event thread already started for this channel");
    }
    else                            //RF source case
    {
        pthread_attr_t attr;
        pthread_attr_init(&attr);  // sets default - thread is joinable

        //TODO: Consider if 256 KB is really needed
        const int THREAD_STACK_SIZE = 256 * 1024; // 256K

        pthread_attr_setstacksize(&attr, THREAD_STACK_SIZE);

        int threadRetvalue = pthread_create(&psiEventHandlerThread,
                                            &attr,
                                            eventthreadFunc,
                                            (void *) this);

        if (!threadRetvalue)
        {
            threadRetvalue = pthread_setname_np(psiEventHandlerThread, "MSP_PSI_Event_Handler");
            if (threadRetvalue)
            {
                LOG(DLOGL_ERROR, "Error setting threadname retVal: %d", threadRetvalue);
            }

        }
        else
        {
            LOG(DLOGL_ERROR, "pthread_create error %d\n", threadRetvalue);
            unlockMutex();
            return kMspStatus_PsiThreadError;
        }

    }

    if (aSource->isDvrSource())      //MRDVR Remote file source case
    {
        status = ParsePsiRemoteSource(aSource);

        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Parsing PSI from remote source failed for MRDVR playback");
            status = kMspStatus_Error;
        }
        else
        {
            LOG(DLOGL_NOISE, "Parsing PSI data from HTTP source success");
            queueEvent(kPsiFileSrcPMTReady);
            status = kMspStatus_Ok;
        }
    }
    else                            //RF source case
    {
        mPgmNo     = aSource->getProgramNumber();
        mSrcHandle = aSource->getCpeSrcHandle();
        queueEvent(kPsiStartEvent);
    }

    unlockMutex();

    return  status;
}



eMspStatus Psi::ParsePsiRemoteSource(const MSPSource *aSource)
{

    uint16_t  sectionLength;
    //tPsiPmt pmt_data;
    int cpeStatus;
    eMspStatus status = kMspStatus_Error;
    int mediaType = 0;
    uint8_t *pmt_data;

    if (aSource == NULL)
    {
        dlog(DL_MSP_PSI, DLOGL_ERROR, "Error: Null aSource\n");
        return kMspStatus_Error;
    }
    mSrcHandle = aSource->getCpeSrcHandle();

    cpeStatus = cpe_src_Get(mSrcHandle, eCpeSrcNames_MediaType, &mediaType, sizeof(tCpeSrcMediaType));
    if (cpeStatus != kCpe_NoErr)
    {
        dlog(DL_MSP_PSI, DLOGL_NOISE, "%s: cpe_src_Get(eCpeSrcNames_MediaType) failed, error=%d\n", __FUNCTION__, cpeStatus);
        return kMspStatus_Error;
    }

    dlog(DL_MSP_PSI, DLOGL_NOISE, "mediatype got from source is %d\n", mediaType);



    pmt_data = (uint8_t *)malloc(1024);

    cpeStatus = cpe_src_Get(mSrcHandle, eCpeSrcNames_PMT, (void*)pmt_data, 1024);
    if (cpeStatus != kCpe_NoErr)
    {
        dlog(DL_MSP_PSI, DLOGL_ERROR, "Unable to get the PMT data from the MRDVR source\n");

        return kMspStatus_Error;
    }
    else
    {
        dlog(DL_MSP_PSI, DLOGL_NOISE, " pmt data fetching success\n");

    }

    tPsiPmt *pmt = (tPsiPmt *)pmt_data;
    if (pmt)
    {
        sectionLength = pmt->sectionLengthHi << 8 | pmt->sectionLengthLo;

        mPSecFilterBuf = (uint8_t *) pmt_data;           //make the sectionf filter buffer pointer to point the raw PMT data,we got from the remote source.
        status = collectPmtData(sectionLength);
        if (status != kMspStatus_Ok)
        {
            dlog(DL_MSP_PSI, DLOGL_ERROR, "Unable to collect the PMT information from Remote source PMT data");
            return kMspStatus_Error;
        }
        mPmt->mPmtInfo.versionNumber = pmt->versionNumber;
    }
    else
    {
        dlog(DL_MSP_PSI, DLOGL_ERROR, "Error: Null pmat data\n");
        return kMspStatus_Error;
    }
    return kMspStatus_Ok;
}




/** *********************************************************
 */
eMspStatus Psi::startSectionFilter(uint16_t pid, bool aPsiUpdateFlag)
{
    FNLOG(DL_MSP_PSI);

    LOG(DLOGL_REALLY_NOISY, "pid: %d  aPsiUpdateFlag: %d  deletePsiRequested: %d", pid, aPsiUpdateFlag, mDeletePsiRequested);

    int16_t ExpectedTid = kAppPatId;
    if (pid)
        ExpectedTid = kAppPmtId;
    else
        ExpectedTid = kAppPatId;

    if (mDeletePsiRequested)
    {
        LOG(DLOGL_NORMAL, "warning: not setting PSI - delete requrested");
        return kMspStatus_Ok;
    }

    eMspStatus status = kMspStatus_Ok;

    // OPEN
    // Open a section filter to read mpeg section data.
    // Actual data is received in callback.
    int err = cpe_sflt_Open(mSrcHandle, kCpeSFlt_HighBandwidth, &mPgrmHandleSf);
    if (!err)
    {
        mState = kPsiStateOpened;
        LOG(DLOGL_REALLY_NOISY, "cpe_sflt_Open success  mPgrmHandleSf: %p", mPgrmHandleSf);
    }
    else
    {
        LOG(DLOGL_ERROR, "cpe_sflt_Open error: %d", err);
        status = kMspStatus_CpeSecionFilterError;
    }


    // SET PID
    if (status == kMspStatus_Ok)
    {
        // call cpe_sflt_Set with eCpeSFltNames_PID to set PID for the the section filter
        int err = cpe_sflt_Set(mPgrmHandleSf, eCpeSFltNames_PID, (void *)&pid);
        if (!err)
        {
            // TODO: should this be part of the state??
            mSfStarted = true;
            LOG(DLOGL_REALLY_NOISY, "cpe_sflt_Set PID %d success  mPgrmHandleSf: %p", pid, mPgrmHandleSf);
        }
        else
        {
            LOG(DLOGL_ERROR, "cpe_sflt_Set PID (%d) error: %d   mPgrmHandleSf: %p", pid, err, mPgrmHandleSf);
            status = kMspStatus_CpeSecionFilterError;
        }
    }


    // Enable CRC checking
    if (status == kMspStatus_Ok)
    {

        bool enable_crc = 1;

        // call cpe_sflt_Set with eCpeSFltNames_PID to set PID for the the section filter
        int err = cpe_sflt_Set(mPgrmHandleSf, eCpeSFltNames_CRC, (void *)&enable_crc);
        if (!err)
        {
            LOG(DLOGL_REALLY_NOISY, "cpe_sflt_Set CRC check success  mPgrmHandleSf: %p", mPgrmHandleSf);
        }
        else
        {
            LOG(DLOGL_ERROR, "cpe_sflt_Set CRC check (%d) error: %d   mPgrmHandleSf: %p", pid, err, mPgrmHandleSf);
            status = kMspStatus_CpeSecionFilterError;
        }
    }


    // SET FILTER (IF UPDATE FLAG SET)
    if (status == kMspStatus_Ok && aPsiUpdateFlag)
    {
        status = setSectionFilterGroup();
    }

    // REGISTER FOR CALLBACKS
    if (status == kMspStatus_Ok)
    {
        err = registerSfCallbacks();
        if (!err)
        {
            LOG(DLOGL_REALLY_NOISY, "sf callbacks registered");
        }
        else
        {
            LOG(DLOGL_ERROR, "error registering sf callbacks");
            status = kMspStatus_CpeSecionFilterError;
        }
    }

    // START SECTION FILTER STREAMING
    if (status == kMspStatus_Ok)
    {
        // Start section filter streaming (callbacks must be set first)
        err = cpe_sflt_Start(mPgrmHandleSf, 0);
        if (!err)
        {
            // TODO: should this be part of the state??
            mSfStarted = true;
            LOG(DLOGL_REALLY_NOISY, "cpe_sflt_Start success  mPgrmHandleSf: %p", mPgrmHandleSf);
        }
        else
        {
            LOG(DLOGL_ERROR, "cpe_sflt_Start error: %d   mPgrmHandleSf: %p", err, mPgrmHandleSf);
            status = kMspStatus_CpeSecionFilterError;
        }
    }

    return status;
}






/**
 * Info on psiStopr*********************************************************
 *
 * @return eMspStatus
 */
eMspStatus Psi::psiStop()
{
    eMspStatus status = kMspStatus_Ok;
    FNLOG(DL_MSP_PSI);

    if (mPgrmHandleSf)
    {
        if (mSfStarted)
        {
            int err = cpe_sflt_Stop(mPgrmHandleSf);
            if (!err)
            {
                LOG(DLOGL_REALLY_NOISY, "cpe_sflt_Stop success");
                // mState = kPsiStateStopped;
                mSfStarted = false;
            }
            else
            {
                LOG(DLOGL_ERROR, "cpe_sflt_Stop error %d, mPgrmHandleSf: %p", err, mPgrmHandleSf);
                status = kMspStatus_CpeSecionFilterError;
                // TODO:  Shouldn't state still be Stopped ?
            }
        }

        if (mSfCbIdError)
        {
            cpe_sflt_UnregisterCallback(mPgrmHandleSf, mSfCbIdError);
        }
        if (mSfCbIdSectionData)
        {
            cpe_sflt_UnregisterCallback(mPgrmHandleSf, mSfCbIdSectionData);
        }
        if (mSfCbIdTimeOut)
        {
            cpe_sflt_UnregisterCallback(mPgrmHandleSf, mSfCbIdTimeOut);
        }


        int err = cpe_sflt_Close(mPgrmHandleSf);
        if (!err)
        {
            LOG(DLOGL_REALLY_NOISY, "cpe_sflt_Close success!");
            // mState = kPsiStateClosed;
            mPgrmHandleSf = NULL;
        }
        else
        {
            LOG(DLOGL_ERROR, "cpe_sflt_Close error: %d  mPgrmHandleSf: %p", err, mPgrmHandleSf);
            status = kMspStatus_CpeSecionFilterError;

            // TODO:  Shouldn't state still be Closed ?
        }

    }


    return  status;
}

/** *********************************************************
 */
void Psi::registerPsiCallback(psiCallbackFunction cb_fun, void* clientData)
{
    FNLOG(DL_MSP_PSI);

    assert(cb_fun && clientData);

    mCallbackFn = cb_fun;
    mCbClientContext = clientData;
}

/** *********************************************************
 */
void Psi::unRegisterPsiCallback()
{
    mCallbackFn = NULL;
}


/** *********************************************************
 */
uint16_t Psi::getProgramNo(void)
{
    return  mPgmNo;
}

/** *********************************************************
 */
Pmt* Psi::getPmtObj(void)
{
    return  mPmt;
}

/***********************************************************
 */
Psi::Psi()
{
    FNLOG(DL_MSP_PSI);

    mDeletePsiRequested = false;
    mPgmNo          = -1;
    mPmtPid         = 0;
    mSrcHandle      = NULL;
    mPgrmHandleSf   = NULL;
    mPSecFilterBuf  = NULL;
    mSecFiltSize    = 0;
    mCallbackFn             = NULL;
    mCbClientContext      = NULL;
    mState          = kPsiStateIdle;
    mSfStarted      = false;
    mPmt            = new Pmt();
    m_ppsiUtils = psiUtils::getpsiUtilsInstance();

    caMetaDataSize = 0;
    caMetaDataPtr = NULL;
    caDescriptorLength = 0;
    caDescriptorPtr = NULL;

    // create event queue for scan thread
    psiThreadEventQueue = new MSPEventQueue();

    psiEventHandlerThread = 0;
    mSfCbIdError = 0;
    mSfCbIdSectionData = 0;
    mSfCbIdTimeOut = 0;
    mCbState = kPSIIdle;
    mSectFileReadAttempts = 0;
    memset(&mSfGroup, 0 , sizeof(tCpeSFltFilterGroup));
    mRawPmtPtr = NULL;
    mRawPmtSize = 0;
    mRawPatPtr = NULL;
    mRawPatSize = 0;
    mCurrentPMTCRC = 0;

    pthread_mutex_init(&mPsiMutex, NULL);
    musicPid = 0;
    dlog(DL_MSP_PSI, DLOGL_NOISE, "Inited mutx %p", &mPsiMutex);

}

void Psi::freePsi()
{
    // this called from event thread when PSI is destructed

    psiStop();

    if (caMetaDataPtr)
    {
        free(caMetaDataPtr);
        caMetaDataPtr = NULL;
        caMetaDataSize = 0;
    }

    if (caDescriptorPtr)
    {
        free(caDescriptorPtr);
        caDescriptorPtr = NULL;
        caDescriptorLength = 0;
    }

    if (mPSecFilterBuf)
    {
        free(mPSecFilterBuf);
        mPSecFilterBuf = NULL;
    }

    if (mRawPmtPtr != NULL)
    {
        free(mRawPmtPtr);
        mRawPmtPtr = NULL;
    }

    if (mRawPatPtr != NULL)
    {
        free(mRawPatPtr);
        mRawPatPtr = NULL;
    }

    // mState = kPsiStateClosed;

}

/** *********************************************************
 */
Psi::~Psi()
{
    FNLOG(DL_MSP_PSI);

    mDeletePsiRequested = true;

    if (psiEventHandlerThread)
    {
        LOG(DLOGL_REALLY_NOISY, "wait for PSI thread to exit");
        queueEvent(kPsiExitEvent);  // tell thread to exit

        pthread_join(psiEventHandlerThread, NULL);       // wait for event thread to exit

        psiEventHandlerThread = 0;
    }

    delete psiThreadEventQueue;
    psiThreadEventQueue = 0;


}



/** *********************************************************
 */
eMspStatus Psi::setSectionFilterGroup()
{
    if (mPmt)
    {
        uint32_t version = mPmt->mPmtInfo.versionNumber;
        LOG(DLOGL_REALLY_NOISY, "version: %d", mPmt->mPmtInfo.versionNumber);
        eMspStatus status;

        memset(&(mSfGroup), 0, sizeof(tCpeSFltFilterGroup));

        ePSFDState State = m_ppsiUtils->parseSectionFilterGroupData(version, &mSfGroup);

        // call cpe_sflt_Set with eCpeSFltNames_AddFltGrp to set filter group for the the section filter
        if (kPSFD_Success == State)
        {
            // call cpe_sflt_Set with eCpeSFltNames_AddFltGrp to set filter group for the the section filte
            int err = cpe_sflt_Set(mPgrmHandleSf, eCpeSFltNames_AddFltGrp, (void *)&mSfGroup);
            if (!err)
            {
                LOG(DLOGL_REALLY_NOISY, "cpe_sflt_Set Filter success  mPgrmHandleSf: %p", mPgrmHandleSf);
                status = kMspStatus_Ok;
            }
            else
            {
                LOG(DLOGL_ERROR, "cpe_sflt_Set Filter error: %d   mPgrmHandleSf: %p", err, mPgrmHandleSf);
                status = kMspStatus_CpeSecionFilterError;
            }
            return status;
        }
        else
        {
            LOG(DLOGL_ERROR, "SfGroup pointer is NULL");
            return kMspStatus_Error;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "%sError: Null mPmt data ", __FILE__);
        return kMspStatus_Error;
    }
    return  kMspStatus_Ok;
}


/** *********************************************************
 */
int Psi::registerSfCallbacks()
{
    int err = 0;

    err |= registerSectionFilterCallback(eCpeSFltCallbackTypes_Error,       mSfCbIdError);                // or should it be &mSfCbIdError ??
    err |= registerSectionFilterCallback(eCpeSFltCallbackTypes_SectionData, mSfCbIdSectionData);
    err |= registerSectionFilterCallback(eCpeSFltCallbackTypes_Timeout,     mSfCbIdTimeOut);

    return err;
}


/** *********************************************************
 */
int Psi::registerSectionFilterCallback(tCpeSFltCallbackTypes callbackType, tCpeSFltCallbackID callbackId)
{
    // Register for Callbacks
    int err = cpe_sflt_RegisterCallback(callbackType,
                                        (void *)this,
                                        (tCpeSFltCallbackFunction)sectionFilterCallbackFun,
                                        &callbackId,
                                        mPgrmHandleSf);

    if (err)
    {
        LOG(DLOGL_ERROR, "cpe_sflt_RegisterCallback error: %d  callbackType: %d", err, callbackType);
    }

    return err;
}




/** *********************************************************
 */
void Psi::sectionFilterCallbackFun(tCpeSFltCallbackTypes type, void *userdata, void *pCallbackSpecific)
{
    //  - this is a static void function which CPERP uses to callback results
    //  - this function executes in the context of CPERP thread - so do minimal processing
    //        copy buffer / post event to PSI thread

    FNLOG(DL_MSP_PSI);
    Psi* p = (Psi *) userdata;

    if (p)
    {
        p->lockMutex();

        switch (type)
        {
        case eCpeSFltCallbackTypes_SectionData: ///< Callback returns the section data to the caller
            LOG(DLOGL_REALLY_NOISY, "SectionData");
            p->setCallbackData((tCpeSFltBuffer *)pCallbackSpecific);
            break;

        case eCpeSFltCallbackTypes_Timeout:   ///< Read operation timed out.
            LOG(DLOGL_SIGNIFICANT_EVENT, "Warning : timeout in PSI read");
            break;

        case eCpeSFltCallbackTypes_Error:           ///< Error occured during read operation.
            LOG(DLOGL_SIGNIFICANT_EVENT, "Error in PSI read");
            break;

        default:
            LOG(DLOGL_ERROR, "Error: unknown type: %d", type);
            break;

        } //end of switch

        p->unlockMutex();
    }
    else
    {
        LOG(DLOGL_ERROR, "%sError: Null psi data ", __FILE__);
        return ;
    }
}

/** *********************************************************
 */

void Psi::setCallbackData(tCpeSFltBuffer *pSfltBuff)
{
    // This is called as part of the callback function called from CPERP PSI handling
    // Makes no sense to return values
    // Allocate a buffer to copy the buffer passed and post event to service in PSI thread

    FNLOG(DL_MSP_PSI);

    dlog(DL_MSP_PSI, DLOGL_NOISE, "Section filter copy\n");

    if (pSfltBuff == NULL)
    {
        LOG(DLOGL_ERROR, "Error: null pSfltBuff");
        return;
    }

    if (pSfltBuff->pBuffer == NULL || pSfltBuff->length == 0 || pSfltBuff->length > kMaxSecfiltBuffer)
    {
        LOG(DLOGL_ERROR, "Error: null pBuffer or bad length: %d", pSfltBuff->length);
        return;
    }

    tTableHeader *header, _header;

    getSectionHeader(pSfltBuff->pBuffer, &_header);
    header = &_header;
    if (header == NULL)
    {
        LOG(DLOGL_ERROR, "%sError: Null header ", __FILE__);
        return ;
    }
    uint32_t      tableID =  header->TableID;
    LOG(DLOGL_REALLY_NOISY, "mState:  %d", mState);
    LOG(DLOGL_REALLY_NOISY, "tableID: %d", tableID);
    LOG(DLOGL_REALLY_NOISY, "Version number %d", header->VersionNumber);

    bool allocMem = false;

    // TODO: Why all the pre-filtering checks.
    //       Determine if allocations don't always happen.
    //       If they do, skip the checks and always allocate and post event

    if (mState == kPsiWaitForPat)
    {
        if (tableID == kAppPatId)
        {
            allocMem = true;
            psiStop();
            mState = kPsiProcessingPAT;
            LOG(DLOGL_NOISE, "Found PAT");
        }
        else
            LOG(DLOGL_ERROR, "Wrong table ID %d for PSI state %d", tableID, mState);

    }
    else if (mState == kPsiWaitForPmt)
    {
        if (tableID == kAppPmtId)
        {
            allocMem = true;
            psiStop();
            mState = kPsiProcessingPMT;
            LOG(DLOGL_NOISE, "Found PMT");
        }
        else
            LOG(DLOGL_ERROR, "Wrong table ID %d for PSI state %d", tableID, mState);
    }
    else if (mState == kPsiWaitForUpdate)
    {
        if (mPmt && (tableID == kAppPmtId) && (header->VersionNumber != mPmt->mPmtInfo.versionNumber))
        {
            psiStop();
            LOG(DLOGL_REALLY_NOISY, "Got PMT revision update callback!!!");
            unsigned int NewPMTCRC = 0;
            NewPMTCRC = crc32(0xFFFFFFFF, (char *)(pSfltBuff->pBuffer + kPMT_HeaderSize), (pSfltBuff->length - kPMT_HeaderSize));
            LOG(DLOGL_NORMAL, "Old PMT CRC = %d, New PMT CRC  =%d", mCurrentPMTCRC, NewPMTCRC);

            if (mCurrentPMTCRC != NewPMTCRC)
            {
                LOG(DLOGL_REALLY_NOISY, "Change in PMT data");
                mState = kPsiProcessingPMTUpdate;
                allocMem = true;
            }
            else
            {
                mState =  kPsiUpdatePMTRevision;
                mPmt->mPmtInfo.versionNumber = header->VersionNumber;
                queueEvent(kPsiRevUpdateEvent);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Wrong table ID %d for PSI state %d? mPmt %p, new version %d",
                tableID, mState, mPmt, header->VersionNumber);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Not stopping filter, Wrong table ID %d or wrong PSI state %d", tableID, mState);
    }

    // TODO:  Change state in response to possible errors

    if (allocMem)
    {
        mSecFiltSize   = pSfltBuff->length;
        if (mPSecFilterBuf != NULL)
        {
            free(mPSecFilterBuf);
            mPSecFilterBuf = NULL;
        }
        mPSecFilterBuf = (uint8_t *)malloc(mSecFiltSize);   // this is freed in parseSectionFilterCallback

        if (tableID == kAppPatId)
        {
            if (mRawPatPtr != NULL)
            {
                free(mRawPatPtr);
                mRawPatPtr = NULL;
            }

            mRawPatPtr = (uint8_t *)malloc(mSecFiltSize);   // this is freed after during PSI destructor call

        }

        if (tableID == kAppPmtId)
        {
            if (mRawPmtPtr != NULL)
            {
                free(mRawPmtPtr);
                mRawPmtPtr = NULL;
            }

            mRawPmtPtr = (uint8_t *)malloc(mSecFiltSize);   // this is freed after during PSI destructor call

        }

        if (mPSecFilterBuf)
        {
            memcpy(mPSecFilterBuf, pSfltBuff->pBuffer, mSecFiltSize);

            if (tableID == kAppPatId)
            {
                memcpy(mRawPatPtr, pSfltBuff->pBuffer, mSecFiltSize);
                mRawPatSize = mSecFiltSize;
            }

            if (tableID == kAppPmtId)
            {
                memcpy(mRawPmtPtr, pSfltBuff->pBuffer, mSecFiltSize);
                LOG(DLOGL_REALLY_NOISY, "Dumping RP section filter data!!!");
                DumpPmtInfo(pSfltBuff->pBuffer, pSfltBuff->length);
                mRawPmtSize = mSecFiltSize;
            }

            queueEvent(kPsiSFCallbackEvent);
        }
        else
        {
            LOG(DLOGL_EMERGENCY, "Error malloc %d bytes", mSecFiltSize);
            return;
        }
    }
    else
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "Warning: sectionfilter callback not processed");
    }
}

/** *********************************************************
 */
eMspStatus Psi::parseSectionFilterCallBackData()
{
    FNLOG(DL_MSP_PSI);
    tTableHeader   *tableHdr, tblHdr;
    if (!mPSecFilterBuf)
    {
        LOG(DLOGL_ERROR, "Error null mPSecFilterBuf");
        return kMspStatus_PsiError;
    }

    if (mSecFiltSize < sizeof(tTableHeader))
    {
        LOG(DLOGL_ERROR, "Error mSecFiltSize too small - size: %d", mSecFiltSize);
        return kMspStatus_PsiError;
    }

    getSectionHeader(mPSecFilterBuf, &tblHdr);
    tableHdr = &tblHdr;

    if (tableHdr == NULL)
    {
        LOG(DLOGL_ERROR, "%sError: Null tableHdr ", __FILE__);
        return kMspStatus_PsiError;
    }

    logTableHeaderInfo(tableHdr);

    eMspStatus status = kMspStatus_Ok;

    LOG(DLOGL_NOISE, "Table ID %d, state %d", tableHdr->TableID, mState);
    switch (tableHdr->TableID)
    {
    case kAppPatId:
        if (mPmt && (mState == kPsiProcessingPAT))
        {
            mPmt->mTsParams.transportID = tableHdr->TransportStreamID;

            status = collectPatData(tableHdr->SectionLength);

            LOG(DLOGL_REALLY_NOISY, "free mPSecFilterBuf");
            free(mPSecFilterBuf);
            mPSecFilterBuf = NULL;


            if (status == kMspStatus_Ok)
            {
                LOG(DLOGL_REALLY_NOISY, "Success: Pgm num %d found after %d attempts", mPgmNo, mSectFileReadAttempts);
                mState = kPsiWaitForPmt;
                mSectFileReadAttempts = 0;
                queueEvent(kPsiPATReadyEvent);
            }
            else
            {
                ++mSectFileReadAttempts;

                if (mSectFileReadAttempts < kAppMaxSectFiltReadAttempts)
                {
                    LOG(DLOGL_ERROR, "Warning: Pgm num %d not found after %d tries, re-open section filter", mPgmNo, mSectFileReadAttempts);
                    mState = kPsiWaitForPat;
                    queueEvent(kPsiStartEvent);
                    status = kMspStatus_Ok;
                }
                else
                {
                    LOG(DLOGL_SIGNIFICANT_EVENT, "Warning: Pgm num %d not found after %d attempts - quitting", mPgmNo, mSectFileReadAttempts);
                    //  psiStop();
                    mState = kPsiStateIdle;
                    mSectFileReadAttempts = 0;
                    // Note: status from collectPatData will be returned which will cause thread to exit
                }
            }
        }
        else
        {
            LOG(DLOGL_SIGNIFICANT_EVENT, "Warning: PAT not processed -  mStatePate: %d", mState);
        }
        break;

    case kAppPmtId:
        if (mPmt && ((mState == kPsiProcessingPMT) || (mState == kPsiProcessingPMTUpdate)))
        {
            LOG(DLOGL_REALLY_NOISY, "Process for PMT Info");
            std::list<tPid> tempVideoPid;
            std::list<tPid> tempAudioPid;
            if (mState == kPsiProcessingPMTUpdate)
            {
                LOG(DLOGL_NORMAL, "Updating the PMT by deleting the older one");
                uint32_t progNumber = mPmt->mTsParams.progNumber;
                tempVideoPid = mPmt->mVideoPid;
                tempAudioPid = mPmt->mAudioPid;
                delete mPmt;
                mPmt = NULL;
                mPmt = new Pmt();
                mPmt->mTsParams.progNumber = progNumber;
            }

            status = collectPmtData(tableHdr->SectionLength);
            mPmt->mPmtInfo.versionNumber = tableHdr->VersionNumber;

            LOG(DLOGL_REALLY_NOISY, "Completed PMT Info");
            if (mPSecFilterBuf)
            {
                free(mPSecFilterBuf);
                mPSecFilterBuf = NULL;
            }

            ++mSectFileReadAttempts;

            if (status == kMspStatus_Ok)
            {
                mSectFileReadAttempts = 0;
                if (mState == kPsiProcessingPMTUpdate)
                {
                    // psiStop();
                    if ((tempVideoPid.size() != mPmt->mVideoPid.size()) || (tempAudioPid.size() != mPmt->mAudioPid.size()))
                    {
                        LOG(DLOGL_NORMAL, "Change in Audio/Video Pid list");
                        queueEvent(kPsiUpdateEvent);
                    }
                    else
                    {
                        std::list<tPid>::iterator iter1, iter2;
                        bool isAudioPidUpdated = false, isVideoPidUpdated = false;
                        for (iter1 = mPmt->mVideoPid.begin(), iter2 = tempVideoPid.begin(); iter1 != mPmt->mVideoPid.end(); iter1++, iter2++)
                        {
                            if ((*iter1).pid != (*iter2).pid)
                            {
                                LOG(DLOGL_REALLY_NOISY, "Video Pid updated in PMT data update");
                                LOG(DLOGL_NORMAL, "Original Pid is %d, updated Pid is %d", (*iter1).pid, (*iter2).pid);
                                isVideoPidUpdated = true;
                                break;
                            }

                        }

                        for (iter1 = mPmt->mAudioPid.begin(), iter2 = tempAudioPid.begin(); iter1 != mPmt->mAudioPid.end(); iter1++, iter2++)
                        {
                            if ((*iter1).pid != (*iter2).pid)
                            {
                                LOG(DLOGL_REALLY_NOISY, "Audio Pid updated in PMT data update");
                                LOG(DLOGL_NORMAL, "Original Pid is %d, updated Pid is %d", (*iter1).pid, (*iter2).pid);
                                isAudioPidUpdated = true;
                                break;
                            }
                        }


                        if ((isAudioPidUpdated == true) || (isVideoPidUpdated == true))
                        {
                            queueEvent(kPsiUpdateEvent);
                        }
                        else
                        {
                            LOG(DLOGL_NORMAL, "No audio, video Pids changed in new PMT");
                            queueEvent(kPsiRevUpdateEvent);
                        }
                    }
                }
                else
                {
                    // psiStop();
                    mCurrentPMTCRC = crc32(0xFFFFFFFF, (char *)(mRawPmtPtr + kPMT_HeaderSize), (mRawPmtSize - kPMT_HeaderSize));
                    queueEvent(kPsiPMTReadyEvent);
                }
            }
            else if ((mSectFileReadAttempts > kAppMaxSectFiltReadAttempts))
            {
                // psiStop();
                LOG(DLOGL_ERROR, "Warning Max SectFilt Read Attempts PMT PID 0x%x not found", mPmtPid);
                mState = kPsiStateIdle;
                mSectFileReadAttempts = 0;
                break;
            }
            else
            {
                startSectionFilter(mPmtPid);
                mState = kPsiWaitForPmt;
            }

        }

        break;

    default:
        LOG(DLOGL_ERROR, "Uknown table ID (0x%x)", tableHdr->TableID);

    } // End Of Switch

    return status;
}



/** *********************************************************
 */

eMspStatus Psi::collectPatData(uint32_t sectionLength)
{
    // precondition:  mPSecFilterBuf has been verified to be valid (non-null and proper size)

    FNLOG(DL_MSP_PSI);
    LOG(DLOGL_REALLY_NOISY, "sectionLength: %d  mPgmNo: 0x%x", sectionLength, mPgmNo);
    tPat           programData;
    int size = (sectionLength - (kAppTableHeaderFromSectionLength + kAppTableCRCSize)) / sizeof(tPat);

    tPat* patData = m_ppsiUtils->getPatData(mPSecFilterBuf);

    for (int idx = 0; idx < size; idx++, patData++)
    {

        getProgram((const uint8_t *)patData, &programData);
        if (patData && mPmt && (programData.ProgNumber == mPgmNo))
        {
            LOG(DLOGL_REALLY_NOISY, "Pgm No : 0x%x -  PID : 0x%x", programData.ProgNumber, programData.PID);
            mPmtPid = programData.PID;     // the only place  mPmtPid is set
            mPmt->mTsParams.progNumber = programData.ProgNumber;
            LOG(DLOGL_REALLY_NOISY, "Found PMT Pid = 0x%x for mPgmNo: %d", mPmtPid, mPgmNo);
            break;
        }
    }

    eMspStatus status;

    if (mPmtPid)
    {
        status = kMspStatus_Ok;
    }
    else
    {
        LOG(DLOGL_MINOR_DEBUG, "mPgmNo 0x%x not found in PAT", mPgmNo);
        status =  kMspStatus_PsiError;
    }

    return status;
}

/** *********************************************************
 */
eMspStatus Psi::collectPmtData(uint32_t sectionLength)
{
    FNLOG(DL_MSP_PSI);

    eMspStatus status = kMspStatus_Ok;

    uint8_t* pSecFilterBuf = mPSecFilterBuf;


    uint8_t        descTag;
    uint8_t        descLen;
    int            totalPmtLen;
    uint8_t       *pPmtEnd;
    int            pgmInfoLen;
    uint8_t       *pPgmInfoEnd;
    uint16_t       esInfoLen;
    uint8_t       *pEsInfoEnd;

    tCpePgrmHandleMpegDesc  *pDescInfo;
    tCpePgrmHandleEsData    *pEsDataInfo;

    if (mPmt == NULL)
    {
        LOG(DLOGL_ERROR, "%sError: Null pmt data ", __FILE__);
        return kMspStatus_Error;
    }
    memset(&(mPmt->mPmtInfo), 0, sizeof(tCpePgrmHandlePmt));

    pSecFilterBuf += sizeof(tTableHeader);     // get past table header
    totalPmtLen = (sectionLength - (kAppTableHeaderFromSectionLength + kAppTableCRCSize));
    pPmtEnd = pSecFilterBuf + totalPmtLen;

    // Clock Pid
    mPmt->mTsParams.pidTable.clockPid = (*pSecFilterBuf++ & 0x1F) << 8;
    mPmt->mTsParams.pidTable.clockPid |= *pSecFilterBuf++;
    mPmt->mPmtInfo.clockPid = mPmt->mTsParams.pidTable.clockPid;

    dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "PCR PID : 0x%x\n", mPmt->mTsParams.pidTable.clockPid);

    // program_info_length
    pgmInfoLen = (*pSecFilterBuf++ & 0x0F) << 8;
    pgmInfoLen |= *pSecFilterBuf++;
    pPgmInfoEnd = pSecFilterBuf + pgmInfoLen;
    if (pPgmInfoEnd > pPmtEnd)
    {
        dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "%s:%d PMT parse length error \n", __FUNCTION__, __LINE__);
        return kMspStatus_PsiError;
    }

    dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "%s:%d program_info_length \n", __FUNCTION__, __LINE__);

    mPmt->mPmtInfo.pgmDescCount = 0;
    if (pgmInfoLen == 0)
    {
        mPmt->mPmtInfo.ppPgmDesc = NULL;
        dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "%s:%d pPmtInfo->ppPgmDesc = NULL; \n", __FUNCTION__, __LINE__);
    }
    else
    {
        mPmt->mPmtInfo.ppPgmDesc = (tCpePgrmHandleMpegDesc **)calloc(kAppMaxProgramDesciptors, sizeof(tCpePgrmHandleMpegDesc *));
        LOG(DLOGL_REALLY_NOISY, "Program descriptor base pointer is %p", mPmt->mPmtInfo.ppPgmDesc);
        while ((pSecFilterBuf < pPgmInfoEnd) && (mPmt->mPmtInfo.pgmDescCount < kAppMaxProgramDesciptors))
        {
            descTag = *pSecFilterBuf++;
            descLen = *pSecFilterBuf++;
            pDescInfo = (tCpePgrmHandleMpegDesc *)calloc(1, sizeof(tCpePgrmHandleMpegDesc));
            if (pDescInfo)
            {
                pDescInfo->tag = descTag;
                pDescInfo->dataLen = descLen;
                pDescInfo->data = (uint8_t *)calloc(1, descLen);
                memcpy(pDescInfo->data, pSecFilterBuf, descLen);

                mPmt->mPmtInfo.ppPgmDesc[mPmt->mPmtInfo.pgmDescCount] = pDescInfo;
                mPmt->mPmtInfo.pgmDescCount++;
                pSecFilterBuf += descLen;
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "%sError: Null pDescInfo ", __FILE__);
                return kMspStatus_PsiError;
            }
        }
    }

    dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "%s:%d ES Info \n", __FUNCTION__, __LINE__);

    // ES Info
    pSecFilterBuf = pPgmInfoEnd;

    mPmt->mPmtInfo.esCount = 0;
    mPmt->mPmtInfo.ppEsData = (tCpePgrmHandleEsData **)calloc(kAppMaxEsCount, sizeof(tCpePgrmHandleEsData *));
    while ((pSecFilterBuf < pPmtEnd) && (mPmt->mPmtInfo.esCount < kAppMaxEsCount))
    {
        uint16_t pid;
        tPid avpid;

        pEsDataInfo = (tCpePgrmHandleEsData *)calloc(1, sizeof(tCpePgrmHandleEsData));
        if (pEsDataInfo == NULL)
        {
            LOG(DLOGL_REALLY_NOISY, "%sError: Null pEsDataInfo ", __FILE__);
            return kMspStatus_PsiError;
        }
        uint8_t streamType = *pSecFilterBuf++;
        pEsDataInfo->reserved[0] = *pSecFilterBuf >> 5;
        pid = (*pSecFilterBuf++ & 0x1F) << 8;
        pid |= *pSecFilterBuf++ & 0xFF;
        pEsDataInfo->streamType = streamType;
        pEsDataInfo->pid = pid;

        dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Stream Type : 0x%x -  PID : 0x%x\n", streamType, pid);

        switch (streamType)
        {
        case kCpeStreamType_MPEG1_Video:
        case kCpeStreamType_MPEG2_Video:
        case kCpeStreamType_H264_Video:
        case kCpeStreamType_GI_Video:
        case kCpeStreamType_VC1_Video:

            mPmt->mTsParams.pidTable.videoStreamType = streamType;
            mPmt->mTsParams.pidTable.videoPid = pid;
            avpid.pid = pid;
            avpid.streamType = streamType;
            mPmt->mVideoPid.push_back(avpid);
            break;

        case kCpeStreamType_MPEG1_Audio:
        case kCpeStreamType_MPEG2_Audio:
        case kCpeStreamType_AAC_Audio:
        case kCpeStreamType_AACplus_Audio:
        case kCpeStreamType_DDPlus_Audio:
        case kCpeStreamType_GI_Audio:

            mPmt->mTsParams.pidTable.audioStreamType = streamType;
            mPmt->mTsParams.pidTable.audioPid = pid;
            avpid.pid = pid;
            avpid.streamType = streamType;
            mPmt->mAudioPid.push_back(avpid);
            break;
        case kText_DCII:
            musicPid = pid;
            break;
        }

        // ES_info_length
        pEsDataInfo->reserved[1] = *pSecFilterBuf >> 4;
        esInfoLen = (*pSecFilterBuf++ & 0xF) << 8;
        esInfoLen |= *pSecFilterBuf++;
        pEsDataInfo->reserved[2] = 0; // ??

        pEsDataInfo->descCount = 0;
        if (esInfoLen == 0)
        {
            pEsDataInfo->ppEsDesc = NULL;
        }
        else
        {
            pEsInfoEnd = pSecFilterBuf + esInfoLen;
            pEsDataInfo->ppEsDesc = (tCpePgrmHandleMpegDesc **)calloc(kMaxESDescriptors, sizeof(tCpePgrmHandleMpegDesc *));
            while ((pSecFilterBuf < pEsInfoEnd) && (pEsDataInfo->descCount < kMaxESDescriptors))
            {
                descTag = *pSecFilterBuf++;
                descLen = *pSecFilterBuf++;
                pDescInfo = (tCpePgrmHandleMpegDesc *)calloc(1, sizeof(tCpePgrmHandleMpegDesc));
                pDescInfo->tag = descTag;
                pDescInfo->dataLen = descLen;
                pDescInfo->data = (uint8_t *)calloc(1, descLen);
                memcpy(pDescInfo->data, pSecFilterBuf, descLen);

                pSecFilterBuf += descLen;
                pEsDataInfo->ppEsDesc[pEsDataInfo->descCount] = pDescInfo;
                pEsDataInfo->descCount++;
            }
        }
        mPmt->mPmtInfo.ppEsData[mPmt->mPmtInfo.esCount] = pEsDataInfo;
        mPmt->mPmtInfo.esCount = mPmt->mPmtInfo.esCount + 1;
    }

    dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "%s:%d PMT PARSE END  \n", __FUNCTION__, __LINE__);

    if ((mPmt->mTsParams.pidTable.audioPid) || (mPmt->mTsParams.pidTable.videoPid))
    {
        dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "%s:%d Found AudPid=0x%x VidPid=0x%x \n", __FUNCTION__, __LINE__, mPmt->mTsParams.pidTable.audioPid, mPmt->mTsParams.pidTable.videoPid);

        mPmt->printPmtInfo();

    }
    else
    {
        dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "%s:%d  No Audio & video PID found in PMT Continuing  \n", __FUNCTION__, __LINE__);
        mPmt->freePmtInfo();
        return kMspStatus_PsiError;

    }

    return status;


}


/** *********************************************************
 */
eMspStatus Psi::queueEvent(ePsiEvent evtyp)
{
    if (!psiThreadEventQueue)
    {
        LOG(DLOGL_ERROR, "Error: no psithreadEventQueue to handle event %d", evtyp);
        return kMspStatus_BadParameters;
    }

    psiThreadEventQueue->dispatchEvent(evtyp);

    return kMspStatus_Ok;
}

eMspStatus Psi::getComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset)
{
    Pmt *pmt = getPmtObj();
    if (pmt)
    {
        tCpePgrmHandlePmt pmtInfo;
        pmt->getPmtInfo(&pmtInfo);
        dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Elementary Stream Count %d %d %d %d\n", pmtInfo.esCount, infoSize, offset, *count);
        *count = 0;
        for (uint32_t i = offset; i < pmtInfo.esCount && *count < infoSize; i++)
        {
            tCpePgrmHandleEsData *esData = pmtInfo.ppEsData[i];
            if (esData)
            {
                info[i - offset].pid = esData->pid;
                info[i - offset].streamType = esData->streamType;
                memset(info[i - offset].langCode, 0 , ISO_639_CODE_LENGTH);
                if (isAudioStreamType(info[i - offset].streamType))
                {
                    for (int j = 0 ; j < esData->descCount; j++)
                    {
                        tCpePgrmHandleMpegDesc *ppEsDesc = esData->ppEsDesc[j];
                        if (ppEsDesc && ppEsDesc->tag == ISO_639_LANG_TAG)
                        {
                            memcpy(info[i - offset].langCode, ppEsDesc->data, ISO_639_CODE_LENGTH);
                            break;
                        }
                    }
                }
                *count = *count + 1;
            }
        }
        return kMspStatus_Ok;
    }
    else
    {
        return kMspStatus_Error;
    }

}

bool Psi::isAudioStreamType(uint16_t aStreamType)
{
    return (aStreamType == kCpeStreamType_MPEG1_Audio ||
            aStreamType == kCpeStreamType_MPEG2_Audio ||
            aStreamType == kCpeStreamType_AAC_Audio ||
            aStreamType == kCpeStreamType_AACplus_Audio ||
            aStreamType == kCpeStreamType_DDPlus_Audio ||
            aStreamType == kCpeStreamType_GI_Audio);
}


eMspStatus Psi::getRawPmt(uint8_t **PmtPtr, unsigned int *PmtSize, uint16_t *PmtPid)
{
    eMspStatus status = kMspStatus_Ok;
    unsigned int srcPointer = 0, destPointer = 0;
    unsigned int crc = 0;
    uint8_t *ptrPmt = NULL;
    uint16_t  pgmInfoLen = 0;
    uint16_t  *pgmInfoLenPtr = NULL;



    ptrPmt = (uint8_t *)calloc(mRawPmtSize, sizeof(uint8_t));
    if (ptrPmt)
    {
        // copy header as it is to destination buffer
        memcpy(ptrPmt, mRawPmtPtr, kPMT_HeaderSize);
        srcPointer += kPMT_HeaderSize;
        destPointer += kPMT_HeaderSize;
        // get the length of program info length
        pgmInfoLenPtr = (uint16_t *)(mRawPmtPtr + srcPointer);
        pgmInfoLen = (uint16_t)(TS_READ_16(pgmInfoLenPtr) & 0x0FFF);
        // copy prog info length and prog info to destination buffer
        memcpy((ptrPmt + destPointer), (mRawPmtPtr + srcPointer), (pgmInfoLen + kPMT_LenSize));
        srcPointer += (pgmInfoLen + kPMT_LenSize);
        destPointer += (pgmInfoLen + kPMT_LenSize);

        while (srcPointer < mRawPmtSize - kPMT_CRCSize)
        {
            uint16_t       esInfoLen;
            uint16_t       *esInfoLenPtr = NULL;
            uint16_t       *destESInfoLenPtr = NULL;
            uint8_t        streamType = 0;
            uint16_t       trackDesc = 0;
            uint16_t       remLen = 0;

            streamType     = *(mRawPmtPtr + srcPointer);
            // copy descriptor header information to destination buffer
            memcpy((ptrPmt + destPointer), (mRawPmtPtr + srcPointer), kPMT_DesInfoSize);
            srcPointer += kPMT_DesInfoSize;
            destPointer += kPMT_DesInfoSize;

            // get the length of descriptor info length
            esInfoLenPtr = (uint16_t *)(mRawPmtPtr + srcPointer);
            esInfoLen = (uint16_t)(TS_READ_16(esInfoLenPtr) & 0x0FFF);
            destESInfoLenPtr = (uint16_t *)(ptrPmt + destPointer);

            memcpy((ptrPmt + destPointer), (mRawPmtPtr + srcPointer), kPMT_LenSize);
            srcPointer += kPMT_LenSize;
            destPointer += kPMT_LenSize;

            // If streamtype is video check for ca data else copy as is to destination buffer
            if ((streamType == kCpeStreamType_MPEG1_Video) || (streamType == kCpeStreamType_MPEG2_Video) || (streamType == kCpeStreamType_H264_Video) ||
                    (streamType == kCpeStreamType_GI_Video) || (streamType == kCpeStreamType_VC1_Video))
            {
                while (trackDesc < esInfoLen)
                {
                    uint8_t tag = *(mRawPmtPtr + srcPointer);
                    uint8_t len = *(mRawPmtPtr + srcPointer + 1);
                    switch (tag)
                    {
                    case 0x09:
                    case 0x65:
                    {
                        srcPointer += (kPMT_DesTagSize + kPMT_DesLenSize + len);
                        remLen += (kPMT_DesTagSize + kPMT_DesLenSize + len);
                    }
                    break;
                    default:
                    {
                        memcpy((ptrPmt + destPointer), (mRawPmtPtr + srcPointer), (kPMT_DesTagSize + kPMT_DesLenSize + len));
                        srcPointer += (kPMT_DesTagSize + kPMT_DesLenSize + len);
                        destPointer += (kPMT_DesTagSize + kPMT_DesLenSize + len);
                    }
                    break;
                    }
                    trackDesc += (kPMT_DesTagSize + kPMT_DesLenSize + len);
                }
            }
            else
            {
                memcpy((ptrPmt + destPointer), (mRawPmtPtr + srcPointer), esInfoLen);
                srcPointer += esInfoLen;
                destPointer += esInfoLen;
            }
            if (remLen)
            {
                *destESInfoLenPtr =  ntohs(0xf000 | (esInfoLen - remLen));
            }
        }
        tTableHeader *hdr;
        //get the section length of original PMT
        hdr = (tTableHeader *)ptrPmt;
        hdr->SectionLength = destPointer + kPMT_CRCSize - kPMT_DesInfoSize;

        unsigned char temp = ptrPmt[1];
        temp = temp & (0xF0);
        temp = temp | ((hdr->SectionLength) & 0xFF00) >> 8;
        ptrPmt[1] = temp;
        ptrPmt[2] = ((hdr->SectionLength) & 0x00FF);

        //calculate CRC and copy them
        crc = crc32(0xFFFFFFFF, (char *)ptrPmt, destPointer);

        ptrPmt[destPointer + 0] = (0xff & (crc >> 24));
        ptrPmt[destPointer + 1] = (0xff & (crc >> 16));
        ptrPmt[destPointer + 2] = (0xff & (crc >> 8));
        ptrPmt[destPointer + 3] = (0xff & (crc));
        *PmtPid = mPmtPid;
        *PmtPtr = ptrPmt;
        *PmtSize = destPointer + kPMT_CRCSize;

    }
    return status;
}

eMspStatus Psi::getRawPat(uint8_t **PatPtr)
{

    tPat* patData;
    unsigned int programCount;
    unsigned int sectionLength;
    unsigned int i = 0;
    unsigned int crc = 0;
    uint8_t * PatPtrBase;
    tTableHeader *hdr;

    dlog(DL_MSP_PSI, DLOGL_NOISE, "entering getrawpat\n");
    *PatPtr = (uint8_t *)calloc(kSPTS_PATSizeWithCRC , sizeof(uint8_t));
    PatPtrBase = *PatPtr;

    tTableHeader _header;

    getSectionHeader(mRawPatPtr, &_header);
    hdr = &_header;

    if (hdr == NULL)
    {
        LOG(DLOGL_ERROR, "%sError: Null header ", __FILE__);
        return kMspStatus_Error;
    }
    sectionLength = hdr->SectionLength;

    //copy table header
    memcpy(*PatPtr, mRawPatPtr, sizeof(tTableHeader));
    //assigning the section
    hdr = (tTableHeader *) *PatPtr;
    if (hdr == NULL)
    {
        LOG(DLOGL_ERROR, "%sError: Null header data ", __FILE__);
        return kMspStatus_Error;
    }
    hdr->SectionLength = kSPTS_SectionLength;

    unsigned char temp = PatPtrBase[1];
    temp = temp & (0xF0);
    temp = temp | ((hdr->SectionLength) & 0xFF00) >> 8;
    PatPtrBase[1] = temp;
    PatPtrBase[2] = ((hdr->SectionLength) & 0x00FF);

    *PatPtr += sizeof(tTableHeader);


    //parse through the Program numbers in PAT.
    patData = (tPat*)(mRawPatPtr + sizeof(tTableHeader));
    if (patData == NULL)
    {
        LOG(DLOGL_ERROR, "%sError: Null patData ", __FILE__);
        return kMspStatus_Error;
    }
    programCount = (sectionLength - (kAppTableHeaderFromSectionLength + kAppTableCRCSize)) / sizeof(tPat);

    tPat  programData;

    for (i = 0; i < programCount; programCount++, patData++)
    {
        getProgram((const uint8_t *)patData, &programData);
        dlog(DL_MSP_PSI, DLOGL_NOISE, "Pgm No : 0x%x -  PID : 0x%x", patData->ProgNumber, patData->PID);
        if (programData.ProgNumber == mPgmNo)
        {
            //copy the matching tPat data
            memcpy(*PatPtr, patData, sizeof(tPat));
            *PatPtr += sizeof(tPat);
            break;
        }
    }

    //calculate CRC and copy them
    crc = crc32(0xFFFFFFFF, (char *)PatPtrBase, kSPTS_PATSize);

    PatPtrBase[kSPTS_PATSize + 0] = (0xff & (crc >> 24));
    PatPtrBase[kSPTS_PATSize + 1] = (0xff & (crc >> 16));
    PatPtrBase[kSPTS_PATSize + 2] = (0xff & (crc >> 8));
    PatPtrBase[kSPTS_PATSize + 3] = (0xff & (crc));


    //reassigning the base pointer to PAT ptr
    *PatPtr = PatPtrBase;

    return kMspStatus_Ok;
}


unsigned int Psi::crc32(unsigned int seed, const char *buf, unsigned int len)
{
    unsigned int crc, index;

    for (crc = seed; len > 0; len--)
    {
        index = 0xff & ((crc >> 24) ^ (*buf++));
        crc = (crc << 8) ^ (crctab[index]);
    }
    return crc;
}


/** *********************************************************
 */
void Psi::lockMutex(void)
{
    dlog(DL_MSP_PSI, DLOGL_NOISE, "Locking mutx %p", &mPsiMutex);
    pthread_mutex_lock(&mPsiMutex);
}

/** *********************************************************
 */
void Psi::unlockMutex(void)
{
    dlog(DL_MSP_PSI, DLOGL_NOISE, "unLocking mutx %p", &mPsiMutex);
    pthread_mutex_unlock(&mPsiMutex);
}


/** *********************************************************
 */
void Psi::logTableHeaderInfo(tTableHeader *tblHdr)
{
    if (tblHdr)
    {
        LOG(DLOGL_REALLY_NOISY, "TableID                : %d", tblHdr->TableID);
        LOG(DLOGL_REALLY_NOISY, "SectionSyntaxIndicator : %d", tblHdr->SectionSyntaxIndicator);
        LOG(DLOGL_REALLY_NOISY, "SectionLength          : %d", tblHdr->SectionLength);
        LOG(DLOGL_REALLY_NOISY, "TS ID / Program No     : %d", tblHdr->TransportStreamID);
        LOG(DLOGL_REALLY_NOISY, "VersionNumber          : %d", tblHdr->VersionNumber);
        LOG(DLOGL_REALLY_NOISY, "CurrentNextIndicator   : %d", tblHdr->CurrentNextIndicator);
        LOG(DLOGL_REALLY_NOISY, "SectionNumber          : %d", tblHdr->SectionNumber);
        LOG(DLOGL_REALLY_NOISY, "LastSectionNumber      : %d", tblHdr->LastSectionNumber);
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: Null psi data ");
        //return ;
    }
}


/** *********************************************************
 */
void Psi::getSectionHeader(const uint8_t *buf, tTableHeader *p_header)
{
    if (NULL != buf && NULL != p_header)
    {
        p_header->TableID = buf[TS_PSI_TABLE_ID_OFFSET];
        p_header->SectionSyntaxIndicator = (buf[TS_PSI_SECTION_LENGTH_OFFSET] >> 7) & 1;
        p_header->dummy = (buf[TS_PSI_SECTION_LENGTH_OFFSET] >> 6) & 1;
        p_header->SectionLength = TS_PSI_GET_SECTION_LENGTH(buf);
        p_header->TransportStreamID = (uint16_t)(TS_READ_16(&(buf)[TS_PSI_TABLE_ID_EXT_OFFSET]) & 0xFFFF);
        p_header->VersionNumber = (uint8_t)((buf[TS_PSI_CNI_OFFSET] >> 1) & 0x1F);
        p_header->CurrentNextIndicator = buf[TS_PSI_CNI_OFFSET] & 1;
        p_header->SectionNumber = buf[TS_PSI_SECTION_NUMBER_OFFSET];
        p_header->LastSectionNumber = buf[TS_PSI_LAST_SECTION_NUMBER_OFFSET];
    }
    else
    {
        LOG(DLOGL_ERROR, "%sError: Null PAT data ", __FILE__);
        return;
    }
}

/** *********************************************************
 */
void Psi::getProgram(const uint8_t *buf, tPat *p_program)
{
    if (NULL != buf && NULL != p_program)
    {
        p_program->ProgNumber = TS_READ_16(buf);
        p_program->PID = (uint16_t)(TS_READ_16(&buf[2]) & 0x1FFF);
    }
    else
    {
        LOG(DLOGL_ERROR, "%s Error: Null p_program ", __FILE__);
    }
}


eMspStatus Psi::DumpPmtInfo(uint8_t *pPmtPsiPacket, int PMTSize)
{
    uint8_t  esInfoStartIndex = 0;
    uint16_t sectionLength = 0;
    uint16_t program_info_length = 0;

    uint16_t       esInfoLen = 0;
    uint8_t        descTag = 0;
    uint8_t        descLen = 0;
    uint32_t       descCount = 0;

    LOG(DLOGL_REALLY_NOISY, "%s: data ptr = %p data size = %d\n", __FUNCTION__, pPmtPsiPacket, PMTSize);

    if (PMTSize != 0 && pPmtPsiPacket != NULL)
    {

        LOG(DLOGL_REALLY_NOISY, "%s: tableId - %d \n", __FUNCTION__, pPmtPsiPacket[0]);
        LOG(DLOGL_REALLY_NOISY, "%s: section_syntax_indicator - %d \n", __FUNCTION__, (pPmtPsiPacket[1] & 0x80));
        LOG(DLOGL_REALLY_NOISY, "%s: zero field - %d \n", __FUNCTION__, (pPmtPsiPacket[1] & 0x40));
        LOG(DLOGL_REALLY_NOISY, "%s: reserved - %d \n", __FUNCTION__, (pPmtPsiPacket[1] & 0x30));

        sectionLength = (((((uint16_t)pPmtPsiPacket[1]) & 0x000F) << 8) | ((uint16_t) pPmtPsiPacket[2]));
        LOG(DLOGL_REALLY_NOISY, "%s: section_length - %d \n", __FUNCTION__, sectionLength);
        LOG(DLOGL_REALLY_NOISY, "%s: prog_no - %d \n", __FUNCTION__, (((pPmtPsiPacket[3]) << 8) | (pPmtPsiPacket[4] & 0xFF)));
        LOG(DLOGL_REALLY_NOISY, "%s: reserved - %d \n", __FUNCTION__, (pPmtPsiPacket[5] & 0xC0));
        LOG(DLOGL_REALLY_NOISY, "%s: version no - %d \n", __FUNCTION__, (pPmtPsiPacket[5] & 0x3E));
        LOG(DLOGL_REALLY_NOISY, "%s: current_next_indicator - %d \n", __FUNCTION__, (pPmtPsiPacket[5] & 0x01));
        LOG(DLOGL_REALLY_NOISY, "%s: section no - %d \n", __FUNCTION__, (pPmtPsiPacket[6]));
        LOG(DLOGL_REALLY_NOISY, "%s: last_section_no - %d \n", __FUNCTION__, (pPmtPsiPacket[7]));
        LOG(DLOGL_REALLY_NOISY, "%s: reserved - %d \n", __FUNCTION__, (pPmtPsiPacket[8] & 0xE0));
        LOG(DLOGL_REALLY_NOISY, "%s: PCR PID - %d \n", __FUNCTION__, (((pPmtPsiPacket[8] & 0x1F) << 8) | (pPmtPsiPacket[9])));

        // Clock Pid
        uint32_t clockPid = (((pPmtPsiPacket[8] & 0x1F) << 8) | (pPmtPsiPacket[9]));
        dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "PCR PID : 0x%x\n", clockPid);

        LOG(DLOGL_REALLY_NOISY, "%s: reserved - %d \n", __FUNCTION__, (pPmtPsiPacket[10] & 0xF0));

        program_info_length = (((((uint16_t)pPmtPsiPacket[10]) & 0x000F) << 8) | ((uint16_t) pPmtPsiPacket[11]));
        LOG(DLOGL_REALLY_NOISY, "%s: prog_info_length - %d \n", __FUNCTION__, program_info_length);

        uint8_t *pPgmInfoStart = pPmtPsiPacket + kAppPmtHeaderLength;
        uint8_t *pPgmInfoEnd   = pPgmInfoStart + program_info_length;

        // Dumping Program Info Descriptors
        if (program_info_length == 0)
        {
            dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "%s:%d pPmtInfo->ppPgmDesc = NULL; \n", __FUNCTION__, __LINE__);
        }
        else
        {
            // Dumping Program Info Descriptors
            while ((pPgmInfoStart < pPgmInfoEnd))
            {
                descTag = *pPgmInfoStart++;
                descLen = *pPgmInfoStart++;

                dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Program Info DescTag:0x%x DescLen:%d", descTag, descLen);

                pPgmInfoStart += descLen;
            }
        }

        esInfoStartIndex = kAppPmtHeaderLength + program_info_length;
        LOG(DLOGL_REALLY_NOISY, "%s: total length to skip - %d \n\n", __FUNCTION__, esInfoStartIndex);

        uint8_t *pEsInfoStart = (pPmtPsiPacket + esInfoStartIndex);
        uint8_t *pEsInfoEnd   = (pPmtPsiPacket + kAppPmtTableLengthMinusSectionLength + sectionLength  - kAppTableCRCSize);

        // Dumping ES Type and PID Number and ES Descriptors
        while ((pEsInfoStart < pEsInfoEnd))
        {
            uint16_t pid = 0;

            uint8_t streamType = *pEsInfoStart++;

            pid = (*pEsInfoStart++ & 0x1F) << 8;
            pid |= *pEsInfoStart++ & 0xFF;

            dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Stream Type : 0x%x -  PID : 0x%x\n", streamType, pid);

            // ES_info_length
            esInfoLen = (*pEsInfoStart++ & 0xF) << 8;
            esInfoLen |= *pEsInfoStart++;

            dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Elementary stream INFO length :%d", esInfoLen);

            // Dumping ES Descriptors
            if (esInfoLen == 0)
            {
                dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Elementary stream INFO length is 0: No ES Descriptors present");
            }
            else
            {
                uint8_t* pDsInfoEnd = NULL;
                pDsInfoEnd = pEsInfoStart + esInfoLen;

                // Dumping ES Descriptors
                while ((pEsInfoStart < pDsInfoEnd) && (descCount < kMaxESDescriptors))
                {
                    descTag = *pEsInfoStart++;
                    descLen = *pEsInfoStart++;

                    dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Elementary stream DescTag:0x%x DescLen:%d", descTag, descLen);

                    pEsInfoStart += descLen;

                    descCount++;
                }
            }
        }

    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid->NULL \n", __FUNCTION__);
    }

    return kMspStatus_Ok;
}
