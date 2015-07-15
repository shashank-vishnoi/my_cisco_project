// standard CPE includes
#include <stdio.h>
#include <stdlib.h>
#include "CSailApiScheduler.h"
#include "mrdvrserver.h"
#include "MSPSourceFactory.h"
#include "dlog.h"
#include "pthread_named.h"
#include "pmt.h"
#include "csci-websvcs-gateway-streaming-api.h"
#include "csci-websvcs-gateway-sysmgr-api.h"
#include <assert.h>
#include "CurrentVideoMgr.h"
#include "misc_strlcpy.h"
#include "csci-signaling-api.h"
#include "sail-clm-api.h"
#include "conflict.h"

#if ENABLE_MSPMEDIASHRINK == 1 && PLATFORM_NAME == G8
#include "MSPMediaShrinkInterface.h"
#endif


unsigned int getMaxClient(void);
#define SET 1
#define UNSET 0


#define SCOPELOG(section, scopename)  dlogns::ScopeLog __xscopelog(section, scopename, __FILE__, __LINE__, DLOGL_FUNCTION_CALLS)
#define FNLOG(section)  dlogns::ScopeLog __xscopelog(section, __PRETTY_FUNCTION__, __FILE__, __LINE__, DLOGL_FUNCTION_CALLS)

#ifdef LOG
#error  LOG already defined
#endif
#define LOG(level, msg, args...)  dlog(DL_MSP_MRDVR, level,"MRDvrServer:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#define UNUSED_PARAM(a) (void)a;

#define SRCURL_LEN				1024		//As defined in MDA

const char *gAvfsVodPrefix 		= HN_ONDEMAND_STREAMING_URL;
const char *gAvfsVodLscpPrefix 	= "avfs://item=vod/lscp://";
const char *gDecUrl 			= "decoder://primary";

MRDvrServer *MRDvrServer::instance = NULL;
#if PLATFORM_NAME == G8
static void HandleTunerFreeNotification()
{
    LOG(DLOGL_REALLY_NOISY, "Obtained CallBack For Tuner Free Event..Setting TunerFreeFlag to proceed with the live stream request - session reload/cleanup");
    MRDvrServer *inst = MRDvrServer::getInstance();
    if (inst)
    {
        inst->setTunerFreeFlag(SET);//TunerFreeFlag = 1;
        LOG(DLOGL_REALLY_NOISY, "*** TunerFlag value: %d ***", inst->getTunerFreeFlag());
        inst->Retry();
    }
    else
    {
        LOG(DLOGL_ERROR, "MRDVR Instance Null");
    }
}
#endif
extern "C" uint32_t Csci_Msp_MrdvrSrv_GetSessionID(uint32_t i)
{
    if (MRDvrServer::getHandle() != NULL)
        return MRDvrServer::getHandle()->GetSessionID(i);
    return 0;
}

//API exposed to DVRSCHEDULING module to get the MAC
extern "C" const char * Csci_Msp_MrdvrSrv_GetClientSessionMac(uint32_t sessionID)
{
    if (MRDvrServer::getHandle() != NULL)
        return MRDvrServer::getHandle()->GetClientSessionMac(sessionID);
    return "UNKNOWN";
}

// API exposed to DVRSCHEDULING module to get the position of the sessionID cancelled to remove its equialvent url populated
extern "C" uint32_t Csci_Msp_MrdvrSrv_GetRemovedAssetID()
{
    if (MRDvrServer::getHandle() != NULL)
        return MRDvrServer::getHandle()->GetRemovedAssetID();
    return 0;
}

// API exposed to DVRSCHEDULING module to stop live streaming session that was cancelled via the conflict barker
extern "C" void Csci_Msp_MrdvrSrv_ApplyResolution()
{
    if (MRDvrServer::getHandle() != NULL)
        MRDvrServer::getHandle()->ApplyResolution();
}

extern "C" bool Csci_Msp_MrdvrSrv_NotifyWarning()
{
    if (MRDvrServer::getHandle() != NULL)
        return MRDvrServer::getHandle()->NotifyWarning();
    return false;//MRDVR module not initialized
}

extern "C" bool Csci_Msp_MrdvrSrv_UpdateAsset(const MultiMediaEvent * pCancelledConflictItem, bool isLoser)
{
    if (MRDvrServer::getHandle() != NULL)
        return MRDvrServer::getHandle()->updateAsset(pCancelledConflictItem, isLoser);
    return false;//MRDVR module not initialized
}

extern "C" bool Csci_Msp_MrdvrSrv_NotifyRecordingStop(const char *serviceUrl)
{
    FNLOG(DL_MSP_MRDVR);
    LOG(DLOGL_ERROR, "URL is : %s", serviceUrl);
    IMediaPlayerSession* Session = IMediaPlayerSession_FindSessionFromServiceUrl(serviceUrl);
    if (Session != NULL)
    {
        LOG(DLOGL_NORMAL, " Session is : %p", Session);
        IMediaController *controller = Session->getMediaController();
        if (controller != NULL)
        {
            if (controller->isBackground() == false)
            {
                LOG(DLOGL_NORMAL, "Its a foreground session.. Return True ");
                if (MRDvrServer::getHandle() != NULL)
                {
                    if (MRDvrServer::getHandle()->conflictSessInfo.isConflict == true)
                    {
                        LOG(DLOGL_NORMAL, "Foreground rec was cancelled for IPClient tuner req.. Since tuner will not be freed up, client retry to request for tuner again");
                        MRDvrServer::getHandle()->setConflictStatus(kCsciMspMrdvrConflictStatus_Resolved);
                        MRDvrServer::getHandle()->Retry();
                    }
                }
                return true;
            }
        }
    }
    return false;
}

extern "C" eCsciMspMrdvrConflictStatus Csci_Msp_MrdvrSrv_GetConflictStatus()
{
    if (MRDvrServer::getHandle() != NULL)
        return MRDvrServer::getHandle()->getConflictStatus();
    return kCsciMspMrdvrConflictStatus_Finalized;
}

extern "C" void Csci_Msp_MrdvrSrv_SetConflictStatus(eCsciMspMrdvrConflictStatus status)
{
    if (MRDvrServer::getHandle() != NULL)
        MRDvrServer::getHandle()->setConflictStatus(status);
}

extern "C" const char* Csci_Msp_MrdvrSrv_FindAndUpdateActiveURL(const char *setTopName, bool isLoser)
{
    if (MRDvrServer::getHandle() != NULL)
        return MRDvrServer::getHandle()->FindAndUpdateActiveURL(setTopName, isLoser);
    return "not available";
}

extern "C" bool Csci_Msp_MrdvrSrv_IsLiveStreamingActive()
{
    if (MRDvrServer::getHandle() != NULL)
        return MRDvrServer::getHandle()->IsLiveStreamingActive();
    return false;
}
extern "C" eCsciMspMrdvrSrvStatus Csci_Msp_MrdvrSrv_Initialize()
{
    FNLOG(DL_MSP_MRDVR);

    eMRDvrServerStatus retStatus = kMRDvrServer_Error;
    eCsciMspMrdvrSrvStatus srvStatus = kCsciMspMrdvrSrvStatus_Failed;

    retStatus = MRDvrServer::getInstance()->Initialize();

    if (retStatus == kMRDvrServer_Ok)
    {
        srvStatus = kCsciMspMrdvrSrvStatus_OK;
    }
    else if (retStatus == kMRDvrServer_AlreadyInitialized)
    {
        srvStatus = kCsciMspMrdvrSrvStatus_AlreadyInitialized;
    }

    return srvStatus;
};


extern "C" void Csci_Msp_MrdvrSrv_Finalize()
{
    FNLOG(DL_MSP_MRDVR);

    MRDvrServer::getInstance()->MRDvrServer_Finalize();

    return;
};

extern "C" eCsciMspMrdvrSrvStatus Csci_Msp_MrdvrSrv_GetStreamingStatus(
    const char *srvUrl,
    bool *pIsStreaming)
{

    eCsciMspMrdvrSrvStatus srvStatus = kCsciMspMrdvrSrvStatus_Invalid;
    eMRDvrServerStatus retStatus = kMRDvrServer_Error;

    retStatus = MRDvrServer::getInstance()->isAssetInStreaming(srvUrl, pIsStreaming);
    if (retStatus == kMRDvrServer_Ok)
    {
        srvStatus = kCsciMspMrdvrSrvStatus_OK;
    }
    return srvStatus;
}

MRDvrServer::MRDvrServer()
{
    FNLOG(DL_MSP_MRDVR);
    suCallbackId = 0;
    tdCallbackId = 0;
    isInitialized = false;
    isMrDvrAuthorized = true;
    eventHandlerThread = -1;
    m_sessionIDptr = 0;
    mCancelledSessionPositon = 0;
    mTunerFreeFlag = 0;
    // create event queue for scan thread
    threadEventQueue = new MSPEventQueue();
    conflictSessInfo.isConflict = false;
    conflictSessInfo.isCancelled = false;
    conflictSessInfo.sessionID = -1;
    strlcpy(conflictSessInfo.URL, "\0", SRCURL_LEN);
    strlcpy(conflictSessInfo.MAC, "\0", MAX_MACADDR_LEN);
    conflictSessInfo.OutofSeqCount = 0;
    mConflictState = kCsciMspMrdvrConflictStatus_None;
    mPrevConflictState = kCsciMspMrdvrConflictStatus_None;
}

MRDvrServer::~MRDvrServer()
{
    if (threadEventQueue) /*Coverity  19895*/
        delete threadEventQueue;
}

MRDvrServer * MRDvrServer::getInstance()
{
    if (instance == NULL)
    {
        instance = new MRDvrServer();
    }
    return instance;
}

void MRDvrServer::lockMutex(void)
{
    pthread_mutex_lock(&mMutex);
}
void MRDvrServer::unlockMutex(void)
{
    pthread_mutex_unlock(&mMutex);
}

eMRDvrServerStatus MRDvrServer::Initialize()
{
    FNLOG(DL_MSP_MRDVR);

    if (isInitialized == true)
    {
        LOG(DLOGL_ERROR, "MRDvrServer was already initialized. Avoid initializing it again");
        return kMRDvrServer_AlreadyInitialized;
    }

    if (pthread_mutex_init(&(mMutex), NULL))
    {
        LOG(DLOGL_ERROR, "Unable to initalize mutex ");
        return kMRDvrServer_NotInitialized;
    }

    if (kCpe_NoErr != cpe_hnsrvmgr_RegisterCallback(eCpeHnSrvMgrCallbackTypes_MediaServeRequest, (void*)this, ServerManagerCallback, &suCallbackId))
    {
        LOG(DLOGL_ERROR, "Serve Request Callback Reg Failed");
        return kMRDvrServer_Error;
    }

    if (kCpe_NoErr != cpe_hnsrvmgr_RegisterCallback(eCpeHnSrvMgrCallbackTypes_MediaTeardownRequired, (void*)this, ServerManagerCallback, &tdCallbackId))
    {
        LOG(DLOGL_ERROR, "Serve Teardown Callback Reg Failed");
        return kMRDvrServer_Error;
    }
    if (kCpe_NoErr != CurrentVideoMgr::instance()->CurrentVideo_Init())
    {
        LOG(DLOGL_ERROR, "Current video initialization Failed");
        return kMRDvrServer_Error;
    }
    if (kCpe_NoErr != CurrentVideoMgr::instance()->RegisterTerminateSessionCB((void*)this))
    {
        LOG(DLOGL_ERROR, "Current Video Register Callback Reg Failed");
        return kMRDvrServer_Error;
    }
    createThread();
#if PLATFORM_NAME == G8
    Csci_Dvr_RegisterTunerAvailabiltyCallback(HandleTunerFreeNotification);
#endif
    isInitialized = true;
    return kMRDvrServer_Ok;
}

