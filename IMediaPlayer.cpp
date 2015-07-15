//#include <media-player-session.h>
#include <iostream>
#include <list>
#include "IMediaPlayer.h"
#include "IMediaStreamer.h"
#include "IMediaPlayerSession.h"
#if PLATFORM_NAME == IP_CLIENT
#include "zapper_ic.h"
#include "mrdvr_ic.h"
#include "avpm_ic.h"
#include "ApplicationDataExt_ic.h"
#endif
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#include "zapper.h"
#include "mrdvr.h"
#include "dvr.h"
#include "avpm.h"
#include "ondemand.h"
#include "ApplicationDataExt.h"
#include "CurrentVideoMgr.h"
#include "mrdvrserver.h"
#endif
#include "CDvrPriorityMediator.h"
#include "MediaControllerClassFactory.h"
#include "sail-settingsuser-api.h"
#include "use_common.h"
#include "MSPScopedPerfCheck.h"
#if (ENABLE_MSPMEDIASHRINK == 1 )
#include "MSPMediaShrink.h"
#endif
#include "MSPSourceFactory.h"

#if defined(DMALLOC)
#include "dmalloc.h"
#endif
#include <dlog.h>
#ifdef LOG
#error  LOG already defined
#endif

#include"pthread_named.h"

#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"IMediaPlayer:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

/**
 *Output Policies : Implemented for Diag Page Copy Protection Support
 * These values can be modified in future
 */
#define DVI_HDMI_POLICY   "0x0000000C v1"
#define YPrPb_POLICY      "0x00000004 v1"
#define PORT1394_POLICY   "0x00000000 v1"
#define Composite_POLICY  "0x00000000 v1"
#define VOD_POLICY        "0x00000000 v1"

#define RESMON_INIT_RETRY_COUNT 10

// TODO:  GET RID OF THIS INCLUDE FILE!!
#include "../include/misc_platform.h"
#if PLATFORM_NAME == IP_CLIENT
#include "cgmiPlayerApi.h"
#endif

#define USE_PARAM(PARAM) PARAM=PARAM;
#define COMP_CHUNKSIZE 3

IMediaPlayer *IMediaPlayer::mInstance = NULL;
std::list<IMediaPlayerSession*> IMediaPlayer::mSessionList(0);

IMediaPlayer::IMediaPlayer()
{
    FNLOG(DL_MSP_MPLAYER);

    mLiveRecCount = 0;
    mEasAudioActive = false;
    m_bConnected = false;
    m_fd = -1;

    // Initialising the mutex that protects Mediaplayer shared resources
    pthread_mutexattr_t mta;
    pthread_mutexattr_init(&mta);
    pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m_PlayerMutex, &mta);
    pthread_mutex_setname_np(&m_PlayerMutex, "MEDIA_PLAYER_MUTEX");
    pthread_mutexattr_destroy(&mta);

    // create event queue for media player event scan thread
    threadEventQueue = new MSPEventQueue();

    // create media player event handler thread
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024);

    int thread_create = pthread_create(&mEventHandlerThread, &attr , eventThreadFunc, (void *) this);
    if (thread_create)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "*****Error in thread create");
    }

    thread_create = pthread_setname_np(mEventHandlerThread, "MediaPlayer Event handler");
    if (0 != thread_create)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Thread Setname Failed:%d", thread_create);
    }

    thread_create = pthread_attr_destroy(&attr);
    if (0 != thread_create)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Thread Setname Failed:%d", thread_create);
    }

    // Retry RESMON_INIT_RETRY_COUNT times till we connect to ResMon server
    for (int i = 0; i < RESMON_INIT_RETRY_COUNT; i++)
    {
        eResMonStatus resMonStatus = ResMon_init(&m_fd);
        if (resMonStatus == kResMon_Ok)
        {
            m_bConnected = true;
            dlog(DL_MSP_MPLAYER, DLOGL_MINOR_DEBUG, "%s:%s:%d ZZOpen call ResMon_init return fd %d", __FILE__, __FUNCTION__, __LINE__, m_fd);
            break;
        }
        else
        {
            sleep(1);
            LOG(DLOGL_ERROR, "Could not connect to ResMon server, retrying...");
        }
    }
}

IMediaPlayer::~IMediaPlayer()
{
    FNLOG(DL_MSP_MPLAYER);

    eResMonStatus resMonStatus = ResMon_finalize(m_fd);
    if (resMonStatus != kResMon_Ok)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "ResMon_finalize failed status:%d", resMonStatus);
    }

    // Exit the thread and wait for the thread to exit
    if ((threadEventQueue != NULL) && (mEventHandlerThread))
    {
        queueEvent(kMediaPlayerEventThreadExit, NULL);
        pthread_join(mEventHandlerThread, NULL);         // wait for event thread to exit
        mEventHandlerThread = 0;
    }

    // Delete the event queue
    if (threadEventQueue != NULL)
    {
        delete threadEventQueue;
        threadEventQueue = NULL;
    }

    // Destroy the media player mutex
    pthread_mutex_destroy(&m_PlayerMutex);
}

// Locks the player mutex for protecting accessing and deletion of the below shared resources
//  1 - list of active sessions - mSessionList.push_back, remove, begin and end
//  2 - delete pIMediaPlayerSession;
//  3 - delete controller;
// between the threads
//  1 - GalioEntry
//  2 - SDV Handler
//  3 - Diag pages
//  4 - Dvr server adapter
//  5 - CA that notifies CCI updates
//  6 - MSP_MRDvr_ServerHandler
void IMediaPlayer::lockplayermutex()
{
    pthread_mutex_lock(&m_PlayerMutex);
}

// Unlocks the player mutex for protecting accessing and deletion of the below shared resources
//  1 - list of active sessions - mSessionList.push_back, remove, begin and end
//  2 - delete pIMediaPlayerSession;
//  3 - delete controller;
// between the threads
//  1 - GalioEntry
//  2 - SDV Handler
//  3 - Diag pages
//  4 - Dvr server adapter
//  5 - CA that notifies CCI updates
//  6 - MSP_MRDvr_ServerHandler
void IMediaPlayer::unlockplayermutex()
{
    pthread_mutex_unlock(&m_PlayerMutex);
}

IMediaPlayer * IMediaPlayer::getMediaPlayerInstance()
{
    if (mInstance == NULL)
    {
#if PLATFORM_NAME == IP_CLIENT
        cgmi_Status stat = CGMI_ERROR_SUCCESS;
        stat = cgmi_Init();
        if (stat != CGMI_ERROR_SUCCESS)
        {
            LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_Init Failed with %d \n", __FILE__, __LINE__, __FUNCTION__, stat);
            return mInstance;
        }
#endif
        mInstance = new IMediaPlayer();
    }
    return mInstance;
}

std::list<IMediaPlayerSession *>::iterator IMediaPlayer::getIterator(IMediaPlayerSession * session)
{
    std::list<IMediaPlayerSession *>::iterator iter;

    for (iter = mSessionList.begin(); iter != mSessionList.end(); iter++)
    {
        if (*iter == session)
        {
            break;
        }
    }

    return iter;
}

IMediaPlayerSession* IMediaPlayer::IMediaPlayerSession_FindSessionFromServiceUrl(const char *srvUrl)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    std::list<IMediaPlayerSession *>::iterator iter;
    IMediaPlayerSession *session = NULL;

    for (iter = mSessionList.begin(); iter != mSessionList.end(); iter++)
    {
        IMediaController *controller = (*iter)->getMediaController();
        if (controller)
        {
            std::string str = controller->GetSourceURL();
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s(%d)GetSourceURL returns: %s", __FUNCTION__, __LINE__, str.c_str());
            if (strcmp(srvUrl, str.c_str()) == 0)
            {
                session = *iter;
                break;
            }
        }
    }

    unlockplayermutex();

    return session;
}

bool IMediaPlayer::IMediaPlayerSession_IsServiceUrlActive()
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    bool status = false;
    std::list<IMediaPlayerSession *>::iterator iter1;

    for (iter1 = mSessionList.begin(); iter1 != mSessionList.end(); iter1++)
    {
        IMediaController *controller = (*iter1)->getMediaController();
        if (controller)
        {
            std::string str = controller->GetSourceURL();

            if (((str.find(PPV_SOURCE_URI_PREFIX)) != std::string::npos) || ((str.find(VOD_SOURCE_URI_PREFIX)) != std::string::npos))
            {
                status = true;
            }
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d)Invalid controller type", __FUNCTION__, __LINE__);
        }
    }

    unlockplayermutex();

    return status;
}

eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_AttachCallback(IMediaPlayerSession *pIMediaPlayerSession,
        IMediaPlayerStatusCallback eventStatusCB, void *pClientContext)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    if (!IsSessionRegistered(pIMediaPlayerSession))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    eIMediaPlayerStatus status = kMediaPlayerStatus_Error_Unknown;
    IMediaController *controller = pIMediaPlayerSession->getMediaController();
    if (controller)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s(%d)Setting callback", __FUNCTION__, __LINE__);
        status = controller->SetCallback(pIMediaPlayerSession, eventStatusCB, pClientContext);
    }

    unlockplayermutex();

    return status;
}

eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_DetachCallback(IMediaPlayerSession *pIMediaPlayerSession,
        IMediaPlayerStatusCallback eventStatusCB)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    if (!IsSessionRegistered(pIMediaPlayerSession))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    eIMediaPlayerStatus status = kMediaPlayerStatus_Error_Unknown;
    IMediaController *controller = pIMediaPlayerSession->getMediaController();
    if (controller)
    {
        status = controller->DetachCallback(eventStatusCB);
    }

    unlockplayermutex();

    return status;
}

eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_Create(IMediaPlayerSession ** ppIMediaPlayerSession,
        IMediaPlayerStatusCallback eventStatusCB, void* pClientContext)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

#if defined(__PERF_DEBUG__)
    START_TIME;
#endif
    *ppIMediaPlayerSession = NULL;
    *ppIMediaPlayerSession = new IMediaPlayerSession(eventStatusCB, pClientContext);
    if (*ppIMediaPlayerSession)
    {
        //session list should not be accessed by any other thread same time
        //hence protecting under player mutex

        mSessionList.push_back(*ppIMediaPlayerSession);
        LOG(DLOGL_NORMAL, " session: %p   (%d total sessions)   **SAIL API**", *ppIMediaPlayerSession, mSessionList.size());

#if defined(__PERF_DEBUG__)
        END_TIME;
        PRINT_EXEC_TIME;
#endif

        unlockplayermutex();

        return kMediaPlayerStatus_Ok;
    }
    else
    {
#if defined(__PERF_DEBUG__)
        END_TIME;
        PRINT_EXEC_TIME;
#endif

        unlockplayermutex();

        return kMediaPlayerStatus_Error_OutOfMemory;
    }
}

///This method destroys an IMediaPlayerSession.
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_Destroy(IMediaPlayerSession *pIMediaPlayerSession)
{
    FNLOG(DL_MSP_MPLAYER);

    //session list and session should not be accessed by any other thread same time
    //hence protecting under player mutex
    lockplayermutex();

    LOG(DLOGL_NORMAL, " session: %p  **SAIL API**", pIMediaPlayerSession);

    if (!IsSessionRegistered(pIMediaPlayerSession) || (pIMediaPlayerSession == NULL))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    IMediaController* controller = pIMediaPlayerSession->getMediaController();
    if (controller)
    {
        CDvrPriorityMediator::updateUsedTuners(controller->GetSourceURL(true), kMPlaySessFailed, 0, 0, NULL);
    }

    mSessionList.remove(pIMediaPlayerSession);
    delete pIMediaPlayerSession;
    pIMediaPlayerSession = NULL;

    LOG(DLOGL_REALLY_NOISY, " In Destroy (%d total sessions)   **SAIL API**", mSessionList.size());

    unlockplayermutex();

    return kMediaPlayerStatus_Ok;
}

eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_Load(IMediaPlayerSession *pIMediaPlayerSession, const char * serviceUrl,
        const MultiMediaEvent** pMme)
{
    std::string oldSrcUrl;
    std::string newSrcUrl;
    IMediaController* mediaController;
    MediaControllerClassFactory::eControllerType oldControllerType = MediaControllerClassFactory::eControllerTypeUnknown;
    MediaControllerClassFactory::eControllerType newControllerType = MediaControllerClassFactory::eControllerTypeUnknown;

#if (ENABLE_MSPMEDIASHRINK == 1)
    eMSPSourceType newSrcType = kMSPInvalidSource;
#endif

    FNLOG(DL_MSP_MPLAYER);

    //controller should not be accessed by any other thread at the time of deletion
    //hence protecting under player mutex
    lockplayermutex();

#if defined(__PERF_DEBUG__)
    START_TIME;
#endif
    //We allow a single EMERGENCY log message here for each channel change
    LOG(DLOGL_EMERGENCY, " URL: %s  session: %p     **SAIL API**", serviceUrl, pIMediaPlayerSession);

    USE_PARAM(pMme);

    if (!IsSessionRegistered(pIMediaPlayerSession) || (pIMediaPlayerSession == NULL))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    if (!serviceUrl)
    {
        LOG(DLOGL_ERROR, "Warning: null serviceUrl");
        unlockplayermutex();
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    //
    // Change to re-use existing controller if it exists AND the controller type is the same and the
    // source URL type exactly matches the old one.
    // this hits the most common case which is plain channel change from normal channel to normal channel
    // it allows the controller and source to be re-used so we don't have to give up the tuner

    // get new controller type
    newSrcUrl = serviceUrl;
    newControllerType = MediaControllerClassFactory::GetControllerType(newSrcUrl);
#if (ENABLE_MSPMEDIASHRINK == 1)
    newSrcType = MSPSourceFactory :: getMSPSourceType(serviceUrl);

    LOG(DLOGL_NOISE, "newSrcType =%d kMSPFileSource =  %d", newSrcType , kMSPFileSource);
    if (newSrcType == kMSPFileSource)
    {
        MSPMediaShrink* mshkObj = NULL;
        if ((mshkObj = MSPMediaShrink::getMediaShrinkInstance()))
        {
            LOG(DLOGL_NOISE, "calling MSHNK RecPlayBackStartNotification");
            mshkObj->RecPlayBackStartNotification();
        }
    }
#endif
    LOG(DLOGL_NOISE, "Existing URL: %s", newSrcUrl.c_str());

    // and old controller type from old controller source url
    mediaController = pIMediaPlayerSession->getMediaController();
    if (mediaController != NULL)
    {
        oldSrcUrl = mediaController->GetSourceURL();
        oldControllerType = MediaControllerClassFactory::GetControllerType(oldSrcUrl);
        LOG(DLOGL_NOISE, "Existing controller on this session, src url is %s new url is %s", oldSrcUrl.c_str(), serviceUrl);
    }
    else
    {
        LOG(DLOGL_NOISE, "No existing controller on this session.");
    }

    if ((newControllerType == oldControllerType) && (newControllerType != MediaControllerClassFactory::eControllerTypeVod)
            && (newControllerType != MediaControllerClassFactory::eControllerTypeMrdvr)
            && (newControllerType != MediaControllerClassFactory::eControllerTypeHnOnDemandStreamer))
    {
        LOG(DLOGL_NOISE, "Media controller same type(%d). Not re-creating media controller for url %s",
            newControllerType, serviceUrl);
    }
    else
    {

        LOG(DLOGL_NOISE, "Media controller type mismatch %d %d. Delete old controller for session %p",
            oldControllerType, newControllerType, pIMediaPlayerSession);
        pIMediaPlayerSession->clearMediaController();
        delete mediaController; // explicitly legal to delete NULL here in case of no old controller
        mediaController = NULL;

        mediaController = MediaControllerClassFactory::CreateController(newSrcUrl, pIMediaPlayerSession);
        if (mediaController)
        {
            pIMediaPlayerSession->setMediaController(mediaController);
            mediaController->lockMutex();
            mediaController->SetEasAudioActive(mEasAudioActive);
            mediaController->RegisterCCICallback(pIMediaPlayerSession, CCIUpdated);
            mediaController->unLockMutex();
        }
        else
        {
            LOG(DLOGL_ERROR, "mediaController is NULL");

        }
    }

    if (mediaController)
    {
        CDvrPriorityMediator::updateUsedTuners(std::string(serviceUrl), kMPlaySessLoad, 0, 0, pMme);
        if (*pMme)
        {
#if PLATFORM_NAME == G8
            Csci_Msp_MrdvrSrv_SetConflictStatus(kCsciMspMrdvrConflictStatus_Detected);
#endif
            unlockplayermutex();
            return kMediaPlayerStatus_TuningResourceUnavailable;
        }

        pIMediaPlayerSession->SetServiceUrl(serviceUrl);

        mediaController->lockMutex();
        status = mediaController->Load(serviceUrl, pMme);
        mediaController->unLockMutex();

        //US44524: apply a default restrictive CCI setting right away here.
        if (strstr((char *)serviceUrl, FILE2_SOURCE_URI_PREFIX) != NULL || strstr((char *)serviceUrl, MRDVR_SOURCE_URI_PREFIX) != NULL)
        {
            //For local and remote recording playback apply DEFAULT_RESTRICTIVE_CCI
            ProcessCCIUpdated(pIMediaPlayerSession, DEFAULT_RESTRICTIVE_CCI);
        }
        else
        {
            //For live apply 0 - When the real callback comes, it will overwrite the default one
            ProcessCCIUpdated(pIMediaPlayerSession, 0);
        }
    }
    else
    {
        status = kMediaPlayerStatus_Error_InvalidURL;
        LOG(DLOGL_ERROR, "Error creating media controller for url %s", serviceUrl);
    }

    unlockplayermutex();

#if defined(__PERF_DEBUG__)
    END_TIME;
    PRINT_EXEC_TIME;
#endif
    return status;
}

///Ejects service from the Media Player Session.
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_Eject(IMediaPlayerSession *pIMediaPlayerSession)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    LOG(DLOGL_NORMAL, "session: %p    **SAIL API**", pIMediaPlayerSession);

    if (!IsSessionRegistered(pIMediaPlayerSession) || (pIMediaPlayerSession == NULL))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    // Session is Shutting down so apply CCI policy again.
    pIMediaPlayerSession->SetCCI(0);

    IMediaController* pController = pIMediaPlayerSession->getMediaController();
    if (pController)
    {
        //EAS session ejection is in progress.
        //Inform all the in progress sessions that EAS audio is disabled.
        if (strstr((char *)pController->GetSourceURL().c_str(), AUDIO_SOURCE_URI_PREFIX) != NULL)
        {
            SetEasAudioActive(false);
        }
    }


#if PLATFORM_NAME == IP_CLIENT
    updateCCI();
#endif

    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    IMediaController* controller = pIMediaPlayerSession->getMediaController();
    if (controller)
    {
        LOG(DLOGL_REALLY_NOISY, "controller->lockMutex");
        controller->lockMutex();

        LOG(DLOGL_NOISE, "srcURL %s, isPlayback %d", controller->GetSourceURL().c_str(), controller->isRecordingPlayback());
        CDvrPriorityMediator::updateUsedTuners(controller->GetSourceURL(true), kMPlaySessEject, 0, 0, NULL);

#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8

        bool IsForeGround = false;

        if (!controller->isBackground())
        {
            LOG(DLOGL_NORMAL, "Session is Foreground,so  updating the ports with the cci value");
            IsForeGround = true;
        }
        else
        {

            LOG(DLOGL_NORMAL, "Session is background, so not necessary to update the cci values to the port");
        }
#endif
        pIMediaPlayerSession->deleteAllClientSession();

        LOG(DLOGL_REALLY_NOISY, "controller->Eject");
        controller->Eject();

        LOG(DLOGL_REALLY_NOISY, "controller->unLockMutex");
        controller->unLockMutex();

        LOG(DLOGL_REALLY_NOISY, "clearMediaController");
        pIMediaPlayerSession->clearMediaController();

        LOG(DLOGL_REALLY_NOISY, "delete controller");
        delete controller;
        controller = NULL;

#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
        if (IsForeGround)
        {
            updateCCI();
        }
#endif

    }
    else
    {
        LOG(DLOGL_ERROR, "Warning: no controller for session - out-of-state");
        status = kMediaPlayerStatus_Error_OutOfState;
    }

    unlockplayermutex();

    return status;
}

eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_Play(IMediaPlayerSession *pIMediaPlayerSession, const char *outputUrl,
        float nptStartTime, const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;
    LOG(DLOGL_NORMAL, "sess: %p  outputUrl: %s  npt: %f   **SAIL API**", pIMediaPlayerSession, outputUrl, nptStartTime);

    if (!IsSessionRegistered(pIMediaPlayerSession) || (pIMediaPlayerSession == NULL))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    IMediaController *controller = pIMediaPlayerSession->getMediaController();

    if (controller)
    {
        CDvrPriorityMediator::updateUsedTuners(controller->GetSourceURL(true), kMPlaySessNoChange, +1, 0, NULL);
    }

    status = pIMediaPlayerSession->Play(outputUrl, nptStartTime, pMme);

    unlockplayermutex();

    return status;
}

eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_PersistentRecord(IMediaPlayerSession *pIMediaPlayerSession,
        const char *recordUrl, float nptRecordStartTime, float nptRecordStopTime, const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    LOG(DLOGL_EMERGENCY, "sess: %p  recordUrl: %s  start: %f   stop: %f    **SAIL API**",
        pIMediaPlayerSession, recordUrl, nptRecordStartTime, nptRecordStopTime);
#if defined(__PERF_DEBUG__)
    START_TIME;
#endif

    if (!IsSessionRegistered(pIMediaPlayerSession) || (pIMediaPlayerSession == NULL))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    IMediaController *controller = pIMediaPlayerSession->getMediaController();
    if (controller)
    {
        CDvrPriorityMediator::updateUsedTuners(controller->GetSourceURL(true), kMPlaySessNoChange, 0, +1, NULL);

        controller->lockMutex();
        status = controller->PersistentRecord(recordUrl, nptRecordStartTime, nptRecordStopTime, pMme);
        controller->unLockMutex();
#if (ENABLE_MSPMEDIASHRINK == 1)
        MSPMediaShrink* mshkObj = NULL;
        if ((mshkObj = MSPMediaShrink::getMediaShrinkInstance()))
        {
            mshkObj->RecStartNotification();
        }
#endif
    }
    else
    {
        status = kMediaPlayerStatus_Error_OutOfState;
    }

    unlockplayermutex();

#if defined(__PERF_DEBUG__)
    END_TIME;
    PRINT_EXEC_TIME;
#endif
    return status;
}

eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_Stop(IMediaPlayerSession *pIMediaPlayerSession, bool stopPlay,
        bool stopPersistentRecord)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    LOG(DLOGL_NORMAL, "sess: %p  stopPlay: %d  stopRec: %d  **SAIL API**", pIMediaPlayerSession, stopPlay, stopPersistentRecord);

    if (!IsSessionRegistered(pIMediaPlayerSession) || (pIMediaPlayerSession == NULL))
    {
        unlockplayermutex();
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

    unlockplayermutex();

    return status;
}

/// This method sets the playback speed of the media service.
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_SetSpeed(IMediaPlayerSession *pIMediaPlayerSession, int numerator,
        unsigned int denominator)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    LOG(DLOGL_NOISE, "sess: %p  numerator: %d  denominator: %d  **SAIL API**", pIMediaPlayerSession, numerator, denominator);

    eIMediaPlayerStatus status;

    if (getIterator(pIMediaPlayerSession) != mSessionList.end() && (pIMediaPlayerSession != NULL))
    {
        if (pIMediaPlayerSession->getMediaController())
        {
            pIMediaPlayerSession->getMediaController()->lockMutex();
            status = pIMediaPlayerSession->getMediaController()->SetSpeed(numerator, denominator);
            pIMediaPlayerSession->getMediaController()->unLockMutex();
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

    unlockplayermutex();

    return status;
}

///This method gets the current playback speed on an IMediaPlayerSession. Support speeds vary across plat-
///     form and service. Typical values: +/- x2, +/- x4, +/- x16, +/- x32, +/- x64, +/- x128.
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_GetSpeed(IMediaPlayerSession *pIMediaPlayerSession, int *pNumerator,
        unsigned int *pDenominator)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    eIMediaPlayerStatus status;

    if (getIterator(pIMediaPlayerSession) != mSessionList.end() && (pIMediaPlayerSession != NULL))
    {
        if (pIMediaPlayerSession->getMediaController())
        {
            pIMediaPlayerSession->getMediaController()->lockMutex();
            status = pIMediaPlayerSession->getMediaController()->GetSpeed(pNumerator, pDenominator);
            pIMediaPlayerSession->getMediaController()->unLockMutex();
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

    unlockplayermutex();

    return status;
}

///This method sets an approximated current NPT position.
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_SetPosition(IMediaPlayerSession *pIMediaPlayerSession, float nptTime)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    LOG(DLOGL_FUNCTION_CALLS, "enter nptTime: %f", nptTime);

    eIMediaPlayerStatus status;

    if (getIterator(pIMediaPlayerSession) != mSessionList.end() && (pIMediaPlayerSession != NULL))
    {
        if (pIMediaPlayerSession->getMediaController())
        {
            pIMediaPlayerSession->getMediaController()->lockMutex();
            status = pIMediaPlayerSession->getMediaController()->SetPosition(nptTime);
            pIMediaPlayerSession->getMediaController()->unLockMutex();
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

    unlockplayermutex();

    return status;
}
///This method gets an approximated current NPT position. NPT is relative to beginning of stream.
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_GetPosition(IMediaPlayerSession *pIMediaPlayerSession, float *pNptTime)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    eIMediaPlayerStatus status;

    if (getIterator(pIMediaPlayerSession) != mSessionList.end() && (pIMediaPlayerSession != NULL))
    {
        if (pIMediaPlayerSession->getMediaController())
        {
            pIMediaPlayerSession->getMediaController()->lockMutex();
            status = pIMediaPlayerSession->getMediaController()->GetPosition(pNptTime);
            pIMediaPlayerSession->getMediaController()->unLockMutex();
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

    unlockplayermutex();

    return status;
}

///This method gets an approximated start NPT position. NPT is relative to beginning of stream.
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_GetStartPosition(IMediaPlayerSession *pIMediaPlayerSession, float *pNptTime)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    eIMediaPlayerStatus status;

    if (getIterator(pIMediaPlayerSession) != mSessionList.end() && (pIMediaPlayerSession != NULL))
    {
        if (pIMediaPlayerSession->getMediaController())
        {
            pIMediaPlayerSession->getMediaController()->lockMutex();
            status = pIMediaPlayerSession->getMediaController()->GetStartPosition(pNptTime);
            pIMediaPlayerSession->getMediaController()->unLockMutex();
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

    unlockplayermutex();

    return status;
}

///This method gets an approximated end NPT position. NPT is relative to beginning of stream.
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_GetEndPosition(IMediaPlayerSession *pIMediaPlayerSession, float *pNptTime)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    eIMediaPlayerStatus status;

    if (getIterator(pIMediaPlayerSession) != mSessionList.end() && (pIMediaPlayerSession != NULL))
    {
        if (pIMediaPlayerSession->getMediaController())
        {
            pIMediaPlayerSession->getMediaController()->lockMutex();
            status = pIMediaPlayerSession->getMediaController()->GetEndPosition(pNptTime);
            pIMediaPlayerSession->getMediaController()->unLockMutex();
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

    unlockplayermutex();

    return status;
}

///This function is used to set the presentation parameters of the media player session.
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_SetPresentationParams(IMediaPlayerSession *pIMediaPlayerSession,
        tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    if (vidScreenRect)
    {
        LOG(
            DLOGL_NOISE,
            "Session: %p  x: %d  y:%d  width: %d  height: %d  audioFocus: %d  **SAIL API**",
            pIMediaPlayerSession, vidScreenRect->x, vidScreenRect->y, vidScreenRect->width, vidScreenRect->height, enableAudioFocus);
    }
    else
    {
        LOG(DLOGL_NOISE, "Session: %p  vidScreenRect: NULL!!  audioFocus: %d  **SAIL API**",
            pIMediaPlayerSession, enableAudioFocus);
    }

    eIMediaPlayerStatus status;

    if (!IsSessionRegistered(pIMediaPlayerSession) || (pIMediaPlayerSession == NULL))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    status =  pIMediaPlayerSession->SetPresentationParams(vidScreenRect, enablePictureModeSetting, enableAudioFocus);

    unlockplayermutex();

    return status;
}

/// This function is used to get the presentation parameters of the media player session.
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_GetPresentationParams(IMediaPlayerSession *pIMediaPlayerSession,
        tAvRect *vidScreenRect, bool *pEnablePictureModeSetting, bool *pEnableAudioFocus)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    eIMediaPlayerStatus status;

    if (!IsSessionRegistered(pIMediaPlayerSession) || (pIMediaPlayerSession == NULL))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    status =  pIMediaPlayerSession->GetPresentationParams(vidScreenRect, pEnablePictureModeSetting, pEnableAudioFocus);

    unlockplayermutex();

    return status;
}

/*** This method swaps the audio and/ or display control of two media player sessions. This is a convenience
 function provided for picture-in-picture (PIP) and picture-outside-picture (POP) user interfaces.
 **/
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_Swap(IMediaPlayerSession *pIMediaPlayerSession1,
        IMediaPlayerSession *pIMediaPlayerSession2, bool swapAudioFocus, bool swapDisplaySettings)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    LOG(DLOGL_NOISE, "sess1: %p  sess2: %p  swapAudio: %d  swapDisplay: %d   **SAIL API**",
        pIMediaPlayerSession1, pIMediaPlayerSession2, swapAudioFocus, swapDisplaySettings);

    if (!IsSessionRegistered(pIMediaPlayerSession1) || !IsSessionRegistered(pIMediaPlayerSession2)
            || (pIMediaPlayerSession1 == NULL) || (pIMediaPlayerSession2 == NULL))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    // verify valid controllers,  these could be null if session
    //   -  created but not loaded
    //   -  ejected but not destroyed

    IMediaController *controller_1 = pIMediaPlayerSession1->getMediaController();
    IMediaController *controller_2 = pIMediaPlayerSession2->getMediaController();

    if (!controller_1)
    {
        LOG(DLOGL_ERROR, "Error: No controller for sess1: %p (state error)", pIMediaPlayerSession1);
        unlockplayermutex();
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (!controller_2)
    {
        LOG(DLOGL_ERROR, "Error: No controller for sess2: %p (state error)", pIMediaPlayerSession2);
        unlockplayermutex();
        return kMediaPlayerStatus_Error_OutOfState;
    }

    bool enaPic1, enaPic2;
    bool enaAudio1, enaAudio2;
    tAvRect screenRect1, screenRect2;

    IMediaPlayerSession_GetPresentationParams(pIMediaPlayerSession1, &screenRect1, &enaPic1, &enaAudio1);
    IMediaPlayerSession_GetPresentationParams(pIMediaPlayerSession2, &screenRect2, &enaPic2, &enaAudio2);

    if (swapAudioFocus)
    {
        bool tmp = enaPic2;
        enaPic2 = enaPic1;
        enaPic1 = tmp;
    }

    if (swapDisplaySettings)
    {
        tAvRect tmp = screenRect2;
        screenRect2 = screenRect1;
        screenRect1 = tmp;
    }

    controller_1->CloseDisplaySession();
    controller_2->CloseDisplaySession();
    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s, %d, audioFocus = %d", __FUNCTION__, __LINE__, enaAudio1);
    controller_1->SetPresentationParams(&screenRect1, enaPic1, enaAudio1);
    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s, %d, audioFocus = %d", __FUNCTION__, __LINE__, enaAudio2);
    controller_2->SetPresentationParams(&screenRect2, enaPic2, enaAudio2);

    controller_1->StartDisplaySession();
    controller_2->StartDisplaySession();

    unlockplayermutex();

    return kMediaPlayerStatus_Ok;
}

///This method allows the services layer to configure presentation parameters for the media player session.
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_ConfigurePresentationParams(IMediaPlayerSession *pIMediaPlayerSession,
        tAvRect *vidScreenRect, bool pendingPictureModeSetting, bool pendingAudioFocus)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    if (vidScreenRect)
    {
        LOG(
            DLOGL_REALLY_NOISY,
            "Session: %p  x: %d  y:%d  width: %d  height: %d  pendingPic: %d pendigAudio: %d  **SAIL API**",
            pIMediaPlayerSession, vidScreenRect->x, vidScreenRect->y, vidScreenRect->width, vidScreenRect->height, pendingPictureModeSetting, pendingAudioFocus);
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "Session: %p  vidScreenRect: NULL!!   pendingPic: %d pendigAudio: %d  **SAIL API**",
            pIMediaPlayerSession, pendingPictureModeSetting, pendingAudioFocus);
    }

    if (getIterator(pIMediaPlayerSession) == mSessionList.end() || (pIMediaPlayerSession == NULL))
    {
        LOG(DLOGL_ERROR, "Error: unknown session: %p", pIMediaPlayerSession);
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    eIMediaPlayerStatus status = pIMediaPlayerSession->ConfigurePresentationParams(vidScreenRect, pendingPictureModeSetting,
                                 pendingAudioFocus);

    unlockplayermutex();

    return status;
}

/// This method is used to query the configured pending presentation params for the specified media player session.                                                                                             File Documentation
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_GetPendingPresentationParams(IMediaPlayerSession *pIMediaPlayerSession,
        tAvRect *vidScreenRect, bool *pPendingPictureModeSetting, bool *pPendingAudioFocus)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    eIMediaPlayerStatus status;

    if (!IsSessionRegistered(pIMediaPlayerSession) || (pIMediaPlayerSession == NULL))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    status = pIMediaPlayerSession->GetPendingPresentationParams(vidScreenRect, pPendingPictureModeSetting, pPendingAudioFocus);

    unlockplayermutex();

    return status;
}

/*
 IMediaPlayer::IMediaPlayerSession_CommitPresentationParams
 1. Iterate through all sessions & get list of all foreground sessions for which
 presentation params are configured & need to be applied.
 2. Check for conflict (If more than 1 session has audio focus as true or more than 1
 session has audio focus as false)
 3. Based on Set Presentaion Params & Current Pending (Configured) presentation params
 decide whether we are converting PIP into Main or Main into PIP.
 4. If there are 2 sessions (Main to PIP & PIP to Main) then its a swap so follow steps for
 Swap else simply commit presentation params.
 */
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_CommitPresentationParams(void)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    std::list<IMediaPlayerSession *>::iterator iter;
    int totalMainSessionCount = 0;
    int totalPipSessionCount = 0;
    std::list<IMediaPlayerSession *> configuredSessionList;
    bool pip_swap_pending = false, main_swap_pending = false;

    bool currentAudioFocus , pipAudioFocus = 0, mainAudioFocus = 0, badSessionAudioFocus = 0;
    bool currentPictureMode , pipPictureMode = 0, mainPictureMode = 0, badSessionPictureMode = 0;
    tAvRect currentScreenRect , pipScreenRect, mainScreenRect, badSessionScreenRect;
    tComponentInfo info[3];
    uint32_t infoSize = 3;
    uint32_t offset = 0;
    uint32_t count;

    IMediaPlayerSession *mainToPipSession = NULL;
    IMediaPlayerSession *pipToMainSession = NULL;
    IMediaPlayerSession *BadSession = NULL;
    std::string SrcUrl;
    MediaControllerClassFactory::eControllerType ControllerType = MediaControllerClassFactory::eControllerTypeUnknown;

    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    /* 1. Iterate through all sessions & get list of all foreground sessions for which
          presentation params are configured & need to be applied.
          Based on Set Presentaion Params & Current Pending (Configured) presentation params
          decide whether we are converting PIP into Main or Main into PIP.  */
    for (iter = mSessionList.begin(); iter != mSessionList.end(); iter++)
    {
        IMediaPlayerSession *currentSession = *iter;
        SrcUrl = currentSession->GetServiceUrl();
        ControllerType = MediaControllerClassFactory::GetControllerType(SrcUrl);
        LOG(DLOGL_NOISE, "Src url is %s and Controller type = %d", SrcUrl.c_str(), ControllerType);
        if (currentSession->getMediaController() && !(currentSession->getMediaController()->isBackground()) && (ControllerType != MediaControllerClassFactory::eControllerTypeAudio))
        {
            tAvRect pendingScreenRect;
            bool  pendingAudioFocus;
            bool  pendingPictureMode;

            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Session %p is a foreground Session", currentSession);

            currentSession->GetPendingPresentationParams(&pendingScreenRect, &pendingPictureMode, &pendingAudioFocus);


            currentSession->GetPresentationParams(&currentScreenRect, &currentPictureMode, &currentAudioFocus);

            LOG(DLOGL_NOISE, " For session %p ,pending audio focus is %d, current audio focus is %d", currentSession, pendingAudioFocus, currentAudioFocus);


            if (pendingScreenRect.width == 0 || pendingScreenRect.height == 0)
            {
                LOG(DLOGL_NOISE, "No pending params for session: %p  currentAudioFocus: %d", currentSession, currentAudioFocus);

                // For tracking audio focus, the "pending" audio focus will be same as the current audio focus
                pendingAudioFocus = currentAudioFocus;
            }
            else
            {
                configuredSessionList.push_back(currentSession);

                // Determine if this session will PIP/Main swap based on change in audio focus

                //look for bad stream channels handling.
                eIMediaPlayerStatus comp_status = kMediaPlayerStatus_Ok;
                comp_status = currentSession->getMediaController()->GetComponents(info, infoSize, &count, offset);
                if ((comp_status == kMediaPlayerStatus_ContentNotFound) && (count == 0))
                {
                    BadSession = currentSession;
                    badSessionAudioFocus =    pendingAudioFocus;
                    badSessionPictureMode =   pendingPictureMode;
                    badSessionScreenRect =    pendingScreenRect;

                    LOG(DLOGL_NORMAL, "Alert: Bad stream session %p with components count %d", BadSession, count);
                }

                if (pendingAudioFocus != currentAudioFocus)
                {
                    if (currentAudioFocus)    //Current Main session
                    {
                        // Main will have audio focus
                        if (!mainToPipSession)
                        {
                            mainToPipSession = currentSession;            //this should have been Main to PIP
                            pipAudioFocus =    pendingAudioFocus;
                            pipPictureMode =   pendingPictureMode;
                            pipScreenRect =    pendingScreenRect;
                            main_swap_pending = true;

                            LOG(DLOGL_NORMAL, "mainToPipSession: %p", mainToPipSession);
                        }
                        else
                        {
                            LOG(DLOGL_ERROR, "Error: pipToMainSession already set to %p", pipToMainSession);
                        }
                    }
                    else   //Current PIP session
                    {
                        // No audio focus means PIP
                        if (!pipToMainSession)
                        {
                            pipToMainSession = currentSession;       //this should have been PIP to Main
                            mainAudioFocus =   pendingAudioFocus;
                            mainPictureMode =  pendingPictureMode;
                            mainScreenRect =   pendingScreenRect;
                            pip_swap_pending = true;

                            LOG(DLOGL_NORMAL, "pipToMainSession: %p", pipToMainSession);
                        }
                        else
                        {
                            LOG(DLOGL_ERROR, "Error: mainToPipSession already set to %p", mainToPipSession);
                            status =  kMediaPlayerStatus_Error_Unknown;
                        }
                    }
                }   // end if audio focus changes
            } // end pending params for session

            if (pendingAudioFocus)
            {
                ++totalMainSessionCount;
            }
            else
            {
                ++totalPipSessionCount;
            }
        }
        else if ((currentSession->getMediaController()) && (currentSession->getMediaController()->isBackground()))
        {
            dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "Session %p is a Background Session", currentSession);
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Session %p is a foreground audio Session", currentSession);
        }
    }  // end for all sessions

    // Lock mutex for all configured sessions

    for (iter = configuredSessionList.begin(); iter != configuredSessionList.end(); iter++)
    {
        (*iter)->getMediaController()->lockMutex();
    }

    //  Check for conflict (If more than 1 session has audio focus as true or more than 1
    //  session has audio focus as false)

    LOG(DLOGL_NORMAL, "status: %d  totalMainSessionCount: %d  totalPipSessionCount: %d", status, totalMainSessionCount, totalPipSessionCount);

#if (ENABLE_MSPMEDIASHRINK == 1)
    uint32_t pipOn = ((totalMainSessionCount + totalPipSessionCount) == 2) ? 1 : 0;

    // If pipOn is true, it means the box is entering PIP/POP state and the mediashrink needs
    // to be turned off. But if it is false, it means the transition is from PIP/POP "ON" --> "OFF" and
    // in this case the mediashrink should be turned back on after the presentation params have been configured.
    MSPMediaShrink* mshkObj = NULL;
    mshkObj = MSPMediaShrink::getMediaShrinkInstance();

    if (pipOn)
    {
        if (mshkObj)
        {
            mshkObj->PipNotification(pipOn);
        }
    }
#endif

    if (status == kMediaPlayerStatus_Ok && ((totalMainSessionCount < 2 && totalPipSessionCount < 2) || (BadSession)))
    {
        for (iter = configuredSessionList.begin(); iter != configuredSessionList.end(); iter++)
        {
            IMediaPlayerSession *currentSession = *iter;

            if (currentSession != mainToPipSession && currentSession != pipToMainSession)
            {
                currentSession->CommitPresentationParams();
            }
        }

        /*
        If there are 2 sessions (Main to PIP & PIP to Main) then its a swap so follow steps for
        Swap else simply commit presentation params.
        */

        if (mainToPipSession && pipToMainSession)
        {
            // Stop Current video streaming.
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
            CurrentVideoMgr::instance()->CurrentVideo_RemoveLiveSession();
#endif
            // Its a swap
            LOG(DLOGL_NOISE, "swap main and pip");

            mainToPipSession->getMediaController()->CloseDisplaySession();
            pipToMainSession->getMediaController()->CloseDisplaySession();
            LOG(DLOGL_NOISE, "Both sessions closed");

            pipToMainSession->CommitPresentationParams();
            mainToPipSession->CommitPresentationParams();
            LOG(DLOGL_NOISE, "Committed presentation params");

            LOG(DLOGL_NORMAL, "Starting Main Session");
            pipToMainSession->getMediaController()->StartDisplaySession();
            LOG(DLOGL_NORMAL, "Starting PIP Session\n");
            mainToPipSession->getMediaController()->StartDisplaySession();

            LOG(DLOGL_NORMAL, "Setting presentation params for Main session");
            pipToMainSession->getMediaController()->SetPresentationParams(&mainScreenRect, mainPictureMode, mainAudioFocus);

            LOG(DLOGL_NORMAL, "Setting presentation params for PIP session");
            mainToPipSession->getMediaController()->SetPresentationParams(&pipScreenRect, pipPictureMode, pipAudioFocus);
        }
        else
        {
            if (mainToPipSession)
            {
                if (main_swap_pending == true)
                {
                    // Stop Current video streaming.
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
                    CurrentVideoMgr::instance()->CurrentVideo_RemoveLiveSession();
#endif
                    LOG(DLOGL_NORMAL, " Single valid session ,which switches from  Main to PIP");

                    if (BadSession)
                    {
                        LOG(DLOGL_NORMAL, "Current PIP session is bad stream. closing it and restarting as MAIN anyway");
                        BadSession->getMediaController()->CloseDisplaySession();
                    }
                    mainToPipSession->getMediaController()->CloseDisplaySession();
                    LOG(DLOGL_NOISE, "Commit mainToPipSession");

                    mainToPipSession->CommitPresentationParams();
                    mainToPipSession->getMediaController()->StartDisplaySession();
                    mainToPipSession->getMediaController()->SetPresentationParams(&pipScreenRect, pipPictureMode, pipAudioFocus);

                    if (BadSession)
                    {
                        badSessionAudioFocus = 1;
                        BadSession->CommitPresentationParams();
                        BadSession->getMediaController()->StartDisplaySession();
                        BadSession->getMediaController()->SetPresentationParams(&badSessionScreenRect, badSessionPictureMode, badSessionAudioFocus);
                    }
                }

            }

            if (pipToMainSession)
            {
                if (pip_swap_pending == true)
                {
                    LOG(DLOGL_NORMAL, " Single valid session ,which switches from PIP to Main");

                    if (BadSession)  //Assumption: In case of two bad stream sessions,swap may not happen,as both of them will fails to switch their audio focus.so, two bad
                    {
                        LOG(DLOGL_NORMAL, "Current Main session is bad stream. closing it and restarting as PIP anyway");
                        BadSession->getMediaController()->CloseDisplaySession();
                    }

                    pipToMainSession->getMediaController()->CloseDisplaySession();

                    LOG(DLOGL_NOISE, "Commit pipToMainSession");
                    pipToMainSession->CommitPresentationParams();

                    pipToMainSession->getMediaController()->StartDisplaySession();
                    pipToMainSession->getMediaController()->SetPresentationParams(&mainScreenRect, mainPictureMode, mainAudioFocus);

                    if (BadSession)
                    {
                        badSessionAudioFocus = 0;
                        BadSession->CommitPresentationParams();
                        BadSession->getMediaController()->StartDisplaySession();
                        BadSession->getMediaController()->SetPresentationParams(&badSessionScreenRect, badSessionPictureMode, badSessionAudioFocus);
                    }
                }

            }
        }

        status =  kMediaPlayerStatus_Ok;
#if (ENABLE_MSPMEDIASHRINK == 1)
        //Turn back medisahrink on
        if (!pipOn)
        {
            if (mshkObj)
            {
                mshkObj->PipNotification(pipOn);
            }
        }
#endif
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: Conflict in configured params - clearing all pending params");
#if 0
        for (iter = configuredSessionList.begin(); iter != configuredSessionList.end(); iter++)
        {
            IMediaPlayerSession *currentSession = *iter;
            tAvRect pendingScreenRect;
            pendingScreenRect.x = 0;
            pendingScreenRect.y = 0;
            pendingScreenRect.width = 0;
            pendingScreenRect.height = 0;
            currentSession->ConfigurePresentationParams(&pendingScreenRect, 0 , 0);
        }
#endif
        status =  kMediaPlayerStatus_Error_Unknown;
    }

    for (iter = configuredSessionList.begin(); iter != configuredSessionList.end(); iter++)
    {
        (*iter)->getMediaController()->unLockMutex();
    }

    unlockplayermutex();

    return status;
}




///This function is used to create an IP Bandwidth Gauge display.
eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_IpBwGauge(IMediaPlayerSession *pIMediaPlayerSession,
        const int8_t *sTryServiceUrl, uint32_t *pMaxBwProvision, uint32_t *pTryServiceBw, uint32_t *pTotalBwConsumption)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    if (getIterator(pIMediaPlayerSession) == mSessionList.end() || (pIMediaPlayerSession == NULL))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }
    if (pIMediaPlayerSession->getMediaController())
    {
        pIMediaPlayerSession->getMediaController()->lockMutex();
        status = pIMediaPlayerSession->getMediaController()->IpBwGauge((const char *) sTryServiceUrl, pMaxBwProvision,
                 pTryServiceBw, pTotalBwConsumption);
        pIMediaPlayerSession->getMediaController()->unLockMutex();
    }
    else
    {
        status = kMediaPlayerStatus_Error_OutOfState;
    }

    unlockplayermutex();

    return status;
}

