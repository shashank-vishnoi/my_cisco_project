/**
   \file dvr.cpp
   \class dvr

    Implementation file for dvr media controller class
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
#include "dvr.h"
#include "DisplaySession.h"
#include "RecordSession.h"
#include "psi.h"
#include "AnalogPsi.h"
#include "eventQueue.h"

#include "IMediaPlayer.h"
#include "TsbHandler.h"
#include "MSPSourceFactory.h"
#include "MSPPPVSource.h"
#include "pthread_named.h"

#include "csci-dvr-scheduler-api.h"


#include "MSPScopedPerfCheck.h"

//#include <cpeutil_hnplatlive.h>
#include "CurrentVideoMgr.h"

#include "mrdvrserver.h"
#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_DVR, level,"dvr:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

#define TIMEOUT 5
#define MAX_FILE_SIZE 20
const int MAX_FILTERING_PACKETS = 20;

//interface for getting TSB file name for platform.cpp
extern int get_tsb_file(char *tsb_file, unsigned int session_number);
static struct timeval tv_start, tv_stop;

///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

#define UNUSED_PARAM(a) (void)a;
static volatile bool tunerLocked = false;       // DEBUG ONLY

static pthread_mutex_t  sListMutex = PTHREAD_MUTEX_INITIALIZER;
///////////////////////////////////////////////////////////////////////////
//                      Member implementation
///////////////////////////////////////////////////////////////////////////
#define __PERF_DEBUG__

#if defined(__PERF_DEBUG__)
struct timeval start_clk, end_clk, res_clk;
int PerstRecord;
#endif

bool Dvr::mEasAudioActive = false;

/** *********************************************************
*/
void* Dvr::eventthreadFunc(void *aData)
{
    bool done = false;
    Dvr *inst = (Dvr *)aData;
    Event *evt;

    FNLOG(DL_MSP_DVR);

    while (!done)
    {
        unsigned int waitTime = 0;
        if (inst->mState == kDvrWaitSourceReady)
        {
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Inside Wait for Tuner lock\n");
            waitTime = TIMEOUT;
        }
        inst->mThreadEventQueue->setTimeOutSecs(waitTime);
        evt = inst->mThreadEventQueue->popEventQueue();
        inst->mThreadEventQueue->unSetTimeOut();
        inst->lockMutex();
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Event thread got event %d ", evt->eventType);
        done = inst->dispatchEvent(evt);  // call member function to handle event
        inst->mThreadEventQueue->freeEvent(evt);
        inst->unLockMutex();
    }

    dlog(DL_MSP_DVR, DLOGL_NOISE, "Exiting Dvr event thread\n");
    pthread_exit(NULL);
    return NULL;
}

void Dvr::StartDwellTimerForTsb()
{
    // Start Dwell Timer for TSB creation is allowed

    FNLOG(DL_MSP_DVR);

    if (!mPtrLiveSource)
    {
        LOG(DLOGL_ERROR, "Error null mPtrLiveSource");
        return;
    }

    if (!mPtrLiveSource->canRecord())
    {
        LOG(DLOGL_ERROR, "Warning: cannot record source");
        return;
    }

    if (mTsbState != kTsbNotCreated)
    {
        LOG(DLOGL_ERROR, "Error: TSB already created");
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "Calling AddLiveSession when TSB already created");
        AddLiveSession();
        return;
    }

    if (mCurrentSourceType == kDvrLiveSrc)
    {
        int threadRetvalue;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, (128 * 1024)); //128kb

        threadRetvalue = pthread_create(&mDwellTimerThread, &attr, dwellTimerFun, (void *) this);
        if (threadRetvalue)
        {
            LOG(DLOGL_ERROR, "Error dwellTimer thread: %d", threadRetvalue);
        }

        threadRetvalue = pthread_setname_np(mDwellTimerThread, "TSB Dwelltimer thread");
        if (threadRetvalue)
        {
            LOG(DLOGL_ERROR, "ErrorL Dwell timer thread setname: %d", threadRetvalue);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: source type not kDvrLiveSrc, is %d", mCurrentSourceType);
    }
}



/** *********************************************************
*/
bool Dvr::dispatchEvent(Event *aEvt)
{
    FNLOG(DL_MSP_DVR);
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
        LOG(DLOGL_NOISE, "Exit Dvr Ctrl Thread!");
        return true;

        // no break required here
    case kDvrTimeOutEvent:
        if (mPtrLiveSource)
        {
            if (mPtrLiveSource->isPPV())
            {
                LOG(DLOGL_NORMAL, "Timeout waiting for video on PPV channel.Skipping intimation of services layer");
            }
            else if (mPtrLiveSource->isSDV())
            {
                LOG(DLOGL_ERROR, "Warning: Tuner lock time Out for SDV Channel ");
                LOG(DLOGL_NORMAL, "Callback to MDA about SDV Service Not Available");
                DoCallback(kMediaPlayerSignal_ServiceNotAvailableDueToSdv, kMediaPlayerStatus_Ok);
            }
            else
            {
                if (mState == kDvrWaitSourceReady)
                {
                    LOG(DLOGL_EMERGENCY, "Warning: Tuner lock timeout.Signal strength may be low or no stream on tuned frequency");
                }
                else if (mState == kDvrSourceReady) // i.e. waiting for PSI to be ready
                {
                    LOG(DLOGL_ERROR, "Warning: PSI Data not ready");
                }
            }
        }
        else
        {
            LOG(DLOGL_NORMAL, "NULL Live source pointer on timeout event");
        }
        break;

    case kDvrEventStop:
        break;

    case kDvrAppDataReady:
        DoCallback(kMediaPlayerSignal_ApplicationData, kMediaPlayerStatus_Ok);
        LOG(DLOGL_MINOR_EVENT, "Callback ApplicationData kMediaPlayerStatus_Ok");
        break;

    case kDvrLiveFwd:
        DoCallback(kMediaPlayerSignal_EndOfStream, kMediaPlayerStatus_Ok);
        LOG(DLOGL_MINOR_EVENT, "Callback _EndOfStream kMediaPlayerStatus_Ok");
        break;

    case kDvrEventPlay:
        // TODO: This should go into its own function
        if ((mCurrentSourceType == kDvrLiveSrc) && (mPtrLiveSource))
        {
            eMspStatus status;
            status = mPtrLiveSource->open(mTunerPriority);
            if (status == kMspStatus_Loading)
            {
                dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "We are still waiting for tuning parameters, we will revisit Play again\n");
                mState = kDvrWaitTuningParams;
                break;
            }
            else if (status == kMspStatus_WaitForTuner)
            {
                dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Waiting for tuning acquisition");
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
                    DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_InvalidParameter);
                }
                LOG(DLOGL_MINOR_EVENT, "Callback Problem InvalidParameter");
            }
            else
            {
                dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Video available.going to tune for it\n");
                status = mPtrLiveSource->start();
                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "Source Start error %d", status);
                    DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
                    LOG(DLOGL_MINOR_EVENT, "Callback Problem ContentNotFound");

                }
                mState = kDvrWaitSourceReady;
            }
        }
        else if (mCurrentSourceType == kDvrFileSrc && mPtrFileSource)
        {
            // handle load for Dvr here
            // TODO: Need to fix priority.  Not needed for file source
            status = mPtrFileSource->open(kRMPriority_ForegroundRecording);
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "File source open error %d", status);
                DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
                LOG(DLOGL_MINOR_EVENT, "Callback Problem ContentNotFound");
            }
            else
            {
                queueEvent(kDvrEventPlayBkCallback);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Error: unknown source type %d", mCurrentSourceType);
        }
        break;

// get RF tuner locked callback, create display session and psi, start psi
    case kDvrEventRFCallback:
        if (mState != kDvrWaitSourceReady)
        {
            dlog(DL_MSP_DVR, DLOGL_NOISE, "Tuner lock callback in wrong state. State: %d\n", mState);
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
                dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "tuner callback arrived.Starting PSI!!!!!!!!");
                mPtrPsi->psiStart(mPtrLiveSource);
            }

        }
        break;

    case kDvrEventPlayBkCallback:
        mState = kDvrSourceReady;
        if (mPtrPsi == NULL)
        {
            mPtrPsi = new Psi();
        }
        mPtrPsi->registerPsiCallback(psiCallback, this);
        // get psi started then wait for PSI ready callback
        mPtrPsi->psiStart(mFileName);
        break;

    case kDvrPSIReadyEvent:
        if (mState == kDvrSourceReady)
        {
            SetDisplaySession();

            if (mPtrLiveSource)
            {
                if (mPtrLiveSource->isSDV())
                {
                    mPtrLiveSource->setMediaSessionInstance(mIMediaPlayerSession);
                }
            }

            if (mCurrentSourceType == kDvrFileSrc)
            {
                status = mPtrFileSource->start();
                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "File source start error %d", status);
                    DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
                }
                else
                {
                    LOG(DLOGL_REALLY_NOISY, "DVR Source Started Successfully");
                }
            }
            mPtrDispSession->start(mEasAudioActive);
        }
        else
        {
            LOG(DLOGL_ERROR, "Warning: not starting display, mState: %d", mState);
        }
        break;

        // get RF tuner locked callback, create display session and psi, start psi
    case kDvrEventRFAnalogCallback:
        if (mState != kDvrWaitSourceReady)
        {
            dlog(DL_MSP_DVR, DLOGL_NOISE, "Analog Tuner lock callback in wrong state. State: %d\n", mState);
        }
        else if (mPtrLiveSource)
        {
            mPtrAnalogPsi = new AnalogPsi();
            mState = kDvrSourceReady;
            status = mPtrAnalogPsi->psiStart(mPtrLiveSource);
            psiCallback(kPSIReady, (void *)this);
        }
        break;

    case kDvrNextFilePlay:
        dlog(DL_MSP_DVR, DLOGL_NOISE, "Next File Event has come");
        if (mCurrentSourceType == kDvrFileSrc && mPtrFileSource != NULL)
        {
            mPtrFileSource->stop();
        }
        if (mPtrDispSession)
        {
            mPtrDispSession->freeze();
            CloseDisplaySession();
        }

        StopDeletePSI();
        if (mCurrentSourceType == kDvrFileSrc && mPtrFileSource != NULL)
        {
            mState = kDvrStateStop;
            mFileName = mPtrFileSource->getFileName();
            queueEvent(kDvrEventPlay);
        }
        break;

    case kDvrSchedRecEvent:
        LOG(DLOGL_NORMAL, "kDvrSchedRecEvent StartRecording Setting tuner priority to kRMPriority_BackgroundRecording ");
        status = StartRecording();
        if (status != kMediaPlayerStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Background recording attempt failed");
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ServiceNotAvailable);
        }
        else
        {
            // callback to Service Layer
            DoCallback(kMediaPlayerSignal_PersistentRecordingStarted, kMediaPlayerStatus_Ok);
        }

        if (mPtrLiveSource)
        {
            if (mPtrLiveSource->isSDV())
            {
                mPtrLiveSource->setMediaSessionInstance(mIMediaPlayerSession);
            }
        }

        break;

    case kDvrDwellTimeExpiredEvent:
        LOG(DLOGL_MINOR_EVENT, "kDvrDwellTimeExpiredEvent");
        if ((mPtrPsi || mPtrAnalogPsi) && mPtrLiveSource &&
                mPtrLiveSource->canRecord() &&
                mState == kDvrStateRendering &&
                mTsbState == kTsbNotCreated)
        {

            LOG(DLOGL_REALLY_NOISY, "goto StartTSBRecordSession");
            status = StartTSBRecordSession("");
            if (status == kMspStatus_NotSupported)
            {
                LOG(DLOGL_ERROR, "Music channel with no video.Recording not supported");
            }
            else if (status != kMspStatus_Ok)
            {
                // TODO:  This needs a callback if not recording ??
                LOG(DLOGL_ERROR, "Error: TSB Creation Failed");
            }
            else
            {
                LOG(DLOGL_NOISE, "TSB creation on dwell time expiry successful");
                mTsbState = kTsbCreated;
                dlog(DL_MSP_MRDVR, DLOGL_NOISE, "Calling AddLiveSession on TSB created event");
                AddLiveSession();
            }
        }
        else
        {
            LOG(DLOGL_NOISE, "Warning: Not attempting to StartTSBRecordSession");
        }
        break;

    case kDvrTSBStartEvent:
        if (mState != kDvrStateStop)
        {
            LOG(DLOGL_NORMAL, "TSB started successfully");
            mTsbState = kTsbStarted;
            if (mPersistentRecordState == kPersistentRecordWaitingTSB)
            {
                status = StartPersistentRecording();
                if (status != kMspStatus_Ok)
                {
                    mPersistentRecordState = kPersistentRecordNotStarted;
                    LOG(DLOGL_ERROR, "Persistent Recording failed");
                    return kMediaPlayerStatus_Error_Unknown;
                }
                else
                {
                    mPersistentRecordState = kPersistentRecordStarted;
                }

#if defined(__PERF_DEBUG__)
                gettimeofday(&end_clk, NULL);
                res_clk.tv_sec  = end_clk.tv_sec - start_clk.tv_sec ;
                res_clk.tv_usec = end_clk.tv_usec - start_clk.tv_usec;
                while (res_clk.tv_usec < 0)
                {
                    res_clk.tv_usec += 1000000;
                    res_clk.tv_sec -= 1;
                }
                res_clk.tv_usec = res_clk.tv_usec / 1000;
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "\n#MSP_PROFILE:%s()kDvrTSBStartEvent:exec-time: %ld sec %ld msec \n", __FUNCTION__, res_clk.tv_sec, res_clk.tv_usec);
                //    PerstRecord = 0;
#endif
            }

            if (mPendingTrickPlay == true)
            {
                SetSpeed(mTrickNum, mTrickDen);
                mPendingTrickPlay = false;
            }
        }
        else
        {
            LOG(DLOGL_NORMAL, "TSB started event of previously tuned channel.Ignoring it.current state is %d", mState);
        }

        break;

    case kDvrRecordingStoppedEvent:
        LOG(DLOGL_NORMAL, " Persistent Recording stopped successfully");
        break;


    case kDvrPSIUpdateEvent:
        LOG(DLOGL_NORMAL, "kDvrPSIUpdateEvent, mstate: %d, mPersistentRecordState %d, mPtrRecSession %p, mTsbState %d",
            mState, mPersistentRecordState, mPtrRecSession, mTsbState);

        if (mState == kDvrStateRendering)
        {
            CloseDisplaySession();
            LOG(DLOGL_REALLY_NOISY, "Set kDvrLiveSrc");
            mCurrentSourceType = kDvrLiveSrc;
            SetDisplaySession();

            mPtrDispSession->start(mEasAudioActive);

            mState = kDvrSourceReady;
        }
