/**
   \file MrdvrTsbStreamer.cpp
   \class MrdvrTsbStreamer

    Implementation file for MrdvrTsbStreamer controller class
*/


///////////////////////////////////////////////////////////////////////////
//                    Standard Includes
///////////////////////////////////////////////////////////////////////////
#include <list>
#include <assert.h>
#include <syslog.h>
#if defined(DMALLOC)
#include "dmalloc.h"
#endif

///////////////////////////////////////////////////////////////////////////
//                    CPE Includes
///////////////////////////////////////////////////////////////////////////
#include <cpe_error.h>
#include <cpe_source.h>
#include <directfb.h>
#include <glib.h>
#include "sys/xattr.h"
#include "cpe_cam.h"
//////////////////////////////////////////////////////////////////////////
//                    SAIL Includes
///////////////////////////////////////////////////////////////////////////
#include <sail-clm-api.h>
#include <dlog.h>
#include <Cam.h>

#include<sys/time.h>
///////////////////////////////////////////////////////////////////////////
//                    Local Includes
///////////////////////////////////////////////////////////////////////////
#include "MrdvrTsbStreamer.h"
#include "DisplaySession.h"
#include "RecordSession.h"
#include "psi.h"
#include "AnalogPsi.h"
#include "eventQueue.h"
#include "IMediaPlayer.h"
#include "MSPSourceFactory.h"
#include "MSPPPVSource.h"
#include "MSPMrdvrStreamerSource.h"

#include "pthread_named.h"

#include "csci-dvr-scheduler-api.h"
#include "mrdvrserver.h"

#include "MSPScopedPerfCheck.h"
#include "TsbHandler.h"

#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_MRDVR, level,"mrdvr:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

#define TIMEOUT 5
#define PSI_TIMEOUT 3

//interface for getting TSB file name for platform.cpp
extern int get_tsb_file(char *tsb_file, unsigned int session_number);

bool MrdvrTsbStreamer::mEasAudioActive = false;

///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

#if PLATFORM_NAME == IP_CLIENT
#define UNUSED_PARAM(a) (void)a;
#endif
void MrdvrTsbStreamer::InMemoryStreamingEntitlemntCallback(void *pData, pEntitlementStatus entStatus)
{
    MrdvrTsbStreamer *pSession = (MrdvrTsbStreamer *)pData;
    LOG(DLOGL_REALLY_NOISY, "session %p, Informing SL new CA Entitlement status %d", pSession, entStatus);

    if (pSession  != NULL)
    {
        LOG(DLOGL_REALLY_NOISY, "%s:Streaming Entitlement callback is called with stattus %d", __func__, entStatus);
        pSession->StreamingEntitlementCallback(entStatus);
    }
    else
    {
        LOG(DLOGL_ERROR, "pSession is NULL,so callback is not called");

    }
}

void MrdvrTsbStreamer::StreamingEntitlementCallback(pEntitlementStatus entStatus)
{

    if (entStatus  ==  CAM_AUTHORIZED_FOR_TUNER_STREAMING)
    {
        LOG(DLOGL_REALLY_NOISY, " %s:CAM_AUTHORIZED_FOR_TUNER_STREAMING ,so kDvrEventServiceAuthorized event is posted%d", __func__, entStatus);
        queueEvent(kDvrEventServiceAuthorized);
    }
    else if (entStatus == CAM_NOT_AUTHORIZED_FOR_TUNER_STREAMING)
    {
        LOG(DLOGL_REALLY_NOISY, " %s:CAM_AUTHORIZED_FOR_TUNER_STREAMING ,so kDvrEventDeServiceAuthorized event is posted%d", __func__, entStatus);
        queueEvent(kDvrEventServiceDeAuthorized);
    }
    else
    {
        LOG(DLOGL_ERROR, " %s:Entitlement status does not match, unknown type: %d something wrongd", __func__, entStatus);

    }



}

bool MrdvrTsbStreamer::IsInMemoryStreaming()
{
    int pos = -1;
    std::string srcURL = "";
    if (mPtrLiveSource)
    {
        srcURL = mPtrLiveSource->getSourceUrl();
    }

    if ((pos = srcURL.find("avfs://item=vod/")) == 0)
    {
        LOG(DLOGL_REALLY_NOISY, "SID:%d Publsihed as vod..Do in-memory stremaing for URL:%s", mSessionId, srcURL.c_str());
        return true;
    }
    LOG(DLOGL_REALLY_NOISY, "SID:%d Publsihed as live..Do TSB streaming for URL:%s", mSessionId, srcURL.c_str());
    return false;
}
///////////////////////////////////////////////////////////////////////////
//                      Member implementation
///////////////////////////////////////////////////////////////////////////

/// MrdvrTsbStreamer constructor function
MrdvrTsbStreamer::MrdvrTsbStreamer(IMediaPlayerSession *pIMediaPlayerSession)
{
    FNLOG(DL_MSP_MRDVR);
    mDestUrl = "";
    mPtrPsi = NULL;
    mPtrAnalogPsi = NULL;
    mState = kDvrStateIdle;
    mCurrentSourceType = kDvrNoSrc;
    mCurrentSource = kMSPInvalidSource;
    mTsbState = kTsbNotCreated;
    mPtrLiveSource = NULL;

    // create event queue for scan thread
    mThreadEventQueue = new MSPEventQueue();
    mPtrPsi = new Psi();
    mPtrRecSession = NULL;
    mCBData = NULL;
    mCCICBFn = NULL;
    mCamCaHandle = NULL;
    mEventHandlerThread = 0;
    mPsiTimeoutThread = 0;
    mptrcaStream = NULL;
    pthread_mutexattr_t mta;
    pthread_mutexattr_init(&mta);
    pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mMutex, &mta);
    mTsbNumber = 0xffff;
    mTsbHardDrive = 0;
    mregid = -1;
    mEntitleid = -1;
    mIMediaPlayerSession = pIMediaPlayerSession;
    mAppClientsList.clear();
    mTunerPriority = kRMPriorityIPClient_VideoWithAudioFocus;
    mPtrTsbStreamerSource = NULL;
    mPtrHnOnDemandStreamerSource = NULL;
    mPtrHnOnDemandRFSource = NULL;
    mptrcaStream = NULL;
    mSessionId = 0;
    mReqState = kHnSessionIdle;
    m_CCIbyte = DEFAULT_RESTRICTIVE_CCI;  //Use default restrict one
    mPsiReady = false;
}

/// MrdvrTsbStreamer Destructor function
MrdvrTsbStreamer::~MrdvrTsbStreamer()
{
    eMspStatus status;
    FNLOG(DL_MSP_MRDVR);

    std::list<CallbackInfo*>::iterator iter;

    LOG(DLOGL_REALLY_NOISY, "LOCKING mCallbackList mutex");

    LOG(DLOGL_REALLY_NOISY, "SIZE=%d", mCallbackList.size());

    for (iter = mCallbackList.begin(); iter != mCallbackList.end(); iter++)
    {
        delete(*iter);
        *iter = NULL;
    }

    mCallbackList.clear();
    LOG(DLOGL_REALLY_NOISY, "AFTER SIZE=%d", mCallbackList.size());

    LOG(DLOGL_REALLY_NOISY, "UNLOCKED mCallbackList mutex");

    if (mPsiTimeoutThread)
    {
        pthread_join(mPsiTimeoutThread, NULL);       // wait for PSI timeout thread to exit
        mPsiTimeoutThread = 0;
    }

    if (mEventHandlerThread)
    {
        queueEvent(kDvrEventExit);  // tell thread to exit
        unLockMutex();
        pthread_join(mEventHandlerThread, NULL);      // wait for event thread to exit
        lockMutex();
        mEventHandlerThread = 0;
    }

    if (mptrcaStream)
    {
        LOG(DLOGL_NORMAL, "%s: SID:%d mptrcaStream instance is deleted,so cleaning up the session", __func__, mSessionId);
        mptrcaStream->unRegisterCCICallback(mregid);
        mptrcaStream->shutdown();
        delete mptrcaStream;
        mptrcaStream = NULL;
    }

    if (mPtrHnOnDemandStreamerSource)
    {
        LOG(DLOGL_REALLY_NOISY, "SID:%d mPtrHnOnDemandStreamerSource->stop", mSessionId);
        mPtrHnOnDemandStreamerSource->stop();
        delete mPtrHnOnDemandStreamerSource;
        mPtrHnOnDemandStreamerSource = NULL;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "SID:%d mPtrHnOnDemandStreamerSource is null", mSessionId);
    }


    if (mPtrTsbStreamerSource != NULL)
    {
        status = mPtrTsbStreamerSource->stop();
        if (status != kMspStatus_Ok)
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s:%d SID:%d TSB Source live streaming Stop failed", __FUNCTION__, __LINE__, mSessionId);
        }

        delete mPtrTsbStreamerSource;
        mPtrTsbStreamerSource = NULL;
    }

    if (mPtrRecSession != NULL)
    {
        mTsbHardDrive = 0;
        mPtrRecSession->UnSetCCICallback();
        status = mPtrRecSession->stop();
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "SID:%d Record Session stop error %d", mSessionId, status);
        }

        mPtrRecSession->unregisterRecordCallback();

        status = mPtrRecSession->close();
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "SID:%d Record Session close error %d", mSessionId, status);
        }

        mTsbState = kTsbNotCreated;

        mPtrRecSession->clearCallback(mRecordSessioncallbackConnection);

        delete mPtrRecSession;
        mPtrRecSession = NULL;
    }

    /*
    Delete psi after deleting RecordSession as RecordSession has reference to PMT data.
    */
    if (mPtrPsi != NULL)
    {
        StopDeletePSI();
    }

    if (mPtrAnalogPsi)
    {
        StopDeleteAnalogPSI();
    }

    if (mTsbNumber != 0xffff)
    {
        TsbHandler * tsbhandler = TsbHandler::getTsbHandlerInstance();
        status = tsbhandler->release_tsb(&mTsbNumber);
        mTsbNumber = 0xffff;
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "release_tsb error %d", status);
        }
    }

    if (mPtrLiveSource != NULL)
    {
        status = mPtrLiveSource->stop();
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "SID:%d RF source stop error %d", mSessionId, status);
        }

        delete mPtrLiveSource;
        mPtrLiveSource = NULL;
    }

    LOG(DLOGL_REALLY_NOISY, "delete threadEventQueue");
    if (mThreadEventQueue)
    {
        delete mThreadEventQueue;
        mThreadEventQueue = NULL;
    }

    mSessionId = 0;
}


