/**
 \file ondemand.cpp
 \class OnDemand

 Implementation file for ondemand media controller class
 */

///////////////////////////////////////////////////////////////////////////
//                    Standard Includes
///////////////////////////////////////////////////////////////////////////
#include <list>
#include <assert.h>
#if defined(DMALLOC)
#include "dmalloc.h"
#endif

#include <sstream>
#include <iostream>

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
#include "ondemand.h"
#include "zapper.h"
#include "eventQueue.h"
#include "SeaChange_StreamControl.h"
#include "pthread_named.h"
#include <sail-message-api.h>
#include <csci-base-message-api.h>
///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

#define UNUSED_PARAM(a) (void)a;

#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_ONDEMAND, level,"OnDemand(TID:%lx):%s:%d " msg, pthread_self(), __FUNCTION__, __LINE__, ##args);

#ifdef HEREIAM
#error  HEREIAM already defined
#endif
#define HEREIAM  LOG(DLOGL_REALLY_NOISY, "HEREIAM")

bool OnDemand::mEasAudioActive = false;

///////////////////////////////////////////////////////////////////////////
//                      Member implementation
///////////////////////////////////////////////////////////////////////////

// static member inititialization
std::list<void*> OnDemand::mActiveControllerList;
pthread_mutex_t  OnDemand::mControllerListMutex;
bool             OnDemand::mIsActControlListInititialized = false;

EventLoop*       OnDemand::mEventLoop = NULL;
pthread_t        OnDemand::evtloopHandlerThread;
struct event     *gSessSocketEvent;


OnDemandState::OnDemandState()
{
    mState = kOnDemandStateInit;
}

void OnDemandState::Change(eOnDemandState newState)
{
    LOG(DLOGL_MINOR_DEBUG, "change state from %d to %d", mState, newState);
    mState = newState;
}

/** *********************************************************
*/
OnDemand::OnDemand()
{
    FNLOG(DL_MSP_ONDEMAND);

    onDemandState.Change(kOnDemandStateInit);

    if (!mIsActControlListInititialized)
    {
        pthread_mutex_init(&mControllerListMutex, NULL);
        mIsActControlListInititialized = true;
    }

    mSrcUrl = "";
    mDestUrl = "";
    mStartNptMs = 0;

    callback = NULL;
    mpSession = NULL;
    callbackContext = NULL;

    pthread_mutex_init(&mMutex, NULL);

    vodStreamContrl = NULL;
    vodSessContrl = NULL;

    mStreamSocketEvent = NULL;
    mEventTimer = NULL;

    tunerController =  NULL;

    sessionSetupRetryCount = 0;
    mSessionActivatedTime = NULL;

    mCurrentNumerator = 100;
    mCurrentDenominator = 100;
    mCurrentSetPosition = 0;
    mPendingPlayResponse = false;
    mPendingSpeed = false;
    mPendingPosition = false;

    pthread_mutex_init(&mStopMutex, NULL);
    pthread_cond_init(&mStopCond, NULL);

    // initialize OnDemandSystemClient to get IP addresses and SG ID
    OnDemandSystemClient::getInstance();


    if (!mEventLoop)
    {
        LOG(DLOGL_EMERGENCY, "Creating event loop for first time");

        mEventLoop = new EventLoop();
        if (!mEventLoop)
        {
            LOG(DLOGL_EMERGENCY, "Error creating EventLoop");
            assert(mEventLoop);
        }

        ELReturnCode result = mEventLoop->softInit();
        if (result != EL_SUCCESS)
        {
            LOG(DLOGL_EMERGENCY, "Error init mEventLoop");
            assert(result == EL_SUCCESS);
        }

        // add dummy timer
        const long int TEN_YEARS_SECS = 315360000;
        mEventLoop->addTimer(EVENTTIMER_TIMEOUT, TEN_YEARS_SECS, 0, dummyLongTime_cb, NULL);

        gSessSocketEvent = event_new(mEventLoop->getBase(),
                                     VOD_SessionControl::GetFD(), EV_READ | EV_PERSIST,
                                     VOD_SessionControl::ReadSessionSocketCallback, NULL);

        int err = event_add(gSessSocketEvent, NULL);
        if (err)
        {
            event_free(gSessSocketEvent);
            gSessSocketEvent = NULL;
            LOG(DLOGL_ERROR, "warning gSessSocketEvent event_add err: %d", err);
        }

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, (128 * 1024));

        // By default the thread is created as joinable.
        err  = pthread_create(&evtloopHandlerThread, &attr, evtloopthreadFunc, NULL);
        if (!err)
        {
            // failing to set name is not considered an major error
            int retval = pthread_setname_np(evtloopHandlerThread, "OnDemand_EvntHandlr");
            if (retval)
            {
                LOG(DLOGL_ERROR, "pthread_setname_np error: %d - ignore", retval);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "pthread_create error %d\n", err);
        }

    }
    else
    {
        LOG(DLOGL_MINOR_DEBUG, "event thread already init");
    }

    onDemandState.Change(kOnDemandStateWaitForLoad);
}



void OnDemand::dummyLongTime_cb(evutil_socket_t fd, short event, void *arg)
{

    UNUSED_PARAM(fd);
    UNUSED_PARAM(event);
    UNUSED_PARAM(arg);

    LOG(DLOGL_EMERGENCY, "No way should this ever get here");
    assert(true);
}


/** *********************************************************
 */
OnDemand::~OnDemand()
{
    FNLOG(DL_MSP_ONDEMAND);

    std::list<CallbackInfo*>::iterator iter;
    for (iter = mCallbackList.begin(); iter != mCallbackList.end(); iter++)
    {
        delete(*iter);
        *iter = NULL;
    }
    mCallbackList.clear();


    LOG(DLOGL_REALLY_NOISY, "delete tunerController\n");
    delete tunerController;
    tunerController = NULL;
    // move to stopEventThread
#if 0
    LOG(DLOGL_REALLY_NOISY, "delete mEventLoop\n");
    delete mEventLoop;
#endif

    // TODO:  is this needed?
    pthread_mutex_destroy(&mMutex);
    pthread_mutex_destroy(&mStopMutex);
    pthread_cond_destroy(&mStopCond);

    LOG(DLOGL_MINOR_DEBUG, "OnDemand Destructed");
}


/** *********************************************************
 */
void* OnDemand::evtloopthreadFunc(void *data)
{
    // The main event loop.  Uses eventLib

    UNUSED_PARAM(data)
    LOG(DLOGL_REALLY_NOISY, "Enter");
    OnDemand::mEventLoop->loop();
    LOG(DLOGL_EMERGENCY, "ERROR: evtloopthreadFunc exits, should not be here");
    assert(1);
    pthread_exit(NULL);
}


/** *********************************************************
 *
 */
