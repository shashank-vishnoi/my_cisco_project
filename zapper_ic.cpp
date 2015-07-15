/**
   \file zapper_ic.cpp
   \class zapper

Implementation file for zapper media controller class
*/


///////////////////////////////////////////////////////////////////////////
//                    Standard Includes
///////////////////////////////////////////////////////////////////////////
#include <list>
#include <assert.h>
#include <poll.h>
#if defined(DMALLOC)
#include "dmalloc.h"
#endif

#include <glib.h>

///////////////////////////////////////////////////////////////////////////
//                    SAIL Includes
///////////////////////////////////////////////////////////////////////////
#include <sail-clm-api.h>
#include <csci-ipclient-msp-api.h>
#include <dlog.h>
#include <Cam.h>
#include <csci-base-message-api.h>

///////////////////////////////////////////////////////////////////////////
//                    Local Includes
///////////////////////////////////////////////////////////////////////////
#include "MspCommon.h"
#include "zapper_ic.h"
#include "eventQueue.h"
#include "pthread_named.h"
#include "csci-sys-ipclient-platform-api.h"
#include "csci-dlna-ipclient-gatewayDiscovery-api.h"
#include <csci-sys-api.h>
#include "csci-sdv-api.h"
#include "csci-dvr-common-api.h"
#include "MediaPlayerSseEventHandler.h"

#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"Zapper:%s:%s:%d " msg, __FILE__, __FUNCTION__, __LINE__, ##args);

///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

#define UNUSED_PARAM(a) (void)a;

static volatile bool tunerLocked = false;       // DEBUG ONLY
bool Zapper::mEasAudioActive = false;

///////////////////////////////////////////////////////////////////////////
//                      Member implementation
///////////////////////////////////////////////////////////////////////////


/**
 * @brief Callback function registered with MediaPlayerSSEHandler with the caller instance
*/
static void MediaPlayerCallbackFunc(void *pData, eIMediaPlayerSignal signal, eIMediaPlayerStatus status, char* desc)
{
    FNLOG(DL_MSP_ZAPPER);
    LOG(DLOGL_REALLY_NOISY, "Got Media player Callback for current session  Object :%p", pData);
    Zapper *inst = (Zapper *) pData;
    /*
     This callback is triggered from Mediplayer SSE Handler Interface with mutex protection. If the object being referred here is deleted,
     It would unregister for the service, so any upcoming SSE notifications will be ignored. If the MSG is already dispacthed,
     Unregister will wait until mutex is acquired[after sse events are posted in controller's Queue]
    */
    if (inst)
    {
        tMediaSsePayload *payLoadData = new tMediaSsePayload();
        payLoadData->tSignal = signal;
        payLoadData->tStatus = status;
        strlcpy(payLoadData->tDesc, desc, MAX_MEDIASTREAM_NOTIFY_DESC_LENGTH);
        LOG(DLOGL_REALLY_NOISY, "Generated the payload and dispatched the data");
        inst->dispatchWebServiceEvent(payLoadData);
    }
}

void Zapper::dispatchWebServiceEvent(tMediaSsePayload *pData)
{
    FNLOG(DL_MSP_ZAPPER);
    eMspStatus status = queueEvent(kZapperWebSvcEvent, (void*)pData);
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "Error in queuing event");
    }
}
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

#if CSCI_PAID_SESS_API_TEST

// Thread entry point  for Csci_IpClient_MediaService_IsPaidSessActive() API test
void* checkpaidservicethread(void* args)
{
    UNUSED_PARAM(args);
    bool *pIsActive = (bool*) malloc(sizeof(bool));

    while (1)
    {
        Csci_IpClient_MediaService_IsPaidSessActive(pIsActive);


        if (*pIsActive)
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "\nPlaying video is a paid channel From thread Fn\n");
        else
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "\nPlaying video is NOT a paid channel From thread Fn\n");

        poll(0, 0, 1000);
    }

    return NULL;
}

#endif

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

    switch (evt->eventType)
    {
        // exit
    case kZapperEventExit:
        dlog(DL_MSP_ZAPPER, DLOGL_NOISE, "Exit Sesion");
        return true;
        // no break required here

    case kZapperEventPlay:
    {
        if (mSource)
        {
            eMspStatus status;
            status = mSource->open(kRMPriority_VideoWithAudioFocus);

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
                    LOG(DLOGL_REALLY_NOISY, "Its PPV source,avoiding sending signal to service layer");
                }
                else
                {
                    DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_InvalidParameter);
                }
            }
            else
            {
                if (mSource->start() != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "%s: Starting source failed unfortunately\n", __FUNCTION__);
                }
                else
                {
                    LOG(DLOGL_REALLY_NOISY, "%s: Starting source", __FUNCTION__);
                    mSessionState = kSessionLoading;
                }
            }
        }
    }
    break;

    case kZapperPpvInstStart:
        dlog(DL_MSP_ZAPPER, DLOGL_NOISE, "Entering interstitial video start case");
        DoCallback(kMediaPlayerSignal_BeginningOfInterstitial, kMediaPlayerStatus_Ok);
        // Tear Down earlier session
        tearDownToRetune();

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
            StopDeletePSI();
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

    case kZapperAppDataReady:
        LOG(DLOGL_NOISE, "Got APP DATA !!!!!!!!!!!!!!");

        DoCallback(kMediaPlayerSignal_ApplicationData, kMediaPlayerStatus_Ok);
        break;

    case kZapperEventStop:
        break;

    case kZapperTimeOutEvent:
        break;

        // get RF tuner locked callback
    case kZapperTunerLockedEvent:
        break;

    case kZapperEventRFAnalogCallback:
        break;

    case kZapperPMTReadyEvent:
        dlog(DL_MSP_ZAPPER, DLOGL_REALLY_NOISY, "kZapperPMTReadyEvent received successfully");
        break;

    case kZapperAnalogPSIReadyEvent:
    case kZapperPSIReadyEvent:
        dlog(DL_MSP_ZAPPER, DLOGL_REALLY_NOISY, "kZapperPSIReadyEvent received successfully");

        if (mSource)
        {
            LOG(DLOGL_REALLY_NOISY, "Create psi");
            mPsi = new Psi();

            if (mPsi != NULL)
            {
                eMspStatus status = kMspStatus_Ok;

                LOG(DLOGL_REALLY_NOISY, "Start psi");
                status = mPsi->psiStart(mSource);
                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "Error: Start psi failed");
                    DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
                }
                else
                {
                    LOG(DLOGL_REALLY_NOISY, "Select Audio Video Components with EAS being %s", (mEasAudioActive == true) ? "TRUE" : "FALSE");
                    status = mSource->SelectAudioVideoComponents(mPsi, mEasAudioActive);
                    if (status != kMspStatus_Ok)
                    {
                        LOG(DLOGL_ERROR, "Error: selecting AUDIO and VIDEO components failed");
                        DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
                    }
                    status = mSource->formulateCCLanguageList(mPsi);
                    if (status != kMspStatus_Ok)
                    {
                        LOG(DLOGL_ERROR, "Error: selecting Language  components failed");
                    }
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "Error: Memory allocation failed");
                DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ServiceNotAvailable);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Error: Invalid source");
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ServiceNotAvailable);
        }
        break;

    case kZapperPSIUpdateEvent:
        break;

    case kZapperFirstFrameEvent:

        if (mSessionState == kSessionDeAuthorized)
        {
            if (mSource)
            {
                if (mSource->isMusic())
                {
                    LOG(DLOGL_REALLY_NOISY, "Session is Deauthorized, so not posting the first frame alram ");
                    break;
                }
            }
        }
        if (state != kZapperStateRendering)
        {
            if (mSource)
            {
                /* Playback started... */
                /* Lets register for the audio language change notifications */
                mSource->SetAudioLangCB(this, audioLanguageChangedCB);

                eMspStatus status = Avpm::getAvpmInstance()->connectOutput();
                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "connectOuput error %d", status);
                }

                dlog(DL_MSP_ZAPPER, DLOGL_REALLY_NOISY, "kZapperFirstFrameEventn calling mSource->setPresentationParams for PowerOn Playback and channel change...");
                status = mSource->setPresentationParams(&mVidScreenRect, enaPicMode, enaAudio);

                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "kZapperFirstFrameEventn - setPresentationParams failed");
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "NULL source. Ignoring audio language setting");
            }

            // Received the First Frame event.
            // Since we are going to miror the G8 TSB on IP Client, note down
            // the TSB start time as the time when we received the First Frame Alarm.
            gettimeofday(&mTsbStart, NULL);