/// This method creates the source depending on the service URL
/// Also creates a thread to process events from live source/ PSI and TSB Record sessions
eIMediaPlayerStatus MrdvrTsbStreamer::Load(const char* aServiceUrl, const MultiMediaEvent **pMme)
{
    UNUSED_PARAM(pMme);
    int status;

    FNLOG(DL_MSP_MRDVR);

    if (mState != kDvrStateIdle || mEventHandlerThread != 0)
    {
        LOG(DLOGL_ERROR, "Error wrong state: %d", mState);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (aServiceUrl == NULL)
    {
        LOG(DLOGL_ERROR, "Error null serviceUrl");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    // all we do in load is parse/remember the url for when play happens and start the event thread
    status = parseSource(aServiceUrl);
    if (status != kMspStatus_Ok)
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "Invalid URL");
        return kMediaPlayerStatus_Error_InvalidURL;
    }

    // create and start the Mrdvr LiveStreaming Event Handler
    int threadRetvalue;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, (128 * 1024)); //128kb

    // By default the thread is created as joinable.
    threadRetvalue = pthread_create(&mEventHandlerThread, &attr, eventthreadFunc, (void *) this);
    if (threadRetvalue)
    {
        LOG(DLOGL_ERROR, "pthread_create error %d", threadRetvalue);
        return kMediaPlayerStatus_Error_Unknown;
    }

    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "Created event thread handle %x", (unsigned int)mEventHandlerThread);

    threadRetvalue = pthread_setname_np(mEventHandlerThread, "Mrdvr LiveStreaming Event Handler");
    if (threadRetvalue)
    {
        LOG(DLOGL_ERROR, "thread naming error %d", threadRetvalue);
    }

    mState = kDvrStateStop;

    return  kMediaPlayerStatus_Ok;
}

/// This method creates the source depending on the service URL
eMspStatus MrdvrTsbStreamer::parseSource(const char *aServiceUrl)
{
    FNLOG(DL_MSP_MRDVR);

    MSPSource *source = NULL;
    eMspStatus status = kMspStatus_Ok;

    if (aServiceUrl)
    {
        LOG(DLOGL_REALLY_NOISY, "source: %s", aServiceUrl);

        std::string src_url = aServiceUrl;

        if (mPtrLiveSource == NULL)
        {
            LOG(DLOGL_REALLY_NOISY, "Create new source");
            mCurrentSource = MSPSourceFactory :: getMSPSourceType(aServiceUrl);
            source = MSPSourceFactory::getMSPSourceInstance(mCurrentSource, aServiceUrl, mIMediaPlayerSession);
        }
        else
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "Invalid value of mPtrLiveSource");
            return kMspStatus_Error;
        }

        if (source == NULL)
        {
            LOG(DLOGL_ERROR, "Error null source");
            return kMspStatus_Error;
        }

        LOG(DLOGL_REALLY_NOISY, "Set kDvrLiveSrc");

        mCurrentSourceType = kDvrLiveSrc;
        mPtrLiveSource = source;

        status = mPtrLiveSource->load(sourceCB, this);

        return status;
    }
    else
    {
        LOG(DLOGL_ERROR, "Error null serviceUrl");
        return kMspStatus_Error;
    }
}

/// This method begins playing the media service. Physical resources get assigned during this operation.
eIMediaPlayerStatus MrdvrTsbStreamer::Play(const char* outputUrl, float nptStartTime, const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_MRDVR);

    UNUSED_PARAM(pMme)
    UNUSED_PARAM(nptStartTime)
    UNUSED_PARAM(outputUrl)

    if (mState == kDvrStateIdle && mState != kDvrStateStop)
    {
        LOG(DLOGL_ERROR, "Error Wrong state %d", mState);
        return kMediaPlayerStatus_Error_OutOfState;
    }
    /*
    //Start the thread monitoring TSB started or not
    //if TSB is started within 5 seconds, there will be no action
    //if TSB is not started within 5 seconds, kMediaPlayerSignal_TimeshiftTerminated
    //    will be sent to the callers through media player session callback
    StartTimeoutForTsb();*/

    LOG(DLOGL_REALLY_NOISY, "MrdvrTsbStreamer::Play queueEvent kDvrEventPlay");

    queueEvent(kDvrEventPlay);

    return  kMediaPlayerStatus_Ok;
}

/// This method stops playing the media service.
eIMediaPlayerStatus MrdvrTsbStreamer::Stop(bool stopPlay, bool stopPersistentRecord)
{
    eMspStatus ret_value;
    FNLOG(DL_MSP_MRDVR);

    (void) stopPlay;
    (void) stopPersistentRecord;

    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "SID:%d In MrdvrTsbStreamer::Stop entered \n", mSessionId);

    mState = kDvrStateStop;
    if (mptrcaStream)
    {
        LOG(DLOGL_NORMAL, "%s: SID:%d mptrcaStream instance is deleted,so cleaning up the session", __func__, mSessionId);
        mptrcaStream->unRegisterCCICallback(mregid);
        mptrcaStream->shutdown();
        delete mptrcaStream;
        mptrcaStream = NULL;
    }

    /* Stop the streaming source that is doing HN Live Streaming from TSB source */
    if (mPtrTsbStreamerSource != NULL)
    {
        ret_value = mPtrTsbStreamerSource->stop();
        if (ret_value != kMspStatus_Ok)
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s:%d SID:%d TSB Source live streaming Stop failed", __FUNCTION__, __LINE__, mSessionId);
        }

        delete mPtrTsbStreamerSource;
        mPtrTsbStreamerSource = NULL;
    }

    if (mPtrHnOnDemandStreamerSource)
    {
        LOG(DLOGL_REALLY_NOISY, "SID:%d mPtrHnOnDemandStreamerSource->stop", mSessionId);
        mPtrHnOnDemandStreamerSource->stop();
        delete mPtrHnOnDemandStreamerSource;
        mPtrHnOnDemandStreamerSource = NULL;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "SID:%d mPtrHnOnDemandStreamerSource is null", mSessionId);
    }

    //Ensuring PSI stop, in case of normal channel change with no live recording.
    if (mPtrPsi != NULL)
    {
        StopDeletePSI();
    }

    if (mPsiTimeoutThread)
    {
        pthread_join(mPsiTimeoutThread, NULL);       // wait for PSI timeout thread to exit
        mPsiTimeoutThread = 0;
    }

    if (mPtrAnalogPsi)
    {
        StopDeleteAnalogPSI();
    }

    /* Stop the TSB buffering session */
    ret_value = StopRecordSession();
    if (ret_value != kMspStatus_Ok)
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s:%d SID:%d RecordSession Stop failed", __FUNCTION__, __LINE__, mSessionId);
    }

    if (mPtrLiveSource && mPtrLiveSource->isSDV())
    {
        mPtrLiveSource->setMediaSessionInstance(NULL);
    }

    /* stop the RF source and release the tuner */
    if (mPtrLiveSource != NULL)
    {
        ret_value = mPtrLiveSource->stop();
        if (ret_value != kMspStatus_Ok)
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s:%d SID:%d RF Source Stop failed", __FUNCTION__, __LINE__, mSessionId);
        }

        delete mPtrLiveSource;
        mPtrLiveSource = NULL;
    }

    if (mThreadEventQueue)
    {
        mThreadEventQueue->flushQueue(); //flush out any pending events posted prior/during Stop() call.
    }

    return  kMediaPlayerStatus_Ok;
}


///   This method releases all the resources from the controller
void MrdvrTsbStreamer::Eject()
{
    FNLOG(DL_MSP_MRDVR);

    if (mPsiTimeoutThread)
    {
        pthread_join(mPsiTimeoutThread, NULL);       // wait for PSI timeout thread to exit
        mPsiTimeoutThread = 0;
    }
    if (mEventHandlerThread)
    {
        queueEvent(kDvrEventExit);  // tell thread to exit
        unLockMutex();
        pthread_join(mEventHandlerThread, NULL);       // wait for event thread to exit
        lockMutex();
        mEventHandlerThread = 0;
    }

    if (mptrcaStream)
    {
        LOG(DLOGL_NORMAL, "%s: SID:%d Eject is called,so cleaning up the session", __func__, mSessionId);
        mptrcaStream->unRegisterCCICallback(mregid);
        mptrcaStream->shutdown();
        delete mptrcaStream;
        mptrcaStream = NULL;
    }
    /* Stop the streaming source that is doing HN Live Streaming from TSB source */
    if (mPtrTsbStreamerSource != NULL)
    {
        eMspStatus status = mPtrTsbStreamerSource->stop();
        if (status != kMspStatus_Ok)
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s:%d SID:%d TSB Source live streaming Stop failed", __FUNCTION__, __LINE__, mSessionId);
        }

        delete mPtrTsbStreamerSource;
        mPtrTsbStreamerSource = NULL;
    }

    if (mPtrHnOnDemandStreamerSource)
    {
        LOG(DLOGL_REALLY_NOISY, "SID:%d mPtrHnOnDemandStreamerSource->stop", mSessionId);
        mPtrHnOnDemandStreamerSource->stop();
        delete mPtrHnOnDemandStreamerSource;
        mPtrHnOnDemandStreamerSource = NULL;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "SID:%d mPtrHnOnDemandStreamerSource is null", mSessionId);
    }

    if (mPtrRecSession != NULL)
    {
        mTsbHardDrive = 0;
        mPtrRecSession->UnSetCCICallback();
        mPtrRecSession->stop();
        mPtrRecSession->unregisterRecordCallback();
        mPtrRecSession->close();
        mPtrRecSession->clearCallback(mRecordSessioncallbackConnection);
        mTsbState = kTsbNotCreated;
        delete mPtrRecSession;
        mPtrRecSession = NULL;
    }
    /*
        Delete psi after deleting RecordSession as RecordSession has reference to PMT data.
    */
    StopDeletePSI();
    if (mPtrAnalogPsi)
    {
        StopDeleteAnalogPSI();
    }

    if (mPtrLiveSource)
    {
        if (mPtrLiveSource->isSDV())
        {
            mPtrLiveSource->setMediaSessionInstance(NULL);
        }
    }

    if (mPtrLiveSource != NULL)
    {
        mPtrLiveSource->stop();
        delete mPtrLiveSource;
        mPtrLiveSource = NULL;
    }

    if (mTsbNumber != 0xffff)
    {
        TsbHandler * tsbhandler = TsbHandler::getTsbHandlerInstance();
        eMspStatus status = tsbhandler->release_tsb(&mTsbNumber);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Error release_tsb status: %d  mTsbNumber: %d", status, mTsbNumber);
        }
        mTsbNumber = 0xffff;
    }
}

///   This method stops playing the media service and restarts playing the media service.
void MrdvrTsbStreamer::tearDownToRetune()
{
    eMspStatus status = kMspStatus_Ok;

    /* Stop the streaming source that is doing HN Live Streaming from TSB source */
    if (mptrcaStream)
    {
        LOG(DLOGL_NORMAL, "%s: SID:%d mptrcaStream instance is deleted,so cleaning up the session", __func__, mSessionId);
        mptrcaStream->unRegisterCCICallback(mregid);
        mptrcaStream->shutdown();
        delete mptrcaStream;
        mptrcaStream = NULL;
    }
    if (mPtrTsbStreamerSource != NULL)
    {
        status = mPtrTsbStreamerSource->stop();
        if (status != kMspStatus_Ok)
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s:%d SID:%d TSB Source live streaming Stop failed", __FUNCTION__, __LINE__, mSessionId);
        }

        delete mPtrTsbStreamerSource;
        mPtrTsbStreamerSource = NULL;
    }

    if (mPtrHnOnDemandStreamerSource)
    {
        LOG(DLOGL_REALLY_NOISY, "SID:%d mPtrHnOnDemandStreamerSource->stop", mSessionId);
        mPtrHnOnDemandStreamerSource->stop();
        delete mPtrHnOnDemandStreamerSource;
        mPtrHnOnDemandStreamerSource = NULL;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "SID:%d mPtrHnOnDemandStreamerSource is null", mSessionId);
    }


    if (mPtrRecSession != NULL)
    {
        status = StopRecordSession();
        if (status != kMspStatus_Ok)
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "SID:%d Record session unregister callback failed\n", mSessionId);
        }
    }

    StopDeletePSI();
    StopDeleteAnalogPSI();

    if (mPtrLiveSource)
    {
        if (mPtrLiveSource->isSDV())
        {
            mPtrLiveSource->setMediaSessionInstance(NULL);
        }
    }

    if (mPtrLiveSource != NULL)
    {
        mPtrLiveSource->stop();    //TODO status check
    }
}

