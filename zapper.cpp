/**
   \file zapper.cpp
   \class zapper

Implementation file for zapper media controller class
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
#include <glib.h>

///////////////////////////////////////////////////////////////////////////
//                    SAIL Includes
///////////////////////////////////////////////////////////////////////////
#include <sail-clm-api.h>
#include <dlog.h>
#include <Cam.h>

///////////////////////////////////////////////////////////////////////////
//                    Local Includes
///////////////////////////////////////////////////////////////////////////
#include "zapper.h"
#include "DisplaySession.h"
#include "psi.h"
#include "languageSelection.h"
#include "eventQueue.h"
#include "MSPRFSource.h"
#include"pthread_named.h"
#include "AnalogPsi.h"

#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"Zapper:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

#define UNUSED_PARAM(a) (void)a;

static volatile bool tunerLocked = false;       // DEBUG ONLY
static struct timeval tv_start, tv_stop;
bool Zapper::mEasAudioActive = false;

///////////////////////////////////////////////////////////////////////////
//                      Member implementation
///////////////////////////////////////////////////////////////////////////


/** *********************************************************
*/
void* Zapper::eventthreadFunc(void *data)
{
    // This is a static method running in a pthread and does not have
    // direct access to class variables.  Access is through data pointer.

    FNLOG(DL_MSP_ZAPPER);

    Zapper*     inst        = (Zapper*)data;
    MSPEventQueue* eventQueue  = inst->threadEventQueue;
    assert(eventQueue);

    bool done = false;
    while (!done)
    {
        unsigned int waitTime = 0;
        if (inst->state == kZapperWaitSourceReady)
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

    LOG(DLOGL_REALLY_NOISY, "pthread_exit");
    pthread_exit(NULL);
    return NULL;
}

/** *********************************************************
*/
void Zapper::displaySessionCallbackFunction(eIMediaPlayerSignal sig, eIMediaPlayerStatus stat)
{
    UNUSED_PARAM(stat);

    switch (sig)
    {
    case kMediaPlayerSignal_ServiceAuthorized:
        queueEvent(kZapperEventServiceAuthorized);
        break;

    case kMediaPlayerSignal_ServiceDeauthorized:
        queueEvent(kZapperEventServiceDeAuthorized);
        break;

    default:
        LOG(DLOGL_ERROR, "Unhandled event from display session about authorization status %d", sig);
        break;
    }
}

/** *********************************************************
*/
bool Zapper::handleEvent(Event *evt)
{
    FNLOG(DL_MSP_ZAPPER);

    if (!evt)
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "warning null event in state: %d", state);
        return false;
    }

    unsigned int event = evt->eventType;


    LOG(DLOGL_REALLY_NOISY, "eventType: %d  state: %d", event, state);


    eMspStatus status;

    switch (evt->eventType)
    {
        // exit
    case kZapperEventExit:
        dlog(DL_MSP_ZAPPER, DLOGL_NOISE, "Exit Sesion");
        return true;
        // no break required here

    case kZapperEventStop:
        break;

    case kZapperAppDataReady:
        LOG(DLOGL_NOISE, "Got APP DATA !!!!!!!!!!!!!!");

        DoCallback(kMediaPlayerSignal_ApplicationData, kMediaPlayerStatus_Ok);
        break;

    case kZapperTimeOutEvent:
        if (mSource)
        {
            if (mSource->isPPV())
            {
                LOG(DLOGL_NORMAL, "Timeout waiting for video on PPV channel.Skipping intimation of services layer");
            }
            else if (mSource->isSDV())
            {
                LOG(DLOGL_ERROR, "Warning: Time Out for SDV Channel ");
                LOG(DLOGL_MINOR_EVENT, "Callback Problem SDV Service Not Available");
                DoCallback(kMediaPlayerSignal_ServiceNotAvailableDueToSdv, kMediaPlayerStatus_Ok);
            }
            else
            {
                if (state == kZapperWaitSourceReady)
                {
                    LOG(DLOGL_EMERGENCY, "Warning - Tuner lock timeout.May be signal strength is low or no stream on tuned frequency!!");
                }
                else if (state == kZapperTunerLocked) // i.e. waiting for PSI to be ready
                {
                    LOG(DLOGL_ERROR, "Warning - PSI not available. DoCallback - ContentNotFound!!");
                }
            }
        }
        else
        {
            LOG(DLOGL_NORMAL, "NULL live source pointer on time out event");
        }
        break;

    case kZapperEventPlay:
    {
        if (mSource)
        {
            eMspStatus status;

            eResMonPriority tunerPriority;

            if (mIsVod)
            {
                tunerPriority = kRMPriority_VodVideoPlayOrRecording;
            }
            else if (mSource->isPPV())
            {
                tunerPriority = kRMPriority_PpvVideoPlayOrRecording;
            }
            else if (mSource->isQamrf())
            {
                tunerPriority = kRMPriority_VodVideoPlayOrRecording;
            }
            else
            {
                tunerPriority = kRMPriority_VideoWithAudioFocus;
            }

            status = mSource->open(tunerPriority);

            if (status == kMspStatus_Loading)
            {
                LOG(DLOGL_REALLY_NOISY, "waiting for tuning params");
                state = kZapperWaitTuningParams;
                break;
            }
            else if (status == kMspStatus_WaitForTuner)
            {
                dlog(DL_MSP_ZAPPER, DLOGL_REALLY_NOISY, "Waiting for tuning acquisition");
                state = kZapperWaitForTuner;
                break;
            }
            else if (status != kMspStatus_Ok)
            {
                if (mSource->isPPV())
                {
                    LOG(DLOGL_ERROR, "Its PPV source,avoiding sending signal to service layer");
                }
                else
                {
                    DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_InvalidParameter);
                }
            }
            else
            {
                mSource->start();
                state = kZapperWaitSourceReady;
            }
        }
    }
    break;

