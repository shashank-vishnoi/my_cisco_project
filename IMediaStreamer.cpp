#include <iostream>
#include "IMediaStreamer.h"
#include "IMediaPlayerSession.h"
#include "IMediaPlayer.h"
#if defined(DMALLOC)
#include "dmalloc.h"
#endif
#include "mrdvrserver.h"
#include "CDvrPriorityMediator.h"
#include "MediaControllerClassFactory.h"
#if (ENABLE_MSPMEDIASHRINK == 1 )
#include "MSPMediaShrink.h"
#endif

#include <dlog.h>
#ifdef LOG
#error  LOG already defined
#endif

#include "pthread_named.h"

#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"IMediaStreamer:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#define USE_PARAM(a) (void)a;

std::list<IMediaPlayerSession*> IMediaStreamer::mstreamingSessionList(0);

IMediaStreamer *IMediaStreamer::mInstance = NULL;

///This method creates an IMediaStreamer instance.
IMediaStreamer * IMediaStreamer::getMediaStreamerInstance()
{
    if (mInstance == NULL)
    {
        mInstance = new IMediaStreamer();
    }

    return mInstance;
}

///Constructor
IMediaStreamer::IMediaStreamer()
{
    FNLOG(DL_MSP_MPLAYER);

    // Initialising the mutex that protects Mediastreamer shared resources
    pthread_mutexattr_t mta;
    pthread_mutexattr_init(&mta);
    pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m_StreamerMutex, &mta);
    pthread_mutex_setname_np(&m_StreamerMutex, "MEDIA_STREAMER_MUTEX");
    pthread_mutexattr_destroy(&mta);

    // create event queue for media streamer event scan thread
    threadEventQueue = new MSPEventQueue();

    // create media streamer event handler thread
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024);

    int thread_ret_value = pthread_create(&mEventHandlerThread, &attr , streamerEventThreadFunc, (void *) this);
    if (thread_ret_value)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "*****Error in thread create");
    }

    thread_ret_value = pthread_setname_np(mEventHandlerThread, "MediaStreamer Event handler");
    if (0 != thread_ret_value)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Thread Setname Failed:%d", thread_ret_value);
    }

    thread_ret_value = pthread_attr_destroy(&attr);
    if (0 != thread_ret_value)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Thread destory attribute Failed:%d", thread_ret_value);
    }
}

///Destructor
IMediaStreamer::~IMediaStreamer()
{
    FNLOG(DL_MSP_MPLAYER);

    // Exit the thread and wait for the thread to exit
    if ((threadEventQueue != NULL) && (mEventHandlerThread))
    {
        queueEvent(kMediaStreamerEventThreadExit, NULL);
        pthread_join(mEventHandlerThread, NULL);         // wait for event thread to exit
        mEventHandlerThread = 0;
    }

    // Delete the event queue
    if (threadEventQueue != NULL)
    {
        delete threadEventQueue;
        threadEventQueue = NULL;
    }

    pthread_mutex_destroy(&m_StreamerMutex);
}

/// Streamer mutex is introduced here to prevent streaming session from being destroyed and CCI callback being executed .

void IMediaStreamer::lockmutex()
{
    pthread_mutex_lock(&m_StreamerMutex);
}


void IMediaStreamer::unlockmutex()
{
    pthread_mutex_unlock(&m_StreamerMutex);
}

std::list<IMediaPlayerSession *>::iterator IMediaStreamer::getIterator(IMediaPlayerSession * session)
{
    std::list<IMediaPlayerSession *>::iterator iter;

    for (iter =  mstreamingSessionList.begin(); iter !=  mstreamingSessionList.end(); iter++)
    {
        if (*iter == session)
        {
            break;
        }
    }
    return iter;
}


///This method creates an IMediaPlayerSession instance.
eIMediaPlayerStatus IMediaStreamer::IMediaStreamerSession_Create(IMediaPlayerSession ** ppIMediaPlayerSession,
        IMediaPlayerStatusCallback eventStatusCB,
        void* pClientContext)
{
    FNLOG(DL_MSP_MPLAYER);

    lockmutex();

#if defined(__PERF_DEBUG__)
    START_TIME;
#endif
    *ppIMediaPlayerSession = NULL;
    *ppIMediaPlayerSession = new IMediaPlayerSession(eventStatusCB, pClientContext);
    if (*ppIMediaPlayerSession)
    {
        mstreamingSessionList.push_back(*ppIMediaPlayerSession);
        LOG(DLOGL_NOISE, " session: %p   (%d total sessions)   **SAIL API**", *ppIMediaPlayerSession,  mstreamingSessionList.size());
#if defined(__PERF_DEBUG__)
        END_TIME;
        PRINT_EXEC_TIME;
#endif
        unlockmutex();
        return kMediaPlayerStatus_Ok;
    }
    else
    {
#if defined(__PERF_DEBUG__)
        END_TIME;
        PRINT_EXEC_TIME;
#endif
        unlockmutex();
        return kMediaPlayerStatus_Error_OutOfMemory;
    }


}


