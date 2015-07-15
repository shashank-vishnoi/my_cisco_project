/**
	\file audioPlayer_ic.cpp
	\class AudioPlayer

Implementation of AudioPlayer - an instance of an IMediaController object
*/

#include <sys/stat.h>
#include <assert.h>
#include <pthread.h>

#include "dlog.h"
#include "pthread_named.h"

#include "eventQueue.h"
#include "audioPlayer_ic.h"


// audio player resides in MSP.  Since it is used only in association with EAS
//   audio processing, use EAS logging macros.
#define LOG_NORMAL( format, args... ) dlog( DL_SIG_EAS, DLOGL_NORMAL, format, ##args );
#define LOG_ERROR( format, args... ) dlog( DL_SIG_EAS, DLOGL_ERROR, format, ##args );

bool AudioPlayer::mEasAudioActive = false;

/********************************************************************************
 *
 *	Function:	AudioPlayer
 *
 *	Purpose:	Initialize all members, then create an EventQueue and thread
 *				for processing audio player events.
 *
 */

AudioPlayer::AudioPlayer(IMediaPlayerSession *pIMediaPlayerSession) :
    repeatDelay(0),
    mutexStack(0),
    currentState(kState_Idle),
    timerId(0),
    mediaPlayerInstance(NULL),
    mIMediaPlayerSession(pIMediaPlayerSession)
{
    srcUrl = "";

    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);

    // get a pointer to the MediaPlayer object and error check
    mediaPlayerInstance = IMediaPlayer::getMediaPlayerInstance();
    assert(mediaPlayerInstance);

    // acquire timer resources
    Timer_init();
    mpSessionId = NULL;
    // initialize queue and mutex
    apQueue = new MSPEventQueue();
    pthread_mutex_init(&apMutex, NULL);

    // initialize thread machinery
    createThread();
}


/********************************************************************************
 *
 *	Function:	~AudioPlayer
 *
 *	Purpose:	Release all remaining audio player resources.
 *
 */

AudioPlayer::~AudioPlayer(void)
{
    LOG_NORMAL("%s(), enter", __FUNCTION__);

    std::list<CallbackInfo*>::iterator iter;
    for (iter = callbackList.begin(); iter != callbackList.end(); iter++)
    {
        delete(*iter);
        *iter = NULL;
    }
    callbackList.clear();

    delete apQueue;
    pthread_mutex_destroy(&apMutex);

    // if active, cancel timer
    cancelTimer();

    LOG_NORMAL("%s(), exit", __FUNCTION__);
}


/********************************************************************************
 *
 *	Function:	Load
 *
 *	Purpose:	Parse serviceUrl to initialize playback parameters
 *
 *
 *
 */


eIMediaPlayerStatus
AudioPlayer::Load(const char* serviceUrl, const MultiMediaEvent **pMme)
{
    char copyUrl[kMaxUrlSize + 1];
    char *ptr;
    char fileUrltmp[kMaxUrlSize];
    (void) pMme;

    memset(copyUrl, 0, kMaxUrlSize + 1);
    // save serviceUrl and make a copy
    if (serviceUrl != NULL)
    {
        srcUrl = serviceUrl;
        strncpy(copyUrl, serviceUrl, kMaxUrlSize);
    }

    // parse for the delay parameter
    ptr = strstr(copyUrl, "&delay=");
    if (ptr)
    {
        sscanf(ptr, "&delay=%d", &repeatDelay);
        *ptr = '\0';	// chop off delay portion of url in copyUrl
    }

    // parse for file name
    sscanf(copyUrl, "audio://%s", fileUrltmp);
    sprintf(fileUrl, "file://%s", fileUrltmp);
    LOG_NORMAL("enter %s(),  fileUrl: %s fileUrltmp:%s  repeatDelay: %d    current state: %d",
               __FUNCTION__, fileUrl, fileUrltmp, repeatDelay, currentState);

    return kMediaPlayerStatus_Ok;
}