eIMediaPlayerStatus IMediaPlayer::updateCCI()
{
    FNLOG(DL_MSP_MPLAYER);
    std::list<IMediaPlayerSession *>::iterator iter;
    CCIData cciData;
    uint8_t masterCCIbyte = 0;
    cciData.emi = 0;
    cciData.aps = 0;
    cciData.cit = 0;
    bool isSocEnabled = false;

    for (iter = mSessionList.begin(); iter != mSessionList.end(); iter++)
    {
        LOG(DLOGL_REALLY_NOISY, " Session id is  %p", *iter);;
        if (*iter && (*iter)->getMediaController())
        {
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
            if (!(*iter)->getMediaController()->isBackground())

#endif
            {
                // Search for most restrictive values.
                CCIData sessionCCIData;
                sessionCCIData.emi = 0;
                sessionCCIData.aps = 0;
                sessionCCIData.cit = 0;

                (*iter)->GetCCI(sessionCCIData);
                LOG(DLOGL_NOISE, "Session's CCI bits: emi =  %u, aps = %u,cit = %u", sessionCCIData.emi, sessionCCIData.aps, sessionCCIData.cit);
                //check if SOC enabled in CCI byte
                if (sessionCCIData.emi == COPY_NO_MORE)
                {
                    LOG(DLOGL_SIGNIFICANT_EVENT, "SOC IS Enabled in Imediaplayer");;
                    isSocEnabled = true;
                }

                if ((sessionCCIData.emi == COPY_ONCE) && (*iter)->getMediaController()->isRecordingPlayback())
                {
                    LOG(DLOGL_SIGNIFICANT_EVENT, "CCI Bit is COPY_ONCE for recording playblack,so COPY_NO_MORE bit is set");;
                    cciData.emi = COPY_NO_MORE;
                }
                else if (cciData.emi < sessionCCIData.emi)
                {
                    cciData.emi = sessionCCIData.emi;
                }
                if (cciData.aps < sessionCCIData.aps)
                {
                    cciData.aps = sessionCCIData.aps;
                }
                if (cciData.cit < sessionCCIData.cit)
                {
                    cciData.cit = sessionCCIData.cit;
                }
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "getController is NULL and Failed");
        }
    }


    Avpm *inst = Avpm::getAvpmInstance();
    masterCCIbyte = masterCCIbyte | (cciData.emi);
    masterCCIbyte = masterCCIbyte | (cciData.aps) << 2;
    masterCCIbyte = masterCCIbyte | (cciData.cit) << 4;
    LOG(DLOGL_REALLY_NOISY, "Master CCI byte  = %d ,EMI = %d ,APS = %d, CIT = %d", masterCCIbyte, cciData.emi, cciData.aps, cciData.cit);
    if (inst)
        inst->SetCCIBits(masterCCIbyte, isSocEnabled);

    return kMediaPlayerStatus_Ok;
}

eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_SetApplicationDataPid(IMediaPlayerSession *pIMediaPlayerSession,
        uint32_t pid)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    eIMediaPlayerStatus status;
    LOG(DLOGL_NORMAL, "SETTING APPLICATION PID=%d ", pid);
    if (getIterator(pIMediaPlayerSession) == mSessionList.end() || (pIMediaPlayerSession == NULL))
    {
        LOG(DLOGL_ERROR, "SETTING APPLICATION PID=%d EARLY RETURN (UNKNOWN SESSION)", pid);
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }
    IMediaController *controller = pIMediaPlayerSession->getMediaController();
    if (controller)
    {
        controller->lockMutex();
        status = controller->SetApplicationDataPid(pid);
        controller->unLockMutex();
    }
    else
    {
        LOG(DLOGL_ERROR, "SETTING APPLICATION PID=%d (OUT OF STATE)", pid);
        status = kMediaPlayerStatus_Error_OutOfState;
    }

    unlockplayermutex();

    return status;
}

eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_SetApplicationDataPidExt(IMediaPlayerSession *pIMediaPlayerSession,
        IMediaPlayerStatusCallback eventStatusCB, void *pEventStatusCBClientContext, uint32_t pid, const tIMediaPlayerSfltFilterGroup *sfltGroup,
        IMediaPlayerClientSession **ppClientSession)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    eIMediaPlayerStatus status;
    LOG(DLOGL_NORMAL, "SETTING APPLICATION PID=%d ", pid);

    if (getIterator(pIMediaPlayerSession) == mSessionList.end() || (pIMediaPlayerSession == NULL))
    {
        unlockplayermutex();
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
                unlockplayermutex();
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
    }

    //delete and remove the clientsession from the list.
    if (pid == INVALID_PID_VALUE)
    {
        controller->lockMutex();
        pIMediaPlayerSession->deleteClientSession(*ppClientSession);
        controller->unLockMutex();
    }

    unlockplayermutex();

    return status;
}

eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_GetApplicationData(IMediaPlayerSession *pIMediaPlayerSession,
        uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    FNLOG(DL_MSP_MPLAYER);

    eIMediaPlayerStatus status;

    if (getIterator(pIMediaPlayerSession) == mSessionList.end() || (pIMediaPlayerSession == NULL))
    {
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    IMediaController *controller = pIMediaPlayerSession->getMediaController();
    if (controller)
    {
        controller->lockMutex();
        status = controller->GetApplicationData(bufferSize, buffer, dataSize);
        controller->unLockMutex();
    }
    else
    {
        status = kMediaPlayerStatus_Error_OutOfState;
    }

    return status;
}

eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_GetApplicationDataExt(IMediaPlayerClientSession * pClientSession,
        IMediaPlayerSession *pIMediaPlayerSession, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;
    if (getIterator(pIMediaPlayerSession) == mSessionList.end() || (pIMediaPlayerSession == NULL))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }

    //If ClientSession does not exists, don't call getApplicationDataExt
    if (pClientSession == NULL)
    {
        unlockplayermutex();
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
    }

    unlockplayermutex();

    return status;
}

eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_SetAudioPid(IMediaPlayerSession *pIMediaPlayerSession, uint32_t pid)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    eIMediaPlayerStatus status;
    if (getIterator(pIMediaPlayerSession) == mSessionList.end() || (pIMediaPlayerSession == NULL))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }
    IMediaController *controller = pIMediaPlayerSession->getMediaController();
    if (controller)
    {
        controller->lockMutex();
        status = controller->SetAudioPid(pid);
        controller->unLockMutex();
    }
    else
    {
        status = kMediaPlayerStatus_Error_OutOfState;
    }

    unlockplayermutex();

    return status;
}

eIMediaPlayerStatus IMediaPlayer::IMediaPlayerSession_GetComponents(IMediaPlayerSession *pIMediaPlayerSession,
        tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset)
{
    FNLOG(DL_MSP_MPLAYER);

    lockplayermutex();

    eIMediaPlayerStatus status;
    if (getIterator(pIMediaPlayerSession) == mSessionList.end() || (pIMediaPlayerSession == NULL))
    {
        unlockplayermutex();
        return kMediaPlayerStatus_Error_UnknownSession;
    }
    IMediaController *controller = pIMediaPlayerSession->getMediaController();
    if (controller)
    {
        controller->lockMutex();
        status = controller->GetComponents(info, infoSize, count, offset);
        controller->unLockMutex();
    }
    else
    {
        status = kMediaPlayerStatus_Error_OutOfState;
    }

    unlockplayermutex();

    return status;
}

void* IMediaPlayer::eventThreadFunc(void *data)
{
    bool done = false;

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Event thread MediaPlayer");

    while (!done)
    {
        IMediaPlayer *inst = (IMediaPlayer *) data;
        if (inst == NULL)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Media Player instance is not yet created... Out Of State...");
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

        inst->lockplayermutex();
        switch (evt->eventType)
        {

        case kMediaPlayerEventCCIUpdated:
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "kMediaPlayerEventCCIUpdated event received...\n");
            tMediaPlayerSessionCciData *pCciData = (tMediaPlayerSessionCciData *) evt->eventData;
            if (pCciData == NULL)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Null data. Ignoring the CCI updated event");
                break;
            }

            inst->ProcessCCIUpdated(pCciData->pData, pCciData->CCIbyte);

            delete pCciData;
            pCciData = NULL;
        }
        break;

        case kMediaPlayerEventStopPrimAudioAndStartEas:
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "kMediaPlayerEventStopPrimAudioAndStartEas event received...\n");
            IMediaPlayerSession *pIMediaPlayerSession = (IMediaPlayerSession *) evt->eventData;
            if (pIMediaPlayerSession == NULL)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Null data. Ignoring the StopPrimAudioAndStartEas event");
                break;
            }

            inst->ProcessStopInFocusAudioAndStartEasAudio(pIMediaPlayerSession);
        }
        break;

        case kMediaPlayerEventStartPrimAudio:
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "kMediaPlayerEventStartPrimAudio event received...\n");
            inst->ProcessRestartInFocusAudio();
        }
        break;

        case kMediaPlayerEventThreadExit:
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "kMediaPlayerEventThreadExit event received...\n");
            done = true;
        }
        break;
        default:
            break;
        }
        inst->threadEventQueue->freeEvent(evt);
        inst->unlockplayermutex();
    }

    pthread_exit(NULL);
    return NULL;
}