#if 0
        if (mPtrRecSession && (mTsbState >= kTsbCreated ||
                               mPersistentRecordState == kPersistentRecordWaitingTSB ||
                               mPersistentRecordState == kPersistentRecordStarted))
        {
            LOG(DLOGL_NORMAL, "TSB or Recoding going on, need to stop and re-start it");
            enum eRecordSessionState recState;
            mPtrRecSession->stopForPmtUpdate(&recState);
            mPtrRecSession->restartForPmtUpdate(recState);
        }
#endif
        break;
    case kDvrPmtRevUpdateEvent:
        LOG(DLOGL_NORMAL, "kDvrPmtRevUpdateEvent mstate: %d", mState);
        if (mPtrDispSession && mCurrentSourceType == kDvrLiveSrc && mPtrPsi)
        {
            mPtrDispSession->PmtRevUpdated(mPtrPsi);
        }
        // No AV pid change; Find out if CSD (caption service descriptor changed)
        break;
    case kDvrEventTuningUpdate:
        //updates from PPV or SDV about tuning information change.
        //Need to stop and start the session again.. //to tune to the new frequency
        if ((mState == kDvrStateRendering) || (mState == kDvrWaitSourceReady) || (mState == kDvrSourceReady))
        {
            tearDownToRetune();
            mState = kDvrStateStop;
            LOG(DLOGL_REALLY_NOISY, "Set kDvrLiveSrc");
            mCurrentSourceType = kDvrLiveSrc;
            queueEvent(kDvrEventPlay);
        }
        break;

    case kDvrPpvInstStart:
        dlog(DL_MSP_DVR, DLOGL_NOISE, "Entering interstitial video start case, mState %d, mCurrentSourceType %d \n", mState, mCurrentSourceType);
        DoCallback(kMediaPlayerSignal_BeginningOfInterstitial, kMediaPlayerStatus_Ok);
        // We need to tune to other frequency
        tearDownToRetune();
        mState = kDvrStateStop;
        mCurrentSourceType = kDvrLiveSrc;
        queueEvent(kDvrEventPlay);  //we might need to send Presentation Started signal to ANT,after this
        break;

    case kDvrPpvInstStop:
        dlog(DL_MSP_DVR, DLOGL_NOISE, "%s:%d Entering interstitial video stop case %d\n", __FUNCTION__, __LINE__, mState);
        DoCallback(kMediaPlayerSignal_EndOfInterstitial, kMediaPlayerStatus_Ok);
        tearDownToRetune();
        if (mPtrLiveSource != NULL)
        {
            mPtrLiveSource->release();
        }
        mState = kDvrStateStop;
        mCurrentSourceType = kDvrLiveSrc;
        break;
    case kDvrPpvSubscnAuth:
        dlog(DL_MSP_DVR, DLOGL_NOISE, "mspppv:entering ppv subscription authorized case\n");
        DoCallback(kMediaPlayerSignal_PpvSubscriptionAuthorized, kMediaPlayerStatus_Ok); //TODO-How to trigger video,if its stopped before.Will PPV manager will send the signal to start video?
        break;

    case kDvrPpvSubscnExprd: //BIG DOUBT 2.Incase of authorization expiration,will PPV direct us to stop video by posting StopVideo event???.Currently,I'm stopping anyway.TODO- confirm this
        dlog(DL_MSP_DVR, DLOGL_NOISE, "mspppv:entering ppv subscription expired case\n");
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
        dlog(DL_MSP_DVR, DLOGL_NOISE, "mspppv:entering ppv start video case\n");
        tearDownToRetune();
        mState = kDvrStateStop;
        mCurrentSourceType = kDvrLiveSrc;
        queueEvent(kDvrEventPlay);  //we might need to send Presentation Started signal to ANT,after this
        break;

    case kDvrPpvStopVideo:
        dlog(DL_MSP_DVR, DLOGL_NOISE, "%s:%d mspppv:entering ppv stop video case %d\n", __FUNCTION__, __LINE__, mState);
        // We need to tune to other frequency
        tearDownToRetune();
        if (mPtrLiveSource != NULL)
        {
            mPtrLiveSource->release();
        }
        mState = kDvrStateStop;
        mCurrentSourceType = kDvrLiveSrc;
        break;

    case kDvrPpvContentNotFound:
        DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
        break;

    case kDvrTunerLost:
        LOG(DLOGL_NORMAL, "tuner lost callback. state is %d", mState);
        DoCallback(kMediaPlayerSignal_ResourceLost, kMediaPlayerStatus_Ok);
        tearDownToRetune();
        mState = kDvrStateStop;
        break;

    case kDvrTunerRestored:
        LOG(DLOGL_NOISE, "Tuner restored event -- queue play event");
        DoCallback(kMediaPlayerSignal_ResourceRestored, kMediaPlayerStatus_Ok);
        queueEvent(kDvrEventPlay);  // re-queue play event now that we have a tuner
        break;

    case kDvrFirstFrameEvent:
        dlog(DL_MSP_DVR, DLOGL_NORMAL, "Video Presentation Started successfully");
        gettimeofday(&tv_stop, 0);
        dlog(DL_MSP_DVR, DLOGL_NOISE, "Stop TV time, elapsed secs %ld", (tv_stop.tv_sec - tv_start.tv_sec));
        mState = kDvrStateRendering;   //set state to rendering as we received the first frame
        DoCallback(kMediaPlayerSignal_PresentationStarted, kMediaPlayerStatus_Ok);
        if ((mPersistentRecordState == kPersistentRecordWaitingPMT) && (mTsbState == kTsbNotCreated) && mPtrLiveSource && mPtrLiveSource->canRecord())
        {
            LOG(DLOGL_NORMAL, " starting Recording as Persistent Record was kept pending because of PMT pending ");
            StartRecording();
        }
        else
        {
            StartDwellTimerForTsb();
        }
        break;

    case kDvrEventServiceAuthorized:
        LOG(DLOGL_NOISE, "Service authorized by CAM");
        {
            bool serviceDeAuth = false;
            CurrentVideoMgr::instance()->CurrentVideo_SetServiceAttribute(&serviceDeAuth);
            if (mTsbState == kTsbCreated || mTsbState == kTsbStarted)
            {
                AddLiveSession();
            }
        }
        if (mPtrDispSession)
        {
            eMspStatus status = kMspStatus_Ok;
            status = mPtrDispSession->startOutput();
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "startOutput error %d on content authorization", status);
            }
        }
        DoCallback(kMediaPlayerSignal_ServiceAuthorized, kMediaPlayerStatus_Ok);
        break;


    case kDvrEventServiceDeAuthorized:
        LOG(DLOGL_ERROR, "Service DeAuthorized by CAM");
        {
            bool serviceDeAuth = true;
            CurrentVideoMgr::instance()->CurrentVideo_SetServiceAttribute(&serviceDeAuth);
        }
        if (mPtrDispSession)
        {
            eMspStatus status = kMspStatus_Ok;
            status = mPtrDispSession->stopOutput();
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "stopOutput error %d on content deauthorization", status);
            }
        }
        DoCallback(kMediaPlayerSignal_ServiceDeauthorized, kMediaPlayerStatus_Ok);
        break;

    case kDvrEventAudioLangChangeCb:
        if (mPtrDispSession)
        {
            mPtrDispSession->updatePids(mPtrPsi);
        }
        else
        {
            LOG(DLOGL_ERROR, "NULL Display session.Ignoring audio language changed setting");
        }
        break;

    case kDvrEventBOF:
        DoCallback(kMediaPlayerSignal_BeginningOfStream , kMediaPlayerStatus_Ok);
        break;

    case kDvrEventEOF:
        DoCallback(kMediaPlayerSignal_EndOfStream , kMediaPlayerStatus_Ok);
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
            DoCallback(kMediaPlayerSignal_ServiceNotAvailableDueToSdv , kMediaPlayerStatus_ServiceNotAvailable);
        }
        break;

    case kDvrEventSDVCancelled:
        if (mState != kDvrStateStop)
        {
            DoCallback(kMediaPlayerSignal_ServiceNotAvailableDueToSdv , kMediaPlayerStatus_Ok);
        }
        break;

    case kDvrEventSDVKeepAliveNeeded:
        if (mState != kDvrStateStop)
        {
            DoCallback(kMediaPlayerSignal_NetworkResourceReclamationWarning , kMediaPlayerStatus_Ok);
        }
        break;

    case kDvrRecordingAborted:
    case kDvrRecordingDiskFull:
        if (mPersistentRecordState == kPersistentRecordStarted)
        {
            LOG(DLOGL_ERROR, "Stop converting the ongoing recording");
            mPersistentRecordState = kPersistentRecordNotStarted;
            if (mPtrRecSession != NULL)
            {
                eMspStatus ret_value;
                ret_value = mPtrRecSession->stopConvert();
                if (ret_value != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "TSB Conversion stop on disk full/recording abort failed.");
                }
            }
        }
        DoCallback(kMediaPlayerSignal_PersistentRecordingTerminated, kMediaPlayerStatus_Ok);
        break;


    case kDvrEventSDVLoaded:
        if (mState != kDvrStateStop)
        {
            DoCallback(kMediaPlayerSignal_ServiceLoaded , kMediaPlayerStatus_Ok);
            queueEvent(kDvrEventPlay);
        }
        break;

    default:
        LOG(DLOGL_ERROR, "Not a valid callback event %d", aEvt->eventType);
        break;
    }
    return false;
}

/** *********************************************************
 *
*/
void* Dvr::dwellTimerFun(void *aData)
{
    bool done = false;
    Dvr *inst = (Dvr *)aData;

    struct timeval tv;
    long secs_start, secs_stop;

    FNLOG(DL_MSP_DVR);

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
        if (secs_stop > (secs_start + TSB_DwellTime))
        {
            LOG(DLOGL_REALLY_NOISY, "Dwell Time Expired");
            inst->queueEvent(kDvrDwellTimeExpiredEvent);
            done = true;
        }

        if (inst->getDvrState() != kDvrStateRendering)
        {
            LOG(DLOGL_REALLY_NOISY, "exit dwellTimer thread - state != kDvrStateRendering");
            done = true;
        }
        usleep(100000);      // give other threads a chance to run
    }

    LOG(DLOGL_REALLY_NOISY, "dwellTimer Thread Exit");
    pthread_exit(NULL);
    return NULL;
}


/** *********************************************************
 *
*/
eDvrState Dvr::getDvrState(void)
{
    return mState;
}



void Dvr::mediaCB(void *clientInst, tCpeMediaCallbackTypes type)
{
    if (!clientInst)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "Error: null clientInst for type: %d", type);
        return;
    }

    Dvr *inst = (Dvr *)clientInst;
    dlog(DL_MSP_DVR, DLOGL_NOISE, "%s, %d, type %d", __FUNCTION__, __LINE__, type);

    switch (type)
    {
    case eCpeMediaCallbackType_FirstFrameAlarm:
        inst->queueEvent(kDvrFirstFrameEvent);
        break;

    default:
        dlog(DL_MSP_DVR, DLOGL_NOISE, "media type %d not handled", type);
        break;
    }
}



/** *********************************************************
 *
*/
void Dvr::sourceCB(void *aData, eSourceState aSrcState)
{
    Dvr *inst = (Dvr *)aData;
    if (inst != NULL)
    {
        switch (aSrcState)
        {
        case kSrcTunerLocked:
            dlog(DL_MSP_DVR, DLOGL_NOISE, "source callback: tuner lock callback");
            inst->queueEvent(kDvrEventRFCallback);
            break;
        case kAnalogSrcTunerLocked:
            dlog(DL_MSP_DVR, DLOGL_NORMAL, "source callback: tuner lock callback kAnalogSrcTunerLocked\n");
            inst->queueEvent(kDvrEventRFAnalogCallback);
            break;
        case kSrcBOF:
            dlog(DL_MSP_DVR, DLOGL_NOISE, "BOF event");
            inst->queueEvent(kDvrEventBOF);
            break;

        case kSrcEOF:
            dlog(DL_MSP_DVR, DLOGL_NOISE, "EOF event");
            inst->queueEvent(kDvrEventEOF);
            break;

        case kSrcSDVServiceLoading:
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, " \" SDV Service Loading \"  event ");
            inst->queueEvent(kDvrEventSDVLoading);
            break;

        case kSrcSDVServiceUnAvailable:
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, " \"SDV Service Un-Available \" event");
            inst->queueEvent(kDvrEventSDVUnavailable);
            break;

        case kSrcSDVServiceChanged:
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, " \" SDV Service changed\" event ");
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
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, " \" SDV Service Cancelled \" event");
            inst->queueEvent(kDvrEventSDVCancelled);
            break;

        case kSrcSDVKnown:
            inst->queueEvent(kDvrEventSDVLoaded);
            break;

        case kSrcPPVInterstitialStart:
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "source callback: ppv interstitial start callback");
            inst->queueEvent(kDvrPpvInstStart);
            break;

        case kSrcPPVInterstitialStop:
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "source callback: ppv interstitial stop callback");
            inst->queueEvent(kDvrPpvInstStop);
            break;

        case kSrcPPVSubscriptionAuthorized:
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "source callback: ppv authorization callback");
            inst->queueEvent(kDvrPpvSubscnAuth);
            break;

        case kSrcPPVSubscriptionExpired:
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "source callback: ppv auth expiration callback");
            inst->queueEvent(kDvrPpvSubscnExprd);
            break;

        case kSrcPPVStartVideo:
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "source callback: ppv start video callback");
            inst->queueEvent(kDvrPpvStartVideo);
            break;

        case kSrcPPVStopVideo:
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "source callback: ppv stop video callback");
            inst->queueEvent(kDvrPpvStopVideo);
            break;


        case kSrcPPVContentNotFound:
            dlog(DL_MSP_DVR, DLOGL_ERROR, "Source Callback PPV Content not found");
            inst->queueEvent(kDvrPpvContentNotFound);
            break;

        case kSrcTunerLost:
            dlog(DL_MSP_DVR, DLOGL_NOISE, "Source Callback Tuner lost");
            inst->queueEvent(kDvrTunerLost);
            break;

        case kSrcTunerRegained:
            dlog(DL_MSP_DVR, DLOGL_NOISE, "Source Callback Tuner restored");
            inst->queueEvent(kDvrTunerRestored);
            break;

        default:
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Not a valid callback event");
            break;
        }
    }
}

