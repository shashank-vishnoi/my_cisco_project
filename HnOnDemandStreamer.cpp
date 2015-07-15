/**
 \file HnOnDemandStreamer.cpp
 \class HnOnDemandStreamer

 Implementation file for HnOnDemandStreamer media controller class
 */

///////////////////////////////////////////////////////////////////////////
//                    Standard Includes
///////////////////////////////////////////////////////////////////////////
#include <list>
#include <assert.h>
#if defined(DMALLOC)
#include "dmalloc.h"
#endif

///////////////////////////////////////////////////////////////////////////
//                    CPE Includes
///////////////////////////////////////////////////////////////////////////
#include <cpe_error.h>
#include <cpe_source.h>
#include <directfb.h>



///////////////////////////////////////////////////////////////////////////
//                    SAIL Includes
///////////////////////////////////////////////////////////////////////////
#include <dlog.h>

///////////////////////////////////////////////////////////////////////////
//                    Local Includes
///////////////////////////////////////////////////////////////////////////
#include "HnOnDemandStreamer.h"
#include "eventQueue.h"
#include "SeaChange_SessionControl.h"
#include "SeaChange_StreamControl.h"
#include "pthread_named.h"
#include <sail-message-api.h>
#include <csci-base-message-api.h>
///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_ONDEMAND, level,"HnOnDemand(TID:%lx):%s:%d " msg, pthread_self(), __FUNCTION__, __LINE__, ##args);

///////////////////////////////////////////////////////////////////////////
//                      Member implementation
///////////////////////////////////////////////////////////////////////////

/** *********************************************************
*/
HnOnDemandStreamer::HnOnDemandStreamer()
{
    FNLOG(DL_MSP_ONDEMAND);
    mPtrHnOnDemandStreamerSource = NULL;
    mPtrHnOnDemandRFSource = NULL;

    mPsi 	= NULL;  /**< pointer to our PSI instance, NULL if not created yet */
    mPtrAnalogPsi  = NULL;
    mEventHandlerThread = 0;
    mSessionId = 0;

    mCallbackList.clear();
    mSrcUrl.clear();
    mptrcaStream = NULL;
    mOndemandZapperState = kZapperStateIdle;
    // create event queue for scan thread
    mThreadEventQueue = new MSPEventQueue();
    m_CCIbyte = 0;
    mCCICBFn = NULL;
    mCBData = NULL;
    mregid = -1;
    mptrcaStream = NULL;
    mCamCaHandle = NULL;
}


/** *********************************************************
 */
HnOnDemandStreamer::~HnOnDemandStreamer()
{
    FNLOG(DL_MSP_ONDEMAND);

    std::list<CallbackInfo*>::iterator iter;
    for (iter = mCallbackList.begin(); iter != mCallbackList.end(); iter++)
    {
        delete(*iter);
        *iter = NULL;
    }
    mCallbackList.clear();

    if (mThreadEventQueue)
    {
        delete mThreadEventQueue;
        mThreadEventQueue = NULL;
    }

    LOG(DLOGL_MINOR_DEBUG, "HnOnDemandStreamer Destructed");
}

/** *********************************************************
 */
///   This method returns the CPERP program handle for the current session
tCpePgrmHandle HnOnDemandStreamer::getCpeProgHandle()
{
    if (mPtrHnOnDemandStreamerSource)
        return mPtrHnOnDemandStreamerSource->getCpeProgHandle();
    else
        return 0;
}


///   This method sets/stores the CPERP streaming session ID in controller/ source
void HnOnDemandStreamer::SetCpeStreamingSessionID(uint32_t sessionId)
{
    FNLOG(DL_MSP_ONDEMAND);

    mSessionId = sessionId;
}

/** *********************************************************
 */
eIMediaPlayerStatus HnOnDemandStreamer::Load(const char* serviceUrl,
        const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_ONDEMAND);
    UNUSED_PARAM(pMme);

    eIMediaPlayerStatus status = kMediaPlayerStatus_Error_Unknown;

    if (onDemandState.Get() != kOnDemandStateWaitForLoad)
    {
        LOG(DLOGL_ERROR, "Error: state: %d", onDemandState.Get());
        status = kMediaPlayerStatus_Error_OutOfState;
    }

    if (!serviceUrl)
    {
        LOG(DLOGL_ERROR, "Error null serviceUrl");
        status = kMediaPlayerStatus_Error_InvalidParameter;
    }

    if (status == kMediaPlayerStatus_Error_Unknown)
    {
        LOG(DLOGL_NORMAL, "url: %s", serviceUrl);

        mSrcUrl = serviceUrl;

        string strippedSrcUrl = mSrcUrl.substr(strlen(HN_ONDEMAND_STREAMING_URL));

        if (strippedSrcUrl.c_str() == NULL)
        {
            LOG(DLOGL_ERROR, "NULL URL on stripping");
            status = kMediaPlayerStatus_Error_InvalidParameter;
        }

        if (status == kMediaPlayerStatus_Error_Unknown)
        {

            status = setupSessionAndStreamControllers(strippedSrcUrl.c_str());
            if (status == kMediaPlayerStatus_Ok)
            {

                LOG(DLOGL_NOISE, "After Load vodSessContrl :%p", vodSessContrl);

                // Must start event thread after setup so that there is at least one
                // event registered.
                int err = startEventThread();
                if (!err)
                {
                    onDemandState.Change(kOnDemandStatePreparingToView);  // wait for Play
                    status = kMediaPlayerStatus_Ok;
                }
                else
                {
                    LOG(DLOGL_ERROR, "Error creating event loop thread");
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "setupStream error");
            }
        }
    }
    return status;
}

/** *********************************************************
 */
bool HnOnDemandStreamer::isBackground(void)
{
    return true;
}

/** *********************************************************
 */