void OnDemand::SessionKeepAliveTimerCallBack(int fd, short event, void *arg)
{
    // TODO: Test this - verify EventTimer is returned in args
    UNUSED_PARAM(fd)
    UNUSED_PARAM(event)

    EventTimer* pEvt = (EventTimer*) arg;

    if (pEvt)
    {
        EventCallbackData *sessKeepAliveEvt = (EventCallbackData *) pEvt->getUserData();

        if (sessKeepAliveEvt)
        {
            bool result = OnDemand::checkOnDemandInstActive(sessKeepAliveEvt);
            if (result)
            {
                OnDemand *onDemandInst = sessKeepAliveEvt->objPtr;
                if (onDemandInst)
                {
                    onDemandInst->lockMutex();
                    onDemandInst->HandleCallback(sessKeepAliveEvt->odEvtType);
                    onDemandInst->unLockMutex();
                }
                else
                {
                    LOG(DLOGL_ERROR, "Null onDemandInst");
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "OnDemand session is not active ");
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Null sessKeepAliveEvt");
        }
    }
    else
    {
        LOG(DLOGL_ERROR, " Null pEvt");
    }
}



/** *********************************************************
 */
eIMediaPlayerStatus OnDemand::DoCallback(eIMediaPlayerSignal sig,
        eIMediaPlayerStatus stat)
{
    FNLOG(DL_MSP_ONDEMAND);
    tIMediaPlayerCallbackData cbData = {sig, stat, {0}};

    std::list<CallbackInfo *>::iterator iter;
    for (iter = mCallbackList.begin(); iter != mCallbackList.end(); iter++)
    {
        if ((*iter)->mCallback != NULL)
        {
            (*iter)->mCallback((*iter)->mpSession, cbData, (*iter)->mClientContext, NULL);
        }
    }

    return kMediaPlayerStatus_Ok;
}


/** *********************************************************
 */
void OnDemand::lockMutex(void)
{
    pthread_mutex_lock(&mMutex);
}

/** *********************************************************
 */
void OnDemand::unLockMutex(void)
{
    pthread_mutex_unlock(&mMutex);
}



// return 0 on success
int OnDemand::startEventThread()
{
    FNLOG(DL_MSP_ONDEMAND);

    // precondition:  mEventLoop is initialized (in Load)


    // TODO:  Start event with dummy timer event - then add controller and real events

    // Log controller as active after thread has started
    pthread_mutex_lock(&mControllerListMutex);
    mActiveControllerList.push_back(this);
    pthread_mutex_unlock(&mControllerListMutex);

    return 0;
}


void OnDemand::stopEventThread()
{
    FNLOG(DL_MSP_ONDEMAND);

    // queue event for thread to stop
    //    queue event will quit when this is seen?
    //   - this will remove all interests
    //   - wait for thread to exit
    //   - will there be a period when a queue event may occur
    //   - ensure all queue events happen in context of event handler???
    //   -


    pthread_mutex_lock(&mStopMutex);
    queueEvent(kOnDemandStopControllerEvent);

    // wait for signal
    LOG(DLOGL_REALLY_NOISY, "OnDemand::%s wait for stop signal", __FUNCTION__);
    int rc = pthread_cond_wait(&mStopCond, &mStopMutex);
    // rc = pthread_cond_timedwait(&mCond, &mMutex, &ts);

    pthread_mutex_unlock(&mStopMutex);

    LOG(DLOGL_REALLY_NOISY, "OnDemand::%s stop signal received rc: %d", __FUNCTION__, rc);

#if 0
    pthread_mutex_lock(&mControllerListMutex);
    // remove from active controller list and stop thread
    LOG(DLOGL_REALLY_NOISY, "mActiveControllerList.remove %p", this);
    mActiveControllerList.remove(this);
    pthread_mutex_unlock(&mControllerListMutex);
#endif

#if 0
    if (evtloopHandlerThread)
    {
        // TODO: Remove this - see if really necesasry
        // LOG(DLOGL_MINOR_DEBUG, "wait for pthread_cancel");
        // pthread_cancel(evtloopHandlerThread); // wait for event thread to exit
        LOG(DLOGL_MINOR_DEBUG, "wait for pthread_join");
        pthread_join(evtloopHandlerThread, NULL); // wait for event thread to exit
        evtloopHandlerThread = 0;
    }
    else
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "warning no evtloopHandlerThread");
    }

    LOG(DLOGL_REALLY_NOISY, "delete mEventLoop\n");
    delete mEventLoop;
#endif

}


/** *********************************************************
 */
