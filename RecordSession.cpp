/**
   \file RecordSession.cpp
   \class RecordSession

Implementation file for RecordSession class
*/


///////////////////////////////////////////////////////////////////////////
//                          Includes
///////////////////////////////////////////////////////////////////////////

#include "RecordSession.h"
#include "languageSelection.h"
#include "dvr_metadata_reader.h"
#include <cpe_error.h>
#include <cpe_common.h>
#include <misc_platform.h>
#include <dlog.h>
#include <syslog.h>
#include "IMediaPlayer.h"
#if defined(DMALLOC)
#include "dmalloc.h"
#endif


#include "csci-dvr-scheduler-api.h"

#include <assert.h>


#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_DVR, level,"RecSession:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

#define UNUSED_PARAM(a) (void)a;

//TODO-Currently sharing mount details from platform.cpp with extern.needs to do in a better way.
extern char dvr_mount_point[DVR_MNT_PNT_LEN];

extern int get_tsb_file(char *tsb_file, unsigned int session_number);


/** *********************************************************
 *  used to call through to the proper media controller's callback function
 *  registered for entitlement updates
*/


static void caEntitlemntCallback(void *pData, pcamEntitlementStatus entStatus)
{
    MSPRecordSession *pSession = (MSPRecordSession *)pData;
    LOG(DLOGL_ERROR, "session %p, Informing SL new CA Entitlement status %d", pSession, entStatus);

    if (pSession && pSession->mCbIsSet == true)
    {
        if (entStatus == CAM_AUTHORIZED)
        {
            pSession->callback(kMediaPlayerSignal_ServiceAuthorized, kMediaPlayerStatus_Ok);
        }
        else
        {
            pSession->callback(kMediaPlayerSignal_ServiceDeauthorized, kMediaPlayerStatus_NotAuthorized);
        }
    }

    return;
}

/** *********************************************************
 *  Registers controller's callback function to dispatch cam entitlemnt updates
*/

boost::signals2::connection MSPRecordSession::setCallback(callbackfunctiontype cbfunc)
{
    mCbIsSet = true;
    return callback.connect(cbfunc);
}

/** *********************************************************
 *  Unregisters controller's callback function registered for cam entitlemnt updates
*/

void MSPRecordSession::clearCallback(boost::signals2::connection conn)
{
    mCbIsSet = false;
    conn.disconnect();

}
///////////////////////////////////////////////////////////////////////////
//                      Member implementation
///////////////////////////////////////////////////////////////////////////

/** *********************************************************
*/
void MSPRecordSession::metadataCallback(void)
{
    eMspStatus status;

    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "CA metadata has arrived\n");
    status = saveCAMetaData();
    if (status != kMspStatus_Ok)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d SaveCAMetaData returned error.  Code %d", __FUNCTION__, __LINE__, status);
    }
    else
    {
        mIsCAMetaWritten = true;
    }
    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Writing CA metadata is finished !!!!\n");
}


/** *********************************************************
 *  only used to call through to the proper record session
 *  instance
*/
void MSPRecordSession::caDvrMetadataCallback(void *ctx)
{
    MSPRecordSession *inst = (MSPRecordSession *)ctx;

    inst->metadataCallback();

}


char* MSPRecordSession::GetTsbFileName()
{
    return mtsb_filename;
}

void MSPRecordSession::SetTsbFileName(int *tsbHardDrive, unsigned int tsb_number)
{
    assert(tsbHardDrive);

    if (*tsbHardDrive == -1)
    {
        // Get TSB drive from scheduler
        *tsbHardDrive = Csci_Dvr_GetTsbDrive();
    }

    LOG(DLOGL_NOISE, " tsbHardDrive: %d", *tsbHardDrive);

    snprintf(mtsb_filename, TSB_MAX_FILENAME_SIZE, "/mnt/dvr%d/dvr00%d", *tsbHardDrive, tsb_number + 1);
    LOG(DLOGL_NOISE, "%s : TSB = %s", __FUNCTION__, mtsb_filename);
}


/** *********************************************************
/returns
*/
eMspStatus MSPRecordSession::open(const tCpeSrcHandle srcHandle,
                                  int *tsbHardDrive,
                                  unsigned int tsb_number,
                                  Psi *psi,
                                  int sourceId,
                                  std::string first_fragment_file)
{
    FNLOG(DL_MSP_DVR);

    LOG(DLOGL_NOISE, "srcHandle: %p  tsbHardDrive: %d  tsb_number: %d  sourceId: %d psi=%p",
        srcHandle, *tsbHardDrive, tsb_number, sourceId, psi);

    eMspStatus status = kMspStatus_Ok;
    int err;

    mSourceHandle = srcHandle;
    mPsiptr = psi;

    SetTsbFileName(tsbHardDrive, tsb_number);

    if (!mtsb_filename)
    {
        LOG(DLOGL_ERROR, "Error:  mtsb_filename not set");
        return kMspStatus_Error;
    }

    LOG(DLOGL_NOISE, "tsb_number: %d  mtsb_filename %s", tsb_number, mtsb_filename);

// start CA code
// Create and setup CAM RecordSession
    if (mRecordSession == NULL)
    {
        err = Cam::getInstance()->createRecordSession(&mRecordSession);
        if (err || mRecordSession == NULL)
        {
            LOG(DLOGL_ERROR, "Error Cam createRecordSession: %d  mRecordSession: %p",
                err, mRecordSession);
            return kMspStatus_Error;
        }

        err = mRecordSession->registerDvrMetadataAvailable(caDvrMetadataCallback, this);
        if (err)
        {
            LOG(DLOGL_ERROR, "Error registerDvrMetadataAvailable %d", err);
            return kMspStatus_Error;
        }
        LOG(DLOGL_REALLY_NOISY, "Calling CAM rec session attachService, src ID %d", sourceId);
        err = mRecordSession->attachService(sourceId);
        if (err)
        {
            LOG(DLOGL_ERROR, "Error attachService sourceId: 0x%x  err: %d ", sourceId, err);
        }
    }

    status = openTSB(srcHandle);
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "Error openTSB");
        return status;
    }

    LOG(DLOGL_REALLY_NOISY, "openTSB success!!");

    status = setTSB(first_fragment_file);

    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "setTSB error");
        return status;
    }

    LOG(DLOGL_REALLY_NOISY, "setTSB success!!");

    status = InjectPmtData();
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "Problem with PAT/PMT Injection.Proceeding with TSB anyway.Warning.MRDVR playback won't work for this session's recordings");
    }

    if (mRecordSession != NULL)
    {
        err = mRecordSession->addProgramHandle(mRecHandle);
        if (err)
        {
            LOG(DLOGL_ERROR, "Error addProgramHandle err: %d", err);
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_NOISE, "%s:%d Call recordSession->addProgramHandle Success!!!", __FUNCTION__, __LINE__);
        }
    }

    status = register_callbacks();
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "Error register_callbacks err: %d", status);
    }

    if (mRecordSession != NULL)
    {
        mEntRegId = mRecordSession->registerEntitlementUpdate((void *)this, (EntitlementCallback_p)caEntitlemntCallback);
        if (mEntRegId == -1)
        {
            LOG(DLOGL_ERROR, "Error in Registering for entitlement update");
        }
        else
        {
            LOG(DLOGL_NOISE, " Registered for entitlement updates with registration ID:%d", mEntRegId);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: Recordsession object is null");
        return kMspStatus_Error;
    }
    return status;
}

eMspStatus MSPRecordSession::open(const tCpeSrcHandle srcHandle, int *tsbHardDrive, unsigned int tsb_number,
                                  AnalogPsi *psi, int sourceId, std::string first_fragment_file)
{
    // TODO: Re-visit this method. The interface has changed since 3.1 was started (see the Psi version of open).
    // We will have to re-write this method to conform to the new interface and maintained state of the class
    //dlog(DL_MSP_DVR, DLOGL_ERROR, "%s - %d *** This method is currently unavailable ***", __PRETTY_FUNCTION__, __LINE__);
    //return kMspStatus_Error;

    eMspStatus status = kMspStatus_Ok;
    int ret_value, err;
    UNUSED_PARAM(sourceId);
    (void)first_fragment_file;
    FNLOG(DL_MSP_DVR);

    mSourceHandle = srcHandle;
    // mPsiptr = psi;
    mbIsAnalog = true;

    SetTsbFileName(tsbHardDrive, tsb_number);

    if (!mtsb_filename)
    {
        LOG(DLOGL_ERROR, "Error:  mtsb_filename not set");
        return kMspStatus_Error;
    }

    LOG(DLOGL_NOISE, "tsb_number: %d  mtsb_filename %s", tsb_number, mtsb_filename);

// start CA code
// Create and setup CAM RecordSession
    if (mRecordSession == NULL)
    {
        err = Cam::getInstance()->createRecordSession(&mRecordSession);
        if (err || mRecordSession == NULL)
        {
            LOG(DLOGL_ERROR, "Error Cam createRecordSession: %d  mRecordSession: %p",
                err, mRecordSession);
            return kMspStatus_Error;
        }

        err = mRecordSession->registerDvrMetadataAvailable(caDvrMetadataCallback, this);
        if (err)
        {
            LOG(DLOGL_ERROR, "Error registerDvrMetadataAvailable %d", err);
            return kMspStatus_Error;
        }
        LOG(DLOGL_REALLY_NOISY, "Calling CAM rec session attachService, src ID %d", sourceId);
        err = mRecordSession->attachService(sourceId);
        if (err)
        {
            LOG(DLOGL_ERROR, "Error attachService sourceId: 0x%x  err: %d ", sourceId, err);
        }
    }

    /*    ret_value=get_tsb_file(mtsb_filename,tsb_number);
        if(ret_value==-1)
        {
               dlog(DL_MSP_DVR,DLOGL_ERROR,"unable to get tsb file name for tsb number %d",tsb_number);
               return kMspStatus_Error;
        } */

    status = openTSB(srcHandle);
    if (status != kMspStatus_Ok)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Call RecordSession::openTSB(...) returns error ", __FUNCTION__, __LINE__);
        return status;
    }
    else
    {
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s:%d Call RecordSession::openTSB(...) success!!!", __FUNCTION__, __LINE__);
    }


    status = setTSB(psi);
    if (status != kMspStatus_Ok)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Call RecordSession::setTSB(...) returns error ", __FUNCTION__, __LINE__);
        return status;
    }
    else
    {
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s:%d Call RecordSession::setTSB(...) success!!!", __FUNCTION__, __LINE__);
    }

    if (mRecordSession != NULL)
    {
        ret_value = mRecordSession->addProgramHandle(mRecHandle);
        if (ret_value != 0)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Could not add prog. handle.  err: %d ", __FUNCTION__, __LINE__, ret_value);
        }
        else
            dlog(DL_MSP_DVR, DLOGL_NORMAL, "%s:%d Call recordSession->addProgramHandle Success!!!", __FUNCTION__, __LINE__);
    }

    status = register_callbacks();
    if (status != kMspStatus_Ok)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d RecordSession::register_callbacks(...) return error ", __FUNCTION__, __LINE__);
    }

    return status;
}

eMspStatus MSPRecordSession::restartForPmtUpdate(eRecordSessionState RecState)
{
    eMspStatus status;
    LOG(DLOGL_NORMAL, "%s, %d", __FUNCTION__, __LINE__);
    status = setTSB("", true, true);

    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "setTSB error");
        // return status;
    }

    LOG(DLOGL_REALLY_NOISY, "setTSB success!!");

    status = InjectPmtData();
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "Error InjectPmtData err: %d", status);
    }

    status = register_callbacks();
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "Error register_callbacks err: %d", status);
    }
    mState = RecState;

    return status;
}