eIMediaPlayerStatus HnOnDemandStreamer::loadTuningParamsAndPlay()
{
    // called from event handler in response to session setup

    FNLOG(DL_MSP_ONDEMAND);

    if (onDemandState.Get() != kOnDemandSessionServerReady)
    {
        LOG(DLOGL_ERROR, "warning - no action in state %d", onDemandState.Get());
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (vodSessContrl)
    {
        // Set tuning parameters
        tCpeSrcRFTune tuningParams = vodSessContrl->GetTuningParameters();
        int           programNum   = vodSessContrl->GetProgramNumber();

        eIMediaPlayerStatus status = Load(tuningParams, programNum, NULL);

        if (status == kMediaPlayerStatus_Ok)
        {
            StartSessionKeepAliveTimer();

            LOG(DLOGL_SIGNIFICANT_EVENT, "vodStreamContrl->StreamPlay (starting at 0 always for now)");
            if (vodStreamContrl)
            {
                vodStreamContrl->StreamSetNPT(mStartNptMs);

                LOG(DLOGL_SIGNIFICANT_EVENT, "vodStreamContrl->StreamPlay called create new event");

                EventCallbackData *strmCbData = new EventCallbackData;

                if (strmCbData)
                {
                    strmCbData->objPtr  = this;
                    strmCbData->odEvtType = kOnDemandStreamReadEvent;

                    mStreamSocketEvent = event_new(mEventLoop->getBase(),
                                                   vodStreamContrl->GetFD(), EV_READ | EV_PERSIST,
                                                   ReadStreamEvtCallback, strmCbData);

                    if (mStreamSocketEvent)
                    {
                        int err = event_add(mStreamSocketEvent, NULL);

                        if (err)
                        {
                            event_del(mStreamSocketEvent);
                            delete strmCbData;
                            LOG(DLOGL_ERROR, "mStreamSocketEvent event_add error...!");
                            return kMediaPlayerStatus_Error_OutOfState;
                        }
                    }
                    else
                    {
                        delete strmCbData;
                        LOG(DLOGL_ERROR, "null mSessSocketEvent");
                        return kMediaPlayerStatus_Error_OutOfState;
                    }
                }
                else
                {
                    LOG(DLOGL_ERROR, "null strmCbData");
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "null vodStreamContrl");
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "warning Load status: %d", status);
            return status;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "null vodSessContrl");
    }

    return kMediaPlayerStatus_Ok;
}

/** *********************************************************
 */
eIMediaPlayerStatus HnOnDemandStreamer::SetCallback(
    IMediaPlayerSession *pIMediaPlayerSession,
    IMediaPlayerStatusCallback cb, void* pClientContext)
{
    FNLOG(DL_MSP_ONDEMAND);

    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    LOG(DLOGL_MINOR_DEBUG, "HnOnDemandStreamer is the tunerController pIMediaPlayerSession: %p and pClientContext: %p", pIMediaPlayerSession, pClientContext);

    CallbackInfo *cbInfo = new CallbackInfo();
    if (cbInfo)
    {
        cbInfo->mpSession = pIMediaPlayerSession;
        cbInfo->mCallback = cb;
        cbInfo->mClientContext = pClientContext;
        mCallbackList.push_back(cbInfo);
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: Unable to alloc mem");
        status = kMediaPlayerStatus_Error_OutOfMemory;
    }

    return status;
}


/** *********************************************************
 */
eIMediaPlayerStatus HnOnDemandStreamer::DetachCallback(IMediaPlayerStatusCallback cb)
{
    FNLOG(DL_MSP_ONDEMAND);

    std::list<CallbackInfo*>::iterator iter;
    for (iter = mCallbackList.begin(); iter != mCallbackList.end(); iter++)
    {
        if ((*iter)->mCallback == cb)
        {
            LOG(DLOGL_REALLY_NOISY, "Removing the callback:%p from the list", cb);
            mCallbackList.remove(*iter);
            delete(*iter);
            *iter = NULL;
            break;
        }
    }

    return kMediaPlayerStatus_Ok;
}


/** *********************************************************
 */
eIMediaPlayerStatus HnOnDemandStreamer::Load(tCpeSrcRFTune tuningParams,
        int programNumber,
        const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_ONDEMAND);
    LOG(DLOGL_FUNCTION_CALLS, "tuning params supplied");

    UNUSED_PARAM(pMme)

    eIMediaPlayerStatus mediaPlayerStatus = kMediaPlayerStatus_Ok;

    //TODO Check if we really need this
    if ((mOndemandZapperState != kZapperStateIdle) && (mOndemandZapperState != kZapperStateStop))
    {
        LOG(DLOGL_ERROR, "error: bad state: %d", mOndemandZapperState);
        mediaPlayerStatus =  kMediaPlayerStatus_Error_OutOfState;
    }
    else
    {
        if (mPtrHnOnDemandRFSource != NULL)
        {
            LOG(DLOGL_ERROR, "Warning. source exists already.Shouldn't have existed");
            mPtrHnOnDemandRFSource->stop();
            delete mPtrHnOnDemandRFSource;
        }

        LOG(DLOGL_REALLY_NOISY, "Creating a HnOnDemandRFSource");
        mPtrHnOnDemandRFSource =  new MSPRFSource(tuningParams, programNumber);

        mediaPlayerStatus = loadSource();  // will call mPtrHnOnDemandRFSource->load
    }
    return mediaPlayerStatus;
}


/** *********************************************************
 */
eIMediaPlayerStatus HnOnDemandStreamer::Stop(bool stopPlay, bool stopPersistentRecord)
{
    FNLOG(DL_MSP_ONDEMAND);

    UNUSED_PARAM(stopPlay)
    UNUSED_PARAM(stopPersistentRecord)

    if (mptrcaStream)
    {
        LOG(DLOGL_NORMAL, "%s: so cleaning up the session", __func__);
        mptrcaStream->unRegisterCCICallback(mregid);
        mptrcaStream->shutdown();
        delete mptrcaStream;
        mCamCaHandle = NULL;
        mptrcaStream = NULL;
    }

    if (mPsi != NULL)
    {
        delete mPsi;
        mPsi = NULL;
    }

    if (mPtrAnalogPsi)
    {
        delete mPtrAnalogPsi;
        mPtrAnalogPsi = NULL;
    }

    if (mThreadEventQueue)
    {
        LOG(DLOGL_MINOR_DEBUG, "Flushing the event queue");
        mThreadEventQueue->flushQueue(); //flush out any pending events posted prior/during Stop() call.
    }

    mOndemandZapperState = kZapperStateStop;

    if (vodStreamContrl)
    {
        vodStreamContrl->StreamTearDown();
    }

    if (vodSessContrl)
    {
        if (onDemandState.Get() >= kOnDemandSessionServerReady)
        {
            vodSessContrl->SessionTeardown();
        }
        else
        {
            LOG(DLOGL_MINOR_DEBUG, "vodSessContrl->SessionTeardown not sent in state: %d", onDemandState.Get());
        }
    }

    onDemandState.Change(kOnDemandStateStopPending);

    return kMediaPlayerStatus_Ok;
}