eIMediaPlayerStatus OnDemand::Load(const char* serviceUrl,
                                   const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_ONDEMAND);
    UNUSED_PARAM(pMme);

    LOG(DLOGL_NORMAL, "url: %s", serviceUrl);

    if (onDemandState.Get() != kOnDemandStateWaitForLoad)
    {
        LOG(DLOGL_ERROR, "Error: state: %d", onDemandState.Get());
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (!serviceUrl)
    {
        LOG(DLOGL_ERROR, "Error null serviceUrl");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

#if 0
    // create mEventLoop
    // must be done first since setupSessionAndStreamControllers add an event

    mEventLoop = new EventLoop();
    if (!mEventLoop)
    {
        LOG(DLOGL_EMERGENCY, "Error creating EventLoop");
        assert(mEventLoop);
    }

    ELReturnCode result =
        qq
        mEventLoop->softInit();   // event_base_new()
    if (result != EL_SUCCESS)
    {
        LOG(DLOGL_EMERGENCY, "Error init mEventLoop");
        assert(result == EL_SUCCESS);
    }
#endif

    // TODO:  How about starting this is a separate thread and wait for signal for
    //        when it is started?

    mSrcUrl = serviceUrl;

    eIMediaPlayerStatus status = kMediaPlayerStatus_Error_Unknown;

    status = setupSessionAndStreamControllers(serviceUrl);
    if (status == kMediaPlayerStatus_Ok)
    {
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

    // TODO:  Determine state transition if error

    return status;
}

eIMediaPlayerStatus OnDemand::setupSessionAndStreamControllers(const char* serviceUrl)
{
    if (vodSessContrl)
    {
        LOG(DLOGL_ERROR, "warning deleting existing vodSessContrl: %p", vodSessContrl);
        delete vodSessContrl;
        vodSessContrl = NULL;
    }

    vodSessContrl = VODFactory::getVODSessionControlInstance(this, serviceUrl);
    if (vodSessContrl)
    {
        if (vodStreamContrl)
        {
            LOG(DLOGL_ERROR, "warning deleting existing vodStreamContrl: %p", vodStreamContrl);
            delete vodStreamContrl;
            vodStreamContrl = NULL;
        }
        vodStreamContrl = VODFactory::getVODStreamControlInstance(this, serviceUrl);
        if (!vodStreamContrl)
        {
            LOG(DLOGL_ERROR, "Warning: Null vodStreamContrl");
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Warning: Null vodSessContrl");
    }

    return kMediaPlayerStatus_Ok;
}


// static
void OnDemand::tunerCallback(eZapperState state, void *data)
{
    // this is callback from tuner

    // TODO: Since onDemand controls this resource, the OnDemand instance should always be valid
    //       However, may need further analyis on this or test for valid controller

    OnDemand *odPtr = (OnDemand *) data;

    if (odPtr)
    {
        switch (state)
        {
        case kZapperStateRendering:
        {
            LOG(DLOGL_MINOR_EVENT, "kZapperStateRendering!!!!");

            //TODO: Use queueEvent for this
            EventCallbackData *evt = new EventCallbackData;
            if (evt)
            {
                evt->objPtr = odPtr;
                evt->odEvtType = kOnDemandDisplayRenderingEvent;
                event_base_once(odPtr->GetEventLoop()->getBase(),
                                -1,
                                EV_TIMEOUT,
                                odPtr->OnDemandEvtCallback,
                                (void *)evt,
                                NULL);
            }
        }
        break;

        default:
            LOG(DLOGL_MINOR_EVENT, "warning: recvd event: %d", state);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, " NULL odPtr");
    }
}



/** *********************************************************
 */
void OnDemand::Eject()
{
    FNLOG(DL_MSP_ONDEMAND);
    if (tunerController)
    {
        LOG(DLOGL_MINOR_DEBUG, "tunerController->Eject");

        tunerController->lockMutex();
        tunerController->Eject();
        tunerController->unLockMutex();
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: Null tunerController");
    }
}

/** *********************************************************
 */
eIMediaPlayerStatus OnDemand::Play(const char* outputUrl,
                                   float nptStartTime,
                                   const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_ONDEMAND);

    UNUSED_PARAM(pMme)
    // TODO: How to handle PMme?

    eOnDemandState currentState = onDemandState.Get();

    if (currentState != kOnDemandStatePreparingToView)
    {
        LOG(DLOGL_ERROR, "Error: state: %d", currentState);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (outputUrl)
    {
        mDestUrl = outputUrl;
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: NULL outputUrl... Proceeding further with this error");
    }


    // TODO: This may need to change if CMOD provides NPT in milliseconds
    mStartNptMs = nptStartTime * 1000;   // convert seconds to ms
    if (vodSessContrl)
    {
        eIMediaPlayerStatus errStatus = vodSessContrl->SessionSetup(mSrcUrl);

        if (!errStatus)
        {
            LOG(DLOGL_NOISE, "SessionSetup OK");
            onDemandState.Change(kOnDemandStateSessionPending);
        }
        else
        {
            LOG(DLOGL_ERROR, "SessionSetup error: 0x%x", errStatus);
            //IMP_NOTE: If we return Synchronous Error to MDA,UI does not display Error Barker
            //Hence passing Asynchronous Error to MDA and returning kMediaPlayerStatus_Ok from here.
            //Our aim is to show an Error barker to User, if session setup failed.
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ServerError);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: Null vodSessContrl");
        return kMediaPlayerStatus_Error_OutOfState;
    }
    return kMediaPlayerStatus_Ok;
}

/** *********************************************************
  /returns always returns kMediaPlayerStatus_Error_NotSupported
 */
eIMediaPlayerStatus OnDemand::PersistentRecord(const char* recordUrl,
        float nptRecordStartTime, float nptRecordStopTime,
        const MultiMediaEvent **pMme)
{
    UNUSED_PARAM(recordUrl)
    UNUSED_PARAM(nptRecordStartTime)
    UNUSED_PARAM(nptRecordStopTime)
    UNUSED_PARAM(pMme)
    FNLOG(DL_MSP_MPLAYER);

    return kMediaPlayerStatus_Error_NotSupported;
}

/** *********************************************************
 */
eIMediaPlayerStatus OnDemand::Stop(bool stopPlay, bool stopPersistentRecord)
{
    FNLOG(DL_MSP_ONDEMAND);

    // TODO:  check state??


    if (tunerController)
    {
        tunerController->lockMutex();
        tunerController->Stop(stopPlay, stopPersistentRecord);
        tunerController->unLockMutex();
    }

    if (vodStreamContrl)
    {
        vodStreamContrl->StreamTearDown();
    }

    if (vodSessContrl)
    {
        if ((onDemandState.Get() >= kOnDemandSessionServerReady) && (onDemandState.Get() <= kOnDemandStateStopPending))
        {
            vodSessContrl->SessionTeardown();
        }
        else
        {
            LOG(DLOGL_MINOR_DEBUG, "vodSessContrl->SessionTeardown not sent in state: %d", onDemandState.Get());
        }
    }


    onDemandState.Change(kOnDemandStateStopPending);

    //Adding this here to free VodSessionController,VodStreamController
    //Close both session and stream control sockets.
    unLockMutex();
    stopEventThread();
    lockMutex();

    return kMediaPlayerStatus_Ok;
}

/** *********************************************************
 */
eIMediaPlayerStatus OnDemand::SetSpeed(int numerator, unsigned int denominator)
{
    FNLOG(DL_MSP_ONDEMAND);


    if (onDemandState.Get() != kOnDemandStateStreaming)
    {
        LOG(DLOGL_NORMAL, "warning - no action in state %d", onDemandState.Get());
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if ((numerator == 0) && (denominator == 0))
    {
        LOG(DLOGL_ERROR, "Error invalid params num: %d  denom: %d", numerator, denominator);
        return kMediaPlayerStatus_Error_InvalidParameter;
    }


    if ((mPendingSpeed == false) && (numerator == mCurrentNumerator) && (denominator == mCurrentDenominator))
    {
        LOG(DLOGL_NORMAL, "Request for already set speed of numerator %d denominator %d.Ignoring it ", numerator, denominator);
        return kMediaPlayerStatus_Ok;
    }

    mCurrentNumerator = numerator;
    mCurrentDenominator = denominator;

    if (mPendingPlayResponse == true)
    {
        LOG(DLOGL_NORMAL, "Response from VOD server on previous speed/position setting is pending.Delaying the new speed setting until then");
        mPendingSpeed = true;
        return kMediaPlayerStatus_Ok;
    }

    mPendingPlayResponse = true;

    if (vodStreamContrl)
    {
        if (numerator == 0)
        {
            vodStreamContrl->StreamPause();
        }
        else
        {
            vodStreamContrl->StreamSetSpeed((int16_t) numerator, (uint16_t) denominator);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: Null vodStreamContrl");
    }
    // TODO:  Does streamControl return a status that can be used?

    return kMediaPlayerStatus_Ok;
}

/** *********************************************************
  \returns Both *pNumerator and *pDenominator are set to 1 since set speed is not supported
 */
eIMediaPlayerStatus OnDemand::GetSpeed(int* pNumerator,
                                       unsigned int* pDenominator)
{
    FNLOG(DL_MSP_ONDEMAND);

    if (onDemandState.Get() != kOnDemandStateStreaming)
    {
        LOG(DLOGL_NORMAL, "warning - no action in state %d", onDemandState.Get());
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if ((pNumerator == NULL) || (pDenominator == NULL) || vodStreamContrl == NULL)
    {
        LOG(DLOGL_ERROR, "Error null *numerator or *denominator");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    eIOnDemandStatus strmStatus = vodStreamContrl->StreamGetSpeed((int16_t *) pNumerator,
                                  (uint16_t *) pDenominator);

    eIMediaPlayerStatus ret;

    if (strmStatus == ON_DEMAND_OK)
    {
        ret = kMediaPlayerStatus_Ok;
    }
    else
    {
        ret = kMediaPlayerStatus_Error_Unknown;
    }

    return ret;
}

/** *********************************************************
  \returns always returns kMediaPlayerStatus_Error_NotSupported
 */
eIMediaPlayerStatus OnDemand::SetPosition(float nptTime)
{
    FNLOG(DL_MSP_ONDEMAND);

    LOG(DLOGL_NORMAL, "nptTime %f secs", nptTime);

    if ((vodStreamContrl == NULL) || (onDemandState.Get() != kOnDemandStateStreaming))
    {
        LOG(DLOGL_NORMAL, "warning - no action in state %d", onDemandState.Get());
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (mPendingPlayResponse == true)
    {
        LOG(DLOGL_NORMAL, "Response from VOD server on previous speed/position setting is pending.Delaying the new position setting until then");
        mCurrentSetPosition = nptTime;
        mPendingPosition = true;
        return kMediaPlayerStatus_Ok;
    }

    mPendingPlayResponse = true;

    LOG(DLOGL_NORMAL, " SetPosition nptTime: %f",  nptTime);


    float nptTimeMs = nptTime * 1000;

    vodStreamContrl->StreamSetNPT(nptTimeMs);

    return  kMediaPlayerStatus_Ok;
}

/** *********************************************************
  \returns always returns kMediaPlayerStatus_Error_NotSupported
 */
eIMediaPlayerStatus OnDemand::GetPosition(float* pNptTime)
{
    FNLOG(DL_MSP_ONDEMAND);

    if ((vodStreamContrl == NULL) || (onDemandState.Get() != kOnDemandStateStreaming))
    {
        LOG(DLOGL_NORMAL, "warning - no action in state %d", onDemandState.Get());
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (pNptTime == NULL)
    {
        LOG(DLOGL_ERROR, "Error null *pNptTime");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    eIOnDemandStatus  strmStatus = vodStreamContrl->StreamGetPosition(pNptTime);

    eIMediaPlayerStatus ret;

    if (strmStatus == ON_DEMAND_OK)
    {
        ret = kMediaPlayerStatus_Ok;
        LOG(DLOGL_NORMAL, "returning OK - nptTime: %f",  *pNptTime);
    }
    else
    {
        ret = kMediaPlayerStatus_Error_Unknown;
    }

    return ret;
}

/** *********************************************************
 */
eIMediaPlayerStatus OnDemand::IpBwGauge(const char *sTryServiceUrl,
                                        unsigned int *pMaxBwProvision,
                                        unsigned int *pTryServiceBw,
                                        unsigned int *pTotalBwCoynsumption)
{
    FNLOG(DL_MSP_ONDEMAND);
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    if (tunerController)
    {
        tunerController->lockMutex();
        status = tunerController->IpBwGauge(sTryServiceUrl,
                                            pMaxBwProvision,
                                            pTryServiceBw,
                                            pTotalBwCoynsumption);
        tunerController->unLockMutex();
    }

    return status;
}

/** *********************************************************
 */
eIMediaPlayerStatus OnDemand::SetPresentationParams(tAvRect *vidScreenRect,
        bool enablePictureModeSetting,
        bool enableAudioFocus)
{
    FNLOG(DL_MSP_ONDEMAND);
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    if (tunerController)
    {
        tunerController->lockMutex();
        status = tunerController->SetPresentationParams(vidScreenRect,
                 enablePictureModeSetting,
                 enableAudioFocus);
        tunerController->unLockMutex();
    }
    return status;
}



/** *********************************************************
  \returns always returns kMediaPlayerStatus_Error_NotSupported
 */
eIMediaPlayerStatus OnDemand::GetStartPosition(float* pNptTime)
{
    FNLOG(DL_MSP_ONDEMAND);

    // TODO: Verify this should always be zero
    *pNptTime = 0;

    return kMediaPlayerStatus_Ok;
}


/** *********************************************************
  \returns always returns kMediaPlayerStatus_Error_NotSupported
 */
eIMediaPlayerStatus OnDemand::GetEndPosition(float* pNptTime)
{
    FNLOG(DL_MSP_ONDEMAND);

    *pNptTime = vodSessContrl->getEndPosition();
    return kMediaPlayerStatus_Ok;
}

/** *********************************************************
 */
eIMediaPlayerStatus OnDemand::SetCallback(
    IMediaPlayerSession *pIMediaPlayerSession,
    IMediaPlayerStatusCallback cb, void* pClientContext)
{
    FNLOG(DL_MSP_ONDEMAND);

    if (tunerController)
    {
        LOG(DLOGL_MINOR_DEBUG, "tunerController: %p exists already.. Deleting it...!", tunerController);
        delete tunerController;
        tunerController = NULL;
    }

    tunerController = new Zapper(true, NULL);
    if (!tunerController)
    {
        LOG(DLOGL_EMERGENCY, "Error allocating Zapper for OnDemand");
        assert(tunerController);    // this is bad enough to reboot
    }

    LOG(DLOGL_MINOR_DEBUG, "tunerController: %p", tunerController);

    tunerController->registerZapperCallback(tunerCallback, this);

    LOG(DLOGL_MINOR_DEBUG, "pIMediaPlayerSession: %p   tunerController: %p", pIMediaPlayerSession, tunerController);

    if (tunerController)
    {
        tunerController->lockMutex();
        eIMediaPlayerStatus status = tunerController->SetCallback(pIMediaPlayerSession, cb, pClientContext);
        tunerController->unLockMutex();

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
            LOG(DLOGL_EMERGENCY, "Error: Unable to alloc mem");
            return status;
        }
    }
    else
    {
        LOG(DLOGL_EMERGENCY, "Error: Null tunerController");
        return kMediaPlayerStatus_Error_OutOfState;
    }
    return kMediaPlayerStatus_Ok;
}

eIMediaPlayerStatus OnDemand::DetachCallback(IMediaPlayerStatusCallback cb)
{
    FNLOG(DL_MSP_ONDEMAND);
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;
    if (tunerController)
    {
        tunerController->lockMutex();
        status = tunerController->DetachCallback(cb);
        tunerController->unLockMutex();
    }

    std::list<CallbackInfo*>::iterator iter;
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



void OnDemand::StartSessionKeepAliveTimer()
{
    uint32_t keepAliveTime = 0;
    OnDemandSystemClient::getInstance()->GetSessionInProgressTimer(&keepAliveTime);
    LOG(DLOGL_REALLY_NOISY, "OnDemand::%s  SIP timer value: %d", __FUNCTION__, keepAliveTime);

    EventCallbackData *timerEvt = new EventCallbackData;

    if (timerEvt)
    {
        timerEvt->objPtr = this;
        timerEvt->odEvtType = kOnDemandKeepAliveTimerEvent;

        // event_new and event_add for timer
        mEventTimer = mEventLoop->addTimer(EVENTTIMER_PERSIST,
                                           keepAliveTime,
                                           0,
                                           SessionKeepAliveTimerCallBack,
                                           timerEvt);
    }
}



eIMediaPlayerStatus OnDemand::loadTuningParamsAndPlay()
{
    FNLOG(DL_MSP_ONDEMAND);
    // called from event handler in response to session setup

    //assert(onDemandState.Get() == kOnDemandSessionServerReady);
    if (onDemandState.Get() != kOnDemandSessionServerReady)
    {
        LOG(DLOGL_NORMAL, "warning - no action in state %d", onDemandState.Get());
        return kMediaPlayerStatus_Error_OutOfState;
    }
    // Set tuning parameters
    tCpeSrcRFTune tuningParams = vodSessContrl->GetTuningParameters();
    int           programNum   = vodSessContrl->GetProgramNumber();

    if (tunerController)
    {
        LOG(DLOGL_REALLY_NOISY, "tunerController->Load");
        tunerController->lockMutex();
        eIMediaPlayerStatus status = tunerController->Load(tuningParams, programNum, NULL);
        tunerController->unLockMutex();

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
                            event_free(mStreamSocketEvent);
                            mStreamSocketEvent = NULL;
                            LOG(DLOGL_ERROR, "event_add null mStreamSocketEvent ");
                            return kMediaPlayerStatus_Error_OutOfState;
                        }
                    }
                    else
                    {
                        LOG(DLOGL_ERROR, "null mStreamSocketEvent");
                        return kMediaPlayerStatus_Error_OutOfState;
                    }
                }
            }
        }
        else
        {
            LOG(DLOGL_SIGNIFICANT_EVENT, "warning tunerController->Load status: %d", status);
            return status;
        }
    }
    else
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "warning: Null tunerController");
        return kMediaPlayerStatus_Error_OutOfState;
    }

    return kMediaPlayerStatus_Ok;
}






void OnDemand::HandleCallback(eOnDemandEvent onDemandEvt)
{
    eOnDemandState currentState = onDemandState.Get();

    LOG(DLOGL_REALLY_NOISY, "onDemandEvt: %d  currentState: %d", onDemandEvt, currentState);

    bool invalidStateForAction = false;

    switch (onDemandEvt)
    {
    case kOnDemandSetupRespEvent:
        if (currentState == kOnDemandStateSessionPending)
        {
            onDemandState.Change(kOnDemandSessionServerReady);
            // load tuning params, start session keep alive, and start stream play
            eIMediaPlayerStatus status = loadTuningParamsAndPlay();
            if (status == kMediaPlayerStatus_Ok)
            {
                onDemandState.Change(kOnDemandControlServerPending);
            }
            else
            {
                LOG(DLOGL_ERROR, "warning: loadTuningParamsAndPlay status %d", status);
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

    case kOnDemandRTSPPlaySuccess:
    case kOnDemandRTSPPlayFailed:
        //for success case No kMediaPlayerSignal_Problem Callback is given
        if (onDemandEvt == kOnDemandRTSPPlayFailed)
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ServerError);

        if (tunerController && (currentState == kOnDemandControlServerPending))
        {
            onDemandState.Change(kOnDemandControlServerReady);
            tunerController->lockMutex();
            eIMediaPlayerStatus status = tunerController->Play(mDestUrl.c_str(), 0, NULL);
            tunerController->unLockMutex();
            if (status != kMediaPlayerStatus_Ok)
            {
                // TODO: different action if Play returns error?
                LOG(DLOGL_SIGNIFICANT_EVENT, " warning: tunerController->Play status: %d  IGNORING??", status);
            }
            onDemandState.Change(kOnDemandTunerPending);
        }
        else
        {
            invalidStateForAction = true;
        }

        mPendingPlayResponse = false;

        if (mPendingPosition == true)
        {
            LOG(DLOGL_NORMAL, "Applying the pending position setting now");
            mPendingPosition = false;
            SetPosition(mCurrentSetPosition);
        }

        if (mPendingSpeed == true)
        {
            LOG(DLOGL_NORMAL, "Applying the pending speed setting now");
            SetSpeed(mCurrentNumerator, mCurrentDenominator);
            mPendingSpeed = false;
        }
        break;

    case kOnDemandPlayRespEvent:
        if (vodStreamContrl == NULL)
        {
            return ;
        }
        switch (vodStreamContrl->GetReturnStatus())
        {
        case LSC_OK:   //No action
            break;
        case LSC_NO_PERMISSION:
            Sail_Message mess;

            LOG(DLOGL_SIGNIFICANT_EVENT, "Callback signal for LSC NO PERMISSION");

            memset((void *)&mess, 0, sizeof(Sail_Message));
            mess.message_type = SAIL_MESSAGE_USER;
            mess.data[0] = kSailMsgEvt_VOD_TrickPlayInformation;	// VOD Message type
            mess.data[1] = kVODPlaybackInfo_TrickPlayNotSupported;	// Trick Play not supported
            mess.strings = NULL;

            LOG(DLOGL_NORMAL, " BEFORE SEND SAIL MSG.\n");
            Csci_BaseMessage_Send(&mess);
            LOG(DLOGL_NORMAL, " AFTER SEND SAIL MSG.\n");

            DoCallback(kMediaPlayerSignal_VodPurchaseNotification, kMediaPlayerStatus_NotAuthorized);
            break;
        case LSC_NO_RESOURCES:
            LOG(DLOGL_SIGNIFICANT_EVENT, "Callback signal for LSC_NO_RESOURCES");
            DoCallback(kMediaPlayerSignal_VodPurchaseNotification, kMediaPlayerStatus_ContentNotFound);
            break;
        case LSC_NO_MEMORY:
            LOG(DLOGL_SIGNIFICANT_EVENT, "Callback signal for LSC_NO_MEMORY");
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_OutOfMemory);
            break;

        case LSC_WRONG_STATE:
            LOG(DLOGL_SIGNIFICANT_EVENT, "Callback signal for LSC_WRONG_STATE");
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_OutOfState);
            break;
        case LSC_BAD_PARAM:
            LOG(DLOGL_SIGNIFICANT_EVENT, "Callback signal for LSC_BAD_PARAM");
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
            LOG(DLOGL_SIGNIFICANT_EVENT, "Callback signal for LSC_SERVER_ERROR");
            LOG(DLOGL_NORMAL, "callback signal from VOD server is %d ,intimating services layer about the problem", vodStreamContrl->GetReturnStatus());
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ServerError);
            break;

        case LSC_BAD_SCALE:
            LOG(DLOGL_NORMAL, "callback signal from VOD server about BAD_SCALE.ignoring it");
            break;


        default:
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Unknown STATE from server");
            break;
        }//end switch*/

        if (tunerController && (currentState == kOnDemandControlServerPending))
        {
            onDemandState.Change(kOnDemandControlServerReady);

            tunerController->lockMutex();
            eIMediaPlayerStatus status = tunerController->Play(mDestUrl.c_str(), 0, NULL);
            tunerController->unLockMutex();
            if (status != kMediaPlayerStatus_Ok)
            {
                // TODO: different action if Play returns error?
                LOG(DLOGL_SIGNIFICANT_EVENT, " warning: tunerController->Play status: %d  IGNORING??", status);
            }

            onDemandState.Change(kOnDemandTunerPending);
        }
        else
        {
            invalidStateForAction = true;
        }

        mPendingPlayResponse = false;

        if (mPendingPosition == true)
        {
            LOG(DLOGL_NORMAL, "Applying the pending position setting now");
            mPendingPosition = false;
            SetPosition(mCurrentSetPosition);
        }

        if (mPendingSpeed == true)
        {
            LOG(DLOGL_NORMAL, "Applying the pending speed setting now");
            SetSpeed(mCurrentNumerator, mCurrentDenominator);
            mPendingSpeed = false;
        }

        break;
    case kOnDemandPlayBOFEvent:
        LOG(DLOGL_NORMAL, "Callback signal for BeginningOfStream");
        DoCallback(kMediaPlayerSignal_BeginningOfStream, kMediaPlayerStatus_Ok);
        break;

    case kOnDemandPlayEOFEvent:
        LOG(DLOGL_NORMAL, "Callback signal for EndOfStream");
        DoCallback(kMediaPlayerSignal_EndOfStream, kMediaPlayerStatus_Ok);
        break;

    case kOnDemandPlayDoneEvent:
    {
        if (vodStreamContrl)
        {
            LOG(DLOGL_REALLY_NOISY, "VOD Server Mode: %hu  ", vodStreamContrl->GetServerMode());
            // TODO: action for this state
            LOG(DLOGL_SIGNIFICANT_EVENT, "kOnDemandPlayDoneEvent rcvd in state: %d  - action??", onDemandState.Get());
            if (vodStreamContrl->GetPos() == 0x00000000)
            {
                if ((vodStreamContrl->GetServerMode() == O_MODE) && (vodStreamContrl->GetNumerator() == 0))
                {
                    LOG(DLOGL_SIGNIFICANT_EVENT, "Callback signal for Time-out EndOfStream");
                    DoCallback(kMediaPlayerSignal_EndOfStream, kMediaPlayerStatus_Ok);
                    onDemandState.Change(kOnDemandStateStopPending);
                }
                else
                {
                    LOG(DLOGL_SIGNIFICANT_EVENT, "Callback signal for BeginningOfStream");
                    DoCallback(kMediaPlayerSignal_BeginningOfStream,
                               kMediaPlayerStatus_Ok);
                }
            }
            else
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "Callback signal for EndOfStream");
                DoCallback(kMediaPlayerSignal_EndOfStream, kMediaPlayerStatus_Ok);
            }
        }
    }
    break;

    case kOnDemandReleaseRespEvent:
        if (currentState == kOnDemandStateStopPending)
        {
            onDemandState.Change(kOnDemandStateStopped);
            LOG(DLOGL_SIGNIFICANT_EVENT, "Changed onDemand state to kOnDemandStateStopped");
        }
        else
        {
            invalidStateForAction = true;
        }
        break;

    case kOnDemandDisplayRenderingEvent:
        if (currentState == kOnDemandTunerPending)
        {
            onDemandState.Change(kOnDemandStateStreaming);
            if (vodSessContrl)
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "Displaying VOD!!! encrypted %d", vodSessContrl->GetEncryptionflag());
                if (vodSessContrl->GetEncryptionflag())
                {
                    CakMsgRcvResult data;
                    uint32_t oldcount, newcount;
                    // get old EMM count
                    oldcount = vodSessContrl->GetEmmCount();
                    newcount = 0;
                    memset(&data, 0, sizeof(CakMsgRcvResult));

                    data.caMsgType = eEMM;
                    if (!cam_getCakMsgRcvResult(0, &data))
                    {
                        newcount = (uint32_t)data.seCureAcceptNum;
                        LOG(DLOGL_MINOR_DEBUG, " emmCount before %u after %u", oldcount, newcount);
                        if (newcount > oldcount)
                        {
                            vodSessContrl->SetCakResp(CAKSTAT_SUCCESS);
                        }
                    }
                }
            }

        }
        else
        {
            invalidStateForAction = true;
        }
        break;


    case kOnDemandKeepAliveTimerEvent:

        LOG(DLOGL_MINOR_EVENT, "VOD Session Keep Alive Timer");

        if (vodSessContrl && (currentState == kOnDemandStateStreaming))
        {
            LOG(DLOGL_MINOR_EVENT, " vodSessContrl->SendKeepAlive");
            vodSessContrl->SendKeepAlive();

            // mEventLoop->delTimer(pEvt);

        }
        else
        {
            LOG(DLOGL_MINOR_EVENT, "warning - not sending keep alive in state: %d", currentState);
            // invalidStateForAction = true;

        }
        break;


    case kOnDemandStreamReadEvent:
        LOG(DLOGL_MINOR_EVENT, "kOnDemandStreamReadEvent");
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
                LOG(DLOGL_NOISE, "SessionSetup OK");
                onDemandState.Change(kOnDemandStateSessionPending);
            }
            else
            {
                LOG(DLOGL_ERROR, "SessionSetup error: 0x%x", errStatus);
            }

            sessionSetupRetryCount++;
        }
        else
        {
            LOG(DLOGL_ERROR, "Callback signal for Session Setup > retry Count error");
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ServerError);
        }
    }
    break;


    case kOnDemandStreamErrorEvent:
    {
        LOG(DLOGL_ERROR, "Callback signal for Stream Setup  error");
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
        LOG(DLOGL_SIGNIFICANT_EVENT, "Remove Controller");

        mActiveControllerList.remove(this);

        LOG(DLOGL_SIGNIFICANT_EVENT, "kOnDemandStopControllerEvent");
        // Free eventlib events

        if (mStreamSocketEvent)
        {
            EventCallbackData *userData = (EventCallbackData *)mStreamSocketEvent->ev_arg;
            if (userData)
            {
                delete userData;
            }
            LOG(DLOGL_MINOR_DEBUG, "Ondemand:HandleCallback: event_del(mStreamSocketEvent) %p", mStreamSocketEvent);
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
            LOG(DLOGL_MINOR_DEBUG, "Ondemand:HandleCallback:mEventLoop->delTimer(mEventTimer) %p", mEventTimer);
            mEventLoop->delTimer(mEventTimer);
        }

        if (vodStreamContrl)
        {
            if (vodSessContrl)
                vodSessContrl->setNpt(vodStreamContrl->GetPos());
            else
                LOG(DLOGL_ERROR, "vodSessContrl is %p", vodSessContrl);

            LOG(DLOGL_MINOR_DEBUG, "delete streamControl %p", vodStreamContrl);
            delete vodStreamContrl;
            vodStreamContrl = NULL;
        }

        if (vodSessContrl)
        {
            LOG(DLOGL_MINOR_DEBUG, "delete sessionControl: %p", vodSessContrl);
            delete vodSessContrl;
            vodSessContrl = NULL;
        }

        LOG(DLOGL_REALLY_NOISY, "Ondemand:HandleCallback:signal done");
        pthread_mutex_lock(&mStopMutex);
        pthread_cond_signal(&mStopCond);
        pthread_mutex_unlock(&mStopMutex);

        LOG(DLOGL_REALLY_NOISY, "Ondemand:HandleCallback: done");
    }
    break;

    default:
        LOG(DLOGL_SIGNIFICANT_EVENT, "warning: event %d not handled", onDemandEvt);
        break;
    }

    if (invalidStateForAction)
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "warning onDemandEvt (%d) recvd in state (%d) - no action taken",
            onDemandEvt, currentState);
    }

}