/** *********************************************************
/returns
*/
eMspStatus MSPRecordSession::start()
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    int err;

    //Store PMT info on Record Handle -- Required for Cable Card

    mPsiptr->lockMutex();

    Pmt *pmt = mPsiptr->getPmtObj();
    mPgmNo = mPsiptr->getProgramNo();
    if (pmt == NULL)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "PMT structure is NULL,Not able to store metadata");
        mPsiptr->unlockMutex();
        return kMspStatus_Error;
    }

    status = pmt->getPmtInfo(&mPmtInfo);
    if (status != kMspStatus_Ok)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "Not able to get PMTInfo,Not able to store metadata");
        mPsiptr->unlockMutex();
        return kMspStatus_Error;
    }

    err = cpe_ProgramHandle_Set(mRecHandle, eCpePgrmHandleNames_Pmt, (void *)&mPmtInfo);
    if (err != kCpe_NoErr)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "DisplaySession::%s:%d cpe_ProgramHandle_Set[eCpePgrmHandleNames_Pmt] Failed rc=%d", __FUNCTION__, __LINE__, err);
        status = kMspStatus_CpeMediaError;
    }

    uint32_t progNum = mPgmNo;

    err = cpe_ProgramHandle_Set(mRecHandle, eCpePgrmHandleNames_ProgramNumber, (void *)&progNum);
    if (err != kCpe_NoErr)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "DisplaySession::%s:%d cpe_ProgramHandle_Set[eCpePgrmHandleNames_ProgramNumber] Failed rc=%d", __FUNCTION__, __LINE__, err);
        status = kMspStatus_CpeMediaError;
    }
    else
    {
        LOG(DLOGL_NOISE, "Set Program No %d, handle %p", progNum, mRecHandle);
    }

    if (mRecordSession != NULL)
    {
        err = mRecordSession->startRecording();
        if (err != 0)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d CA recordSession startRecording error.  code %d ", __FUNCTION__, __LINE__, err);
            status = kMspStatus_CpeRecordError;
            mPsiptr->unlockMutex();
            return status;
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_NOISE, "%s:%d Call recordSession->startRecording(...) success!!!", __FUNCTION__, __LINE__);
        }
    }
    if (status == kMspStatus_Ok)
        status = startTSB();

    if (status != kMspStatus_Ok)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Call RecordSession::startTSB(...) returns error ", __FUNCTION__, __LINE__);
        mPsiptr->unlockMutex();
        return status;
    }
    else
    {
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s:%d Call RecordSession::startTSB(...) success!!!", __FUNCTION__, __LINE__);
    }
    mPsiptr->unlockMutex();
    return status;
}


eMspStatus MSPRecordSession::start(AnalogPsi *psi)
{
    psi = psi;

    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    int err;

    if (mRecordSession != NULL)
    {
        err = mRecordSession->startRecording();
        if (err != 0)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d CA recordSession startRecording error.  code %d ", __FUNCTION__, __LINE__, err);
        }
        else
            dlog(DL_MSP_DVR, DLOGL_NORMAL, "%s:%d Call recordSession->startRecording(...) success!!!", __FUNCTION__, __LINE__);
    }

    status = startTSB();
    if (status != kMspStatus_Ok)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Call RecordSession::startTSB(...) returns error ", __FUNCTION__, __LINE__);
        return status;
    }
    else
    {
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s:%d Call RecordSession::startTSB(...) success!!!", __FUNCTION__, __LINE__);
    }

    return status;
}

eMspStatus MSPRecordSession::stopForPmtUpdate(eRecordSessionState * pRecState)
{
    int err;
    eMspStatus status = kMspStatus_Ok;

    FNLOG(DL_MSP_DVR);
    LOG(DLOGL_NORMAL, "%s, %d", __FUNCTION__, __LINE__);

    err = cpe_record_CancelInjectData(mRecHandle, mInjectPat);
    if (err != kCpe_NoErr)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_CancelInjectData(...)  for PAT return error %d, with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
        syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
               "DLOG|MSP|Recording Failure|%s:err cpe_record_CancelInjectData err %d",
               __FUNCTION__, err);
    }

    err = cpe_record_CancelInjectData(mRecHandle, mInjectPmt);
    if (err != kCpe_NoErr)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_CancelInjectData(...)  for PMT return error %d, with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
        syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
               "DLOG|MSP|Recording Failure|%s:err cpe_record_CancelInjectData err %d",
               __FUNCTION__, err);
    }

    status = unregister_callbacks();
    if (status != kMspStatus_Ok)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d RecordSession::unregister_callbacks(...) return error ", __FUNCTION__, __LINE__);
    }

    else
    {
        dlog(DL_MSP_DVR, DLOGL_NOISE, "%s:%d Call RecordSession::unregister_callbacks(...) success!!!", __FUNCTION__, __LINE__);
    }
    *pRecState = mState;
    mState = kRecordSessionOpened;
    return status;

}

/** *********************************************************
/returns
*/
eMspStatus MSPRecordSession::stop()
{
    int ret;
    eMspStatus status = kMspStatus_Ok;

    FNLOG(DL_MSP_DVR);

    status = stopTSB();
    if (status != kMspStatus_Ok)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Call RecordSession::stop(...) returns error ", __FUNCTION__, __LINE__);
        return status;
    }
    else
    {
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s:%d Call RecordSession::stop(...) success!!!", __FUNCTION__, __LINE__);
    }

    status = unregister_callbacks();
    if (status != kMspStatus_Ok)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d RecordSession::unregister_callbacks(...) return error ", __FUNCTION__, __LINE__);
    }

    else
    {
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s:%d Call RecordSession::unregister_callbacks(...) success!!!", __FUNCTION__, __LINE__);
    }

    if (mRecordSession != NULL)
    {
        mRecordSession->unRegisterEntitlementUpdate(mEntRegId);

        if ((mCaPid != 0) && !UseCableCardRpCAK())
        {
            status = stopCaFilter();
            if (status != kMspStatus_Ok)
            {
                dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Error calling stopCASectionFilter. code %d ", __FUNCTION__, __LINE__, status);
            }
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "CA Pid is 0 which means CA filter is not started\n");
        }

        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s:%d Call CA stopRecording", __FUNCTION__, __LINE__);
        ret = mRecordSession->stopRecording();
        if (ret != 0)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Error calling CA stopRecording. code %d ", __FUNCTION__, __LINE__, ret);
        }
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s:%d Call CA disconnectAsset", __FUNCTION__, __LINE__);
        ret = mRecordSession->disconnectCaAsset(mtsb_filename);
        if (ret != 0)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Error calling CA disconnectAsset. code %d ", __FUNCTION__, __LINE__, ret);
        }
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s:%d Call CA destroySession", __FUNCTION__, __LINE__);
        ret = mRecordSession->destroySession();
        if (ret != 0)
        {
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s:%d Error calling CA destroySession. code %d ", __FUNCTION__, __LINE__, ret);
        }
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s:%d set CA session NULL", __FUNCTION__, __LINE__);
        mRecordSession = NULL;
    }

    return status;
}


/** *********************************************************
/returns
*/
eMspStatus MSPRecordSession::close()
{
    FNLOG(DL_MSP_DVR);

    // TODO:  Reconsider this one-line function

    return closeTSB();
}


/** *********************************************************
/returns
*/
eMspStatus MSPRecordSession::startConvert(std::string recfilename, uint32_t nptRecordStartTime, uint32_t nptRecordStopTime)
{

    FNLOG(DL_MSP_DVR);

    LOG(DLOGL_NOISE, "file: %s  nptStart: %d  nptStop: %d", recfilename.c_str(), nptRecordStartTime, nptRecordStopTime);

    eMspStatus status = kMspStatus_Ok;
    int err;
    unsigned int timeout_ms = 0;

    Pmt *pmt = NULL;
    if (mPsiptr)
    {
        LOG(DLOGL_NOISE, "mPsiptr: %p ", mPsiptr);

        mPsiptr->lockMutex();
        pmt = mPsiptr->getPmtObj();


        if (pmt)
        {
            status = pmt->getPmtInfo(&mPmtInfo);

            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "Error: Unable to get PMTInfo");
                mPsiptr->unlockMutex();
                syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                       "DLOG|MSP|Recording Failure|%s: unable to get PMTInfo",
                       __FUNCTION__);
                return kMspStatus_Error;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "ERROR: Null PMT");
            mPsiptr->unlockMutex();
            syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                   "DLOG|MSP|Recording Failure|%s: null PMT",
                   __FUNCTION__);
            return kMspStatus_Error;
        }
        mPsiptr->unlockMutex();
    }
    else
    {
        LOG(DLOGL_ERROR, "mPsiptr is NULL");
    }
    switch (mState)
    {
    case kRecordSessionStarted:
    {
        if (mIsCAMetaWritten == false)
        {
            /* Wait for CS metadata --- this handles the condition when immediately on channel change recording is started*/
            while (!mIsCAMetaWritten && (timeout_ms < 50000))
            {
                usleep(10000);
                timeout_ms += 100;
            }
            LOG(DLOGL_NOISE, "CA metadata available = %d", mIsCAMetaWritten);
        }

        if (mIsCAMetaWritten)
        {
            err = cpe_record_TSBConversionStart(mRecHandle, recfilename.c_str(), nptRecordStartTime, nptRecordStopTime);
            if (err != kCpe_NoErr)
            {
                LOG(DLOGL_ERROR, "cpe_record_TSBConversionStart error %d with record handle %d", err, (int) mRecHandle);
                syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                       "DLOG|MSP|Recording Failure|%s: cpe_record_TSBConversionStart err %d",
                       __FUNCTION__, err);
                status = kMspStatus_CpeRecordError;
                return status;
            }
            else
            {
                LOG(DLOGL_NOISE, "cpe_record_TSBConversionStart success!");
                mState = kRecordSessionConversionStarted;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Error CA Metadata storage failed");
            status = kMspStatus_CpeRecordError;
            syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                   "DLOG|MSP|Recording Failure|%s: CA Metadata storage failed, %d",
                   __FUNCTION__, status);
            return status;
        }

        err = writeAllMetaData(recfilename);
        if (err != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "Error writeAllMetaData failed. error %d with record handle %d ", err, (int) mRecHandle);
            syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                   "DLOG|MSP|Recording Failure|%s:writing meta data failed, %d",
                   __FUNCTION__, err);
        }

    }
    break;

    default:
        LOG(DLOGL_ERROR, "Error Wrong state, current state %d ", mState);
        syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
               "DLOG|MSP|Recording Failure|%s: wrong state, current state %d",
               __FUNCTION__, mState);
        status = kMspStatus_StateError;
        break;
    }
    return  status;
}

/** *********************************************************
/returns
*/
eMspStatus MSPRecordSession::startConvert(std::string recfilename, uint32_t nptRecordStartTime, uint32_t nptRecordStopTime, AnalogPsi *psi)
{
    psi = psi;
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    status = startConvertAnalogTSB(recfilename, nptRecordStartTime, nptRecordStopTime);
    if (status != kMspStatus_Ok)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Call RecordSession::startConvertAnalogTSB(...) returns error ", __FUNCTION__, __LINE__);
        return status;
    }
    else
    {
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s:%d Call RecordSession::startConvertAnalogTSB(...) success!!!", __FUNCTION__, __LINE__);
    }

    return status;
}


/** *********************************************************
/returns
*/
eMspStatus MSPRecordSession::stopConvert()
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;

    switch (mState)
    {
    case kRecordSessionConversionStarted:
    {
        int err = cpe_record_TSBConversionStop(mRecHandle);
        if (err)
        {
            LOG(DLOGL_ERROR, "Error cpe_record_TSBConversionStop %d mRecHandle %p ", err, mRecHandle);
            syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                   "DLOG|MSP|Recording Failure|%s: err stopping recording %d",
                   __FUNCTION__, err);
            status = kMspStatus_CpeRecordError;
        }
        else
        {
            LOG(DLOGL_NOISE, "cpe_record_TSBConversionStop  mRecHandle %p success!!", mRecHandle);
            //restoring to original TSB running freely state with no conversion
            mState = kRecordSessionStarted;
        }
    }
    break;

    default:
        LOG(DLOGL_ERROR, "Error Bad state: %d ", mState);
        syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
               "DLOG|MSP|Recording Failure|%s:Bad state %d",
               __FUNCTION__, mState);
        status = kMspStatus_StateError;
    }

    return  status;

}