#if RTT_TIMER_RETRY
            //Retry is valid only for live channels and PPV channels
            std::string aSrcUrl = mSource->getSourceUrl();
            if ((std::string::npos != aSrcUrl.find(RF_SOURCE_URI_PREFIX)) || (std::string::npos != aSrcUrl.find(PPV_SOURCE_URI_PREFIX)))
            {
                MediaRTT *inst = MediaRTT::getMediaRTTInstance();
                if (NULL != inst)
                {
                    LOG(DLOGL_REALLY_NOISY, "Registering with RTT for retry at 3AM");
                    inst->MediaRTTRegisterCallback(RTTRetryCallback, this);
                }
                else
                {
                    LOG(DLOGL_ERROR, "RTT instance is NULL");
                }
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "RTT not registering for channels other than Live and PPV");
            }
#endif
        }

        dlog(DL_MSP_ZAPPER, DLOGL_REALLY_NOISY, "Video Presentation started successfully");
        state = kZapperStateRendering;
        mSessionState = kSessionStarted;
        isPresentationStarted = true;
        callbackToClient(state);
        DoCallback(kMediaPlayerSignal_PresentationStarted, kMediaPlayerStatus_Ok);
        break;

    case kZapperSrcBOF:
        LOG(DLOGL_REALLY_NOISY, "Callback signal for kZapperSrcBOF event");
        DoCallback(kMediaPlayerSignal_BeginningOfStream, kMediaPlayerStatus_Ok);
        break;

    case kZapperSrcEOF:
        //Donot send EOF signal to app if stream ended because of service deauthorization or the session was revoked on a conflict
        if (mSessionState == kSessionDeAuthorized || mSessionState == kSessionTunerRevoked)
        {
            LOG(DLOGL_REALLY_NOISY, "Not Sending EOF event");
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "Callback signal for kZapperSrcEOF event");
            DoCallback(kMediaPlayerSignal_EndOfStream, kMediaPlayerStatus_Ok);
        }
        break;
    case kZapperEventTuningUpdate:       //updates from PPV or SDV about tuning information change.
        break;

    case kZapperTunerLost:
        break;

    case kZapperTunerRestored:
        break;

    case kZapperEventServiceAuthorized:
        break;

    case kZapperEventServiceDeAuthorized:
        break;

    case kZapperSrcPendingSetSpeed:
        LOG(DLOGL_REALLY_NOISY, "Pending SetSpeed callback.Speed setting can be applied now");
        SetSpeed(mCurrentNumerator, mCurrentDenominator);
        break;

    case kZapperSrcPendingSetPosition:
        LOG(DLOGL_REALLY_NOISY, "Pending SetPosition callback. Position setting can be applied now");
        SetPosition(mPosition);
        break;

    case kZapperEventAudioLangChangeCb:
        if (mSource)
        {
            mSource->UpdateAudioLanguage(mPsi);
        }
        else
        {
            LOG(DLOGL_ERROR, "NULL Display source. Ignoring audio language changed setting");
        }
        break;
    case kZapperWebSvcEvent:
    {
        LOG(DLOGL_REALLY_NOISY, "Recieved SSE notification. Handle the event");
        tMediaSsePayload *pData = (tMediaSsePayload*)evt->eventData;
        if (pData == NULL)
        {
            LOG(DLOGL_ERROR, "Null data.Ignoring the event");
            break;
        }
        if (mSource)
        {
            LOG(DLOGL_REALLY_NOISY, "Source is Valid. Proceed with handling the event");
            MediaCallbackFuncHandler(pData->tSignal, pData->tStatus, pData->tDesc);
        }
        else
        {
            LOG(DLOGL_ERROR, "NULL Source. Ignoring the Event");
        }
        delete pData;
        pData = NULL;
    }
    break;
#if RTT_TIMER_RETRY
    case kZapperEventRTTRetry:
    {
        Retry();
        break;
    }