///Loads a service into the media player session.
eIMediaPlayerStatus IMediaStreamer::IMediaStreamerSession_Load(IMediaPlayerSession *pIMediaPlayerSession,
        const char * serviceUrl,
        const MultiMediaEvent** pMme)
{
    FNLOG(DL_MSP_MPLAYER);

    lockmutex();
    IMediaController* mediaController = NULL;

#if defined(__PERF_DEBUG__)
    START_TIME;
#endif
    //We allow a single EMERGENCY log message here for each channel change
    LOG(DLOGL_EMERGENCY, " URL: %s  session: %p     **SAIL API**", serviceUrl, pIMediaPlayerSession);

    USE_PARAM(pMme);

    if (!IsSessionRegistered(pIMediaPlayerSession) || (pIMediaPlayerSession == NULL))
    {
        unlockmutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    if (!serviceUrl)
    {
        LOG(DLOGL_ERROR, "Warning: null serviceUrl");
        unlockmutex();
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    LOG(DLOGL_NOISE, "Existing URL: %s", serviceUrl);

    mediaController = pIMediaPlayerSession->getMediaController();
    if (mediaController != NULL)
    {
        //Ideally it should not happen
        pIMediaPlayerSession->clearMediaController();
        delete mediaController; // explicitly legal to delete NULL here in case of no old controller
        mediaController = NULL;
    }
    mediaController = MediaControllerClassFactory::CreateController(serviceUrl, pIMediaPlayerSession);
    if (mediaController)
    {
        pIMediaPlayerSession->setMediaController(mediaController);
        mediaController->lockMutex();
        mediaController->RegisterCCICallback(pIMediaPlayerSession, StreamerCCIUpdated);
        mediaController->unLockMutex();
    }

    if (mediaController)
    {
        //US44524: apply a default resitrict CCI setting right away here. When the real callback comes, it will overwrite the default one
        ProcessStreamerCCIUpdated(pIMediaPlayerSession, DEFAULT_RESTRICTIVE_CCI);
        CDvrPriorityMediator::updateUsedTuners(std::string(serviceUrl), kMPlaySessLoad, 0, 0, pMme);
        if (*pMme)
        {
#if PLATFORM_NAME == G8
            Csci_Msp_MrdvrSrv_SetConflictStatus(kCsciMspMrdvrConflictStatus_Detected);
#endif
            unlockmutex();
            return kMediaPlayerStatus_TuningResourceUnavailable;
        }
        pIMediaPlayerSession->SetServiceUrl(serviceUrl);
        mediaController->lockMutex();
        status = mediaController->Load(serviceUrl, pMme);
        mediaController->unLockMutex();
    }
    else
    {
        status = kMediaPlayerStatus_Error_InvalidURL;
        LOG(DLOGL_ERROR, "Error creating media controller for url %s", serviceUrl);
    }
#if defined(__PERF_DEBUG__)
    END_TIME;
    PRINT_EXEC_TIME;
#endif
    unlockmutex();
    return status;

}

///   This method begins streaming the media service. Physical resources get assigned during this operation.
eIMediaPlayerStatus IMediaStreamer::IMediaStreamerSession_Play(IMediaPlayerSession *pIMediaPlayerSession,
        const char *outputUrl,
        float nptStartTime,
        const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_MPLAYER);

    lockmutex();

    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;
    LOG(DLOGL_NOISE, "sess: %p  outputUrl: %s  npt: %f   **SAIL API**", pIMediaPlayerSession, outputUrl, nptStartTime);

    if (!IsSessionRegistered(pIMediaPlayerSession) || (pIMediaPlayerSession == NULL))
    {
        unlockmutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    IMediaController *controller = pIMediaPlayerSession->getMediaController();

    if (controller)
    {
        CDvrPriorityMediator::updateUsedTuners(controller->GetSourceURL(true), kMPlaySessNoChange, +1, 0, NULL);
    }

    status = pIMediaPlayerSession->Play(outputUrl, nptStartTime, pMme);
    unlockmutex();
    return status;
}

/// This method stops the media service streaming.
eIMediaPlayerStatus IMediaStreamer::IMediaStreamerSession_Stop(IMediaPlayerSession *pIMediaPlayerSession,
        bool stopPlay,
        bool stopPersistentRecord)
{
    FNLOG(DL_MSP_MPLAYER);

    lockmutex();

    LOG(DLOGL_NOISE, "sess: %p  stopPlay: %d  stopRec: %d  **SAIL API**", pIMediaPlayerSession, stopPlay, stopPersistentRecord);

    if (!IsSessionRegistered(pIMediaPlayerSession) || (pIMediaPlayerSession == NULL))
    {
        unlockmutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    IMediaController *controller = pIMediaPlayerSession->getMediaController();

    if (controller)
    {
        //We will unlock the mutex only after completion of Complete Stop event.
        //A case occurred where, mutex was released after deleteAllClientSession(), and SDV context(SetApplicationDataPidExt)
        //Got the lock, and resulted abnormalities.

        controller->lockMutex();

        if (false == controller->isRecordingPlayback() || controller->isLiveSourceUsed() == true)
        {
            if ((false == controller->isLiveRecording() && (stopPlay))) //channel change with no recording ON.Release session from conflict list
            {
                LOG(DLOGL_NOISE, "No recording on the session. Hence requesting priority mediator to release session from list");
                bool bLiveSrc = controller->isLiveSourceUsed();
                CDvrPriorityMediator::updateUsedTuners(controller->GetSourceURL(bLiveSrc), kMPlaySessEject, 0, 0, NULL);

                pIMediaPlayerSession->deleteAllClientSession();

                //Delete Mosaic/Music Appdata Section
                controller->SetApplicationDataPid(INVALID_PID_VALUE);
            }
            else
            {
                LOG(DLOGL_NOISE, "Recording goes on in the  session. Hence updating,as per request");
                CDvrPriorityMediator::updateUsedTuners(controller->GetSourceURL(), kMPlaySessNoChange, stopPlay ? -1 : 0,
                                                       stopPersistentRecord ? -1 : 0, NULL);
            }
        }

        status = controller->Stop(stopPlay, stopPersistentRecord);
        if (stopPlay)
        {
            pIMediaPlayerSession->SetAudioFocus(false);
        }

#if (ENABLE_MSPMEDIASHRINK == 1)

        MSPMediaShrink* mshkObj = NULL;
        mshkObj = MSPMediaShrink::getMediaShrinkInstance();

        if ((true == controller->isRecordingPlayback() && (stopPlay)))
        {

            if (mshkObj)
            {
                mshkObj->RecPlayBackStopNotification();
            }
        }

        if (stopPersistentRecord)
        {
            if (mshkObj)
            {
                mshkObj->RecStopNotification();
            }
        }
#endif
        controller->unLockMutex();

    }
    else
    {
        LOG(DLOGL_NOISE, "Error: null controller");
        status = kMediaPlayerStatus_Error_OutOfState;
    }
    unlockmutex();
    return status;


}



///Ejects service from the Media Player Session.
eIMediaPlayerStatus IMediaStreamer::IMediaStreamerSession_Eject(IMediaPlayerSession *pIMediaPlayerSession)
{
    FNLOG(DL_MSP_MPLAYER);

    lockmutex();

    LOG(DLOGL_NOISE, "session: %p    **SAIL API**", pIMediaPlayerSession);

    if (!IsSessionRegistered(pIMediaPlayerSession) || (pIMediaPlayerSession == NULL))
    {
        unlockmutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    // Session is Shutting down so apply CCI policy again.
    pIMediaPlayerSession->SetCCI(0);



    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    IMediaController* controller = pIMediaPlayerSession->getMediaController();

    if (controller)
    {
        LOG(DLOGL_NOISE, "srcURL %s, isPlayback %d", controller->GetSourceURL().c_str(), controller->isRecordingPlayback());

        CDvrPriorityMediator::updateUsedTuners(controller->GetSourceURL(true), kMPlaySessEject, 0, 0, NULL);

        LOG(DLOGL_REALLY_NOISY, "controller->lockMutex");
        controller->lockMutex();

        pIMediaPlayerSession->deleteAllClientSession();

        LOG(DLOGL_REALLY_NOISY, "controller->Eject");
        controller->Eject();

        LOG(DLOGL_REALLY_NOISY, "clearMediaController");
        pIMediaPlayerSession->clearMediaController();

        LOG(DLOGL_REALLY_NOISY, "controller->unLockMutex");
        controller->unLockMutex();

        LOG(DLOGL_REALLY_NOISY, "delete controller");
        delete controller;
        controller = NULL;
    }
    else
    {
        LOG(DLOGL_ERROR, "Warning: no controller for session - out-of-state");
        status = kMediaPlayerStatus_Error_OutOfState;
    }

    unlockmutex();
    return status;


}

///This method destroys the Media Player Session.
eIMediaPlayerStatus IMediaStreamer::IMediaStreamerSession_Destroy(IMediaPlayerSession *pIMediaPlayerSession)
{
    FNLOG(DL_MSP_MPLAYER);
    lockmutex();
    LOG(DLOGL_NOISE, " session: %p  **SAIL API**", pIMediaPlayerSession);

    if (!IsSessionRegistered(pIMediaPlayerSession) || (pIMediaPlayerSession == NULL))
    {
        unlockmutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    IMediaController* controller = pIMediaPlayerSession->getMediaController();
    if (controller)
    {
        CDvrPriorityMediator::updateUsedTuners(controller->GetSourceURL(true), kMPlaySessFailed, 0, 0, NULL);
    }

    mstreamingSessionList.remove(pIMediaPlayerSession);
    delete pIMediaPlayerSession;
    pIMediaPlayerSession = NULL;

    LOG(DLOGL_REALLY_NOISY, " In Destroy (%d total sessions)   **SAIL API**", mstreamingSessionList.size());

    unlockmutex();
    return kMediaPlayerStatus_Ok;



}

///Sets the playback speed of the IMediaPlayerSession.
eIMediaPlayerStatus IMediaStreamer::IMediaStreamerSession_SetSpeed(IMediaPlayerSession* pIMediaPlayerSession,
        int numerator,
        unsigned int denominator)
{

    lockmutex();

    LOG(DLOGL_NOISE, "sess: %p  numerator: %d  denominator: %d  **SAIL API**", pIMediaPlayerSession, numerator, denominator);

    eIMediaPlayerStatus status;

    if (getIterator(pIMediaPlayerSession) != mstreamingSessionList.end() && (pIMediaPlayerSession != NULL))
    {
        IMediaController* controller = pIMediaPlayerSession->getMediaController();
        if (controller)
        {
            controller->lockMutex();
            status = controller->SetSpeed(numerator, denominator);
            controller->unLockMutex();
        }
        else
        {
            status = kMediaPlayerStatus_Error_OutOfState;
        }
    }
    else
    {
        status = kMediaPlayerStatus_Error_UnknownSession;
    }

    if (status == kMediaPlayerStatus_Ok)
    {
        LOG(DLOGL_NOISE, "returning OK -  num: %d  denom: %d", numerator, denominator);
    }
    else
    {
        LOG(DLOGL_NOISE, "warning returning status: %d", status);
    }

    unlockmutex();
    return status;

}

//Gets the current playback speed on an IMediaPlayerSession.
eIMediaPlayerStatus IMediaStreamer::IMediaStreamerSession_GetSpeed(IMediaPlayerSession* pIMediaPlayerSession,
        int* pNumerator,
        unsigned int* pDenominator)
{
    lockmutex();

    LOG(DLOGL_FUNCTION_CALLS, "enter");

    eIMediaPlayerStatus status;

    if (getIterator(pIMediaPlayerSession) != mstreamingSessionList.end() && (pIMediaPlayerSession != NULL))
    {
        IMediaController* controller = pIMediaPlayerSession->getMediaController();
        if (controller)
        {
            controller->lockMutex();
            status = controller->GetSpeed(pNumerator, pDenominator);
            controller->unLockMutex();
        }
        else
        {
            status = kMediaPlayerStatus_Error_OutOfState;
        }
    }
    else
    {
        status = kMediaPlayerStatus_Error_UnknownSession;
    }

    if (status == kMediaPlayerStatus_Ok)
    {
        LOG(DLOGL_NOISE, "returning OK - num: %d  denom : %d", *pNumerator, *pDenominator);
    }
    else
    {
        LOG(DLOGL_NOISE, "warning returning status: %d", status);
    }

    unlockmutex();
    return status;


}


//Sets an approximated current NPT (Normal Play Time) position.
eIMediaPlayerStatus IMediaStreamer::IMediaStreamerSession_SetPosition(IMediaPlayerSession* pIMediaPlayerSession, float nptTime)
{
    lockmutex();

    LOG(DLOGL_FUNCTION_CALLS, "enter nptTime: %f", nptTime);

    eIMediaPlayerStatus status;

    if (getIterator(pIMediaPlayerSession) != mstreamingSessionList.end() && (pIMediaPlayerSession != NULL))
    {
        IMediaController* controller = pIMediaPlayerSession->getMediaController();
        if (controller)
        {
            controller->lockMutex();
            status = controller->SetPosition(nptTime);
            controller->unLockMutex();
        }
        else
        {
            status = kMediaPlayerStatus_Error_OutOfState;
        }
    }
    else
    {
        status = kMediaPlayerStatus_Error_UnknownSession;
    }

    if (status == kMediaPlayerStatus_Ok)
    {
        LOG(DLOGL_NOISE, "returning OK - nptTime: %f", nptTime);
    }
    else
    {
        LOG(DLOGL_NOISE, "warning returning status: %d", status);
    }

    unlockmutex();
    return status;

}


//Gets an approximated current NPT (Normal Play Time) position.
eIMediaPlayerStatus IMediaStreamer::IMediaStreamerSession_GetPosition(IMediaPlayerSession* pIMediaPlayerSession, float* pNptTime)
{
    lockmutex();
    LOG(DLOGL_FUNCTION_CALLS, "enter");

    eIMediaPlayerStatus status;

    if (getIterator(pIMediaPlayerSession) != mstreamingSessionList.end() && (pIMediaPlayerSession != NULL))
    {
        IMediaController* controller = pIMediaPlayerSession->getMediaController();
        if (controller)
        {
            controller->lockMutex();
            status = controller->GetPosition(pNptTime);
            controller->unLockMutex();
        }
        else
        {
            status = kMediaPlayerStatus_Error_OutOfState;
        }
    }
    else
    {
        status = kMediaPlayerStatus_Error_UnknownSession;
    }

    if (status == kMediaPlayerStatus_Ok)
    {
        LOG(DLOGL_NOISE, "returning OK - npt: %f", *pNptTime);
    }
    else
    {
        LOG(DLOGL_NOISE, "warning returning status: %d", status);
    }

    unlockmutex();
    return status;

}

bool IMediaStreamer::IsSessionRegistered(IMediaPlayerSession* pIMediaPlayerSession)
{
    if (getIterator(pIMediaPlayerSession) ==  mstreamingSessionList.end())
    {
        LOG(DLOGL_ERROR, "session %p not in list", pIMediaPlayerSession);
        return false;
    }

    return true;
}

void IMediaStreamer::GetMspCopyProtectionInfo(DiagCCIData *pStreamingCopyInfo, int *psessioncount)
{

    FNLOG(DL_MSP_MPLAYER);
    std::list<IMediaPlayerSession *>::iterator iter;
    std::string source, destination;
    char srcstring[7] =
    { '\0' };

    IMediaStreamer *streamer = IMediaStreamer::getMediaStreamerInstance();
    if (streamer)
    {
        streamer->lockmutex();
        for (iter = mstreamingSessionList.begin(); iter != mstreamingSessionList.end(); iter++, ++(*psessioncount))
        {
            LOG(DLOGL_REALLY_NOISY, " Number of session = %d", (*psessioncount) + 1);
            if (*psessioncount == 8)
            {
                LOG(DLOGL_EMERGENCY, "Total Number of session exceeds than the number of tuners, could be due to out of sequence,breaking here to avoid memory corrutpion ");
                break;

            }
            // Search for most restrictive values.
            CCIData sessionCCIData;
            sessionCCIData.emi = 0;
            sessionCCIData.aps = 0;
            sessionCCIData.cit = 0;
            sessionCCIData.rct = 0;
            IMediaPlayerSession *currentSession = *iter;
            if (*iter && (*iter)->getMediaController())
            {
                // get source url
                source = (*iter)->getMediaController()->GetSourceURL();
                destination = (*iter)->getMediaController()->GetDestURL();
                LOG(DLOGL_NOISE, " source is  = %s, destination %s  ", source.c_str(), destination.c_str());
                strncpy(srcstring, source.c_str(), sizeof(srcstring));

                if (strncmp("sctetv", srcstring, sizeof(srcstring) - 1) == 0 ||
                        strncmp("sappv", srcstring, sizeof(srcstring) - 2) == 0)
                {
                    strncpy(pStreamingCopyInfo[*psessioncount].SrcStr, "RF", sizeof(pStreamingCopyInfo[*psessioncount].SrcStr));

                    if (Csci_Msp_MrdvrSrv_IsIPClientSession((*iter)))  // streaming to IP client
                    {
                        strncpy(pStreamingCopyInfo[*psessioncount].DestStr, "In Home Net", sizeof(pStreamingCopyInfo[*psessioncount].DestStr));
                    }
                    else if (destination != "")  // go to output
                    {
                        strncpy(pStreamingCopyInfo[*psessioncount].DestStr, "Video Output", sizeof(pStreamingCopyInfo[*psessioncount].DestStr));
                    }
                    else // go to recording
                    {
                        strncpy(pStreamingCopyInfo[*psessioncount].DestStr, "Disk", sizeof(pStreamingCopyInfo[*psessioncount].DestStr));
                    }

                }
                else if (strncmp("sadvr", srcstring, sizeof(srcstring) - 2) == 0)
                {
                    strncpy(pStreamingCopyInfo[*psessioncount].SrcStr, "Disk", sizeof(pStreamingCopyInfo[*psessioncount].SrcStr));
                    strncpy(pStreamingCopyInfo[*psessioncount].DestStr, "Video Output", sizeof(pStreamingCopyInfo[*psessioncount].DestStr));
                }

                else if (strncmp("mrdvr", srcstring, sizeof(srcstring) - 2) == 0)
                {
                    strncpy(pStreamingCopyInfo[*psessioncount].SrcStr, "In Home Net", sizeof(pStreamingCopyInfo[*psessioncount].SrcStr));
                    strncpy(pStreamingCopyInfo[*psessioncount].DestStr, "Video Output", sizeof(pStreamingCopyInfo[*psessioncount].DestStr));
                }
#if PLATFORM_NAME == G8
                else if (source.find("avfs://item=live") == 0 ||
                         source.find("avfs://item=vod") == 0 ||
                         source.find("avfs://item=sappv") == 0)
                {
                    strncpy(pStreamingCopyInfo[*psessioncount].SrcStr, "RF", sizeof(pStreamingCopyInfo[*psessioncount].SrcStr));
                    strncpy(pStreamingCopyInfo[*psessioncount].DestStr, "In Home Net", sizeof(pStreamingCopyInfo[*psessioncount].DestStr));
                }
#endif
                else if (source.find(MRDVR_REC_STREAMING_URL) == 0)   // MRDVR streaming to IP client,"svfs://"
                {
                    strncpy(pStreamingCopyInfo[*psessioncount].SrcStr, "Disk", sizeof(pStreamingCopyInfo[*psessioncount].SrcStr));
                    strncpy(pStreamingCopyInfo[*psessioncount].DestStr, "In Home Net", sizeof(pStreamingCopyInfo[*psessioncount].DestStr));
                }
                else   //on demand video
                {
                    strncpy(pStreamingCopyInfo[*psessioncount].SrcStr, "RF", sizeof(pStreamingCopyInfo[*psessioncount].SrcStr));
                    strncpy(pStreamingCopyInfo[*psessioncount].DestStr, "Video Output", sizeof(pStreamingCopyInfo[*psessioncount].DestStr));
                }

                // For diag page, just return the original CCI byte saved for each session
                // Fetch CCI data, do not compare which one is more restrictive, which is done when applying to AVPM
                (*iter)->GetCCI(sessionCCIData);

                pStreamingCopyInfo[*psessioncount].emi = sessionCCIData.emi;

                pStreamingCopyInfo[*psessioncount].aps = sessionCCIData.aps;

                pStreamingCopyInfo[*psessioncount].cit = sessionCCIData.cit;

                pStreamingCopyInfo[*psessioncount].rct = sessionCCIData.rct;

                if (sessionCCIData.emi == 0)
                {
                    if (sessionCCIData.rct == 0)
                    {
                        pStreamingCopyInfo[*psessioncount].epn = 0;
                    }
                    else
                    {
                        pStreamingCopyInfo[*psessioncount].epn = 1;
                    }
                }
                LOG(
                    DLOGL_REALLY_NOISY,
                    "Session %p,  cit %d, aps %d emi %d  rct %d epn %d",
                    currentSession, pStreamingCopyInfo[*psessioncount].cit, pStreamingCopyInfo[*psessioncount].aps, pStreamingCopyInfo[*psessioncount].emi, pStreamingCopyInfo[*psessioncount].rct, pStreamingCopyInfo[*psessioncount].epn);
            }
            LOG(DLOGL_REALLY_NOISY, "Source  %s, Destination %s", pStreamingCopyInfo->SrcStr, pStreamingCopyInfo->DestStr);
        }
        streamer->unlockmutex();
    }
    LOG(DLOGL_NORMAL, " TOTAL Number of Streaming session = %d", *psessioncount);
}


eIMediaPlayerStatus IMediaStreamer::IMediaStreamerSession_SetApplicationDataPidExt(IMediaPlayerSession *pIMediaPlayerSession,
        IMediaPlayerStatusCallback eventStatusCB, void *pEventStatusCBClientContext, uint32_t pid, const tIMediaPlayerSfltFilterGroup *sfltGroup,
        IMediaPlayerClientSession **ppClientSession)
{
    FNLOG(DL_MSP_MPLAYER);

    lockmutex();

    eIMediaPlayerStatus status;
    LOG(DLOGL_NORMAL, "SETTING APPLICATION PID=%d ", pid);

    if (getIterator(pIMediaPlayerSession) == mstreamingSessionList.end() || (pIMediaPlayerSession == NULL))
    {
        LOG(DLOGL_ERROR, "SETTING APPLICATION PID=%d EARLY RETURN (UNKNOWN SESSION)", pid);
        unlockmutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    IMediaController *controller = pIMediaPlayerSession->getMediaController();
    if (controller)
    {
        controller->lockMutex();
        //Keeping addClientSession into the controller's Lock
        //It will avoid the race condition
        if (pid != INVALID_PID_VALUE)
        {
            *ppClientSession = new IMediaPlayerClientSession(eventStatusCB, pid, sfltGroup);
            if (*ppClientSession)
            {
                pIMediaPlayerSession->addClientSession(*ppClientSession);
            }
            else
            {
                LOG(DLOGL_ERROR, "Failed to  Allocate memory for new client session");
                status = kMediaPlayerStatus_Error_Unknown;
                controller->unLockMutex();
                unlockmutex();
                return status;
            }
        }
        else
        {
            (*ppClientSession)->mPid = pid;
        }

        pEventStatusCBClientContext = (void *) controller->GetSDVClentContext(*ppClientSession);
        status = controller->SetApplicationDataPidExt(*ppClientSession);
        controller->unLockMutex();
    }
    else
    {
        status = kMediaPlayerStatus_Error_OutOfState;
        LOG(DLOGL_ERROR, "%s: Error: null controller", __FUNCTION__);
    }

    //delete and remove the clientsession from the list.

    if (pid == INVALID_PID_VALUE)
    {
        if (controller)
        {
            controller->lockMutex();
            pIMediaPlayerSession->deleteClientSession(*ppClientSession);
            controller->unLockMutex();
        }
        else
        {
            status = kMediaPlayerStatus_Error_OutOfState;
            LOG(DLOGL_ERROR, "%s: Error: null controller", __FUNCTION__);
        }
    }

    unlockmutex();

    return status;
}

eIMediaPlayerStatus IMediaStreamer::IMediaStreamerSession_GetApplicationDataExt(IMediaPlayerClientSession * pClientSession,
        IMediaPlayerSession *pIMediaPlayerSession, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    FNLOG(DL_MSP_MPLAYER);

    lockmutex();

    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;
    if (getIterator(pIMediaPlayerSession) == mstreamingSessionList.end() || (pIMediaPlayerSession == NULL))
    {
        unlockmutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    //If ClientSession does not exists, don't call getApplicationDataExt
    if (pClientSession == NULL)
    {
        unlockmutex();
        return kMediaPlayerStatus_Ok;
    }



    IMediaController *controller = pIMediaPlayerSession->getMediaController();
    if (controller)
    {
        controller->lockMutex();
        status = controller->GetApplicationDataExt(pClientSession, bufferSize, buffer, dataSize);
        controller->unLockMutex();
    }
    else
    {
        status = kMediaPlayerStatus_Error_OutOfState;
        LOG(DLOGL_ERROR, "%s: Error: null controller", __FUNCTION__);
    }

    unlockmutex();

    return status;
}

//Stops the streaming source associated with the IMediaPlayerSession
eIMediaPlayerStatus IMediaStreamer::IMediaStreamerSession_StopStreaming(IMediaPlayerSession *pIMediaPlayerSession)
{
    FNLOG(DL_MSP_MPLAYER);
    lockmutex();
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;
    if (getIterator(pIMediaPlayerSession) != mstreamingSessionList.end() && (pIMediaPlayerSession != NULL))
    {
        IMediaController* controller = pIMediaPlayerSession->getMediaController();
        if (controller)
        {
            controller->lockMutex();
            LOG(DLOGL_REALLY_NOISY, "%s: calling controller stop streaming", __FUNCTION__);
            status = controller->StopStreaming();
            controller->unLockMutex();
        }
        else
        {
            status = kMediaPlayerStatus_Error_OutOfState;
        }
    }
    else
    {
        status = kMediaPlayerStatus_Error_UnknownSession;
    }
    unlockmutex();
    return status;
}

void* IMediaStreamer::streamerEventThreadFunc(void *data)
{
    bool done = false;

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "MediaStreamer Event processing thread entry function");

    while (!done)
    {
        IMediaStreamer *inst = (IMediaStreamer *) data;
        if (inst == NULL)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Media Streamer instance is not yet created... Out Of State...");
            return NULL;
        }

        Event *evt = NULL;
        evt = inst->threadEventQueue->popEventQueue();
        if (!evt)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d) Error: Null event instance", __FUNCTION__, __LINE__);
            return NULL;
        }

        dlog(DL_MSP_MPLAYER, DLOGL_SIGNIFICANT_EVENT, "event received is %d...\n", evt->eventType);

        inst->lockmutex();
        switch (evt->eventType)
        {

        case kMediaStreamerEventCCIUpdated:
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "kMediaStreamerEventCCIUpdated event received...\n");

            tMediaStreamerSessionCciData *pCciData = (tMediaStreamerSessionCciData *) evt->eventData;
            if (pCciData)
            {
                inst->ProcessStreamerCCIUpdated(pCciData->pData, pCciData->CCIbyte);
                delete pCciData;
                pCciData = NULL;
            }
            else
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Null data. Ignoring the CCI updated event");
            }
        }
        break;

        case kMediaStreamerEventThreadExit:
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "kMediaStreamerEventThreadExit event received...\n");
            done = true;
        }
        break;
        default:
            break;
        }
        inst->threadEventQueue->freeEvent(evt);
        inst->unlockmutex();
    }

    pthread_exit(NULL);
    return NULL;
}