/** *********************************************************
/returns
*/
eMspStatus MSPRecordSession::GetCurrentNpt(uint32_t* pNpt)
{
    assert(pNpt);

    int status = cpe_record_Get(mRecHandle, eCpeRecNames_CurrentNPT, pNpt, sizeof(uint32_t));
    if (status != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "cpe_record_Get error: %d", status);
        return kMspStatus_Error;
    }
    else
    {
        LOG(DLOGL_NOISE, "current NPT: %d", *pNpt);
        return kMspStatus_Ok;
    }
}


eMspStatus MSPRecordSession::GetStartNpt(uint32_t* pNpt)
{
    int status;

    status = cpe_record_Get(mRecHandle, eCpeRecNames_StartNPT, pNpt, sizeof(uint32_t));
    if (status != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "cpe_record_Get error: %d", status);
        return kMspStatus_Error;
    }
    else
    {
        LOG(DLOGL_NOISE, "start NPT: %d", *pNpt);
    }

    return kMspStatus_Ok;
}




/** *********************************************************
/returns
*/
eMspStatus MSPRecordSession::openTSB(const tCpeSrcHandle srcHandle)
{
    FNLOG(DL_MSP_DVR);

    if (!srcHandle)
    {
        LOG(DLOGL_ERROR, "Error null srcHandle");
        return kMspStatus_BadParameters;
    }

    eMspStatus status = kMspStatus_Ok;

    switch (mState)
    {
    case kRecordSessionIdle:
    case kRecordSessionClosed:
    {
        int err = cpe_record_Open(srcHandle, eCpeRecType_TSBBuffer, mtsb_filename, &mRecHandle);
        if (err)
        {
            LOG(DLOGL_ERROR, "Error cpe_record_Open err %d, srcHandle: %p  mtsb_filename: %s ", err,  srcHandle, mtsb_filename);
            syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                   "DLOG|MSP|Recording Failure|%s: cpe_record_Open err %d",
                   __FUNCTION__, err);
            status = kMspStatus_CpeRecordError;
        }
        else
        {
            LOG(DLOGL_NOISE, "cpe_record_Open mtsb_filename: %s  mRecHandle: %p ", mtsb_filename, mRecHandle);
            mState = kRecordSessionOpened;
        }
    }
    break;

    default:
        LOG(DLOGL_ERROR, "Error: Bad state %d ", mState);
        syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
               "DLOG|MSP|Recording Failure|%s: err: bad state %d",
               __FUNCTION__, mState);
        status = kMspStatus_StateError;
        break;
    }

    return  status;
}

/** *********************************************************
/returns
*/
eMspStatus MSPRecordSession::setTSB(std::string first_fragment_file, bool bUseCaBlob, bool bRemapPids)
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    int err;


    switch (mState)
    {
    case kRecordSessionOpened:
    {
        //allocating PIDS size for 15 PIDS.TODO-Need to get to know number of PIDS in the stream from PSI and allocate that much
        int pidListSize = 0;
        Pmt *pmt = mPsiptr->getPmtObj();
        if (pmt)
        {
            std::list<tPid> *audioList = pmt->getAudioPidList();
            if (audioList)
            {
                pidListSize = pidListSize + audioList->size();
            }
            std::list<tPid> *videoList = pmt->getVideoPidList();
            if (videoList)
            {
                pidListSize = pidListSize + videoList->size();
            }
            // Increment pidListSize for Clock
            ++pidListSize;
        }
        if (pidListSize == 0)
        {
            return kMspStatus_Error;
        }

        tCpeSrcPFPidDef * pOldPids = mPids;

        mPids = (tCpeSrcPFPidDef *)malloc(sizeof(tCpeSrcPFPidDef) + sizeof(tCpeSrcPFPidStruct) * pidListSize);
        if (mPids == NULL)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Could not allocate mPids", __FUNCTION__, __LINE__);
            return kMspStatus_Error;
        }
        mPids->numOfPids = 0;

        status = getAudioPid();
        if (status != kMspStatus_Ok)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Call RecordSession::getAudioPid(...) returns error ", __FUNCTION__, __LINE__);
            return status;
        }

        status = getClockPid();
        if (status != kMspStatus_Ok)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Call RecordSession::getClockPid(...) returns error ", __FUNCTION__, __LINE__);
            return status;
        }
        //Need to call Video Pids last,as it also triggers CA callback.By that time,we need all Pids info.
        status = getVideoPid(first_fragment_file, bUseCaBlob, bRemapPids);
        if (status != kMspStatus_Ok)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Call RecordSession::getVideoPid(...) returns error ", __FUNCTION__, __LINE__);
            return status;
        }
        LOG(DLOGL_REALLY_NOISY, "mPids %p, bRemapPids %d, pOldPids %p", mPids, bRemapPids, pOldPids);
        uint32_t m = 0;  // used for index of remap file
        if (mPids && bRemapPids && pOldPids)
        {
            tCpeRecPIDRemapList *pidRemap;
            pidRemap = (tCpeRecPIDRemapList *)malloc(sizeof(tCpeRecPIDRemapList) + sizeof(tCpeRecPIDRemap) * mPids->numOfPids);

            pidRemap->numOfPids = mPids->numOfPids;

            uint32_t i = 0, j = 0;

            for (i = 0; i < mPids->numOfPids; i++)
            {
                for (j = 0; j < pOldPids->numOfPids; j++)
                {
                    if (mPids->pids[i].type == pOldPids->pids[j].type &&
                            pOldPids->pids[j].pid != mPids->pids[i].pid)
                    {
                        pidRemap->remap[m].remapPID = mPids->pids[i].pid ;
                        pidRemap->remap[m].type = mPids->pids[i].type;

                        pidRemap->remap[m].originalPID = pOldPids->pids[j].pid;

                        LOG(DLOGL_NORMAL, "Remap pid 0x%x, type 0x%x, original pid 0x%x, total Remap pid %d",
                            pidRemap->remap[m].remapPID, pidRemap->remap[m].type, pidRemap->remap[m].originalPID, (m + 1));
                        m++;
                        break;
                    }
                }
            }
            LOG(DLOGL_NORMAL, "Total number of remapped pids %d", m);
            pidRemap->numOfPids = m;

            err = cpe_record_Set(mRecHandle, eCpeRecNames_RemapPID, pidRemap);
            if (err != kCpe_NoErr)
            {
                dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_Set(...) return error %d with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
                status = kMspStatus_CpeRecordError;
                mState = kRecordSessionConfigured;
                //return status;
            }
            else
            {
                mState = kRecordSessionConfigured;
                dlog(DL_MSP_DVR, DLOGL_NOISE, "RecordSession::%s:%d Call cpe_record_Set(...) Success  ", __FUNCTION__, __LINE__);
            }

            // clean-up memory after RP calls
            free(pOldPids);
            free(pidRemap);
        }
        else
        {

            err = cpe_record_Set(mRecHandle, eCpeRecNames_PIDNumbers, mPids);
            if (err != kCpe_NoErr)
            {
                dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_Set(...) return error %d with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
                status = kMspStatus_CpeRecordError;
                mState = kRecordSessionConfigured;
                //return status;
            }
            else
            {
                mState = kRecordSessionConfigured;
                dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "RecordSession::%s:%d Call cpe_record_Set(...) Success  ", __FUNCTION__, __LINE__);
            }

            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "RecordSession::%s:%d Number of pids is %d ", __FUNCTION__, __LINE__, mPids->numOfPids);
        }
// add all program pids (only audio, video, PCR) to the record session_number
        if (mRecordSession != NULL)
        {
            for (unsigned int i = 0; i < mPids->numOfPids; i++)
            {
                dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "RecordSession::%s:%d Addstream %d pid %d", __FUNCTION__, __LINE__, i, mPids->pids[i].pid);
                mRecordSession->addStream(mPids->pids[i].pid);
            }
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "RecordSession::%s:%d Done calling addstream", __FUNCTION__, __LINE__);
        }

    }
    break;
    default:
        dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Wrong state, current state %d ", __FUNCTION__, __LINE__, mState);
        status = kMspStatus_StateError;
        break;
    }

    return  status;
}


eMspStatus MSPRecordSession::setTSB(AnalogPsi *psi)
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    int err;
    dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s:%d Entered fucntion AnalogTSB setTSB ", __FUNCTION__, __LINE__);
    switch (mState)
    {
    case kRecordSessionOpened:
    {
#if 1
        //allocating PIDS size for 15 PIDS.TODO-Need to get to know number of PIDS in the stream from PSI and allocate that much
        mPids = (tCpeSrcPFPidDef *)malloc(sizeof(tCpeSrcPFPidDef) + sizeof(tCpeSrcPFPidStruct) * 3);
        if (mPids == NULL)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Could not allocate mPids", __FUNCTION__, __LINE__);
            return kMspStatus_Error;
        }
        memcpy(mPids, psi->getPids(), sizeof(tCpeSrcPFPidDef) + sizeof(tCpeSrcPFPidStruct) * 3);
        // dlog(DL_MSP_DVR,DLOGL_NORMAL,"RecordSession::%s:%d Total Pid in Analog PSI %d",__FUNCTION__,__LINE__, psi->mPids->numOfPids);

#endif


        //saving PIDS data upfront,so that be ready by the time CA callback arrives for the CA metadata
        status = savePidsMetaData();
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Saving Pids metadata failed");
        }
        else
        {
            LOG(DLOGL_NOISE, "Saving Pids metadata success");
            if (mRecordSession)
            {
                uint8_t *descriptorAddress;
                uint32_t descriptorSize;
                // setup CA system with CA descriptor info
                mScramblingMode = 0;                         // default values
                descriptorAddress = NULL;
                descriptorSize = 0;

                //dlog(DL_MSP_DVR,DLOGL_REALLY_NOISY,"CA addPSIData: %p %d %d",descriptorAddress,descriptorSize,mScramblingMode);
                int ret_value = Cam::getInstance()->addPsiData(*mRecordSession, descriptorAddress, descriptorSize, mScramblingMode);
                if (ret_value != 0)
                {
                    dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d addPSIData failed err=%d", __FUNCTION__, __LINE__, ret_value);
                }

                //  if(first_fragment_file == "")        //Normal recording.Asks CA to create a fresh CA blob for the recording.
                {
                    dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Normal recording , hence calling up CA createCaAsset passing TSB file %s", mtsb_filename);
                    ret_value = mRecordSession->createCaAsset(mtsb_filename);
                    if (ret_value != 0)
                    {
                        LOG(DLOGL_ERROR, "RecordSession::Could not create ca asset. err=%d", ret_value);
                    }
                    else
                    {
                        LOG(DLOGL_REALLY_NOISY, "CA createCaAsset returned %d", ret_value);
                    }
                }
            }
        }


        err = cpe_record_Set(mRecHandle, eCpeRecNames_PIDNumbers, psi->getPids());
        if (err != kCpe_NoErr)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_Set(...) return error %d with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
            status = kMspStatus_CpeRecordError;
            return status;
        }
        else
        {
            mState = kRecordSessionConfigured;
            dlog(DL_MSP_DVR, DLOGL_NORMAL, "RecordSession::%s:%d Call cpe_record_Set(...) Success  ", __FUNCTION__, __LINE__);
        }


        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "RecordSession::%s:%d Number of pids is %d ", __FUNCTION__, __LINE__, mPids->numOfPids);