#endif
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
        LOG(DLOGL_MINOR_EVENT, "source callback: tuner lock callback kAnalogSrcTunerLocked");
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
        LOG(DLOGL_MINOR_EVENT, "Source Callback Tuner lost");
        inst->queueEvent(kZapperTunerLost);
        break;

    case kSrcTunerRegained:
        LOG(DLOGL_MINOR_EVENT, "Source Callback Tuner restored");
        inst->queueEvent(kZapperTunerRestored);
        break;

    case kSrcFirstFrameEvent:
        LOG(DLOGL_MINOR_EVENT, "%s: handle kSrcFirstFrameEvent event", __FUNCTION__);
        inst->queueEvent(kZapperFirstFrameEvent);
        break;

    case kSrcBOF:
        LOG(DLOGL_MINOR_EVENT, " handle kSrcBOF event");
        inst->queueEvent(kZapperSrcBOF);
        break;

    case kSrcEOF:
        LOG(DLOGL_MINOR_EVENT, " handle kSrcEOF event");
        inst->queueEvent(kZapperSrcEOF);
        break;

    case kSrcPSIReadyEvent:
        LOG(DLOGL_MINOR_EVENT, "%s: handle kSrcPSIReadyEvent event", __FUNCTION__);
        inst->queueEvent(kZapperPSIReadyEvent);
        break;

    case kSrcPMTReadyEvent:
        LOG(DLOGL_MINOR_EVENT, "%s: handle kSrcPMTReadyEvent event", __FUNCTION__);
        inst->queueEvent(kZapperPMTReadyEvent);
        break;

    case kSrcPendingSetSpeed:
        LOG(DLOGL_MINOR_EVENT, " handle kSrcPendingSetSpeed event");
        inst->queueEvent(kZapperSrcPendingSetSpeed);
        break;

    case kSrcPendingSetPosition:
        LOG(DLOGL_MINOR_EVENT, " handle kSrcPendingSetPosition event");
        inst->queueEvent(kZapperSrcPendingSetPosition);
        break;

    default:
        LOG(DLOGL_ERROR, "Warning: unknown event: %d", aSrcState);
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
eMspStatus Zapper::queueEvent(eZapperEvent evtyp, void* pData)
{
    if (!threadEventQueue)
    {
        LOG(DLOGL_ERROR, "warning: no queue to dispatch event %d", evtyp);
        return kMspStatus_BadParameters;
    }

    threadEventQueue->dispatchEvent(evtyp, pData);
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

    /*
     * Just set whether the URL is for Live channel or Vod channel
     */
    char * pIsVodUrl = strstr(serviceUrl, VOD_SOURCE_URI_PREFIX);
    if (pIsVodUrl != 0)
    {
        LOG(DLOGL_REALLY_NOISY, "Received Load for Vod Playback");
        mIsVodPlay = true;

        /*
         * Find end position here itself and keep it in member variable for later reference.
         * This is done to avoid sending extra message to G8 to just the end position
         */
        const char * pIsEndPos = strstr(serviceUrl, "EndPos=");
        if (pIsEndPos)
        {
            char *pNextToken = strstr(pIsEndPos, "&");
            if (pNextToken)
            {
                char strEndPos[20] = {0};

                /*
                 * URL will contain something like "EndPos=value". "EndPos=" will be fixed string, so we need to move
                 * forward to get the actual value
                 */
                pIsEndPos += strlen("EndPos=");
                strncpy(strEndPos, pIsEndPos, strlen(pIsEndPos) - strlen(pNextToken));
                mEndPosition = atof(strEndPos);
                LOG(DLOGL_REALLY_NOISY, "Setting End position to: %f", mEndPosition);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "LSCP URL does not contain EndPos= string..URL is incorrect");
        }

    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "Received Load for Live Playback");
        mIsVodPlay = false;
    }

    isPresentationStarted = false;

    eIMediaPlayerStatus mediaPlayerStatus = kMediaPlayerStatus_Ok;
    if (mSource == NULL)
    {
        mCurrentSource = MSPSourceFactory::getMSPSourceType(serviceUrl);
        mSource = MSPSourceFactory::getMSPSourceInstance(mCurrentSource, serviceUrl, mIMediaPlayerSession);
        LOG(DLOGL_REALLY_NOISY, "Created a fresh mSource");
    }
    else
    {
        mCurrentSource = MSPSourceFactory ::getMSPSourceType(serviceUrl);

        LOG(DLOGL_NOISE, "Create new source,as source type change across channel change.");
        UnRegisterMediaPlayerCallbackFunc((mSource->getSourceUrl()).c_str());
        mSource->stop();
        if (threadEventQueue)
        {
            threadEventQueue->flushQueue(); //flush out any pending events in zapper queue posted prior/during Stop() of the source.
        }
        delete mSource;
        mSource = NULL;

        mSource = MSPSourceFactory::getMSPSourceInstance(mCurrentSource, serviceUrl, mIMediaPlayerSession);
        if (mSource == NULL)
        {
            LOG(DLOGL_ERROR, "Unable to create a new source for the serviceURL %s", serviceUrl);
            mediaPlayerStatus = kMediaPlayerStatus_Error_InvalidURL;
        }

    }

    LOG(DLOGL_REALLY_NOISY, "mediaPlayerStatus:%d", mediaPlayerStatus);
    if (kMediaPlayerStatus_Ok == mediaPlayerStatus)
    {
        LOG(DLOGL_REALLY_NOISY, "Calling loadSource");
        //Moved here as registration/unregistration must happen only once,before source load and unregistered before source stop
        RegisterMediaPlayerCallbackFunc((void*)this, serviceUrl, (MediaCallback_t)MediaPlayerCallbackFunc);
        mediaPlayerStatus = loadSource();  // will call mSource->load
    }

    //IMP NOTE: CURRENTLY WE ARE ONLY PERFORMING ASYNCHRONOUS CALLBACK FROM HERE IN CASE OF LOADING FAILURE OF RF SOURCE.
    //MOREOVER, SIMILAR CHANGES NEED TO BE DONE IN "dvr.cpp" FOR RF SOURCE TUNING.

    if (kMediaPlayerStatus_Ok != mediaPlayerStatus)
    {
        //we want session getting cleared ONLY once, when MSP perform asynchronous callback below.
        //setting "mediaPlayerStatus" to "kMediaPlayerStatus_Ok", so that error dont get propogated synchronously
        UnRegisterMediaPlayerCallbackFunc(serviceUrl);
        mediaPlayerStatus = kMediaPlayerStatus_Ok;
        DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_InvalidParameter);
    }

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
#if CSCI_PAID_SESS_API_TEST
    pthread_t thread1;
#endif

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
#if CSCI_PAID_SESS_API_TEST

// Creating thread for testing Csci_IpClient_MediaService_IsPaidSessActive() API
    int testerr  = pthread_create(&thread1, NULL, &checkpaidservicethread, NULL);
    if (!testerr)
    {
        LOG(DLOGL_REALLY_NOISY, "checkservicepaidthread created successfully ... \n");
    }
    else
    {
        LOG(DLOGL_ERROR, "pthread_create error %d", testerr);
    }

#endif
    return err;
}

void Zapper::tearDown()
{
    // This is called in context of event loop when kZapperEventExit event is handled
    // teardown in reverse order
    // TODO: determine how this order affect performance

    FNLOG(DL_MSP_ZAPPER);

    CloseDisplaySession();

    StopDeletePSI();
    if (mSource)
    {
        LOG(DLOGL_REALLY_NOISY, "mSource->stop");
        UnRegisterMediaPlayerCallbackFunc((mSource->getSourceUrl()).c_str());
        mSource->stop();
        mSource->SetAudioLangCB(NULL, NULL);
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

eIMediaPlayerStatus Zapper::setVodClientInfo(tVodClientInfo clientInfo)
{
    FNLOG(DL_MSP_ZAPPER);
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    if ((clientInfo.clientMacId == NULL) || (clientInfo.contentUrl == NULL) || (clientInfo.sourceUrl == NULL))
    {
        status = kMediaPlayerStatus_Error_Unknown;
        LOG(DLOGL_ERROR, "Invalid information Received. clientInfo.clientMacId(%p), clientInfo.contentUrl(%p), clientInfo.sourceUrl(%p)",
            clientInfo.clientMacId, clientInfo.contentUrl, clientInfo.sourceUrl);
    }
    else
    {
        mVodClientInfo.clientMacId = strdup(clientInfo.clientMacId);
        mVodClientInfo.contentUrl = strdup(clientInfo.contentUrl);
        mVodClientInfo.sourceUrl = strdup(clientInfo.sourceUrl);

        if ((mVodClientInfo.clientMacId != NULL) && (mVodClientInfo.contentUrl != NULL) && (mVodClientInfo.sourceUrl != NULL))
        {
            mSource->setFileName(mVodClientInfo.contentUrl);
            LOG(DLOGL_REALLY_NOISY, "Valid information Received. mVodClientInfo.clientMacId(%s), mVodClientInfo.contentUrl(%s), mVodClientInfo.sourceUrl(%s)",
                mVodClientInfo.clientMacId, mVodClientInfo.contentUrl, mVodClientInfo.sourceUrl);

        }
        else
        {
            if (mVodClientInfo.clientMacId)
            {
                free(mVodClientInfo.clientMacId);
                mVodClientInfo.clientMacId = NULL;
            }

            if (mVodClientInfo.contentUrl)
            {
                free(mVodClientInfo.contentUrl);
                mVodClientInfo.contentUrl = NULL;
            }

            if (mVodClientInfo.sourceUrl)
            {
                free(mVodClientInfo.sourceUrl);
                mVodClientInfo.sourceUrl = NULL;
            }

            status = kMediaPlayerStatus_Error_OutOfMemory;
            LOG(DLOGL_ERROR, "strdup() can not allocate memory for..mVodClientInfo members");
        }
    }

    return status;
}


/** *********************************************************
*/
eIMediaPlayerStatus Zapper::Play(const char* outputUrl,
                                 float nptStartTime,
                                 const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_ZAPPER);

    UNUSED_PARAM(pMme)
    eIMediaPlayerStatus retStatus = kMediaPlayerStatus_Ok;

    mNptPendingSeek = nptStartTime;

    LOG(DLOGL_MINOR_EVENT, "outputUrl: %s  state: %d startTime %f", outputUrl, state, nptStartTime);

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


    /*
     * Check whether the streaming request is for a live channel or for VOD channel.
     */
    if (mIsVodPlay == false)
    {
        LOG(DLOGL_REALLY_NOISY, "queing kZapperEventPlay event");
        queueEvent(kZapperEventPlay);
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "VOD Playback Case..Going to start Session Setup");

        /*
         *  In case of Vod playback:
         * 1. Request Web services framework to update session information.
         * 2. Get CDS content url from ClientAdaptor
         */

        std::string srcurl = HN_ONDEMAND_STREAMING_URL;
        LOG(DLOGL_REALLY_NOISY, "srcurl initialized with %s", srcurl.c_str());
        srcurl.append(GetSourceURL());
        LOG(DLOGL_REALLY_NOISY, "srcurl sent to session setup is %s", srcurl.c_str());
        eCsciWebSvcsStatus sessionResult = Csci_Websvcs_Ipclient_Streaming_SessionSetup(srcurl.c_str(), nptStartTime, mVodSessionId);
        if (kCsciWebSvcsStatus_Ok != sessionResult)
        {
            retStatus = kMediaPlayerStatus_Error_Unknown;
            LOG(DLOGL_ERROR, "Error from Csci_Websvcs_Streaming_SessionSetup: %d", sessionResult);
        }
        else
        {
            tVodClientInfo clientInfo = {NULL, NULL, NULL};
            eCsciMrdvrSchedStatus ret =  Csci_Mrdvr_Client_GetVodClientInfo(clientInfo);
            if (kCsciMrdvrSchedStatus_OK == ret)
            {
                LOG(DLOGL_REALLY_NOISY, " Csci_Mrdvr_Client_GetVodClientInfo return : %d", ret);
                eIMediaPlayerStatus status = this->setVodClientInfo(clientInfo);
                if (kMediaPlayerStatus_Ok == status)
                {
                    LOG(DLOGL_REALLY_NOISY, "Queueing kZapperEventPlay event: sourceURL:%s, contentURL:%s, clientMacId:%s",
                        clientInfo.sourceUrl, clientInfo.contentUrl, clientInfo.clientMacId);

                    //everything went fine, just queue play request now
                    queueEvent(kZapperEventPlay);
                }
                else
                {
                    LOG(DLOGL_ERROR, "Failed to set Vod info received after session set-up");
                    DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ClientError);
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "Failed Csci_Mrdvr_Client_GetVodClientInfo API ret:%d", ret);
                DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ClientError);
            }
        }
    }

    return  retStatus;
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
    // this is unused by zapper
    UNUSED_PARAM(stopPlay)
    UNUSED_PARAM(stopPersistentRecord)

    FNLOG(DL_MSP_ZAPPER);
    eIMediaPlayerStatus retStatus = kMediaPlayerStatus_Ok;