// get RF tuner locked callback, create display session and psi, start psi
    case kZapperTunerLockedEvent:
        if (mSource == NULL)
        {
            LOG(DLOGL_ERROR, " NULL mSource !!");
        }
        else if (state != kZapperWaitSourceReady)
        {
            LOG(DLOGL_NOISE, "Error kZapperSourceReadyEvent wrong state: %d", state);
        }
        else
        {
            //TODO: this really should go in a separate function
            state = kZapperTunerLocked;
            LOG(DLOGL_REALLY_NOISY, "Create psi");
            psi = new Psi();
            LOG(DLOGL_REALLY_NOISY, "register psi callback");
            psi->registerPsiCallback(psiCallback, this);

            LOG(DLOGL_REALLY_NOISY, "Start psi");
            // get psi started then wait for PSI ready callback
            psi->psiStart(mSource);
        }
        break;

    case kZapperEventRFAnalogCallback:
        if (mSource == NULL)
        {
            LOG(DLOGL_ERROR, " NULL mSource !!");
        }
        else if (state != kZapperWaitSourceReady)
        {
            LOG(DLOGL_NOISE, "Error kZapperSourceReadyEvent wrong state: %d", state);
        }
        else
        {
            state = kZapperTunerLocked;
            mPtrAnalogPsi = new AnalogPsi();
            status = mPtrAnalogPsi->psiStart(mSource);
            queueEvent(kZapperAnalogPSIReadyEvent);
        }
        break;

    case kZapperAnalogPSIReadyEvent:
    case kZapperPSIReadyEvent:
        if (state == kZapperTunerLocked)
        {
            LOG(DLOGL_MINOR_EVENT, "ZapperPSIReadyEvent disp_session: %p", disp_session);
            StartDisplaySession();

            if (mSource->isSDV())
            {
                mSource->setMediaSessionInstance(mIMediaPlayerSession);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Warning: not starting display, mState: %d", state);
        }
        break;

    case kZapperPSIUpdateEvent:
        LOG(DLOGL_NORMAL, "kZapperPSIUpdateEvent, state: %d ", state);
        if (state == kZapperStateRendering)
        {
            LOG(DLOGL_NORMAL, "Will restart display session");

            eIMediaPlayerStatus ret = CloseDisplaySession();
            if (ret)
            {
                LOG(DLOGL_ERROR, "CloseDisplaySession err %d", ret);
            }
            ret = StartDisplaySession();
            if (ret)
            {
                LOG(DLOGL_ERROR, "StartDisplaySession err %d", ret);
            }

        }
        else
        {
            LOG(DLOGL_SIGNIFICANT_EVENT, "Warning: kZapperPSIUpdateEvent while in state: %d", state);
        }
        break;

    case kZapperPmtRevUpdateEvent:
        LOG(DLOGL_NORMAL, "kDvrPmtRevUpdateEvent mstate %d", state);
        if (disp_session && psi)
        {
            disp_session->PmtRevUpdated(psi);
        }
        // No AV pid change; Find out if CSD (caption service descriptor changed)
        break;

    case kZapperPpvInstStart:
        dlog(DL_MSP_ZAPPER, DLOGL_NOISE, "Entering interstitial video start case");
        DoCallback(kMediaPlayerSignal_BeginningOfInterstitial, kMediaPlayerStatus_Ok);
        // Tear Down earlier session
        tearDownToRetune();

        psi = new Psi();
        disp_session = new DisplaySession;
        queueEvent(kZapperEventPlay);  //we might need to send Presentation Started signal to ANT,after this
        break;

    case kZapperPpvInstStop:
        DoCallback(kMediaPlayerSignal_EndOfInterstitial, kMediaPlayerStatus_Ok);
        dlog(DL_MSP_ZAPPER, DLOGL_NOISE, "Entering interstitial video stop case");
        tearDownToRetune();
        if (mSource != NULL)
        {
            mSource->release();
        }

        break;

    case kZapperPpvSubscnAuth:
        dlog(DL_MSP_ZAPPER, DLOGL_NOISE, "Entering ppv subscription authorized case");
        DoCallback(kMediaPlayerSignal_PpvSubscriptionAuthorized, kMediaPlayerStatus_Ok); //TODO-How to trigger video,if its stopped before.Will PPV manager will send the signal to start video?
        break;

    case kZapperPpvSubscnExprd: //BIG DOUBT 2.Incase of authorization expiration,will PPV direct us to stop video by posting StopVideo event???.Currently,I'm stopping anyway.TODO- confirm this
        dlog(DL_MSP_ZAPPER, DLOGL_NOISE, "Entering ppv subscription expired case");
        if ((state == kZapperStateRendering) || (state == kZapperWaitSourceReady) || (state == kZapperTunerLocked))
        {
            CloseDisplaySession();

            if (psi != NULL)
            {
                delete psi;
                psi = NULL;
            }

            if (mSource != NULL)
            {
                mSource->stop();    //TODO status check
                mSource->release();
            }
            state = kZapperStateStop;
        }
        DoCallback(kMediaPlayerSignal_PpvSubscriptionExpired, kMediaPlayerStatus_Ok);
        break;

    case kZapperPpvStartVideo:
        dlog(DL_MSP_ZAPPER, DLOGL_NOISE, "Entering ppv start video case");
        // TODO: refactor to call new Psi and display session from one place
        tearDownToRetune();

        psi = new Psi();
        disp_session = new DisplaySession;
        disp_session->registerDisplayMediaCallback(this, mediaCB);
        queueEvent(kZapperEventPlay);  //we might need to send Presentation Started signal to ANT,after this
        break;

    case kZapperPpvStopVideo:
        dlog(DL_MSP_ZAPPER, DLOGL_NOISE, "Entering ppv stop video case");
        tearDownToRetune();
        if (mSource != NULL)
        {
            mSource->release();
        }
        break;

    case kZapperFirstFrameEvent:
        dlog(DL_MSP_ZAPPER, DLOGL_NORMAL, "Video Presentation started successfully");
        state = kZapperStateRendering;
        isPresentationStarted = true;
        callbackToClient(state);
        DoCallback(kMediaPlayerSignal_PresentationStarted, kMediaPlayerStatus_Ok);
        gettimeofday(&tv_stop, 0);
        dlog(DL_MSP_DVR, DLOGL_NOISE, "Stop TV time, elapsed secs %ld", (tv_stop.tv_sec - tv_start.tv_sec));
        break;

    case kZapperEventTuningUpdate:       //updates from PPV or SDV about tuning information change.
    {
        //Need to stop and start the session again.. //to tune to the new frequency
        if ((state == kZapperStateRendering) || (state == kZapperWaitSourceReady) || (state == kZapperTunerLocked))
        {
            CloseDisplaySession();

            if (psi != NULL)
            {
                delete psi;
                psi = NULL;
            }

            if (mSource != NULL)
            {
                mSource->stop();    //TODO status check
            }
            state = kZapperStateStop;
            queueEvent(kZapperEventPlay);
        }
    }
    break;

    case kZapperTunerLost:
        LOG(DLOGL_NORMAL, "Tuner lost callback. state is %d", state);
        DoCallback(kMediaPlayerSignal_ResourceLost, kMediaPlayerStatus_Ok);
        tearDownToRetune();
        state = kZapperStateStop;
        break;

    case kZapperTunerRestored:
        if ((!(mSource->isMusic()) || isPresentationStarted == true))	//Music channels require PMT info to be ready before Playing event is sent, Check is here
        {
            LOG(DLOGL_NOISE, "Tuner restored event -- Sending event to MDA");
            DoCallback(kMediaPlayerSignal_ResourceRestored, kMediaPlayerStatus_Ok);
        }
        LOG(DLOGL_NOISE, "Tuner restored event -- queue play event");
        queueEvent(kZapperEventPlay);
        break;

    case kZapperEventServiceAuthorized:
        LOG(DLOGL_NOISE, "Service authorized by CAM");
        if (disp_session)
        {
            eMspStatus status = kMspStatus_Ok;
            status = disp_session->startOutput();
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "startOutput error %d on content authorization", status);
            }
        }
        DoCallback(kMediaPlayerSignal_ServiceAuthorized, kMediaPlayerStatus_Ok);
        break;

    case kZapperEventServiceDeAuthorized:
        LOG(DLOGL_ERROR, "Service DeAuthorized by CAM");
        if (disp_session)
        {
            eMspStatus status = kMspStatus_Ok;
            status = disp_session->stopOutput();
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "stopOutput error %d on content deauthorization", status);
            }
        }
        DoCallback(kMediaPlayerSignal_ServiceDeauthorized, kMediaPlayerStatus_Ok);
        break;

    case kZapperEventAudioLangChangeCb:
        if (disp_session)
        {
            disp_session->updatePids(psi);
        }
        else
        {
            LOG(DLOGL_ERROR, "NULL Display session.Ignoring audio language changed setting");
        }
        break;

    case kZapperEventSDVLoading:
        if (state != kZapperStateStop) //avoid posting previous channel SDV events if any, pending in queue.
        {
            DoCallback(kMediaPlayerSignal_ServiceLoading , kMediaPlayerStatus_Loading);
        }
        break;

    case kZapperEventSDVUnavailable:
        if (state != kZapperStateStop)
        {
            DoCallback(kMediaPlayerSignal_ServiceNotAvailableDueToSdv , kMediaPlayerStatus_ServiceNotAvailable);
        }
        break;

    case kZapperEventSDVCancelled:
        if (state != kZapperStateStop)
        {
            DoCallback(kMediaPlayerSignal_ServiceNotAvailableDueToSdv , kMediaPlayerStatus_Ok);
        }
        break;

    case kZapperEventSDVKeepAliveNeeded:
        if (state != kZapperStateStop)
        {
            DoCallback(kMediaPlayerSignal_NetworkResourceReclamationWarning , kMediaPlayerStatus_Ok);
        }
        break;

    case kZapperEventSDVLoaded:
        if (state != kZapperStateStop)
        {
            DoCallback(kMediaPlayerSignal_ServiceLoaded , kMediaPlayerStatus_Ok);
            queueEvent(kZapperEventPlay);
        }
        break;

    default:
        LOG(DLOGL_ERROR, "Not a valid callback event %d", evt->eventType);
        break;
    }

    return false;
}