//This function will tear down the CLIENT session.
void MrdvrTsbStreamer::tearDownClientSession()
{
    FNLOG(DL_MSP_MRDVR);
    int cpeStatus = kCpe_NoErr;
    eMspStatus status = kMspStatus_Error;
    if (NULL != mPtrTsbStreamerSource)
    {
        if (mPtrTsbStreamerSource->getCpeProgHandle() != 0)
        {
            cpeStatus = cpe_hnsrvmgr_Stop(mPtrTsbStreamerSource->getCpeProgHandle());
            if (cpeStatus != kCpe_NoErr)
            {
                LOG(DLOGL_ERROR, "SID:%d MRDVR HN server stop failed with error code %d", mSessionId, cpeStatus);
            }
            else
            {
                LOG(DLOGL_NOISE, "SID:%d MRDVR HN server stop successful with error code %d", mSessionId, cpeStatus);
            }
        }

    }
    else if (mPtrHnOnDemandStreamerSource)
    {
        if (mPtrHnOnDemandStreamerSource->getCpeProgHandle() != 0)
        {
            cpeStatus = cpe_hnsrvmgr_Stop(mPtrHnOnDemandStreamerSource->getCpeProgHandle());
            if (cpeStatus != kCpe_NoErr)
            {
                LOG(DLOGL_ERROR, "SID:%d MRDVR HN server stop failed with error code %d", mSessionId, cpeStatus);
            }
            else
            {
                LOG(DLOGL_NOISE, "SID:%d MRDVR HN server stop successful with error code %d", mSessionId, cpeStatus);
            }

        }
    }

    else
    {

        LOG(DLOGL_NORMAL, "SID:%d Deleting  the TSB and closing the RF Source,since streaming has not been started so far", mSessionId);

        if (mPtrRecSession)
        {
            status = mPtrRecSession->stopConvert();
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "%s:%d TSB Conversion Stop failed", __FUNCTION__, __LINE__);
            }

            status = StopRecordSession();
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "Record session unregister callback failed\n");
            }
        }

        StopDeletePSI();
        StopDeleteAnalogPSI();

        deleteAllClientSession();

        if (mPtrLiveSource)
        {

            if (mPtrLiveSource->isSDV())
            {
                mPtrLiveSource->setMediaSessionInstance(NULL);
            }

            if (mPtrLiveSource)
            {
                LOG(DLOGL_NORMAL, "SID:%d Stopping the source before starting the streaming ", mSessionId);
                mPtrLiveSource->stop();
            }
            else
            {
                LOG(DLOGL_ERROR, "SID:%d mPtrLiveSource is NULL ,so cpe source stop canot be called ", mSessionId);
            }

        }

        mState = kDvrStateStop;

    }

}

///   This method starts the TSB buffering/recording session.
eMspStatus MrdvrTsbStreamer::StartTSBRecordSession(std::string first_fragment_file)
{
    FNLOG(DL_MSP_MRDVR);
    eMspStatus status;

    TsbHandler *tsbhandler = TsbHandler::getTsbHandlerInstance();

    assert(tsbhandler);

    LOG(DLOGL_MINOR_DEBUG, "get_tsb mTsbNumber: %d", mTsbNumber);

    if (mPtrRecSession == NULL) //sometimes, Record session might be attempted to create more than once,if previous attempt failed.
    {
        eMspStatus status = tsbhandler->get_tsb(&mTsbNumber);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "SID:%d Error: No available TSB", mSessionId);
            return status;
        }

        mPtrRecSession = new MSPRecordSession();
    }

    if (mPtrRecSession != NULL)
    {
        mPtrRecSession->setCallback(boost::bind(&MrdvrTsbStreamer::recordSessionCallbackFunction, this, _1, _2));
        LOG(DLOGL_REALLY_NOISY, "SID:%d Set Callback for dispatching authorization signals..", mSessionId);
    }
    else
    {
        LOG(DLOGL_ERROR, "SID:%d Null object.. Couldnt allocate memory", mSessionId);
        return kMspStatus_OutofMemory;
    }

    if (mPtrRecSession && mPtrLiveSource)
    {
        LOG(DLOGL_MINOR_DEBUG, "SID:%d mPtrRecSession: %p  call open with mTsbHardDrive: %d  mTsbNumber: %d" , mSessionId,
            mPtrRecSession, mTsbHardDrive, mTsbNumber);

        if (mPtrLiveSource->isAnalogSource())
        {
            dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "%s %d SID:%d This is ANALOG Source ...So using ANALOG PSI \n", __FILE__, __LINE__, mSessionId);
            status = mPtrRecSession->open(mPtrLiveSource->getCpeSrcHandle(), &mTsbHardDrive, mTsbNumber, mPtrAnalogPsi,
                                          mPtrLiveSource->getSourceId(), first_fragment_file);
        }
        else
        {
            dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "%s %d SID:%d This is Digital Source ...So using Digital PSI \n", __FILE__, __LINE__, mSessionId);
            status = mPtrRecSession->open(mPtrLiveSource->getCpeSrcHandle(), &mTsbHardDrive, mTsbNumber, mPtrPsi,
                                          mPtrLiveSource->getSourceId(), first_fragment_file);
        }

        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "SID:%d Error: mPtrRecSession->open ", mSessionId);
            return status;
        }

        LOG(DLOGL_MINOR_DEBUG, "SID:%d after open mTsbHardDrive: %d" , mSessionId, mTsbHardDrive);

        status = mPtrRecSession->registerRecordCallback(recordSessionCallback, this);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Error registerRecordCallback");
            return status;
        }

        mPtrRecSession->SetCCICallback(mCBData, mCCICBFn);
        dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "SID:%d Set CCI Callback called to register for CCI updates from CAM", mSessionId);
        if (mPtrAnalogPsi && mPtrLiveSource && mPtrLiveSource->isAnalogSource())
        {
            dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "%s %d SID:%d This is ANALOG Source ...So using ANALOG PSI to Start Recording Session  \n", __FILE__, __LINE__, mSessionId);
            status = mPtrRecSession->start(mPtrAnalogPsi);
            if (status == kMspStatus_Ok)
            {
                dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "%s:%d SID:%d Successfully started ANALOG TSB!!! :) ", __FUNCTION__, __LINE__, mSessionId);
            }
        }
        else
        {
            dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "%s %d SID:%d This is DIGITAL Source ...So using DIGITAL PSI to Start Recording Session \n", __FILE__, __LINE__, mSessionId);
            status = mPtrRecSession->start();
            if (status == kMspStatus_Ok)
            {
                dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "%s:%d SID:%d Successfully started TSB!!! :) ", __FUNCTION__, __LINE__, mSessionId);
            }
        }

        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "SID:%d Error: Unable to start TSB!", mSessionId);
            return status;
        }

        return kMspStatus_Ok;
    }
    else
    {
        LOG(DLOGL_ERROR, "SID:%d Error: unable to alloc MSPRecordSession", mSessionId);
        return kMspStatus_Error;
    }
}

///   This method stops the TSB buffering/ recording session.
eMspStatus MrdvrTsbStreamer::StopRecordSession()
{
    FNLOG(DL_MSP_MRDVR);

    if (mPtrRecSession != NULL)
    {
        mPtrRecSession->UnSetCCICallback();
        eMspStatus ret_value = mPtrRecSession->stop();
        if (ret_value != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "SID:%d stop error %d", mSessionId, ret_value);
            return ret_value;
        }

        mTsbHardDrive = -1;
        mPtrRecSession->unregisterRecordCallback();
        mPtrRecSession->close();
        mTsbState = kTsbNotCreated;
        mPtrRecSession->clearCallback(mRecordSessioncallbackConnection);
        delete mPtrRecSession;
        mPtrRecSession = NULL;
    }

    if (mTsbNumber != 0xffff)
    {
        TsbHandler * tsbhandler = TsbHandler::getTsbHandlerInstance();
        eMspStatus status = tsbhandler->release_tsb(&mTsbNumber);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Error release_tsb status: %d  mTsbNumber: %d", status, mTsbNumber);
        }
        mTsbNumber = 0xffff;
    }

    return kMspStatus_Ok;
}

///   This method stops and deletes the PSI peocess
void MrdvrTsbStreamer::StopDeletePSI()
{
    FNLOG(DL_MSP_MRDVR);
    if (mPtrPsi != NULL)
    {
        delete mPtrPsi;
        mPtrPsi = NULL;
    }
}

///   This method stops and deletes the Analog PSI process.
void MrdvrTsbStreamer::StopDeleteAnalogPSI()
{
    FNLOG(DL_MSP_MRDVR);
    if (mPtrAnalogPsi != NULL)
    {
        mPtrAnalogPsi->psiStop();
        //mPtrAnalogPsi->unRegisterPsiCallback();
        delete mPtrAnalogPsi;
        mPtrAnalogPsi = NULL;
    }
}

///   This method returns the CPERP program handle for the current session
tCpePgrmHandle MrdvrTsbStreamer::getCpeProgHandle()
{
    if (mPtrTsbStreamerSource)
        return mPtrTsbStreamerSource->getCpeProgHandle();
    else if (mPtrHnOnDemandStreamerSource)
        return mPtrHnOnDemandStreamerSource->getCpeProgHandle();
    else
        return 0;
}

///   This method sets/ stores the CPERP streaming session ID in controller/ source
void MrdvrTsbStreamer::SetCpeStreamingSessionID(uint32_t sessionId)
{
    mSessionId = sessionId;
    if (mPtrTsbStreamerSource)
    {
        mPtrTsbStreamerSource->SetCpeStreamingSessionID(sessionId);
    }
    else if (mPtrHnOnDemandStreamerSource)
    {
        mPtrHnOnDemandStreamerSource->SetCpeStreamingSessionID(sessionId);
    }
    ValidateSessionState();
    return;
}

///   This method allows the user to register a callback to receive the media player status events
eIMediaPlayerStatus MrdvrTsbStreamer::SetCallback(IMediaPlayerSession *pIMediaPlayerSession, IMediaPlayerStatusCallback cb, void* pClientContext)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(cb)
    UNUSED_PARAM(pClientContext)

    CallbackInfo *cbInfo = new CallbackInfo();
    if (cbInfo)
    {
        cbInfo->mpSession = pIMediaPlayerSession;
        cbInfo->mCallback = cb;
        cbInfo->mClientContext = pClientContext;
        mCallbackList.push_back(cbInfo);
    }

    return  kMediaPlayerStatus_Ok;
}