// add all program pids (only audio, video, PCR) to the record session_number
        if (mRecordSession != NULL)
        {
            for (unsigned int i = 0; i < mPids->numOfPids; i++)
            {
                dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "RecordSession::%s:%d Addstream %d pid %d", __FUNCTION__, __LINE__, i, mPids->pids[i].pid);
                mRecordSession->addStream(mPids->pids[i].pid);
            }
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "RecordSession::%s:%d Done calling addstream", __FUNCTION__, __LINE__);
        }

    }
    break;
    default:
        dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Wrong state, current state %d ", __FUNCTION__, __LINE__, mState);
        status = kMspStatus_StateError;
        break;
    }

    return  status;
}



/** *********************************************************
/returns
*/
eMspStatus MSPRecordSession::getAudioPid()
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    //getting audio pid informations

    mPsiptr->lockMutex();

    LanguageSelection langSelect(LANG_SELECT_AUDIO, mPsiptr);
    tPid audioPidStruct = langSelect.pidSelected();

    dlog(DL_MSP_DVR, DLOGL_NOISE, "audio pid type value is %d", audioPidStruct.streamType);
    Pmt *pmt = mPsiptr->getPmtObj();
    mUseAC3 = 0;
    int j = 0; // index for audio pid
    if (pmt)
    {
        std::list<tPid> *audioList = pmt->getAudioPidList();
        std::list<tPid>::iterator iter;
        for (iter = audioList->begin(); iter != audioList->end(); iter++)
        {
            audioPidStruct = (*iter);
            switch (audioPidStruct.streamType)
            {
            case kCpeStreamType_MPEG1_Audio:
            case kCpeStreamType_MPEG2_Audio:
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_AudMpegPid;
                break;
            case kCpeStreamType_GI_Audio:
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_AudAC3Pid;
                mUseAC3 = mUseAC3 | (1 << j);
                break;
            case kCpeStreamType_DDPlus_Audio:
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_AudAC3Pid;
                break;

            case kCpeStreamType_AAC_Audio:
            case kCpeStreamType_AACplus_Audio:          //Not sure about,whether this case means AC3.TODO-verify it
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_AudAACPid;
                break;
            case kCpeStreamType_LPCM:
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_AudLPCMPid;
                break;
            default:
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_AudMpegPid;
                break;
            }
            mPids->pids[mPids->numOfPids].hasPCR = false;
            mPids->pids[mPids->numOfPids].pid = audioPidStruct.pid;
            mPids->numOfPids++;
            j++;
        }
    }

    mPsiptr->unlockMutex();

    return  status;
}


/** *********************************************************
/returns
*/
eMspStatus MSPRecordSession::getVideoPid(std::string first_fragment_file, bool bUseCaBlob, bool bRemapPids)
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    tPid videoPidStruct = {0, 0};
    int ret_value;
    tCpePgrmHandleMpegDesc cakDescriptor;
    tCpePgrmHandleMpegDesc cakSystemDescriptor;
    uint8_t *descriptorAddress;
    uint32_t descriptorSize;


    Pmt *pmt = mPsiptr->getPmtObj();
    if (pmt)
    {
        std::list<tPid>* videoList = pmt->getVideoPidList();
        //no for loop to handle the list??.Expecting just one PID?
        int n = videoList->size();
        if (n == 0)
        {
            dlog(DL_MSP_DVR, DLOGL_SIGNIFICANT_EVENT, "DisplaySession::%s:%d No video pid.Recording not supported ", __FUNCTION__, __LINE__);
            return  kMspStatus_NotSupported;
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_NOISE, "RecordSession::%s:%d number of video pid in the list is %d ", __FUNCTION__, __LINE__, n);
            std::list<tPid>::iterator iter = videoList->begin();

            //Choose the first one, don't know what to do if we have more than one video pid
            videoPidStruct = (tPid) * iter;

            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "video pid type value is %d", videoPidStruct.streamType);
            switch (videoPidStruct.streamType)
            {
            case kCpeStreamType_MPEG1_Video:
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_VidMPEG1Pid;
                break;


            case kCpeStreamType_MPEG2_Video:
            case kCpeStreamType_GI_Video:
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_VidMpeg2Pid;
                break;

            case kCpeStreamType_H264_Video:
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_VidH264Pid;
                break;

            case kCpeStreamType_VC1_Video:
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_VidVC1Pid;
                break;

            default:
                //mPids->pids[mPids->numOfPids].type=eCpeSrcPFPidType_OtherPid;
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_VidMpeg2Pid; //we are getting PID type value as 128.This seems to be wrong.So used a temporary workaround.
                // TODO needs to be verified and fixed
                break;
            }

            mPids->pids[mPids->numOfPids].hasPCR = false;
            mPids->pids[mPids->numOfPids].pid = videoPidStruct.pid;
            mPids->numOfPids++;
        }

        //saving PIDS data upfront,so that be ready by the time CA callback arrives for the CA metadata
        status = savePidsMetaData();
        if (bRemapPids)
        {
            return status;
        }
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Saving Pids metadata failed");
        }
        else
        {
            LOG(DLOGL_NOISE, "Saving Pids metadata success");
            if (mRecordSession)
            {
                // setup CA system with CA descriptor info
                mScramblingMode = 0;                         // default values
                descriptorAddress = NULL;
                descriptorSize = 0;

                // get the CA descriptor from the PMT
                cakDescriptor.tag = 0x9;
                cakDescriptor.dataLen = 0;
                cakDescriptor.data = NULL;


                status = mPsiptr->getPmtObj()->getDescriptor(&cakDescriptor, videoPidStruct.pid);
                if (status != kMspStatus_Ok)
                {
                    dlog(DL_MSP_DVR, DLOGL_NOISE, "CA descriptor not found in PMT: %d ", status);
                    descriptorAddress = mPsiptr->getCADescriptorPtr();
                    descriptorSize = mPsiptr->getCADescriptorLength();
                    status = kMspStatus_Ok;         // fixup status for return since this is not really an error
                }
                else
                {
                    mCaSystem = (cakDescriptor.data[0] << 8) + cakDescriptor.data[1];
                    if (!UseCableCardRpCAK())
                    {
                        mCaPid = ((cakDescriptor.data[2] << 8) + cakDescriptor.data[3]) & 0x1fff;
                        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "CA Descriptor: ecmpid: 0x%x sys: 0x%x len: %d: ", mCaPid, mCaSystem, cakDescriptor.dataLen);
                    }

                    descriptorAddress = cakDescriptor.data;
                    descriptorSize = cakDescriptor.dataLen;

                    // now get the CA system descriptor
                    cakSystemDescriptor.tag = 0x65;
                    cakSystemDescriptor.dataLen = 0;
                    cakSystemDescriptor.data = NULL;

                    status = mPsiptr->getPmtObj()->getDescriptor(&cakSystemDescriptor, videoPidStruct.pid);
                    if (status != kMspStatus_Ok)
                    {
                        dlog(DL_MSP_DVR, DLOGL_NOISE, "CA descriptor not found in PMT: %d ", status);
                        status = kMspStatus_Ok;         // fixup status for return since this is not really an error
                    }
                    else
                    {

                        mScramblingMode = cakSystemDescriptor.data[0];
                        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "CA scrambling mode: 0x%x", mScramblingMode);
                        mPsiptr->getPmtObj()->releaseDescriptor(&cakSystemDescriptor);
                    }
                }
                dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "CA addPSIData: %p %d %d", descriptorAddress, descriptorSize, mScramblingMode);
                ret_value = Cam::getInstance()->addPsiData(*mRecordSession, descriptorAddress, descriptorSize, mScramblingMode);
                if (ret_value != 0)
                {
                    dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d addPSIData failed err=%d", __FUNCTION__, __LINE__, ret_value);
                }

                mPsiptr->getPmtObj()->releaseDescriptor(&cakDescriptor);

                /*Normal recording.Asks CA to create a fresh CA blob for the recording.
                CSCuo28421/CSCup46423 : Also added check for interrupted recording with first fragment missing, it should consider the second fragment as new recording and proceed with new Ca Asset*/
                int fileExists = access((char *)first_fragment_file.c_str(), F_OK);

                if (bUseCaBlob)
                {
                    ret_value = mRecordSession->connectDvrMetadata(mCaMetaDataPtr, mCaMetaDataSize);
                    if (ret_value != 0)
                    {
                        LOG(DLOGL_ERROR, "RecordSession::Could not use exisitng CA blon? err=%d", ret_value);
                    }
                }
                else if (first_fragment_file == "" || (fileExists == -1))
                {
                    if (fileExists != -1)
                        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Normal recording , hence calling up CA createCaAsset passing TSB file %s", mtsb_filename);
                    else
                        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Interrupted rec with missing fragment %s, hence \
												calling up CA createCaAsset passing TSB file %s", first_fragment_file.c_str(), mtsb_filename);
                    ret_value = mRecordSession->createCaAsset(mtsb_filename);
                    if (ret_value != 0)
                    {
                        LOG(DLOGL_ERROR, "RecordSession::Could not create ca asset. err=%d", ret_value);
                    }
                    else
                    {
                        LOG(DLOGL_REALLY_NOISY, "CA createCaAsset returned %d", ret_value);
                    }
                }
                else                                  //interrupted recording.Asks CA to use original CA blob of first fragment.
                {

                    ret_value = mRecordSession->connectCaAsset(mtsb_filename);
                    if (ret_value != 0)
                    {
                        LOG(DLOGL_ERROR, "RecordSession::Could not connect ca asset. err=%d", ret_value);
                    }
                    else
                    {
                        LOG(DLOGL_REALLY_NOISY, "CA connectCaAsset returned %d", ret_value);
                    }


                    dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Interrupted recording , hence calling up CA ReadMrdvrMetadata passing fragment file  %s", first_fragment_file.c_str());
                    tCpeRecDataBase *DvrBlobDb = NULL;
                    tCpeRecDataBaseType *CaDvrBlobdb = NULL;

                    //retrieve the DVR metadata of the original fragment.
                    status = ReadMrdvrMetaData((char *)first_fragment_file.c_str(), &DvrBlobDb);
                    if (status == kMspStatus_Ok)
                    {
                        LOG(DLOGL_REALLY_NOISY, "first record fragment for DVR blob retrieval success");

                        //retrieve the CA blob.
                        eBlobType dvr_blob;  //unused here though
                        CaDvrBlobdb  = GetDecryptCABlob(DvrBlobDb , &dvr_blob);
                        if (CaDvrBlobdb != NULL)
                        {

                            //pass it up to CA
                            ret_value = mRecordSession->connectDvrMetadata((const uint8_t * const) CaDvrBlobdb->dataBuf, CaDvrBlobdb->size);
                            if (ret_value != 0)
                            {
                                dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d connectDvrMetadata failed. err=%d", __FUNCTION__, __LINE__, ret_value);
                            }
                            else
                            {
                                dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "CA createCaAsset returned %d", ret_value);
                            }

                            dlog(DL_MSP_DVR, DLOGL_NOISE, "Freeing the CAM blob memories allocated");
                        }
                        else
                        {
                            LOG(DLOGL_ERROR, "Unable to fetch the CAM blob from DVR metadata");

                        }
                    }
                    else
                    {
                        LOG(DLOGL_ERROR, "Unable to open the first record fragment for DVR blob");
                    }

                    if (DvrBlobDb != NULL)
                        delete [] DvrBlobDb;

                    if (CaDvrBlobdb != NULL)
                        delete CaDvrBlobdb;

                }


                if (mCaPid != 0 && !UseCableCardRpCAK())
                {
                    startCaFilter(mCaPid, mSourceHandle);
                }
            }
        }
    }
    else
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Got null pmt object ", __FUNCTION__, __LINE__);
        status = kMspStatus_PsiError;
    }

    return  status;

}



/** *********************************************************
/returns
*/
eMspStatus MSPRecordSession::getClockPid()
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    Pmt *pmt = mPsiptr->getPmtObj();
    if (pmt)
    {
        mPids->pids[mPids->numOfPids].hasPCR = true;
        mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_OtherPid; //The sample application uses this value for clock PID type.Don't know why.TODO-verify with them
        mPids->pids[mPids->numOfPids].pid = pmt->getPcrpid();
        mPids->numOfPids++;
    }
    else
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Got null pmt object ", __FUNCTION__, __LINE__);
        status = kMspStatus_PsiError;
    }
    return  status;

}