void Zapper::sourceCB(void *data, eSourceState aSrcState)
{
    LOG(DLOGL_REALLY_NOISY, "data: %p  aSrcState: %d", data, aSrcState);

    if (!data)
    {
        LOG(DLOGL_MINOR_EVENT, "Warning: null data aSrcState: %d", aSrcState);
        return;
    }

    Zapper *inst = (Zapper *)data;

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

    case kSrcSDVServiceLoading:
        LOG(DLOGL_MINOR_EVENT, "kSrcSDVServiceLoading");
        inst->queueEvent(kZapperEventSDVLoading);
        break;

    case kSrcSDVServiceUnAvailable:
        LOG(DLOGL_MINOR_EVENT, "kSrcSDVServiceUnAvailable");
        inst->queueEvent(kZapperEventSDVUnavailable);
        break;

    case kSrcSDVServiceChanged:
        LOG(DLOGL_MINOR_EVENT, "kSrcSDVServiceChanged");
        inst->queueEvent(kZapperEventTuningUpdate);
        break;

    case kSrcSDVServiceCancelled:
        LOG(DLOGL_MINOR_EVENT, "kSrcSDVServiceCancelled");
        inst->queueEvent(kZapperEventSDVCancelled);
        break;

    case kSrcSDVKeepAliveNeeded:
        LOG(DLOGL_MINOR_EVENT, "kSrcSDVKeepAliverNeeded");
        inst->queueEvent(kZapperEventSDVKeepAliveNeeded);
        break;

    case kSrcPPVInterstitialStart:
        LOG(DLOGL_MINOR_EVENT, "kSrcPPVInterstitialStart");
        inst->queueEvent(kZapperPpvInstStart);
        break;

    case kSrcPPVInterstitialStop:
        LOG(DLOGL_MINOR_EVENT, "kSrcPPVInterstitialStop");
        inst->queueEvent(kZapperPpvInstStop);
        break;

    case kSrcPPVSubscriptionAuthorized:
        LOG(DLOGL_MINOR_EVENT, "kSrcPPVSubscriptionAuthorized");
        inst->queueEvent(kZapperPpvSubscnAuth);
        break;

    case kSrcPPVSubscriptionExpired:
        LOG(DLOGL_MINOR_EVENT, "kSrcPPVSubscriptionExpired");
        inst->queueEvent(kZapperPpvSubscnExprd);
        break;

    case kSrcPPVStartVideo:
        LOG(DLOGL_MINOR_EVENT, "kSrcPPVStartVideo");
        inst->queueEvent(kZapperPpvStartVideo);
        break;

    case kSrcPPVStopVideo:
        LOG(DLOGL_MINOR_EVENT, "kSrcPPVStopVideo");
        inst->queueEvent(kZapperPpvStopVideo);
        break;

    case kSrcSDVKnown:
        LOG(DLOGL_MINOR_EVENT, "kSrcSDVKnown");
        inst->queueEvent(kZapperEventSDVLoaded);
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
 *
*/
void Zapper::psiCallback(ePsiCallBackState state, void *data)
{
    Zapper *inst = (Zapper *)data;

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

    case kPmtRevUpdate:
        LOG(DLOGL_NOISE, "kPmtRevUpdate");
        inst->queueEvent(kZapperPmtRevUpdateEvent);

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
eIMediaPlayerStatus Zapper::DoCallback(eIMediaPlayerSignal sig, eIMediaPlayerStatus stat)
{
    FNLOG(DL_MSP_ZAPPER);
    tIMediaPlayerCallbackData cbData;

    cbData.status = stat;
    cbData.signalType = sig;
    cbData.data[0] = '\0';

    LOG(DLOGL_NOISE, "%s:%d SL callback. signal %d status %d", __FUNCTION__, __LINE__, sig, stat);

    std::list<CallbackInfo *>::iterator iter;

    for (iter = mCallbackList.begin(); iter != mCallbackList.end(); iter++)
    {
        if ((*iter)->mCallback != NULL)
        {
            (*iter)->mCallback((*iter)->mpSession, cbData, (*iter)->mClientContext, NULL);
        }
    }
    return  kMediaPlayerStatus_Ok;
}

/** *********************************************************
*/
eMspStatus Zapper::queueEvent(eZapperEvent evtyp)
{
    if (!threadEventQueue)
    {
        LOG(DLOGL_ERROR, "warning: no queue to dispatch event %d", evtyp);
        return kMspStatus_BadParameters;
    }

    threadEventQueue->dispatchEvent(evtyp);
    return kMspStatus_Ok;
}

/** *********************************************************
*/
void Zapper::lockMutex(void)
{
    pthread_mutex_lock(&mMutex);
}

/** *********************************************************
*/
void Zapper::unLockMutex(void)
{
    pthread_mutex_unlock(&mMutex);
}

/** *********************************************************
*/

eIMediaPlayerStatus Zapper::Load(const char* serviceUrl, const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_ZAPPER);
    LOG(DLOGL_FUNCTION_CALLS, "serviceUrl: %s", serviceUrl);

    UNUSED_PARAM(pMme)

    if ((state != kZapperStateIdle)  && (state != kZapperStateStop) && (state != kZapperWaitForTuner))
    {
        LOG(DLOGL_ERROR, "error: bad state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (!serviceUrl)
    {
        LOG(DLOGL_ERROR, "error: null serviceUrl");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }
    isPresentationStarted = false;
    gettimeofday(&tv_start, 0);

    eIMediaPlayerStatus mediaPlayerStatus = kMediaPlayerStatus_Ok;
    if (mSource == NULL)
    {
        mCurrentSource = MSPSourceFactory::getMSPSourceType(serviceUrl);
        mSource = MSPSourceFactory::getMSPSourceInstance(mCurrentSource, serviceUrl, mIMediaPlayerSession);
    }
    else
    {
        mCurrentSource = MSPSourceFactory ::getMSPSourceType(serviceUrl);
        LOG(DLOGL_NOISE, "Create new source,as source type change across channel change.");
        mSource->stop();
        delete mSource;
        mSource = NULL;
        mSource = MSPSourceFactory::getMSPSourceInstance(mCurrentSource, serviceUrl, mIMediaPlayerSession);
        if (mSource == NULL)
        {
            LOG(DLOGL_ERROR, "Unable to create a new source for the serviceURL %s", serviceUrl);
            mediaPlayerStatus = kMediaPlayerStatus_Error_InvalidURL;
        }
        else
        {
            mSource->updateTuningParams(serviceUrl);
        }
    }

    if (kMediaPlayerStatus_Ok == mediaPlayerStatus)
    {
        mediaPlayerStatus = loadSource();  // will call mSource->load
    }

    //IMP NOTE: CURRENTLY WE ARE ONLY PERFORMING ASYNCHRONOUS CALLBACK FROM HERE IN CASE OF LOADING FAILURE OF RF SOURCE.
    //MOREOVER, SIMILAR CHANGES NEED TO BE DONE IN "dvr.cpp" FOR RF SOURCE TUNING.

    if (kMediaPlayerStatus_Ok != mediaPlayerStatus)
    {
        //we want session getting cleared ONLY once, when MSP perform asynchronous callback below.
        //setting "mediaPlayerStatus" to "kMediaPlayerStatus_Ok", so that error dont get propogated synchronously
        mediaPlayerStatus = kMediaPlayerStatus_Ok;
        DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_InvalidParameter);
    }

    return mediaPlayerStatus;
}


eIMediaPlayerStatus Zapper::Load(tCpeSrcRFTune tuningParams,
                                 int programNumber,
                                 const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_ZAPPER);
    LOG(DLOGL_FUNCTION_CALLS, "tuning params supplied");

    UNUSED_PARAM(pMme)

    // TODO:  Possibly validate tuningParams (or could be done at lower level)

    if ((state != kZapperStateIdle) && (state != kZapperStateStop))
    {
        LOG(DLOGL_ERROR, "error: bad state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (mSource == NULL)
    {
        mCurrentSource = kMSPRFSource;
        mSource =  new MSPRFSource(tuningParams, programNumber);
    }
    else
    {
        LOG(DLOGL_ERROR, "Warning. source exists already.should not happened.");
        mSource->stop();
        delete mSource;
        mCurrentSource = kMSPRFSource;
        mSource =  new MSPRFSource(tuningParams, programNumber);
    }

    eIMediaPlayerStatus mediaPlayerStatus = loadSource();  // will call mSource->load

    return mediaPlayerStatus;
}



eIMediaPlayerStatus Zapper::loadSource()
{
    eIMediaPlayerStatus mediaPlayerStatus = kMediaPlayerStatus_Ok;
    FNLOG(DL_MSP_ZAPPER);


    if ((state != kZapperStateIdle) && (state != kZapperStateStop))
    {
        LOG(DLOGL_ERROR, "error: bad state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (!mSource)
    {
        LOG(DLOGL_ERROR, "error: NULL msource");
        return kMediaPlayerStatus_Error_Unknown;
    }


    eMspStatus status = mSource->load(sourceCB, this);
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "load failed");
        mediaPlayerStatus = kMediaPlayerStatus_Error_InvalidURL;
    }
    else if (eventHandlerThread == 0)
    {
        int err = createEventThread();
        if (!err)
        {
            // TODO: Create display and psi objects here ??
            state = kZapperStateStop;
            LOG(DLOGL_REALLY_NOISY, "state = kZapperStateStop");
            mediaPlayerStatus = kMediaPlayerStatus_Ok;
        }
        else
        {
            LOG(DLOGL_EMERGENCY, "Error: unable to start thread");
            mediaPlayerStatus = kMediaPlayerStatus_Error_Unknown;
        }
    }

    return mediaPlayerStatus;
}




// return 0 on success
int Zapper::createEventThread()
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, (128 * 1024));

    // By default the thread is created as joinable.
    int err  = pthread_create(&eventHandlerThread, &attr, eventthreadFunc, (void *) this);
    if (!err)
    {
        // failing to set name is not considered an major error
        int retval = pthread_setname_np(eventHandlerThread, "MSP_Zapper_EvntHandlr");
        if (retval)
        {
            LOG(DLOGL_ERROR, "pthread_setname_np error: %d", retval);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "pthread_create error %d", err);
    }

    return err;
}

void Zapper::tearDown()
{
    // This is called in context of event loop when kZapperEventExit event is handled
    // teardown in reverse order
    // TODO: determine how this order affect performance

    FNLOG(DL_MSP_ZAPPER);

    CloseDisplaySession();

    if (psi)
    {
        LOG(DLOGL_REALLY_NOISY, "psiStop");
        delete psi;
        psi = NULL;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "psi null");
    }
    if (mPtrAnalogPsi)
    {
        delete mPtrAnalogPsi;
        mPtrAnalogPsi = NULL;
    }
    if (mSource)
    {
        LOG(DLOGL_REALLY_NOISY, "mSource->stop");
        mSource->stop();
        delete mSource;
        mSource = NULL;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "mSource null");
    }

}