/********************************************************************************
 *
 *	Function:	Play
 *
 *	Purpose:	Send an event to the audio player thread instructing it
 *				to start audio playback.
 *
 */

eIMediaPlayerStatus
AudioPlayer::Play(const char* outputUrl, float nptStartTime, const MultiMediaEvent **pMme)
{
    (void) outputUrl;
    (void) nptStartTime;
    (void) pMme;

    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);

    // request that the thread start source playback
    queueEvent(kPlaySourceEvent);

    return kMediaPlayerStatus_Ok;
}


/********************************************************************************
 *
 *	Function:	Stop
 *
 *	Purpose:	Stop audio playback and restore normal audio.
 *
 *
 */

eIMediaPlayerStatus
AudioPlayer::Stop(bool stopPlay, bool stopPersistentRecord)
{
    (void) stopPlay;
    (void) stopPersistentRecord;

    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);

    // if audio playback is active
    if (currentState == kState_PlaySource)
        completeAudioTeardown();

    // transition into the stopping state
    currentState = kState_Stopped;

    return kMediaPlayerStatus_Ok;
}



/********************************************************************************
 *
 *	Function:	Eject
 *
 */

void
AudioPlayer::Eject(void)
{
    LOG_NORMAL("enter %s()", __FUNCTION__);

    if (apThread)
    {
        LOG_NORMAL("%s(), queueing exit event", __FUNCTION__);
        queueEvent(kExitThreadEvent);           // tell thread to exit
        unLockMutex();
        pthread_join(apThread, NULL);           // wait for event thread to exit
        lockMutex();
        apThread = 0;
    }
    else
    {
        LOG_NORMAL("%s(), exit previously requested, no action taken", __FUNCTION__);
    }
}


/********************************************************************************
 *
 *	Function:	enterPlaySourceState
 *
 *	Purpose:	Start the playback of EAS audio. Steps:
 *				- stop primary audio
 *                              - use cgmi calls to Create, Load and
 *                                Play session
 */

void
AudioPlayer::enterPlaySourceState(void)
{

    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);
    // stop the primary audio source and start EAS audio playback
    stopPrimaryAudioAndStartEasAudio();

}

// Responsible for starting the EAS audio playback
// This API is applicable for the controllers associated with EAS audio playback session
// For the controllers associated with non EAS audio playback sessions simply returns
//
// Since AudioPlayer is the controller associated with EAS audio playback session,
// this API is starting the playback of the EAS audio clip
void AudioPlayer::startEasAudio(void)
{
    // assume that the play source sequence will fail
    currentState = kState_Error;

    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);

    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    stat = cgmi_CreateSession(audioCallback, (void *) this, &mpSessionId);
    if (stat == CGMI_ERROR_SUCCESS)
    {
        if (mpSessionId)
        {
            stat = cgmi_Load(mpSessionId, (char*) fileUrl);
            if (stat == CGMI_ERROR_SUCCESS)
            {
                stat = cgmi_Play(mpSessionId, 1);
                if (stat == CGMI_ERROR_SUCCESS)
                {
                    currentState = kState_PlaySource;
                    LOG_NORMAL("%s: cgmi_Play successful", __FUNCTION__);
                    doCallback(kMediaPlayerSignal_PresentationStarted, kMediaPlayerStatus_Ok);
                }
                else
                {
                    LOG_ERROR("%s: cgmi_Play failed with errCode %d \n", __FUNCTION__, stat);
                }
            }
            else
            {
                LOG_ERROR("%s: cgmi_Load failed with errCode %d \n", __FUNCTION__, stat);
            }
        }
        else
        {
            LOG_ERROR("%s: mpSessionId is NULL", __FUNCTION__);
        }
    }
    else
    {
        LOG_ERROR("%s: cgmi_CreateSession failed with errCode %d \n", __FUNCTION__, stat);
    }

    // if playback did not start correctly
    if (currentState != kState_PlaySource)
    {
        restartPrimaryAudio();
        // notify client of error
        doCallback(kMediaPlayerSignal_PresentationTerminated, kMediaPlayerStatus_Error_Unknown);
    }
}