int MRDvrServer::createThread()
{
    FNLOG(DL_MSP_MRDVR);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, (128 * 1024));

    // By default the thread is created as joinable.
    int err  = pthread_create(&eventHandlerThread, &attr, eventthreadFunc, (void *) this);
    LOG(DLOGL_REALLY_NOISY, "MRDvrServer eventHandlerThread pthread_create() executed with status : %d -- Thread id : %lu", err, eventHandlerThread);
    if (!err)
    {
        // failing to set name is not considered an major error
        int retval = pthread_setname_np(eventHandlerThread, "MSP_MRDvr_ServerHandler");
        if (retval)
        {
            LOG(DLOGL_ERROR, "pthread_setname_np error: %d", retval);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "pthread_create error %d\n", err);
    }

    return err;
}


void MRDvrServer::stopThread()
{
    FNLOG(DL_MSP_MRDVR);
    LOG(DLOGL_REALLY_NOISY, "MRDvrServer eventHandlerThread id : %lu ", eventHandlerThread);
    if (eventHandlerThread != (pthread_t) - 1)
    {
        pthread_cancel(eventHandlerThread);
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "MRDvrServer eventHandlerThread is uninitialized; Avoid canceling it");
    }
    return;
}

/** *********************************************************
 */
void* MRDvrServer::eventthreadFunc(void *data)
{
    // This is a static method running in a pthread and does not have
    // direct access to class variables.  Access is through data pointer.

    FNLOG(DL_MSP_MRDVR);

    MRDvrServer*     inst        = (MRDvrServer*)data;
    if (inst)
    {
        MSPEventQueue* eventQueue  = inst->threadEventQueue;
        if (eventQueue)
        {
            LOG(DLOGL_REALLY_NOISY, "MRDvrServer::eventthreadFunc started ");

            while (1)
            {
                Event* evt = eventQueue->popEventQueue();
                // lock mutux
                bool status = inst->handleEvent(evt);  // call member function to handle event
                eventQueue->freeEvent(evt);

                // lock mutux
                if (status)
                {
                    LOG(DLOGL_REALLY_NOISY, " exit exit ");
                    break;
                }
            }
        }
    }
    LOG(DLOGL_NORMAL, "MRDvrServer::eventthreadFunc exit ");
    pthread_exit(NULL);
    return NULL;
}

int MRDvrServer::ServerManagerCallback(tCpeHnSrvMgrCallbackTypes type, void *userdata, void *pCallbackSpecific)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(userdata)
    int ret = -1;
    // if serving 3 client then return error
    MRDvrServer*     inst        = (MRDvrServer*)userdata;

    if (inst)
    {
        LOG(DLOGL_REALLY_NOISY, "ServerManagerCallback inst->ServeSessList.size() :%d \n", inst->ServeSessList.size());

        if (((type == eCpeHnSrvMgrCallbackTypes_MediaServeRequest) && (inst->ServeSessList.size() < getMaxClient())) ||
                type == eCpeHnSrvMgrCallbackTypes_MediaTeardownRequired)
        {
            tMrdvrSrvEventType evtType = (tMrdvrSrvEventType)type;
            dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "Current serve count is %d\n", inst->ServeSessList.size());
            LOG(DLOGL_REALLY_NOISY, " dispatch event: %d\n", type);
            inst->threadEventQueue->dispatchEvent(evtType, pCallbackSpecific);
            ret = 0;
        }
        else
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, " Not going to serve the request from client,as current serving count is %d\n", inst->ServeSessList.size());
        }
    }
    return ret;
}


bool MRDvrServer::handleEvent(Event *evt)
{
    FNLOG(DL_MSP_MRDVR);

    if (!evt)
    {
        LOG(DLOGL_ERROR, "warning null event ");
        return false;
    }

    LOG(DLOGL_NOISE, "MRDvrServer::handleEvent : %d\n", evt->eventType);

    switch (evt->eventType)
    {
    case kMrdvrServeEvent:
    {
        HandleServeRequest((tCpeHnSrvMgrMediaServeRequestInfo *)evt->eventData);
    }
    break;

    case kMrdvrTeardownEvent:
    {
        HandleTeardownRequest((tCpePgrmHandle*)evt->eventData);
    }
    break;
    case kMrdvrExitThreadEvent:
    {
        LOG(DLOGL_NOISE, "kMrdvrExitThreadEvent return true\n");
        return true;
    }
    break;
    case kMrdvrTerminateSessionEvent:
    {
        HandleTerminateSession();
    }
    break;
    default:
        break;
    }
    return false;
}


void MRDvrServer::MRDvrServer_Finalize()
{
    FNLOG(DL_MSP_MRDVR);
    if (isInitialized)
    {
        if (suCallbackId)
        {
            cpe_hnsrvmgr_UnregisterCallback(suCallbackId);
            suCallbackId = 0;
        }
        LOG(DLOGL_NOISE, "cpe_hnsrvmgr_UnregisterCallback(suCallbackId)\n");

        if (tdCallbackId)
        {
            cpe_hnsrvmgr_UnregisterCallback(tdCallbackId);
            tdCallbackId = 0;
        }
        LOG(DLOGL_NOISE, "cpe_hnsrvmgr_UnregisterCallback(tdCallbackId)\n");

        if (CurrentVideoMgr::instance())
        {
            if (kCpe_NoErr != CurrentVideoMgr::instance()->UnregisterTerminateSessionCB())
            {
                LOG(DLOGL_ERROR, "Current Video unRegister Callback Reg Failed");
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Current video get instance failed");
        }

        // Stop thread
        threadEventQueue->dispatchEvent(kMrdvrExitThreadEvent, NULL);
        LOG(DLOGL_NOISE, "dispatched Event waiting for join\n");
        pthread_join(eventHandlerThread, NULL);       // wait for event thread to exit
        LOG(DLOGL_NOISE, "return from join now exit\n");
        eventHandlerThread = 0;

        // Delete MRDvr Server Object
        if (instance)
        {
            LOG(DLOGL_NOISE, "delete instance\n");
            delete instance;
            instance = NULL;
        }
#if PLATFORM_NAME == G8
        Csci_Dvr_UnRegisterTunerAvailabiltyCallback(HandleTunerFreeNotification);
#endif
        LOG(DLOGL_NOISE, "exit finalize\n");
    }
    else
    {
        LOG(DLOGL_NORMAL, "MRDvrServer is not initialized or already finalized!");
    }
}

void MRDvrServer::HandleServeRequest(tCpeHnSrvMgrMediaServeRequestInfo *reqInfo)
{

    FNLOG(DL_MSP_MRDVR);
    eIMediaPlayerStatus playerStatus = kMediaPlayerStatus_Ok;
    bool isNotSequential = false;

    if (reqInfo == NULL)
    {
        LOG(DLOGL_ERROR, "%s:%d reqInfo is NULL", __FUNCTION__, __LINE__);
        return ;
    }
    if (IsSrvReqValid(reqInfo) != true)
    {
        LOG(DLOGL_ERROR, "Invalid Serve request.. Not handled");
        return ;
    }

    if (IsSrvReqPending(reqInfo) == true)
    {
        LOG(DLOGL_ERROR, "Request is in a stale state... Take appropriate action");
        HandlePendingRequest(reqInfo);
        return;
    }
    else
    {
        isNotSequential = CheckForOutOfSequenceEvents(reqInfo);
    }
    if (isNotSequential == true)
    {
        LOG(DLOGL_REALLY_NOISY , "its an out of sequence request.. Proceeding to serve this request but donot serve if its resulting in conflict");
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "Not out of sequence");
    }
    ServeSessionInfo *session = NULL;

    bool bCurrentVideoRequest = false;
    if (strstr((char *)reqInfo->pURL, "avfs://item=plat-live"))
    {
        bCurrentVideoRequest = true;
        LOG(DLOGL_NORMAL, "Current video media serve request received");
    }
    else if (!isMrDvrAuthorized) //TODO - Does this needs to be extended to add additional check for IP clients package? - ARUN
    {
        LOG(DLOGL_ERROR, "MRDVR package not authorized, nor current video. Hence turning down the request");
        return;
    }

    string filepath, filename;
    uint32_t ipaddr;
    char MAC[MAX_MACADDR_LEN] = {0};
    sprintf(MAC, "%02x%02x%02x%02x%02x%02x", reqInfo->macAddr[0], reqInfo->macAddr[1], reqInfo->macAddr[2], reqInfo->macAddr[3], reqInfo->macAddr[4], reqInfo->macAddr[5]);
    char srcurl[SRCURL_LEN] = {0};
    float nptPosition = 0.0;

    ipaddr = reqInfo->ipAddr[0] | (reqInfo->ipAddr[1] << 8) | (reqInfo->ipAddr[2] << 16) | (reqInfo->ipAddr[3] << 24);

    LOG(DLOGL_REALLY_NOISY, "HandleServeRequest !!!!! ipaddress: %x %x %x %x \n", reqInfo->ipAddr[0], reqInfo->ipAddr[1], reqInfo->ipAddr[2], reqInfo->ipAddr[3]);
    LOG(DLOGL_REALLY_NOISY, "macid: %x %x %x %x %x %x \n", reqInfo->macAddr[0], reqInfo->macAddr[1], reqInfo->macAddr[2], reqInfo->macAddr[3], reqInfo->macAddr[4], reqInfo->macAddr[5]);
    LOG(DLOGL_REALLY_NOISY, "pReqURL %s \n", reqInfo->pReqURL);
    LOG(DLOGL_REALLY_NOISY, "pURL %s \n", reqInfo->pURL);
    LOG(DLOGL_REALLY_NOISY, "session id %x\n", reqInfo->sessionID);

    // Request for sign-on if it is not already done.
    static bool isSignOnTriggered = false;
    if (isSignOnTriggered == false)
    {
        LOG(DLOGL_ERROR, " Not an Error: calling Csci_Signaling_RequestFastBoot API");
        Csci_Signaling_RequestFastBoot();
        isSignOnTriggered = true;
    }

    // If current video request is from second client, reject it. Only one live streaming supported.
    if (bCurrentVideoRequest)
    {
        uint32_t clientIP = CurrentVideoMgr::instance()->CurrentVideo_getClientIP();
        if (clientIP == 0)
        {
            CurrentVideoMgr::instance()->CurrentVideo_setClientIP(ipaddr);
        }
        else if (clientIP != ipaddr)
        {
            LOG(DLOGL_ERROR, "Current video is already being streamed to a client, this new request will not be served for: %d.", ipaddr);
            return;
        }
    }
    else
    {
#if PLATFORM_NAME == G8
        if (strstr((char *)reqInfo->pURL, "avfs://item=live/") || strstr((char *)reqInfo->pURL, "avfs://item=vod/"))
        {
            eUnmanagedDevice_Status status_unmanaged_device = isUnmanagedDevice(MAC);
            if (status_unmanaged_device == kUnmanagedDevice_Status_Found)
            {
                LOG(DLOGL_ERROR, "Its an unmanaged device. So stop the streaming");
                if (cpe_hnsrvmgr_NotifyServeFailure(reqInfo->sessionID, eCpeHnSrvMgrMediaServeStatus_TuneRejected) != 0)
                {
                    LOG(DLOGL_ERROR, "Failed to notify platform of serve failure");
                }
                else
                {
                    LOG(DLOGL_NORMAL, "Notified successfully for stopping unmanaged device streaming - cpe_hnsrvmgr_NotifyServeFailure");
                }
                return;
            }
            else if (status_unmanaged_device == kUnmanagedDevice_Status_NotFound)
            {
                LOG(DLOGL_REALLY_NOISY, "Not an unmanaged device. So continue streaming to client");
            }
            else
            {
                LOG(DLOGL_ERROR, "Error occured while finding unmanaged device");
            }
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "Its a recording url. Continue streaming rec url");
        }
#endif
    }

    if ((reqInfo->pURL != NULL) && strlen((char*)reqInfo->pURL) <= SRCURL_LEN)
    {
        strcpy(srcurl, (const char *) reqInfo->pURL);
    }
    else
    {
        if (reqInfo->pURL == NULL)
        {
            LOG(DLOGL_ERROR, "NULL reqInfo->pURL... Returning");
        }
        else
        {
            LOG(DLOGL_ERROR, "pURL:%s is lengthier than %d... strcpy might overrun srcurl", reqInfo->pURL, SRCURL_LEN);
            LOG(DLOGL_ERROR, "No point in proceeding further... Returning");
        }
        return;
    }