///   This method allows the user to un-register a callback registered earlier
///   to receive the media player status events
eIMediaPlayerStatus MrdvrTsbStreamer::DetachCallback(IMediaPlayerStatusCallback cb)
{
    std::list<CallbackInfo*>::iterator iter;
    eIMediaPlayerStatus status = kMediaPlayerStatus_Error_InvalidParameter;
    for (iter = mCallbackList.begin(); iter != mCallbackList.end(); iter++)
    {
        if ((*iter)->mCallback == cb)
        {
            mCallbackList.remove(*iter);
            delete(*iter);
            *iter = NULL;
            status = kMediaPlayerStatus_Ok;
            break;
        }
    }
    return status;
}

///   This method receives the source related notifications from sources like RF source/ PPV source etc
void MrdvrTsbStreamer::sourceCB(void *aData, eSourceState aSrcState)
{
    MrdvrTsbStreamer *inst = (MrdvrTsbStreamer *)aData;
    if (inst != NULL)
    {
        switch (aSrcState)
        {
        case kSrcTunerLocked:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "source callback: tuner lock callback");
            inst->queueEvent(kDvrEventRFCallback);
            break;

        case kAnalogSrcTunerLocked:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "source callback: tuner lock callback kAnalogSrcTunerLocked\n");
            inst->queueEvent(kDvrEventRFAnalogCallback);
            break;

        case kSrcBOF:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "BOF event");
            inst->queueEvent(kDvrEventBOF);
            break;

        case kSrcEOF:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "EOF event");
            inst->queueEvent(kDvrEventEOF);
            break;

        case kSrcSDVServiceLoading:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, " \" SDV Service Loading \"  event ");
            inst->queueEvent(kDvrEventSDVLoading);
            break;

        case kSrcSDVServiceUnAvailable:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, " \"SDV Service Un-Available \" event");
            inst->queueEvent(kDvrEventSDVUnavailable);
            break;

        case kSrcSDVServiceChanged:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, " \" SDV Service changed\" event ");
            inst->queueEvent(kDvrEventTuningUpdate);
            break;

        case kSrcSDVKeepAliveNeeded:
            LOG(DLOGL_NOISE, " \" SDV KeepAlive Needed \" event ");
            inst->queueEvent(kDvrEventSDVKeepAliveNeeded);
            break;

        case kSrcNextFile:
            inst->queueEvent(kDvrNextFilePlay);
            break;

        case kSrcSDVServiceCancelled:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, " \" SDV Service Cancelled \" event");
            inst->queueEvent(kDvrEventSDVCancelled);
            break;

        case kSrcSDVKnown:
            inst->queueEvent(kDvrEventSDVLoaded);
            break;

        case kSrcPPVInterstitialStart:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "source callback: ppv interstitial start callback");
            inst->queueEvent(kDvrPpvInstStart);
            break;

        case kSrcPPVInterstitialStop:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "source callback: ppv interstitial stop callback");
            inst->queueEvent(kDvrPpvInstStop);
            break;

        case kSrcPPVSubscriptionAuthorized:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "source callback: ppv authorization callback");
            inst->queueEvent(kDvrPpvSubscnAuth);
            break;

        case kSrcPPVSubscriptionExpired:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "source callback: ppv auth expiration callback");
            inst->queueEvent(kDvrPpvSubscnExprd);
            break;

        case kSrcPPVStartVideo:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "source callback: ppv start video callback");
            inst->queueEvent(kDvrPpvStartVideo);
            break;

        case kSrcPPVStopVideo:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "source callback: ppv stop video callback");
            inst->queueEvent(kDvrPpvStopVideo);
            break;


        case kSrcPPVContentNotFound:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "Source Callback PPV Content not found");
            inst->queueEvent(kDvrPpvContentNotFound);
            break;

        case kSrcTunerLost:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "Source Callback Tuner lost");
            inst->queueEvent(kDvrTunerLost);
            break;

        case kSrcTunerRegained:
            dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "Source Callback Tuner restored");
            inst->queueEvent(kDvrTunerRestored);
            break;
        case kSrcTunerUnlocked:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "Source Callback Tuner lost due to RF disconnect");
            inst->queueEvent(kDvrEventTunerUnlocked);
            break;

        default:
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "Not a valid callback event");
            break;
        }
    }
}

///   This method receives the PSI related notifications from PSI monitoring process
void MrdvrTsbStreamer::psiCallback(ePsiCallBackState aState, void *aData)
{
    FNLOG(DL_MSP_MRDVR);

    MrdvrTsbStreamer *inst = (MrdvrTsbStreamer *)aData;

    switch (aState)
    {
    case kPSIReady:
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "PSI Ready Callback arrived!! ");
        inst->queueEvent(kDvrPSIReadyEvent);
        break;
    case kPSIUpdate:
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "PSI Update Callback arrived!! ");
        inst->queueEvent(kDvrPSIUpdateEvent);
        break;

    case kPSITimeOut:
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "PSI Timeout Callback arrived!! ");
        inst->queueEvent(kDvrTimeOutEvent);
        break;

    case kPSIError:
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "PSI Error Callback arrived!!");
        inst->queueEvent(kDvrTimeOutEvent);
        break;

    default:
        break;
    }

}

///   This method receives the record relates notifications from CPERP recording module
void MrdvrTsbStreamer::recordSessionCallback(tCpeRecCallbackTypes aType, void *aData)
{
    FNLOG(DL_MSP_MRDVR);

    MrdvrTsbStreamer *inst = (MrdvrTsbStreamer *)aData;

    if (inst == NULL)
    {
        LOG(DLOGL_ERROR, "Record session callback of type %d with NULL controller pointer reference", aType);
    }
    else
    {
        switch (aType)
        {
        case eCpeRecCallbackTypes_RecordingStarted:
            LOG(DLOGL_NOISE, "TSB start callback event");
            inst->queueEvent(kDvrTSBStartEvent);
            break;

        case eCpeRecCallbackTypes_ConversionComplete:
            LOG(DLOGL_NOISE, "TSB conversion complete event");
            inst->queueEvent(kDvrRecordingStoppedEvent);
            break;

        case eCpeRecCallbackTypes_DiskFull:
            LOG(DLOGL_NOISE, "Recording disk full event");
            inst->queueEvent(kDvrRecordingDiskFull);
            break;

        case eCpeRecCallbackTypes_RecAborted:
            LOG(DLOGL_NOISE, "Recording aborted  event");
            inst->queueEvent(kDvrRecordingAborted);
            break;

        default:
            break;
        }
    }
}

///   This method receives the auth events from CPERP
void MrdvrTsbStreamer::recordSessionCallbackFunction(eIMediaPlayerSignal sig, eIMediaPlayerStatus stat)
{
    UNUSED_PARAM(stat);

    switch (sig)
    {
    case kMediaPlayerSignal_ServiceAuthorized:
        queueEvent(kDvrEventServiceAuthorized);
        break;

    case kMediaPlayerSignal_ServiceDeauthorized:
        queueEvent(kDvrEventServiceDeAuthorized);
        break;

    default:
        LOG(DLOGL_ERROR, "Unhandled event from display session about authorization status %d", sig);
        break;
    }
}

///   This method queues the events onto mThreadEventQueue
eMspStatus MrdvrTsbStreamer::queueEvent(eDvrEvent evtyp)
{
    if (!mThreadEventQueue)
    {
        LOG(DLOGL_ERROR, "error: no event queue");
        return kMspStatus_BadParameters;
    }

    mThreadEventQueue->dispatchEvent(evtyp);
    return kMspStatus_Ok;
}


///   This method listens to the events posted onto mThreadEventQueue
///   and calls dispatchEvent member function to process the events read from the queue
void* MrdvrTsbStreamer::eventthreadFunc(void *aData)
{
    bool done = false;
    MrdvrTsbStreamer *inst = (MrdvrTsbStreamer *)aData;
    Event *evt;

    FNLOG(DL_MSP_MRDVR);

    while (!done)
    {
        unsigned int waitTime = 0;
        if (inst->mState == kDvrWaitSourceReady)
        {
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "Inside Wait for Tuner lock\n");
            waitTime = TIMEOUT;
        }
        inst->mThreadEventQueue->setTimeOutSecs(waitTime);
        evt = inst->mThreadEventQueue->popEventQueue();
        inst->mThreadEventQueue->unSetTimeOut();
        inst->lockMutex();
        dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "Event thread got event %d ", evt->eventType);
        done = inst->dispatchEvent(evt);  // call member function to handle event
        inst->mThreadEventQueue->freeEvent(evt);
        inst->unLockMutex();
    }

    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "Exiting MrdvrTsbStreamer event thread\n");
    pthread_exit(NULL);
    return NULL;
}