/** *********************************************************
*/
void Zapper::Eject()
{
    FNLOG(DL_MSP_ZAPPER);



    if (eventHandlerThread)
    {
        LOG(DLOGL_REALLY_NOISY, "exit thread");
        queueEvent(kZapperEventExit);  // tell thread to exit first. so that, it don't process any stale callbacks from core modules,that may come during the process of teardown.
        unLockMutex();
        pthread_join(eventHandlerThread, NULL);       // wait for event thread to exit
        lockMutex();
        eventHandlerThread = 0;
        tearDown();
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "eventHandlerThread null");
    }

}



/** *********************************************************
*/
eIMediaPlayerStatus Zapper::Play(const char* outputUrl,
                                 float nptStartTime,
                                 const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_ZAPPER);

    UNUSED_PARAM(pMme)

    mNptPendingSeek = nptStartTime;
    LOG(DLOGL_MINOR_EVENT, "outputUrl: %s  state: %d", outputUrl, state);

    if ((state != kZapperStateStop) && (state != kZapperWaitSourceReady))
    {
        LOG(DLOGL_ERROR, "Error state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (!outputUrl)
    {
        LOG(DLOGL_ERROR, "Error null URL");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    dest_url = outputUrl;

    if ((dest_url.find("decoder://primary") != 0) && (dest_url.find("decoder://secondary") != 0))
    {
        LOG(DLOGL_ERROR, "Error destUrl: %s", dest_url.c_str());
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    queueEvent(kZapperEventPlay);

    return  kMediaPlayerStatus_Ok;
}

/** *********************************************************
/returns always returns kMediaPlayerStatus_Error_NotSupported
*/
eIMediaPlayerStatus Zapper::PersistentRecord(const char* recordUrl, float nptRecordStartTime, float nptRecordStopTime, const MultiMediaEvent **pMme)
{
    UNUSED_PARAM(recordUrl)
    UNUSED_PARAM(nptRecordStartTime)
    UNUSED_PARAM(nptRecordStopTime)
    UNUSED_PARAM(pMme)
    FNLOG(DL_MSP_ZAPPER);

    return  kMediaPlayerStatus_Error_NotSupported;
}

/** *********************************************************
*/
eIMediaPlayerStatus Zapper::Stop(bool stopPlay, bool stopPersistentRecord)
{
    //int status;

    // this is unused by zapper
    UNUSED_PARAM(stopPlay)
    UNUSED_PARAM(stopPersistentRecord)
    FNLOG(DL_MSP_ZAPPER);

    CloseDisplaySession();

    if (psi != NULL)
    {
        delete psi;
        psi = NULL;
    }

    if (mSource && mSource->isSDV())
    {
        mSource->setMediaSessionInstance(NULL);
        deleteAllClientSession();
    }

    if (mPtrAnalogPsi)
    {
        delete mPtrAnalogPsi;
        mPtrAnalogPsi = NULL;
    }
    if (threadEventQueue)
    {
        threadEventQueue->flushQueue(); //flush out any pending events posted prior/during Stop() call.
    }
    isPresentationStarted = false;
    state = kZapperStateStop;

    return  kMediaPlayerStatus_Ok;

}

/** *********************************************************
    \returns always returns kMediaPlayerStatus_Error_NotSupported
*/
eIMediaPlayerStatus Zapper::SetSpeed(int numerator, unsigned int denominator)
{
    UNUSED_PARAM(numerator)
    UNUSED_PARAM(denominator)

    FNLOG(DL_MSP_ZAPPER);

    return  kMediaPlayerStatus_Error_NotSupported;

}

/** *********************************************************
    \returns Both *pNumerator and *pDenominator are set to 1 since set speed is not supported
*/
eIMediaPlayerStatus Zapper::GetSpeed(int* pNumerator, unsigned int* pDenominator)
{
    FNLOG(DL_MSP_ZAPPER);

    if (state != kZapperStateRendering)
    {
        dlog(DL_MSP_ZAPPER, DLOGL_ERROR, "GetSpeed during wrong state. state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if ((pNumerator == NULL) || (pDenominator == NULL))
    {
        dlog(DL_MSP_ZAPPER, DLOGL_ERROR, "NULL *numerator or *denominator in GetSpeed");
        return  kMediaPlayerStatus_Error_InvalidParameter;
    }

    *pNumerator = 1;
    *pDenominator = 1;

    return  kMediaPlayerStatus_Ok;
}

/** *********************************************************
    \returns always returns kMediaPlayerStatus_Error_NotSupported
*/
eIMediaPlayerStatus Zapper::SetPosition(float nptTime)
{
    FNLOG(DL_MSP_ZAPPER);
    UNUSED_PARAM(nptTime)

    return  kMediaPlayerStatus_Error_NotSupported;
}

/** *********************************************************
    \returns always returns kMediaPlayerStatus_Error_NotSupported
*/
eIMediaPlayerStatus Zapper::GetPosition(float* pNptTime)
{
    FNLOG(DL_MSP_ZAPPER);
    UNUSED_PARAM(pNptTime)

    return  kMediaPlayerStatus_Error_NotSupported;
}

/** *********************************************************
*/
eIMediaPlayerStatus Zapper::IpBwGauge(const char *sTryServiceUrl, unsigned int *pMaxBwProvision, unsigned int *pTryServiceBw, unsigned int *pTotalBwCoynsumption)
{
    FNLOG(DL_MSP_ZAPPER);
    UNUSED_PARAM(sTryServiceUrl)
    UNUSED_PARAM(pMaxBwProvision)
    UNUSED_PARAM(pTryServiceBw)
    UNUSED_PARAM(pTotalBwCoynsumption)

    return  kMediaPlayerStatus_Ok;
}

/** *********************************************************
*/
eIMediaPlayerStatus Zapper::SetPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus)
{
    FNLOG(DL_MSP_ZAPPER);

    if (vidScreenRect == NULL)
    {
        LOG(DLOGL_ERROR, " Error null vidScreenRect");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    LOG(DLOGL_REALLY_NOISY, "rect x: %d  y: %d  w: %d  h: %d  audio: %d",
        vidScreenRect->x, vidScreenRect->y, vidScreenRect->width, vidScreenRect->height, enableAudioFocus);

// translate rects (without really moving it anywhere)
    screenRect.x = vidScreenRect->x;
    screenRect.y = vidScreenRect->y;
    screenRect.w = vidScreenRect->width;
    screenRect.h = vidScreenRect->height;

    enaPicMode = enablePictureModeSetting;
    enaAudio = enableAudioFocus;


// TODO: check rectangle validity?

    if (disp_session != NULL)
    {
        disp_session->setVideoWindow(screenRect, enaAudio);
    }

    return  kMediaPlayerStatus_Ok;
}

/** *********************************************************
    \returns always returns kMediaPlayerStatus_Error_NotSupported
*/
eIMediaPlayerStatus Zapper::GetStartPosition(float* pNptTime)
{
    FNLOG(DL_MSP_ZAPPER);
    UNUSED_PARAM(pNptTime)


    return  kMediaPlayerStatus_Error_NotSupported;
}

/** *********************************************************
    \returns always returns kMediaPlayerStatus_Error_NotSupported
*/
eIMediaPlayerStatus Zapper::GetEndPosition(float* pNptTime)
{
    FNLOG(DL_MSP_ZAPPER);
    UNUSED_PARAM(pNptTime)

    return  kMediaPlayerStatus_Error_NotSupported;
}

/** *********************************************************
*/
eIMediaPlayerStatus Zapper::SetCallback(IMediaPlayerSession *pIMediaPlayerSession,
                                        IMediaPlayerStatusCallback cb,
                                        void* pClientContext)
{
    // FNLOG(DL_MSP_ZAPPER);

    LOG(DLOGL_REALLY_NOISY, "pIMediaPlayerSession: %p", pIMediaPlayerSession);

    // TODO:  fix the assignments in constructor

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
        assert(cbInfo);
    }

    return  kMediaPlayerStatus_Ok;
}


eIMediaPlayerStatus Zapper::DetachCallback(IMediaPlayerStatusCallback cb)
{
    std::list<CallbackInfo*>::iterator iter;
    eIMediaPlayerStatus status = kMediaPlayerStatus_Error_InvalidParameter;
    for (iter = mCallbackList.begin(); iter != mCallbackList.end(); iter++)
    {
        if ((*iter)->mCallback == cb)
        {
            mCallbackList.remove(*iter);
            delete(*iter);
            status = kMediaPlayerStatus_Ok;
            break;
        }
    }
    return status;
}

/** *********************************************************
*/
Zapper::~Zapper()
{
    FNLOG(DL_MSP_ZAPPER);

    Eject();

    std::list<CallbackInfo*>::iterator iter;
    for (iter = mCallbackList.begin(); iter != mCallbackList.end(); iter++)
    {
        delete(*iter);
        *iter = NULL;
    }
    mCallbackList.clear();

    if (threadEventQueue)
    {
        delete threadEventQueue;
        threadEventQueue = NULL;
    }
    isPresentationStarted = false;
}


/** *********************************************************
*/
Zapper::Zapper(bool isVod, IMediaPlayerSession *pIMediaPlayerSession)
{
    FNLOG(DL_MSP_ZAPPER);

    mIsVod = isVod;
    dest_url = "";
    disp_session = NULL;
    psi = NULL;
    state = kZapperStateIdle;
    mPtrAnalogPsi = NULL;

    // create event queue for scan thread
    threadEventQueue = new MSPEventQueue();

    eventHandlerThread = 0;

    screenRect.x = 0;
    screenRect.y = 0;
    screenRect.w = 1280;  //updated window size to HD resolution.TODO -this hardcoding had to be removed.
    screenRect.h = 720;
    enaPicMode = true;
    enaAudio = false;
    mSource = NULL;
    pthread_mutexattr_t mta;
    pthread_mutexattr_init(&mta);
    pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mMutex, &mta);
    mCBData = NULL;
    mCCICBFn = NULL;
    mNptPendingSeek = 0; /*Coverity 20030*/
    mCallbackFn      = NULL;
    mCbClientContext = NULL;
    mCurrentSource = kMSPInvalidSource;
    isPresentationStarted = false;

    mIMediaPlayerSession = pIMediaPlayerSession;
    mAppClientsList.clear();
}


/////////////////////////////////////////
// TODO:  hook all these up to display session now

eIMediaPlayerStatus Zapper::RegisterCCICallback(void* data, CCIcallback_t cb)
{
    if (disp_session)
    {
        LOG(DLOGL_NOISE, "Zapper::RegisterCCICallback is calledback when displaysession is not NULL");
        disp_session->SetCCICallback(data, cb);
    }
    else
    {
        LOG(DLOGL_NOISE, "Displaysession is not created,so CCI callback is not registered.when displaysession is created .callback will be registered");
    }
    mCBData = data;
    mCCICBFn = cb;
    return kMediaPlayerStatus_Ok;
}

eIMediaPlayerStatus Zapper::UnRegisterCCICallback()
{
    if (disp_session)
    {
        disp_session->UnSetCCICallback();
    }
    mCBData = NULL;
    mCCICBFn = NULL;
    return kMediaPlayerStatus_Ok;
}

std::string Zapper::GetSourceURL(bool liveSrcOnly)const
{
    UNUSED_PARAM(liveSrcOnly);
    if (mSource != NULL)
    {
        return mSource->getSourceUrl();
    }
    else
    {
        std::string srcURL;
        // This case is normal in case of tuning conflict with music channel, not an error
        // This is how Dvr::GetSourceURL() works also in case of tuning conflict to
        // avoid crashing when destroying the last media player session that caused the conflict
        LOG(DLOGL_NOISE, "Zapper::GetSourceURL NULL source module on tuning conflict with music channel");
        return srcURL;
    }
}

std::string Zapper::GetDestURL()const
{
    return dest_url;
}

bool Zapper::isRecordingPlayback()const
{
    return false;
}

bool Zapper::isLiveSourceUsed() const
{
    return true;
}

bool Zapper::isLiveRecording()const
{
    return false;
}


eIMediaPlayerStatus Zapper::SetApplicationDataPid(uint32_t aPid)
{
    eMspStatus status = kMspStatus_Ok;

    //If Displaysession is not there, di not filter application data
    if ((kZapperStateStop != state) && (disp_session))
    {
        LOG(DLOGL_NOISE, "ApplicationData Instance = %p pid = %d", disp_session->getAppDataInstance(), aPid);
        if (mSource)
        {
            status = disp_session->filterAppDataPid(aPid, appDataReadyCallbackFn, this, mSource->isMusic());
        }
        else
        {
            // if the source is not valid yet, then the source cannot be a music channel
            status = disp_session->filterAppDataPid(aPid, appDataReadyCallbackFn, this, false);
        }

        if (status == kMspStatus_Ok)
            return kMediaPlayerStatus_Ok;
        else
        {
            LOG(DLOGL_ERROR, "Error Set Application Data Pid");
            return kMediaPlayerStatus_Error_NotSupported;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "disp_session=%p, ZapperState=%d", disp_session, state);
        return kMediaPlayerStatus_Error_Unknown;
    }

}

eIMediaPlayerStatus Zapper::SetApplicationDataPidExt(IMediaPlayerClientSession *ApplnClient)
{
    FNLOG(DL_MSP_MPLAYER);

    //If ApplnClient is Null, return from here, no use of going ahead
    if ((kZapperStateStop != state) && (NULL != ApplnClient))
    {
        if (NULL == mSource)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: mSource is NULL !!!", __FUNCTION__);
            return kMediaPlayerStatus_Error_Unknown;
        }
        tCpeSrcHandle srcHandle = NULL;
        srcHandle = mSource->getCpeSrcHandle();
        if (srcHandle == NULL)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "srcHandle = NULL   getCpeSrcHandle Error");
            return kMediaPlayerStatus_Error_Unknown;
        }
        LOG(DLOGL_NOISE, "Inside SetApplicationDataPidExt Pid = %d", ApplnClient->mPid);

        eMspStatus status = ApplnClient->filterAppDataPidExt(srcHandle);
        if (status == kMspStatus_Ok)
        {
            return kMediaPlayerStatus_Ok;
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Error SetApplicationDataPidExt %s", eMspStatusString[status]);
            return kMediaPlayerStatus_Error_NotSupported;
        }
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: State = %d ApplnClient = %p", __FUNCTION__, state, ApplnClient);
        return  kMediaPlayerStatus_Error_Unknown;
    }
}