/** *********************************************************
 */
void HnOnDemandStreamer::Eject()
{
    FNLOG(DL_MSP_ONDEMAND);

    if (mEventHandlerThread)
    {
        LOG(DLOGL_REALLY_NOISY, "exit thread");
        queueEvent(kZapperEventExit);  // tell thread to exit first. so that, it don't process any stale callbacks from core modules,that may come during the process of teardown.
        unLockMutex();
        pthread_join(mEventHandlerThread, NULL);       // wait for event thread to exit
        lockMutex();
        mEventHandlerThread = 0;
        tearDown();
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "mEventHandlerThread null");
    }

    unLockMutex();
    stopEventThread();
    lockMutex();

}


/** *********************************************************
 */
void HnOnDemandStreamer::tearDown(void)
{
    // This is called in context of event loop when kZapperEventExit event is handled
    // teardown in reverse order

    FNLOG(DL_MSP_ONDEMAND);

    if (mPsi)
    {
        LOG(DLOGL_REALLY_NOISY, "psiStop");
        delete mPsi;
        mPsi = NULL;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "mPsi is null");
    }
    if (mPtrAnalogPsi)
    {
        delete mPtrAnalogPsi;
        mPtrAnalogPsi = NULL;
    }

    //Also stop the cpe_hnservermgr streamer source
    if (mPtrHnOnDemandStreamerSource)
    {
        LOG(DLOGL_REALLY_NOISY, "mPtrHnOnDemandStreamerSource->stop");
        mPtrHnOnDemandStreamerSource->stop();
        delete mPtrHnOnDemandStreamerSource;
        mPtrHnOnDemandStreamerSource = NULL;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "mPtrHnOnDemandStreamerSource is null");
    }

    if (mPtrHnOnDemandRFSource)
    {
        LOG(DLOGL_REALLY_NOISY, "mPtrHnOnDemandRFSource->stop");
        mPtrHnOnDemandRFSource->stop();
        delete mPtrHnOnDemandRFSource;
        mPtrHnOnDemandRFSource = NULL;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "mPtrHnOnDemandRFSource is null");
    }


    mOndemandZapperState = kZapperStateStop;
}

/** *********************************************************
 */
eIMediaPlayerStatus HnOnDemandStreamer::loadSource(void)
{
    FNLOG(DL_MSP_ONDEMAND);

    eIMediaPlayerStatus mediaPlayerStatus = kMediaPlayerStatus_Ok;

    if ((mOndemandZapperState != kZapperStateIdle) && (mOndemandZapperState != kZapperStateStop))
    {
        LOG(DLOGL_ERROR, "error: bad state: %d", mOndemandZapperState);
        mediaPlayerStatus = kMediaPlayerStatus_Error_OutOfState;
    }

    if (!mPtrHnOnDemandRFSource)
    {
        LOG(DLOGL_ERROR, "error: NULL mPtrHnOnDemandRFSource");
        mediaPlayerStatus = kMediaPlayerStatus_Error_Unknown;
    }

    if (mediaPlayerStatus == kMediaPlayerStatus_Ok)
    {
        eMspStatus status = mPtrHnOnDemandRFSource->load(hnOnDemandRFSourceCB, this);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "HnOnDemandRFSource load failed");
            mediaPlayerStatus = kMediaPlayerStatus_Error_InvalidURL;
        }
        else if (mEventHandlerThread == 0)
        {
            int err = createEventThread();
            if (!err)
            {
                // TODO: Create display and psi objects here ??
                mOndemandZapperState = kZapperStateStop;
                LOG(DLOGL_REALLY_NOISY, "mOndemandZapperState = kZapperStateStop");
            }
            else
            {
                LOG(DLOGL_ERROR, "Error: unable to start thread");
                mediaPlayerStatus = kMediaPlayerStatus_Error_Unknown;
            }
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "createEventThread is not called since mEventHandlerThread is %d", (int)mEventHandlerThread);
        }
    }

    return mediaPlayerStatus;
}




/** *********************************************************
 */
// return 0 on success
int HnOnDemandStreamer::createEventThread(void)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, (128 * 1024));
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    // By default the thread is created as joinable.
    int err  = pthread_create(&mEventHandlerThread, &attr, eventthreadFunc, (void *) this);
    if (!err)
    {
        // failing to set name is not considered an major error
        int retval = pthread_setname_np(mEventHandlerThread, "MSP_HnOnDemandStreamer_EvntHandlr");
        if (retval)
        {
            LOG(DLOGL_ERROR, "pthread_setname_np error: %d", retval);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "pthread_create error %d", err);
    }

    pthread_attr_destroy(&attr);

    return err;
}



/** *********************************************************
*/
void* HnOnDemandStreamer::eventthreadFunc(void *data)
{
    // This is a static method running in a pthread and does not have
    // direct access to class variables.  Access is through data pointer.

    FNLOG(DL_MSP_ONDEMAND);

    HnOnDemandStreamer* inst  = (HnOnDemandStreamer*)data;
    if (!inst)
    {
        LOG(DLOGL_ERROR, "HnOnDemandStreamer instance is NULL here");
        return NULL;
    }

    MSPEventQueue* eventQueue = inst->mThreadEventQueue;
    assert(eventQueue);

    bool done = false;
    while (!done)
    {
        unsigned int waitTime = 0;
        if (inst->mOndemandZapperState == kZapperWaitSourceReady)
        {
            waitTime = 5;
            LOG(DLOGL_REALLY_NOISY, "Wait %d seconds for tuner lock", waitTime);
        }
        eventQueue->setTimeOutSecs(waitTime);
        Event* evt = eventQueue->popEventQueue();
        eventQueue->unSetTimeOut();
        inst->lockMutex();
        done = inst->handleEvent(evt);  // call member function to handle event
        eventQueue->freeEvent(evt);
        inst->unLockMutex();
    }

    LOG(DLOGL_REALLY_NOISY, "MSP_HnOnDemandStreamer_EvntHandlr thread exit");
    pthread_exit(NULL);
    return NULL;
}