#if RTT_TIMER_RETRY
    MediaRTT *inst = MediaRTT::getMediaRTTInstance();
    if (NULL != inst)
    {
        LOG(DLOGL_REALLY_NOISY, "Unregister RTT timer");
        inst->MediaRTTUnRegisterCallback();
    }
    else
    {
        LOG(DLOGL_ERROR, "RTT instance is NULL");
    }
#endif


    StopDeletePSI();
    /* Since its not possible to differenciate sdv and normal live streaming, we pass the control to sdvhandler_ic.cpp.
     * If luatimer is running, its sdv channel and stop the timer in sdv handler. If not do nothing.
     */
    eSdvmStatus SdvLUAStatus;
    SdvLUAStatus = Csci_Sdvm_ReportLUA(kSdvLUAReq_StopTimer);

    if (SdvLUAStatus != kSdvm_Ok)
    {
        LOG(DLOGL_ERROR, "SDV lua stop timer opearation failed. Probably sdvInit would have failed");
    }

    //If the request is to stop a vod streaming then first teardown the session with webservices
    if (mIsVodPlay == true)
    {
        /*
         * Initiate Session TearDown
         */
        eCsciWebSvcsStatus ret = Csci_Websvcs_Ipclient_Streaming_SessionTearDown(mVodSessionId);
        if (kCsciWebSvcsStatus_Ok != ret)
        {
            retStatus = kMediaPlayerStatus_Error_Unknown;
            LOG(DLOGL_ERROR, "Session teardown with sessionId: %s is UN-successful", mVodSessionId);
        }


    }

    if (mSource && mSource->isSDV())
    {
        mSource->setMediaSessionInstance(NULL);
    }

    if (threadEventQueue)
    {
        threadEventQueue->flushQueue(); //flush out any pending events posted prior/during Stop() call.
    }

    isPresentationStarted = false;
    state = kZapperStateStop;
    mSessionState = kSessionIdle;
    return  retStatus;
}

/** *********************************************************
    \returns kMediaPlayerStatus_Error_NotSupported for invalid values
			 kMediaPlayerStatus_Ok for Success
*/
eIMediaPlayerStatus Zapper::SetSpeed(int numerator, unsigned int denominator)
{
    eIMediaPlayerStatus retStatus = kMediaPlayerStatus_Error_NotSupported;
    FNLOG(DL_MSP_ZAPPER);
    eMspStatus status = kMspStatus_Ok;
    /*
     * check whether the request is for VOD  or live.
     * If it is for live, use the source to set speed with the given parameters
     * If VOD, use the CSCI API to set the speed
     */
    if (state != kZapperStateRendering)
    {
        LOG(DLOGL_REALLY_NOISY, "SetSpeed during wrong state. state: %d", state);
        retStatus = kMediaPlayerStatus_Error_OutOfState;
    }
    else if (denominator == 0)
    {
        LOG(DLOGL_ERROR, "Error invalid params denom:");
        retStatus = kMediaPlayerStatus_Error_InvalidParameter;
    }
    else
    {
        mCurrentNumerator = numerator;
        mCurrentDenominator = denominator;

        if (mIsVodPlay ==  true)
        {
            /*
            * Call CSCI API's to send setSpeed request to G8 Server. Before calling API make sure that a valid session exist
            */
            if (strlen(mVodSessionId) > 0)
            {
                eCsciWebSvcsStatus status = Csci_Websvcs_Ipclient_TrickPlay_SetSpeed(mVodSessionId, numerator, denominator);
                if (kCsciWebSvcsStatus_Ok != status)
                {
                    retStatus = kMediaPlayerStatus_Error_Unknown;
                    LOG(DLOGL_ERROR, "Received error from Csci_Websvcs_TrickPlay_SetSpeed. mVodSessionId(%s), numerator(%d), denominator(%d)",
                        mVodSessionId, numerator, denominator);
                }
                else
                {
                    LOG(DLOGL_REALLY_NOISY, "Received SUCCESS from Csci_Websvcs_TrickPlay_SetSpeed. mVodSessionId(%s), numerator(%d), denominator(%d)",
                        mVodSessionId, numerator, denominator);
                    retStatus = kMediaPlayerStatus_Ok;
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "Error: Null sessionId");
            }
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, " Inside Live channel Set Speed ant_num = %d  ant_den = %d", numerator, denominator);
            if (mSource)
            {
                status = mSource->setSpeed(numerator, denominator, 0);
                if (status == kMspStatus_Loading)
                {
                    LOG(DLOGL_REALLY_NOISY, "HTTP source is not ready for speed settings. Delaying it");
                    retStatus = kMediaPlayerStatus_Ok;
                }
                else if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "Set speed Failed, Error = %d", status);
                    retStatus = kMediaPlayerStatus_Error_Unknown;
                }
                else
                {
                    LOG(DLOGL_REALLY_NOISY, " CGMI SET speed success ");
                    retStatus = kMediaPlayerStatus_Ok;
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "Null source");
                retStatus = kMediaPlayerStatus_Error_Unknown;
            }
        }
    }
    return retStatus;
}