///   This method either process the events locally
///    or
///   notifies the events to the registered parties using DoCallback method
///   depending on the type of event
bool MrdvrTsbStreamer::dispatchEvent(Event *aEvt)
{
    FNLOG(DL_MSP_MRDVR);
    int status;

    if (aEvt == NULL)
    {
        LOG(DLOGL_ERROR, "Error: Null event instance");
        return false;
    }
    switch (aEvt->eventType)
    {
        // exit
    case kDvrEventExit:
        LOG(DLOGL_NOISE, "Exit MrdvrTsbStreamer Ctrl Thread!");
        return true;

        // no break required here
    case kDvrTimeOutEvent:
        if (mPtrLiveSource)
        {
            if (mPtrLiveSource->isPPV())
            {
                LOG(DLOGL_ERROR, "Timeout waiting for video on PPV channel.Skipping intimation of services layer");
            }
            else if (mPtrLiveSource->isSDV())
            {
                LOG(DLOGL_ERROR, "Warning: Tuner lock time Out for SDV Channel... Callback to MDA about SDV Service Not Available...");
                DoCallback(kMediaPlayerSignal_ServiceNotAvailableDueToSdv, kMediaPlayerStatus_Ok);
            }
            else
            {
                if (mState == kDvrWaitSourceReady)
                {
                    LOG(DLOGL_ERROR, "Warning: Tuner lock timeout.Signal strength may be low or no stream on tuned frequency");
                }
                else if (mState == kDvrSourceReady) // i.e. waiting for PSI to be ready
                {
                    //Since mState != kDvrStateStreaming, notify serve failure as streaming has not started
                    LOG(DLOGL_ERROR, "Warning: PSI Data not ready... ");
                    NotifyServeFailure();
                    mReqState = kHnSessionPsiTimeOut;
                }
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "NULL Live source pointer on timeout event");
        }
        break;

        // play request cam in, try to reserve the tuner resource and open RF source
    case kDvrEventPlay:
        if ((mCurrentSourceType == kDvrLiveSrc) && (mPtrLiveSource))
        {
            eMspStatus status = kMspStatus_Ok;

            // try to open the rf tuner source with the required priority
            status = mPtrLiveSource->open(mTunerPriority);

            if (status == kMspStatus_Loading)
            {
                dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "We are still waiting for tuning parameters, we will revisit Play again\n");
                mState = kDvrWaitTuningParams;
                break;
            }
            else if (status == kMspStatus_WaitForTuner)
            {
                dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "Waiting for tuning acquisition");
                mState = kDvrWaitForTuner;
                break;
            }
            else if (status == kMspStatus_SdvError)
            {
                DoCallback(kMediaPlayerSignal_ServiceNotAvailableDueToSdv, kMediaPlayerStatus_Ok);
                LOG(DLOGL_MINOR_EVENT, "Callback Problem SDV Service Not Available");
            }
            else if (status != kMspStatus_Ok)
            {
                if (mPtrLiveSource->isPPV())
                {
                    LOG(DLOGL_ERROR, "Its PPV source,avoiding sending signal to service layer");
                }
                else
                {
                    LOG(DLOGL_ERROR, "SID:%d Rf source open failed. Notify serve failure to platform", mSessionId);
                    NotifyServeFailure();
                    DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_InvalidParameter);
                }
                LOG(DLOGL_MINOR_EVENT, "Callback Problem InvalidParameter");
            }
            else
            {
                LOG(DLOGL_EMERGENCY, "SID:%d RF source started for session: %p.Video available.going to tune for it\n", mSessionId, mIMediaPlayerSession);
                status = mPtrLiveSource->start();
                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "Source Start error %d", status);
                    DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
                }
                mState = kDvrWaitSourceReady;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "SID:%d Error: unknown source type %d", mCurrentSourceType, mSessionId);
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_InvalidURL);
        }
        break;

        // get RF tuner locked callback, create display session and psi, start psi
    case kDvrEventRFCallback:
        if (mReqState == kHnSessionUnlocked)
        {
            // Moving the log level to ERROR intentionally to track TSB error scenario trick mode issues.
            LOG(DLOGL_EMERGENCY, "SID:%d Got RF callback indicating a tuner locked event. Sending resource restored signal only if it was tuner unlocked state earlier.", mSessionId);
            if ((NULL != mPtrRecSession) && (false == IsInMemoryStreaming()))
            {
                status = mPtrRecSession->tsbPauseResume(false);
                if (kMspStatus_Ok != status)
                {
                    LOG(DLOGL_ERROR, "TSB Resume returned error %d", status);
                }
            }

            DoCallback(kMediaPlayerSignal_ResourceRestored, kMediaPlayerStatus_Ok);
            mReqState = kHnSessionLocked;
        }
        if (mState != kDvrWaitSourceReady)
        {
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "SID:%d Tuner lock callback in wrong state. State: %d\n", mSessionId, mState);
        }
        else
        {
            mState = kDvrSourceReady;
            if (mPtrPsi == NULL)
            {
                mPtrPsi = new Psi();
            }
            mPtrPsi->registerPsiCallback(psiCallback, this);
            if ((mCurrentSourceType == kDvrLiveSrc) && (mPtrLiveSource))
            {
                LOG(DLOGL_EMERGENCY, "SID:%d RFtuner callback arrived. Tuned successfully for session: %p. Starting PSI!!!!!!!!", mSessionId, mIMediaPlayerSession);
                mPtrPsi->psiStart(mPtrLiveSource);
                StartPSITimeout();
            }
        }
        break;

        // get RF tuner locked callback, create display session and psi, start psi
    case kDvrEventRFAnalogCallback:
        if (mState != kDvrWaitSourceReady)
        {
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "Analog Tuner lock callback in wrong state. State: %d\n", mState);
        }
        else if (mPtrLiveSource)
        {
            mPtrAnalogPsi = new AnalogPsi();
            mState = kDvrSourceReady;
            status = mPtrAnalogPsi->psiStart(mPtrLiveSource);
            psiCallback(kPSIReady, (void *)this);
        }
        break;

    case kDvrPSIReadyEvent:
        mPsiReady = true;
        if (IsInMemoryStreaming() == true)
        {
            LOG(DLOGL_EMERGENCY, "SID:%d Doing in memory streaming on PSI Ready Event received", mSessionId);
            StartTunerStreaming();
        }
        else if (mState == kDvrSourceReady)
        {
            if (mPtrLiveSource->isSDV())
            {
                LOG(DLOGL_NOISE, "Updating SDV with MediaPlayerSession");
                mPtrLiveSource->setMediaSessionInstance(mIMediaPlayerSession);
            }

            /* starting TSB buffering session for the purpose of LIVE streaming */
            status = StartTSBRecordSession("");
            if (status == kMspStatus_NotSupported)
            {
                LOG(DLOGL_ERROR, "SID:%d Music channel with no video.Recording not supported\n", mSessionId);
            }
            else if (status != kMspStatus_Ok)
            {
                // TODO:  This needs a callback if not recording ??
                LOG(DLOGL_ERROR, "SID:%d Error: TSB Creation Failed\n", mSessionId);
                DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_NotSupported);
            }
            else
            {
                LOG(DLOGL_EMERGENCY, "SID:%d TSB creation on kDvrPSIReadyEvent Successfull for session %p\n", mSessionId, mIMediaPlayerSession);
                mTsbState = kTsbCreated;
            }
        }
        else
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "SID:%d Warning: not starting TSB buffering, mState: %d\n", mSessionId, mState);
        }
        break;

    case kDvrTSBStartEvent:
        if ((mReqState == kHnSessionAuth || mReqState == kHnSessionLocked || mReqState == kHnSessionPsiTimeOut) && mTsbState != kTsbStarted)
        {
            LOG(DLOGL_EMERGENCY, "Streaming is yet to start for the session.. It has recovered from the error now and ready to stream.. initimate the client to retry");
            DoCallback(kMediaPlayerSignal_ServiceRetry, kMediaPlayerStatus_Ok);
            mTsbState = kTsbCreated;
            break;
        }
        if (mTsbState == kTsbCreated && mState == kDvrSourceReady)
        {

            LOG(DLOGL_EMERGENCY, "SID:%d Recieved Recording started Alarm.TSB started successfully \n", mSessionId);

            mTsbState = kTsbStarted;

            if (mPtrTsbStreamerSource == NULL)
            {
                std::string aSrcUrl = "";

                if (mPtrRecSession)
                {
                    aSrcUrl = mPtrRecSession->GetTsbFileName();
                }

                /* Converting the TSB file name (/mnt/dvr0/filename) to AVFS format (avfs://mnt/dvr0/filename) */
                aSrcUrl.replace(0, 1, "avfs://");
                LOG(DLOGL_EMERGENCY, "Starting streaming from the TSB file %s \n", aSrcUrl.c_str());

                mPtrTsbStreamerSource = new MSPMrdvrStreamerSource(aSrcUrl);
                if (mPtrTsbStreamerSource == NULL)
                {
                    LOG(DLOGL_ERROR, "SID:%d Creating  TSB Streamer source failed \n", mSessionId);
                    //NotifyServeFailure(); TODO: Need to uncomment after platform fix
                }
            }

            if (mPtrTsbStreamerSource)
            {
                // Set the CPERP streaming session id with TSB streaming source
                mPtrTsbStreamerSource->SetCpeStreamingSessionID(mSessionId);

                // Loading the TSB streaming source
                eMspStatus status = mPtrTsbStreamerSource->load(sourceCB, (void*) this);
                if (status != kMspStatus_Ok)
                {
                    dlog(DL_MSP_MRDVR, DLOGL_ERROR, "SID:%d TSB streaming source load failed \n", mSessionId);
                    //NotifyServeFailure(); TODO: Need to uncomment after platform fix
                    DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_UnknownSession);
                    break;
                }

                // Opening the TSB streaming source
                if (status == kMspStatus_Ok)
                {
                    status = mPtrTsbStreamerSource->open(kRMPriorityIPClient_VideoWithAudioFocus);
                    if (status != kMspStatus_Ok)
                    {
                        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "SID:%d TSB streaming source open failed \n", mSessionId);
                        //NotifyServeFailure(); TODO: Uncomment after platform fix
                        if (status == kMspStatus_TimeOut)
                        {
                            mReqState = kHnSessionTimedOut;
                            mTsbState = kTsbCreated;//yet to start and stream the tsb content as session timedout.. so move back the state
                            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "Session timed out, But the resources are available for streaming.. notify the client to retry and establish connection");
                            DoCallback(kMediaPlayerSignal_ServiceRetry, kMediaPlayerStatus_Ok);
                        }
                        else
                        {
                            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_UnknownSession);
                        }
                        break;
                    }
                }

                // Starting the TSB streaming source
                if (status == kMspStatus_Ok)
                {
                    status = mPtrTsbStreamerSource->InjectCCI(m_CCIbyte);
                    if (status != kMspStatus_Ok)

                    {
                        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s :InjectCCI returns Error ", __func__);
                    }
                    else
                        dlog(DL_MSP_MRDVR, DLOGL_NOISE, " %s:InjectCCI returns success ", __func__);

                    status = mPtrTsbStreamerSource->start();
                    if (status != kMspStatus_Ok)
                    {
                        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "SID:%d TSB streaming source start failed \n", mSessionId);
                        //NotifyServeFailure(); TODO: Uncomment after Platform fix
                        DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_UnknownSession);
                        break;
                    }
                }

                if (status != kMspStatus_Ok)
                {
                    dlog(DL_MSP_MRDVR, DLOGL_ERROR, "SID:%d After doing everything failed in the last step. So sad...\n for client with session_handle:%p ProgramHandle:%p, URL: %s", mSessionId, mIMediaPlayerSession, getCpeProgHandle(), GetSourceURL().c_str());
                    DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_UnknownSession);
                    break;
                }
                else
                {
                    dlog(DL_MSP_MRDVR, DLOGL_EMERGENCY, "SID:%d NICE to see that everything went well and streaming started... for client with session_handle:%p ProgramHandle:%p, URL: %s", mSessionId, mIMediaPlayerSession, getCpeProgHandle(), GetSourceURL().c_str());
                    mReqState = kHnSessionStarted;
                    mState = kDvrStateStreaming;
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "Creating  TSB Streamer source failed \n");
                DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_OutOfMemory);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Ignoring TSB started event in wrong state <SrcState:%d TsbState:%d>\n", mState, mTsbState);
        }
        break;

    case kDvrPSIUpdateEvent:
        LOG(DLOGL_NOISE, "kDvrPSIUpdateEvent  mstate: %d", mState);
        /*
        * kDvrStateStreaming state indicates that streaming has started and is used here
        */
        if (mState == kDvrStateStreaming)
        {
            /*
            * Since the session has started streaming,Content_not_found is sent as status,as the retry signal should be triggered after teardown.
            * This signal+status code specifies that the content being streamed with PID specified is no longer valid.
            */
            LOG(DLOGL_NORMAL, "Notify the client to Retry and establish a new session after teardown");
            DoCallback(kMediaPlayerSignal_ServiceRetry, kMediaPlayerStatus_ContentNotFound);
            StopStreaming();
        }
        break;

    case kDvrEventTuningUpdate:
        //updates from PPV or SDV about tuning information change.
        //Need to stop and start the session again.. //to tune to the new frequency
        /*
        * kTsbStarted state indicates that streaming has started and is used here
        * since kDvrStateRendering state is never set as we dont recieve any first frame alarm
        */
        if ((mState == kDvrStateRendering) || (mState == kDvrWaitSourceReady) || (mState == kDvrSourceReady) || (mTsbState == kTsbStarted))
        {
            //cpe_hnsrvmgr_stop();
            //send a retry message for the client to establish a new connection
            LOG(DLOGL_NORMAL, "Notify the client to Retry and establish a new session");
            DoCallback(kMediaPlayerSignal_ServiceRetry, kMediaPlayerStatus_Ok);
            /*tearDownToRetune();
            mState = kDvrStateStop;
            LOG(DLOGL_REALLY_NOISY, "Set kDvrLiveSrc");
            mCurrentSourceType = kDvrLiveSrc;
            queueEvent(kDvrEventPlay);*/
        }
        break;

    case kDvrPpvInstStart:
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "Entering interstitial video start case, mState %d, mCurrentSourceType %d \n", mState, mCurrentSourceType);
        DoCallback(kMediaPlayerSignal_BeginningOfInterstitial, kMediaPlayerStatus_Ok);
        // We need to tune to other frequency

        mState = kDvrStateStop;
        mCurrentSourceType = kDvrLiveSrc;
        queueEvent(kDvrEventPlay);  //we might need to send Presentation Started signal to ANT,after this
        break;

    case kDvrPpvInstStop:
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "%s:%d Entering interstitial video stop case %d\n", __FUNCTION__, __LINE__, mState);
        DoCallback(kMediaPlayerSignal_EndOfInterstitial, kMediaPlayerStatus_Ok);
        tearDownClientSession();

        if (mPtrLiveSource != NULL)
        {
            mPtrLiveSource->release();
        }
        mCurrentSourceType = kDvrLiveSrc;
        break;

    case kDvrPpvSubscnAuth:
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "mspppv:entering ppv subscription authorized case\n");
        DoCallback(kMediaPlayerSignal_PpvSubscriptionAuthorized, kMediaPlayerStatus_Ok); //TODO-How to trigger video,if its stopped before.Will PPV manager will send the signal to start video?
        break;

    case kDvrPpvSubscnExprd: //BIG DOUBT 2.Incase of authorization expiration,will PPV direct us to stop video by posting StopVideo event???.Currently,I'm stopping anyway.TODO- confirm this
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "mspppv:entering ppv subscription expired case\n");
        if ((mState == kDvrStateRendering) || (mState == kDvrWaitSourceReady) || (mState == kDvrSourceReady))
        {
            tearDownToRetune();
            if (mPtrLiveSource != NULL)
            {
                mPtrLiveSource->release();
            }
            mState = kDvrStateStop;
        }
        DoCallback(kMediaPlayerSignal_PpvSubscriptionExpired, kMediaPlayerStatus_Ok);
        break;

    case kDvrPpvStartVideo:
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "mspppv:entering ppv start video case\n");
        mState = kDvrStateStop;
        mCurrentSourceType = kDvrLiveSrc;
        queueEvent(kDvrEventPlay);  //we might need to send Presentation Started signal to ANT,after this
        break;

    case kDvrPpvStopVideo:
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "%s:%d mspppv:entering ppv stop video case %d\n", __FUNCTION__, __LINE__, mState);
        // We need to tune to other frequency
        tearDownClientSession();
        if (mPtrLiveSource != NULL)
        {
            mPtrLiveSource->release();
        }
        mCurrentSourceType = kDvrLiveSrc;
        break;

    case kDvrPpvContentNotFound:
        DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
        break;

    case kDvrTunerLost:
        LOG(DLOGL_NOISE, "SID:%d tuner lost callback. state is %d", mSessionId, mState);
        DoCallback(kMediaPlayerSignal_ResourceLost, kMediaPlayerStatus_Ok);
        tearDownClientSession();//Cleans up the session if the tuner lost event is obtained for the current session
        break;

    case kDvrTunerRestored:
        LOG(DLOGL_NOISE, "SID:%d Tuner restored event -- queue play event", mSessionId);
        DoCallback(kMediaPlayerSignal_ResourceRestored, kMediaPlayerStatus_Ok);
        queueEvent(kDvrEventPlay);  // re-queue play event now that we have a tuner
        break;

    case kDvrEventServiceAuthorized:
        // Moving the log level to ERROR intentionally to track TSB error scenario trick mode issues.
        LOG(DLOGL_NOISE, "SID:%d Service authorized by CAM", mSessionId);
        mReqState = kHnSessionAuth;
        if ((NULL != mPtrRecSession) && (false == IsInMemoryStreaming()))
        {
            status =  mPtrRecSession->tsbPauseResume(false);
            if (kMspStatus_Ok != status)
            {
                LOG(DLOGL_ERROR, "SID:%d TSB Resume returned error %d", mSessionId, status);
            }

        }

        DoCallback(kMediaPlayerSignal_ServiceAuthorized, kMediaPlayerStatus_Ok);
        if (mTsbState == kTsbStarted)
        {
            LOG(DLOGL_NOISE, "SID:%d Authorized event recieved in the middle of streaming", mSessionId);
        }
        else
        {
            LOG(DLOGL_NOISE, "SID:%d Send a retry signal once recording started callback arrives", mSessionId);
        }
        break;


    case kDvrEventServiceDeAuthorized:
        //Notify only if the sessions has not yet started
        if (kHnSessionIdle == mReqState)
        {
            LOG(DLOGL_ERROR, "SID:%d Service was found DeAuthorized before streaming.. notify the platform of the error", mSessionId);
            NotifyServeFailure();
        }

        if ((NULL != mPtrRecSession) && (false == IsInMemoryStreaming()))
        {
            status = mPtrRecSession->tsbPauseResume(true);
            if (kMspStatus_Ok != status)
            {
                LOG(DLOGL_ERROR, "SID:%d TSB Pause returned error %d", mSessionId, status);
            }
        }
        // Moving the log level to ERROR intentionally to track TSB error scenario trick mode issues.
        LOG(DLOGL_ERROR, "SID:%d Service DeAuthorized by CAM", mSessionId);
        mReqState = kHnSessionDeAuth;
        DoCallback(kMediaPlayerSignal_ServiceDeauthorized, kMediaPlayerStatus_Ok);
        break;

    case kDvrEventSDVLoading:
        if (mState != kDvrStateStop)
        {
            DoCallback(kMediaPlayerSignal_ServiceLoading , kMediaPlayerStatus_Loading);
        }
        break;

    case kDvrEventSDVUnavailable:
        if (mState != kDvrStateStop)
        {
            LOG(DLOGL_ERROR, "SID:%d SDV Bandwidth not available", mSessionId);
            DoCallback(kMediaPlayerSignal_ServiceNotAvailableDueToSdv , kMediaPlayerStatus_ServiceNotAvailable);
        }
        break;

    case kDvrEventSDVCancelled:
        if (mState != kDvrStateStop)
        {
            LOG(DLOGL_ERROR, "SID:%d SDV Bandwidth not available", mSessionId);
            DoCallback(kMediaPlayerSignal_ServiceNotAvailableDueToSdv , kMediaPlayerStatus_Ok);
        }
        break;

    case kDvrEventSDVKeepAliveNeeded:
        if (mState != kDvrStateStop)
        {
            LOG(DLOGL_ERROR, "SID:%d SDV Network Bandwidth Reclamation Warning", mSessionId);
            DoCallback(kMediaPlayerSignal_NetworkResourceReclamationWarning , kMediaPlayerStatus_Ok);
        }
        break;

    case kDvrEventSDVLoaded:
        if (mState != kDvrStateStop)
        {
            DoCallback(kMediaPlayerSignal_ServiceLoaded , kMediaPlayerStatus_Ok);
            queueEvent(kDvrEventPlay);
        }
        break;

    case kDvrAppDataReady:
        DoCallback(kMediaPlayerSignal_ApplicationData, kMediaPlayerStatus_Ok);
        LOG(DLOGL_ERROR, "Callback ApplicationData kMediaPlayerStatus_Ok");
        break;

    case kDvrEventTunerUnlocked:
        //Notify only if the sessions has not yet started
        if (kHnSessionIdle == mReqState)
        {
            LOG(DLOGL_ERROR, "SID:%d Got Tuner Unlocked event before session start notifying serve failure", mSessionId);
            NotifyServeFailure();
        }

        if ((NULL != mPtrRecSession) && (false == IsInMemoryStreaming()))
        {
            status = mPtrRecSession->tsbPauseResume(true);
            if (kMspStatus_Ok != status)
            {
                LOG(DLOGL_ERROR, "SID:%d TSB Pause returned error %d", mSessionId, status);
            }
        }

        // Moving the log level to ERROR intentionally to track TSB error scenario trick mode issues.
        LOG(DLOGL_ERROR, "SID:%d Tuner Unlocked", mSessionId);
        mReqState = kHnSessionUnlocked;
        DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ErrorRecoverable);
        break;

    case kDvrEventAudioLangChangeCb:
    case kDvrEventBOF:
    case kDvrEventEOF:
    case kDvrRecordingAborted:
    case kDvrRecordingDiskFull:
    case kDvrFirstFrameEvent:
    case kDvrRecordingStoppedEvent:
    case kDvrNextFilePlay:
    case kDvrSchedRecEvent:
    case kDvrEventPlayBkCallback:
    case kDvrLiveFwd:
    case kDvrEventStop:
    case kDvrDwellTimeExpiredEvent:
    default:
        LOG(DLOGL_ERROR, "Not a valid callback event %d", aEvt->eventType);
        break;
    }
    return false;
}