/** *********************************************************
*/
bool HnOnDemandStreamer::handleEvent(Event *evt)
{
    FNLOG(DL_MSP_ONDEMAND);

    if (!evt)
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "warning null event in state: %d", mOndemandZapperState);
        return false;
    }

    unsigned int event = evt->eventType;

    LOG(DLOGL_REALLY_NOISY, "eventType: %d  state: %d", event, mOndemandZapperState);

    eMspStatus status = kMspStatus_Ok;

    switch (evt->eventType)
    {
        // exit
    case kZapperEventExit:
        LOG(DLOGL_NOISE, "Exit Sesion");
        return true;
        // no break required here

    case kZapperTimeOutEvent:
        if (mPtrHnOnDemandRFSource)
        {
            if (mOndemandZapperState == kZapperWaitSourceReady)
            {
                LOG(DLOGL_NORMAL, "Warning - Tuner lock timeout.May be signal strength is low or no stream on tuned frequency!!");
            }
            else if (mOndemandZapperState == kZapperTunerLocked) // i.e. waiting for PSI to be ready
            {
                LOG(DLOGL_ERROR, "Warning - PSI not available. DoCallback - ContentNotFound!!");
            }
            else
            {
                LOG(DLOGL_ERROR, "Warning - Unexpected OndemandZapperState:%d", mOndemandZapperState);
            }
        }
        else
        {
            LOG(DLOGL_NORMAL, "NULL HnOnDemandRFSource source pointer on time out event");
        }
        break;

    case kZapperEventPlay:
    {
        if (mPtrHnOnDemandRFSource)
        {
            LOG(DLOGL_REALLY_NOISY, "Opening the mPtrHnOnDemandRFSource with kRMPriorityIPClient_VodVideoPlayOrRecording");
            status = mPtrHnOnDemandRFSource->open(kRMPriorityIPClient_VodVideoPlayOrRecording);
            if (status == kMspStatus_Loading)
            {
                LOG(DLOGL_REALLY_NOISY, "waiting for tuning params");
                mOndemandZapperState = kZapperWaitTuningParams;
            }
            else if (status == kMspStatus_WaitForTuner)
            {
                LOG(DLOGL_REALLY_NOISY, "Waiting for tuning acquisition");
                mOndemandZapperState = kZapperWaitForTuner;
            }
            else if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "Some problem with mPtrHnOnDemandRFSource open...! Informing concerned parties");
                DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_InvalidParameter);
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "Starting the mPtrHnOnDemandRFSource");
                mPtrHnOnDemandRFSource->start();
                mOndemandZapperState = kZapperWaitSourceReady;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "NULL HnOnDemandRFSource source pointer");
        }
    }
    break;

    // get RF tuner locked callback, create display session and psi, start psi
    case kZapperTunerLockedEvent:
        if (mPtrHnOnDemandRFSource == NULL)
        {
            LOG(DLOGL_ERROR, " NULL mPtrHnOnDemandRFSource !!");
        }
        else if (mOndemandZapperState != kZapperWaitSourceReady)
        {
            LOG(DLOGL_NOISE, "Error kZapperSourceReadyEvent wrong state: %d", mOndemandZapperState);
        }
        else
        {
            //TODO: this really should go in a separate function
            mOndemandZapperState = kZapperTunerLocked;
            LOG(DLOGL_REALLY_NOISY, "Create psi");
            mPsi = new Psi();
            LOG(DLOGL_REALLY_NOISY, "register psi callback");
            mPsi->registerPsiCallback(psiCallback, this);

            LOG(DLOGL_REALLY_NOISY, "Start psi");
            // get psi started then wait for PSI ready callback
            mPsi->psiStart(mPtrHnOnDemandRFSource);
        }
        break;

    case kZapperEventRFAnalogCallback:
        if (mPtrHnOnDemandRFSource == NULL)
        {
            LOG(DLOGL_ERROR, " NULL mPtrHnOnDemandRFSource !!");
        }
        else if (mOndemandZapperState != kZapperWaitSourceReady)
        {
            LOG(DLOGL_NOISE, "Error kZapperSourceReadyEvent wrong state: %d", mOndemandZapperState);
        }
        else
        {
            mOndemandZapperState = kZapperTunerLocked;
            mPtrAnalogPsi = new AnalogPsi();
            status = mPtrAnalogPsi->psiStart(mPtrHnOnDemandRFSource);
            queueEvent(kZapperAnalogPSIReadyEvent);
        }
        break;

    case kZapperAnalogPSIReadyEvent:
    case kZapperPSIReadyEvent:
        if (mPtrHnOnDemandRFSource)
        {
            if (mOndemandZapperState == kZapperTunerLocked)
            {
                StartVodInMemoryStreaming();
            }
            else
            {
                LOG(DLOGL_ERROR, "Warning: not starting display, mState: %d", mOndemandZapperState);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "NULL HnOnDemandRFSource source pointer");
        }
        break;

    case kZapperPSIUpdateEvent:

        if (mOndemandZapperState == kZapperTunerLocked)
        {
            LOG(DLOGL_NORMAL, "kZapperPSIUpdateEvent teardown and retune");
            tearDown();
            mPsi = new Psi();
            queueEvent(kZapperEventPlay);
        }
        else
        {
            LOG(DLOGL_SIGNIFICANT_EVENT, "Warning: kZapperPSIUpdateEvent while in state: %d", mOndemandZapperState);
        }
        break;

    case kZapperTunerLost:
        LOG(DLOGL_NORMAL, "Tuner lost callback. state is %d", mOndemandZapperState);
        DoCallback(kMediaPlayerSignal_ResourceLost, kMediaPlayerStatus_Ok);
        tearDown();
        mOndemandZapperState = kZapperStateStop;
        break;

    case kZapperTunerRestored:
        LOG(DLOGL_NOISE, "Tuner restored event -- Sending event to MDA");
        DoCallback(kMediaPlayerSignal_ResourceRestored, kMediaPlayerStatus_Ok);
        LOG(DLOGL_NOISE, "Tuner restored event -- queue play event");
        queueEvent(kZapperEventPlay);
        break;

    default:
        LOG(DLOGL_ERROR, "Not a valid callback event %d", evt->eventType);
        break;
    }

    return false;
}