/** *********************************************************
 *  \returns If VOD streaming then get Speed from G8
			 If Live Streaming, the get the last speed values set by SET speed API
*/
eIMediaPlayerStatus Zapper::GetSpeed(int* pNumerator, unsigned int* pDenominator)
{
    FNLOG(DL_MSP_ZAPPER);
    eIMediaPlayerStatus retStatus = kMediaPlayerStatus_Error_NotSupported;

    /*
     * GetSpeed is supported for Zapper controller also, so it is preferrable here to first do
     * normal error checking and after that check whether the request is for VOD  or live.
     */
    if (state != kZapperStateRendering)
    {
        LOG(DLOGL_REALLY_NOISY, "GetSpeed during wrong state. state: %d", state);
        retStatus =  kMediaPlayerStatus_Error_OutOfState;

    }
    else if ((pNumerator == NULL) || (pDenominator == NULL))
    {
        LOG(DLOGL_ERROR, "Received invalid memory for numerator or denominator. pNumerator(%p), pDenominator(%p)", pNumerator, pDenominator);
        retStatus = kMediaPlayerStatus_Error_InvalidParameter;
    }
    else
    {
        if (mIsVodPlay == true)
        {
            LOG(DLOGL_REALLY_NOISY, "Received  GetSpeed for VOD Playback");

            /*
             * Call CSCI to getSpeed from VOD Server through G8
             */
            eCsciWebSvcsStatus status = Csci_Websvcs_Ipclient_TrickPlay_GetSpeed(mVodSessionId, pNumerator, pDenominator);
            if (kCsciWebSvcsStatus_Ok != status)
            {
                retStatus = kMediaPlayerStatus_Error_Unknown;
                LOG(DLOGL_ERROR, "Received error from Csci_Websvcs_TrickPlay_GetSpeed. mVodSessionId(%s)", mVodSessionId);
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "Received SUCCESS from Csci_Websvcs_TrickPlay_GetSpeed. mVodSessionId(%s), pNumerator(%d), pDenominator(%d)",
                    mVodSessionId, *pNumerator, *pDenominator);
                retStatus = kMediaPlayerStatus_Ok;
            }
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, " Received GetSpeed for live playback. Setting numerator = %d,denominator = %d", mCurrentNumerator, mCurrentDenominator);
            *pNumerator = mCurrentNumerator;
            *pDenominator = mCurrentDenominator;
            retStatus = kMediaPlayerStatus_Ok;
        }
    }
    return  retStatus;
}

/** *********************************************************
    \returns If live streaming then set Position with the nptTime
    else for VOD, fetch position from G8
*/
eIMediaPlayerStatus Zapper::SetPosition(float nptTime)
{

    eIMediaPlayerStatus retStatus = kMediaPlayerStatus_Error_NotSupported;
    FNLOG(DL_MSP_ZAPPER);
    if (state != kZapperStateRendering)
    {
        LOG(DLOGL_REALLY_NOISY, "SetPosition during wrong state. state: %d", state);
        retStatus =  kMediaPlayerStatus_Error_OutOfState;
    }
    else
    {
        /*
         * check whether the request is for VOD or live,
         * if it is for live, use the source to set the position with the nptTime
         */
        if (mIsVodPlay ==  true)
        {
            /*
             * Call CSCI API's to send SetPosition request to G8 Server. Before calling API make sure that a valid session exist
             */
            if (strlen(mVodSessionId) > 0)
            {
                eCsciWebSvcsStatus status = Csci_Websvcs_Ipclient_TrickPlay_SetPosition(mVodSessionId, nptTime);
                if (kCsciWebSvcsStatus_Ok != status)
                {
                    retStatus = kMediaPlayerStatus_Error_Unknown;
                    LOG(DLOGL_ERROR, "Received error from Csci_Websvcs_TrickPlay_SetPosition. mVodSessionId(%s), nptTime(%f)",
                        mVodSessionId, nptTime);
                }
                else
                {
                    LOG(DLOGL_REALLY_NOISY, "Received SUCCESS from Csci_Websvcs_TrickPlay_SetPosition. mVodSessionId(%s), nptTime(%f)",
                        mVodSessionId, nptTime);
                    retStatus = kMediaPlayerStatus_Ok;
                }
            }
            else
            {

                LOG(DLOGL_ERROR, "Error: Null sessionId");
            }
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "Received Set Position request for Live content");
            LOG(DLOGL_REALLY_NOISY, "\n  SetPosition pNptTime = %f  ", nptTime);

            if (nptTime == MEDIA_PLAYER_NPT_START)
            {
                retStatus = kMediaPlayerStatus_Ok;
            }
            else if (nptTime == MEDIA_PLAYER_NPT_NOW)
            {
                retStatus = kMediaPlayerStatus_Ok;
            }
            else if (nptTime == MEDIA_PLAYER_NPT_END)
            {
                LOG(DLOGL_REALLY_NOISY, "Switching to LIVE point");

                // Get the LIVE point
                retStatus = GetEndPosition(&nptTime);
                if (retStatus == kMediaPlayerStatus_Ok)
                {
                    LOG(DLOGL_REALLY_NOISY, "\n  LIVE point switching to is = %f sec ", nptTime);

                    // Set the LIVE point
                    retStatus = SetPosition(nptTime);
                    if (retStatus == kMediaPlayerStatus_Ok)
                    {
                        LOG(DLOGL_REALLY_NOISY, "\n  LIVE point switching to is = %f sec ", nptTime);
                    }
                    else
                    {
                        LOG(DLOGL_ERROR, "Set Position failed to set LIVE position, Error = %d", retStatus);
                        retStatus = kMediaPlayerStatus_Error_Unknown;
                    }
                }
                else
                {
                    LOG(DLOGL_ERROR, "Get Position failed to get LIVE position, Error = %d", retStatus);
                    retStatus = kMediaPlayerStatus_Error_Unknown;
                }
            }
            else if (nptTime < 0)
            {
                LOG(DLOGL_ERROR, "NPT can't be negative.Invalid NPT");
                retStatus = kMediaPlayerStatus_Error_Unknown;   //NPT can't be negative.other NPT_END
            }
            else
            {
                if (mSource != NULL)
                {
                    mPosition = nptTime;
                    LOG(DLOGL_REALLY_NOISY, "set Position for live playback");
                    eMspStatus status = mSource->setPosition(nptTime);
                    if (status == kMspStatus_Loading)
                    {
                        LOG(DLOGL_REALLY_NOISY, "HTTP player is not ready for position setting. Delaying the position setting");
                        retStatus = kMediaPlayerStatus_Ok;
                    }
                    else if (status != kMspStatus_Ok)
                    {
                        LOG(DLOGL_ERROR, "Set Postion failed to set NPT requested, Error = %d", status);
                        retStatus = kMediaPlayerStatus_Error_Unknown;
                    }
                    else
                    {
                        LOG(DLOGL_REALLY_NOISY, " CGMI SET position success ");
                        retStatus = kMediaPlayerStatus_Ok;
                    }
                }
                else
                {
                    LOG(DLOGL_ERROR, " Invalid source <NULL> ");
                    retStatus = kMediaPlayerStatus_Error_Unknown;
                }
            }
        }
    }
    return  retStatus;
}