/** *********************************************************
 *
*/
void Dvr::psiCallback(ePsiCallBackState aState, void *aData)
{
    FNLOG(DL_MSP_DVR);

    Dvr *inst = (Dvr *)aData;

    switch (aState)
    {
    case kPSIReady:
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "PSI Callback arrived!! ");
        if (inst->mDestUrl != "") //regular live video
        {
            inst->queueEvent(kDvrPSIReadyEvent);
        }
        else   //scheduled recording case
        {
            inst->queueEvent(kDvrSchedRecEvent);
        }

        break;
    case kPSIUpdate:
        inst->queueEvent(kDvrPSIUpdateEvent);
        break;
    case kPmtRevUpdate:
        inst->queueEvent(kDvrPmtRevUpdateEvent);

    case kPSITimeOut:
        dlog(DL_MSP_DVR, DLOGL_ERROR, "PSI Timeout");
        inst->queueEvent(kDvrTimeOutEvent);
        break;

    case kPSIError:
        dlog(DL_MSP_DVR, DLOGL_ERROR, "PSI Error!");
        inst->queueEvent(kDvrTimeOutEvent);
        break;

    default:
        break;
    }

}

/** *********************************************************
 *
*/
void Dvr::recordSessionCallback(tCpeRecCallbackTypes aType, void *aData)
{
    FNLOG(DL_MSP_DVR);

    Dvr *inst = (Dvr *)aData;

    if (inst == NULL)
    {
        LOG(DLOGL_ERROR, " Record session callback of type %d with NULL controller pointer reference", aType);
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
            LOG(DLOGL_ERROR, "Recording disk full event");
            inst->queueEvent(kDvrRecordingDiskFull);
            break;

        case eCpeRecCallbackTypes_RecAborted:
            LOG(DLOGL_ERROR, "Recording aborted  event");
            inst->queueEvent(kDvrRecordingAborted);
            break;

        default:
            break;
        }
    }
}

/** *********************************************************
*/
eMspStatus Dvr::setSpeedMode(int num, unsigned int den, tCpePlaySpeed *pPlaySpeed)
{

    FNLOG(DL_MSP_DVR);

    if (num > 0)
    {
        pPlaySpeed->mode = eCpePlaySpeedMode_Forward;
        dlog(DL_MSP_DVR, DLOGL_NOISE, "SetSpeedMode:: eCpePlaySpeedMode_Forward ");
    }

    else if (num < 0)
    {
        pPlaySpeed->mode = eCpePlaySpeedMode_Rewind;
        dlog(DL_MSP_DVR, DLOGL_NOISE, "SetSpeedMode:: eCpePlaySpeedMode_Rewind ");
    }

    else if (num == 0)
    {
        pPlaySpeed->mode = eCpePlaySpeedMode_Pause;
        dlog(DL_MSP_DVR, DLOGL_NOISE, "SetSpeedMode:: eCpePlaySpeedMode_Pause ");
    }

    pPlaySpeed->scale.numerator = abs(num);
    pPlaySpeed->scale.denominator = abs((long int)den);

    mSpeedNumerator = num;
    mSpeedDenominator = den;
    dlog(DL_MSP_DVR, DLOGL_NOISE, " ant_num = %d  ant_den = %d", num, den);
    dlog(DL_MSP_DVR, DLOGL_NOISE, " cpe_num = %d  cpe_den = %d", pPlaySpeed->scale.numerator, pPlaySpeed->scale.denominator);

    return kMspStatus_Ok;

}



/** *********************************************************
*/
eIMediaPlayerStatus Dvr::Load(const char* aServiceUrl, const MultiMediaEvent **pMme)
{
    UNUSED_PARAM(pMme);
    int status;

    FNLOG(DL_MSP_DVR);
    mPackets = 0;
    if ((mState != kDvrStateIdle) && (mState != kDvrStateStop) && (mState != kDvrWaitForTuner))
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
// if we are re-using the controller, don't re-create event thread on subsequent loads
    status = parseSource(aServiceUrl);
    if ((status != kMspStatus_Ok) && (status != kMspStatus_Loading))
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Invalid URL");
        return kMediaPlayerStatus_Error_InvalidURL;
    }

    dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s, %d, Start TV time", __FUNCTION__, __LINE__);
    gettimeofday(&tv_start, 0);

    if ((status == kMspStatus_Ok || status == kMspStatus_Loading) && (mEventHandlerThread == 0))
    {
        int threadRetvalue;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, (128 * 1024)); //128kb

        // By default the thread is created as joinable.
        threadRetvalue = pthread_create(&mEventHandlerThread, &attr, eventthreadFunc, (void *) this);
        if (threadRetvalue)
        {
            LOG(DLOGL_ERROR, "pthread_create error %d", threadRetvalue);
        }
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Created event thread handle %x", (unsigned int)mEventHandlerThread);

        threadRetvalue = pthread_setname_np(mEventHandlerThread, "DVR Cntrl Event Handler");
        if (threadRetvalue)
        {
            LOG(DLOGL_ERROR, "thread naming error %d", threadRetvalue);
        }

        mState = kDvrStateStop;

    }

    return  kMediaPlayerStatus_Ok;
}


/** *********************************************************
*/
void Dvr::Eject()
{
    FNLOG(DL_MSP_DVR);

    if (mDwellTimerThread)
    {
        pthread_join(mDwellTimerThread, NULL);       // wait for dwell timer thread to exit,if it was created.
        mDwellTimerThread = 0;
    }

    if (mEventHandlerThread)
    {
        queueEvent(kDvrEventExit);  // tell thread to exit
        unLockMutex();
        pthread_join(mEventHandlerThread, NULL);       // wait for event thread to exit
        lockMutex();
        mEventHandlerThread = 0;
    }


    CloseDisplaySession();

    if (mPtrRecSession != NULL)
    {
        mTsbHardDrive = -1;
        mPtrRecSession->stop();
        mPtrRecSession->unregisterRecordCallback();
        mPtrRecSession->close();
        mTsbState = kTsbNotCreated;
        mIsPatPmtInjected = false;
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

    if (mPtrFileSource != NULL)
    {
        mPtrFileSource->stop();
        delete mPtrFileSource;
        mPtrFileSource = NULL;
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

/** *********************************************************
*/


eMspStatus Dvr::parseSource(const char *aServiceUrl)
{
    MSPSource *source;
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    source = mPtrLiveSource; //just for initialization
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
            mCurrentSource = MSPSourceFactory :: getMSPSourceType(aServiceUrl);
            LOG(DLOGL_NOISE, "Create new source,as source type change across channel change.");
            mPtrLiveSource->stop();
            delete mPtrLiveSource;
            mPtrLiveSource = NULL;
            source = MSPSourceFactory::getMSPSourceInstance(mCurrentSource, aServiceUrl, mIMediaPlayerSession);
        }
        if (source == NULL)
        {
            LOG(DLOGL_ERROR, "Error null source");
            return kMspStatus_Error;
        }

        if (source->isDvrSource())
        {
            LOG(DLOGL_NORMAL, "Set kDvrFileSrc");
            mCurrentSourceType = kDvrFileSrc;
            mPtrFileSource = source;
            status = mPtrFileSource->load(sourceCB, this);
            if (status == kMspStatus_Ok)
            {
                mFileName = source->getFileName();
                LOG(DLOGL_REALLY_NOISY, "filename: %s", mFileName.c_str());
            }
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "Set kDvrLiveSrc");
            mCurrentSourceType = kDvrLiveSrc;
            mPtrLiveSource = source;
            status = mPtrLiveSource->load(sourceCB, this);
            LOG(DLOGL_REALLY_NOISY, "source load returns %d", status);
        }
        return status;
    }
    else
    {
        return kMspStatus_Error;
    }
}


/** *********************************************************
*/
eIMediaPlayerStatus Dvr::Play(const char* outputUrl, float nptStartTime, const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_DVR);

    UNUSED_PARAM(pMme)

    mNptSetPos = nptStartTime;

    if (mState == kDvrStateIdle)
    {
        LOG(DLOGL_ERROR, "Error Wrong state %d", mState);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (outputUrl == NULL)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "NULL outputUrl in Play");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    mDestUrl = outputUrl;
    if ((mDestUrl.find("decoder://primary") != 0) && (mDestUrl.find("decoder://secondary") != 0))
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "bad destUrl in Play url:%s\n", mDestUrl.c_str());
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    mIsBackground = false;
    if (mPersistentRecordState == kPersistentRecordNotStarted)            //brand new session
    {
        queueEvent(kDvrEventPlay);
    }
    else                                //already the session is active with recording on it ON.So no need to tune to it again
    {
        mState = kDvrSourceReady;

        if ((mPtrLiveSource && mPtrPsi) || (mPtrLiveSource && mPtrAnalogPsi))
        {
            mCurrentSourceType = kDvrLiveSrc;
            LOG(DLOGL_REALLY_NOISY, "Set kDvrLiveSrc");
            if (mPtrLiveSource->isPPV() == true)
            {
                LOG(DLOGL_NOISE, "Saving and setting the tuner priority to PPV video or recording priority  ");
                mTunerPriority = kRMPriority_PpvVideoPlayOrRecording;
                mPtrLiveSource->setTunerPriority(kRMPriority_PpvVideoPlayOrRecording);
            }
            else
            {
                LOG(DLOGL_NOISE, "Saving and setting the tuner priority to  foreground recording priority  ");
                mTunerPriority = kRMPriority_ForegroundRecording;
                mPtrLiveSource->setTunerPriority(kRMPriority_ForegroundRecording);
            }
            if (mPtrAnalogPsi && mPtrLiveSource && mPtrLiveSource->isAnalogSource())
            {
                mPtrAnalogPsi->psiStart(mPtrLiveSource);
                psiCallback(kPSIReady, (void *)this);
            }
            else
            {
                if (mPtrPsi)
                    mPtrPsi->psiStart(mPtrLiveSource);
            }

        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "There is no Live Source Dummy Recording, mPtrLiveSource %p, mPtrPsi %p, mPtrAnalogPsi %p",
                 mPtrLiveSource, mPtrPsi, mPtrAnalogPsi);
            return kMediaPlayerStatus_Error_Unknown;
        }

    }

    return  kMediaPlayerStatus_Ok;
}



eIMediaPlayerStatus Dvr::GetNptStartTimeMs(uint32_t *convertedStartNptMs, uint32_t *tsbDurationMs)
{
    // Will check for valid NPT start time  and convert

    uint32_t currentNptMs;
    if (mPtrRecSession == NULL)
    {
        LOG(DLOGL_ERROR, "Error: Null mPtrRecSession");
        return kMediaPlayerStatus_ServiceNotAvailable;
    }
    eMspStatus status = mPtrRecSession->GetCurrentNpt(&currentNptMs);
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "Error %d getting current NPT", status);
        return kMediaPlayerStatus_ServiceNotAvailable;
    }


    uint32_t startNptMs;
    status = mPtrRecSession->GetStartNpt(&startNptMs);
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "Error %d getting current NPT", status);
        return kMediaPlayerStatus_ServiceNotAvailable;
    }

    if (mRecStartTime == MEDIA_PLAYER_NPT_START) //probably,ANT wants to record the channel whole of the channel TSB.
    {
        *convertedStartNptMs = startNptMs;
    }
    else if (mRecStartTime == MEDIA_PLAYER_NPT_NOW)
    {
        *convertedStartNptMs = currentNptMs;
    }
    else if (mRecStartTime == MEDIA_PLAYER_NPT_END)
    {
        LOG(DLOGL_ERROR, "Error MEDIA_PLAYER_NPT_END is invalid");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }
    else
    {
        if (mRecStartTime < 0)
        {
            LOG(DLOGL_ERROR, "Error nptRecordStartTime < 0");
            return kMediaPlayerStatus_Error_InvalidParameter;
        }

        *convertedStartNptMs = (uint32_t) mRecStartTime * 1000;             //converting seconds to milliseconds.
    }

    // TSB Duration is the lenght of program currently in the TSB that will be converted.
    // For example, if the TSB started 5 minutes ago and the request is for MEDIA_PLAYER_NPT_START,
    // the tsbDurationMs should be (5 * 60 * 1000)

    *tsbDurationMs =  currentNptMs - *convertedStartNptMs;

    return kMediaPlayerStatus_Ok;

}


eIMediaPlayerStatus Dvr::GetNptStopTimeMs(uint32_t *convertedStopNpt)
{
    // Will check for valid NPT stop time  and convert

    if (mRecStopTime == MEDIA_PLAYER_NPT_START)      //invalid parameter for stoptime!!!!!!
    {
        LOG(DLOGL_ERROR, "Error MEDIA_PLAYER_NPT_START is invalid");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }
    else if (mRecStopTime == MEDIA_PLAYER_NPT_NOW)
    {
        uint32_t npt;
        eMspStatus status = mPtrRecSession->GetCurrentNpt(&npt);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Error %d getting current NPT", status);
            return kMediaPlayerStatus_ServiceNotAvailable;
        }
        *convertedStopNpt = npt;
    }
    else if (mRecStopTime == MEDIA_PLAYER_NPT_END)
    {
        *convertedStopNpt = (uint32_t) kCpeRec_EndTime; //setting the CPERP macro for the unknown end time of recording.
    }
    else
    {
        *convertedStopNpt = (uint32_t)mRecStopTime * 1000;
    }

    return kMediaPlayerStatus_Ok;
}