eIMediaPlayerStatus Zapper::GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset)
{
    LOG(DLOGL_NOISE, "Inside Zapper::GetComponents");
    *count = 0;
    if (infoSize == 0 || info == NULL)
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
    else if (psi)
    {
        psi->getComponents(info, infoSize, count, offset);
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
        LOG(DLOGL_ERROR, "PSI/PMT Info Not found");
        return kMediaPlayerStatus_ContentNotFound;
    }

}

eIMediaPlayerStatus Zapper::GetApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    if (disp_session && disp_session->getAppDataInstance())
    {
        LOG(DLOGL_NOISE, "ApplicationData Instance = %p", disp_session->getAppDataInstance());

        disp_session->getApplicationData(bufferSize, buffer, dataSize);
        if (dataSize == 0)
        {
            LOG(DLOGL_ERROR, "Data Not found");
            return kMediaPlayerStatus_ContentNotFound;
        }
        else
        {
            LOG(DLOGL_NOISE, "Data Returned of Buffer Size %d, Data Size %d", bufferSize, *dataSize);
            return kMediaPlayerStatus_Ok;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Display Session is not Initialized");
        return kMediaPlayerStatus_Error_OutOfState;
    }
}

uint32_t Zapper::GetSDVClentContext(IMediaPlayerClientSession *ApplnClient)
{
    FNLOG(DL_MSP_MPLAYER);
    if (NULL != ApplnClient)
        return ApplnClient->getSDVClentContext();
    else
        return 0;

}