/** *********************************************************
 *
*/
void HnOnDemandStreamer::psiCallback(ePsiCallBackState state, void *data)
{
    FNLOG(DL_MSP_ONDEMAND);
    HnOnDemandStreamer *inst = (HnOnDemandStreamer *)data;

    if (!inst)
    {
        LOG(DLOGL_ERROR, "HnOnDemandStreamer instance is NULL here");
        return;
    }

    switch (state)
    {
    case kPSIReady:
        LOG(DLOGL_NOISE, "kPSIReady");
        inst->queueEvent(kZapperPSIReadyEvent);
        break;

    case kPSIUpdate:
        LOG(DLOGL_NOISE, "kPSIUpdate");
        inst->queueEvent(kZapperPSIUpdateEvent);
        break;

    case kPSITimeOut:
        LOG(DLOGL_NOISE, "kPSITimeOut");
        inst->queueEvent(kZapperTimeOutEvent);
        break;

    case kPSIError:
        LOG(DLOGL_NOISE, "Warning: kPSIError - handle as timeout");
        inst->queueEvent(kZapperTimeOutEvent);
        break;

    default:
        LOG(DLOGL_ERROR, "Warning: Unhandled event: %d", state);
        break;
    }

}
/** *********************************************************
*/
eMspStatus HnOnDemandStreamer::queueEvent(eZapperEvent evtyp)
{
    FNLOG(DL_MSP_ONDEMAND);
    if (!mThreadEventQueue)
    {
        LOG(DLOGL_ERROR, "HnOnDemandStreamer warning: no queue to dispatch event %d", evtyp);
        return kMspStatus_BadParameters;
    }

    mThreadEventQueue->dispatchEvent(evtyp);
    return kMspStatus_Ok;
}

/** *********************************************************
 */
void HnOnDemandStreamer::hnOnDemandRFSourceCB(void *data, eSourceState aSrcState)
{
    FNLOG(DL_MSP_ONDEMAND);
    LOG(DLOGL_REALLY_NOISY, "data: %p  aSrcState: %d", data, aSrcState);

    if (!data)
    {
        LOG(DLOGL_MINOR_EVENT, "Warning: null data aSrcState: %d", aSrcState);
        return;
    }

    HnOnDemandStreamer *inst = (HnOnDemandStreamer *)data;

    switch (aSrcState)
    {
    case kSrcTunerLocked:
        LOG(DLOGL_MINOR_EVENT, "kSrcTunerLocked");
        inst->queueEvent(kZapperTunerLockedEvent);
        break;

    case kAnalogSrcTunerLocked:
        LOG(DLOGL_NORMAL, "source callback: tuner lock callback kAnalogSrcTunerLocked");
        inst->queueEvent(kZapperEventRFAnalogCallback);
        break;

    case kSrcTunerLost:
        LOG(DLOGL_NOISE, "Source Callback Tuner lost");
        inst->queueEvent(kZapperTunerLost);
        break;

    case kSrcTunerRegained:
        LOG(DLOGL_NOISE, "Source Callback Tuner restored");
        inst->queueEvent(kZapperTunerRestored);
        break;

    default:
        LOG(DLOGL_ERROR, "Warning: unknown event: %d", aSrcState);
        break;
    }
}

/** *********************************************************
 */