void Dvr::SetRecordDrive()
{
    // Set drive based on mRecFile - this is the recordUrl mRecFile saved in PeristentRecord

    int prevDrive = mTsbHardDrive;

    LOG(DLOGL_MINOR_DEBUG, "mRecFile: %s ", mRecFile.c_str());

    size_t found = mRecFile.find("/mnt/dvr0");
    if (found != string::npos)
    {
        mTsbHardDrive = 0;
        LOG(DLOGL_MINOR_DEBUG, "internal");
    }
    else
    {
        size_t found = mRecFile.find("/mnt/dvr1");
        if (found != string::npos)
        {
            mTsbHardDrive = 1;
            LOG(DLOGL_MINOR_DEBUG, "external");
        }
        else
        {
            LOG(DLOGL_MINOR_DEBUG, "no drive specified in record URL");
        }
    }

    // TODO: Need to consider case where Persistent Record is on drive differnt from current TSB
    //       May need to stop current TSB on current drive and restart
    //       This could happen when restarting recording after reboot
    if ((prevDrive == 0 || prevDrive == 1) && prevDrive != mTsbHardDrive)
    {
        LOG(DLOGL_NOISE, "WARNING: Specified drive different than current TSB");
        LOG(DLOGL_NOISE, "         ** This case not yet handled **");
        mTsbHardDrive = prevDrive;
    }

    LOG(DLOGL_MINOR_DEBUG, "mTsbHardDrive: %d", mTsbHardDrive);
}



void Dvr::SetRecordFilename()
{
    // Prepends mount point to mRecFile if needed

    LOG(DLOGL_MINOR_DEBUG, "mRecFile: %s  mTsbHardDrive: %d", mRecFile.c_str(), mTsbHardDrive);

    size_t found = mRecFile.find("mnt/dvr");
    if (found != string::npos)
    {
        mRecFile = "/" + mRecFile;  // adding the root path
        LOG(DLOGL_MINOR_DEBUG, "drive already specified");
    }
    else
    {
        if (mTsbHardDrive == 0)
        {
            mRecFile = "/mnt/dvr0/" + mRecFile;  // internal drive
        }
        else if (mTsbHardDrive == 1)
        {
            mRecFile = "/mnt/dvr1/" + mRecFile;  // external drive
        }
        else
        {
            LOG(DLOGL_ERROR, "ERROR: Drive not specifed mTsbHardDrive: %d", mTsbHardDrive);
            // assert for now
            assert(1);

            mRecFile = "/mnt/dvr0/" + mRecFile;  // default to internal drive
        }
    }

    LOG(DLOGL_REALLY_NOISY, "mRecFile: %s", mRecFile.c_str());

}



eMspStatus Dvr::StartPersistentRecording()
{
    // start peristent recording
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    uint32_t nptRecordStartTimeMs;
    uint32_t durationCurrentTsbMs;

    eIMediaPlayerStatus nptConvertErr = GetNptStartTimeMs(&nptRecordStartTimeMs, &durationCurrentTsbMs);
    if (nptConvertErr)
    {
        return kMspStatus_Error;
    }

    uint32_t nptRecordStopTimeMs;

    nptConvertErr = GetNptStopTimeMs(&nptRecordStopTimeMs);
    if (nptConvertErr)
    {
        return kMspStatus_Error;
    }

    LOG(DLOGL_NOISE, "nptRecordStartTimeMs: %d  nptRecordStopTimeMs: %d", nptRecordStartTimeMs, nptRecordStopTimeMs);

    if (nptRecordStopTimeMs < nptRecordStartTimeMs)
    {
        LOG(DLOGL_ERROR, "Error: nptRecordStopTimeMs < nptRecordStartTimeMs");
        return kMspStatus_Error;
    }

    SetRecordFilename();  // sets mRecFile to full path.  //setting the record file path here,so that path of recording URL has been set,after TSB has been created (as both should be on same drive)

    LOG(DLOGL_NOISE, "nptRecordStartTimeMs: %d  nptRecordStopTimeMs: %d", nptRecordStartTimeMs, nptRecordStopTimeMs);

    status = mPtrRecSession->startConvert(mRecFile, nptRecordStartTimeMs, nptRecordStopTimeMs);

    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "startConvert error %d ", status);
        syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
               "DLOG|MSP|Recording Failure|%s: startConvert error, status %d" ,
               __FUNCTION__, status);

        if ((mPtrLiveSource != NULL) && (mPtrLiveSource->isPPV() == false))   //reduce back the priority setting.
        {
            if (mEnaAudio)
            {
                LOG(DLOGL_NOISE, "Setting the tuner priority to kRMPriority_VideoWithAudioFocus ");
                mPtrLiveSource->setTunerPriority(kRMPriority_VideoWithAudioFocus);
            }
            else
            {
                LOG(DLOGL_NOISE, "Setting the tuner priority to kRMPriority_VideoWithoutAudioFocus ");
                mPtrLiveSource->setTunerPriority(kRMPriority_VideoWithoutAudioFocus);
            }
        }

        return kMspStatus_Error;
    }
    else
    {
        LOG(DLOGL_NOISE, "Recording started!");
        if (mPtrLiveSource)
            mPtrLiveSource->setSdv(true, kSdvmService_IsRecording);

        // Notify record scheduler of drive associated with recording filename
        int found = mRecFile.find_last_of("/\\");
        string fileName = mRecFile.substr(found + 1);

        float durationCurrentTsbSec = (float)durationCurrentTsbMs / 1000;

        LOG(DLOGL_NOISE, "Csci_Dvr_NotifyRecordingStart name: %s  mTsbHardDrive: %d durationCurrentTsbSec: %f",
            fileName.c_str(), mTsbHardDrive, durationCurrentTsbSec);

        Csci_Dvr_NotifyRecordingStart(fileName.c_str(), mTsbHardDrive, durationCurrentTsbSec);

    }

    return kMspStatus_Ok;
}





eIMediaPlayerStatus Dvr::StartRecording()
{
    // start peristent recording
    FNLOG(DL_MSP_DVR);
    eMspStatus status = kMspStatus_Ok;
    int pos;
    std::string first_fragment_file;

    pos = mRecFile.find(",");
    if (pos == -1) //normal recording
    {
        LOG(DLOGL_NORMAL, "Normal recording,hence first fragment name will %s", first_fragment_file.c_str());
        first_fragment_file = "";
    }
    else  //interrupted recording
    {
        first_fragment_file = mRecFile.substr(0, pos);
        mRecFile = mRecFile.substr(pos + 1); //current fragment
        mRecFile = mRecFile.substr(strlen("sadvr://"));
        LOG(DLOGL_NORMAL, "Interrupted recording,whose original fragment name is %s, curr recFile is %s",
            first_fragment_file.c_str(), mRecFile.c_str());
    }

    if (mTsbState == kTsbNotCreated)
    {
        LOG(DLOGL_NORMAL, "start TSB");
        status = StartTSBRecordSession(first_fragment_file); //passing first fragment file name.
        if (status == kMspStatus_NotSupported)
        {
            LOG(DLOGL_ERROR, "Music channel with no video.Recording not supported");
            syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                   "DLOG|MSP|Recording Failure|%s: Music channel with no video, recording not supported",
                   __FUNCTION__);

        }
        else if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "StartTSBRecordSession error: %d", status);
            syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                   "DLOG|MSP|Recording Failure|%s: StartTSBRecordSession error, status %d" ,
                   __FUNCTION__, status);
            return kMediaPlayerStatus_ServiceNotAvailable;
        }
        else
        {
            mTsbState = kTsbCreated;
            LOG(DLOGL_NOISE, "TSB creation successful in case of recording request before dwell time expiry");
        }

    }
    else   //TSB has been already started by dwell timer.
    {
        LOG(DLOGL_NOISE, "First fragment file name is %s\n", first_fragment_file.c_str());
        if (first_fragment_file != "")  //In case of fragmented recording,need to restart the tsb with original fragment file info for ensuring fragment playback on MRDVR. This corner case occurs,
            // when the box reboots on channel with recording going ON and video starts playing before DVR scheduler trigger second fragment recording.
        {
            LOG(DLOGL_NORMAL, "Restart TSB for interrupted recording");

            status = StopRecordSession();
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "StopRecordSession Failed");
                return kMediaPlayerStatus_ServiceNotAvailable;
            }

            status = StartTSBRecordSession(first_fragment_file); //passing first fragment file name
            if (status == kMspStatus_NotSupported)
            {
                LOG(DLOGL_ERROR, "Music channel with no video.Recording not supported");
                syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                       "DLOG|MSP|Recording Failure|%s: interrupted rec:Music channel with no video, recording not supported",
                       __FUNCTION__);
            }
            else if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "StartTSBRecordSession error: %d", status);
                syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                       "DLOG|MSP|Recording Failure|%s:Interrupted rec:StartTSBRecordSession error, status %d" ,
                       __FUNCTION__, status);
                return kMediaPlayerStatus_ServiceNotAvailable;
            }
            else
            {
                mTsbState = kTsbCreated;
                LOG(DLOGL_NOISE, "TSB creation successful in case of restart TSB for interrupted recording");
            }
        }
    }

    if (mTsbState == kTsbCreated)
    {
        AddLiveSession();
    }

    if (mTsbState == kTsbStarted)
    {
        status = StartPersistentRecording();
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Persistent Recording failed");
            mPersistentRecordState = kPersistentRecordNotStarted;
            return kMediaPlayerStatus_Error_Unknown;
        }
        else
        {
            LOG(DLOGL_NOISE, "Persistent Recording started successfully");
            mPersistentRecordState = kPersistentRecordStarted;
        }
    }
    else
    {
        mPersistentRecordState = kPersistentRecordWaitingTSB;
    }

    return kMediaPlayerStatus_Ok;

}

/** *********************************************************
*/

eIMediaPlayerStatus Dvr::PersistentRecord(const char* recordUrl,
        float nptRecordStartTime,
        float nptRecordStopTime,
        const MultiMediaEvent **pMme)
{
    UNUSED_PARAM(pMme);
    FNLOG(DL_MSP_DVR);
#if defined(__PERF_DEBUG__)
//    PerstRecord = 1;
    gettimeofday(&start_clk, NULL);
#endif
    // Note: parameters were printed in IMediaPlayer::IMediaPlayerSession_PersistentRecord

    eMspStatus status = kMspStatus_Ok;

    if (!mPtrLiveSource)
    {
        LOG(DLOGL_ERROR, "Error: null mPtrLiveSource");
        return kMediaPlayerStatus_ServiceNotAvailable;
    }

    if (!mPtrLiveSource->canRecord())
    {
        LOG(DLOGL_ERROR, "Error: cannot record source");
        return kMediaPlayerStatus_ServiceNotAvailable;
    }

    mRecStartTime = nptRecordStartTime;
    mRecStopTime  = nptRecordStopTime;

    // strip off "sadvr://" and save to class attribute file
    mRecFile = recordUrl;
    mRecFile = mRecFile.substr(strlen("sadvr://"));

    SetRecordDrive();   // sets mTsbHardDrive


    eIMediaPlayerStatus  playerStatus = kMediaPlayerStatus_Ok;

    mPersistentRecordState = kPersistentRecordWaitingPMT;  //pre stage for recording. but not actually started yet.

    if (mDestUrl == "")
    {
        // Play has not been called.  This is a scheduled recording.
        LOG(DLOGL_NOISE, " mDestUrl not specified");
        if (mPtrLiveSource->isPPV() == true)
        {
            LOG(DLOGL_NOISE, "Saving the tuner priority to PPV video or recording priority  ");
            mTunerPriority = kRMPriority_PpvVideoPlayOrRecording;
        }
        else
        {
            LOG(DLOGL_NOISE, "Saving the tuner priority to  background recording priority  ");
            mTunerPriority = kRMPriority_BackgroundRecording;
        }

        mPtrLiveSource->setTunerPriority(mTunerPriority);
        mIsBackground = true; //background session

        queueEvent(kDvrEventPlay);
    }
    else
    {
        if (mPtrLiveSource->isPPV() == true)
        {
            LOG(DLOGL_NOISE, "Saving the tuner priority to PPV video or recording priority  ");
            mTunerPriority = kRMPriority_PpvVideoPlayOrRecording;
        }
        else
        {
            LOG(DLOGL_NOISE, "Saving the tuner priority to  background recording priority  ");
            mTunerPriority = kRMPriority_ForegroundRecording;
        }

        mPtrLiveSource->setTunerPriority(mTunerPriority);

        if (mState != kDvrStateRendering) //Play() hasn't completed yet.Delaying the recording.
        {
            LOG(DLOGL_ERROR, "returning for here :uday");
            return kMediaPlayerStatus_Ok;
        }

        playerStatus = StartRecording();
        if (playerStatus != kMediaPlayerStatus_Ok)
        {
            LOG(DLOGL_ERROR, "StartRecording error: %d", status);
            return playerStatus;
        }
    }

    return  playerStatus;
}