void OnDemand::HandleSocketReadEvent(eOnDemandEvent onDemandEvt)
{
    FNLOG(DL_MSP_ONDEMAND);
    switch (onDemandEvt)
    {
    case kOnDemandStreamReadEvent:
        LOG(DLOGL_MINOR_EVENT, "kOnDemandStreamReadEvent");
        if (vodStreamContrl)
        {
            vodStreamContrl->ReadStreamSocketData();
        }
        break;
    default:
        LOG(DLOGL_SIGNIFICANT_EVENT, "warning: event %d not handled", onDemandEvt);
        break;
    }
}

// static
bool OnDemand::checkOnDemandInstActive(EventCallbackData *event)
{
    FNLOG(DL_MSP_ONDEMAND);
    bool retValue = false;
    if (event)
    {
        OnDemand *onDemandInst = event->objPtr;

        LOG(DLOGL_REALLY_NOISY, "onDemandInst:%p ", onDemandInst);

        if (onDemandInst)
        {
            // Need to check if this is still a valid controller before calling

            pthread_mutex_lock(&mControllerListMutex);

            list<void *>::iterator it;
            for (it = mActiveControllerList.begin(); it != mActiveControllerList.end(); ++it)
            {
                if (*it == onDemandInst)
                {
                    retValue = true;
                    break;
                }
            }
            pthread_mutex_unlock(&mControllerListMutex);
        }
        else
        {
            LOG(DLOGL_ERROR, " null onDemandInst");
        }
    }
    else
    {
        LOG(DLOGL_ERROR, " null event");
    }
    return retValue;
}