#if PLATFORM_NAME == G8
    std::string sessionId;
    //This is only done for VOD playback requests and also check if its not a live channel published as 'item=vod'
    if ((strstr((char *)reqInfo->pURL, gAvfsVodPrefix) != NULL) && (strncmp((char *)reqInfo->pURL, "avfs://item=vod/sctetv://", strlen("avfs://item=vod/sctetv://")) != 0) && (strncmp((char *)reqInfo->pURL, "avfs://item=vod/sappv://", strlen("avfs://item=vod/sappv://")) != 0))
    {
        std::string urlString = (char*) reqInfo->pURL;
        LOG(DLOGL_REALLY_NOISY, "urlString %s", urlString.c_str());
        sessionId = urlString.substr(strlen(gAvfsVodLscpPrefix));
        LOG(DLOGL_REALLY_NOISY, "sessionId %s", sessionId.c_str());
        if (Csci_Websvcs_Gateway_Streaming_RetrieveServiceUrl(sessionId.c_str(), srcurl) != kCsciWebSvcsStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Unable to retreive the VOD Playback URL...! Not Proceeding further...!");
            return;
        }
        LOG(DLOGL_REALLY_NOISY, "srcurl %s\n", srcurl);
        if (Csci_Websvcs_Gateway_Streaming_RetrieveNptPosition(sessionId.c_str(), &nptPosition) != kCsciWebSvcsStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Unable to retreive the npt Position...! ");
        }
    }
#endif
    /* Why this below hack??? */

    /* The local recording playback source URL is avfs:\\<filename>.
       Mrdvr streaming URL (received in HN serve request) also is "avfs://<filename>".
       Hence to distinguish between the 2,
       the URL received in HN serve request (i.e, avfs:\\<filename>) converted to
       svfs:\\<filename>, before entering into MSP framework (i.e., MSP session load)

       Inside MSP throughout lifetime if streaming the URL is svfs:\\<filename>.
       Depending on this appropriate controller and source will be created.

       Also though MSP framework has the URL as svfs:\\<filename>, any URL interaction
       with CPERP will be with it's original name i.e., avfs:\\<filename>
    */
    if (!strstr((char *)reqInfo->pURL, "avfs://item=live/")  && !strstr((char *)reqInfo->pURL, gAvfsVodPrefix))
    {
        if (srcurl[0] == 'a') srcurl[0] = 's';
    }
    if (!strstr((char *)reqInfo->pURL, "avfs://segmented"))
    {
        LOG(DLOGL_REALLY_NOISY, "Not a Segmented recording ");
        if (bCurrentVideoRequest)
        {
            filepath = "";
            filename = CurrentVideoMgr::instance()->CurrentVideo_GetTsbFileName();
            //filename = "/mnt/dvr0/J07IJ0gG";
            filepath.append("svfs:/");
            filepath.append(filename);
            strcpy(srcurl, filepath.c_str());
            LOG(DLOGL_REALLY_NOISY, "This is a current video request and the filepath is %s", srcurl);
        }
    }


    LOG(DLOGL_REALLY_NOISY, "srcurl %s\n", srcurl);
    session = new ServeSessionInfo;

    if (session == NULL)
        return ;

    bzero(session, sizeof(ServeSessionInfo)); /*Coverity 20008*/

    LOG(DLOGL_REALLY_NOISY, "Creating Media Streaming Session for the source URL %s\n", srcurl);

    IMediaStreamer* ptrIMediaStreamer = IMediaStreamer::getMediaStreamerInstance();
    if (ptrIMediaStreamer == NULL)
    {
        LOG(DLOGL_ERROR, "NULL Media Streamer\n");
        delete session;
        session = NULL;
        return;
    }

    playerStatus = ptrIMediaStreamer->IMediaStreamerSession_Create(&(session->mPtrStreamingSession), mediaPlayerCallbackFunc, this);

    if (playerStatus != kMediaPlayerStatus_Ok)
    {
        LOG(DLOGL_ERROR, "IMediaStreamerSession_Create failed <Status:%d>\n", playerStatus);
        delete session;
        session = NULL;
        return;
    }


    addToCache(m_ipcsession, reqInfo, srcurl, session->mPtrStreamingSession);

#if PLATFORM_NAME == G8 || PLATFORM_NAME == IP_CLIENT
    if (strstr((char *)reqInfo->pURL, HN_ONDEMAND_STREAMING_URL) != NULL && strncmp((char *)reqInfo->pURL, MRDVR_TUNER_STREAMING_URL, strlen(MRDVR_TUNER_STREAMING_URL)) != 0 && strncmp((char *)reqInfo->pURL, MRDVR_TUNER_PPV_STREAMING_URL, strlen(MRDVR_TUNER_PPV_STREAMING_URL)) != 0)
    {
        //Add MediaPlayer Session object in MAP contained in webservices.
        LOG(DLOGL_REALLY_NOISY, "URL received for play request is: %s, corresponding IMediaPlayerSession session is: %p", reqInfo->pURL, session->mPtrStreamingSession);
        if (Csci_Websvcs_Gateway_Streaming_Set_MediaSessionInstance(sessionId.c_str(), session->mPtrStreamingSession) != kCsciWebSvcsStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Unable to set mediaSession Instance .! Not Proceeding further...!");
            /* media streamer session destroy */
            pthread_mutex_lock(&mMutex);
            playerStatus = ptrIMediaStreamer->IMediaStreamerSession_Destroy(session->mPtrStreamingSession);
            pthread_mutex_unlock(&mMutex);
            if (playerStatus != kMediaPlayerStatus_Ok)
            {
                LOG(DLOGL_ERROR, "IMediaStreamerSession_Destroy failed <Status:%d>\n", playerStatus);
            }

            /* removing from cache */
            removeFromCache(m_ipcsession, session->mPtrStreamingSession);

            delete session;
            session = NULL;
            return;
        }
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "Its an In-memory streaming request");
        LOG(DLOGL_REALLY_NOISY, "URL received for play request is: %s, corresponding IMediaPlayerSession session is: %p", reqInfo->pURL, session->mPtrStreamingSession);
    }

#endif

    if (session->mPtrStreamingSession == NULL)
    {
        LOG(DLOGL_ERROR, "IMediaStreamerSession_Create returned NULL session <Status:%d>\n", playerStatus);
        delete session;
        session = NULL;
        return ;
    }


    LOG(DLOGL_EMERGENCY, "%s: HN Serve session setup completed with the streaming session_handle:%p for client:%s with SID:%d and URL:%s\n", __FUNCTION__, session->mPtrStreamingSession, MAC, reqInfo->sessionID, reqInfo->pURL);

    const MultiMediaEvent *pMMEvent = NULL;

    playerStatus = ptrIMediaStreamer->IMediaStreamerSession_Load(session->mPtrStreamingSession, (const char *) srcurl, &pMMEvent);
    if (playerStatus != kMediaPlayerStatus_Ok)
    {

        LOG(DLOGL_ERROR, "IMediaStreamerSession_Load failed <Status:%d>\n", playerStatus);
        LOG(DLOGL_ERROR, "Checking if conflict exists");
        HandleTunerConflict(reqInfo, MAC, pMMEvent, isNotSequential);
        cleanupStreamingSession(session->mPtrStreamingSession);
#if PLATFORM_NAME == G8 || PLATFORM_NAME == IP_CLIENT
        //Issues connection complete and speeds up cgmi_Unload() call and retry request handling
        cpe_hnsrvmgr_NotifyServeFailure(reqInfo->sessionID, eCpeHnSrvMgrMediaServeStatus_TuneRejected);
#endif
        delete session;
        session = NULL;
        return;
    }
    else
    {
        /* Save the CPERP Streaming Session ID in MSP controller and Source */
        IMediaController *controller = session->mPtrStreamingSession->getMediaController();
        if (controller)
        {
            controller->SetCpeStreamingSessionID(reqInfo->sessionID);
        }
        else
        {
            LOG(DLOGL_ERROR, "NULL controller\n");

            /* Clean up the streaming session */
            cleanupStreamingSession(session->mPtrStreamingSession);
            delete session;
            session = NULL;
            return;
        }
    }
    printDetails();
    const MultiMediaEvent *pMMEvent2 = NULL;
    playerStatus = ptrIMediaStreamer->IMediaStreamerSession_Play(session->mPtrStreamingSession, gDecUrl, nptPosition, &pMMEvent2);
    if (playerStatus != kMediaPlayerStatus_Ok)
    {
        LOG(DLOGL_ERROR, "IMediaStreamerSession_Play failed <Status:%d>\n", playerStatus);

        if (playerStatus == kMediaPlayerStatus_ClientError)
        {
            //prepare SSE data
            tIMediaPlayerCallbackData cbData = {kMediaPlayerSignal_Problem, kMediaPlayerStatus_ClientError, {0}};

            //Need to send SSE from here.
            MRDvrServer::mediaPlayerCallbackFunc(session->mPtrStreamingSession, cbData, NULL, NULL);
        }

        /* Clean up the streaming session */
        cleanupStreamingSession(session->mPtrStreamingSession);
        delete session;
        session = NULL;


        return;
    }
    else
    {
        if (bCurrentVideoRequest)
        {
            CurrentVideoMgr::instance()->CurrentVideo_SetCurrentVideoStreaming(true);
        }

        if (!pthread_mutex_lock(&(mMutex)))
        {
            ServeSessList.push_back(session);
            pthread_mutex_unlock(&(mMutex));
            if (bCurrentVideoRequest)
            {
                CurrentVideoMgr::instance()->setCurrentVideoCpeSrcHandle(session);
            }

        }
        else
        {
            delete session;
            session = NULL;
        }
    }
    LOG(DLOGL_REALLY_NOISY, "IMediaStreamerSession_Play success <Status:%d>\n", playerStatus);

    return;
}