eIMediaPlayerStatus Zapper::GetApplicationDataExt(IMediaPlayerClientSession *ApplnClient, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    FNLOG(DL_MSP_MPLAYER);
    //If ApplnClient is Null, return from here, no use of going ahead
    if (NULL == ApplnClient)
    {
        LOG(DLOGL_NOISE, "%s: ApplnClient Pointer is NULL !!!!", __FUNCTION__);
        return kMediaPlayerStatus_Error_Unknown;
    }

    ApplnClient->getApplicationDataExt(bufferSize, buffer, dataSize);
    if (dataSize == 0)
    {
        LOG(DLOGL_ERROR, "Data Not found");
        return kMediaPlayerStatus_ContentNotFound;
    }
    else
    {
        LOG(DLOGL_NOISE, "Data Returned of Buffer Size %d, Data Size %d", bufferSize, *dataSize);
        return kMediaPlayerStatus_Ok;
    }
}

eIMediaPlayerStatus Zapper::SetAudioPid(uint32_t aPid)
{
    LOG(DLOGL_NOISE, "Inside Set Audio Zapper::Pid %d", aPid);
    if (disp_session)
    {
        eMspStatus status = disp_session->updateAudioPid(psi, aPid);
        if (status == kMspStatus_Ok)
            return kMediaPlayerStatus_Ok;
    }
    else
    {
        LOG(DLOGL_ERROR, "Zapper Display Session is Not Initialized");
    }
    return kMediaPlayerStatus_Error_NotSupported;
}