/** *********************************************************
*/
eIMediaPlayerStatus Dvr::Stop(bool stopPlay, bool stopPersistentRecord)
{
    eMspStatus ret_value;
    FNLOG(DL_MSP_DVR);
    dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "In Dvr::Stop stopPlay = %d,  stopPersistentRecord = %d state = %d\n", stopPlay, stopPersistentRecord , mState);

    if (stopPersistentRecord)
    {

        if ((mPtrLiveSource != NULL) && (mPtrLiveSource->isPPV() == false))   //shouldn't reduce PPV priority on stopping a recording on PPV channel.
        {
            if (mEnaAudio)
            {
                LOG(DLOGL_NOISE, "Setting the tuner priority to kRMPriority_VideoWithAudioFocus ");
                mPtrLiveSource->setTunerPriority(kRMPriority_VideoWithAudioFocus);
            }
            else
            {
                LOG(DLOGL_NOISE, "Setting the tuner priority to kRMPriority_VideoWithoutAudioFocus ");
                mPtrLiveSource->setTunerPriority(kRMPriority_VideoWithoutAudioFocus);
            }
        }

        mPersistentRecordState = kPersistentRecordNotStarted;
        if (mPtrRecSession != NULL)
        {
            ret_value = mPtrRecSession->stopConvert();
            if (ret_value != kMspStatus_Ok)
            {
                dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d TSB Conversion Stop failed", __FUNCTION__, __LINE__);
                return kMediaPlayerStatus_ServiceNotAvailable;
            }
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Stop perm. recording with no recSession", __FUNCTION__, __LINE__);
        }

        if (mDestUrl == "")  //non live recording case
        {
            ret_value = StopRecordSession();
            if (ret_value != kMspStatus_Ok)
            {
                dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d RecordSession Stop failed", __FUNCTION__, __LINE__);
                syslog(LOG_MAKEPRI(LOG_LOCAL7, LOG_INFO),
                       "DLOG|MSP|Recording Failure|%s:StopRecordSession error" ,
                       __FUNCTION__);
                return kMediaPlayerStatus_ServiceNotAvailable;
            }
        }

        if (mPtrLiveSource)
        {
            mPtrLiveSource->setSdv(false, kSdvmService_IsRecording);
        }
    }

    if (stopPlay)
    {
        // Stop Current video streaming.
        if (mEnaAudio)
        {
            CurrentVideoMgr::instance()->CurrentVideo_RemoveLiveSession();
        }

        if (mPtrFileSource != NULL)  //moving the File source stop, ahead of display session close,as it seems leading to problem with G8 MDK
        {
            mPtrFileSource->stop();
        }

        CloseDisplaySession();

        if (mPtrFileSource != NULL)
        {
            delete mPtrFileSource;
            mPtrFileSource = NULL;
        }

        mPendingTrickPlay = false;
        mTrickNum = 100;
        mTrickDen = 100;

        if (mPersistentRecordState == kPersistentRecordNotStarted)  //stop and close the record session,only if there is no live recording.
        {
            mState = kDvrStateStop;

            //Ensuring PSI stop, in case of normal channel change with no live recording.
            if (mPtrPsi != NULL)
            {
                StopDeletePSI();
            }
            if (mPtrAnalogPsi)
            {
                StopDeleteAnalogPSI();
            }
            ret_value = StopRecordSession();
            if (ret_value != kMspStatus_Ok)
            {
                dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d RecordSession Stop failed", __FUNCTION__, __LINE__);
            }

            if (mPtrLiveSource && mPtrLiveSource->isSDV())
            {
                mPtrLiveSource->setMediaSessionInstance(NULL);
                deleteAllClientSession();
            }


            if (mDwellTimerThread)
            {
                pthread_join(mDwellTimerThread, NULL);       // wait for dwell timer thread to exit,if it was created.
                mDwellTimerThread = 0;
            }

            if (mThreadEventQueue)
            {
                mThreadEventQueue->flushQueue(); //flush out any pending events posted prior/during Stop() call.
            }

        }
        else
        {
            if ((mPtrLiveSource != NULL) && (mPtrLiveSource->isPPV() == false))
            {
                LOG(DLOGL_NOISE, "Setting the tuner priority to kRMPriority_BackgroundRecording ");
                mPtrLiveSource->setTunerPriority(kRMPriority_BackgroundRecording);
            }
            mIsBackground = true;

            if (mCurrentSourceType == kDvrFileSrc)  //ensure stopping the file source,in case we move out of channel doing trick play
            {
                if (mPtrFileSource != NULL)
                {
                    mPtrFileSource->stop();
                    delete mPtrFileSource;
                    mPtrFileSource = NULL;
                }
            }
        }
        //Fix for CSCup15341:G6:Rec missing in RPL on scheduling concurrent rec of PPV and other prog
        //This fix makes the other recording as background recording.Previously it treated as foreground recording because of mDestUrl was not NULL.
        mDestUrl = "";
    }
    return  kMediaPlayerStatus_Ok;
}



/** *********************************************************
*/
eIMediaPlayerStatus Dvr::SetSpeed(int numerator, unsigned int denominator)
{
    int status;
    tCpePlaySpeed playSpeed;
    uint32_t npt;
    char myFileName[MAX_FILE_SIZE];
    FNLOG(DL_MSP_DVR);
    dlog(DL_MSP_DVR, DLOGL_NOISE, "Inside Set Speed ant_num = %d  ant_den = %d\n", numerator, denominator);

    if ((mState != kDvrStateRendering)  && (mPtrFileSource == NULL))
    {
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "SetSpeed during wrong state. state: %d", mState);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (mPtrDispSession == NULL)
    {
        LOG(DLOGL_ERROR, "ERROR: No display session");
        return kMediaPlayerStatus_Error_Unknown;
    }


    switch (mCurrentSourceType)
    {

    case kDvrLiveSrc:
    {
        // Do nothing if normal speed
        if ((numerator / denominator) == 1)
        {
            return kMediaPlayerStatus_Ok;
        }
        else if (numerator > (int)denominator)
        {
            /* This event is sent so that UI doesnt display Fwd icon in this case */
            queueEvent(kDvrLiveFwd);
            return kMediaPlayerStatus_Ok;
        }

        if ((mPtrLiveSource == NULL) || !(mPtrLiveSource->canRecord()))
        {
            // If source is not recordable then we wont have TSB & we cant create it.
            return kMediaPlayerStatus_ServiceNotAvailable;
        }


        if (mTsbState == kTsbCreated)
        {
            mPendingTrickPlay = true;
            mTrickNum = numerator;
            mTrickDen = denominator;
            return kMediaPlayerStatus_Ok;
        }
        else if (mTsbState == kTsbNotCreated)  //This handle the scenario,where user tries trickplay even before dwell time expires(creating TSB).
        {
            // Start TSB
            eMspStatus status = StartTSBRecordSession("");
            if (status == kMspStatus_NotSupported)
            {
                LOG(DLOGL_ERROR, "Music channel with no video.Recording not supported");
            }
            else if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "StartTSBRecordSession error %d", status);
                return kMediaPlayerStatus_ServiceNotAvailable;
            }
            else
            {
                mTsbState = kTsbCreated;
                dlog(DL_MSP_MRDVR, DLOGL_NOISE, "Calling AddLiveSession on SetSpeed");
                AddLiveSession();
                LOG(DLOGL_NOISE, "TSB created successfully in case of user pressing trickplay before dwell time");

                mPendingTrickPlay = true;
                mTrickNum = numerator;
                mTrickDen = denominator;
                return kMediaPlayerStatus_Ok;

            }
        }
        else
        {
            LOG(DLOGL_NOISE, "TSB already started");
        }

        if (mPtrRecSession)
        {
            char *tsbFileName = mPtrRecSession->GetTsbFileName();
            if (tsbFileName)
            {
                strncpy(myFileName, tsbFileName, MAX_FILE_SIZE);
                myFileName[MAX_FILE_SIZE - 1] = '\0';
            }
            else
            {
                LOG(DLOGL_ERROR, "Error: null tsbFileName");
                return kMediaPlayerStatus_ServiceNotAvailable;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Error: null mPtrRecSession");
            return kMediaPlayerStatus_ServiceNotAvailable;
        }

        mFileName.assign(myFileName);
        mFileName.insert(0, "avfs:/");

        LOG(DLOGL_NOISE, "TSB myFileName: %s", myFileName);
        LOG(DLOGL_NOISE, "TSB mFileName:  %s", mFileName.c_str());

        eMSPSourceType SrcType;
        SrcType = MSPSourceFactory::getMSPSourceType(mFileName.c_str());
        MSPSource *source = MSPSourceFactory::getMSPSourceInstance(SrcType, mFileName.c_str(), mIMediaPlayerSession);
        if (source && source->isDvrSource())
        {
            mPtrFileSource = source;
        }
        else
        {
            LOG(DLOGL_ERROR, "Error: null invalid URL");
            return kMediaPlayerStatus_Error_InvalidURL;
        }
        if ((mPtrFileSource == NULL))
        {
            LOG(DLOGL_ERROR, "Error: Null mPtrFileSource ");
            return kMediaPlayerStatus_ServiceNotAvailable;
        }

        mPtrFileSource->load(sourceCB, this);

        // background recordings always start as TSB, this will get updated when convert tsb gets called
        status = mPtrFileSource->open(kRMPriority_ForegroundRecording);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "file source open failed  error = %d", status);
            return kMediaPlayerStatus_ServiceNotAvailable;
        }

        status = mPtrRecSession->GetCurrentNpt(&npt);
        LOG(DLOGL_NOISE, "Current Npt in Live Sourc %d", npt);

        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "GetCurrentNpt error = %d", status);
            return kMediaPlayerStatus_ServiceNotAvailable;
        }

        mPtrDispSession->freeze();
        mPtrDispSession->stop();
        mPtrDispSession->close(mEasAudioActive);

        if (mPtrPsi == NULL)
        {
            mPtrPsi = new Psi();
        }

        mPtrPsi->psiStart(mFileName);


        setDisplay();

        setSpeedMode(numerator, denominator, &playSpeed);


        status = mPtrRecSession->GetCurrentNpt(&npt);
        LOG(DLOGL_NOISE, "Current Npt in Live Sourc %d", npt);

        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "GetCurrentNpt error = %d", status);
            return kMediaPlayerStatus_ServiceNotAvailable;
        }


        if (npt <= 1000)
        {
            mPtrFileSource->setSpeed(playSpeed, 2);
        }
        else
        {
            mPtrFileSource->setSpeed(playSpeed, (npt - 1000));
        }

        status = mPtrDispSession->setMediaSpeed(playSpeed);
        if (status != kMspStatus_Ok)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "cpe_media_set failed for disp_session->setSpeed, Error = %d\n", status);
            return kMediaPlayerStatus_Error_InvalidParameter;
        }

        mPtrFileSource->start();
        mPtrDispSession->start(mEasAudioActive);
        mPtrDispSession->controlMediaAudio(playSpeed);
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, " Success disp_session->start() mState= %d \n", mState);
        mCurrentSourceType = kDvrFileSrc;
    }
    break;


    case kDvrFileSrc:
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "\n Dvr::SetSpeed  Not FirstTime kDvrFileSrc\n");
        if ((setSpeedMode(numerator, denominator, &playSpeed)) == kMspStatus_Ok)
        {
            if (mPtrFileSource)
            {
                status = mPtrDispSession->setMediaSpeed(playSpeed);
                if (status != kMspStatus_Ok)
                {
                    dlog(DL_MSP_DVR, DLOGL_ERROR, "cpe_media_set failed for disp_session->setSpeed, Error = %d\n", status);
                    return kMediaPlayerStatus_Error_InvalidParameter;
                }
                mPtrFileSource->setSpeed(playSpeed, 0); //ARUN.HACK.Moving set speed after start, as reverse order not seems working in G8

            }
            else
            {
                dlog(DL_MSP_DVR, DLOGL_ERROR, "Error: Null mPtrFileSource ");
            }
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "SetSpeed:: Not Set--- Wrong Parameters \n");
            return kMediaPlayerStatus_Error_InvalidParameter;
        }

        mPtrDispSession->controlMediaAudio(playSpeed);

        break;

    default :
        dlog(DL_MSP_DVR, DLOGL_ERROR, " Calling Set Speed in out of state Not LiveSrc/Not FileSrc \n");
        break;

    }
    return  kMediaPlayerStatus_Ok;
}

/** *********************************************************
*/
eMspStatus Dvr::setDisplay()
{

    FNLOG(DL_MSP_DVR);

    if (mPtrDispSession != NULL)
    {
        // disp_session->SetCCICallback(mCBData, mCCICBFn);
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, " disp_session->setVideoWindow");
        mPtrDispSession->setVideoWindow(mScreenRect, mEnaAudio);
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, " disp_session->updatePids ");
        mPtrDispSession->updatePids(mPtrPsi);
        mPtrDispSession->open(mPtrFileSource); // TODO: check returns here
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, " Success disp_session->updatePids");
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, " disp_session->open ");
    }

    return  kMspStatus_Ok;
}

/** *********************************************************
    \returns Both *pNumerator and *pDenominator are set to 1 since set speed is not supported
*/
eIMediaPlayerStatus Dvr::GetSpeed(int* pNumerator, unsigned int* pDenominator)
{
    FNLOG(DL_MSP_DVR);


    if (mState != kDvrStateRendering)
    {
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "GetSpeed during wrong state. state: %d", mState);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if ((pNumerator == NULL) || (pDenominator == NULL))
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "NULL *numerator or *denominator in GetSpeed");
        return  kMediaPlayerStatus_Error_InvalidParameter;
    }

    *pNumerator = mSpeedNumerator;
    *pDenominator = mSpeedDenominator;

    return  kMediaPlayerStatus_Ok;
}