eIMediaPlayerStatus IMediaPlayer::queueEvent(eMediaPlayerEvent evtp, void* pData)
{
    if (!threadEventQueue)
    {
        LOG(DLOGL_ERROR, "warning: no queue to dispatch event %d", evtp);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    threadEventQueue->dispatchEvent(evtp, pData);
    return kMediaPlayerStatus_Ok;
}

// Posts the event kMediaPlayerEventCCIUpdated to the media player thread
// for one of the active media player session
// pData - pointer to the media player session for which CCI update received
// CCIbyte - CCI byte received
void IMediaPlayer::CCIUpdated(void *pData, uint8_t CCIbyte)
{
    FNLOG(DL_MSP_ZAPPER);
    IMediaPlayer *instance = IMediaPlayer::getMediaPlayerInstance() ;
    if (instance)
    {
        tMediaPlayerSessionCciData *payLoadData = new tMediaPlayerSessionCciData();
        if (payLoadData == NULL)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Memory allocation failed... Not processing CCIUpdated event...");
            return;
        }

        payLoadData->pData = pData;
        payLoadData->CCIbyte = CCIbyte;

        eIMediaPlayerStatus status = instance->queueEvent(kMediaPlayerEventCCIUpdated, (void *) payLoadData);
        if (status != kMediaPlayerStatus_Ok)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Error in queuing event");
        }
    }
}

// Applies the CCI value to the media player session for which update is received
// Also for all the remaining sessions if the current CCI is more restrictive,
// it will be applied for remaining sessions too.
// pData - pointer to the media player session for which CCI update received
// CCIbyte - CCI byte received
void IMediaPlayer::ProcessCCIUpdated(void *pData, uint8_t CCIbyte)
{
    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "IMediaPlayer::CCI Updated is called for the session %p with CCI value ------> %d\n", pData, CCIbyte);
    if (pData)
    {
        lockplayermutex();

        IMediaPlayerSession *session = (IMediaPlayerSession *) pData;

        if (!IsSessionRegistered(session))
        {
            unlockplayermutex();
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "IMediaPlayer::CCI Updates received for an unknown session\n");
            return;
        }

        if (session->getMediaController())
        {
            session->SetCCI(CCIbyte);

#if PLATFORM_NAME == G8  // current video only apply for G8, not G6
            // Set CCI for current video.
            if (session->HasAudioFocus())
            {
                LOG(DLOGL_NOISE, "Setting CCI bit to Current Video.");
                CCIData cciData;
                session->GetCCI(cciData);

                CurrentVideoMgr::instance()->CurrentVideo_SetCCIAttribute(cciData);
                //check if SOC enabled in CCI byte
                if (cciData.emi == COPY_NO_MORE)
                {
                    bool isSocEnabled = true;
                    CurrentVideoMgr::instance()->CurrentVideo_SetOutputAttribute(&isSocEnabled);

                }
            }
            else
            {
                LOG(DLOGL_NOISE, "Not setting CCI bit to Current Video with no audio focus");
            }

            if (!(session->getMediaController()->isBackground()))
            {
                // only apply CCI when it is foreground
                LOG(DLOGL_NOISE, "Session is Foreground,so  updating the ports with the cci value");
                updateCCI();

            }
            else
            {
                LOG(DLOGL_NORMAL, "Session is Background,so  not updating the ports with the cci value");
            }
#endif

#if PLATFORM_NAME == G6
            if (!(session->getMediaController()->isBackground()))
            {
                LOG(DLOGL_NORMAL, "Session is Foreground,so  updating the ports with the cci value");
                updateCCI();

            }
            else
            {
                LOG(DLOGL_NORMAL, " Session is Background, so not updating the ports with the CCI value");
            }
#endif

#if PLATFORM_NAME == IP_CLIENT
            updateCCI();
#endif
        }

        unlockplayermutex();
    }
    else
    {
        LOG(DLOGL_ERROR, "can not retrieve session data as we received invalid user data");
    }
}


bool IMediaPlayer::IsSessionRegistered(IMediaPlayerSession* pIMediaPlayerSession)
{
    if (getIterator(pIMediaPlayerSession) == mSessionList.end())
    {
        LOG(DLOGL_ERROR, "session %p not in list", pIMediaPlayerSession);
        return false;
    }

    return true;
}

const char* IMediaPlayer::GetServiceUrlWithAudioFocus()
{
    FNLOG(DL_MSP_MPLAYER);
    std::list<IMediaPlayerSession *>::iterator iter;

    lockplayermutex();

    for (iter = mSessionList.begin(); iter != mSessionList.end(); iter++)
    {
        IMediaPlayerSession *session = *iter;
        if (session->HasAudioFocus())
        {
            string serviceUrlWithAudioFocus = session->GetServiceUrl();
            LOG(DLOGL_ERROR, "session %p has audio focus", session);

            unlockplayermutex();

            return serviceUrlWithAudioFocus.c_str();
        }
    }

    unlockplayermutex();

    // no session with audio focus found
    LOG(DLOGL_NORMAL, "warning - no session has audio focus");
    return NULL;
}

// Posts the event kMediaPlayerEventStopPrimAudioAndStartEas to the media player thread
// pIMediaPlayerSession - pointer to the EAS audio playback session
void IMediaPlayer::StopInFocusAudioAndStartEasAudio(IMediaPlayerSession *pIMediaPlayerSession)
{
    FNLOG(DL_MSP_ZAPPER);
    IMediaPlayer *instance = IMediaPlayer::getMediaPlayerInstance() ;
    if (instance)
    {
        eIMediaPlayerStatus status = instance->queueEvent(kMediaPlayerEventStopPrimAudioAndStartEas, (void *) pIMediaPlayerSession);
        if (status != kMediaPlayerStatus_Ok)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Error in queuing event");
        }
    }
}

// Process the event kMediaPlayerEventStopPrimAudioAndStartEas
// pIMediaPlayerSession - pointer to the EAS audio playback session
// The processing this API does is
// 1 - Set the EAS audio active status to true
// 2 - For the in-progess media playback session with audio, stop audio
// 3 - For EAS audio playback session start the EAS audio playback
// All the above processing happens in the media player thread context
void IMediaPlayer::ProcessStopInFocusAudioAndStartEasAudio(IMediaPlayerSession *pIMediaPlayerSession)
{
    lockplayermutex();

    if (!IsSessionRegistered(pIMediaPlayerSession))
    {
        unlockplayermutex();
        return;
    }

    SetEasAudioActive(true);

    std::list<IMediaPlayerSession *>::iterator iter;

    for (iter = mSessionList.begin(); iter != mSessionList.end(); iter++)
    {
        IMediaPlayerSession *session = *iter;

        if (session->HasAudioFocus())
        {
            session->StopAudio();
            break;
        }
    }

    IMediaController *controller = pIMediaPlayerSession->getMediaController();
    if (controller)
    {
        controller->lockMutex();
        controller->startEasAudio();
        controller->unLockMutex();
    }

    unlockplayermutex();
}

// Posts the event kMediaPlayerEventStartPrimAudio to the media player thread
void IMediaPlayer::RestartInFocusAudio(void)
{
    FNLOG(DL_MSP_ZAPPER);
    IMediaPlayer *instance = IMediaPlayer::getMediaPlayerInstance() ;
    if (instance)
    {
        eIMediaPlayerStatus status = instance->queueEvent(kMediaPlayerEventStartPrimAudio, NULL);
        if (status != kMediaPlayerStatus_Ok)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Error in queuing event");
        }
    }
}

// Process the event kMediaPlayerEventStartPrimAudio
// The processing this API does is
// 2 - Set the EAS audio active status to fasle
// 1 - For the in-progess media playback session with audio focus, restart audio
//     playback that was previously stopped for EAS audio playback
void IMediaPlayer::ProcessRestartInFocusAudio(void)
{
    lockplayermutex();

    SetEasAudioActive(false);

    std::list<IMediaPlayerSession *>::iterator iter;

    for (iter = mSessionList.begin(); iter != mSessionList.end(); iter++)
    {
        IMediaPlayerSession *session = *iter;

        if (session->HasAudioFocus())
        {
            session->RestartAudio();
            break;
        }
    }

    unlockplayermutex();
}

int IMediaPlayer::RegisterMspEventCallback(tCsciMspCallbackFunction callback, tCsciMspEvent type, void *clientContext)
{
    return mspEventCallback.RegisterCallback(callback, type, clientContext);
}

int IMediaPlayer::UnregisterMspEventCallback(tCsciMspCallbackFunction callback)
{
    return mspEventCallback.UnregisterCallback(callback);
}

// return pointer to service URL with audio focus or NULL if none
const char* Csci_MediaPlayer_GetServiceUrlWithAudioFocus()
{
    FNLOG(DL_MSP_MPLAYER);

    IMediaPlayer *player = IMediaPlayer::getMediaPlayerInstance();

    if (player)
    {
        return player->GetServiceUrlWithAudioFocus();
    }
    else
    {
        return NULL;
    }
}

int Csci_MediaPlayer_RegisterCallback(tCsciMspCallbackFunction callback, tCsciMspEvent type, void *clientContext)
{

    FNLOG(DL_MSP_MPLAYER);

    IMediaPlayer *player = IMediaPlayer::getMediaPlayerInstance();

    if (player)
    {
        return player->RegisterMspEventCallback(callback, type, clientContext);
    }
    else
    {
        return -1;
    }
}

// return 0 if success, -1 if callback function not found
int Csci_MediaPlayer_UnregisterCallback(tCsciMspCallbackFunction callback)
{

    FNLOG(DL_MSP_MPLAYER);

    IMediaPlayer *player = IMediaPlayer::getMediaPlayerInstance();

    if (player)
    {
        return player->UnregisterMspEventCallback(callback);
    }
    else
    {
        return -1;
    }
}

eCsciMspDiagStatus IMediaPlayer::GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo)
{
    FNLOG(DL_MSP_MPLAYER);
    eCsciMspDiagStatus status = kCsciMspDiagStat_NoData;

    std::list<IMediaPlayerSession *>::iterator iter;
    IMediaPlayer *mediaplayer = IMediaPlayer::getMediaPlayerInstance();
    if (mediaplayer)
    {
        mediaplayer->lockplayermutex();
        for (iter = mSessionList.begin(); iter != mSessionList.end(); iter++)
        {
            IMediaPlayerSession *currentSession = *iter;
            if (currentSession && currentSession->mInFocus) // session is active
            {
                LOG(DLOGL_REALLY_NOISY, "Session %p with focus value %d", currentSession, currentSession->mInFocus);
                IMediaController *controller = currentSession->getMediaController();
                if (controller)
                {
                    controller->lockMutex();
                    status = controller->GetMspNetworkInfo(msgInfo);
                    controller->unLockMutex();
                    LOG(DLOGL_REALLY_NOISY, "Session %p, channel no %d, source id %d frequency %d tuning mode %d",
                        currentSession, msgInfo->ChanNo, msgInfo->SourceId, msgInfo->frequency, msgInfo->mode);
                }
                break;
            }
        }
        mediaplayer->unlockplayermutex();
    }
    else
    {
        LOG(DLOGL_ERROR, "getMediaPlayerInstance returns NULL %s", __func__);
    }

    return status;
}

/********************************************************************************
 *
 *	Function:	SetEasAudioActive
 *
 *	Purpose:	In mediaservices, refer to audioPlayer.cpp.  AudioPlayer is the
 *				component which controls EAS audio playback.  AudioPlayer uses
 *				this routine to communicate if EAS audio is active/inactive.
 *
 *	Parameters:	active - if true, EAS audio is currently active
 *
 */