/** *********************************************************
*   This function writes all the metadata sections to the
recording metadata file*
*
* As of now, there are only 2 sections, the MSP section and the
* CA section.  They are acquired at different times.  This
* function combines them, builds the metadata "database" header
* and writes it
*/

eMspStatus  MSPRecordSession::writeAllMetaData(std::string filename)
{
    int err;
    tCpeRecDataBase *metaDataDBPtr;
    unsigned int metaDataSize;
    uint8_t *mspStartAddress = NULL;
    uint8_t *caStartAddress = NULL;

    FNLOG(DL_MSP_DVR);

    LOG(DLOGL_NOISE, "filename: %s ", filename.c_str());

    if (mState != kRecordSessionStarted)
    {
        LOG(DLOGL_NOISE, "Warning: state: %d - proceeding anyway", mState);
    }

    // TODO:  If we have a new CA descriptor defined from the CA callback update our pid table and PMT!!!!
    // THIS IS IMPORTANT


// base size is size of CPERP DB structure
    metaDataSize = sizeof(tCpeRecDataBase);
    metaDataSize += mDbPidsSize;
    metaDataSize += mCaMetaDataSize;
    metaDataSize += mCaDescriptorLength;

    //FInd out if there is caption service descriptor
    // getting and save caption service descriptor for playback to select the language for closec captioning
    tCpePgrmHandleMpegDesc captionDesc;

    Pmt *pmt = NULL;
    if (!mbIsAnalog && mPsiptr)
    {
        pmt = mPsiptr->getPmtObj();
    }
    mCaptionDescriptorLength = 0;
    if (pmt)
    {
        std::list<tPid> * videoList = pmt->getVideoPidList();

        int n = videoList->size();
        if (n)
        {
            std::list<tPid>::iterator iter = videoList->begin();

            captionDesc.tag = 0x86;
            captionDesc.data = NULL;
            captionDesc.dataLen = 0;
            uint16_t vPid = ((tPid)(*iter)).pid;

            LOG(DLOGL_NOISE, "Recording video pid 0x%x", vPid);

            int ret = pmt->getDescriptor(&captionDesc, vPid);
            //if it has caption descritoir, save it in metadata

            if (!ret && captionDesc.dataLen)
            {
                LOG(DLOGL_NOISE, "Get Caption descriptor, len %d", captionDesc.dataLen);
                mCaptionDescriptorLength = captionDesc.dataLen;
            }
        }
    }
    metaDataSize += mCaptionDescriptorLength;

    LOG(DLOGL_NOISE, "sizeof(tCpeRecDataBase): %d  mMspPidsDataSize: %d  mCaMetaDataSize: %d",
        sizeof(tCpeRecDataBase), mDbPidsSize, mCaMetaDataSize);
    LOG(DLOGL_NOISE, "mCaDescriptorLength: %d  total: %d", mCaDescriptorLength, metaDataSize);

    // allocate size
    metaDataDBPtr = (tCpeRecDataBase *)calloc(1, metaDataSize);
    if (metaDataDBPtr == NULL)
    {
        LOG(DLOGL_ERROR, "error alloc %d bytes", metaDataSize);
        return kMspStatus_Error;
    }


/////////////////////////////////////////////////////////////////////////////////
// now fill in Pids table data. Saving Pids using reference platform's Pids table.
/////////////////////////////////////////////////////////////////////////////////

    metaDataDBPtr->dbEntry[0].tag =  kCpeRec_PIDTableTag;  //0x102 tag.
    metaDataDBPtr->dbEntry[0].size = mDbPidsSize;
    metaDataDBPtr->dbEntry[0].offset = sizeof(tCpeRecDataBase);
    LOG(DLOGL_NOISE, "[0] %x %d %x", metaDataDBPtr->dbEntry[0].tag, metaDataDBPtr->dbEntry[0].size, metaDataDBPtr->dbEntry[0].offset);
    mspStartAddress = reinterpret_cast<uint8_t*>(metaDataDBPtr);
    mspStartAddress += sizeof(tCpeRecDataBase);
    memcpy(mspStartAddress, mDbPids, mDbPidsSize);
    metaDataDBPtr->dbHdr.dbCounts = 1;



///////////////////////////////////////////////////////////////////////
// now fill in data for CA
///////////////////////////////////////////////////////////////////////
    if (mCaMetaDataSize > 0)
    {
// fill in header data for CA
        metaDataDBPtr->dbEntry[1].tag = 0x1FAC;  // TODO: define this somewhere as new CA data
        metaDataDBPtr->dbEntry[1].size = mCaMetaDataSize;
        metaDataDBPtr->dbEntry[1].offset = sizeof(tCpeRecDataBase) + mDbPidsSize;
        dlog(DL_MSP_DVR, DLOGL_NOISE, "RecordSession::%s:%d [1] %x %d %x", __FUNCTION__, __LINE__, metaDataDBPtr->dbEntry[1].tag, metaDataDBPtr->dbEntry[1].size, metaDataDBPtr->dbEntry[1].offset);

// and the actual data for CA
        caStartAddress = reinterpret_cast<uint8_t*>(metaDataDBPtr);
        caStartAddress += sizeof(tCpeRecDataBase) + mDbPidsSize;
        if (mCaMetaDataPtr != NULL)
        {
            memcpy(caStartAddress, mCaMetaDataPtr, mCaMetaDataSize);
        }
        metaDataDBPtr->dbHdr.dbCounts += 1;
    }

// now fill in data for CA descriptor -- temporary hack until we re-factor PSI
// NOTE:  This is not the way it should end up.
// we need to change to write the raw PMT and rebuild the PMT with the new CA descriptor in it
///////////////////////////////////////////////////////////////////////
    if (mCaDescriptorLength)
    {
        // fill in header data for CA descriptor
        metaDataDBPtr->dbEntry[2].tag = 0x1FAD;  // TODO: define this somewhere as new CA descriptor data (temporary)
        metaDataDBPtr->dbEntry[2].size = mCaDescriptorLength;
        metaDataDBPtr->dbEntry[2].offset = sizeof(tCpeRecDataBase) + mDbPidsSize + mCaMetaDataSize;
        dlog(DL_MSP_DVR, DLOGL_NOISE, "RecordSession::%s:%d [2] %x %d %x", __FUNCTION__, __LINE__, metaDataDBPtr->dbEntry[2].tag, metaDataDBPtr->dbEntry[2].size, metaDataDBPtr->dbEntry[2].offset);

        caStartAddress = reinterpret_cast<uint8_t*>(metaDataDBPtr);
        caStartAddress += sizeof(tCpeRecDataBase) + mDbPidsSize + mCaMetaDataSize;
        if (mCaDescriptorPtr != NULL)
        {
            memcpy(caStartAddress, mCaDescriptorPtr, mCaDescriptorLength);
        }

        metaDataDBPtr->dbHdr.dbCounts += 1;
    }
    else
    {
        LOG(DLOGL_NOISE, "Warning: zero mCaDescriptorLength");
    }


    if (mCaDescriptorPtr != NULL)
    {
        LOG(DLOGL_REALLY_NOISY, "ca desc %x %x %x %x", mCaDescriptorPtr[0], mCaDescriptorPtr[1], mCaDescriptorPtr[2], mCaDescriptorPtr[3]);
    }

    LOG(DLOGL_NOISE, "total metadata size %d", metaDataSize);

    if (mCaptionDescriptorLength)
    {
        metaDataDBPtr->dbEntry[3].tag = 0x1FAE;
        metaDataDBPtr->dbEntry[3].size = mCaptionDescriptorLength;
        metaDataDBPtr->dbEntry[3].offset = sizeof(tCpeRecDataBase) + mDbPidsSize + mCaMetaDataSize + mCaDescriptorLength;

        mspStartAddress = reinterpret_cast<uint8_t *>(metaDataDBPtr);
        mspStartAddress += sizeof(tCpeRecDataBase) + mDbPidsSize + mCaMetaDataSize + mCaDescriptorLength;

        dlog(DL_MSP_DVR, DLOGL_NOISE, "RecordSession::%s:%d [3] %x %d %x", __FUNCTION__, __LINE__, metaDataDBPtr->dbEntry[3].tag, metaDataDBPtr->dbEntry[3].size, metaDataDBPtr->dbEntry[3].offset);

        memcpy(mspStartAddress, captionDesc.data, captionDesc.dataLen);

        metaDataDBPtr->dbHdr.dbCounts += 1;

        pmt->releaseDescriptor(&captionDesc);
        LOG(DLOGL_NOISE, "total metadata size %d", metaDataSize);
    }

    //Updating the parameters of Metadatabase Tags
    metaDataDBPtr->dbHdr.version = kCpeRec_DataBaseVersion;
    metaDataDBPtr->dbHdr.size = metaDataSize; //size of total database
    metaDataDBPtr->dbHdr.checksum = calculate_checksum((uint8_t*)metaDataDBPtr, metaDataSize);
    metaDataDBPtr->dbHdr.CCI = mCCiValue;
    LOG(DLOGL_NOISE, "cpe_record_writemetadat writes metadat with the cci value %u", metaDataDBPtr->dbHdr.CCI);
    err = cpe_record_WriteMetaData(mRecHandle, filename.c_str(), metaDataDBPtr, metaDataSize);

    //moved to prevent memory leak
    free(metaDataDBPtr);
    metaDataDBPtr = NULL;
    if (err != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "cpe_record_WriteMetaData error %d  record handle %p", err, mRecHandle);
        return kMspStatus_CpeRecordError;
    }
    else
    {
        LOG(DLOGL_NOISE, "cpe_record_WriteMetaData succeeded");
    }

    return kMspStatus_Ok;
}


eMspStatus  MSPRecordSession::writeAllAnalogMetaData(std::string filename)
{
    FNLOG(DL_MSP_DVR);
    int err = kCpe_NoErr;
    tCpeRecDataBase *metaDataDBPtr = NULL;
    unsigned int metaDataSize;
    // uint8_t *mspStartAddress;

    if (mState != kRecordSessionStarted)
    {
        dlog(DL_MSP_DVR, DLOGL_NOISE, "RecordSession::%s:%d Called in wrong state. %d", __FUNCTION__, __LINE__, mState);
    }
    dlog(DL_MSP_DVR, DLOGL_NOISE, "RecordSession::%s:%d WriteAllMetaData filename %s ", __FUNCTION__, __LINE__, filename.c_str());

    // base size is size of CPERP DB structure
    metaDataSize = sizeof(tCpeRecDataBase);
    metaDataSize += mDbPidsSize;
    metaDataSize += mCaMetaDataSize;
    metaDataSize += mCaDescriptorLength;

    dlog(DL_MSP_DVR, DLOGL_NOISE, "RecordSession::%s:%d msp %d dbpids %d cadesc %d ", __FUNCTION__, __LINE__, mDbPidsSize, mCaMetaDataSize, mCaDescriptorLength);
    dlog(DL_MSP_DVR, DLOGL_NOISE, "RecordSession::%s:%d total %d ", __FUNCTION__, __LINE__, metaDataSize);

    // allocate size
    metaDataDBPtr = (tCpeRecDataBase *)malloc(metaDataSize);
    if (metaDataDBPtr == NULL)
    {
        return kMspStatus_Error;
    }

// fill in header info for MSP
    //  metaDataDBPtr->dbEntry[0].tag = 0x1FAB;  // TODO:  define this somewhere as new MSP data
    //  metaDataDBPtr->dbEntry[0].size = mspMetaDataSize;
    //metaDataDBPtr->dbEntry[0].offset = sizeof(tCpeRecDataBase);
    //dlog(DL_MSP_DVR,DLOGL_NOISE,"RecordSession::%s:%d [0] %x %d %x",__FUNCTION__,__LINE__,metaDataDBPtr->dbEntry[0].tag,metaDataDBPtr->dbEntry[0].size,metaDataDBPtr->dbEntry[0].offset);

///////////////////////////////////////////////////////////////////////
// now fill in data for MSP
///////////////////////////////////////////////////////////////////////
    //  mspStartAddress = reinterpret_cast<uint8_t*>(metaDataDBPtr);
    // mspStartAddress += sizeof(tCpeRecDataBase);


    metaDataDBPtr->dbHdr.dbCounts = 0;



//Updating the parameters of Metadatabase Tags
    metaDataDBPtr->dbHdr.version = kCpeRec_DataBaseVersion;
    metaDataDBPtr->dbHdr.size = metaDataSize; //size of total database
    metaDataDBPtr->dbHdr.checksum = calculate_checksum((uint8_t*)metaDataDBPtr, metaDataSize);
    metaDataDBPtr->dbHdr.CCI = 0;           // TODO:  does CAM even use this


    err = cpe_record_WriteMetaData(mRecHandle, filename.c_str(), metaDataDBPtr, metaDataSize);

    //moved to prevent memory leak
    free(metaDataDBPtr);
    metaDataDBPtr = NULL;


    if (err != kCpe_NoErr)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_WriteMetaData(...) return error %d, with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
        return kMspStatus_CpeRecordError;
    }

    return kMspStatus_Ok;
}