/********************************************************************************
 *
 *	Function:	enterDelayState
 *
 *	Purpose:	Transition into the delay state.  Steps:
 *				- update state
 *				- start delay timer
 *
 */

void
AudioPlayer::enterDelayState(void)
{
    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);

    // update state and start timer
    currentState = kState_Delay;
    startTimer(repeatDelay);
}

/********************************************************************************
 *
 *	Function:	stopPrimaryAudioAndStartEasAudio
 *
 *	Purpose:	Stop the audio associated with currently active video.  Audio
 *				player state machine calls this method just before starting
 *				the EAS audio playback.
 *
 */

void
AudioPlayer::stopPrimaryAudioAndStartEasAudio(void)
{
    mediaPlayerInstance->StopInFocusAudioAndStartEasAudio(mIMediaPlayerSession);
}

/********************************************************************************
 *
 *	Function:	restartPrimaryAudio
 *
 *	Purpose:	Restart the audio associated with currently active video.  Audio
 *				player state machine calls this method after the EAS audio has
 *				completed its playback.
 *
 */

void
AudioPlayer::restartPrimaryAudio(void)
{
    mediaPlayerInstance->RestartInFocusAudio();
}


/********************************************************************************
 *
 *	Function:	completeAudioTeardown
 *
 *	Purpose:	Tearding down audio playback is an asynchronous completion
 *				sequence.  The state machine executes this routine after the
 *				reference platform has reported that audio playback is no
 *				longer active.
 *
 */

void
AudioPlayer::completeAudioTeardown(void)
{
    LOG_NORMAL("enter %s()", __FUNCTION__);

    if (mpSessionId)
    {
        cgmi_Status stat = CGMI_ERROR_SUCCESS;

        stat = cgmi_Unload(mpSessionId);
        if (stat == CGMI_ERROR_SUCCESS)
        {
            LOG_NORMAL("%s(): cgmi_Unload successful", __FUNCTION__);
        }
        else
        {
            LOG_ERROR("%s(): cgmi_Unload failed with errCode %d \n", __FUNCTION__, stat);
        }

        stat = cgmi_DestroySession(mpSessionId);
        if (stat == CGMI_ERROR_SUCCESS)
        {
            LOG_NORMAL("%s(): cgmi_DestroySession successful", __FUNCTION__);
        }
        else
        {
            LOG_ERROR("%s(): cgmi_DestroySession failed with errCode %d \n", __FUNCTION__, stat);
        }

    }
    else
    {
        LOG_ERROR("%s(): mpSessionId is NULL", __FUNCTION__);
    }
    // restart audio associated with current video
    restartPrimaryAudio();
}


/********************************************************************************
 *
 *	Function:	processTimerEvent
 *
 *	Purpose:	Process timer event.  Cancel timer.  If in delay state,
 *				transition to play source state.
 *
 */

void
AudioPlayer::processTimerEvent(void)
{
    // log entry
    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);

    // always cancel the timer
    cancelTimer();

    if (currentState == kState_Delay)
    {
        // repeat source playback
        enterPlaySourceState();
    }
    else
    {
        LOG_ERROR("%s, unexpected timer event", __FUNCTION__);
    }
}


/********************************************************************************
 *
 *	Function:	processAudioEvent
 *
 *	Purpose:	Process an audio event.
 *
 *	Parameters:	eventData - payload of an audio player event
 *
 */

void
AudioPlayer::processRestartAudioEvent()
{

    LOG_NORMAL("%s, currentState: %d ", __FUNCTION__, currentState);


    if (currentState == kState_PlaySource)
    {
        // complete audio teardown sequence
        completeAudioTeardown();
        // audio restored, start delay
        enterDelayState();
    }
    else
    {
        LOG_NORMAL("%s(): Unexpected state, %d ", __FUNCTION__, currentState);
    }
}