void MRDvrServer::HandleTeardownRequest(tCpePgrmHandle* pPgrmHandle)
{
    FNLOG(DL_MSP_MRDVR);
    ServeSessionInfo *session = NULL;

    if (pPgrmHandle == NULL)
    {
        LOG(DLOGL_ERROR, "HN Srv stop failed - Invalid program handle\n");
        return;
    }

    LOG(DLOGL_REALLY_NOISY, " callback called HandleTeardownRequest !!!!! size:%d\n ", ServeSessList.size());
    // If tear down request is for current video, stop streaming.
    CurrentVideoMgr* currentVideo = CurrentVideoMgr::instance();
    if (currentVideo != NULL)
    {
        if (currentVideo->getCurrentVideoCpeSrcHandle() != NULL)
        {
            if (currentVideo->getCurrentVideoCpeSrcHandle()->mPtrStreamingSession != NULL)
            {
                if (currentVideo->getCurrentVideoCpeSrcHandle()->mPtrStreamingSession->getMediaController() != NULL)
                {
                    if (currentVideo->getCurrentVideoCpeSrcHandle()->mPtrStreamingSession->
                            getMediaController()->getCpeProgHandle() != NULL)
                    {
                        if (currentVideo->getCurrentVideoCpeSrcHandle()->mPtrStreamingSession->getMediaController()->getCpeProgHandle() == pPgrmHandle)
                        {
                            dlog(DL_MSP_MRDVR, DLOGL_SIGNIFICANT_EVENT, "TeardownRequest is for current video.");
                            currentVideo->setHandleTearDownReceived(true);
                            if (currentVideo->CurrentVideo_IsCurrentVideoStreaming() && currentVideo->CurrentVideo_getLiveSessionId())
                            {
                                currentVideo->CurrentVideo_StopStreaming();
                                /* Clean up of current video session */
                                currentVideo->setCurrentVideoCpeSrcHandle(NULL);
                                currentVideo->CurrentVideo_setClientIP(0);
                            }
                            else
                            {
                                dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "NOT calling CurrentVideo_StopStreaming because isStreamingtoClient:%d sessionId:%d.", \
                                     currentVideo->CurrentVideo_IsCurrentVideoStreaming(), currentVideo->CurrentVideo_getLiveSessionId());
                            }
                        }
                        else
                        {
                            dlog(DL_MSP_MRDVR, DLOGL_MINOR_DEBUG, "TeardownRequest is NOT for current video.");
                        }
                    }
                }

            }
        }
    }


    /* why mutex is necessary here ?. while resolving tuner conflicts ,there is a possiblity for mrdvrserver to clean up the same streaming session via
     * handleteardown request or handlelocalteardown (this will occur when cpe_hnsrvmgr_stop fails and posts handlelocalteardown request to MW for the same session
     */
    pthread_mutex_lock(&mMutex) ;

    list<ServeSessionInfo *>::iterator itr;
    for (itr = ServeSessList.begin(); itr != ServeSessList.end(); ++itr)
    {
        LOG(DLOGL_EMERGENCY, "progHandle:%p  inputProgHandle:%p \n", (*itr)->mPtrStreamingSession->getMediaController()->getCpeProgHandle(), pPgrmHandle);
        if ((*itr)->mPtrStreamingSession->getMediaController()->getCpeProgHandle() == pPgrmHandle)
        {
            session = *itr;
            ServeSessList.remove(session);
            break;
        }
    }
    pthread_mutex_unlock(&mMutex) ;

    if (session == NULL)
    {
        LOG(DLOGL_ERROR, "HN Srv stop failed - Invalid session - not found \n");
        return;
    }

    char *MacAddr = getMacFromHandle(session->mPtrStreamingSession);
    if (MacAddr != NULL)
    {
        LOG(DLOGL_EMERGENCY, "%s: HN Srv stop called for the streaming session %p for client:%s with URL:%s \n", __FUNCTION__, session->mPtrStreamingSession, MacAddr, session->mPtrStreamingSession->getMediaController()->GetSourceURL().c_str());
    }
    else
    {
        LOG(DLOGL_ERROR, "NULL Mac address");
    }
    /*
    Before Cleaning UP send the client a notification to retry incase we have an out of seq req that was not served because of conflict
    */

    if (isRequestOutofSeq(session->mPtrStreamingSession) == true)
    {
        LOG(DLOGL_REALLY_NOISY, "Request Came Out of sequence..");
        /*
        * conflictSessInfo.OutofSeqCount indicates the number of requests that came out of sequence and was not handled bcause it resulted in conflict.
        * If its greater than zero, indicates there are requests pending to be handled. Else, no retry since we serve out of sequence requests if tuners are available
        */
        if (conflictSessInfo.OutofSeqCount > 0)
        {
            LOG(DLOGL_ERROR, "Retrying due to out of seq event. Ignore the previous session failure");
            sendSseNotification(getOutOfSeqURL(session->mPtrStreamingSession), "RETRY", getMacFromHandle(session->mPtrStreamingSession), kMediaPlayerStatus_Ok, kMediaPlayerSignal_ServiceRetry);
            /*
            * Do counter reset after cleanup is done to enusre  a client who is awaiting for a conflict resolution to complete doesnt retry now
            */
        }
    }

    /* Clean up the streaming session */
    cleanupStreamingSession(session->mPtrStreamingSession);

    if (conflictSessInfo.OutofSeqCount > 0)
    {
        LOG(DLOGL_REALLY_NOISY, "Counter reset done..");
        conflictSessInfo.OutofSeqCount--;
    }
    /*delete the serve session (freeing the memory) */
    delete session;
    session = NULL;

    LOG(DLOGL_REALLY_NOISY, "Exit - MRDvrServer::HandleTeardownRequest\n");
//Notify Mediashrink to commit any deferred transcoding
#if ENABLE_MSPMEDIASHRINK == 1 && PLATFORM_NAME == G8
    Msp_Mediashrink_Rec_Play_Stop_Notify();
#endif
    return;
}

eMRDvrServerStatus MRDvrServer::isAssetInStreaming(
    const char *srvUrl,
    bool *pIsStreaming)
{
    FNLOG(DL_MSP_MRDVR);
    eMRDvrServerStatus retStatus = kMRDvrServer_Ok;
    list<ServeSessionInfo *>::iterator itr;
    if (srvUrl == NULL)
    {
        retStatus = kMRDvrServer_InvalidParameter;
    }
    else
    {
        pthread_mutex_lock(&(mMutex));
        for (itr = ServeSessList.begin(); itr != ServeSessList.end(); ++itr)
        {
            //We will replace svfs with avfs in tmpStr - the url which is being streamed...
            string tmpStr = (*itr)->mPtrStreamingSession->getMediaController()->GetSourceURL();
            const char* searchToken = "svfs";
            const char* replaceToken = "avfs";


            LOG(DLOGL_NORMAL, "Before change - SourceURL=%s \n", tmpStr.data());
            size_t pos = tmpStr.find(searchToken);
            if (pos != string::npos)
            {
                tmpStr.replace(pos, strlen(replaceToken), replaceToken);
            }

            LOG(DLOGL_NORMAL, "After change: SourceURL=%s , ServerURL=%s\n", tmpStr.data(), srvUrl);
            if (strstr(tmpStr.c_str(), srvUrl))
            {
                *pIsStreaming = true;
                break;
            }
        }
        pthread_mutex_unlock(&(mMutex));
    }
    return retStatus;
}

void MRDvrServer::cleanupStreamingSession(IMediaPlayerSession *pMPSession)
{
    eIMediaPlayerStatus playerStatus = kMediaPlayerStatus_Ok;

    /* Check for Valid Session */
    if (pMPSession == NULL)
    {
        LOG(DLOGL_ERROR, "MRDvrServer::cleanStreamingSession failed - NULL Session\n");
        return;
    }

    /* Get MSP Media Streamer */
    IMediaStreamer* ptrIMediaStreamer = IMediaStreamer::getMediaStreamerInstance();
    if (ptrIMediaStreamer)
    {
        LOG(DLOGL_REALLY_NOISY, "Trying to acquire streamer lock\n");

        //remove the session details stored.Should always be called before stop&eject
        removeFromCache(m_ipcsession, pMPSession);

        playerStatus = ptrIMediaStreamer->IMediaStreamerSession_Stop(pMPSession, true, false);
        if (playerStatus != kMediaPlayerStatus_Ok)
        {
            LOG(DLOGL_ERROR, "IMediaStreamerSession_Stop failed <Status:%d>\n", playerStatus);
        }

        playerStatus = ptrIMediaStreamer->IMediaStreamerSession_Eject(pMPSession);
        if (playerStatus != kMediaPlayerStatus_Ok)
        {
            LOG(DLOGL_ERROR, "IMediaStreamerSession_Eject failed <Status:%d>\n", playerStatus);
        }

        playerStatus = ptrIMediaStreamer->IMediaStreamerSession_Destroy(pMPSession);
        if (playerStatus != kMediaPlayerStatus_Ok)
        {
            LOG(DLOGL_ERROR, "IMediaStreamerSession_Destroy failed <Status:%d>\n", playerStatus);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "MRDvrServer::cleanStreamingSession failed - No Media Streamer");
    }

    return;
}

/**
 *   @param pMPSession - Media steaming session for which tear down triggered
 *
 *   @return None
 *   @brief  Helper function to tear down the media streaming session in SAIL, without
 *     waiting for the tear down request from CPERP. This will be useful in handling
 *     error scenarios to clean up the orphan streaming sessions.
 *
 */

void MRDvrServer::handleLocalTeardown(IMediaPlayerSession *pMPSession)
{
    ServeSessionInfo *session = NULL;
    LOG(DLOGL_NORMAL, "handleLocalTeardown called for the session %p", pMPSession);

    if (pMPSession == NULL)
    {
        LOG(DLOGL_ERROR, "Tear down triggered for an invalid NULL media player session");
        return;
    }
    //Why mutex is introduced here ?. localteardown and handleteardownrequest should not happen in parallel for the same streaming session
    pthread_mutex_lock(&mMutex);
    list<ServeSessionInfo *>::iterator itr;
    for (itr = ServeSessList.begin(); itr != ServeSessList.end(); ++itr)
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "session:%p  inputSession:%p ", (*itr)->mPtrStreamingSession, pMPSession);
        if ((*itr)->mPtrStreamingSession == pMPSession)
        {
            session = *itr;
            ServeSessList.remove(session);
            break;
        }
    }
    pthread_mutex_unlock(&mMutex);

    if (session == NULL)
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "Locally triggered stop failed - Invalid session - not found ");
        return;
    }

    dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s: Locally triggered stop called for the streaming session %p", __FUNCTION__, session->mPtrStreamingSession);

    /* Clean up the streaming session */
    cleanupStreamingSession(session->mPtrStreamingSession);

    /* Remove the serve session from the list */
    delete session;
    session = NULL;
}