void HnOnDemandStreamer::HandleCallback(eOnDemandEvent onDemandEvt)
{
    FNLOG(DL_MSP_ONDEMAND);
    eOnDemandState currentState = onDemandState.Get();
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    LOG(DLOGL_REALLY_NOISY, "HnOnDemandStreamer onDemandEvt: %d  currentState: %d", onDemandEvt, currentState);

    bool invalidStateForAction = false;

    switch (onDemandEvt)
    {
    case kOnDemandSetupRespEvent:
        if (currentState == kOnDemandStateSessionPending)
        {
            onDemandState.Change(kOnDemandSessionServerReady);
            // load tuning params, start session keep alive, and start stream play
            status = loadTuningParamsAndPlay();
            if (status == kMediaPlayerStatus_Ok)
            {
                onDemandState.Change(kOnDemandControlServerPending);
            }
            else
            {
                LOG(DLOGL_ERROR, "HnOnDemandStreamer warning: loadTuningParamsAndPlay status %d", status);
            }

            time_t val = 0;
            val = time(NULL);
            mSessionActivatedTime = localtime(&val);
        }
        else
        {
            invalidStateForAction = true;
        }
        break;

    case kOnDemandPlayRespEvent:
        if (vodStreamContrl == NULL)
        {
            LOG(DLOGL_ERROR, "vodStreamContrl is NULL... Returning");
            return ;
        }
        switch (vodStreamContrl->GetReturnStatus())
        {
        case LSC_OK:   //No action
            break;
        case LSC_NO_PERMISSION:
            DoCallback(kMediaPlayerSignal_VodPurchaseNotification, kMediaPlayerStatus_NotAuthorized);
            break;
        case LSC_NO_RESOURCES:
            LOG(DLOGL_SIGNIFICANT_EVENT, "HnOnDemandStreamer Callback signal for LSC_NO_RESOURCES");
            DoCallback(kMediaPlayerSignal_VodPurchaseNotification, kMediaPlayerStatus_ContentNotFound);
            break;
        case LSC_NO_MEMORY:
            LOG(DLOGL_SIGNIFICANT_EVENT, "HnOnDemandStreamer Callback signal for LSC_NO_MEMORY");
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_OutOfMemory);
            break;

        case LSC_WRONG_STATE:
            LOG(DLOGL_SIGNIFICANT_EVENT, "HnOnDemandStreamer Callback signal for LSC_WRONG_STATE");
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_OutOfState);
            break;
        case LSC_BAD_PARAM:
            LOG(DLOGL_SIGNIFICANT_EVENT, "HnOnDemandStreamer Callback signal for LSC_BAD_PARAM");
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_InvalidParameter);
            break;

        case LSC_BAD_STREAM:
        case LSC_BAD_REQUEST:
        case LSC_UNKNOWN:
        case LSC_NO_IMPLEMENT:
        case LSC_IMP_LIMIT:
        case LSC_TRANSIENT:
        case LSC_SERVER_ERROR:
        case LSC_SERVER_FAILURE:
        case LSC_BAD_START:
        case LSC_BAD_STOP:
        case LSC_MPEG_DELIVERY:
            LOG(DLOGL_SIGNIFICANT_EVENT, "HnOnDemandStreamer Callback signal for LSC_SERVER_ERROR");
            LOG(DLOGL_NORMAL, "HnOnDemandStreamer callback signal from VOD server is %d ,intimating services layer about the problem", vodStreamContrl->GetReturnStatus());
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ServerError);
            break;

        case LSC_BAD_SCALE:
            LOG(DLOGL_NORMAL, "HnOnDemandStreamer callback signal from VOD server about BAD_SCALE.ignoring it");
            break;


        default:
            LOG(DLOGL_MINOR_EVENT, "HnOnDemandStreamer Unknown STATE from server");
            break;
        }//end switch*/

        if (currentState == kOnDemandControlServerPending)
        {
            onDemandState.Change(kOnDemandControlServerReady);

            if ((mOndemandZapperState == kZapperStateStop) || (mOndemandZapperState == kZapperStateIdle))
            {
                LOG(DLOGL_REALLY_NOISY, "Posting kZapperEventPlay to the eventQueue...!");
                queueEvent(kZapperEventPlay);
            }
            else
            {
                LOG(DLOGL_ERROR, "HnOnDemandStreamer Error state: %d", mOndemandZapperState);
                status = kMediaPlayerStatus_Error_OutOfState;
            }

            onDemandState.Change(kOnDemandTunerPending);
        }
        else
        {
            LOG(DLOGL_ERROR, "Invalid state for playing...!");
            invalidStateForAction = true;
        }

        mPendingPlayResponse = false;

        if (mPendingPosition == true)
        {
            LOG(DLOGL_NORMAL, "HnOnDemandStreamer Applying the pending position setting now");
            mPendingPosition = false;
            SetPosition(mCurrentSetPosition);
        }

        if (mPendingSpeed == true)
        {
            LOG(DLOGL_NORMAL, "HnOnDemandStreamer Applying the pending speed setting now");
            SetSpeed(mCurrentNumerator, mCurrentDenominator);
            mPendingSpeed = false;
        }

        break;

    case kOnDemandPlayBOFEvent:
        LOG(DLOGL_NORMAL, "HnOnDemandStreamer Callback signal for BeginningOfStream");
        DoCallback(kMediaPlayerSignal_BeginningOfStream, kMediaPlayerStatus_Ok);
        break;

    case kOnDemandPlayEOFEvent:
        LOG(DLOGL_NORMAL, "HnOnDemandStreamer Callback signal for EndOfStream");
        DoCallback(kMediaPlayerSignal_EndOfStream, kMediaPlayerStatus_Ok);
        break;

    case kOnDemandPlayDoneEvent:
    {
        if (vodStreamContrl)
        {
            LOG(DLOGL_REALLY_NOISY, "HnOnDemandStreamer VOD Server Mode: %hu  ", vodStreamContrl->GetServerMode());
            // TODO: action for this state
            LOG(DLOGL_SIGNIFICANT_EVENT, "HnOnDemandStreamer kOnDemandPlayDoneEvent rcvd in state: %d  - action??", onDemandState.Get());
            if (vodStreamContrl->GetPos() == 0x00000000)
            {
                if ((vodStreamContrl->GetServerMode() == O_MODE) && (vodStreamContrl->GetNumerator() == 0))
                {
                    LOG(DLOGL_SIGNIFICANT_EVENT, "HnOnDemandStreamer Callback signal for Time-out EndOfStream");
                    DoCallback(kMediaPlayerSignal_EndOfStream, kMediaPlayerStatus_Ok);
                    onDemandState.Change(kOnDemandStateStopPending);
                }
                else
                {
                    LOG(DLOGL_SIGNIFICANT_EVENT, "HnOnDemandStreamer Callback signal for BeginningOfStream");
                    DoCallback(kMediaPlayerSignal_BeginningOfStream,
                               kMediaPlayerStatus_Ok);
                }
            }
            else
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "HnOnDemandStreamer Callback signal for EndOfStream");
                DoCallback(kMediaPlayerSignal_EndOfStream, kMediaPlayerStatus_Ok);
            }
        }
    }
    break;

    case kOnDemandReleaseRespEvent:
        if (currentState == kOnDemandStateStopPending)
        {
            onDemandState.Change(kOnDemandStateStopped);
            LOG(DLOGL_SIGNIFICANT_EVENT, "HnOnDemandStreamer Changed HnOnDemandStreamer state to kOnDemandStateStopped");
        }
        else
        {
            invalidStateForAction = true;
        }
        break;

    case kOnDemandKeepAliveTimerEvent:

        LOG(DLOGL_MINOR_EVENT, "HnOnDemandStreamer VOD Session Keep Alive Timer");

        if (vodSessContrl && (currentState == kOnDemandStateStreaming))
        {
            LOG(DLOGL_MINOR_EVENT, "HnOnDemandStreamer  vodSessContrl->SendKeepAlive");
            vodSessContrl->SendKeepAlive();
        }
        else
        {
            LOG(DLOGL_MINOR_EVENT, "HnOnDemandStreamer warning - not sending keep alive in state: %d", currentState);
        }
        break;

    case kOnDemandStreamReadEvent:
        LOG(DLOGL_MINOR_EVENT, "HnOnDemandStreamer kOnDemandStreamReadEvent");
        if (vodStreamContrl)
            vodStreamContrl->ReadStreamSocketData();
        break;

    case kOnDemandSessionErrorEvent:
    {
        // if forward count is not zero we have to retry session setup
        // TODO: Verify this is the correct meaning of maxForwardCount
        uint8_t maxForwardCount = 0;
        OnDemandSystemClient::getInstance()->GetMaxForwardCount(&maxForwardCount);
        if (vodSessContrl && (sessionSetupRetryCount < maxForwardCount))
        {
            eIMediaPlayerStatus errStatus = vodSessContrl->SessionSetup(mSrcUrl);

            if (!errStatus)
            {
                LOG(DLOGL_NOISE, "HnOnDemandStreamer SessionSetup OK");
                onDemandState.Change(kOnDemandStateSessionPending);
            }
            else if (kMediaPlayerStatus_ClientError == errStatus)
            {
                LOG(DLOGL_ERROR, "HnOnDemandStreamer ClientError in SessionSetup");
                DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ClientError);
            }
            else
            {
                LOG(DLOGL_ERROR, "HnOnDemandStreamer SessionSetup error: 0x%x", errStatus);
            }

            sessionSetupRetryCount++;
        }
        else
        {
            LOG(DLOGL_ERROR, "HnOnDemandStreamer Callback signal for Session Setup > retry Count error");
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ServerError);
        }
    }
    break;


    case kOnDemandStreamErrorEvent:
    {
        LOG(DLOGL_ERROR, "HnOnDemandStreamer Callback signal for Stream Setup  error");
        DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ServerError);
        onDemandState.Change(kOnDemandStateStopped);
        if (mStreamSocketEvent)
        {
            LOG(DLOGL_MINOR_DEBUG, "Ondemand:HandleCallback: event_del(mStreamSocketEvent) %p", mStreamSocketEvent);
            event_free(mStreamSocketEvent);
            mStreamSocketEvent = NULL;
        }
    }
    break;


    case kOnDemandStopControllerEvent:
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "HnOnDemandStreamer Remove Controller");

        mActiveControllerList.remove(this);

        LOG(DLOGL_SIGNIFICANT_EVENT, "HnOnDemandStreamer kOnDemandStopControllerEvent");
        // Free eventlib events

        if (mStreamSocketEvent)
        {
            EventCallbackData *userData = (EventCallbackData *)mStreamSocketEvent->ev_arg;
            if (userData)
            {
                delete userData;
            }
            LOG(DLOGL_MINOR_DEBUG, "HnOnDemandStreamer:HandleCallback: event_del(mStreamSocketEvent) %p", mStreamSocketEvent);
            event_free(mStreamSocketEvent);
            mStreamSocketEvent = NULL;
        }

        if (mEventTimer && mEventLoop)
        {
            EventCallbackData *userData = (EventCallbackData *)mEventTimer->getUserData();
            if (userData)
            {
                delete userData;
            }
            LOG(DLOGL_MINOR_DEBUG, "HnOnDemandStreamer:HandleCallback:mEventLoop->delTimer(mEventTimer) %p", mEventTimer);
            mEventLoop->delTimer(mEventTimer);
            mEventTimer = NULL;
        }

        if (vodStreamContrl)
        {
            vodSessContrl->setNpt(vodStreamContrl->GetPos());
            LOG(DLOGL_MINOR_DEBUG, "HnOnDemandStreamer delete streamControl %p", vodStreamContrl);
            delete vodStreamContrl;
            vodStreamContrl = NULL;
        }

        if (vodSessContrl)
        {
            LOG(DLOGL_MINOR_DEBUG, "HnOnDemandStreamer delete sessionControl: %p", vodSessContrl);
            delete vodSessContrl;
            vodSessContrl = NULL;
        }

        LOG(DLOGL_REALLY_NOISY, "HnOnDemandStreamer:HandleCallback:signal done");
        pthread_mutex_lock(&mStopMutex);
        pthread_cond_signal(&mStopCond);
        pthread_mutex_unlock(&mStopMutex);

        LOG(DLOGL_REALLY_NOISY, "HnOnDemandStreamer:HandleCallback: done");
    }
    break;

    default:
        LOG(DLOGL_SIGNIFICANT_EVENT, "HnOnDemandStreamer warning: event %d not handled", onDemandEvt);
        break;
    }

    if (invalidStateForAction)
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "HnOnDemandStreamer warning onDemandEvt (%d) recvd in state (%d) - no action taked",
            onDemandEvt, currentState);
    }
}