/********************************************************************************
 *
 *	Function:	processEvent
 *
 *	Purpose:	Process an audio player event.  In most cases, the event is
 *				delegated to a type specific handler.
 *
 *	Parameters:	event - an audio player event
 *
 *	Returns:	Boolean.  If false, tells caller( threadFunction() ) to continue
 *				processing events.  If true, tells caller to stop processing
 *				events and terminate the thread.
 *
 */

bool
AudioPlayer::processEvent(Event* event)
{
    bool finished;

    // assume that we will continue processing events
    finished = false;

    // process event
    switch (event->eventType)
    {
    case kTimerEvent:
        processTimerEvent();
        break;
    case kRestartAudioEvent:
        processRestartAudioEvent();
        break;
    case kPlaySourceEvent:
        enterPlaySourceState();
        break;
    case kExitThreadEvent:
        LOG_NORMAL("%s(), exit event received.", __FUNCTION__);
        finished = true;
        break;
    default:
        LOG_ERROR("%s(), unexpected event type: %d", __FUNCTION__, event->eventType);
        break;
    }

    return finished;
}


/********************************************************************************
 *
 *	Function:	createThread
 *
 *	Purpose:	Create the audio player thread.  Steps:
 *				- set stack size
 *				- spawn the thread
 *				- set thread name
 *
 *				The newly created thread will begin execution
 *				at threadFunction().
 */

void
AudioPlayer::createThread(void)
{
    int error;
    pthread_attr_t attr;
    const char* threadName = "MSP AudioPlayer";

    // set attributes to default and set stack size
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kStackSize);

    // create thread and error check
    error = pthread_create(&apThread, &attr, threadFunction, this);
    if (error)
    {
        LOG_ERROR("pthread create error: %d", error);
    }

    // set thread name and error check
    error = pthread_setname_np(apThread, threadName);
    if (error)
    {
        LOG_ERROR("pthread set name error: %d", error);
    }
}


/********************************************************************************
 *
 *	Function:	threadFunction
 *
 *	Purpose:	Root function of the audio player thread.  In an infinite loop,
 *				perform a blocking read on the apQueue.  Each time an event
 *				is placed on this queue, process it, delete associated event
 *				data, and repeat.
 *
 *	Parameters:	data - not used
 *
 */

void*
AudioPlayer::threadFunction(void* data)
{
    AudioPlayer* instance = (AudioPlayer*) data;
    MSPEventQueue* eventQueue = instance->apQueue;
    assert(eventQueue);

    bool finished = false;;
    while (!finished)
    {
        Event* event = eventQueue->popEventQueue();
        instance->lockMutex();
        finished = instance->processEvent(event);

        eventQueue->freeEvent(event);
        instance->unLockMutex();
    }
    LOG_NORMAL("%s(), exiting audio player thread.", __FUNCTION__);
    pthread_exit(NULL);
    return NULL;
}


/********************************************************************************
 *
 *	Function:	queueEvent
 *
 *	Purpose:	Place an event on the audio player's internal queue.  Refer to
 *				threadFunction() to see where the event is processed.
 *
 *				This routine is for events which do not carry a payload.
 *
 *	Parameters:	type - type of event
 *
 */

void
AudioPlayer::queueEvent(eAudioPlayerEvent type)
{
    assert(apQueue);
    apQueue->dispatchEvent(type, NULL);
}



/********************************************************************************
 *
 *	Function:	lockMutex
 *
 *	Purpose:	Lock the audio player mutex.
 *
 */

void
AudioPlayer::lockMutex(void)
{
    pthread_mutex_lock(&apMutex);
    mutexStack++;
    LOG_NORMAL("%s(), stack: %d", __FUNCTION__, mutexStack);
}