// static
void OnDemand::OnDemandEvtCallback(int32_t fd, int16_t event, void *arg)
{
    FNLOG(DL_MSP_ONDEMAND);

    UNUSED_PARAM(fd)
    UNUSED_PARAM(event)

    EventCallbackData *timerEvt = (EventCallbackData *)arg;
    if (timerEvt)
    {
        bool result = OnDemand::checkOnDemandInstActive(timerEvt);
        if (result)
        {
            OnDemand *onDemandInst = timerEvt->objPtr;
            if (onDemandInst)
            {
                onDemandInst->lockMutex();
                onDemandInst->HandleCallback(timerEvt->odEvtType);
                onDemandInst->unLockMutex();
            }
            else
            {
                LOG(DLOGL_ERROR, " null onDemandInst");
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "session object is not in active session list");
        }
        delete timerEvt;
    }
    else
    {
        LOG(DLOGL_ERROR, "WARNING null timerEvt");
    }
}

// static
void OnDemand::ReadStreamEvtCallback(int32_t fd, int16_t event, void *arg)
{
    FNLOG(DL_MSP_ONDEMAND);

    UNUSED_PARAM(fd)
    UNUSED_PARAM(event)

    EventCallbackData *timerEvt = (EventCallbackData *) arg;

    if (timerEvt)
    {
        bool result = OnDemand::checkOnDemandInstActive(timerEvt);
        if (result)
        {
            OnDemand *onDemandInst = timerEvt->objPtr;
            if (onDemandInst)
            {
                onDemandInst->HandleSocketReadEvent(timerEvt->odEvtType);
            }
            else
            {
                LOG(DLOGL_ERROR, " null onDemandInst");
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "session object is not in active session list");
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "WARNING null timerEvt");
    }
}