/**
 *   @param pIMediaPlayerSession - Media steaming session for which callback triggered
 *   @param callbackData         - event information carried in callback
 *   @param pClientContext       - user data to identify caller context
 *   @param pMme                 - MME
 *
 *   @return None
 *   @brief Media Player Callback Function to pass the asynchronous events from
 *          media streaming session to mrdvr server that invoked the media
 *          streaming session.
 *
 */
void MRDvrServer::mediaPlayerCallbackFunc(IMediaPlayerSession* pIMediaPlayerSession,
        tIMediaPlayerCallbackData callbackData,
        void* pClientContext,
        MultiMediaEvent *pMme)
{
    (void)pMme;

    if (pIMediaPlayerSession == NULL)
    {
        LOG(DLOGL_ERROR, "Invalid Media PLayer Session received in media player callback\n");
        return;
    }

    MRDvrServer *inst = (MRDvrServer *) pClientContext;
    if (inst == NULL)
    {
        LOG(DLOGL_ERROR, "Invalid user data <client context> received in media player callback\n");
        return;
    }

    dlog(DL_MSP_MRDVR, DLOGL_NOISE, "Mediaplayercb for session %p <Signal %d status %d\n> \n", pIMediaPlayerSession, callbackData.signalType, callbackData.status);

    if ((callbackData.signalType != kMediaPlayerSignal_TimeshiftTerminated) && (callbackData.signalType != kMediaPlayerSignal_ResourceLost) && (callbackData.signalType != kMediaPlayerSignal_ServiceRetry))
    {
        inst->sendSseNotification((char *)pIMediaPlayerSession->GetServiceUrl().c_str(), "Media_Player_Callback", inst->getMacFromHandle(pIMediaPlayerSession), callbackData.status, callbackData.signalType);
    }
    if (callbackData.signalType == kMediaPlayerSignal_ServiceLoaded)
    {
        int connectionId = Csci_Sdvm_getConnectionId(pIMediaPlayerSession);
        LOG(DLOGL_ERROR, "connectionId in sending sse=%d\n", connectionId);
        if (connectionId != 0)
        {
            inst->sendSseNotification_SDV((char *)pIMediaPlayerSession->GetServiceUrl().c_str(), connectionId, MRDvrServer::getInstance()->getMacFromHandle(pIMediaPlayerSession));
        }
    }
    else {}


    switch (callbackData.signalType)
    {
    case kMediaPlayerSignal_TimeshiftTerminated:
    {
        inst->handleLocalTeardown(pIMediaPlayerSession);
    }
    break;

    case kMediaPlayerSignal_Problem:
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "Media Player signal problem.. Not able to stream the content..");
    }
    break;
    case kMediaPlayerSignal_ServiceAuthorized:
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "Service Authorized event recieved.");
    }
    break;
    case kMediaPlayerSignal_ServiceDeauthorized:
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "Service Deauthorized event recieved..Donot Teardown the session and wait for authorization (or) till client closes the session ");
    }
    break;
    case kMediaPlayerSignal_ServiceRetry:
    {
        if (callbackData.status == kMediaPlayerStatus_ContentNotFound)
        {
            inst->EnableRetry(pIMediaPlayerSession);
        }
        else
        {
            inst->sendSseNotification((char *)pIMediaPlayerSession->GetServiceUrl().c_str(), "RETRY", inst->getMacFromHandle(pIMediaPlayerSession), kMediaPlayerStatus_Ok, kMediaPlayerSignal_ServiceRetry);
            dlog(DL_MSP_MRDVR, DLOGL_NORMAL, "Service Retry request recieved.. client will establish a new connection to stream");
        }
    }
    break;
    case kMediaPlayerSignal_ServiceLoaded:
        break;
    case kMediaPlayerSignal_ServiceLoading:
        break;
    case kMediaPlayerSignal_PresentationStarted:
        break;
    case kMediaPlayerSignal_PresentationTerminated:
        break;
    case kMediaPlayerSignal_TimeshiftReady:
        break;
    case kMediaPlayerSignal_PersistentRecordingStarted:
        break;
    case kMediaPlayerSignal_PersistentRecordingTerminated:
        break;
    case kMediaPlayerSignal_StreamAttributeChanged:
        break;
    case kMediaPlayerSignal_BeginningOfStream:
        break;
    case kMediaPlayerSignal_EndOfStream:
        break;
    case kMediaPlayerSignal_NetworkResourceReclamationWarning:
        break;
    case kMediaPlayerSignal_BeginningOfInterstitial:
        break;
    case kMediaPlayerSignal_EndOfInterstitial:
        break;
    case kMediaPlayerSignal_PpvSubscriptionExpired:
        break;
    case kMediaPlayerSignal_PpvSubscriptionAuthorized:
        break;
    case kMediaPlayerSignal_VodPurchaseNotification:
        break;
    case kMediaPlayerSignal_CancelNetworkResourceReclamationWarning:
        break;
    case kMediaPlayerSignal_ApplicationData:
        break;
    case kMediaPlayerSignal_ServiceNotAvailableDueToSdv:
        break;
    case kMediaPlayerSignal_ResourceLost:
    {
        //send SSE event that tuner is lost for the streaming session in progress
        inst->sendSseNotification((char *)pIMediaPlayerSession->GetServiceUrl().c_str(), "TUNER_REVOKED", inst->getMacFromHandle(pIMediaPlayerSession), callbackData.status, callbackData.signalType);
    }
    break;
    case kMediaPlayerSignal_ResourceRestored:
        break;
    default:
    {
        inst->sendSseNotification((char *)pIMediaPlayerSession->GetServiceUrl().c_str(), "Media_Player_Callback", inst->getMacFromHandle(pIMediaPlayerSession), callbackData.status, callbackData.signalType);
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s: event (type %d)(status %d)", __FUNCTION__, callbackData.signalType, callbackData.status);
        break;
    }
    }
}