/********************************************************************************
 *
 *	Function:	unLockMutex
 *
 *	Purpose:	Unlock the audio player mutex.
 *
 */

void
AudioPlayer::unLockMutex(void)
{
    LOG_NORMAL("%s(), stack: %d", __FUNCTION__, mutexStack);
    pthread_mutex_unlock(&apMutex);
    mutexStack--;
}


/********************************************************************************
 *
 *	Function:	audioCallback
 *
 *	Purpose:	The audio playback logic calls this routine to communicate
 *				current status of a previous audio playback request.  Extract
 *				the status and queue the appropriate event.
 *
 *	Parameters:		pUserData- pointer to self
 *				pSession - not used
 *                              event    - event sent by platform
 *                              pData    - not used
 *
 */

void
AudioPlayer::audioCallback(void *pUserData, void *pSession, tcgmi_Event event, tcgmi_Data *pData)
{
    (void) pSession;
    (void) pData;

    // get a pointer to self and then queue event
    AudioPlayer* instance = (AudioPlayer*) pUserData;

    LOG_NORMAL("%s, instance: %p", __FUNCTION__, instance);

    if (instance != NULL)
    {
        switch (event)
        {
        case NOTIFY_STREAMING_OK:                   ///<
            LOG_NORMAL("%s: NOTIFY_STREAMING_OK - callback recieved\n", __FUNCTION__);
            break;
        case NOTIFY_START_OF_STREAM:                ///< The Current position is now at the Start of the stream
            LOG_NORMAL("%s: NOTIFY_START_OF_STREAM - callback received\n", __FUNCTION__);
            break;
        case NOTIFY_END_OF_STREAM:
            LOG_NORMAL("%s: NOTIFY_END_OF_STREAM - callback recieved\n", __FUNCTION__);
            instance->queueEvent(kRestartAudioEvent);
            break;
        case NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE:   ///<The requested URL could not be opened.
            LOG_NORMAL("%s: NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE\n", __FUNCTION__);
            break;
        default:
            LOG_NORMAL("%s: Unknown callback event %d", __FUNCTION__, event);
            break;
        }
    }
    else
    {
        LOG_ERROR("%s(): audioPlayer instance is NULL", __FUNCTION__);
    }


}


/********************************************************************************
 *
 *	Function:	timerCallback
 *
 *	Purpose:	The timer logic will call this routine when the audio player
 *				timer expires.  Simply queue a timer event.
 *
 *	Parameters:	data - pointer to an EventTimer structure.
 *				fd and what are not used
 *
 */

void
AudioPlayer::timerCallback(evutil_socket_t fd, short what, void* data)
{
    (void) fd;
    (void) what;

    EventTimer* timer = reinterpret_cast <EventTimer*>(data);
    AudioPlayer* instance = reinterpret_cast <AudioPlayer*>(timer->getUserData()) ;

    LOG_NORMAL("%s, instance: %p", __FUNCTION__, instance);

    instance->queueEvent(kTimerEvent);
}


/********************************************************************************
 *
 *	Function:	startTimer
 *
 *	Purpose:	Start the audio timer using the indicated timer period.
 *
 *	Parameters:	seconds - timer will expire in this many seconds
 *
 */

void
AudioPlayer::startTimer(int seconds)
{
    eTimer_StatusCode result;

    result = Timer_addTimerDuration(seconds, timerCallback,
                                    this, &timerId);
    LOG_NORMAL("%s, currentState: %d   result: %d    this: %p",
               __FUNCTION__, currentState, result, this);
}


/********************************************************************************
 *
 *	Function:	cancelTimer
 *
 *	Purpose:	If the audio player timer is running, cancel it.
 *
 *
 */

void
AudioPlayer::cancelTimer(void)
{
    // if a timer is active, cancel it
    if (timerId)
    {
        Timer_deleteTimer(timerId);
        timerId = 0;
    }
}