eOnDemandState OnDemand::GetOnDemandState() const
{
    eOnDemandState currentState = onDemandState.Get();
    return currentState;
}

/** *********************************************************
 */
eIMediaPlayerStatus OnDemand::queueEvent(eOnDemandEvent evtyp)
{
    LOG(DLOGL_SIGNIFICANT_EVENT, "OnDemand: event posted EvtType:%d", evtyp);
    EventCallbackData *evt = new EventCallbackData;

    if (evt != NULL)
    {
        evt->objPtr = this;
        evt->odEvtType = evtyp;
        event_base_once(mEventLoop->getBase(),
                        -1,
                        EV_TIMEOUT,
                        OnDemandEvtCallback,
                        (void *)evt,
                        NULL);
    }
    return kMediaPlayerStatus_Ok;
}


eIMediaPlayerStatus OnDemand::RegisterCCICallback(void* data, CCIcallback_t cb)
{
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    if (tunerController)
    {
        tunerController->lockMutex();
        status = tunerController->RegisterCCICallback(data, cb);
        tunerController->unLockMutex();
    }
    return status;
}


eIMediaPlayerStatus OnDemand::UnRegisterCCICallback()
{
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    if (tunerController)
    {
        tunerController->lockMutex();
        status = tunerController->UnRegisterCCICallback();
        tunerController->unLockMutex();
    }
    return status;
}