void Zapper::audioLanguageChangedCB(void *cbData)
{
    Zapper *inst = (Zapper *) cbData;
    LOG(DLOGL_NOISE, "Inside Audio Language Changed CB");

    if (inst)
    {
        inst->queueEvent(kZapperEventAudioLangChangeCb);
    }
}

void Zapper::SapChangedCB(void *aCbData)
{
    FNLOG(DL_MSP_MPLAYER);
    Zapper *inst = (Zapper *) aCbData;

    if (inst && inst->disp_session)
    {
        inst->disp_session->SapChangedCb();
    }
}

void Zapper::appDataReadyCallbackFn(void *aClientContext)
{
    LOG(DLOGL_NOISE, "Inside App Data Ready");
    Zapper *inst = (Zapper *)aClientContext;
    if (inst)
    {
        inst->queueEvent(kZapperAppDataReady);
    }
}


void Zapper::registerZapperCallback(zapperCallbackFunction cb_fun, void* clientData)
{
    // intended for OnDemand Controller
    FNLOG(DL_MSP_MPLAYER);

    assert(cb_fun && clientData);

    mCallbackFn = cb_fun;
    mCbClientContext = clientData;
}

void Zapper::callbackToClient(eZapperState state)
{
    if (mCallbackFn)
    {
        mCallbackFn(state, mCbClientContext);
        LOG(DLOGL_REALLY_NOISY, "state: %d", state);
    }
    else
    {
        LOG(DLOGL_NOISE, "warning null mCallbackFn");
    }
}