void IMediaPlayer::SetEasAudioActive(bool active)
{
    lockplayermutex();

    dlog(DL_SIG_EAS, DLOGL_NORMAL, "%s(), active = %d", __FUNCTION__, active);

    mEasAudioActive = active;

    std::list<IMediaPlayerSession *>::iterator iter;
    for (iter = mSessionList.begin(); iter != mSessionList.end(); iter++)
    {
        IMediaController *controller = (*iter)->getMediaController();
        if (controller)
        {
            controller->lockMutex();
            controller->SetEasAudioActive(mEasAudioActive);
            controller->unLockMutex();
        }
    }

    unlockplayermutex();
}

/********************************************************************************
 *
 *	Function:	IsEasAudioActive
 *
 *	Purpose:	An accessor which indicates if EAS audio is currently active.
 *				Currently, there is only one audio decoder in the STB.  As a
 *				result, the code has to be careful how it uses the decoder when
 *				EAS audio is active.  In mediaservices, refer to DisplaySession.cpp
 *				to see how this routine is used.
 *
 *	Returns:	Boolean.  If true, EAS audio is currently active
 *
 */

bool IMediaPlayer::IsEasAudioActive(void)
{
    dlog(DL_SIG_EAS, DLOGL_NORMAL, "%s(), active = %d", __FUNCTION__, mEasAudioActive);
    return mEasAudioActive;
}

void IMediaPlayer::SignalAudioFocusChange(bool active)
{
    dlog(DL_SIG_EAS, DLOGL_NORMAL, "%s(), active = %d", __FUNCTION__, active);

    // if audio focus has changed to active state
    if (active)
    {
        mspEventCallback.ProcessEvents();
    }
}

eCsciMspDiagStatus Csci_Diag_GetMspNetworkInfo(DiagMspNetworkInfo *diagInfo)
{
    FNLOG(DL_MSP_MPLAYER);

    eCsciMspDiagStatus status = kCsciMspDiagStat_OK;

    if (diagInfo)
    {
        status = IMediaPlayer::GetMspNetworkInfo(diagInfo);
    }
    else
    {
        LOG(DLOGL_ERROR, "NULL diag info pointer from Diag module to MSP");
        status = kCsciMspDiagStat_InvalidInput;
    }

    return status;
}

eCsciMspDiagStatus IMediaPlayer::GetMspCopyProtectionInfo(DiagCCIData *msgCCIInfo)
{
    FNLOG(DL_MSP_MPLAYER);
    std::list<IMediaPlayerSession *>::iterator iter;
    std::string source, destination;
    char srcstring[7] =
    { '\0' };

    memset(msgCCIInfo->SrcStr, 0, sizeof(msgCCIInfo->SrcStr));
    memset(msgCCIInfo->DestStr, 0, sizeof(msgCCIInfo->DestStr));
    int sesscount;

    IMediaPlayer *mediaplayer = IMediaPlayer::getMediaPlayerInstance();
    if (mediaplayer)
    {
        mediaplayer->lockplayermutex();

        for (iter = mSessionList.begin(), sesscount = 0; iter != mSessionList.end(); iter++, sesscount++)
        {
            LOG(DLOGL_REALLY_NOISY, " Number of session = %d", sesscount + 1);
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
#if PLATFORM_NAME == IP_CLIENT
                    strncpy(msgCCIInfo[sesscount].SrcStr, "Gateway", sizeof(msgCCIInfo[sesscount].SrcStr));
                    strncpy(msgCCIInfo[sesscount].DestStr, "Video Output", sizeof(msgCCIInfo[sesscount].DestStr));
#endif
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
                    strncpy(msgCCIInfo[sesscount].SrcStr, "RF", sizeof(msgCCIInfo[sesscount].SrcStr));

                    if (destination != "")  // go to output
                    {
                        strncpy(msgCCIInfo[sesscount].DestStr, "Video Output", sizeof(msgCCIInfo[sesscount].DestStr));
                    }
                    else // go to recording
                    {
                        strncpy(msgCCIInfo[sesscount].DestStr, "Disk", sizeof(msgCCIInfo[sesscount].DestStr));
                    }
#endif

                }
                else if (strncmp("sadvr", srcstring, sizeof(srcstring) - 2) == 0)
                {
                    strncpy(msgCCIInfo[sesscount].SrcStr, "Disk", sizeof(msgCCIInfo[sesscount].SrcStr));
                    strncpy(msgCCIInfo[sesscount].DestStr, "Video Output", sizeof(msgCCIInfo[sesscount].DestStr));
                }

                else if (strncmp("mrdvr", srcstring, sizeof(srcstring) - 2) == 0)
                {
#if PLATFORM_NAME == IP_CLIENT
                    strncpy(msgCCIInfo[sesscount].SrcStr, "Gateway", sizeof(msgCCIInfo[sesscount].SrcStr));
                    strncpy(msgCCIInfo[sesscount].DestStr, "Video Output", sizeof(msgCCIInfo[sesscount].DestStr));
#endif
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
                    strncpy(msgCCIInfo[sesscount].SrcStr, "In Home Net", sizeof(msgCCIInfo[sesscount].SrcStr));
                    strncpy(msgCCIInfo[sesscount].DestStr, "Video Output", sizeof(msgCCIInfo[sesscount].DestStr));
#endif
                }
#if PLATFORM_NAME == G8
                else if (source.find("avfs://item=live") == 0 ||
                         source.find("avfs://item=vod") == 0 ||
                         source.find("avfs://item=sappv") == 0)
                {
                    strncpy(msgCCIInfo[sesscount].SrcStr, "RF", sizeof(msgCCIInfo[sesscount].SrcStr));
                    strncpy(msgCCIInfo[sesscount].DestStr, "In Home Net", sizeof(msgCCIInfo[sesscount].DestStr));
                }
#endif
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
                else if (source.find(MRDVR_REC_STREAMING_URL) == 0)   // MRDVR streaming to IP client,"svfs://"
                {
                    strncpy(msgCCIInfo[sesscount].SrcStr, "Disk", sizeof(msgCCIInfo[sesscount].SrcStr));
                    strncpy(msgCCIInfo[sesscount].DestStr, "In Home Net", sizeof(msgCCIInfo[sesscount].DestStr));
                }
#endif
                else   //on demand video
                {
#if PLATFORM_NAME == IP_CLIENT
                    strncpy(msgCCIInfo[sesscount].SrcStr, "Gateway", sizeof(msgCCIInfo[sesscount].SrcStr));
#endif
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
                    strncpy(msgCCIInfo[sesscount].SrcStr, "RF", sizeof(msgCCIInfo[sesscount].SrcStr));
#endif
                    strncpy(msgCCIInfo[sesscount].DestStr, "Video Output", sizeof(msgCCIInfo[sesscount].DestStr));
                }

                // For diag page, just return the original CCI byte saved for each session
                // Fetch CCI data, do not compare which one is more restrictive, which is done when applying to AVPM
                (*iter)->GetCCI(sessionCCIData);

                msgCCIInfo[sesscount].emi = sessionCCIData.emi;

                msgCCIInfo[sesscount].aps = sessionCCIData.aps;

                msgCCIInfo[sesscount].cit = sessionCCIData.cit;

                msgCCIInfo[sesscount].rct = sessionCCIData.rct;

                if (sessionCCIData.emi == 0)
                {
                    if (sessionCCIData.rct == 0)
                    {
                        msgCCIInfo[sesscount].epn = 0;
                    }
                    else
                    {
                        msgCCIInfo[sesscount].epn = 1;
                    }
                }
                LOG(
                    DLOGL_REALLY_NOISY,
                    "Session %p,  cit %d, aps %d emi %d  rct %d epn %d",
                    currentSession, msgCCIInfo[sesscount].cit, msgCCIInfo[sesscount].aps, msgCCIInfo[sesscount].emi, msgCCIInfo[sesscount].rct, msgCCIInfo[sesscount].epn);
            }
            LOG(DLOGL_REALLY_NOISY, "Source  %s, Destination %s", msgCCIInfo[sesscount].SrcStr, msgCCIInfo[sesscount].DestStr);
        }
        LOG(DLOGL_NORMAL, " TOTAL Number of local session = %d", sesscount);


#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
        IMediaStreamer *streamer = NULL;
        streamer = IMediaStreamer::getMediaStreamerInstance();
        if (streamer)
        {
            streamer->GetMspCopyProtectionInfo(msgCCIInfo, &sesscount);
        }
        else
        {
            LOG(DLOGL_ERROR, "Streamer instance is NULL ");
        }
#endif

        mediaplayer->unlockplayermutex();
    }
    return kCsciMspDiagStat_OK;
}

/******************************************************************************
 * Description:
 *  This function returns the number of audio and video components available in
 *  the stream currently played. Also it gives the information about each of
 *  the audio and video components available in the stream.
 *
 * Parameters:
 *  pNumOfComponents     	handle to a conversion session
 *  ppDiagComponentsInfo    audio and video components info available in stream
 *
 * Returns:
 *  kCsciMspDiagStat_OK on success
 *  kCsciMspDiagStat_NoData on failure
 *****************************************************************************/
eCsciMspDiagStatus IMediaPlayer::GetMspComponentInfo(uint32_t *pNumOfComponents, DiagComponentsInfo_t **ppDiagComponentsInfo)
{
    FNLOG(DL_MSP_MPLAYER);

    eCsciMspDiagStatus status = kCsciMspDiagStat_NoData;

    std::list<IMediaPlayerSession *>::iterator iter;

    IMediaPlayer *mediaplayer = IMediaPlayer::getMediaPlayerInstance();
    if (mediaplayer)
    {
        mediaplayer->lockplayermutex();

        for (iter = mSessionList.begin(); iter != mSessionList.end(); iter++)
        {
            IMediaPlayerSession *currentSession = *iter;

            if (currentSession && currentSession->mInFocus) // session is active
            {
                LOG(DLOGL_REALLY_NOISY, "Session %p with focus value %d", currentSession, currentSession->mInFocus);
                IMediaController *controller = currentSession->getMediaController();
                if (controller)
                {
                    controller->lockMutex();

                    uint32_t total = 0; 		//total number of components
                    uint32_t comp_count = 0;	//component count received in current iteration

                    DiagComponentsInfo_t *pDiagComponentsInfo = NULL;   // components list

                    // first get all components just to count them
                    // in a loop, each time or iteration we will query 3 components
                    // will exit loop when the current iteration returns less than 3 components
                    do
                    {
                        tComponentInfo comp_info[COMP_CHUNKSIZE];

                        //Get the next 3 components available in the stream
                        if (controller->GetComponents(comp_info, COMP_CHUNKSIZE, &comp_count, total) == kMediaPlayerStatus_Ok)
                        {
                            total += comp_count;
                        }
                    }
                    while (comp_count == COMP_CHUNKSIZE);  /* call again for the next COMP_CHUNKSIZE of components */

                    if (total) //Checking the number of components available
                    {
                        //Allocating enough memory to hold the component info
                        pDiagComponentsInfo = new DiagComponentsInfo_t[total];
                        if (pDiagComponentsInfo)
                        {
                            //Get the information about all the components available once
                            if (controller->GetComponents((tComponentInfo *) pDiagComponentsInfo, total, &comp_count, 0) != kMediaPlayerStatus_Ok)
                            {
                                LOG(DLOGL_ERROR, "%s: Error: GetComponents FAILED !!!", __FUNCTION__);
                                delete [] pDiagComponentsInfo;
                                pDiagComponentsInfo = NULL;
                            }
                            else
                            {
                                //Populating the number of components and component info out params
                                *ppDiagComponentsInfo = pDiagComponentsInfo;
                                *pNumOfComponents = total;

                                status = kCsciMspDiagStat_OK;
                            }
                        }
                        else
                        {
                            LOG(DLOGL_ERROR, "%s: Error: pDiagComponentsInfo allocation  FAILED !!!", __FUNCTION__);
                        }
                    }

                    controller->unLockMutex();
                }
                break;
            }
        }
        mediaplayer->unlockplayermutex();
    }


    return status;
}