std::string OnDemand::GetSourceURL(bool liveSrcOnly) const
{
    UNUSED_PARAM(liveSrcOnly);
    return mSrcUrl;
}

std::string OnDemand::GetDestURL() const
{
    return mDestUrl;
}

bool OnDemand::isRecordingPlayback() const
{
    return false;
}

bool OnDemand::isLiveSourceUsed() const
{
    return true;
}

bool OnDemand::isLiveRecording() const
{
    return false;
}

eIMediaPlayerStatus OnDemand::SetApplicationDataPid(uint32_t aPid)
{
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    if (tunerController)
    {
        tunerController->lockMutex();
        status = tunerController->SetApplicationDataPid(aPid);
        tunerController->unLockMutex();
    }
    return status;
}

eIMediaPlayerStatus OnDemand::SetApplicationDataPidExt(IMediaPlayerClientSession *ApplnClient)
{
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    if (tunerController)
    {
        status = tunerController->SetApplicationDataPidExt(ApplnClient);
    }

    return status;
}

eIMediaPlayerStatus OnDemand::GetComponents(tComponentInfo *info,
        uint32_t infoSize,
        uint32_t *cnt,
        uint32_t offset)
{
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    if (tunerController)
    {
        tunerController->lockMutex();
        status = tunerController->GetComponents(info, infoSize, cnt, offset);
        tunerController->unLockMutex();
    }

    return status;
}


eIMediaPlayerStatus OnDemand::GetApplicationData(uint32_t bufferSize,
        uint8_t *buffer,
        uint32_t *dataSize)
{
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    if (tunerController)
    {
        tunerController->lockMutex();
        status = tunerController->GetApplicationData(bufferSize, buffer, dataSize);
        tunerController->unLockMutex();
    }
    return status;
}

eIMediaPlayerStatus OnDemand::GetApplicationDataExt(IMediaPlayerClientSession *ApplnClient,
        uint32_t bufferSize,
        uint8_t *buffer,
        uint32_t *dataSize)
{
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    if (tunerController)
    {
        status = tunerController->GetApplicationDataExt(ApplnClient, bufferSize, buffer, dataSize);
    }
    return status;
}