///   This method notifies the media player status to all the registered clients
eIMediaPlayerStatus MrdvrTsbStreamer::DoCallback(eIMediaPlayerSignal sig, eIMediaPlayerStatus stat)
{
    FNLOG(DL_MSP_MRDVR);
    tIMediaPlayerCallbackData cbData;

    cbData.data[0] = '\0';

    LOG(DLOGL_NOISE, "signal %d status %d callback size=%d ", sig, stat, mCallbackList.size());

    cbData.status = stat;
    cbData.signalType = sig;
    std::list<CallbackInfo *>::iterator iter;
    for (iter = mCallbackList.begin(); iter != mCallbackList.end(); iter++)
    {
        if (*iter)
            if ((*iter)->mCallback != NULL)
            {
                (*iter)->mCallback((*iter)->mpSession, cbData, (*iter)->mClientContext, NULL);
            }
    }

    return  kMediaPlayerStatus_Ok;
}

///   This method locks the mutex
void MrdvrTsbStreamer::lockMutex(void)
{
    pthread_mutex_lock(&mMutex);
}

///   This method unlocks the mutex
void MrdvrTsbStreamer::unLockMutex(void)
{
    pthread_mutex_unlock(&mMutex);
}

///   This method returns the source URL of the service being streamed
std::string MrdvrTsbStreamer::GetSourceURL(bool liveSrcOnly)const
{
    (void) liveSrcOnly;

    std::string srcURL;

    if (mPtrLiveSource)
    {
        srcURL = mPtrLiveSource->getSourceUrl();
    }

    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "MrdvrTsbStreamer::GetSourceURL getSourceUrl:%s\n", srcURL.c_str());

    return srcURL;
}