eIMediaPlayerStatus HnOnDemandStreamer::RegisterCCICallback(void* data, CCIcallback_t cb)
{
    FNLOG(DL_MSP_ONDEMAND);
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;
    mCBData = data;
    mCCICBFn = cb;

    return status;
}

eIMediaPlayerStatus HnOnDemandStreamer::UnRegisterCCICallback()
{
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;
    mCBData = NULL;
    mCCICBFn = NULL;
    return status;
}


void HnOnDemandStreamer::StartVodInMemoryStreaming()
{
    if (mPtrHnOnDemandRFSource)
    {
        //Start the MSPHnOnDemandStreamerSource here
        mPtrHnOnDemandStreamerSource = new MSPHnOnDemandStreamerSource(mPtrHnOnDemandRFSource->getSourceUrl() , mPtrHnOnDemandRFSource->getCpeSrcHandle());
        if (mPtrHnOnDemandStreamerSource)
        {
            LOG(DLOGL_REALLY_NOISY, "Created HnOnDemandStreamer source...!");
            // Set the CPERP streaming session id with HnOnDemandStreamer source
            mPtrHnOnDemandStreamerSource->SetCpeStreamingSessionID(mSessionId);

            // Loading the HnOnDemandStreamer source
            eMspStatus status = mPtrHnOnDemandStreamerSource->load(hnOnDemandRFSourceCB, (void*) this);
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "HnOnDemandStreamer source load failed...!");
            }
            else
            {
                LOG(DLOGL_MINOR_DEBUG, "Successfully loaded the HnOnDemandStreamer source");
                // Opening the HnOnDemandStreamer source
                LOG(DLOGL_MINOR_DEBUG, "Opening the HnOnDemandStreamer source");
                status = mPtrHnOnDemandStreamerSource->open(kRMPriorityIPClient_VodVideoPlayOrRecording);
                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "HnOnDemandStreamer source open failed...!");
                }
                else
                {
                    LOG(DLOGL_MINOR_DEBUG, "Successfully opened the HnOnDemandStreamer source");
                }
            }

            //Set the PID table which also should automatically inject PAT/PMT in the HnOnDemand stream source to the IP Client
            status = mPtrHnOnDemandStreamerSource->setStreamInfo(mPsi);
            if (status != kMspStatus_Ok)
            {

                LOG(DLOGL_ERROR, "HnOnDemandStreamer Set Stream Info failed...!");
            }
            else
            {
                LOG(DLOGL_NORMAL, "HnOnDemandStreamer Injected PAT/PMT to the stream...!");
                LOG(DLOGL_NORMAL, "HnOnDemandStreamer SetPatPMT is about to be called");
                mPtrHnOnDemandStreamerSource->SetPatPMT(mPsi);
                tCpePgrmHandle m_pPgmHandle   =  mPtrHnOnDemandStreamerSource->getPgrmHandle();
                if (m_pPgmHandle != NULL)
                {
                    mptrcaStream = new InMemoryStream;
                    if (NULL == mptrcaStream)
                    {
                        LOG(DLOGL_ERROR, "mptrcaStream is NULL because , failed to allocte memory");
                    }
                    else
                    {
                        if (mCCICBFn)
                        {
                            LOG(DLOGL_NORMAL, " mCCICBFn is not NULL,so cci callback is registered with Inmemorystreaming session");
                            mregid = mptrcaStream->RegisterCCICallback(mCBData, mCCICBFn);
                        }
                        else
                        {
                            LOG(DLOGL_ERROR, " mCCICBFn is  NULL,so cci callback is not registered with Inmemorystreaming session");
                        }
                        LOG(DLOGL_NORMAL, "HnOnDemandStreamer calls InMemoryStream::startDeScrambling  with source id %d", mPtrHnOnDemandRFSource->getSourceId());
                        mptrcaStream->DeScrambleSource(mPtrHnOnDemandRFSource->getSourceId(), m_pPgmHandle, false, &mCamCaHandle);
                        LOG(DLOGL_NORMAL, "cam handle returned is %p", mCamCaHandle);
                    }
                    status = mPtrHnOnDemandStreamerSource->InjectCCI(m_CCIbyte);
                    if (kMspStatus_Ok != status)
                    {
                        LOG(DLOGL_ERROR, "Injection CCI value via MPSHnondemandstreamer source fails");
                    }

                    status = mPtrHnOnDemandStreamerSource->start();
                    if (status != kMspStatus_Ok)
                    {
                        LOG(DLOGL_ERROR, "HnOnDemandStreamer source start failed...!");
                        if (mptrcaStream)
                        {
                            LOG(DLOGL_ERROR, "%s:Hnsrvmgr start fails ,so cleaning up the session", __func__);
                            mptrcaStream->unRegisterCCICallback(mregid);
                            mptrcaStream->shutdown();
                            delete mptrcaStream;
                            mCamCaHandle = NULL;
                            mptrcaStream = NULL;

                        }
                    }
                    else
                    {
                        onDemandState.Change(kOnDemandStateStreaming);
                    }
                }
            }

            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_EMERGENCY, "After doing everything failed in the last step. So sad...!CleanUp the session");
                if (mPtrHnOnDemandStreamerSource != NULL)
                {
                    if (mPtrHnOnDemandStreamerSource->stop() != kMspStatus_Ok)
                    {
                        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s:%d mPtrHnOnDemandStreamerSource streaming Stop failed", __FUNCTION__, __LINE__);
                    }

                    delete mPtrHnOnDemandStreamerSource;
                    mPtrHnOnDemandStreamerSource = NULL;
                }
            }
            else
            {
                LOG(DLOGL_EMERGENCY, "NICE to see that everything went well and streaming started...!");
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Creating  HnOnDemandStreamer source failed...!");
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "mPtrHnOnDemandRFSource is NULL...!");
    }
}