eIMediaPlayerStatus
AudioPlayer::doCallback(eIMediaPlayerSignal sig, eIMediaPlayerStatus stat)
{
    FNLOG(DL_MSP_ZAPPER);
    tIMediaPlayerCallbackData cbData;

    cbData.status = stat;
    cbData.signalType = sig;
    cbData.data[0] = '\0';

    std::list<CallbackInfo *>::iterator iter;

    for (iter = callbackList.begin(); iter != callbackList.end(); iter++)
    {
        if ((*iter)->mCallback != NULL)
        {
            (*iter)->mCallback((*iter)->mpSession, cbData, (*iter)->mClientContext, NULL);
        }
    }
    return  kMediaPlayerStatus_Ok;
}



eIMediaPlayerStatus
AudioPlayer::SetCallback(IMediaPlayerSession *pIMediaPlayerSession,
                         IMediaPlayerStatusCallback cb,
                         void* pClientContext)
{
    CallbackInfo *cbInfo = new CallbackInfo();
    if (cbInfo)
    {
        cbInfo->mpSession = pIMediaPlayerSession;
        cbInfo->mCallback = cb;
        cbInfo->mClientContext = pClientContext;
        callbackList.push_back(cbInfo);
    }
    else
    {
        LOG_ERROR("%s(), Error: Unable to alloc mem", __FUNCTION__);
        assert(cbInfo);
    }

    return  kMediaPlayerStatus_Ok;
}


/*************************************************************************
 *
 *	DetachCallback
 *
 *
 */

eIMediaPlayerStatus
AudioPlayer::DetachCallback(IMediaPlayerStatusCallback cb)
{
    std::list<CallbackInfo*>::iterator iter;
    eIMediaPlayerStatus status = kMediaPlayerStatus_Error_InvalidParameter;
    for (iter = callbackList.begin(); iter != callbackList.end(); iter++)
    {
        if ((*iter)->mCallback == cb)
        {
            callbackList.remove(*iter);
            delete(*iter);
            status = kMediaPlayerStatus_Ok;
            break;
        }
    }
    return status;
}

/***********************************************************************

	An AudioPlayer object is a IMediaController which plays back
	PCM audio originating from a file.  Many of the methods specified
	by the base class don't apply to this type of audio playback.
	Below are 'dummy' implementations of these methods.

***********************************************************************/