/** ***************************************************************************
    \returns returns kMediaPlayerStatus_Ok on Success
	         and fetch the position from the CGMI API using http source
             else fetches NPT position from G8
*/
eIMediaPlayerStatus Zapper::GetPosition(float* pNptTime)
{
    FNLOG(DL_MSP_ZAPPER);
    eIMediaPlayerStatus retStatus = kMediaPlayerStatus_Error_NotSupported;

    if (state != kZapperStateRendering)
    {
        LOG(DLOGL_REALLY_NOISY, "GetPosition during wrong state. state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }
    else if (pNptTime == NULL)
    {
        LOG(DLOGL_ERROR, "pNptTime is NULL");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    /*
     * check whether the request is for VOD or live.
     * if it is for live then use the source to get the position
     */
    if (mIsVodPlay ==  true)
    {
        LOG(DLOGL_REALLY_NOISY, "Received GetPosition request for VOD Play");
        /*
         * Call CSCI API's to send setSpeed request to G8 Server. Before calling API make sure that a valid session exist
         */
        if (strlen(mVodSessionId) > 0)
        {
            eCsciWebSvcsStatus status = Csci_Websvcs_Ipclient_TrickPlay_GetPosition(mVodSessionId, pNptTime);
            if (kCsciWebSvcsStatus_Ok != status)
            {
                retStatus = kMediaPlayerStatus_Error_Unknown;
                LOG(DLOGL_ERROR, "Received error from Csci_Websvcs_TrickPlay_GetPosition. mVodSessionId(%s), pNptTime(%p)",
                    mVodSessionId, pNptTime);
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "Received SUCCESS from Csci_Websvcs_TrickPlay_GetPosition. mVodSessionId(%s), NptValue(%f)",
                    mVodSessionId, *pNptTime);
                retStatus = kMediaPlayerStatus_Ok;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Error: NULL VodSessionId");
        }
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "Received GetPosition request for Live content");
        if (mSource != NULL)
        {
            eMspStatus status = mSource->getPosition(pNptTime);
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "Get Position Failed");
                retStatus = kMediaPlayerStatus_ContentNotFound;
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "returning OK - current npt: %f", *pNptTime);
                retStatus =  kMediaPlayerStatus_Ok;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Null source");
            retStatus = kMediaPlayerStatus_Error_Unknown;
        }
    }

    return retStatus;
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

    //SetPresentationParam for MSP will not be called since Rendering has yet not started. Hence save the rect and call SetPresentationParam from first frame...
    mVidScreenRect.x = vidScreenRect->x;
    mVidScreenRect.y = vidScreenRect->y;
    mVidScreenRect.width = vidScreenRect->width;
    mVidScreenRect.height = vidScreenRect->height;

    LOG(DLOGL_REALLY_NOISY, "rect x: %d  y: %d  w: %d  h: %d  audio: %d",
        vidScreenRect->x, vidScreenRect->y, vidScreenRect->width, vidScreenRect->height, enableAudioFocus);

    // translate rects (without really moving it anywhere)
    enaPicMode = enablePictureModeSetting;
    enaAudio = enableAudioFocus;

    if (!mSource)
    {
        LOG(DLOGL_ERROR, "error: NULL msource");
        return kMediaPlayerStatus_Error_Unknown;
    }

    if (state != kZapperStateRendering)
    {
        dlog(DLOGL_REALLY_NOISY, DLOGL_ERROR, "SetPresentationParams during wrong state. state: %d", state);
        return kMediaPlayerStatus_Ok;
    }

    eMspStatus status = mSource->setPresentationParams(vidScreenRect, enablePictureModeSetting, enableAudioFocus);
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "setPresentationParams failed");
    }

    return  kMediaPlayerStatus_Ok;
}