void MRDvrServer::HandleTunerConflict(tCpeHnSrvMgrMediaServeRequestInfo *reqInfo, char* MAC, const MultiMediaEvent *pMMEvent, bool isOutofSeq)
{
    FNLOG(DL_MSP_MRDVR);
    if (pMMEvent != NULL)
    {
        /*Before Triggering the callback, check if a tuner conflict resolution is already in progress because of another client request.
        If yes, Donot trigger another callback but deny the serve request and intimate the user as prev conflict has to be resolved before taking up the next request.
        If no, initmate the user that there is a conflict and trigger the callback.*/
        if (isOutofSeq == false)
        {
            if (conflictSessInfo.isConflict == false)
            {
                if (getConflictStatus() != kCsciMspMrdvrConflictStatus_Busy)
                {
                    LOG(DLOGL_REALLY_NOISY, "Triggering callback");
                    setTunerFreeFlag(UNSET);//TunerFreeFlag = 0;//Flag to identify if a tuner is available
                    CSailApiScheduler::getHandle().triggerCallback(kDvrAlertEventIPClientGeneratedConflict, (void*) pMMEvent, NULL); //to send mme to the above layer

                    //send SSE event that tuner conflict is introduced and resolution is in progress
                    sendSseNotification((char *)reqInfo->pURL, "TUNER_CONFLICT", MAC, kMediaPlayerStatus_Loading, kMediaPlayerSignal_ServiceLoading);

                    /* Clean up the streaming session after book keeping this conflict generated session's MAC and url details to send SSE for retry/denied notification*/
                    strlcpy(conflictSessInfo.MAC, MAC, MAX_MACADDR_LEN);
                    strlcpy(conflictSessInfo.URL, (char *)reqInfo->pURL, SRCURL_LEN);
                    conflictSessInfo.sessionID = reqInfo->sessionID;
                    conflictSessInfo.isConflict = true;
                    conflictSessInfo.isCancelled = false;
                }
                else
                {
                    LOG(DLOGL_ERROR , "Conflict Resolution is already in progress.. couldnt serve this request.. Sending a deny message to the client..");
                    sendSseNotification((char *)reqInfo->pURL, "TUNER_DENIED", MAC, kMediaPlayerStatus_TuningResourceUnavailable, kMediaPlayerSignal_ServiceLoading);
                    MultiMediaEvent_Finalize(&pMMEvent);
                }
            }
            else
            {
                if (strncmp(conflictSessInfo.MAC, MAC, strlen(MAC)) == 0)
                {
                    LOG(DLOGL_ERROR , "Request from the same client that generated conflict.Update the Channel URL alone for sending SSE notifications");
                    //send SSE event that tuner conflict is introduced and resolution is in progress
                    sendSseNotification((char *)reqInfo->pURL, "TUNER_CONFLICT", MAC, kMediaPlayerStatus_Loading, kMediaPlayerSignal_ServiceLoading);
                    strlcpy(conflictSessInfo.URL, (char *)reqInfo->pURL, SRCURL_LEN);
                }
                else
                {
                    LOG(DLOGL_ERROR , "Conflict Resolution is already in progress.. couldnt serve this request.. Sending a deny message to the client..");

                    /*
                    * send SSE event that tuner was denied for conflict generated request since conflict resoltion is already under progress,
                    * and its not possible to handle further requests resulting in conflict
                    */
                    sendSseNotification((char *)reqInfo->pURL, "TUNER_DENIED", MAC, kMediaPlayerStatus_TuningResourceUnavailable, kMediaPlayerSignal_ServiceLoading);
                }
                MultiMediaEvent_Finalize(&pMMEvent);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Conflict due to out of seq request.. update the count");
            conflictSessInfo.OutofSeqCount++;
            LOG(DLOGL_ERROR, "Reset conflict state to previous state as this detection is invalid");
            mConflictState = mPrevConflictState;
            MultiMediaEvent_Finalize(&pMMEvent);
        }
    }
}

uint32_t MRDvrServer::GetSessionID(uint32_t i)
{
    FNLOG(DL_MSP_MRDVR);
    if (i == (uint32_t) - 1)
    {
        LOG(DLOGL_NORMAL, "session ID returned : %d", m_sessionID[m_sessionIDptr - 1]);
        return m_sessionID[m_sessionIDptr - 1];
    }
    LOG(DLOGL_NORMAL, "session ID returned : %d", m_sessionID[i]);
    return m_sessionID[i];
}

const char * MRDvrServer::GetClientSessionMac(uint32_t sessionID)
{
    FNLOG(DL_MSP_MRDVR);
    pthread_mutex_lock(&mMutex);
    ClientCache::iterator temp = m_ipcsession.begin();
    while (temp != m_ipcsession.end())
    {
        if ((*temp)->session == sessionID)
        {
            pthread_mutex_unlock(&mMutex);
            return (*temp)->macAddress;
        }
        temp++;
    }
    pthread_mutex_unlock(&mMutex);
    return "UNKNOWN";
}

uint32_t MRDvrServer::GetRemovedAssetID()
{
    FNLOG(DL_MSP_MRDVR);
    return mCancelledSessionPositon;
}
void MRDvrServer::ApplyResolution()
{
    FNLOG(DL_MSP_MRDVR);
    if (true == conflictSessInfo.isCancelled) //Denied
    {
        /*
        * Conflict Generated session is cancelled.
        * send SSE event that tuner was denied for conflict generated request and return true since the session was already cleaned up
        */
        sendSseNotification(conflictSessInfo.URL, "TUNER_DENIED", conflictSessInfo.MAC, kMediaPlayerStatus_TuningResourceUnavailable, kMediaPlayerSignal_EndOfStream);
        conflictSessInfo.isConflict = false;
        conflictSessInfo.isCancelled = false;
        return;
    }
    else //Allowed (or) Another IPC/Gateway session was cancelled
    {
        ClientCache::iterator temp = m_ipcsession.begin();//iterator
        LOG(DLOGL_REALLY_NOISY, "Check if another client session is cancelled and cleanup as required");
        while (temp != m_ipcsession.end())
        {
            if ((*temp)->isCancelled == true)
            {
                LOG(DLOGL_REALLY_NOISY, "calling hnservemgrstop...\n");
                if ((*temp)->handle != NULL)
                {
                    if ((((*temp)->handle)->getMediaController())->getCpeProgHandle() != NULL)
                    {
                        //send SSE event that tuner is revoked and then trigger a teardown
                        sendSseNotification((*temp)->avfs, "TUNER_REVOKED", (*temp)->macAddress, kMediaPlayerStatus_TuningResourceUnavailable, kMediaPlayerSignal_EndOfStream);

                        //wait till session is cleaned up / tuner is made free
                        setTunerFreeFlag(UNSET);//TunerFreeFlag = 0;

                        //trigger a teardown request for the session cancelled if session is being streamed/if the session went into an error state
                        if ((((*temp)->handle)->getMediaController())->getCpeProgHandle() != 0)
                        {
                            if (cpe_hnsrvmgr_Stop((((*temp)->handle)->getMediaController())->getCpeProgHandle()) != kCpe_NoErr)
                            {
                                handleLocalTeardown((*temp)->handle);
                                LOG(DLOGL_REALLY_NOISY, "Hn_stop failed... cleaning locally.\n");
                            }
                            else
                            {
                                LOG(DLOGL_REALLY_NOISY, "Waiting for Platform to trigger teardown");
                            }
                        }
                        else
                        {
                            handleLocalTeardown((*temp)->handle);
                        }
                        while (getTunerFreeFlag() != 1)
                        {
                            usleep(1000000);//wait till tunerfreenotification is obtained
                            LOG(DLOGL_ERROR, "Waiting for session to be cleaned up...");
                        }
                        LOG(DLOGL_ERROR, "exited waiting loop..");
                    }
                    else
                    {
                        //send SSE event that tuner is revoked and then trigger a teardown
                        sendSseNotification((*temp)->avfs, "TUNER_REVOKED", (*temp)->macAddress, kMediaPlayerStatus_TuningResourceUnavailable, kMediaPlayerSignal_EndOfStream);

                        LOG(DLOGL_ERROR, "prgm handle is null...Platform has cleanedup the session (or) the session is yet to be streamed - bcoz of an error...Middleware cleanup has to be done");
                        handleLocalTeardown((*temp)->handle);
                    }
                }
                temp = m_ipcsession.begin();
                LOG(DLOGL_NORMAL, " Since a cleanup would have happened, iterator position will be lost.")
            }
            else
            {
                LOG(DLOGL_ERROR, "Skipping node.");
                temp++;
            }
        }
    }
}

void MRDvrServer::setTunerFreeFlag(int val)
{
    FNLOG(DL_MSP_MRDVR);
    if (!pthread_mutex_lock(&mMutex))
    {
        LOG(DLOGL_REALLY_NOISY, "setting tuner flag with value..%d", val);
        mTunerFreeFlag = val;
        pthread_mutex_unlock(&mMutex);
    }
}

int MRDvrServer::getTunerFreeFlag()
{
    FNLOG(DL_MSP_MRDVR);
    int retval = -1;
    if (!pthread_mutex_lock(&mMutex))
    {
        retval = mTunerFreeFlag;
        pthread_mutex_unlock(&mMutex);
    }
    return retval;
}


char* MRDvrServer::getMacFromHandle(IMediaPlayerSession* handle)
{
    FNLOG(DL_MSP_MRDVR);
    ClientCache::iterator temp;
    pthread_mutex_lock(&mMutex);
    for (temp = m_ipcsession.begin(); temp != m_ipcsession.end(); temp++)
    {
        if ((*temp)->handle == handle)
        {
            pthread_mutex_unlock(&mMutex);
            return (*temp)->macAddress;
        }
    }
    pthread_mutex_unlock(&mMutex);
    return NULL;
}

void MRDvrServer::sendSseNotification(char* srcurl, char* msg, char *MAC, eIMediaPlayerStatus status, eIMediaPlayerSignal signal)
{
    eCsciWebSvcsMediaInfoPayloadType payloadType;
    if (MAC != NULL)
    {
        payloadType = kCsciWebSvcsMediaInfoPayloadType_StreamInfoPayload;
        tCsciWebSvcsMediaStreamInfoPayload *obj = new tCsciWebSvcsMediaStreamInfoPayload;
        obj->mediaStreamStatus =  status;
        obj->mediaStreamSignal =  signal;
        obj->mediaStreamConflict =  kMmeResourceType_Tuner;
        strncpy(obj->notificationDesc, msg, strlen(msg) + 1);
        LOG(DLOGL_REALLY_NOISY, "SrcURL is:%s", srcurl);
        LOG(DLOGL_EMERGENCY, "sse to ipc in sendSseNotification signal=%d and status=%d\n", signal, status);

#if PLATFORM_NAME == G8
        //This section should only be invoked for Gateway functionality.
        if (Csci_Websvcs_Gateway_Streaming_Notify((unsigned char *)MAC, (const char*)srcurl, payloadType, obj) == kCsciWebSvcsStatus_Ok)
        {
            LOG(DLOGL_REALLY_NOISY, "SSE sent successfully");
        }
        else
        {
            LOG(DLOGL_ERROR, "Failed to send SSE");
        }
#endif
        delete obj;
        obj = NULL;
    }
    else
    {
        LOG(DLOGL_ERROR, "No Mac found for the session. it was removed/deleted");
    }
}

void MRDvrServer::sendSseNotification_SDV(char* srcurl, int connId, char* MAC)
{
    tCsciWebSvcsSdvInfoPayload *obj = new tCsciWebSvcsSdvInfoPayload;
    obj->sdvConnectionId = connId ;
    LOG(DLOGL_EMERGENCY, "sse to ipc in sendSseNotification_SDV connId=%d\n", obj->sdvConnectionId);
    LOG(DLOGL_REALLY_NOISY, "SrcURL is:%s", srcurl);
    if (MAC != NULL)
    {
#if PLATFORM_NAME == G8
        if (Csci_Websvcs_Gateway_Streaming_Notify((unsigned char*)MAC, (const char*)srcurl, kCsciWebSvcsMediaInfoPayloadType_SdvInfoPayload, obj) == 0)
        {
            LOG(DLOGL_NOISE, "SSE sent successfully");
        }
        else
        {
            LOG(DLOGL_ERROR, "Failed to send SSE");
        }
#endif
    }
    else
    {
        LOG(DLOGL_ERROR, "No Mac found for the session. it was removed/deleted");
    }
    delete obj;
    obj = NULL;
}

//remove session and reorder the list to maintain one-one mapping
void MRDvrServer::removesess(int id)
{
    FNLOG(DL_MSP_MRDVR);
    LOG(DLOGL_REALLY_NOISY, "in remove func");
    int i = 0;
    for (i = 0; i < m_sessionIDptr; i++)
    {
        if (m_sessionID[i] == id)
        {
            LOG(DLOGL_REALLY_NOISY, "match found i value:%d", i);
            mCancelledSessionPositon = i + 1;
            break;
        }
    }
    if (i != m_sessionIDptr)
    {
        while (m_sessionID[i] != 0 && i != m_sessionIDptr)
        {
            m_sessionID[i] = m_sessionID[i + 1];
            i++;
        }
        m_sessionID[i] = 0;
        m_sessionIDptr--;
    }
    else
    {
        LOG(DLOGL_ERROR, "no match found..\n");
    }
}

//reference method to keep track of all sessions (live and recorded) being streamed
void MRDvrServer::printDetails()
{
    FNLOG(DL_MSP_MRDVR);
    ClientCache::iterator temp;
    pthread_mutex_lock(&mMutex);
    for (temp = m_ipcsession.begin(); temp != m_ipcsession.end(); temp++)
    {
        LOG(DLOGL_REALLY_NOISY, "avfs url:%s\nIPAddress : %s\nMAC: %s\nsessionid:%d\nsession handle: %p", (*temp)->avfs, (*temp)->ipAddress, (*temp)->macAddress, (*temp)->session, (*temp)->handle);
    }
    pthread_mutex_unlock(&mMutex);
}

//Remove the session details when a teardown request/session is cancelled and clean up its occurence
void MRDvrServer::removeFromCache(ClientCache &list, IMediaPlayerSession *handle)
{
    FNLOG(DL_MSP_MRDVR);
    ClientCache::iterator itr;
    pthread_mutex_lock(&(mMutex));
    for (itr = list.begin(); itr != list.end(); itr++)
    {
        if ((*itr)->handle == handle)
            break;
    }
    if (itr != list.end())
    {
        if ((*itr)->mRetry == true)
        {
            sendSseNotification((*itr)->avfs, "RETRY", (*itr)->macAddress , kMediaPlayerStatus_Ok, kMediaPlayerSignal_ServiceRetry);
        }
        removesess((*itr)->session);
        ipcsession *temp = *itr;
        list.erase(itr);
        delete temp;
        temp = NULL;
    }
    pthread_mutex_unlock(&(mMutex));
    printDetails();
}

//Add the session details to the cache maintained, when a new serve request comes
void MRDvrServer::addToCache(ClientCache &list, tCpeHnSrvMgrMediaServeRequestInfo *reqInfo, char * srcurl, IMediaPlayerSession *mPtrStreamingSession)
{
    FNLOG(DL_MSP_MRDVR);
    pthread_mutex_lock(&mMutex);
    ipcsession *temp = new ipcsession;
    if (temp != NULL)
    {
        char ipAddress[MAX_IPADDR_LEN] = {0};
        char macAddress[MAX_MACADDR_LEN] = {0};
        sprintf(ipAddress, "%d.%d.%d.%d", reqInfo->ipAddr[3], reqInfo->ipAddr[2], reqInfo->ipAddr[1], reqInfo->ipAddr[0]);
        sprintf(macAddress, "%02x%02x%02x%02x%02x%02x", reqInfo->macAddr[0], reqInfo->macAddr[1], reqInfo->macAddr[2], reqInfo->macAddr[3], reqInfo->macAddr[4], reqInfo->macAddr[5]);
        strlcpy(temp->avfs, srcurl, SRCURL_LEN);
        strlcpy(temp->ipAddress, ipAddress, MAX_IPADDR_LEN);
        strlcpy(temp->macAddress, macAddress, MAX_MACADDR_LEN);
        temp->session = reqInfo->sessionID;
        temp->handle = mPtrStreamingSession;
        temp->isOutofSeq = false;
        temp->isCancelled = false;
        temp->mRetry = false;
        strlcpy(temp->OutofSeqURL, "\0", SRCURL_LEN);
        if (strncmp(srcurl, "avfs://item=", strlen("avfs://item=")) == 0)
            m_sessionID[m_sessionIDptr++] = reqInfo->sessionID;
        list.push_back(temp);
    }
    else
    {
        LOG(DLOGL_ERROR, "Cannot allocate memory for cache.. This session's Details were not stored");
    }
    pthread_mutex_unlock(&mMutex);
}

void MRDvrServer::HandleTerminateSession()
{
    FNLOG(DL_MSP_MRDVR);
    CurrentVideoMgr::instance()->CurrentVideo_RemoveLiveSession();
}

bool MRDvrServer::IsSrvReqValid(tCpeHnSrvMgrMediaServeRequestInfo *reqInfo)
{
    FNLOG(DL_MSP_MRDVR);
    if (reqInfo == NULL || reqInfo->pReqURL == NULL || reqInfo->pURL == NULL)
    {
        LOG(DLOGL_ERROR, "NULL attributes found.");
        return false;
    }
    LOG(DLOGL_REALLY_NOISY, "Valid request");
    return true;
}
/*
Checks if a request is pending and cleans up if there is any junk session in the list
retval	True - if the last request that came in and stored in the list (for a client) is the same as the current request
		False - If the last and current request(for a client) are for different channel
*/

bool MRDvrServer::IsSrvReqPending(tCpeHnSrvMgrMediaServeRequestInfo *reqInfo)
{
    FNLOG(DL_MSP_MRDVR);
    ClientCache::iterator itr;
    char MAC[MAX_MACADDR_LEN] = {0};
    sprintf(MAC, "%02x%02x%02x%02x%02x%02x", reqInfo->macAddr[0], reqInfo->macAddr[1], reqInfo->macAddr[2], reqInfo->macAddr[3], reqInfo->macAddr[4], reqInfo->macAddr[5]);
    LOG(DLOGL_REALLY_NOISY, " Got Srv request for MAC :%s", MAC);

    for (itr = m_ipcsession.begin(); itr != m_ipcsession.end() ; ++itr)
    {
        LOG(DLOGL_REALLY_NOISY, "list size is...%d", m_ipcsession.size());
        if (strcmp((*itr)->macAddress, MAC) == 0)
        {
            LOG(DLOGL_REALLY_NOISY, "Found a MATCH..Check if the request is for the same CDS url (Retry after recovering from an ERROR) ");
            if (strcmp((*itr)->avfs, (char *)reqInfo->pURL) == 0)
            {
                LOG(DLOGL_REALLY_NOISY, "Request from the same client for the same URL.. Chk if prog handle is valid or not to make sure if teardown didnt come or its a rety request");
                if ((((*itr)->handle)->getMediaController()) != NULL)
                {
                    if ((((*itr)->handle)->getMediaController())->getCpeProgHandle() == 0)
                    {
                        LOG(DLOGL_REALLY_NOISY, " Its a retry request... so update the session ID in the list and handle the pending requests");
                        UpdateSessionID((*itr)->session, reqInfo->sessionID);
                        (*itr)->session = reqInfo->sessionID;
                        return true;
                    }
                    else
                    {
                        LOG(DLOGL_NORMAL, "Req from same client for Same URL.. But prev prog handle is not null..Came out of seq..");
                    }
                }
                else
                {
                    LOG(DLOGL_ERROR, " NULL controller");
                }
            }
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, " Ignoring this node since its different client..");
        }
    }
    return false;
}