/** *********************************************************
    \returns always returns kMediaPlayerStatus_Error_NotSupported
*/
eIMediaPlayerStatus Dvr::SetPosition(float nptTime)
{
    uint32_t npt = 0;
    int numerator = 100;
    unsigned int denominator = 100;
    tCpePlaySpeed playSpeed;
    char myFileName[MAX_FILE_SIZE];
    FNLOG(DL_MSP_DVR);

    dlog(DL_MSP_DVR, DLOGL_NOISE, " SetPosition pNptTime = %f  ", nptTime);

    if (nptTime == MEDIA_PLAYER_NPT_START)
    {
        return kMediaPlayerStatus_Ok;  //ignore this.   //temporary workaround to avoid trickplay problem,due to ANT calling wrong.TODO-Remove this,once ANT resolves issue
    }
    else if (nptTime == MEDIA_PLAYER_NPT_NOW)
    {
        return kMediaPlayerStatus_Ok;  //ignore this.
    }
    else if (nptTime == MEDIA_PLAYER_NPT_END)
    {
        if (mPtrLiveSource && mPtrDispSession && mPtrPsi)
        {
            mPtrDispSession->freeze();
            if (mPtrFileSource != NULL)
            {
                mPtrFileSource->stop();
            }

            CloseDisplaySession();

            if (mPtrFileSource != NULL)
            {
                delete mPtrFileSource;
                mPtrFileSource = NULL;
            }
            LOG(DLOGL_REALLY_NOISY, "Set kDvrLiveSrc");
            mCurrentSourceType = kDvrLiveSrc;

            if (!mPtrLiveSource->isAnalogSource())
                mPtrPsi->psiStart(mPtrLiveSource);
            else
            {
                LOG(DLOGL_NORMAL, "Analog channel, Start Live display");
                queueEvent(kDvrPSIReadyEvent);
            }

            return  kMediaPlayerStatus_Ok;
        }

        else
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "There is no Live Source OR DisplaySession mPtrLiveSource = NULL");
            return kMediaPlayerStatus_Error_Unknown;
        }
    }

    else
    {
        if (nptTime < 0)
        {
            LOG(DLOGL_ERROR, "NPT Time is Negative!");
            return kMediaPlayerStatus_Error_Unknown; //NPT can't be negative.other NPT_END
        }

        npt = (uint32_t)(nptTime * 1000);          //assigning the value passed by ANT/converting to milliseconds
        if (NULL == mPtrFileSource)
        {
            if (mPtrDispSession == NULL)
            {
                LOG(DLOGL_ERROR, "Displaysession is NULL.Skipping position setting");
                return kMediaPlayerStatus_Error_Unknown; //If display session is null, no need to go ahead
            }

            //currently not having condition checking for nptTime > max possible.,its not possible with recorded video.
            //I assume,that condition might be handled by CPERP itself.. ARUN

            LOG(DLOGL_REALLY_NOISY, "NPT  Time *****************: %d", npt);

            if ((mPtrLiveSource == NULL) || !(mPtrLiveSource->canRecord()))
            {
                // If source is not recordable then we wont have TSB & we cant create it.
                return kMediaPlayerStatus_ServiceNotAvailable;
            }
            if (mTsbState == kTsbNotCreated)  //This handle the scenario,where user tries trickplay even before dwell time expires(creating TSB).
            {
                // Start TSB
                eMspStatus status = StartTSBRecordSession("");
                if (status == kMspStatus_NotSupported)
                {
                    LOG(DLOGL_ERROR, "Music channel with no video.Recording not supported");
                }
                else if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "StartTSBRecordSession error %d", status);
                    return kMediaPlayerStatus_ServiceNotAvailable;
                }
                else
                {
                    mTsbState = kTsbCreated;
                    AddLiveSession();
                    LOG(DLOGL_NOISE, "TSB created successfully in case of user pressing trickplay before dwell time");
                }
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "TSB already started");
            }
            if (mPtrRecSession)
            {
                char *tsbFileName = mPtrRecSession->GetTsbFileName();
                if (tsbFileName)
                {
                    strncpy(myFileName, tsbFileName, MAX_FILE_SIZE);
                    myFileName[MAX_FILE_SIZE - 1] = '\0';
                }
                else
                {
                    LOG(DLOGL_ERROR, "Error: null tsbFileName");
                    return kMediaPlayerStatus_ServiceNotAvailable;
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "Error: null mPtrRecSession");
                return kMediaPlayerStatus_ServiceNotAvailable;
            }
            mFileName.assign(myFileName);
            mFileName.insert(0, "avfs:/");
            LOG(DLOGL_REALLY_NOISY, "TSB myFileName: %s", myFileName);
            LOG(DLOGL_REALLY_NOISY, "TSB mFileName:  %s", mFileName.c_str());
            eMSPSourceType SrcType;
            SrcType = MSPSourceFactory::getMSPSourceType(mFileName.c_str());
            MSPSource *source = MSPSourceFactory::getMSPSourceInstance(SrcType, mFileName.c_str(), mIMediaPlayerSession);
            if (source && source->isDvrSource())
            {
                mPtrFileSource = source;
            }
            else
            {
                LOG(DLOGL_ERROR, "Error: null invalid URL");
                return kMediaPlayerStatus_Error_InvalidURL;
            }
            if ((mPtrFileSource == NULL) || (mPtrPsi == NULL))
            {
                LOG(DLOGL_ERROR, "Error: Null mPtrFileSource or mPtrPsi");
                return kMediaPlayerStatus_ServiceNotAvailable;
            }

            eMspStatus status = mPtrFileSource->load(sourceCB, this);
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "Error: File source is not loading. Returning from here status = %d", status);
                return kMediaPlayerStatus_Error_Unknown;
            }
            // background recordings always start as TSB, this will get updated when convert tsb gets called
            eMspStatus mStatus =  mPtrFileSource->open(kRMPriority_ForegroundRecording);
            if (mStatus != kMspStatus_Ok)
            {
                StopRecordSession();
                if (mPtrFileSource != NULL)
                {
                    mPtrFileSource->stop();
                    delete mPtrFileSource;
                    mPtrFileSource = NULL;
                }

                return kMediaPlayerStatus_Error_Unknown;

            }
            else
            {
                mPtrDispSession->freeze();
                mPtrDispSession->stop();
                mPtrDispSession->close(mEasAudioActive);
                mPtrPsi->psiStart(mFileName);
                setDisplay();
                setSpeedMode(numerator, denominator, &playSpeed);
                if (npt <= 1000)
                {
                    mPtrFileSource->setSpeed(playSpeed, 2);
                }
                else
                {
                    mPtrFileSource->setSpeed(playSpeed, (npt - 1000));
                }
                status = mPtrDispSession->setMediaSpeed(playSpeed);
                if (status != kMspStatus_Ok)
                {
                    dlog(DL_MSP_DVR, DLOGL_ERROR, "cpe_media_set failed for disp_session->setSpeed, Error = %d\n", status);
                    return kMediaPlayerStatus_Error_InvalidParameter;
                }
                if (playSpeed.mode != eCpePlaySpeedMode_Pause)
                {
                    mPtrDispSession->start(mEasAudioActive);
                    mPtrFileSource->start();
                    mPtrDispSession->controlMediaAudio(playSpeed);
                    dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, " Success disp_session->start() mState= %d \n", mState);
                }
                mCurrentSourceType = kDvrFileSrc;
                return  kMediaPlayerStatus_Ok;
            }
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Already in file source");
        }

    }

    if (mPtrFileSource != NULL)
    {
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Set Position npt value is %d\n", npt);
        eMspStatus status = mPtrFileSource->setPosition(npt);
        if (status != kMspStatus_Ok)
        {
            if (status == kMspStatus_Loading) // Interrupted Playback so we need to move to next file
            {
                dlog(DL_MSP_DVR, DLOGL_NOISE, "Need to be applied later\n");
                mNptSetPos = nptTime;
                return kMediaPlayerStatus_Ok;
            }
            else
            {
                dlog(DL_MSP_DVR, DLOGL_ERROR, "cpe_src_Set failed to set eCpeSrcNames_CurrentNPT, Error = %d\n", status);
                return kMediaPlayerStatus_Error_Unknown;
            }
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, " cpe_src_Set(...) success !!!\n");
        }
    }
    else
    {
        return kMediaPlayerStatus_Error_Unknown;   //NPT can't be negative.other NPT_END
    }
    return  kMediaPlayerStatus_Ok;
}

/** *********************************************************
    \returns always returns kMediaPlayerStatus_Error_NotSupported
*/
eIMediaPlayerStatus Dvr::GetPosition(float* pNptTime)
{

    FNLOG(DL_MSP_DVR);
    static float sFirstPausePosition = 0.0; //zero by default
    if (pNptTime)
    {
        if ((mCurrentSourceType == kDvrFileSrc) && (mPtrFileSource != NULL))
        {
            eMspStatus status = mPtrFileSource->getPosition(pNptTime);
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "Get Position Failed");
                return kMediaPlayerStatus_ContentNotFound;
            }
            else if (*pNptTime == 0.0)  //indication of first pause (frozen)
            {
                if (sFirstPausePosition == 0.0) //query from record session about frozen position
                {
                    uint32_t npt;
                    if (mPtrRecSession != NULL)
                    {
                        eMspStatus status;
                        status = mPtrRecSession->GetCurrentNpt(&npt);
                        if (status != kMspStatus_Ok)
                        {
                            dlog(DL_MSP_DVR, DLOGL_ERROR, "Failed to get current recording position in File Source case, Error = %d", status);
                            return kMediaPlayerStatus_ServiceNotAvailable;
                        }
                        else
                        {
                            sFirstPausePosition = *pNptTime = (float)(npt / 1000);
                            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Get Position from Record Session in File Source case is  %f\n", *pNptTime);
                            return kMediaPlayerStatus_Ok;
                        }
                    }
                }
                else  //just return the frozen position for subsequent queries.
                {
                    *pNptTime = sFirstPausePosition;
                    dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Get Position in File Source case is  %f\n", *pNptTime);
                    return kMediaPlayerStatus_Ok;
                }
            }  //file source is started. reset the "first pause position variable" back to zero.
            else
            {
                sFirstPausePosition = 0.0;
                dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "GetPosition = %f", *pNptTime);
                return  kMediaPlayerStatus_Ok;
            }
        }
        else
        {
            /* In case we are in LIVE mode we should return Current position from Record Session */
            uint32_t npt;
            sFirstPausePosition = 0.0;
            if (mPtrRecSession != NULL)
            {
                eMspStatus status;
                status = mPtrRecSession->GetCurrentNpt(&npt);
                if (status != kMspStatus_Ok)
                {
                    dlog(DL_MSP_DVR, DLOGL_ERROR, "Failed to get current recording position, Error = %d", status);
                    return kMediaPlayerStatus_ServiceNotAvailable;
                }
                else
                {
                    *pNptTime = (float)(npt / 1000);
                    dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Get Position from Record Session is  %f\n", *pNptTime);
                    return kMediaPlayerStatus_Ok;
                }

            }
        }
    }

    return  kMediaPlayerStatus_Error_OutOfState;
}

/** *********************************************************
*/
eIMediaPlayerStatus Dvr::IpBwGauge(const char *sTryServiceUrl, unsigned int *pMaxBwProvision, unsigned int *pTryServiceBw, unsigned int *pTotalBwCoynsumption)
{
    FNLOG(DL_MSP_DVR);
    UNUSED_PARAM(sTryServiceUrl)
    UNUSED_PARAM(pMaxBwProvision)
    UNUSED_PARAM(pTryServiceBw)
    UNUSED_PARAM(pTotalBwCoynsumption)

    LOG(DLOGL_ERROR, "WARNING IpBwGauge NOT SUPPORTED!!");

    return  kMediaPlayerStatus_Ok;
}

/** *********************************************************
*/
eIMediaPlayerStatus Dvr::SetPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus)
{
    FNLOG(DL_MSP_DVR);

    if (!vidScreenRect)
    {
        LOG(DLOGL_ERROR, "Error: NULL vidScreenRect");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    LOG(DLOGL_REALLY_NOISY, "vidScreenRect x: %d  y: %d  w: %d  h: %d  audio: %d",
        vidScreenRect->x, vidScreenRect->y, vidScreenRect->width, vidScreenRect->height, enableAudioFocus);

    // translate rects (without really moving it anywhere)
    mScreenRect.x = vidScreenRect->x;
    mScreenRect.y = vidScreenRect->y;
    mScreenRect.w = vidScreenRect->width;
    mScreenRect.h = vidScreenRect->height;

    mEnaPicMode = enablePictureModeSetting;
    mEnaAudio = enableAudioFocus;
    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s, %d, audioFocus = %d", __FUNCTION__, __LINE__, mEnaAudio);
    if (mPtrLiveSource != NULL)
    {
        if (mPtrLiveSource->isPPV() == true)
        {
            LOG(DLOGL_NOISE, "Saving the tuner priority to PPV video or recording priority  ");
            mTunerPriority = kRMPriority_PpvVideoPlayOrRecording;
        }
        else
        {

            if (mPersistentRecordState != kPersistentRecordNotStarted)
            {
                if (mEnaAudio)
                {
                    LOG(DLOGL_NOISE, "Saving the tuner priority to kRMPriority_ForegroundRecording ");
                    mTunerPriority = kRMPriority_ForegroundRecording;
                }
                else
                {
                    LOG(DLOGL_NOISE, "Saving the tuner priority to kRMPriority_BackgroundRecording ");
                    mTunerPriority = kRMPriority_BackgroundRecording;
                }
            }
            else
            {
                if (mEnaAudio)
                {
                    LOG(DLOGL_NOISE, "Saving the tuner priority to kRMPriority_VideoWithAudioFocus ");
                    mTunerPriority = kRMPriority_VideoWithAudioFocus;
                }
                else
                {
                    LOG(DLOGL_NOISE, "Saving the tuner priority to kRMPriority_VideoWithoutAudioFocus ");
                    mTunerPriority = kRMPriority_VideoWithoutAudioFocus;
                }
            }
        }

    }
    else
    {
        LOG(DLOGL_NOISE, "warning: null mPtrLiveSource");
    }

    // TODO: check rectangle validity?

    if (mPtrDispSession)
    {
        mPtrDispSession->setVideoWindow(mScreenRect, mEnaAudio);
    }
    else
    {
        LOG(DLOGL_NOISE, "warning: null mPtrDispSession - not setting video window");
    }

    return  kMediaPlayerStatus_Ok;
}



/** *********************************************************
*/
eIMediaPlayerStatus Dvr::GetStartPosition(float* pNptTime)
{
    FNLOG(DL_MSP_DVR);

    if (!pNptTime)
    {
        LOG(DLOGL_ERROR, "Error null pNptTime");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    eIMediaPlayerStatus retStatus = kMediaPlayerStatus_Ok;

    if (mPtrRecSession)
    {
        uint32_t npt;

        eMspStatus getStartStatus = mPtrRecSession->GetStartNpt(&npt);
        if (getStartStatus != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "GetStartNpt error %d", getStartStatus);
            retStatus =  kMediaPlayerStatus_ServiceNotAvailable;
        }
        else
        {
            *pNptTime = (float)(npt / 1000);
            LOG(DLOGL_REALLY_NOISY, "Start Position: %f", *pNptTime);
        }
    }
    else
    {
        *pNptTime = 0;
    }

    return retStatus;
}

/** *********************************************************
    \returns always returns kMediaPlayerStatus_Error_NotSupported
*/
eIMediaPlayerStatus Dvr::GetEndPosition(float* pNptTime)
{
    uint32_t npt;
    eMspStatus status = kMspStatus_Ok;
    FNLOG(DL_MSP_DVR);

    if (pNptTime && mPtrRecSession)
    {
        status = mPtrRecSession->GetCurrentNpt(&npt);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "GetCurrentNpt error %d", status);
            return kMediaPlayerStatus_ServiceNotAvailable;
        }
        else
        {
            *pNptTime = (float)(npt / 1000);
            LOG(DLOGL_REALLY_NOISY, "End Position: %f", *pNptTime);
            return kMediaPlayerStatus_Ok;
        }
    }
    return kMediaPlayerStatus_Error_InvalidParameter;
}