/** *********************************************************
*
*returns
*
*/
eMspStatus  MSPRecordSession::saveCAMetaData()
{
    FNLOG(DL_MSP_DVR);
    uint8_t *caMetaDataOriginal, *caDescOriginal;

    mRecordSession->getDvrMetadata(&caMetaDataOriginal, mCaMetaDataSize, &caDescOriginal, mCaDescriptorLength);
    LOG(DLOGL_REALLY_NOISY, "size: %d caDescLen: %d  caDesc: %p", mCaMetaDataSize, mCaDescriptorLength, caDescOriginal);

// save off both the CA metadata and the update CA descriptor that goes into our PMT
    if ((mCaMetaDataSize > 0) && (caMetaDataOriginal != NULL))
    {
        mCaMetaDataPtr = (uint8_t*)malloc(mCaMetaDataSize);
        if (mCaMetaDataPtr != NULL)
        {
            memcpy(mCaMetaDataPtr, caMetaDataOriginal, mCaMetaDataSize);
        }
    }

    if ((mCaDescriptorLength > 0) && (caDescOriginal != NULL))
    {
        mCaDescriptorPtr = (uint8_t*)malloc(mCaDescriptorLength);
        if (mCaDescriptorPtr != NULL)
        {
            memcpy(mCaDescriptorPtr, caDescOriginal, mCaDescriptorLength);
        }
    }


// check to make sure we already have MSP metadata, if so signal the metadata thread to do the write
    if (mDbPidsSize > 0)
    {
        writeAllMetaData(mtsb_filename);
    }

// In G6 RTN CAK destroySession frees the memory
// In G8 MW frees the memory
#if PLATFORM_NAME == G8
    if (caDescOriginal != NULL)
    {
        LOG(DLOGL_REALLY_NOISY, "ca desc %x %x %x %x", caDescOriginal[0], caDescOriginal[1], caDescOriginal[2], caDescOriginal[3]);
        free(caDescOriginal);
        caDescOriginal = NULL;
    }
#endif

    return kMspStatus_Ok;
}

tPid MSPRecordSession::getAudioPidStruc(Pmt *pmt, uint16_t pid, bool & bFound)
{
    //Pmt *pmt = mPsiptr->getPmtObj();
    std::list<tPid> *audioList = pmt->getAudioPidList();
    std::list<tPid>::iterator iter;
    bFound = false;
    tPid audioPid = {0, 0};
    for (iter = audioList->begin(); iter != audioList->end(); iter++)
    {
        audioPid = (*iter);
        if (audioPid.pid == pid)
        {
            bFound = true;
            break;
        }
    }
    return audioPid;
}

eMspStatus MSPRecordSession::savePidsMetaData()
{
    FNLOG(DL_MSP_DVR);


    eMspStatus status;
    tCpePgrmHandleMpegDesc langDescr;
    unsigned int i = 0, startPid = 0;
    Pmt *pmt = NULL;
    langDescr.tag = ISO_639_LANG_DESCR_TAG;

    if (!mbIsAnalog)
    {
        LOG(DLOGL_NOISE, "Digital Source");
        ///First Metadata- PID TABLE storage
        pmt = mPsiptr->getPmtObj();
        if (pmt == NULL)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "PMT structure is NULL,Not able to store metadata");
            return kMspStatus_Error;
        }
    }
    // convert from source PID structure to metadata PID structure
    mDbPidsSize = sizeof(tCpeRecDataBasePidTable);
    if (mPids->numOfPids > 1 && !mbIsAnalog)
    {
        mDbPidsSize += (mPids->numOfPids + 1) * sizeof(tCpeRecDataBasePid);   //Need to store one PAT and PMT Pids. tCpeRecDataBasePidTable structure has one extra,hence adding one more here
        // for storing extra two (one PAT and one PMT Pids)
    }
    else if (mbIsAnalog)
    {
        mDbPidsSize += (mPids->numOfPids) * sizeof(tCpeRecDataBasePid);
    }
    mDbPids = (tCpeRecDataBasePidTable *)calloc(1, mDbPidsSize);

    if (!mbIsAnalog)
    {
        mDbPids->numPID = mPids->numOfPids + 2;   //Including extra PAT and PMT Pids.
    }
    else
        mDbPids->numPID = mPids->numOfPids;

    mDbPids->useAC3 = mUseAC3;

    LOG(DLOGL_NOISE, "Total number of Pids to be stored = %d, useAC3 = 0x%x", mDbPids->numPID, mUseAC3);

    if (!mbIsAnalog)
    {
        //Adding PAT Pid. So that the reference platform can identify it and not trying to recreate and inject PAT/PMT for MRDVR playback serving.
        mDbPids->pids[0].type = eCpeRecDataBasePidType_Data;
        mDbPids->pids[0].pid = 0x0;

        //Adding PMT Pid.
        mDbPids->pids[1].type = eCpeRecDataBasePidType_PMT;
        mDbPids->pids[1].pid = mPmtPid;
        startPid = 2;
    }

    //parsing through all audio,video and clock pids available for storage
    for (i = startPid; i < (mPids->numOfPids + startPid); i++)
    {
        //copying the PID.
        LOG(DLOGL_NOISE, "i=%d, Copying the Pid %d to Pids database, type 0x%x", i, mPids->pids[i - startPid].pid, mPids->pids[i - startPid].type);
        mDbPids->pids[i].pid = mPids->pids[i - startPid].pid;  //copying from mPids structure to mDbPids structure.

        langDescr.dataLen = 0;
        langDescr.data = NULL;
        if (!mbIsAnalog)
        {
            status = pmt->getDescriptor(&langDescr, mDbPids->pids[i].pid);
            if (status == kMspStatus_Ok && langDescr.dataLen >= LANG_CODE_SIZE)
            {
                LOG(DLOGL_NOISE, " Pid %d has language code %c%c%c", mDbPids->pids[i].pid, langDescr.data[0], langDescr.data[1], langDescr.data[2]);
                mDbPids->pids[i].langCode = 0; //RP-Team decided that this is a uint32_t instead of char array.  We want the last byte to be null as we use it as a string.
                memcpy(&mDbPids->pids[i].langCode, langDescr.data, LANG_CODE_SIZE);
                pmt->releaseDescriptor(&langDescr);
            }
            else
            {
                LOG(DLOGL_NOISE, " Pid %d has no language code and its language descriptor length is %d", mDbPids->pids[i].pid, langDescr.dataLen);
            }
        }
        bool bFound = false;
        tPid pidStruc;
        switch (mPids->pids[i - startPid].type)
        {
        case eCpeSrcPFPidType_VidMpeg2Pid:
        case eCpeSrcPFPidType_VidMPEG1Pid:
            mDbPids->pids[i].type = eCpeRecDataBasePidType_Video;
            break;
        case eCpeSrcPFPidType_VidH264Pid:
            mDbPids->pids[i].type = eCpeRecDataBasePidType_VideoH264;
            break;
        case eCpeSrcPFPidType_VidVC1Pid:
            mDbPids->pids[i].type = eCpeRecDataBasePidType_VideoVC1;
            break;
        case eCpeSrcPFPidType_AudMpegPid:
            mDbPids->pids[i].type = eCpeRecDataBasePidType_Audio;
            break;
        case eCpeSrcPFPidType_AudAACPid:
            pidStruc = getAudioPidStruc(pmt, mPids->pids[i - startPid].pid, bFound);
            dlog(DL_MSP_DVR, DLOGL_NOISE, "%s, %d, found Pid %d, original type 0x%x", __FUNCTION__, __LINE__, bFound, pidStruc.streamType);
            if (bFound && pidStruc.streamType == kCpeStreamType_AACplus_Audio)
            {
                mDbPids->pids[i].type = eCpeRecDataBasePidType_AudioAACplus;
            }
            else
                mDbPids->pids[i].type = eCpeRecDataBasePidType_AudioAAC;
            break;

        case eCpeSrcPFPidType_AudMP3Pid:
            mDbPids->pids[i].type = eCpeRecDataBasePidType_Audio;
            break;
        case eCpeSrcPFPidType_AudAC3Pid:
            pidStruc = getAudioPidStruc(pmt, mPids->pids[i - startPid].pid, bFound);
            dlog(DL_MSP_DVR, DLOGL_NOISE, "%s, %d, found Pid %d, original type 0x%x", __FUNCTION__, __LINE__, bFound, pidStruc.streamType);
            if (bFound && pidStruc.streamType == kCpeStreamType_DDPlus_Audio)
            {
                mDbPids->pids[i].type = eCpeRecDataBasePidType_AudioDDplus;
            }
            else
                mDbPids->pids[i].type = eCpeRecDataBasePidType_Audio;
            // mDbPids->useAC3 = (1 << j) | (mDbPids->useAC3);

            break;
        case eCpeSrcPFPidType_AudLPCMPid:
            mDbPids->pids[i].type = eCpeRecDataBasePidType_AudioLPCM;
            break;

        case eCpeSrcPFPidType_OtherPid:
        default:
            if (mPids->pids[i - startPid].hasPCR)
            {
                mDbPids->pids[i].type = eCpeRecDataBasePidType_Pcr;
            }
            else
            {
                mDbPids->pids[i].type = eCpeRecDataBasePidType_Data;
            }
            break;
        }


    }

    return kMspStatus_Ok;

}


int16_t MSPRecordSession::calculate_checksum(uint8_t *databuf, size_t size)
{
    FNLOG(DL_MSP_DVR);
    uint8_t checksum = 0;
    uint32_t offset;

    // adding up all the bytes
    for (offset = 0; offset < size; offset++)
        checksum += databuf[offset];

    // compute checksum
    checksum = REC_SESSION_CHECKSUM_WORD - checksum;
    // return checksum
    return checksum;
}

/** *********************************************************
*/
eMspStatus MSPRecordSession::startTSB()
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    int err;


    switch (mState)
    {
    case kRecordSessionConfigured:
    {

        err = cpe_record_Start(mRecHandle);
        if (err != kCpe_NoErr)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_Start(...) return error %d, with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
            syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                   "DLOG|MSP|Recording Failure|%s:err cpe_record_start err %d",
                   __FUNCTION__, err);
            status = kMspStatus_CpeRecordError;
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_NOISE, "RecordSession::%s:%d Call cpe_record_Start(...) success!!!", __FUNCTION__, __LINE__);
            mState = kRecordSessionStarted;
        }
    }
    break;

    default:
        dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Wrong state, current state %d ", __FUNCTION__, __LINE__, mState);
        syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
               "DLOG|MSP|Recording Failure|%s: wrong state, curr state %d",
               __FUNCTION__, mState);
        status = kMspStatus_StateError;
        break;
    }
    return  status;
}