void MRDvrServer::HandlePendingRequest(tCpeHnSrvMgrMediaServeRequestInfo *reqInfo)
{
    FNLOG(DL_MSP_MRDVR);
    char MAC[MAX_MACADDR_LEN] = {0};
    sprintf(MAC, "%02x%02x%02x%02x%02x%02x", reqInfo->macAddr[0], reqInfo->macAddr[1], reqInfo->macAddr[2], reqInfo->macAddr[3], reqInfo->macAddr[4], reqInfo->macAddr[5]);
    IMediaPlayerSession* pMPSession = getHandleFromMac(MAC);
    if (pMPSession != NULL)
    {
        IMediaController *controller = pMPSession->getMediaController();
        if (controller)
        {
            /*
            * Update the session Id for the pending request and,
            * the controller will decide on respective action based on the previous session state
            */
            LOG(DLOGL_NORMAL, "Going to proceed with serving the recovered session..");
            controller->SetCpeStreamingSessionID(reqInfo->sessionID);
        }

    }
    else
    {
        LOG(DLOGL_ERROR, "No pending session Available");
    }
}

IMediaPlayerSession* MRDvrServer::getHandleFromMac(char* MAC)
{
    FNLOG(DL_MSP_MRDVR);
    ClientCache::iterator temp;
    pthread_mutex_lock(&mMutex);
    for (temp = m_ipcsession.begin(); temp != m_ipcsession.end(); temp++)
    {
        if (strcmp((*temp)->macAddress, MAC) == 0)
        {
            pthread_mutex_unlock(&mMutex);
            return (*temp)->handle;
        }
    }
    pthread_mutex_unlock(&mMutex);
    return NULL;
}

void MRDvrServer::UpdateSessionID(int oldID, int newID)
{
    FNLOG(DL_MSP_MRDVR);
    for (int i = 0; i < m_sessionIDptr; i++)
    {
        if (m_sessionID[i] == oldID)
        {
            m_sessionID[i] = newID;
            LOG(DLOGL_REALLY_NOISY, "Updated old session ID :%d with new session ID :%d", oldID, m_sessionID[i]);
            break;
        }
    }
}

#if PLATFORM_NAME == G8 || PLATFORM_NAME == IP_CLIENT
eUnmanagedDevice_Status MRDvrServer::isUnmanagedDevice(const char *MAC)
{
    eUnmanagedDevice_Status isUnmanagedClient =  kUnmanagedDevice_Status_Error;
    uint32_t arraySize = 0;
    tCsciWebSvcsSysMgrIpClientAttributes *ipClientAttribs = NULL;

    arraySize = Csci_WebSvcs_Gateway_Sysmgr_GetNumOfConnectedIpClients();
    LOG(DLOGL_REALLY_NOISY, "Number of connected IPClients = %d", arraySize);

    if (arraySize == 0)
    {
        isUnmanagedClient = kUnmanagedDevice_Status_Found;
    }
    else
    {
        ipClientAttribs = (tCsciWebSvcsSysMgrIpClientAttributes *)calloc(1, arraySize * sizeof(tCsciWebSvcsSysMgrIpClientAttributes));
        if (ipClientAttribs != NULL)
        {
            eCsciWebSvcsStatus websvc_status = kCsciWebSvcsStatus_Ok;
            websvc_status = Csci_WebSvcs_Gateway_Sysmgr_GetIpClientAttributes(ipClientAttribs, &arraySize);

            if (websvc_status == kCsciWebSvcsStatus_Ok)
            {
                for (uint32_t i = 0; i < arraySize; i++)
                {
                    LOG(DLOGL_REALLY_NOISY, "HN MacAddr found %d) - %s)", i + 1, ipClientAttribs[i].hnMacAddress);
                    if (!strncasecmp(ipClientAttribs[i].hnMacAddress, MAC, MAX_MACADDR_LEN))
                    {
                        isUnmanagedClient = kUnmanagedDevice_Status_NotFound;
                        break;
                    }
                }
                if (isUnmanagedClient != kUnmanagedDevice_Status_NotFound)
                {
                    isUnmanagedClient = kUnmanagedDevice_Status_Found;
                    LOG(DLOGL_ERROR, "Unmanaged device MAC found - %s", MAC);
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "Error in getting ipc attr from webservices - %d", websvc_status);
            }
            free(ipClientAttribs);
            ipClientAttribs = NULL;
        }
        else
        {
            LOG(DLOGL_ERROR, "calloc failed for allocating ipclient attr");
        }
    }
    return isUnmanagedClient;
}

#endif

void MRDvrServer::Retry()
{
    FNLOG(DL_MSP_MRDVR);
    if (conflictSessInfo.isConflict == true && strcmp(conflictSessInfo.URL, "\0") != 0 && strcmp(conflictSessInfo.MAC, "\0") != 0)
    {
        if (conflictSessInfo.OutofSeqCount > 0)
        {
            LOG(DLOGL_NORMAL, "Not retrying now since conflict resolution is not over..Tuner was freed bcoz client changed a channel");
        }
        else
        {
            //Allow the client to retry since a gateway session was cancelled to grant a tuner to client request
            LOG(DLOGL_NORMAL, "Notfiying client to retry");
            sendSseNotification(conflictSessInfo.URL, "TUNER_GRANTED", conflictSessInfo.MAC, kMediaPlayerStatus_Ok, kMediaPlayerSignal_ServiceAuthorized);
            //unset the attributes
            conflictSessInfo.isConflict = false;
            conflictSessInfo.isCancelled = false;
            strlcpy(conflictSessInfo.URL, "\0", SRCURL_LEN);
            strlcpy(conflictSessInfo.MAC, "\0", SRCURL_LEN);
        }
    }
}

bool MRDvrServer::NotifyWarning()
{
    FNLOG(DL_MSP_MRDVR);
    bool retVal = false;//by default false ==> Must return false if all streaming sessions are rec content
    if (m_ipcsession.size() == 0)
        return retVal;//no active clients avail
    ClientCache::iterator temp;
    pthread_mutex_lock(&mMutex);
    for (temp = m_ipcsession.begin(); temp != m_ipcsession.end(); temp++)
    {
        if (strncmp((*temp)->avfs, "avfs://item=", strlen("avfs://item=")) == 0)
        {
            sendSseNotification((*temp)->avfs, "TUNER_WARNING", (*temp)->macAddress, kMediaPlayerStatus_TuningResourceUnavailable, kMediaPlayerSignal_ResourceLost);
            retVal = true;//even if one client has locked to a tuner,return true
        }
    }
    pthread_mutex_unlock(&mMutex);
    return retVal;
}

#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
extern "C" bool Csci_Msp_MrdvrSrv_IsIPClientSession(IMediaPlayerSession *pMPSession)
{
    typedef std::vector <ipcsession*> ClientCache;
    FNLOG(DL_MSP_MRDVR);
    ClientCache::iterator itr_ipc_sessionCache;
    bool is_ipcsession = 0;
    MRDvrServer *instance = MRDvrServer::getInstance();
    if (instance)
    {
        instance->lockMutex();
        for (itr_ipc_sessionCache = MRDvrServer::getInstance()->m_ipcsession.begin(); itr_ipc_sessionCache != MRDvrServer::getInstance()->m_ipcsession.end(); itr_ipc_sessionCache++)
        {
            LOG(DLOGL_NOISE, "Iterating the Imediaplayersessions--(*itr_ipc_sessionCache)->handle=%p", (*itr_ipc_sessionCache)->handle);
            if (pMPSession == (*itr_ipc_sessionCache)->handle)
            {
                is_ipcsession = 1;
                LOG(DLOGL_ERROR, "is_ipcsession = %d", is_ipcsession);
                break;
            }
        }

        instance->unlockMutex();
    }
    return is_ipcsession;
}