/** *********************************************************
*/

eIMediaPlayerStatus Dvr::SetCallback(IMediaPlayerSession *pIMediaPlayerSession, IMediaPlayerStatusCallback cb, void* pClientContext)
{
    FNLOG(DL_MSP_DVR);
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


eIMediaPlayerStatus Dvr::DetachCallback(IMediaPlayerStatusCallback cb)
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


/** *********************************************************
*/
Dvr::~Dvr()
{
    eMspStatus status;
    FNLOG(DL_MSP_DVR);

// TODO:  make a common function for here and eject

    std::list<CallbackInfo*>::iterator iter;
    LOG(DLOGL_NOISE, "LOCKING mCallbackList mutex");
    pthread_mutex_lock(&sListMutex);
    LOG(DLOGL_NOISE, "SIZE=%d", mCallbackList.size());
    for (iter = mCallbackList.begin(); iter != mCallbackList.end(); iter++)
    {
        delete(*iter);
        *iter = NULL;
    }

    mCallbackList.clear();
    LOG(DLOGL_NOISE, "AFTER SIZE=%d", mCallbackList.size());
    pthread_mutex_unlock(&sListMutex);
    LOG(DLOGL_NOISE, "UNLOCKED mCallbackList mutex");

    if (mDwellTimerThread)
    {
        pthread_join(mDwellTimerThread, NULL);       // wait for dwell timer thread to exit,if it was created.
        mDwellTimerThread = 0;
    }

    if (mEventHandlerThread)
    {
        queueEvent(kDvrEventExit);  // tell thread to exit
        unLockMutex();
        pthread_join(mEventHandlerThread, NULL);      // wait for event thread to exit
        lockMutex();
        mEventHandlerThread = 0;
    }

    if (mPtrFileSource != NULL)
    {
        mPtrFileSource->stop();
    }


    // handle the case where normal eject did not happen, and make sure things get cleaned up
    CloseDisplaySession();

    if (mPtrRecSession != NULL)
    {
        mTsbHardDrive = -1;
        mPtrRecSession->stop();
        mPtrRecSession->unregisterRecordCallback();
        mPtrRecSession->close();
        mTsbState = kTsbNotCreated;
        mIsPatPmtInjected = false;
        delete mPtrRecSession;
        mPtrRecSession = NULL;
    }

    mPersistentRecordState = kPersistentRecordNotStarted;

    /*
    Delete psi after deleting RecordSession as RecordSession has reference to PMT data.
    */
    StopDeletePSI();
    if (mPtrAnalogPsi)
    {
        StopDeleteAnalogPSI();
    }
    //
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
        mPtrLiveSource->stop();
        delete mPtrLiveSource;
        mPtrLiveSource = NULL;
    }

    if (mPtrFileSource != NULL)
    {
        delete mPtrFileSource;
        mPtrFileSource = NULL;
    }

    LOG(DLOGL_REALLY_NOISY, "delete threadEventQueue");
    if (mThreadEventQueue)
    {
        delete mThreadEventQueue;
        mThreadEventQueue = NULL;
    }
    // pthread_mutex_destroy(&mMutex);

}

/** *********************************************************
*/
Dvr::Dvr(IMediaPlayerSession *pIMediaPlayerSession)
{
    FNLOG(DL_MSP_DVR);
    mDestUrl = "";
    mPtrDispSession = NULL;
    mPtrPsi = NULL;
    mPtrAnalogPsi = NULL;
    mState = kDvrStateIdle;
    mCurrentSourceType = kDvrNoSrc;
    mCurrentSource = kMSPInvalidSource;
    mTsbState = kTsbNotCreated;
    mIsPatPmtInjected = false;
    mPersistentRecordState = kPersistentRecordNotStarted;
    mFileName = "";
    mPtrLiveSource = NULL;
    mPtrFileSource = NULL;

    // create event queue for scan thread
    mThreadEventQueue = new MSPEventQueue();
    mPtrDispSession  = NULL;
    mPtrPsi = new Psi();
    mPtrRecSession = NULL;
    mEventHandlerThread = 0;
    mDwellTimerThread = 0;

    mScreenRect.x = 0;
    mScreenRect.y = 0;
    mScreenRect.w = 1280;  //updated window size to HD resolution.TODO -this hardcoding had to be removed.
    mScreenRect.h = 720;
    mEnaPicMode = true;
    mEnaAudio = false;
    pthread_mutexattr_t mta;
    pthread_mutexattr_init(&mta);
    pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mMutex, &mta);
    mPtrCBData = NULL;
    mCCICBFn = NULL;
    mTsbNumber = 0xffff;
    mTsbHardDrive = -1;
    mCCIbyte = 0;
    mRecStartTime = 0;
    mRecStopTime = 0;
    mSpeedNumerator = 100;
    mSpeedDenominator = 100;
    mNptSetPos = 0;
    mIsBackground = false;
    mIMediaPlayerSession = pIMediaPlayerSession;
    mAppClientsList.clear();
    mPendingTrickPlay = false;
    mTrickNum = 100;
    mTrickDen = 100;
    mTunerPriority = kRMPriority_EasVideo;
    mPendingRecPriority = kRMPriority_EasVideo;
    mPackets = 0;
}



/** *********************************************************
*/
eIMediaPlayerStatus Dvr::DoCallback(eIMediaPlayerSignal sig, eIMediaPlayerStatus stat)
{
    FNLOG(DL_MSP_DVR);
    tIMediaPlayerCallbackData cbData;

    cbData.data[0] = '\0';

    LOG(DLOGL_NOISE, "signal %d status %d callback size=%d locking mutex", sig, stat, mCallbackList.size());
    pthread_mutex_lock(&sListMutex);
    LOG(DLOGL_NOISE, "signal %d status %d callback size=%d after lock mutex", sig, stat, mCallbackList.size());

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
    pthread_mutex_unlock(&sListMutex);
    return  kMediaPlayerStatus_Ok;
}
/** *********************************************************
*/
eMspStatus Dvr::queueEvent(eDvrEvent evtyp)
{
    if (!mThreadEventQueue)
    {
        LOG(DLOGL_ERROR, "error: no event queue");
        return kMspStatus_BadParameters;
    }

    mThreadEventQueue->dispatchEvent(evtyp);
    return kMspStatus_Ok;
}

void Dvr::lockMutex(void)
{
    pthread_mutex_lock(&mMutex);
}

void Dvr::unLockMutex(void)
{
    pthread_mutex_unlock(&mMutex);
}

eIMediaPlayerStatus Dvr::RegisterCCICallback(void* data, CCIcallback_t cb)
{
    if (mPtrDispSession)
    {
        mPtrDispSession->SetCCICallback(data, cb);
    }
    if (mPtrRecSession)
    {
        mPtrRecSession->SetCCICallback(data, cb);

    }
    mPtrCBData = data;
    mCCICBFn = cb;
    return kMediaPlayerStatus_Ok;
}

eIMediaPlayerStatus Dvr:: UnRegisterCCICallback()
{
    if (mPtrDispSession)
    {
        mPtrDispSession->UnSetCCICallback();
    }
    mPtrCBData = NULL;
    mCCICBFn = NULL;
    return kMediaPlayerStatus_Ok;
}

std::string Dvr::GetSourceURL(bool liveSrcOnly)const
{
    std::string srcURL;
    if (!liveSrcOnly && (mCurrentSourceType == kDvrFileSrc))
    {
        if (mPtrFileSource)
        {
            srcURL = mPtrFileSource->getSourceUrl();
        }
    }
    else
    {
        if (mPtrLiveSource)
        {
            srcURL = mPtrLiveSource->getSourceUrl();
        }
    }
    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Dvr::GetSourceURL getSourceUrl:%s\n", srcURL.c_str());

    return srcURL;
}


std::string Dvr::GetDestURL()const
{
    return mDestUrl;
}

bool Dvr::isRecordingPlayback() const
{
    return (mCurrentSourceType == kDvrFileSrc);
}

bool Dvr::isLiveSourceUsed() const
{
    return (mPtrLiveSource != NULL);
}

bool Dvr::isLiveRecording() const
{
    return (mPersistentRecordState != kPersistentRecordNotStarted);
}


eIMediaPlayerStatus Dvr::SetApplicationDataPid(uint32_t aPid)
{
    eMspStatus status = kMspStatus_Ok;

    //If dvr is not in stopped state, then only proceed
    if ((kDvrStateStop != mState) && (mPtrDispSession))
    {
        mPackets = 0;
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "ApplicationData Instance = %p, pid = %d", mPtrDispSession->getAppDataInstance(), aPid);
        if (mPtrLiveSource)
        {
            status = mPtrDispSession->filterAppDataPid(aPid, appDataReadyCallbackFn, this, mPtrLiveSource->isMusic());
        }
        else
        {
            // if the source is not live, then the source cannot be a music channel
            status = mPtrDispSession->filterAppDataPid(aPid, appDataReadyCallbackFn, this, false);
        }

        if (status == kMspStatus_Ok)
            return kMediaPlayerStatus_Ok;
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Error Set Application Data Pid\n");
            return kMediaPlayerStatus_Error_NotSupported;
        }
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "mPtrDispSession = %p, state = %d\n", mPtrDispSession, mState);
        return kMediaPlayerStatus_Error_Unknown;
    }

}

eIMediaPlayerStatus Dvr::SetApplicationDataPidExt(IMediaPlayerClientSession *ApplnClient)
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
    return kMediaPlayerStatus_Error_Unknown;

}

eIMediaPlayerStatus Dvr::GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset)
{
    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Inside Dvr::GetComponents");
    if (infoSize == 0 || info == NULL || count == NULL)
    {
        return kMediaPlayerStatus_Error_InvalidParameter;
    }
    if (mPtrAnalogPsi)
    {
        eMspStatus status = mPtrAnalogPsi->getComponents(info, infoSize, count, offset);
        if (status == kMspStatus_Ok)
        {
            return kMediaPlayerStatus_Ok;
        }
        else
            return kMediaPlayerStatus_ContentNotFound;

    }
    else if (mPtrPsi)
    {
        mPtrPsi->getComponents(info, infoSize, count, offset);
        if (*count == 0)
        {
            return kMediaPlayerStatus_ContentNotFound;
        }
        else
        {
            return kMediaPlayerStatus_Ok;
        }
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "PSI/PMT Info Not found");
        return kMediaPlayerStatus_ContentNotFound;
    }

}


eIMediaPlayerStatus Dvr::GetApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Inside GetApplicationData\n");

    if (mPtrDispSession && mPtrDispSession->getAppDataInstance())
    {
        mPtrDispSession->getApplicationData(bufferSize, buffer, dataSize);
        if (dataSize == 0)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Data Not found\n");
            return kMediaPlayerStatus_ContentNotFound;
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Data Returned of Buffer Size %d, Data Size %d", bufferSize, *dataSize);
            return kMediaPlayerStatus_Ok;
        }
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Display Session is not Initialized\n");
        return kMediaPlayerStatus_Error_OutOfState;
    }
}