eIMediaPlayerStatus
AudioPlayer::PersistentRecord(const char* recordUrl, float nptRecordStartTime, float nptRecordStopTime, const MultiMediaEvent **pMme)
{
    (void) recordUrl;
    (void) nptRecordStartTime;
    (void) nptRecordStopTime;
    (void) pMme;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::SetSpeed(int numerator, unsigned int denominator)
{
    (void) numerator;
    (void) denominator;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::GetSpeed(int* pNumerator, unsigned int* pDenominator)
{
    (void) pNumerator;
    (void) pDenominator;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::SetPosition(float nptTime)
{
    (void) nptTime;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::GetPosition(float* pNptTime)
{
    (void) pNptTime;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::IpBwGauge(const char *sTryServiceUrl, unsigned int *pMaxBwProvision, unsigned int *pTryServiceBw, unsigned int *pTotalBwConsumption)
{
    (void) sTryServiceUrl;
    (void) pMaxBwProvision;
    (void) pTryServiceBw;
    (void) pTotalBwConsumption;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::SetPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus)
{
    (void) vidScreenRect;
    (void) enablePictureModeSetting;
    (void) enableAudioFocus;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::GetStartPosition(float* pNptTime)
{
    (void) pNptTime;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::GetEndPosition(float* pNptTime)
{
    (void) pNptTime;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::RegisterCCICallback(void* data, CCIcallback_t cb)
{
    (void) data;
    (void) cb;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::UnRegisterCCICallback(void)
{
    return kMediaPlayerStatus_Error_NotSupported;
}

std::string
AudioPlayer::GetSourceURL(bool liveSrcOnly) const
{
    (void) liveSrcOnly;
    return srcUrl;
}

std::string
AudioPlayer::GetDestURL(void) const
{
    return NULL;
}

bool
AudioPlayer::isRecordingPlayback(void) const
{
    return false;
}

bool
AudioPlayer::isLiveRecording(void) const
{
    return false;
}

eIMediaPlayerStatus
AudioPlayer::SetApplicationDataPid(uint32_t aPid)
{
    (void) aPid;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::SetApplicationDataPidExt(IMediaPlayerClientSession *ApplnClient)
{
    (void) ApplnClient;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset)
{
    (void) info;
    (void) infoSize;
    (void) count;
    (void) offset;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::GetApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    (void) bufferSize;
    (void) buffer;
    (void) dataSize;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::GetApplicationDataExt(IMediaPlayerClientSession *ApplnClient, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    (void) ApplnClient;
    (void) bufferSize;
    (void) buffer;
    (void) dataSize;
    return kMediaPlayerStatus_Error_NotSupported;
}

uint32_t AudioPlayer::GetSDVClentContext(IMediaPlayerClientSession *ApplnClient)
{
    (void) ApplnClient;
    return kMediaPlayerStatus_Error_NotSupported;
}
eIMediaPlayerStatus
AudioPlayer::SetAudioPid(uint32_t pid)
{
    (void) pid;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::CloseDisplaySession(void)
{
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::StartDisplaySession(void)
{
    return kMediaPlayerStatus_Error_NotSupported;
}

bool
AudioPlayer::isBackground(void)
{
    return false;
}

eCsciMspDiagStatus
AudioPlayer::GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo)
{
    (void) msgInfo;
    return kCsciMspDiagStat_NoData;
}

void
AudioPlayer::StopAudio(void)
{
    // the IMediaController base class requires an implementation
    //   of StopAudio.  Unlike the other instances of IMediaController,
    //   AudioPlayer does NOT have a DisplaySession member.  Because
    //   of this, AudioPlayer's implementation does nothing.
}


void
AudioPlayer::RestartAudio(void)
{
    // the IMediaController base class requires an implementation
    //   of RestartAudio.  Unlike the other instances of IMediaController,
    //   AudioPlayer does NOT have a DisplaySession member.  Because
    //   of this, AudioPlayer's implementation does nothing.
}



void
AudioPlayer::addClientSession(IMediaPlayerClientSession *pClientSession)
{
    (void) pClientSession;
    // the IMediaController base class requires an implementation
    //   of RestartAudio.  Unlike the other instances of IMediaController,
    //   AudioPlayer does NOT have a DisplaySession member.  Because
    //   of this, AudioPlayer's implementation does nothing.
}

void
AudioPlayer::deleteClientSession(IMediaPlayerClientSession *pClientSession)
{
    (void) pClientSession;
    // the IMediaController base class requires an implementation
    //   of RestartAudio.  Unlike the other instances of IMediaController,
    //   AudioPlayer does NOT have a DisplaySession member.  Because
    //   of this, AudioPlayer's implementation does nothing.
}

void
AudioPlayer::deleteAllClientSession()
{
    // the IMediaController base class requires an implementation
    //   of RestartAudio.  Unlike the other instances of IMediaController,
    //   AudioPlayer does NOT have a DisplaySession member.  Because
    //   of this, AudioPlayer's implementation does nothing.
}

eCsciMspDiagStatus
AudioPlayer::GetMspStreamingInfo(DiagMspStreamingInfo *streamingInfo)
{
    (void) streamingInfo;
    return kCsciMspDiagStat_NoData;
}

/* Returns the CCI byte value */
uint8_t AudioPlayer::GetCciBits(void)
{
    return 0;
}

// Sets the EAS audio playback active status
// True  - EAS audi playback is in progress
// False - EAS audio playback is not in progress
void AudioPlayer::SetEasAudioActive(bool active)
{
    mEasAudioActive = active;
}