eMspStatus MSPRecordSession::startTSB(AnalogPsi *psi)
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    int err = kCpe_NoErr;
    UNUSED_PARAM(psi);

    switch (mState)
    {
    case kRecordSessionConfigured:
    {
        err = writeAllAnalogMetaData(mtsb_filename);
        if (err != kMspStatus_Ok)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d saveMetaData returned error %d, with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
            status = kMspStatus_CpeRecordError;
        }
        err = cpe_record_Start(mRecHandle);
        if (err != kCpe_NoErr)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_Start(...) return error %d, with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
            status = kMspStatus_CpeRecordError;
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_NOISE, "RecordSession::%s:%d Call cpe_record_Start(...) success!!!", __FUNCTION__, __LINE__);
            mState = kRecordSessionStarted;
        }
    }
    break;

    default:
        dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Wrong state, current state %d ", __FUNCTION__, __LINE__, mState);
        status = kMspStatus_StateError;
        break;
    }
    return  status;
}



/** *********************************************************
*/
eMspStatus MSPRecordSession::stopTSB(void)
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    int err;
    LOG(DLOGL_NOISE, "state: %d ", mState);

    switch (mState)
    {
    case kRecordSessionConversionStarted:
        dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Wrong state, current state %d ", __FUNCTION__, __LINE__, mState);
        dlog(DL_MSP_DVR, DLOGL_ERROR, "Attempting to stop TSB,while persistent recording going on  ");
        //break;

    case kRecordSessionStarted:

        err = cpe_record_CancelInjectData(mRecHandle, mInjectPat);
        if (err != kCpe_NoErr)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_CancelInjectData(...)  for PAT return error %d, with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
            syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                   "DLOG|MSP|Recording Failure|%s:err cpe_record_CancelInjectData err %d",
                   __FUNCTION__, err);
        }

        err = cpe_record_CancelInjectData(mRecHandle, mInjectPmt);
        if (err != kCpe_NoErr)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_CancelInjectData(...)  for PMT return error %d, with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
            syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                   "DLOG|MSP|Recording Failure|%s:err cpe_record_CancelInjectData err %d",
                   __FUNCTION__, err);
        }


        err = cpe_record_Stop(mRecHandle);
        if (err != kCpe_NoErr)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_Stop(...) return error %d, with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
            syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                   "DLOG|MSP|Recording Failure|%s:err cpe_record_stop err %d",
                   __FUNCTION__, err);
        }
        mState = kRecordSessionStopped;

        break;

    default:
        LOG(DLOGL_ERROR, "Error: Bad state: %d ", mState);
        syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
               "DLOG|MSP|Recording Failure|%s:err bad state %d",
               __FUNCTION__, mState);
    }

    return  status;
}

/** *********************************************************
    \returns
*/
eMspStatus MSPRecordSession::closeTSB(void)
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;

    switch (mState)
    {
    case kRecordSessionStopped:
    case kRecordSessionOpened:
    case kRecordSessionConfigured:
    {
        int err = cpe_record_Close(mRecHandle);
        if (err != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "Error cpe_record_Close %d mRecHandle %p ", err, mRecHandle);
            syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                   "DLOG|MSP|Recording Failure|%s: cpe_record_Close err %d",
                   __FUNCTION__, err);
            status = kMspStatus_CpeRecordError;
        }
        else
        {
            mRecHandle = 0;
            mState = kRecordSessionClosed;
        }
    }
    break;

    default:
        LOG(DLOGL_ERROR, "Error: Bad state: %d ", mState);
        syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
               "DLOG|MSP|Recording Failure|%s:err bad state %d",
               __FUNCTION__, mState);
        //We will return  kMspStatus_Ok since it is being stopped but complain with the log
    }
    return  status;
}




/** *********************************************************
*/
/*eMspStatus MSPRecordSession::startConvertTSB(std::string recfilename,float nptRecordStartTime, float nptRecordStopTime)
{
    FNLOG(DL_MSP_DVR);

    LOG(DLOGL_NORMAL, "file: %s  nptStart: %f  nptStop: %f", recfilename.c_str(), nptRecordStartTime, nptRecordStopTime);

    eMspStatus status = kMspStatus_Ok;
    int err;
    unsigned int timeout_ms=0;

    switch (mState)
    {
        case kRecordSessionStarted:
            {
                if (mIsCAMetaWritten==false)
                {
                    // Wait for CS metadata --- this handles the condition when immediately on channel change recording is started
                    while (!mIsCAMetaWritten && (timeout_ms<50000))
                    {
                        usleep(10000);
                        timeout_ms+=100;
                    }
                    dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "CA metadata available = %d\n", mIsCAMetaWritten);
                }

                if (mIsCAMetaWritten)
                {
                    err = cpe_record_TSBConversionStart(mRecHandle,recfilename.c_str(),(uint32_t)nptRecordStartTime,(uint32_t)nptRecordStopTime);
                    if (err != kCpe_NoErr)
                    {
                        dlog(DL_MSP_DVR,DLOGL_ERROR,"RecordSession::%s:%d Call cpe_record_TSBConversionStart(...) return error %d with record handle %d ",__FUNCTION__,__LINE__,err, (int) mRecHandle);
                        status = kMspStatus_CpeRecordError;
                        return status;
                    }
                    else
                    {
                        dlog(DL_MSP_DVR,DLOGL_NORMAL,"RecordSession::%s:%d Call cpe_record_TSBConversionStart(...) success!!!",__FUNCTION__,__LINE__);
                        mState=kRecordSessionConversionStarted;
                    }
                }
                else
                {
                    dlog(DL_MSP_DVR,DLOGL_ERROR,"RecordSession::%s:%d CA Metadata storage failed ",__FUNCTION__,__LINE__);
                    status = kMspStatus_CpeRecordError;
                    return status;
                }

                err = writeAllMetaData(recfilename);
                if (err != kCpe_NoErr)
                {
                    dlog(DL_MSP_DVR,DLOGL_ERROR,"RecordSession::%s:%d writeAllMetaData failed. error %d with record handle %d ",__FUNCTION__,__LINE__,err, (int) mRecHandle);
                }

            }
            break;

        default:
            dlog(DL_MSP_DVR,DLOGL_ERROR,"RecordSession::%s:%d Wrong state, current state %d ",__FUNCTION__,__LINE__, mState);
            status = kMspStatus_StateError;
            break;
    }
    return  status;
}*/




/** *********************************************************
*/
eMspStatus MSPRecordSession::startConvertAnalogTSB(std::string recfilename, uint32_t nptRecordStartTime, uint32_t nptRecordStopTime)
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    int err;
    switch (mState)
    {
    case kRecordSessionStarted:
    {
        err = cpe_record_TSBConversionStart(mRecHandle, recfilename.c_str(), (uint32_t)nptRecordStartTime, (uint32_t)nptRecordStopTime);
        if (err != kCpe_NoErr)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_TSBConversionStart(...) return error %d with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
            status = kMspStatus_CpeRecordError;
            return status;
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_NORMAL, "RecordSession::%s:%d Call cpe_record_TSBConversionStart(...) success!!!", __FUNCTION__, __LINE__);
            mState = kRecordSessionConversionStarted;
        }

        err = writeAllAnalogMetaData(recfilename);
        if (err != kCpe_NoErr)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d writeAllAnalogMetaData failed. error %d with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
        }

    }
    break;

    default:
        dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Wrong state, current state %d ", __FUNCTION__, __LINE__, mState);
        status = kMspStatus_StateError;
        break;
    }
    return  status;
}



/** *********************************************************
*/
eMspStatus MSPRecordSession::registerRecordCallback(RecordCallbackFunction callBc, void *clientData)
{
    eMspStatus status = kMspStatus_Ok;

    if (callBc)
    {
        mCb = callBc;
        mRecvdData = clientData;
    }
    else
    {
        LOG(DLOGL_ERROR, "Error null callBc");
        status = kMspStatus_BadParameters;
    }

    return  status;
}


void MSPRecordSession::unregisterRecordCallback(void)
{
    mCb = NULL;
}



void MSPRecordSession::performCb(tCpeRecCallbackTypes type)
{
    FNLOG(DL_MSP_DVR);
    dlog(DL_MSP_DVR, DLOGL_NOISE, "RecordSession::%s:%d, type signal %d", __FUNCTION__, __LINE__, type);

    if (mCb)
    {
        mCb(type, mRecvdData);
    }

}


eMspStatus MSPRecordSession::register_callbacks()
{
    FNLOG(DL_MSP_DVR);
    int err;
    eMspStatus status = kMspStatus_Ok;

    err = cpe_record_RegisterCallback(eCpeRecCallbackTypes_RecordingStarted, (void *)this, (tCpeRecCallbackFunction)RecCallbackFun, &mRecCbRecStartId, mRecHandle, NULL);
    if (err != kCpe_NoErr)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_RegisterCallback(...) return error %d, with record handle %d ", __FUNCTION__, __LINE__, err, (int)mRecHandle);
        status = kMspStatus_CpeRecordError;
    }

    err = cpe_record_RegisterCallback(eCpeRecCallbackTypes_DiskFull, (void *)this, (tCpeRecCallbackFunction)RecCallbackFun, &mRecCbDiskFullId, mRecHandle, NULL);
    if (err != kCpe_NoErr)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_RegisterCallback(...) return error %d, with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
        status = kMspStatus_CpeRecordError;
    }

    err = cpe_record_RegisterCallback(eCpeRecCallbackTypes_ConversionComplete, (void *)this, (tCpeRecCallbackFunction)RecCallbackFun, &mRecCbConvComplete, mRecHandle, NULL);
    if (err != kCpe_NoErr)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_RegisterCallback(...) return error %d, with record handle %d", __FUNCTION__, __LINE__, err, (int) mRecHandle);
        status = kMspStatus_CpeRecordError;
    }

    return status;
}


eMspStatus MSPRecordSession::unregister_callbacks()
{
    FNLOG(DL_MSP_DVR);
    int err;
    eMspStatus status = kMspStatus_Ok;

    if (mRecCbRecStartId != NULL)
    {
        err = cpe_record_UnregisterCallback(mRecHandle, mRecCbRecStartId);
        if (err != kCpe_NoErr)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_UnRegisterCallback(...) return error %d, with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
            status = kMspStatus_CpeRecordError;
        }
        mRecCbRecStartId = NULL;
    }

    if (mRecCbDiskFullId != NULL)
    {
        err = cpe_record_UnregisterCallback(mRecHandle, mRecCbDiskFullId);
        if (err != kCpe_NoErr)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_UnRegisterCallback(...) return error %d, with record handle %d", __FUNCTION__, __LINE__, err, (int) mRecHandle);
            status = kMspStatus_CpeRecordError;
        }
        mRecCbDiskFullId = NULL;
    }

    if (mRecCbConvComplete != NULL)
    {
        err = cpe_record_UnregisterCallback(mRecHandle, mRecCbConvComplete);
        if (err != kCpe_NoErr)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d Call cpe_record_UnRegisterCallback(...) return error %d, with record handle %d ", __FUNCTION__, __LINE__, err, (int) mRecHandle);
            status = kMspStatus_CpeRecordError;
        }
        mRecCbConvComplete = NULL;
    }

    return status;
}



/** *********************************************************
*/
void MSPRecordSession::RecCallbackFun(tCpeRecCallbackTypes type, void *userdata, void *tsbspecific)
{
    FNLOG(DL_MSP_DVR);
    UNUSED_PARAM(tsbspecific);

    dlog(DL_MSP_DVR, DLOGL_NOISE, "%s:%d Callback...signal type is %d", __FUNCTION__, __LINE__, type);

    MSPRecordSession* p = (MSPRecordSession*) userdata;
    if (p)
    {
        p->performCb(type);
    }


}