eIMediaPlayerStatus Dvr::GetApplicationDataExt(IMediaPlayerClientSession *ApplnClient, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
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

uint32_t Dvr::GetSDVClentContext(IMediaPlayerClientSession *ApplnClient)
{
    FNLOG(DL_MSP_MPLAYER);
    if (NULL != ApplnClient)
        return ApplnClient->getSDVClentContext();
    else
        return 0;

}

eIMediaPlayerStatus Dvr::SetAudioPid(uint32_t aPid)
{
    LOG(DLOGL_REALLY_NOISY, "PID: 0x%x", aPid);

    if (mPtrDispSession)
    {
        eMspStatus status = mPtrDispSession->updateAudioPid(mPtrPsi, aPid);
        if (status == kMspStatus_Ok)
        {
            return kMediaPlayerStatus_Ok;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: null mPtrDispSession");
    }

    return kMediaPlayerStatus_Error_OutOfState;
}


/** *********************************************************
*/
void Dvr::displaySessionCallbackFunction(eIMediaPlayerSignal sig, eIMediaPlayerStatus stat)
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


void Dvr::audioLanguageChangedCB(void *aCbData)
{
    FNLOG(DL_MSP_MPLAYER);
    Dvr *inst = (Dvr *) aCbData;

    if (inst)
    {
        inst->queueEvent(kDvrEventAudioLangChangeCb);
    }
}

void Dvr::SapChangedCB(void *aCbData)
{
    FNLOG(DL_MSP_MPLAYER);
    Dvr *inst = (Dvr *) aCbData;

    if (inst && inst->mPtrDispSession)
    {
        inst->mPtrDispSession->SapChangedCb();
    }
}

void Dvr::appDataReadyCallbackFn(void *aClientContext)
{
    FNLOG(DL_MSP_MPLAYER);
    int i = 0;
    Dvr *inst = (Dvr *)aClientContext;
    if (inst)
        i = inst->bump_packets();
    LOG(DLOGL_NOISE, "%s  return from bump packets i=%d", __FUNCTION__, i);
    if (i < MAX_FILTERING_PACKETS)
    {
        if (inst)
        {
            LOG(DLOGL_NOISE, "%s  Queueing event i=%d", __FUNCTION__, i);
            inst->queueEvent(kDvrAppDataReady);
        }
    }
    else
        LOG(DLOGL_NOISE, "Filtering callback - max packets exceeded=%d", i);

}

int Dvr::bump_packets(void)
{
    mPackets++;
    return (mPackets);
}



void Dvr::tearDownToRetune()
{
    // Stop Current video streaming.
    if (mEnaAudio)
    {
        CurrentVideoMgr::instance()->CurrentVideo_RemoveLiveSession();
    }

    eMspStatus status;
    if (mPtrFileSource != NULL)
    {
        mPtrFileSource->stop();
    }

    CloseDisplaySession();

    if (mPtrRecSession != NULL)
    {
        if (mPersistentRecordState == kPersistentRecordStarted)
        {
            mPersistentRecordState = kPersistentRecordNotStarted;

            status = mPtrRecSession->stopConvert();
            if (status != kMspStatus_Ok)
            {
                dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d TSB Conversion Stop failed", __FUNCTION__, __LINE__);
            }

        }
        status = StopRecordSession();
        if (status != kMspStatus_Ok)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "Record session unregister callback failed\n");
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
    }

    if (mPtrLiveSource != NULL)
    {
        mPtrLiveSource->stop();    //TODO status check
    }

    if (mDwellTimerThread)
    {
        pthread_join(mDwellTimerThread, NULL);       // wait for dwell timer thread to exit,if it was created.
        mDwellTimerThread = 0;
    }
}

eIMediaPlayerStatus Dvr::CloseDisplaySession()
{
    FNLOG(DL_MSP_MPLAYER);
    if (mPtrDispSession)
    {
        mPtrDispSession->UnSetCCICallback();
        mPtrDispSession->clearCallback(mDisplaySessioncallbackConnection);

        mPtrDispSession->SetAudioLangCB(NULL, NULL);

        mPtrDispSession->SetSapChangedCB(NULL,  NULL) ;

        mPtrDispSession->stop();
        mPtrDispSession->close(mEasAudioActive);
        delete mPtrDispSession;
        mPtrDispSession = NULL;
    }
    else
    {
        LOG(DLOGL_NOISE, "Disp Session is NULL");
    }
    mState = kDvrSourceReady;

    return kMediaPlayerStatus_Ok;
}


eMspStatus Dvr::SetDisplaySession()
{
    FNLOG(DL_MSP_MPLAYER);
    int cpeStatus;

    if (mPtrDispSession != NULL)
    {
        CloseDisplaySession();
    }

    if (mPtrFileSource && mNptSetPos != 0)
    {
        dlog(DL_MSP_DVR, DLOGL_NOISE, "Calling Set Position\n");
        SetPosition(mNptSetPos);
        mNptSetPos = 0;
    }

    mPtrDispSession = new DisplaySession();

    mDisplaySessioncallbackConnection = mPtrDispSession->setCallback(boost::bind(&Dvr::displaySessionCallbackFunction, this, _1, _2));

    mPtrDispSession->registerDisplayMediaCallback(this, mediaCB);

    MSPSource *source = NULL;
    if (mCurrentSourceType == kDvrLiveSrc)
    {
        source = mPtrLiveSource;
    }
    else
    {
        source = mPtrFileSource;
    }
    if (!mPtrAnalogPsi)
    {
        dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "SetCCICallback is called from setdisplaysession");
        mPtrDispSession->SetCCICallback(mPtrCBData, mCCICBFn);
    }

    if (mEnaAudio) // Check if its Pip Session
    {
        mPtrDispSession->SetAudioLangCB(this, audioLanguageChangedCB);
    }
    if (mPtrAnalogPsi && mEnaAudio)
    {
        mPtrDispSession->SetSapChangedCB(this,  SapChangedCB) ;
    }

    mPtrDispSession->setVideoWindow(mScreenRect, mEnaAudio);

    if (!mPtrAnalogPsi || (source && !source->isAnalogSource()))
        mPtrDispSession->updatePids(mPtrPsi);

    if (mPtrAnalogPsi && source && source->isAnalogSource())
        mPtrDispSession->open(source, 1);
    else
        mPtrDispSession->open(source);

    if (mPtrFileSource)
    {
        if (mSpeedNumerator < 0)
        {
            tCpeRecordedFileInfo recordingExtendedInfo;
            std::string filename = mPtrFileSource->getFileName();
            filename = filename.substr(strlen("avfs://"));
            /*Read the attibutes from fuse FS*/
            int ret = getxattr(filename.c_str(), kCpeRec_ExtendedAttr, (void*)&recordingExtendedInfo, sizeof(tCpeRecordedFileInfo));
            if (ret == -1)
            {
                LOG(DLOGL_ERROR, "Error reading duration of recfile: %s  ret: %d", filename.c_str(), ret);
            }
            else
            {
                LOG(DLOGL_NOISE, "File duration: %d secs", recordingExtendedInfo.lengthInSeconds);
                uint32_t nptLength = recordingExtendedInfo.lengthInSeconds * 1000;
                cpeStatus = cpe_src_Set(mPtrFileSource->getCpeSrcHandle(), eCpeSrcNames_CurrentNPT, (void *) & (nptLength));
                if (cpeStatus != kCpe_NoErr)
                {
                    LOG(DLOGL_ERROR, "cpe_src_set for get handle error code %d", cpeStatus);
                    return kMspStatus_Error;
                }
            }
        }
        SetSpeed(mSpeedNumerator, mSpeedDenominator);
    }
    return kMspStatus_Ok;
}


eIMediaPlayerStatus Dvr::StartDisplaySession()
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status;

    status = SetDisplaySession();
    if (status != kMspStatus_Ok)
    {
        return kMediaPlayerStatus_Error_Unknown;
    }

    if (mTsbState == kTsbCreated || mTsbState == kTsbStarted)
    {
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "Calling AddLiveSession on StartDisplaySession");
        AddLiveSession();
    }

    mPtrDispSession->start(mEasAudioActive);

    return kMediaPlayerStatus_Ok;

}

eMspStatus Dvr::StartTSBRecordSession(std::string first_fragment_file)
{
    FNLOG(DL_MSP_DVR);
    eMspStatus status;

    TsbHandler *tsbhandler = TsbHandler::getTsbHandlerInstance();
    assert(tsbhandler);

    LOG(DLOGL_MINOR_DEBUG, "get_tsb mTsbNumber: %d", mTsbNumber);

    if (mPtrRecSession == NULL) //sometimes, Record session might be attempted to create more than once,if previous attempt failed.
    {
        eMspStatus status = tsbhandler->get_tsb(&mTsbNumber);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Error: No available TSB");
            return status;
        }

        mPtrRecSession = new MSPRecordSession();
    }

    if (mPtrRecSession && mPtrLiveSource)
    {
        LOG(DLOGL_MINOR_DEBUG, "mPtrRecSession: %p  call open with mTsbHardDrive: %d  mTsbNumber: %d" ,
            mPtrRecSession, mTsbHardDrive, mTsbNumber);

        if (mPtrLiveSource->isAnalogSource())
        {
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s %d This is ANALOG Source ...So using ANALOG PSI \n", __FILE__, __LINE__);
            status = mPtrRecSession->open(mPtrLiveSource->getCpeSrcHandle(), &mTsbHardDrive, mTsbNumber, mPtrAnalogPsi,
                                          mPtrLiveSource->getSourceId(), first_fragment_file);
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s %d This is Digital Source ...So using Digital PSI \n", __FILE__, __LINE__);
            status = mPtrRecSession->open(mPtrLiveSource->getCpeSrcHandle(), &mTsbHardDrive, mTsbNumber, mPtrPsi,
                                          mPtrLiveSource->getSourceId(), first_fragment_file);
        }

        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Error: mPtrRecSession->open ");
            return status;
        }

        LOG(DLOGL_MINOR_DEBUG, "after open mTsbHardDrive: %d" , mTsbHardDrive);

        status = mPtrRecSession->registerRecordCallback(recordSessionCallback, this);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Error registerRecordCallback");
            return status;
        }

        if (mPtrAnalogPsi && mPtrLiveSource && mPtrLiveSource->isAnalogSource())
        {
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s %d This is ANALOG Source ...So using ANALOG PSI to Start Recording Session  \n", __FILE__, __LINE__);
            status = mPtrRecSession->start(mPtrAnalogPsi);
            if (status == kMspStatus_Ok)
                dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s:%d Successfully started ANALOG TSB!!! :) ", __FUNCTION__, __LINE__);
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "%s %d This is DIGITAL Source ...So using DIGITAL PSI to Start Recording Session \n", __FILE__, __LINE__);
            status = mPtrRecSession->start();
        }

        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Error: Unable to start TSB!");
            return status;
        }
        return kMspStatus_Ok;
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: unable to alloc MSPRecordSession");
        return kMspStatus_Error;
    }
}

eMspStatus Dvr::StopRecordSession()
{
    FNLOG(DL_MSP_DVR);
    if (mPtrRecSession != NULL)
    {
        eMspStatus ret_value = mPtrRecSession->stop();

        if (ret_value != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "stop error %d", ret_value);
            return ret_value;
        }

        mTsbHardDrive = -1;
        mPtrRecSession->unregisterRecordCallback();
        mPtrRecSession->close();
        mTsbState = kTsbNotCreated;
        mIsPatPmtInjected = false;
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


void Dvr::StopDeletePSI()
{
    FNLOG(DL_MSP_DVR);
    if (mPtrPsi != NULL)
    {
        delete mPtrPsi;
        mPtrPsi = NULL;
    }
}


bool Dvr:: isBackground(void)
{
    return  mIsBackground;
}

void Dvr::StopDeleteAnalogPSI()
{
    FNLOG(DL_MSP_DVR);
    if (mPtrAnalogPsi != NULL)
    {
        mPtrAnalogPsi->psiStop();
        //mPtrAnalogPsi->unRegisterPsiCallback();
        delete mPtrAnalogPsi;
        mPtrAnalogPsi = NULL;
    }
}
eCsciMspDiagStatus Dvr::GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo)
{
    FNLOG(DL_MSP_MRDVR);
    if (mPtrLiveSource)
    {
        return mPtrLiveSource->GetMspNetworkInfo(msgInfo);
    }
    else
    {
        return kCsciMspDiagStat_NoData;
    }

}


void Dvr::addClientSession(IMediaPlayerClientSession *pClientSession)
{
    FNLOG(DL_MSP_MPLAYER);
    mAppClientsList.push_back(pClientSession);
}

void Dvr::deleteClientSession(IMediaPlayerClientSession *pClientSession)
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

void Dvr::deleteAllClientSession()
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



void
Dvr::StopAudio(void)
{
    if (mPtrDispSession)
    {
        mPtrDispSession->StopAudio();
    }
}


void
Dvr::RestartAudio(void)
{
    if (mPtrDispSession)
    {
        mPtrDispSession->RestartAudio();
    }
}
tCpePgrmHandle Dvr::getCpeProgHandle()
{
    return 0;
}

void Dvr::SetCpeStreamingSessionID(uint32_t sessionId)
{
    (void) sessionId;
    return;
}

void Dvr::AddLiveSession()
{
    // Add Live Session only if audio focus is true.
    if (mEnaAudio)
    {
        FNLOG(DL_MSP_MPLAYER);
        CurrentVideoData cvData;
        if (mPtrDispSession)
        {
            cvData.mediaHandle = mPtrDispSession->getMediaHandle();
        }
        if (mPtrRecSession)
        {
            cvData.recFileName = mPtrRecSession->GetTsbFileName();
            cvData.recordingHandle = mPtrRecSession->getRecordingHandle();
        }
        if (mPtrLiveSource)
        {
            cvData.srcHandle = mPtrLiveSource->getCpeSrcHandle();
        }
        CurrentVideoMgr::instance()->AddLiveSession(cvData);
    }
    else
    {
        LOG(DLOGL_NOISE, "Do not AddLiveSession, no audio focus.");
    }
}
void Dvr::InjectCCI(uint8_t CCIbyte)
{
    LOG(DLOGL_NOISE, " Dvr::InjectCCI is called with the value %u", CCIbyte);
    mCCIbyte = CCIbyte;
    if (mPtrRecSession)
    {
        LOG(DLOGL_NOISE, "mPtrRecSession is not NULL,so update CCI and writeTSBMetaData are called");
        LOG(DLOGL_NOISE, "Record file name is %s", mRecFile.c_str());
        mPtrRecSession->updateCCI(CCIbyte);

    }
    else
    {
        LOG(DLOGL_ERROR, "mPtrRecSession is  NULL,so update CCI and writeTSBMetaData are not called,so CCI cannot injected as of now");
    }

}

eIMediaPlayerStatus Dvr::StopStreaming()
{
    // By default the IMediaController base class requires an implementation
    // of StopStreaming since its an abstract class.
    return kMediaPlayerStatus_Error_NotSupported;
}

// Responsible for starting the EAS audio playback
// This API is applicable for the controllers associated with EAS audio playback session
// For the controllers associated with non EAS audio playback sessions simply returned
// Since Dvr is not the controller associated with EAS audio playback session,
// API is returning here without any action performed
void Dvr::startEasAudio(void)
{
    dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(), is not responsible for controlling EAS", __FUNCTION__);
    return;
}

// Sets the EAS audio playback active status
// True  - EAS audi playback is in progress
// False - EAS audio playback is not in progress
void Dvr::SetEasAudioActive(bool active)
{
    mEasAudioActive = active;
}