#if PLATFORM_NAME == IP_CLIENT
/******************************************************************************
 * Description:
 *  This function returns the information about the current/active streaming
 * that is happening from G8 gateway to IP client.
 *
 * Parameters:
 *  streamingInfo     	information about the current streaming
 *
 * Returns:
 *  kCsciMspDiagStat_OK on success
 *  kCsciMspDiagStat_NoData on failure
 *  kCsciMspDiagStat_InvalidInput on invalid arguments passed
 *****************************************************************************/
eCsciMspDiagStatus IMediaPlayer::GetMspStreamingInfo(DiagMspStreamingInfo *streamingInfo)
{
    FNLOG(DL_MSP_MPLAYER);

    eCsciMspDiagStatus status = kCsciMspDiagStat_NoData;

    std::list<IMediaPlayerSession *>::iterator iter;
    IMediaPlayer *mediaplayer = IMediaPlayer::getMediaPlayerInstance();
    if (mediaplayer)
    {
        mediaplayer->lockplayermutex();

        for (iter = mSessionList.begin(); iter != mSessionList.end(); iter++)
        {
            IMediaPlayerSession *currentSession = *iter;

            if (currentSession && currentSession->mInFocus) // session is active
            {
                LOG(DLOGL_REALLY_NOISY, "Session %p with focus value %d", currentSession, currentSession->mInFocus);
                IMediaController *controller = currentSession->getMediaController();
                if (controller)
                {
                    controller->lockMutex();
                    status = controller->GetMspStreamingInfo(streamingInfo);
                    controller->unLockMutex();
                }
                break;
            }
        }

        mediaplayer->unlockplayermutex();
    }

    return status;
}
#endif

eCsciMspDiagStatus Csci_Diag_GetMspCopyProtectionInfo(DiagCCIData *diagCCIInfo)
{
    FNLOG(DL_MSP_MPLAYER);


    eCsciMspDiagStatus status = kCsciMspDiagStat_OK;

    if (diagCCIInfo)
    {
        status = IMediaPlayer::GetMspCopyProtectionInfo(diagCCIInfo);
    }
    else
    {
        LOG(DLOGL_ERROR, "NULL diag info pointer from Diag module to MSP");
        status = kCsciMspDiagStat_InvalidInput;
    }

    return status;
}

eCsciMspDiagStatus IMediaPlayer::GetMspOutputsInfo(DiagOutputsInfo_t *msgOutputsInfo)
{
    FNLOG(DL_MSP_MPLAYER);
    //DVI/HDMI
    msgOutputsInfo[DVI_HDMI].ProtType = eEnabled;
    msgOutputsInfo[DVI_HDMI].bEnabled = true;
    msgOutputsInfo[DVI_HDMI].bConstraind = false;
    strncpy(msgOutputsInfo[DVI_HDMI].Policy, DVI_HDMI_POLICY, sizeof(DVI_HDMI_POLICY));
    LOG(
        DLOGL_REALLY_NOISY,
        "DVI_HDMI Policy %s, cons %d, ena %d, Prot %d",
        msgOutputsInfo[DVI_HDMI].Policy, msgOutputsInfo[DVI_HDMI].bConstraind, msgOutputsInfo[DVI_HDMI].bEnabled, msgOutputsInfo[DVI_HDMI].ProtType);

    //YPrPb
    msgOutputsInfo[YPrPb].ProtType = eEnabled;
    msgOutputsInfo[YPrPb].bEnabled = true;
    msgOutputsInfo[YPrPb].bConstraind = false;
    strncpy(msgOutputsInfo[YPrPb].Policy, YPrPb_POLICY, sizeof(YPrPb_POLICY));
    LOG(
        DLOGL_REALLY_NOISY,
        "YPrPb Policy %s, cons %d, ena %d, Prot %d",
        msgOutputsInfo[YPrPb].Policy, msgOutputsInfo[YPrPb].bConstraind, msgOutputsInfo[YPrPb].bEnabled, msgOutputsInfo[YPrPb].ProtType);

    //1394
#if PLATFORM_NAME == IP_CLIENT
    msgOutputsInfo[Port_1394].ProtType = eNoneType;
    msgOutputsInfo[Port_1394].bEnabled = false;
    msgOutputsInfo[Port_1394].bConstraind = false;
    memset(msgOutputsInfo[Port_1394].Policy, 0, sizeof(PORT1394_POLICY));
#endif
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    msgOutputsInfo[Port_1394].ProtType = eEnabled;
    msgOutputsInfo[Port_1394].bEnabled = true;
    msgOutputsInfo[Port_1394].bConstraind = false;
    strncpy(msgOutputsInfo[Port_1394].Policy, PORT1394_POLICY, sizeof(PORT1394_POLICY));
#endif
    LOG(DLOGL_REALLY_NOISY, " 1394 prot  %d, enabled %d, policy %s ",
        msgOutputsInfo[Port_1394].ProtType, msgOutputsInfo[Port_1394].bEnabled, msgOutputsInfo[Port_1394].Policy);

    //Composite
    msgOutputsInfo[Composite].ProtType = eNoneType;
    //msgOutputsInfo[Composite].bEnabled = true;
    //msgOutputsInfo[Composite].bConstraind = false;
    strncpy(msgOutputsInfo[Composite].Policy, Composite_POLICY, sizeof(Composite_POLICY));
    LOG(DLOGL_REALLY_NOISY, "Composite prot  %d, policy %s",
        msgOutputsInfo[Composite].ProtType, msgOutputsInfo[Composite].Policy);

    //VOD
    //msgOutputsInfo[VOD].ProtType = eEnabled;
    //msgOutputsInfo[VOD].bEnabled = true;
    //msgOutputsInfo[VOD].bConstraind = false;
    strncpy(msgOutputsInfo[VOD].Policy, VOD_POLICY, sizeof(VOD_POLICY));
    LOG(DLOGL_REALLY_NOISY, "Vod Policy  %s ", msgOutputsInfo[VOD].Policy);

    return kCsciMspDiagStat_OK;

}

eCsciMspDiagStatus Csci_Diag_GetMspOutputsInfo(DiagOutputsInfo_t *diagOutputsInfo)
{
    FNLOG(DL_MSP_MPLAYER);

    eCsciMspDiagStatus status = kCsciMspDiagStat_OK;
    if (diagOutputsInfo)
    {
        status = IMediaPlayer::GetMspOutputsInfo(diagOutputsInfo);
    }
    else
    {
        LOG(DLOGL_ERROR, "NULL diag info pointer from Diag module to MSP");
        status = kCsciMspDiagStat_InvalidInput;
    }

    return status;
}

/******************************************************************************
 * Description:
 *  This function returns the number of audio and video components available in
 *  the stream currently played. Also it gives the information about each of
 *  the audio and video components available in the stream.
 *
 * Parameters:
 *  pNumOfComponents     	handle to a conversion session
 *  ppDiagComponentsInfo    audio and video components info available in stream
 *
 * Returns:
 *  kCsciMspDiagStat_OK on success
 *  kCsciMspDiagStat_NoData on failure
 *  kCsciMspDiagStat_InvalidInput on invalid arguments passed
 *****************************************************************************/
eCsciMspDiagStatus Csci_Diag_GetComponentsInfo(uint32_t *pNumOfComponents, DiagComponentsInfo_t **ppDiagComponentsInfo)
{
    FNLOG(DL_MSP_MPLAYER);

    eCsciMspDiagStatus status = kCsciMspDiagStat_OK;
    //checking for valid parameters
    if (ppDiagComponentsInfo && pNumOfComponents)
    {
        status = IMediaPlayer::GetMspComponentInfo(pNumOfComponents, ppDiagComponentsInfo);
    }
    else
    {
        LOG(DLOGL_ERROR, "NULL diag DiagComponentsInfo or NumOfComponents from Diag module to MSP");
        status = kCsciMspDiagStat_InvalidInput;
    }

    return status;
}

#if PLATFORM_NAME == IP_CLIENT
/******************************************************************************
 * Description:
 *  This function returns the information about the current/active streaming
 * that is happening from G8 gateway to IP client.
 *
 * Parameters:
 *  streamingInfo     	information about the current streaming
 *
 * Returns:
 *  kCsciMspDiagStat_OK on success
 *  kCsciMspDiagStat_NoData on failure
 *  kCsciMspDiagStat_InvalidInput on invalid arguments passed
 *****************************************************************************/
eCsciMspDiagStatus Csci_Diag_GetMspStreamingInfo(DiagMspStreamingInfo *streamingInfo)
{
    FNLOG(DL_MSP_MPLAYER);

    eCsciMspDiagStatus status = kCsciMspDiagStat_OK;
    //checking for valid parameters
    if (streamingInfo)
    {
        status = IMediaPlayer::GetMspStreamingInfo(streamingInfo);
    }
    else
    {
        LOG(DLOGL_ERROR, "NULL diag DiagComponentsInfo or NumOfComponents from Diag module to MSP");
        status = kCsciMspDiagStat_InvalidInput;
    }

    return status;
}
#endif

///This method returns total and used number of resources of input resource type
eIMediaPlayerStatus IMediaPlayer::IMediaPlayer_GetResourceUsage(eResourceType type,
        int32_t *cfgQuantity,
        int32_t *used)
{
    FNLOG(DL_MSP_MPLAYER);

    eResMonResourceType resourceType = kResMon_Bandwidth;
    switch (type)
    {
    case kResourceTypeTuner:
        resourceType = kResMon_Tuner;
        break;

    default:
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "UNKNOWN resource type requested %d", type);
        return kMediaPlayerStatus_Error_NotSupported;
        break;
    }

    lockplayermutex();

    eResMonStatus resMonStatus = kResMon_Failure;
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;

    if ((m_bConnected == false) || (m_fd == -1))
    {
        for (int i = 0; i < RESMON_INIT_RETRY_COUNT; i++)
        {
            resMonStatus = ResMon_init(&m_fd);
            if (resMonStatus == kResMon_Ok)
            {
                m_bConnected = true;
                dlog(DL_MSP_MPLAYER, DLOGL_MINOR_DEBUG, "%s:%s:%d ZZOpen call ResMon_init return fd %d", __FILE__, __FUNCTION__, __LINE__, m_fd);
                break;
            }
            else
            {
                sleep(1);
                LOG(DLOGL_ERROR, "Could not connect to ResMon server, retrying...");
            }
        }
    }

    // Make sure we're connected to the ResMon server
    if ((m_bConnected == false) || (m_fd == -1))
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Not connected to ResMon server");
        status = kMediaPlayerStatus_Error_OutOfState;
    }
    // Make sure we have received valid parameters
    else if (cfgQuantity == NULL || used == NULL)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Invalid parameters received");
        status = kMediaPlayerStatus_Error_InvalidParameter;
    }
    else
    {
        resMonStatus = ResMon_usage(m_fd, resourceType, cfgQuantity, used);
        if (resMonStatus != kResMon_Ok)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "ResMon_usage failed status = %d", resMonStatus);
            status = kMediaPlayerStatus_Error_Unknown;
        }

        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "ResMon_usage (Type = %d  ConfigQuantity = %d Used = %d)", type, *cfgQuantity, *used);
    }

    unlockplayermutex();

    return status;
}