#endif

char* MRDvrServer::getOutOfSeqURL(IMediaPlayerSession* handle)
{
    FNLOG(DL_MSP_MRDVR);
    ClientCache::iterator temp;
    pthread_mutex_lock(&mMutex);
    for (temp = m_ipcsession.begin(); temp != m_ipcsession.end(); temp++)
    {
        if ((*temp)->handle == handle)
        {
            LOG(DLOGL_NORMAL, "Going to send retry for out of seq req that resulted in conflict.. URL is:%s", (*temp)->OutofSeqURL);
            pthread_mutex_unlock(&mMutex);
            return (*temp)->OutofSeqURL;
        }
    }
    pthread_mutex_unlock(&mMutex);
    return "";
}

bool MRDvrServer::isRequestOutofSeq(IMediaPlayerSession* handle)
{
    FNLOG(DL_MSP_MRDVR);
    ClientCache::iterator temp;
    pthread_mutex_lock(&mMutex);
    for (temp = m_ipcsession.begin(); temp != m_ipcsession.end(); temp++)
    {
        if ((*temp)->handle == handle)
        {
            pthread_mutex_unlock(&mMutex);
            return (*temp)->isOutofSeq;
        }
    }
    pthread_mutex_unlock(&mMutex);
    return false;
}

bool MRDvrServer::CheckForOutOfSequenceEvents(tCpeHnSrvMgrMediaServeRequestInfo *reqInfo)
{
    FNLOG(DL_MSP_MRDVR);
    ClientCache::iterator itr;
    char MAC[MAX_MACADDR_LEN] = {0};
    sprintf(MAC, "%02x%02x%02x%02x%02x%02x", reqInfo->macAddr[0], reqInfo->macAddr[1], reqInfo->macAddr[2], reqInfo->macAddr[3], reqInfo->macAddr[4], reqInfo->macAddr[5]);
    LOG(DLOGL_REALLY_NOISY, " Got Srv request for MAC :%s", MAC);

    bool retVal = false;//assuming no out of seq events
    eIMediaPlayerStatus streamerStatus = kMediaPlayerStatus_Ok;
    /*
    Iterate through the list and cleanup previous session(s),if request comes out of sequence. Ideally need not have to iterate, if teardown/local teardown is successfull.
    But teardown sometimes never comes when a session goes into error like RF was disconnected. Only on connecting back the teardown is recieved and client might do multiple channel
    changes in the meantime and we must ensure other such channel changes are cleaned up gracefully.
    */

    for (itr = m_ipcsession.begin(); itr != m_ipcsession.end() ;)
    {
        LOG(DLOGL_REALLY_NOISY, "list size is...%d", m_ipcsession.size());
        if (strcmp((*itr)->macAddress, MAC) == 0)
        {
            LOG(DLOGL_REALLY_NOISY, "Streaming session is not yet stopped... trigger teardown.. Request has come out of sequence..");
            IMediaStreamer* ptrIMediaStreamer = IMediaStreamer::getMediaStreamerInstance();
            if (ptrIMediaStreamer)
            {
                streamerStatus = ptrIMediaStreamer->IMediaStreamerSession_StopStreaming((*itr)->handle);
                if (streamerStatus == kMediaPlayerStatus_Ok)
                {
                    /*
                    * Ideally this should not happen, as prev session should have stopped streaming and torn down,
                    * before getting a new  serve request comes. This Might happen when teardown and serve request come out of sequence.
                    * =============================================================================================================================================
                    * TODO: Should i make sure this request is not served now and served only after the earlier session torn down?
                    * This would ensure that there is no conflict if such a scenario happens when all tuner are locked.
                    * Should this request be still served now(may or may not result in conflict) or,
                    * give a "retry" signal hoping that below srvmgr_stop() would clean up the earlier session and retry request would initiate a new serve request
                    * =============================================================================================================================================
                    */
                    LOG(DLOGL_REALLY_NOISY, "IMediaStreamerSession_StopStreaming Success.. Teardown event will be recieved asynchronously");
                    LOG(DLOGL_REALLY_NOISY, "tag this request with the new serve URL for retry");
                    (*itr)->isOutofSeq = true;
                    strlcpy((*itr)->OutofSeqURL, (char *)reqInfo->pURL, SRCURL_LEN); //copy the URL to retry
                    retVal = true;//update this to notify that it was out of seq and also update the cache
                    LOG(DLOGL_REALLY_NOISY, "when teardown comes for this request, client will retry for the URL if earlier req was not handled :%s", (*itr)->OutofSeqURL);
                    ++itr;//inc iterator
                }
                else
                {
                    LOG(DLOGL_ERROR, "StopStreaming() failed..Invoke Local Teardown and Cleanup");
                    pthread_mutex_lock(&mMutex);
                    IMediaPlayerSession* Session = (*itr)->handle;
                    removesess((*itr)->session);
                    ipcsession *temp = *itr;
                    itr = m_ipcsession.erase(itr);//retain the iterator's next position
                    delete temp;
                    temp = NULL;
                    pthread_mutex_unlock(&mMutex);
                    handleLocalTeardown(Session);
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "Null Streamer instance.");
            }
        }
        else
        {
            LOG(DLOGL_NORMAL, "Request for different client..Ignoring this node..");
            ++itr;
        }
    }
    return retVal;
}
bool MRDvrServer::updateAsset(const MultiMediaEvent * pCancelledConflictItem, bool isLoser)
{
    FNLOG(DL_MSP_MRDVR);
    LOG(DLOGL_NORMAL, "%p%d", pCancelledConflictItem, isLoser);
    ClientCache::iterator temp;
    pthread_mutex_lock(&mMutex);
    for (temp = m_ipcsession.begin(); temp != m_ipcsession.end(); temp++)
    {
        if ((*temp)->session == pCancelledConflictItem->sessionID)
        {
            (*temp)->isCancelled = isLoser;
            pthread_mutex_unlock(&mMutex);
            return true;
        }
    }
    pthread_mutex_unlock(&mMutex);
    LOG(DLOGL_NORMAL, "Asset not found in active streaming list.Check if its the one that resulted in conflict");
    if (conflictSessInfo.sessionID == pCancelledConflictItem->sessionID) /*RE-VISIT*/
    {
        conflictSessInfo.isCancelled = isLoser;
        return true;
    }
    LOG(DLOGL_REALLY_NOISY, "Asset not found to be the conflictOwner.Cancelled asset might have been removed[Channel change might have happened]");
    LOG(DLOGL_REALLY_NOISY, "Finding and updating the active service url appropriately based on the MAC address passed");
    const char* currUrl = FindAndUpdateActiveURL(pCancelledConflictItem->macAddress, isLoser);
    if (strncmp(currUrl, "not available", strlen("not available")) == 0)
        return false;
    return true;
}

eCsciMspMrdvrConflictStatus	MRDvrServer::getConflictStatus()
{
    FNLOG(DL_MSP_MRDVR);
    pthread_mutex_lock(&mMutex);
    LOG(DLOGL_NORMAL, "Conflict Status is :%d", mConflictState);
    eCsciMspMrdvrConflictStatus status = mConflictState;
    pthread_mutex_unlock(&mMutex);
    return status;
}

void MRDvrServer::setConflictStatus(eCsciMspMrdvrConflictStatus currentStatus)
{
    FNLOG(DL_MSP_MRDVR);
    pthread_mutex_lock(&mMutex);
    if ((mConflictState == kCsciMspMrdvrConflictStatus_Detected || mConflictState == kCsciMspMrdvrConflictStatus_Busy) && currentStatus == kCsciMspMrdvrConflictStatus_Detected)
    {
        LOG(DLOGL_ERROR, "Conflict detected before earlier conflict is resolved");
        mPrevConflictState = mConflictState;
        mConflictState = kCsciMspMrdvrConflictStatus_Busy;
    }
    else
    {
        if (currentStatus == kCsciMspMrdvrConflictStatus_Resolved)
        {
            mPrevConflictState = currentStatus;
            LOG(DLOGL_NORMAL, "Resetting previous conflict state. No need for further tracking as conflict is resolved");
        }
        mConflictState = currentStatus;
    }
    pthread_mutex_unlock(&mMutex);
    LOG(DLOGL_NORMAL, "Conflict status is :%d Previous status: %d", mConflictState, mPrevConflictState);

}

const char* MRDvrServer::FindAndUpdateActiveURL(const char *MAC, bool isLoser)
{
    FNLOG(DL_MSP_MRDVR);
    ClientCache::iterator temp;
    pthread_mutex_lock(&mMutex);
    LOG(DLOGL_NORMAL, "Mac address queried is:%s is loser:%d", MAC, isLoser);
    for (temp = m_ipcsession.begin(); temp != m_ipcsession.end(); temp++)
    {
        LOG(DLOGL_NORMAL, "List Mac address:%s Url is:%s ", (*temp)->macAddress, (*temp)->avfs);
        if (strncmp((*temp)->macAddress, MAC, strlen(MAC)) == 0 && strncmp((*temp)->avfs, STREAMING_URI_PREFIX , strlen(STREAMING_URI_PREFIX)) == 0)
        {
            (*temp)->isCancelled = isLoser;
            break;
        }
    }
    pthread_mutex_unlock(&mMutex);
    if (temp == m_ipcsession.end())
        return "not available";
    else
        return (*temp)->avfs;
}

bool MRDvrServer::IsLiveStreamingActive()
{
    FNLOG(DL_MSP_MRDVR);
    bool retVal = false;//by default false ==> Must return false if all streaming sessions are rec content
    if (m_ipcsession.size() == 0)
        return retVal;//no active clients avail
    ClientCache::iterator temp;
    pthread_mutex_lock(&mMutex);
    for (temp = m_ipcsession.begin(); temp != m_ipcsession.end(); temp++)
    {
        if (strncmp((*temp)->avfs, "avfs://item=", strlen("avfs://item=")) == 0)
        {
            retVal = true;//even if one client has locked to a tuner,return true
        }
    }
    pthread_mutex_unlock(&mMutex);
    return retVal;
}

MRDvrServer * MRDvrServer::getHandle()
{
    return instance;
}


/**
* @param session[IN] Session Handle associated with a session
* @brief Enable retry flag for this session, so when this session is cleaned up a new retry SSE is sent to set-up a new session .
* @return None
*/

void MRDvrServer::EnableRetry(IMediaPlayerSession* session)
{
    FNLOG(DL_MSP_MRDVR);
    ClientCache::iterator temp;
    pthread_mutex_lock(&mMutex);
    for (temp = m_ipcsession.begin(); temp != m_ipcsession.end(); temp++)
    {
        if ((*temp)->handle == session)
        {
            LOG(DLOGL_NORMAL, "Session :%p is marked to retry ", (*temp)->handle);
            (*temp)->mRetry = true;
            break;
        }
    }
    pthread_mutex_unlock(&mMutex);
}