uint32_t OnDemand::GetSDVClentContext(IMediaPlayerClientSession *ApplnClient)
{
    uint32_t retValue = 0;

    if (tunerController)
    {
        retValue = tunerController->GetSDVClentContext(ApplnClient);
    }
    return retValue;
}


eIMediaPlayerStatus OnDemand::SetAudioPid(uint32_t aPid)
{
    FNLOG(DL_MSP_MPLAYER);
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    if (tunerController)
    {
        tunerController->lockMutex();
        status = tunerController->SetAudioPid(aPid);
        tunerController->unLockMutex();
    }
    return status;
}



eIMediaPlayerStatus OnDemand::CloseDisplaySession()
{
    FNLOG(DL_MSP_ONDEMAND);
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;
    if (tunerController)
    {
        tunerController->lockMutex();
        status = tunerController->CloseDisplaySession();
        tunerController->unLockMutex();
    }
    return status;
}


eIMediaPlayerStatus OnDemand::StartDisplaySession()
{
    FNLOG(DL_MSP_ONDEMAND);
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    if (tunerController)
    {
        tunerController->lockMutex();
        status = tunerController->StartDisplaySession();
        tunerController->unLockMutex();
    }
    return status;
}

void OnDemand::handleReadSessionData(void *pMsg)
{
    FNLOG(DL_MSP_ONDEMAND);

    if (vodSessContrl)
    {
        vodSessContrl->HandleSessionResponse(pMsg);
    }

}

uint8_t * OnDemand::GetStreamServerIPAddress()
{
    return vodSessContrl->GetStreamServerIPAddress();
}


uint16_t OnDemand::GetStreamServerPort()
{
    return vodSessContrl->GetStreamServerPort();
}

uint32_t OnDemand::GetStreamHandle()
{
    return vodSessContrl->GetStreamHandle();
}

bool OnDemand::isBackground(void)
{
    return false;
}

void OnDemand::GetMspVodInfo(DiagMspVodInfo *msgInfo)
{
    if (msgInfo)
    {
        msgInfo->sgId = OnDemandSystemClient::getInstance()->getSgId();

        pthread_mutex_lock(&mControllerListMutex);
        list<void *>::iterator it;

        for (it = mActiveControllerList.begin(); it != mActiveControllerList.end(); ++it)
        {
            OnDemand *onDemandInst = (OnDemand *)*it;
            if (onDemandInst)
            {
                msgInfo->sessionId      = onDemandInst->vodSessContrl->GetSessionId();
                msgInfo->state          = onDemandInst->GetOnDemandState();
                // entitlementId valid for encrypted VOD asset, N/A for clear
                msgInfo->Cakresp        = onDemandInst->vodSessContrl->GetCakResp();
                msgInfo->entitlementId  = onDemandInst->vodSessContrl->GetEntitlementId();
                msgInfo->activatedTime  = onDemandInst->mSessionActivatedTime;

            }
        }

        pthread_mutex_unlock(&mControllerListMutex);
    }
}


eCsciMspDiagStatus Csci_Diag_GetMspVodInfo(DiagMspVodInfo *diagInfo)
{
    eCsciMspDiagStatus status = kCsciMspDiagStat_OK;

    if (diagInfo)
    {
        memset(diagInfo, '\0', sizeof(DiagMspVodInfo));
        OnDemand::GetMspVodInfo(diagInfo);

    }
    else
    {
        status = kCsciMspDiagStat_InvalidInput;

    }

    return status;
}


eCsciMspDiagStatus OnDemand::GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(msgInfo);

    return kCsciMspDiagStat_NoData;
}



void OnDemand::addClientSession(IMediaPlayerClientSession *pClientSession)
{
    FNLOG(DL_MSP_ONDEMAND);
    UNUSED_PARAM(pClientSession);
}

void OnDemand::deleteClientSession(IMediaPlayerClientSession *pClientSession)
{
    FNLOG(DL_MSP_ONDEMAND);
    UNUSED_PARAM(pClientSession);
}

void OnDemand::deleteAllClientSession()
{
    FNLOG(DL_MSP_ONDEMAND);
}

void
OnDemand::StopAudio(void)
{
    if (tunerController)
    {
        tunerController->lockMutex();
        tunerController->StopAudio();
        tunerController->unLockMutex();
    }
}


void
OnDemand::RestartAudio(void)
{
    if (tunerController)
    {
        tunerController->lockMutex();
        tunerController->RestartAudio();
        tunerController->unLockMutex();
    }
}

tCpePgrmHandle OnDemand::getCpeProgHandle()
{
    return 0;
}

void OnDemand::SetCpeStreamingSessionID(uint32_t sessionId)
{
    (void) sessionId;
    return;
}
eIOnDemandStatus OnDemand::SetupCloudDVRStream(void)
{
    FNLOG(DL_MSP_ONDEMAND);
    eIOnDemandStatus status = ON_DEMAND_OK;

    stringstream ssUrl;
    stringstream streamHandle;

    //Since onDemand media controller is the one which calls this method we dont do a NULL check here instead directly access pOnDemand
    streamHandle << GetStreamHandle();
    uint8_t* strmIP = GetStreamServerIPAddress();

    ssUrl << "rtsp://" << strmIP << ":" << GetStreamServerPort() << "/";
    LOG(DLOGL_REALLY_NOISY, "%s:[%d] URL:%s streamHandle:%s", __FUNCTION__, __LINE__, (ssUrl.str()).c_str(), (streamHandle.str()).c_str());

    if (strmIP) delete[] strmIP;

    if (vodStreamContrl && vodStreamContrl->StreamSetup(ssUrl.str(), streamHandle.str()) != ON_DEMAND_ERROR)
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "Stream Setup Success ");
    }
    else
    {
        LOG(DLOGL_ERROR, "Stream Setup failed ");
        status = ON_DEMAND_ERROR;
    }

    return status;
}
void  OnDemand:: InjectCCI(uint8_t CCIbyte)
{
    LOG(DLOGL_ERROR, " InjectCCI in ondemand controller is not supported");
    UNUSED_PARAM(CCIbyte);
}

eIMediaPlayerStatus OnDemand::StopStreaming()
{
    // By default the IMediaController base class requires an implementation
    // of StopStreaming since its an abstract class.
    return kMediaPlayerStatus_Error_NotSupported;
}

// Responsible for starting the EAS audio playback
// This API is applicable for the controllers associated with EAS audio playback session
// For the controllers associated with non EAS audio playback sessions simply returned
// Since OnDemand is not the controller associated with EAS audio playback session,
// API is returning here without any action performed
void OnDemand::startEasAudio(void)
{
    dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(), is not responsible for controlling EAS", __FUNCTION__);
    return;
}

// Sets the EAS audio playback active status
// True  - EAS audi playback is in progress
// False - EAS audio playback is not in progress
void OnDemand::SetEasAudioActive(bool active)
{
    mEasAudioActive = active;
}