/******************************************************************************
*    returns returns kMediaPlayerStatus_Ok on Success
*	Outputs the streaming playback start position
******************************************************************************/
eIMediaPlayerStatus Zapper::GetStartPosition(float* pNptTime)
{
    FNLOG(DL_MSP_ZAPPER);

    if (state != kZapperStateRendering)
    {
        LOG(DLOGL_REALLY_NOISY, "GetStartPosition during wrong state. state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }
    else if (pNptTime == NULL)
    {
        LOG(DLOGL_ERROR, "pNptTime is NULL");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    /*
     * check whether the request is for VOD or live.
     * if it is for live then use the source to get the position
     */
    if (mIsVodPlay ==  true)
    {
        LOG(DLOGL_REALLY_NOISY, "Received GetStartPosition request for VOD Play");
        *pNptTime = 0.0;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "Received GetStartPosition request for LIVE Play");
        gettimeofday(&mTsbEnd, NULL);

        *pNptTime = 0.0;

        LOG(DLOGL_REALLY_NOISY, "End Sec = %ld Start Sec = %ld\n", mTsbEnd.tv_sec, mTsbStart.tv_sec);

        // Check for TSB wrap around
        if ((mTsbEnd.tv_sec - mTsbStart.tv_sec) >= TSB_SIZE_SECS)
        {
            // Once the TSB so far buffered is crossed maximum TSB buffer duration TSB_SIZE_SECS,
            // we need to move the TSB start position such that
            // TSB Start poition + TSB_SIZE_SECS = TSB End position
            *pNptTime = ((mTsbEnd.tv_sec - mTsbStart.tv_sec) - TSB_SIZE_SECS);
        }
    }

    LOG(DLOGL_REALLY_NOISY, "returning OK -   start npt: %f", *pNptTime);

    return kMediaPlayerStatus_Ok;
}

/******************************************************************************
    returns returns kMediaPlayerStatus_Ok on Success
	Outputs the streaming playback end position
*/
eIMediaPlayerStatus Zapper::GetEndPosition(float* pNptTime)
{
    FNLOG(DL_MSP_ZAPPER);

    if (state != kZapperStateRendering)
    {
        LOG(DLOGL_REALLY_NOISY, "GetEndPosition during wrong state. state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }
    else if (pNptTime == NULL)
    {
        LOG(DLOGL_ERROR, "pNptTime is NULL");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    /*
     * check whether the request is for VOD or live.
     * if it is for live then use the source to get the position
     */
    if (mIsVodPlay ==  true)
    {
        LOG(DLOGL_REALLY_NOISY, "Received GetEndPosition request for VOD Play");
        *pNptTime = mEndPosition;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "Received GetEndPosition request for Live content");
        // Get the current wall clock time
        // TSB end time will be the time how long TSB is accumulated after receiving
        // First frame alarm <First frame is treated as TSB start point>
        // So TSB end time = Wall clock of now - Wall clock of TSB
        gettimeofday(&mTsbEnd, NULL);
        LOG(DLOGL_REALLY_NOISY, "End Sec = %ld Start Sec = %ld\n", mTsbEnd.tv_sec, mTsbStart.tv_sec);
        *pNptTime = mTsbEnd.tv_sec - mTsbStart.tv_sec;
    }

    LOG(DLOGL_REALLY_NOISY, "returning OK -     end npt: %f", *pNptTime);

    return kMediaPlayerStatus_Ok;
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

    if (mVodClientInfo.clientMacId)
    {
        free(mVodClientInfo.clientMacId);
        mVodClientInfo.clientMacId = NULL;
    }

    if (mVodClientInfo.contentUrl)
    {
        free(mVodClientInfo.contentUrl);
        mVodClientInfo.contentUrl =  NULL;
    }

    if (mVodClientInfo.sourceUrl)
    {
        free(mVodClientInfo.sourceUrl);
        mVodClientInfo.sourceUrl =  NULL;
    }
}


/** *********************************************************
*/
Zapper::Zapper(bool isVod, IMediaPlayerSession *pIMediaPlayerSession)
{
    FNLOG(DL_MSP_ZAPPER);

    mIsVod = isVod;
    dest_url = "";
    state = kZapperStateIdle;
    mSessionState = kSessionIdle;
    // create event queue for scan thread
    threadEventQueue = new MSPEventQueue();

    eventHandlerThread = 0;

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
    mIsVodPlay = false;

    //initialize vodSessionId with default value
    //TODO: if it is not required to persist as an object member then make it local
    memset(mVodSessionId, '\0', MAX_VOD_SESSION_ID_LEN + 1);

    mVodClientInfo.clientMacId = NULL;
    mVodClientInfo.contentUrl = NULL;
    mVodClientInfo.sourceUrl = NULL;

    mCurrentNumerator = 100;
    mCurrentDenominator = 100;
    mPosition = 0;
    mEndPosition = 0.0;
    mPsi = NULL;
    memset(&mTsbStart, 0, sizeof(struct timeval));
    memset(&mTsbEnd  , 0, sizeof(struct timeval));
}


/////////////////////////////////////////


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
    LOG(DLOGL_NOISE, "Inside SetApplicationDataPid %d", aPid);

    if (mSource)
    {
        eMspStatus status = kMspStatus_Ok;

        status = mSource->filterAppDataPid(aPid, appDataReadyCallbackFn, this, mSource->isMusic());
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
        LOG(DLOGL_ERROR, "Source is NULL");
        return kMediaPlayerStatus_Error_Unknown;
    }

}

eIMediaPlayerStatus Zapper::SetApplicationDataPidExt(IMediaPlayerClientSession *ApplnClient)
{
    FNLOG(DL_MSP_MPLAYER);
    if (kZapperStateStop != state && mSource && (NULL != ApplnClient))
    {
        LOG(DLOGL_NOISE, "Inside SetApplicationDataPidExt %d", ApplnClient->mPid);

        void *pCgmiSessHandle = NULL;
        pCgmiSessHandle = mSource->getCgmiSessHandle();
        if (pCgmiSessHandle == NULL)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "pCgmiSessHandle = NULL   getCgmiSessHandle Error");
            return kMediaPlayerStatus_Error_Unknown;
        }
        LOG(DLOGL_NOISE, "Inside SetApplicationDataPidExt Pid = %d", ApplnClient->mPid);

        eMspStatus status = ApplnClient->filterAppDataPidExt(pCgmiSessHandle);
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
        LOG(DLOGL_ERROR, "%s: Invalid parameters passed in\n", __FUNCTION__);
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    if (mSource && mPsi)
    {
        eMspStatus status = mSource->GetComponents(info, infoSize, count, offset, mPsi);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "%s: PSI/PMT Info Not found (status:%d count:%d)\n", __FUNCTION__, status, *count);
            return kMediaPlayerStatus_ContentNotFound;
        }
        else
        {
            return kMediaPlayerStatus_Ok;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: PSI/PMT Info Not found - NULL Source\n", __FUNCTION__);
        return kMediaPlayerStatus_ContentNotFound;
    }
}

eIMediaPlayerStatus Zapper::GetApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    if (mSource)
    {
        eMspStatus status = kMspStatus_Ok;
        status = mSource->getApplicationData(bufferSize, buffer, dataSize);
        if ((dataSize == 0) || (status != kMspStatus_Ok))
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
        LOG(DLOGL_ERROR, "Source is not Initialized");
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

    if (mSource)
    {
        eMspStatus status = mSource->SetAudioPid(aPid);
        if (status == kMspStatus_Ok)
        {
            return kMediaPlayerStatus_Ok;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Zapper source is Not Initialized");
    }

    LOG(DLOGL_ERROR, "%s: source is unable to select the audio pid requested %d\n", __FUNCTION__, aPid);
    return kMediaPlayerStatus_ContentNotFound;
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

    if (inst)
    {

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

    return kMediaPlayerStatus_Ok;
}


eIMediaPlayerStatus Zapper::StartDisplaySession()
{
    FNLOG(DL_MSP_MPLAYER);

    return kMediaPlayerStatus_Ok;
}

void Zapper::tearDownToRetune()
{
    StopDeletePSI();
    deleteAllClientSession();

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

eCsciMspDiagStatus Zapper::GetMspStreamingInfo(DiagMspStreamingInfo *msgInfo)
{
    FNLOG(DL_MSP_MRDVR);
    if (mSource)
    {
        return mSource->GetMspStreamingInfo(msgInfo);
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

void Zapper::MediaCallbackFuncHandler(eIMediaPlayerSignal signal, eIMediaPlayerStatus status, char* desc)
{
    FNLOG(DL_MSP_MPLAYER);
    if (strncmp(desc, "TUNER", strlen("TUNER")) == 0)
    {
        if (strncmp(desc, "TUNER_GRANTED", strlen("TUNER_GRANTED")) == 0)
        {
            LOG(DLOGL_ERROR, "Got Tuner Granted Message.. Try to reload the session. Ignore the Failure for previous session Playback attempt");
            Retry();
        }
        else
        {
            tCsciDlnaGatewayInfo *GatewayInfo = new tCsciDlnaGatewayInfo();
            if (Csci_Dlna_GetMyGatewayInfo(GatewayInfo) == kCsciDlnaGatewayStatus_Ok)
            {
                LOG(DLOGL_REALLY_NOISY,
                    " ipAddress: %d, mac: %02x:%02x:%02x:%02x:%02x:%02x, friendlyName: %s, rootDeviceName: %s, gatewayAuthorizedSubDeviceName: %s",
                    GatewayInfo->ipAddress,
                    GatewayInfo->mac[0], GatewayInfo->mac[1], GatewayInfo->mac[2], GatewayInfo->mac[3],
                    GatewayInfo->mac[4], GatewayInfo->mac[5], GatewayInfo->friendlyName, GatewayInfo->rootDeviceName,
                    GatewayInfo->gatewayAuthorizedSubDeviceName);
            }
            else
            {
                LOG(DLOGL_ERROR, "failed to get friendly name");
            }

            Sail_Message mess;
            memset((void *)&mess, 0, sizeof(Sail_Message));
            mess.message_type = SAIL_MESSAGE_USER;
            mess.data[0] = kSailMsgEvt_MediaPlayer_ConflictBarker;
            if (strcmp(desc, "TUNER_CONFLICT") == 0)
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "All Tuners are busy. G8 is Busy Resolving the conflict generated");
                mSessionState = kSessionTunerConflict;
                mess.data[1] = kMediaPlayerTunerConflictBarker_Conflict;                                                       //TUNER_CONFLICT
            }
            else if (strcmp(desc, "TUNER_DENIED") == 0)
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "Gateway has denied a tuner for this session.This Session is not valid any more");
                mSessionState = kSessionTunerDenied;
                mess.data[1] = kMediaPlayerTunerConflictBarker_Denied;                                                       //TUNER_DENIED
            }
            else if (strcmp(desc, "TUNER_REVOKED") == 0)
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "Gateway has revoked a tuner for this request.This Session is not valid any more");
                mSessionState = kSessionTunerRevoked;
                mess.data[1] = kMediaPlayerTunerConflictBarker_Revoked;                                                       //TUNER_REVOKED
            }
            else if (strcmp(desc, "TUNER_WARNING") == 0)
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "Gateway has sent a warning alert. Must manage the recordings and existing sessions, else tuner might be revoked");
                mess.data[1] = kMediaPlayerTunerConflictBarker_Warning;														//TUNER_WARNING
            }
            else
            {
                LOG(DLOGL_ERROR, "Invalid Tuner error message recieved..Message is:%s", desc);
                return ;
            }

            LOG(DLOGL_EMERGENCY, "Gateway friendly name on type cast is: %s ", (char*)GatewayInfo->friendlyName);
            mess.string_count = 2;

            mess.strings = (const char *)malloc(strlen("cisco.com") + 1 + strlen((char*)GatewayInfo->friendlyName) + 1);

            if (NULL != mess.strings)
            {
                memset((void *)mess.strings, 0, strlen("cisco.com ") + 1 + strlen((char*)GatewayInfo->friendlyName) + 1);
                memcpy((void *)mess.strings, "cisco.com ", strlen("cisco.com"));
                memcpy((void *)(mess.strings + strlen("cisco.com") + 1), (char*)GatewayInfo->friendlyName, strlen((char*)GatewayInfo->friendlyName));
            }


            dlog(DL_SAILMSG, DLOGL_REALLY_NOISY, "[%s][%d] BEFORE SEND SAIL MSG.", __FUNCTION__, __LINE__);
            Csci_BaseMessage_Send(&mess);
            //Not freeing the memory here. Acc to spec, UI must free up the memory.
        }
    }
    else if (strncmp(desc, "Media_Player_Callback", strlen("Media_Player_Callback")) == 0)
    {
        LOG(DLOGL_NOISE, "dispatching media player signal");
        switch (signal)
        {
        case kMediaPlayerSignal_ServiceDeauthorized:
        {
            LOG(DLOGL_SIGNIFICANT_EVENT, "Service Deauthorized");
            mSessionState = kSessionDeAuthorized;
            DoCallback(signal, status);
        }
        break;
        case kMediaPlayerSignal_ServiceAuthorized:
        {
            LOG(DLOGL_SIGNIFICANT_EVENT, "Service Authorized");
            mSessionState = kSessionAuthorized;
            DoCallback(signal, status);
        }
        break;
        case kMediaPlayerSignal_Problem:
        {
            if (status != kMediaPlayerStatus_ErrorRecoverable)
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "Media Player Signal Problem");
                DoCallback(signal, status);
            }
            else
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "Recoverable error.. Not sending to UI to avoid cleaning up of session");
            }
        }
        break;
        case kMediaPlayerSignal_BeginningOfInterstitial:
        case kMediaPlayerSignal_EndOfInterstitial:
        case kMediaPlayerSignal_PpvSubscriptionExpired:
        case kMediaPlayerSignal_PpvSubscriptionAuthorized:
        {
            LOG(DLOGL_SIGNIFICANT_EVENT, "Ignoring PPV signals recieved via SSE as they are handled by PPV manager");
        }
        break;
        case kMediaPlayerSignal_VodPurchaseNotification:
        {
            if (status == kMediaPlayerStatus_NotAuthorized)
            {
                Sail_Message mess;

                LOG(DLOGL_SIGNIFICANT_EVENT, "Callback signal for LSC NO PERMISSION");

                memset((void *)&mess, 0, sizeof(Sail_Message));
                mess.message_type = SAIL_MESSAGE_USER;
                mess.data[0] = kSailMsgEvt_VOD_TrickPlayInformation;	// VOD Message type
                mess.data[1] = kVODPlaybackInfo_TrickPlayNotSupported;	// Trick Play not supported
                mess.strings = NULL;

                LOG(DLOGL_REALLY_NOISY, " BEFORE SEND SAIL MSG.\n");
                Csci_BaseMessage_Send(&mess);
                LOG(DLOGL_REALLY_NOISY, " AFTER SEND SAIL MSG.\n");

                DoCallback(signal, status);
            }
            else
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "Callback signal for LSC_NO_RESOURCES");
                DoCallback(signal, status);
            }
        }
        break;
        case kMediaPlayerSignal_ResourceRestored:
            // Music and Mosaic channels require PMT info to be ready before Playing event is sent, Check is here
            // Not sending Notification to MDA when the channel is in Unauthorized state. This will cause the barker to go off from the screen.
            // Notification will be sent when the content is authorized.
            if ((((mSource->isMusic() == false) && (mSource->isMosaic() == false)) || isPresentationStarted == true) && (kSessionDeAuthorized != mSessionState))
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "Tuner restored event -- Sending event to MDA");
                DoCallback(kMediaPlayerSignal_ResourceRestored, kMediaPlayerStatus_Ok);
            }
            break;
        case kMediaPlayerSignal_ServiceNotAvailableDueToSdv:
            LOG(DLOGL_SIGNIFICANT_EVENT, "SDV: Bandwidth not available");
            DoCallback(signal, status);
            break;
        default:
        {
            LOG(DLOGL_NOISE, "Unhandled media player signal.. propogating to the above layer");
            DoCallback(signal, status);
        }
        }
    }
    else if (strncmp(desc, "RETRY", strlen("RETRY")) == 0)
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "Recieved retry message. Ignore the previous playback failure");
        //TODO: should i really retry? Make Use of state machines to check?
        Retry();
    }
}
void Zapper::Retry()
{
    FNLOG(DL_MSP_MPLAYER);
    LOG(DLOGL_REALLY_NOISY, "Going to retry for the same channel automatically..");
    if (mSource)
    {
        string Url = mSource->getSourceUrl();
        LOG(DLOGL_NOISE, "URL to load is :%s", Url.c_str());

#if RTT_TIMER_RETRY
        MediaRTT *inst = MediaRTT::getMediaRTTInstance();
        if (NULL != inst)
        {
            LOG(DLOGL_REALLY_NOISY, "Unregister RTT timer");
            inst->MediaRTTUnRegisterCallback();
        }
        else
        {
            LOG(DLOGL_ERROR, "RTT instance is NULL");
        }
#endif

        tearDown();
        //ensure that zapper is in stopped state to reload
        if (state != kZapperStateStop)
            state = kZapperStateStop;
        LOG(DLOGL_NOISE, "torn down the junk session.. going to reload for the same url ");
        Load(Url.c_str(), NULL);
        LOG(DLOGL_NOISE, "Queued zapper play event");
        queueEvent(kZapperEventPlay);
    }
    else
    {
        LOG(DLOGL_ERROR, "Null Source.. Already deleted..Cannot reload");
    }
}