/** *********************************************************
*/
void *MSPRecordSession::sfCallback(tCpeSFltCallbackTypes type, void* pCallbackSpecific)
{
    UNUSED_PARAM(pCallbackSpecific);
    ICaFilter *filterFunc;

    if (type == eCpeSFltCallbackTypes_SectionData)
    {
        tCpeSFltBuffer *pSfltBuff = (tCpeSFltBuffer *)pCallbackSpecific;
        if (mRecordSession != NULL)
        {
            filterFunc = mRecordSession->getCaFilter();
            if (filterFunc != NULL)
            {
                filterFunc->doFilter((char *)pSfltBuff->pBuffer, pSfltBuff->length);
            }
        }
    }
    else
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s: BAD CA section filter callback. Type: %d", __FUNCTION__, type);
    }

    return NULL;
}

/** *********************************************************
*/
void *MSPRecordSession::secFltCallbackFunction(tCpeSFltCallbackTypes type, void* userdata, void* pCallbackSpecific)
{
    MSPRecordSession *rs = (MSPRecordSession *)userdata;

    if (rs != NULL)
    {
        return rs->sfCallback(type, pCallbackSpecific);
    }
    else
    {
        return NULL;
    }
}

/** *********************************************************
*/
eMspStatus MSPRecordSession::startCaFilter(uint16_t pid, const tCpeSrcHandle srcHandle)
{
    int status;
    FNLOG(DL_MSP_DVR);

    status = cpe_sflt_Open(srcHandle, kCpeSFlt_HighBandwidth, &mSfHandle);
    if (status != kCpe_NoErr)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "MSPRecordSession::%s: Failed to open CA section filter. Err: %d", __FUNCTION__, status);
        return kMspStatus_Error;
    }

    dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s: Open CA section filter. Handle %p", __FUNCTION__, mSfHandle);

    status = cpe_sflt_Set(mSfHandle, eCpeSFltNames_PID, (void *)&pid);
    if (status != kCpe_NoErr)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s: Failed to set CA section filter. Err: %d", __FUNCTION__, status);
        return kMspStatus_Error;
    }

    // Register for Callbacks
    cpe_sflt_RegisterCallback(eCpeSFltCallbackTypes_Error, (void *)this, (tCpeSFltCallbackFunction)secFltCallbackFunction, &mSfCbIdError, mSfHandle);
    cpe_sflt_RegisterCallback(eCpeSFltCallbackTypes_SectionData, (void *)this, (tCpeSFltCallbackFunction)secFltCallbackFunction, &mFCbIdSectionData, mSfHandle);
    cpe_sflt_RegisterCallback(eCpeSFltCallbackTypes_Timeout, (void *)this, (tCpeSFltCallbackFunction)secFltCallbackFunction, &mSfCbIdSectionData, mSfHandle);

    status = cpe_sflt_Start(mSfHandle, 0);
    if (status != kCpe_NoErr)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s: Failed to start CA section filter. Err: %d", __FUNCTION__, status);
        return kMspStatus_Error;
    }

    dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s: start CA section filter. handle %p", __FUNCTION__, mSfHandle);
    return kMspStatus_Ok;
}

/** *********************************************************
*/
eMspStatus MSPRecordSession::stopCaFilter(void)
{
    int status;
    FNLOG(DL_MSP_DVR);

    dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s: stop CA section filter. handle %p", __FUNCTION__, mSfHandle);
    if (mCaPid == 0)
    {
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "CA Pid is 0 means section filter not started\n");
        return kMspStatus_Ok;
    }
    if (mSfHandle == NULL)
    {
        return kMspStatus_StateError;
    }

    status = cpe_sflt_Stop(mSfHandle);
    if (status != kCpe_NoErr)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s: Failed to stop CA section filter. Err: %d", __FUNCTION__, status);
    }

    if (mSfCbIdError)
    {
        cpe_sflt_UnregisterCallback(mSfHandle, mSfCbIdError);
    }
    if (mFCbIdSectionData)
    {
        cpe_sflt_UnregisterCallback(mSfHandle, mFCbIdSectionData);
    }
    if (mSfCbIdSectionData)
    {
        cpe_sflt_UnregisterCallback(mSfHandle, mSfCbIdSectionData);
    }

    status = cpe_sflt_Close(mSfHandle);
    if (status != kCpe_NoErr)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s: Failed to close CA section filter. Err: %d", __FUNCTION__, status);
        return kMspStatus_Error;
    }

    mSfHandle = NULL;
    return kMspStatus_Ok;
}


eMspStatus MSPRecordSession::InjectPmtData()
{
    int err;
    eMspStatus status;

    mPsiptr->lockMutex();

    Pmt *pmt = mPsiptr->getPmtObj();
    if (pmt == NULL)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "PMT structure is NULL,Not able to get raw PAT/PMT data");
        mPsiptr->unlockMutex();
        return kMspStatus_Error;
    }

    status = mPsiptr->getRawPmt(&mRawPmtPtr, &mRawPmtSize, &mPmtPid); //TODO status check
    if (status == kMspStatus_Ok)
    {
        LOG(DLOGL_NOISE, " Fetching raw PMT for recording injection success");
    }
    else
    {
        LOG(DLOGL_ERROR, "Warning: Fetching raw PMT for recording injection failed");
    }

    status = mPsiptr->getRawPat(&mRawPatPtr); //TODO status check
    if (status == kMspStatus_Ok)
    {
        LOG(DLOGL_NOISE, " Fetching SPTS PAT success");
    }
    else
    {
        LOG(DLOGL_ERROR, "Warning: Fetching SPTS PAT failed");
    }

    mPsiptr->unlockMutex();

    dlog(DL_MSP_DVR, DLOGL_NOISE, "Going to inject the data of size %d and pointer %p\n", kSPTS_PATSizeWithCRC, mRawPatPtr);

    err = cpe_record_InjectData(mRecHandle, 0, mRawPatPtr, kSPTS_PATSizeWithCRC, eCpeRec_InjPacketPsi, 50, &mInjectPat);
    if (err != kCpe_NoErr)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s: PAT inject failed: %d\n", __FUNCTION__, err);
        syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
               "DLOG|MSP|Recording Failure|%s: cpe_record_InjectData err %d for PAT",
               __FUNCTION__, err);

        return kMspStatus_Error;
    }
    else
    {
        dlog(DL_MSP_DVR, DLOGL_NOISE, "Injection of PAT data into the Record stream succeeded!!!!!!!!!!\n");
    }


    dlog(DL_MSP_DVR, DLOGL_NOISE, "Going to inject the data of size %d and pointer %p and PMT Pid %d\n", mRawPmtSize, mRawPmtPtr, mPmtPid);
    err = cpe_record_InjectData(mRecHandle, mPmtPid, mRawPmtPtr, mRawPmtSize, eCpeRec_InjPacketPsi, 50, &mInjectPmt);
    if (err != kCpe_NoErr)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s: PMT inject failed: %d\n", __FUNCTION__, err);
        syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
               "DLOG|MSP|Recording Failure|%s: cpe_record_InjectData err %d for PMT",
               __FUNCTION__, err);
        return kMspStatus_Error;
    }
    else
    {
        dlog(DL_MSP_DVR, DLOGL_NOISE, "Injection of PMT data into the Record stream succeeded!!!!!!!!!!\n");
    }

    return kMspStatus_Ok;
}




/** *********************************************************
*/
MSPRecordSession::~MSPRecordSession()
{
    FNLOG(DL_MSP_DVR);

    if (mCaMetaDataPtr != NULL)
    {
        free(mCaMetaDataPtr);
        mCaMetaDataPtr = NULL;
    }

    if (mCaDescriptorPtr != NULL)
    {
        free(mCaDescriptorPtr);
        mCaDescriptorPtr = NULL;
    }

    if (mRecordSession != NULL)
    {
        mRecordSession->destroySession();
        mRecordSession = NULL;
    }

    if (mRawPmtPtr)
    {
        free(mRawPmtPtr);
        mRawPmtPtr = NULL;
    }

    if (mRawPatPtr)
    {
        free(mRawPatPtr);
        mRawPatPtr = NULL;
    }

    if (mDbPids != NULL)
    {
        free(mDbPids);
        mDbPids = NULL;
        mDbPidsSize = 0;
    }
    if (mPids != NULL)
    {
        free(mPids);
        mPids = NULL;
    }
}

MSPRecordSession::MSPRecordSession()
{
    FNLOG(DL_MSP_DVR);
    mState = kRecordSessionIdle;
    mCb = NULL;
    mRecHandle = 0;
    mRecCbRecStartId = NULL;
    mRecCbDiskFullId = NULL;
    mRecCbConvComplete = NULL;
    mRecordSession = NULL;
    mCaMetaDataPtr = NULL;
    mCaDescriptorPtr = NULL;
    mCaMetaDataSize = 0;
    mCaDescriptorLength = 0;
    mCaptionDescriptorLength = 0;
    mCaSystem = 0;
    mCaPid = 0;
    mSfHandle = NULL;
    mSourceHandle = NULL;
    mIsCAMetaWritten = false;
    mPsiptr = NULL;
    mPgmNo = -1;
    memset(&mPmtInfo, 0, sizeof(mPmtInfo));
    mtsb_filename[0] = '\0';
    mScramblingMode = 0;
    mFCbIdSectionData = 0;
    mSfCbIdSectionData = 0;
    mRecvdData = NULL;
    mSfCbIdError = 0;
    mPids = NULL;
    mRawPmtPtr = NULL;
    mRawPmtSize = 0;
    mRawPatPtr = NULL;
    mDbPids = NULL;
    mDbPidsSize = 0;
    mbIsAnalog = false;
    mUseAC3 = 0;
    mInjectPat = NULL;
    mInjectPmt = NULL;
    mPmtPid = 0;
    mbIsAnalog = false;
    mEntRegId = 0;
    mPtrCBData = NULL;
    mCCICBFn = NULL;
    mCCiValue = 0;
}


/**
 * @brief       MSP CCI handler function is registered with CAM and record session
 *                      to pass CCI updates recieved for a stream
 * @param[in] data, the Session associated with the stream
 * @param[in] cb, Callback Function to call on recieving CCI updates
 * @return None
 *
 */


void MSPRecordSession::SetCCICallback(void *data, CCIcallback_t cb)
{
    FNLOG(DL_MSP_DVR);
    mPtrCBData = data;
    mCCICBFn = cb;
}

/**
 * @brief       Unregister for CCI updates
 *
 * @return None
 *
 */

void MSPRecordSession::UnSetCCICallback()
{
    FNLOG(DL_MSP_DVR);
    mPtrCBData = NULL;
    mCCICBFn = NULL;
}

void MSPRecordSession::updateCCI(uint8_t CCIupdate)
{
    dlog(DL_MSP_DVR, DLOGL_ERROR, "MSPRecordSession is updated with the CCI value -->%u in %s ", CCIupdate, __func__);
    mCCiValue = CCIupdate;
    writeAllMetaData(mtsb_filename);
}

#if PLATFORM_NAME == G8
eMspStatus MSPRecordSession::tsbPauseResume(bool isPause)
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    int cpeErr = kCpe_NoErr;
    if (0 != mRecHandle)
    {
        if (true == isPause)
        {
            cpeErr = cpe_record_Pause(mRecHandle);
        }
        else
        {
            cpeErr = cpe_record_Resume(mRecHandle);
        }
        if (kCpe_NoErr != cpeErr)
        {
            LOG(DLOGL_ERROR, "%s:%d %s called with rechandle - %d returned error %d", __FUNCTION__, __LINE__,
                ((true == isPause) ? "cpe_record_Pause" : "cpe_record_Resume"), (int) mRecHandle, cpeErr);
            status = kMspStatus_CpeRecordError;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "%s:%d Called with invalid rec handle", __FUNCTION__, __LINE__);
        status = kMspStatus_Error;
    }
    return status;

}
#endif