eIMediaPlayerStatus IMediaStreamer::queueEvent(eMediaStreamerEvent evtp, void* pData)
{
    if (!threadEventQueue)
    {
        LOG(DLOGL_ERROR, "warning: no queue to dispatch event %d", evtp);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    threadEventQueue->dispatchEvent(evtp, pData);
    return kMediaPlayerStatus_Ok;
}

void IMediaStreamer::StreamerCCIUpdated(void *pData, uint8_t CCIbyte)
{
    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "IMediaStreamer::StreamerCCIUpdated  is called with CCI value %d\n", CCIbyte);

    IMediaStreamer *instance = IMediaStreamer::getMediaStreamerInstance() ;
    if (instance)
    {
        tMediaStreamerSessionCciData *payLoadData = new tMediaStreamerSessionCciData();
        if (payLoadData == NULL)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Memory allocation failed... Not processing CCIUpdated event...");
            return;
        }

        payLoadData->pData = pData;
        payLoadData->CCIbyte = CCIbyte;

        eIMediaPlayerStatus status = instance->queueEvent(kMediaStreamerEventCCIUpdated, (void *) payLoadData);
        if (status != kMediaPlayerStatus_Ok)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Error in queuing event");
        }
    }
}

void IMediaStreamer::ProcessStreamerCCIUpdated(void *pData, uint8_t CCIbyte)
{
    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "IMediaStreamer::ProcessStreamerCCIUpdated  is called with CCI value %d\n", CCIbyte);

    if (pData)
    {
        lockmutex();

        IMediaPlayerSession *session = (IMediaPlayerSession *) pData;

        if (!IsSessionRegistered(session))
        {
            unlockmutex();
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "IMediaStreamer::CCI Updates received for an unknown session\n");
            return;
        }

        if (session->getMediaController())
        {
            session->SetCCI(CCIbyte);
            LOG(DLOGL_REALLY_NOISY, "CCI update is for the streaming session,so  Going to inject CCI data");
            session->getMediaController()->InjectCCI(CCIbyte);
        }

        unlockmutex();
    }
    else
    {
        LOG(DLOGL_ERROR, "can not retrieve session data as we received invalid user data");
    }

}