#if RTT_TIMER_RETRY
void Zapper::RTTRetryCallback(void *data)
{
    if (NULL != data)
    {
        Zapper *inst = (Zapper *)data;

        //Source should not be in stopped state
        if (kZapperStateStop != inst->state)
        {
            //Source should not be doing trickmodes
            if ((0 != inst->mCurrentDenominator) && (1.0 == ((float) inst->mCurrentNumerator / (float) inst->mCurrentDenominator)))
            {
                //Made it as a ERROR level log to clrealy point out that this is a RTT initiated retry in slog.
                LOG(DLOGL_SIGNIFICANT_EVENT, "Posting channel retry at 3AM");
                inst->queueEvent(kZapperEventRTTRetry);
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "RTT retry prevented as Channel in trick mode");
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "RTT retry prevented as source in stopped state");
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Zapper instance is NULL");
    }
}
#endif

void Zapper::StopDeletePSI()
{
    FNLOG(DL_MSP_DVR);
    if (mPsi != NULL)
    {
        delete mPsi;
        mPsi = NULL;
    }
}
/*****************************************************************************************
*
*    Function:        StopAudio
*
*    Purpose:         Stop the audio of the current session when EAS audio is playing
*
*/

void Zapper::StopAudio(void)
{
    FNLOG(DL_MSP_MPLAYER);

    if (mSource)
    {
        mSource->StopAudio();
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid Source state", __FUNCTION__);
    }
}

/******************************************************************************************
*
*    Function:       RestartAudio
*
*    Puitpose:       Restart the audio of the current session once EAS audio is stopped
*
*/
void Zapper::RestartAudio(void)
{
    FNLOG(DL_MSP_MPLAYER);

    if (mSource)
    {
        mSource->RestartAudio();
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid Source state", __FUNCTION__);
    }

}
eIMediaPlayerStatus Zapper::RegisterCCICallback(void* data, CCIcallback_t cb)
{
    UNUSED_PARAM(data);
    UNUSED_PARAM(cb);
    return kMediaPlayerStatus_Ok;
}

eIMediaPlayerStatus Zapper::UnRegisterCCICallback()
{
    return kMediaPlayerStatus_Ok;
}

/******************************************************************************
 * Returns the CCI byte value
 *****************************************************************************/
uint8_t Zapper::GetCciBits(void)
{
    FNLOG(DL_MSP_MPLAYER);

    uint8_t cciData = 0;

    if (mSource)
    {
        cciData =  mSource->GetCciBits();
        LOG(DLOGL_REALLY_NOISY, "%s %s ::: %d", __FUNCTION__, "CCI BYTE = ", cciData);
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid Source", __FUNCTION__);
    }

    return cciData;
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