/**
 *   @param CCIbyte       - CCI value from CAM module
 *
 *   @return None
 *   @brief Function      - Function to inject CCI value to the stream via source
                            This function is triggered from IMediaplayer whenever there is a change in CCI Bits for the VOD content streaming
 *
 */

void  HnOnDemandStreamer::InjectCCI(uint8_t CCIbyte)
{
    m_CCIbyte = CCIbyte;

    if (mPtrHnOnDemandStreamerSource)
    {
        LOG(DLOGL_NORMAL, "mPtrHnOnDemandStreamerSource is not NULL,so injectcci is called with the cci value %u", m_CCIbyte);
        eMspStatus status = mPtrHnOnDemandStreamerSource->InjectCCI(m_CCIbyte);
        if (status == kMspStatus_Ok)
        {
            LOG(DLOGL_REALLY_NOISY, " mPtrHnOnDemandStreamerSource->InjectCCI returns Success");
        }
        else
        {
            LOG(DLOGL_ERROR, " mPtrHnOnDemandStreamerSource->InjectCCI returns Error");
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "mPtrHnOnDemandStreamerSource is NULL");

    }
}

/// This method Flushes the pending events from Event thread Queue and stops streaming
eIMediaPlayerStatus HnOnDemandStreamer::StopStreaming()
{
    FNLOG(DL_MSP_MRDVR);
    eIMediaPlayerStatus playerStatus = kMediaPlayerStatus_Ok;
    eMspStatus sourceStatus = kMspStatus_Ok;
    LOG(DLOGL_REALLY_NOISY, "Moving to Suspended State to avoid processing further events");
    mOndemandZapperState = kZapperStateSuspended;
    LOG(DLOGL_REALLY_NOISY, "Flushing the event queue of any pending events and stopping streaming source");
    if (mThreadEventQueue)
    {
        mThreadEventQueue->flushQueue(); //flush out any pending events posted
        if (mPtrHnOnDemandStreamerSource != NULL)
        {
            sourceStatus = mPtrHnOnDemandStreamerSource->release();
            if (sourceStatus != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "%s:InMemoryStreamerSource release failed\n", __FUNCTION__);
                playerStatus = kMediaPlayerStatus_Error_OutOfState;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "%s:NULL InMemoryStreamerSource\n", __FUNCTION__)
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