///   This method return the destionation URL and is always ""
std::string MrdvrTsbStreamer::GetDestURL()const
{
    return mDestUrl;
}

/// This method always returns false
bool MrdvrTsbStreamer::isRecordingPlayback() const
{
    return false;
}

/// This method always should return true
bool MrdvrTsbStreamer::isLiveSourceUsed() const
{
    return (mPtrLiveSource != NULL);
}

/// This method always returns false
bool MrdvrTsbStreamer::isLiveRecording() const
{
    return false;
}

/// returns the current state
eDvrState MrdvrTsbStreamer::getDvrState(void)
{
    return mState;
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::PersistentRecord(const char* recordUrl,
        float nptRecordStartTime,
        float nptRecordStopTime,
        const MultiMediaEvent **pMme)
{
    UNUSED_PARAM(recordUrl)
    UNUSED_PARAM(nptRecordStartTime)
    UNUSED_PARAM(nptRecordStopTime)
    UNUSED_PARAM(pMme)
    FNLOG(DL_MSP_MRDVR);

    return kMediaPlayerStatus_Error_NotSupported;
}

bool MrdvrTsbStreamer:: isBackground(void)
{
    return  true;
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::SetSpeed(int numerator, unsigned int denominator)
{
    UNUSED_PARAM(numerator)
    UNUSED_PARAM(denominator)
    FNLOG(DL_MSP_MRDVR);

    return kMediaPlayerStatus_Error_NotSupported;
}


/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::GetSpeed(int* pNumerator, unsigned int* pDenominator)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(pNumerator)
    UNUSED_PARAM(pDenominator)
    return kMediaPlayerStatus_Error_NotSupported;
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::SetPosition(float nptTime)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(nptTime)
    return kMediaPlayerStatus_Error_NotSupported;
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::GetPosition(float* pNptTime)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(pNptTime)
    return kMediaPlayerStatus_Error_NotSupported;
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::IpBwGauge(const char *sTryServiceUrl, unsigned int *pMaxBwProvision, unsigned int *pTryServiceBw, unsigned int *pTotalBwCoynsumption)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(sTryServiceUrl)
    UNUSED_PARAM(pMaxBwProvision)
    UNUSED_PARAM(pTryServiceBw)
    UNUSED_PARAM(pTotalBwCoynsumption)
    return  kMediaPlayerStatus_Ok;
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::SetPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(vidScreenRect)
    UNUSED_PARAM(enablePictureModeSetting)
    UNUSED_PARAM(enableAudioFocus)
    return kMediaPlayerStatus_Error_NotSupported;
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::GetStartPosition(float* pNptTime)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(pNptTime)
    return kMediaPlayerStatus_Error_NotSupported;
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::GetEndPosition(float* pNptTime)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(pNptTime)
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrTsbStreamer::RegisterCCICallback(void* data, CCIcallback_t cb)
{
    FNLOG(DL_MSP_MRDVR);
    if (mPtrRecSession)
    {
        mPtrRecSession->SetCCICallback(data, cb);
    }
    mCBData = data;
    mCCICBFn = cb;
    return kMediaPlayerStatus_Ok;
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer:: UnRegisterCCICallback()
{
    FNLOG(DL_MSP_MRDVR);
    if (mPtrRecSession)
    {
        mPtrRecSession->UnSetCCICallback();
    }
    mCBData = NULL;
    mCCICBFn = NULL;
    return kMediaPlayerStatus_Ok;
}


/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::SetApplicationDataPid(uint32_t aPid)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(aPid)
    return kMediaPlayerStatus_Error_NotSupported;
}

/// This method registers for section filter data from pid 0x1FEE for sdv streaming sessions for mini carousel data
eIMediaPlayerStatus MrdvrTsbStreamer::SetApplicationDataPidExt(IMediaPlayerClientSession *ApplnClient)
{
    FNLOG(DL_MSP_MPLAYER);

    //If ApplnClient is Null, return from here, no use of going ahead
    if ((kDvrStateStop != mState) && (NULL != ApplnClient))
    {
        if (!mPtrLiveSource)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "mPtrLiveSource = NULL");
            return kMediaPlayerStatus_Error_Unknown;
        }

        tCpeSrcHandle srcHandle = NULL;
        srcHandle = mPtrLiveSource->getCpeSrcHandle();
        if (srcHandle == NULL)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "srcHandle = NULL  getCpeSrcHandle Error");
            return kMediaPlayerStatus_Error_Unknown;
        }

        eMspStatus status = ApplnClient->filterAppDataPidExt(srcHandle);
        if (status == kMspStatus_Ok)
        {
            return kMediaPlayerStatus_Ok;
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Error SetApplicationDataPidExt !!!!!!!!!!\n");
            return kMediaPlayerStatus_Error_NotSupported;
        }
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: State = %d, ApplnClient = %p", __FUNCTION__, mState, ApplnClient);
        return kMediaPlayerStatus_Error_Unknown;
    }
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(info)
    UNUSED_PARAM(infoSize)
    UNUSED_PARAM(count)
    UNUSED_PARAM(offset)
    return kMediaPlayerStatus_Error_NotSupported;
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::GetApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(bufferSize)
    UNUSED_PARAM(buffer)
    UNUSED_PARAM(dataSize)
    return kMediaPlayerStatus_Error_NotSupported;
}

/// This method gets the mini carausel section filter data for sdv streaming sessions
eIMediaPlayerStatus MrdvrTsbStreamer::GetApplicationDataExt(IMediaPlayerClientSession *ApplnClient, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    FNLOG(DL_MSP_MPLAYER);
    //If ApplnClient is Null, return from here, no use of going ahead
    if (NULL == ApplnClient)
    {
        LOG(DLOGL_NOISE, "%s: ApplnClient Pointer is NULL !!!!", __FUNCTION__);
        return kMediaPlayerStatus_Error_Unknown;
    }

    eMspStatus status = ApplnClient->getApplicationDataExt(bufferSize, buffer, dataSize);
    if (status == kMspStatus_Ok)
    {
        return kMediaPlayerStatus_Ok;
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Error GetApplicationDataPidExt !!!!!!!!!!\n");
        return kMediaPlayerStatus_Error_NotSupported;
    }
}

uint32_t MrdvrTsbStreamer::GetSDVClentContext(IMediaPlayerClientSession *ApplnClient)
{
    FNLOG(DL_MSP_MPLAYER);
    if (NULL != ApplnClient)
        return ApplnClient->getSDVClentContext();
    else
        return 0;
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::SetAudioPid(uint32_t aPid)
{
    UNUSED_PARAM(aPid)
    return kMediaPlayerStatus_Error_NotSupported;
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
void MrdvrTsbStreamer::appDataReadyCallbackFn(void *aClientContext)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(aClientContext)
    return;
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::CloseDisplaySession()
{
    FNLOG(DL_MSP_MRDVR);
    return kMediaPlayerStatus_Error_NotSupported;
}

/// This method always returns kMediaPlayerStatus_Error_NotSupported
eIMediaPlayerStatus MrdvrTsbStreamer::StartDisplaySession()
{
    FNLOG(DL_MSP_MRDVR);
    return kMediaPlayerStatus_Error_NotSupported;
}

/// This method always returns kCsciMspDiagStat_NoData
eCsciMspDiagStatus MrdvrTsbStreamer::GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(msgInfo)
    return kCsciMspDiagStat_NoData;;
}

/// This method maintains the sdv sessions for which section filter for mc will be triggered.
void MrdvrTsbStreamer::addClientSession(IMediaPlayerClientSession *pClientSession)
{
    FNLOG(DL_MSP_MPLAYER);
    mAppClientsList.push_back(pClientSession);
}
// This method removes the particular sdv session from the list
void MrdvrTsbStreamer::deleteClientSession(IMediaPlayerClientSession *pClientSession)
{
    FNLOG(DL_MSP_MPLAYER);
    std::list<IMediaPlayerClientSession*>::iterator iter;
    for (iter = mAppClientsList.begin(); iter != mAppClientsList.end(); iter++)
    {
        if ((*iter) == (pClientSession))
        {
            mAppClientsList.remove(*iter);
            delete(*iter);
            (*iter) = NULL;
            pClientSession = NULL;
            break;
        }
    }
}

/// This method removes all the sdv sessions from the list
void MrdvrTsbStreamer::deleteAllClientSession()
{
    FNLOG(DL_MSP_MPLAYER);
    std::list<IMediaPlayerClientSession*>::iterator iter;
    for (iter = mAppClientsList.begin(); iter != mAppClientsList.end(); iter++)
    {
        (*iter)->mPid = INVALID_PID_VALUE;
        SetApplicationDataPidExt(*iter);
        (*iter)->mGuard_word = 0;
        delete(*iter);
        (*iter) = NULL;
    }
    mAppClientsList.clear();
}

/// This method always simply returns
void MrdvrTsbStreamer::StopAudio(void)
{
    FNLOG(DL_MSP_MRDVR);
    return;
}

/// This method always simply returns
void MrdvrTsbStreamer::RestartAudio(void)
{
    FNLOG(DL_MSP_MRDVR);
    return;
}

void MrdvrTsbStreamer::ValidateSessionState()
{
    FNLOG(DL_MSP_MRDVR);
    if (mReqState == kHnSessionIdle)
    {
        dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "New request... Proceed with normal sequence");
    }
    else if ((mReqState == kHnSessionAuth || mReqState == kHnSessionLocked) && mTsbState == kTsbCreated)
    {
        dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "Queuing event to start TSB streaming and change the HN state, as TSB is already created");
        mReqState = kHnSessionTsbCreated;
        queueEvent(kDvrTSBStartEvent);
    }
    else if ((mReqState == kHnSessionTimedOut || mReqState == kHnSessionPsiTimeOut) && mTsbState == kTsbCreated)
    {
        dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "Queuing event to start TSB streaming and change the HN state, since the session timed out while trying to stream from TSB");
        mReqState = kHnSessionTsbCreated;
        queueEvent(kDvrTSBStartEvent);
    }
    else
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "Session is not idle (or) in a recoverable state. Cannot stream now");
    }
}
void MrdvrTsbStreamer::StartTunerStreaming()
{
    if (mPtrLiveSource)
    {
        mPtrHnOnDemandStreamerSource = new MSPHnOnDemandStreamerSource(mPtrLiveSource->getSourceUrl() , mPtrLiveSource->getCpeSrcHandle());
        if (mPtrHnOnDemandStreamerSource)
        {
            LOG(DLOGL_REALLY_NOISY, "Created HnOnDemandStreamer source...!");
            // Set the CPERP streaming session id with HnOnDemandStreamer source
            mPtrHnOnDemandStreamerSource->SetCpeStreamingSessionID(mSessionId);

            // Loading the HnOnDemandStreamer source
            eMspStatus status = mPtrHnOnDemandStreamerSource->load(sourceCB, (void*) this);
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "SID:%d HnOnDemandStreamer source load failed...!", mSessionId);
            }
            else
            {
                LOG(DLOGL_MINOR_DEBUG, "SID:%d Successfully loaded the HnOnDemandStreamer source", mSessionId);
            }

            // Opening the HnOnDemandStreamer source
            if (status == kMspStatus_Ok)
            {
                LOG(DLOGL_MINOR_DEBUG, "SID:%d Opening the HnOnDemandStreamer source", mSessionId);
                status = mPtrHnOnDemandStreamerSource->open(kRMPriorityIPClient_VideoWithAudioFocus);
                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "SID:%d HnOnDemandStreamer source open failed...!", mSessionId);
                }
                else
                {
                    LOG(DLOGL_MINOR_DEBUG, "SID:%d Successfully opened the HnOnDemandStreamer source", mSessionId);
                }
            }


            //Set the PID table which also should automatically inject PAT/PMT in the HnOnDemand stream source to the IP Client
            status = mPtrHnOnDemandStreamerSource->setStreamInfo(mPtrPsi);
            if (status != kMspStatus_Ok)
            {

                LOG(DLOGL_ERROR, "SID:%d HnOnDemandStreamer Set Stream Info failed...!", mSessionId);
                return;
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "SID:%d HnOnDemandStreamer Injected PAT/PMT to the stream...!", mSessionId);
                LOG(DLOGL_REALLY_NOISY, "SID:%d HnOnDemandStreamer SetPatPMT is about to be called", mSessionId);
                mPtrHnOnDemandStreamerSource->SetPatPMT(mPtrPsi);
                tCpePgrmHandle m_pPgmHandle   =  mPtrHnOnDemandStreamerSource->getPgrmHandle();
                if (m_pPgmHandle != NULL)
                {

                    mptrcaStream = new InMemoryStream;
                    if (NULL == mptrcaStream)
                    {

                        LOG(DLOGL_ERROR, "SID:%d mptrcaStream is NULL because , failed to allocte memory", mSessionId);
                    }

                    else
                    {

                        LOG(DLOGL_REALLY_NOISY, " %s:SID:%d mptrcaStream is instantied successfully, CCI callback is about to be called", __func__, mSessionId);

                        mregid = mptrcaStream->RegisterCCICallback(mCBData, mCCICBFn);
                        LOG(DLOGL_REALLY_NOISY, " %s:SID:%d registerEntitlementUpdate is called to regsiter CAM authorization changes", __func__, mSessionId);
                        mEntitleid = mptrcaStream->registerEntitlementUpdate((void *)this, ((EntitlementCallback)InMemoryStreamingEntitlemntCallback));
                        LOG(DLOGL_REALLY_NOISY, "SID:%d HnOnDemandStreamer calls InMemoryStream::startDeScrambling  with source id %d and Entitlement Id %d", mSessionId, mPtrLiveSource->getSourceId(), mEntitleid);
                        mptrcaStream->DeScrambleSource(mPtrLiveSource->getSourceId(), m_pPgmHandle, false, &mCamCaHandle);
                        LOG(DLOGL_REALLY_NOISY, "SID:%d cam handle returned is %p", mSessionId, mCamCaHandle);
                        mPtrHnOnDemandStreamerSource->InjectCCI(m_CCIbyte);
                    }
                    status = mPtrHnOnDemandStreamerSource->start();
                    if (status != kMspStatus_Ok)
                    {
                        LOG(DLOGL_ERROR, "SID:%d HnOnDemandStreamer source start failed...!", mSessionId);
                        if (mptrcaStream)
                        {
                            LOG(DLOGL_NORMAL, "%s:SID%d Hnsrvmgr start fails ,so cleaning up the session", __func__, mSessionId);
                            mptrcaStream->unRegisterCCICallback(mregid);
                            mptrcaStream->shutdown();
                            delete mptrcaStream;
                            mptrcaStream = NULL;

                        }
                    }

                }
            }

            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_EMERGENCY, "SID:%d After doing everything failed in the last step. So sad...!CleanUp the session", mSessionId);
                if (mPtrHnOnDemandStreamerSource != NULL)
                {
                    if (mPtrHnOnDemandStreamerSource->stop() != kMspStatus_Ok)
                    {
                        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s:%d SID:%d TSB Source live streaming Stop failed", __FUNCTION__, __LINE__, mSessionId);
                    }

                    delete mPtrHnOnDemandStreamerSource;
                    mPtrHnOnDemandStreamerSource = NULL;
                }
            }
            else
            {
                LOG(DLOGL_EMERGENCY, "SID:%d NICE to see that everything went well and streaming started...!", mSessionId);
                mReqState = kHnSessionStarted;
                mState = kDvrStateStreaming;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "SID:%d Creating  HnOnDemandStreamer source failed...!", mSessionId);
        }
    }
}