eIMediaPlayerStatus Zapper::CloseDisplaySession()
{
    FNLOG(DL_MSP_MPLAYER);
    eMspStatus status = kMspStatus_Ok;
    if (disp_session)
    {
        disp_session->UnSetCCICallback();
        disp_session->clearCallback(displaySessioncallbackConnection);
        disp_session->SetSapChangedCB(NULL, NULL);
        disp_session->SetAudioLangCB(NULL, NULL);
        status = disp_session->stop();
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Error %d stopping display session", status);
        }
        status = disp_session->close(mEasAudioActive);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Error %d closing display session", status);
        }
        delete disp_session;
        disp_session = NULL;
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Disp Session is NULL");
    }
    return kMediaPlayerStatus_Ok;
}


eIMediaPlayerStatus Zapper::StartDisplaySession()
{
    //TODO: refactor to create all this from one place
    FNLOG(DL_MSP_MPLAYER);
    if (disp_session)
    {
        CloseDisplaySession();
    }

    disp_session = new DisplaySession(mIsVod);

    disp_session->registerDisplayMediaCallback(this, mediaCB);
    displaySessioncallbackConnection = disp_session->setCallback(boost::bind(&Zapper::displaySessionCallbackFunction, this, _1, _2));
    disp_session->setVideoWindow(screenRect, enaAudio);

    if (mSource == NULL)
    {
        LOG(DLOGL_ERROR, " NULL mSource !!");
        return kMediaPlayerStatus_Error_OutOfState;
    }
    if (!mPtrAnalogPsi)
    {
        if (enaAudio)
            disp_session->SetAudioLangCB(this, audioLanguageChangedCB);
        LOG(DLOGL_NOISE, "disp_session->SetCCICallback is called from startdisplaysession");
        disp_session->SetCCICallback(mCBData, mCCICBFn);
        disp_session->updatePids(psi);
        disp_session->open(mSource);
    }
    else
    {
        if (enaAudio)
            disp_session->SetSapChangedCB(this, SapChangedCB) ;
        disp_session->open(mSource, 1);
    }

    disp_session->start(mEasAudioActive);
    return kMediaPlayerStatus_Ok;
}

void Zapper::tearDownToRetune()
{
    // eMspStatus status;

    CloseDisplaySession();

    if (psi != NULL)
    {
        delete psi;
        psi = NULL;
    }

    deleteAllClientSession();
    if (mPtrAnalogPsi)
    {
        delete mPtrAnalogPsi;
        mPtrAnalogPsi = NULL;
    }

    if (mSource != NULL)
    {
        if (mSource->isSDV())
        {
            mSource->setMediaSessionInstance(NULL);
        }
        mSource->stop();    //TODO status check
    }
    isPresentationStarted = false;
    state = kZapperStateStop;
}



void Zapper::mediaCB(void *clientInst, tCpeMediaCallbackTypes type)
{
    if (!clientInst)
    {
        LOG(DLOGL_ERROR, "Error: null clientInst for type: %d", type);
        return;
    }

    Zapper *inst = (Zapper *)clientInst;

    switch (type)
    {
    case eCpeMediaCallbackType_FirstFrameAlarm:
        inst->queueEvent(kZapperFirstFrameEvent);
        break;

    default:
        LOG(DLOGL_REALLY_NOISY, "media type %d not handled", type);
        break;
    }
}


bool Zapper::isBackground(void)
{
    return false;
}


eCsciMspDiagStatus Zapper::GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo)
{
    FNLOG(DL_MSP_MRDVR);
    if (mSource)
    {
        return mSource->GetMspNetworkInfo(msgInfo);
    }
    else
    {
        return kCsciMspDiagStat_NoData;
    }

}


void Zapper::addClientSession(IMediaPlayerClientSession *pClientSession)
{
    FNLOG(DL_MSP_MPLAYER);
    mAppClientsList.push_back(pClientSession);
}

void Zapper::deleteClientSession(IMediaPlayerClientSession *pClientSession)
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

void Zapper::deleteAllClientSession()
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
Zapper::StopAudio(void)
{
    FNLOG(DL_MSP_MPLAYER);

    if (disp_session)
    {
        disp_session->StopAudio();
    }
}


void
Zapper::RestartAudio(void)
{
    FNLOG(DL_MSP_MPLAYER);

    if (disp_session)
    {
        disp_session->RestartAudio();
    }
}

tCpePgrmHandle Zapper::getCpeProgHandle()
{
    return 0;
}

void Zapper::SetCpeStreamingSessionID(uint32_t sessionId)
{
    (void) sessionId;
    return;
}

void Zapper::InjectCCI(uint8_t CCIbyte)
{
    (void)CCIbyte;

}

eIMediaPlayerStatus Zapper::StopStreaming()
{
    // By default the IMediaController base class requires an implementation
    // of StopStreaming since its an abstract class.
    return kMediaPlayerStatus_Error_NotSupported;
}

// Responsible for starting the EAS audio playback
// This API is applicable for the controllers associated with EAS audio playback session
// For the controllers associated with non EAS audio playback sessions simply returned
// Since Zapper is not the controller associated with EAS audio playback session,
// API is returning here without any action performed
void Zapper::startEasAudio(void)
{
    dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(), is not responsible for controlling EAS", __FUNCTION__);
    return;
}

// Sets the EAS audio playback active status
// True  - EAS audi playback is in progress
// False - EAS audio playback is not in progress
void Zapper::SetEasAudioActive(bool active)
{
    mEasAudioActive = active;
}