void MrdvrTsbStreamer::InjectCCI(uint8_t CCIbyte)
{
    m_CCIbyte = CCIbyte;
    if (mPtrTsbStreamerSource != NULL)
    {
        if (mPtrTsbStreamerSource->InjectCCI(m_CCIbyte) != kMspStatus_Ok)
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "SID:%d Not able to set CCI into the stream", mSessionId);
        }
        else
        {
            dlog(DL_MSP_MRDVR, DLOGL_NORMAL, "SID:%d set CCI bits into the stream successfully", mSessionId);
        }
    }
    else if (mPtrHnOnDemandStreamerSource != NULL)
    {
        mPtrHnOnDemandStreamerSource->InjectCCI(m_CCIbyte);
    }
    else
    {
        dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "SID:%d NULL source.. The source is not available now.. CCI Will be injected once the source is created..", mSessionId);
    }

}
void MrdvrTsbStreamer::NotifyServeFailure()
{
    if (cpe_hnsrvmgr_NotifyServeFailure(mSessionId, eCpeHnSrvMgrMediaServeStatus_TuneFailed) != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "Failed to notify platform of serve failure");
    }
    else
    {
        LOG(DLOGL_NORMAL, "Notified successfully");
    }
}

/// This method Flushes the pending events from Event thread Queue and stops streaming
eIMediaPlayerStatus MrdvrTsbStreamer::StopStreaming()
{
    FNLOG(DL_MSP_MRDVR);
    eIMediaPlayerStatus playerStatus = kMediaPlayerStatus_Ok;
    eMspStatus sourceStatus = kMspStatus_Ok;
    LOG(DLOGL_REALLY_NOISY, "Moving to Suspended State to avoid processing further events");
    mState = kDvrStateSuspended;
    LOG(DLOGL_REALLY_NOISY, "Flushing the event queue of any pending events and stop streaming if in progress");
    if (mThreadEventQueue)
    {
        mThreadEventQueue->flushQueue(); //flush out any pending events posted
        if (mPtrTsbStreamerSource != NULL)
        {
            sourceStatus = mPtrTsbStreamerSource->release();
            if (sourceStatus != kMspStatus_Ok)
            {
                dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s:TSB Streamer Source release failed\n", __FUNCTION__);
                playerStatus = kMediaPlayerStatus_Error_OutOfState;
            }
        }
        else if (mPtrHnOnDemandStreamerSource != NULL)
        {
            sourceStatus = mPtrHnOnDemandStreamerSource->release();
            if (sourceStatus != kMspStatus_Ok)
            {
                dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s:In Memory Streamer Source release failed\n", __FUNCTION__);
                playerStatus = kMediaPlayerStatus_Error_OutOfState;
            }
        }
        else
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s:Null Streamer Source\n", __FUNCTION__);
            playerStatus = kMediaPlayerStatus_Error_OutOfState;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Null Event thread Queue ");
        playerStatus = kMediaPlayerStatus_Error_OutOfState;
    }
    return playerStatus;
}

// Responsible for starting the EAS audio playback
// This API is applicable for the controllers associated with EAS audio playback session
// For the controllers associated with non EAS audio playback sessions simply returned
// Since MrdvrTsbStreamer is not the controller associated with EAS audio playback session,
// API is returning here without any action performed
void MrdvrTsbStreamer::startEasAudio(void)
{
    dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(), is not responsible for controlling EAS", __FUNCTION__);
    return;
}

// Sets the EAS audio playback active status
// True  - EAS audi playback is in progress
// False - EAS audio playback is not in progress
void MrdvrTsbStreamer::SetEasAudioActive(bool active)
{
    mEasAudioActive = active;
}

void MrdvrTsbStreamer::StartPSITimeout()
{
    // Start Timer for monitoring PSI data

    FNLOG(DL_MSP_MRDVR);

    int threadRetvalue;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    //Make the thread as join-able
    pthread_attr_setstacksize(&attr, (128 * 1024)); //128kb

    threadRetvalue = pthread_create(&mPsiTimeoutThread, &attr, PsiTimerFunc, (void *) this);
    if (threadRetvalue)
    {
        LOG(DLOGL_ERROR, "Error creating PSI Timeout thread: %d", threadRetvalue);
        threadRetvalue = pthread_attr_destroy(&attr);
        if (threadRetvalue)
        {
            LOG(DLOGL_ERROR, "Error in pthread_attr_destroy for PSI Timeout thread: %d", threadRetvalue);
        }
        return;
    }

    threadRetvalue = pthread_setname_np(mPsiTimeoutThread, "PSI Timeout thread");
    if (threadRetvalue)
    {
        LOG(DLOGL_ERROR, "Error PSI timeout thread setname: %d", threadRetvalue);
    }

    threadRetvalue = pthread_attr_destroy(&attr);
    if (threadRetvalue)
    {
        LOG(DLOGL_ERROR, "Error in pthread_attr_destroy for PSI Timoueout Thread: %d", threadRetvalue);
    }
}


// This method starts a timer to post a DVR timeout event when PSI timeout happens or exit when PSI data is available
void * MrdvrTsbStreamer::PsiTimerFunc(void *aData)
{
    FNLOG(DL_MSP_MRDVR);

    bool done = false;
    MrdvrTsbStreamer *inst = (MrdvrTsbStreamer *)aData;

    struct timeval tv;
    long secs_start, secs_stop;


    if (gettimeofday(&tv, 0) < 0)
    {
        LOG(DLOGL_ERROR, "Error gettimeofday");
    }

    secs_start = tv.tv_sec;

    while (!done)
    {
        if (gettimeofday(&tv, 0) < 0)
        {
            LOG(DLOGL_ERROR, "Error gettimeofday");
        }

        secs_stop = tv.tv_sec;
        if (secs_stop > (secs_start + PSI_TIMEOUT))
        {
            LOG(DLOGL_REALLY_NOISY, "PSI timer expired. Post DVR time out event");
            inst->queueEvent(kDvrTimeOutEvent);
            done = true;
        }

        if (inst->mPsiReady == true || inst->mState == kDvrStateStop)
        {
            LOG(DLOGL_REALLY_NOISY, "Exit PSI timeout thread as PSI data is available [or] Dvr is in stopped state");
            done = true;
        }
        usleep(100000);      // give other threads a chance to run
    }

    LOG(DLOGL_REALLY_NOISY, "PSI Timer Thread Exit");
    pthread_exit(NULL);
    return NULL;
}

