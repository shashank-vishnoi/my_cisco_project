#include <cstddef>
#include <string.h>
#include "CurrentVideoMgr.h"
//#include "IMediaController.h"
#include <cpeutil_hnplatlive.h>
#define WAIT_TEARDOWNREQUEST 40  // 4 secs
CurrentVideoMgr* CurrentVideoMgr::cvInstance = NULL;

CurrentVideoMgr::CurrentVideoMgr(): m_isCurrentVideoAdded(false), m_liveSessId(0), mCurrentVideoCpeSrcHandle(NULL), bTearDownRcvd(false), isStreamingOnClient(false), m_clientIP(0)
{
    m_tsbFileName[0] = '\0';
}

CurrentVideoMgr* CurrentVideoMgr::instance()
{
    if (cvInstance == NULL)
    {
        cvInstance = new CurrentVideoMgr();
    }
    return cvInstance;
}

int CurrentVideoMgr::CurrentVideo_AddLiveSession(void* pLsi)
{
    FNLOG(DL_MSP_MRDVR);
    tCpeUtilHnPlatLiveSessionInfo* lsi = (tCpeUtilHnPlatLiveSessionInfo*)pLsi;
    int retVal = -1;
    if (m_liveSessId == 0)
    {
        retVal = cpeutil_hnplatlive_AddLiveSession(lsi, &m_liveSessId);
        if (kCpe_NoErr == retVal)
        {
            dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "Added current video with session id %d to CDS. The TSB file for current video is %s", m_liveSessId, lsi->recFileName);
            strncpy(m_tsbFileName, lsi->recFileName, TSB_MAX_FILENAME_SIZE);
            m_tsbFileName[TSB_MAX_FILENAME_SIZE - 1] = '\0';
            setHandleTearDownReceived(false);
        }
        else
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "Adding current video with session id %d to CDS failed with error code %d", m_liveSessId, retVal);
        }
    }
    else
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "Live Session already present, not adding again.");
    }
    return retVal;
}

int CurrentVideoMgr::CurrentVideo_RemoveLiveSession()
{
    FNLOG(DL_MSP_MRDVR);
    int retVal = -1;
    if (m_liveSessId > 0)
    {
        retVal = cpeutil_hnplatlive_RemoveLiveSession(m_liveSessId);
        if (kCpe_NoErr == retVal)
        {
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "Removed current video with session id %d from CDS", m_liveSessId);
            m_liveSessId = 0;
            m_tsbFileName[0] = '\0';
            // Stop streaming current video
            if (CurrentVideo_IsCurrentVideoStreaming())
            {
                CurrentVideo_StopStreaming();
            }
            else
            {
                dlog(DL_MSP_MRDVR, DLOGL_NOISE, "No need to stop current video, it is not being streamed.");
            }
            //Fix for CSCum40690 - Reboot in G8 mrdvr server
            //current video clean up
            setCurrentVideoCpeSrcHandle(NULL);
            CurrentVideo_setClientIP(0);
        }
        else
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "Removing current video with session id %d to CDS failed with error code %d", m_liveSessId, retVal);
        }
    }
    else
    {
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "No current video asset added. Hence nothing to remove");
    }
    return retVal;
}

int CurrentVideoMgr::CurrentVideo_SetAttribute(void* pAttrName, void* attrValue)
{
    FNLOG(DL_MSP_MRDVR);
    tCpeUtilHnPlatLiveAttribute* attrName = (tCpeUtilHnPlatLiveAttribute*)pAttrName;
    int retVal = -1;

    retVal = cpeutil_hnplatlive_Set(m_liveSessId, *attrName, attrValue);

    if (kCpe_NoErr == retVal)
    {
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "Setting attribute for current video successful");
    }
    else
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "Set attrivute for current video failed with error code %d", retVal);
    }
    return retVal;
}

int CurrentVideoMgr::CurrentVideo_Init()
{
    FNLOG(DL_MSP_MRDVR);
    int retval = -1;
    retval = cpeutil_hnplatlive_Init();
    if (kCpe_NoErr == retval)
    {
        dlog(DL_MSP_MRDVR, DLOGL_NORMAL, "Current video Init successfull");
    }
    else
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "Current Video Init failed with error code %d", retval);
    }
    return retval;
}

void CurrentVideoMgr::CurrentVideo_setClientIP(uint32_t clientIP)
{
    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "Setting IP address of current video client");
    m_clientIP = clientIP;
}

uint32_t CurrentVideoMgr::CurrentVideo_getClientIP()
{
    return m_clientIP;
}

void CurrentVideoMgr::CurrentVideo_SetTsbFileName(char* tsbFileName)
{
    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "The TSB file for the current video is %s", tsbFileName);
    strncpy(m_tsbFileName, tsbFileName, TSB_MAX_FILENAME_SIZE);
    m_tsbFileName[TSB_MAX_FILENAME_SIZE - 1] = '\0';
}

char* CurrentVideoMgr::CurrentVideo_GetTsbFileName()
{
    return m_tsbFileName;
}

void CurrentVideoMgr::setCurrentVideoCpeSrcHandle(ServeSessionInfo* tcpeHdl)
{
    mCurrentVideoCpeSrcHandle = tcpeHdl;
}

ServeSessionInfo* CurrentVideoMgr::getCurrentVideoCpeSrcHandle()
{
    return mCurrentVideoCpeSrcHandle;
}

void CurrentVideoMgr::setHandleTearDownReceived(bool val)
{
    FNLOG(DL_MSP_MRDVR);
    bTearDownRcvd = val;
    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "TEAR DOWN SET TO %d", val);
}

bool CurrentVideoMgr::getHandleTearDownReceived()
{
    return bTearDownRcvd;
}

bool CurrentVideoMgr::CurrentVideo_IsCurrentVideoStreaming()
{
    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "isStreamingOnClient = %d", isStreamingOnClient);
    return isStreamingOnClient;
}

void CurrentVideoMgr::CurrentVideo_SetCurrentVideoStreaming(bool val)
{
    dlog(DL_MSP_MRDVR, DLOGL_NORMAL, "setting isStreamingOnClient to = %d", val);
    isStreamingOnClient = val;
}

void CurrentVideoMgr::CurrentVideo_StopStreaming()
{
    FNLOG(DL_MSP_MRDVR);

    CurrentVideoMgr* currentVideo = CurrentVideoMgr::instance();
    if (CurrentVideo_IsCurrentVideoStreaming())
    {
        if (currentVideo->getCurrentVideoCpeSrcHandle() && currentVideo->getCurrentVideoCpeSrcHandle()->mPtrStreamingSession->getMediaController()->getCpeProgHandle())
        {
            if (currentVideo->getCurrentVideoCpeSrcHandle()->mPtrStreamingSession->getMediaController()->getCpeProgHandle())
            {
                dlog(DL_MSP_MRDVR, DLOGL_SIGNIFICANT_EVENT, "TeardownRequest is for current video.");
                if (currentVideo->CurrentVideo_IsCurrentVideoStreaming())
                {
                    dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "getCurrentVideoCpeSrcHandle()->ptrSource->getCpeProgHandle() is not null");
                    int cpeStatus = cpe_hnsrvmgr_Stop(currentVideo->getCurrentVideoCpeSrcHandle()->mPtrStreamingSession->getMediaController()->getCpeProgHandle());
                    if (cpeStatus != kCpe_NoErr)
                    {
                        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "MRDVR HN server stop failed with error code %d", cpeStatus);
                    }
                    CurrentVideo_SetCurrentVideoStreaming(false);
                    unsigned int timeout_ms = 0;
                    while ((timeout_ms < WAIT_TEARDOWNREQUEST) && (getHandleTearDownReceived() != true))
                    {
                        timeout_ms += 1;  //polling up for every 100 mS
                        dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "Did not receive tear down callback. Sleeping...: %d", timeout_ms);
                        usleep(100000);
                    }
                    if (getHandleTearDownReceived())
                    {
                        dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "Tear down request received.");
                        m_clientIP = 0;
                    }
                    else
                    {
                        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "Tear down request not received!!!!!!!!!!!!!!!!");
                    }
                    setHandleTearDownReceived(false);
                }
            }
        }
        else
        {
            dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "TeardownRequest is NOT for current video.");
        }
    }
    else
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "Current Video is not being streamed, no need to stop.");
    }
}

int CurrentVideoMgr::CurrentVideo_SetCCIAttribute(CCIData& data)
{
    FNLOG(DL_MSP_MRDVR);
    tCpeUtilHnPlatLiveCCI hnplCCI;
    tCpeUtilHnPlatLiveAttribute hnplCCIAttribute = eCpeUtilHnPlatLive_CCI;
    if (data.emi == COPY_FREELY)
    {
        hnplCCI = eCpeUtilHnPlatLive_CCI_COPY_FREELY;
    }
    else if (data.emi == COPY_NO_MORE)
    {
        hnplCCI = eCpeUtilHnPlatLive_CCI_COPY_NOMORE;
    }
    else if (data.emi == COPY_ONCE)
    {
        hnplCCI = eCpeUtilHnPlatLive_CCI_COPY_ONCE;
    }
    else if (data.emi == COPY_NEVER)
    {
        hnplCCI = eCpeUtilHnPlatLive_CCI_COPY_NEVER;
    }
    else
    {
        hnplCCI = eCpeUtilHnPlatLive_CCI_COPY_INVALID;
    }
    return CurrentVideoMgr::instance()->CurrentVideo_SetAttribute(&hnplCCIAttribute, &hnplCCI);
}

int CurrentVideoMgr::CurrentVideo_SetServiceAttribute(bool* bp)
{
    FNLOG(DL_MSP_MRDVR);
    tCpeUtilHnPlatLiveAttribute hnplCCIAttribute = eCpeUtilHnPlatLive_ServiceDeauthorized;
    return CurrentVideoMgr::instance()->CurrentVideo_SetAttribute(&hnplCCIAttribute, bp);
}

int CurrentVideoMgr::CurrentVideo_SetOutputAttribute(bool* bp)
{
    FNLOG(DL_MSP_MRDVR);
    tCpeUtilHnPlatLiveAttribute hnplCCIAttribute = eCpeUtilHnPlatLive_Output;
    return CurrentVideoMgr::instance()->CurrentVideo_SetAttribute(&hnplCCIAttribute, bp);
}

int CurrentVideoMgr::AddLiveSession(CurrentVideoData& data)
{
    int retVal = -1;
    tCpeUtilHnPlatLiveSessionInfo liveSessInfo;
    liveSessInfo.cciVal = eCpeUtilHnPlatLive_CCI_COPY_ONCE;
    liveSessInfo.mediaHandle = data.mediaHandle;
    liveSessInfo.recFileName = data.recFileName;
    liveSessInfo.recordingHandle = data.recordingHandle;
    liveSessInfo.srcHandle = data.srcHandle;
    liveSessInfo.sessionType = eCpeUtilHnPlatLive_sTypeMain;

    if (!CurrentVideo_AddLiveSession((void*)&liveSessInfo))
    {
        return retVal;
    }
    return retVal;
}

static int ServerManagerTerminateCallback(tCpeUtilHnPlatLiveCallbackTypes type, void* userdata, void* pCallbackSpecific)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(userdata)
    int ret = -1;
    MRDvrServer*     inst        = (MRDvrServer*)userdata;
    if (inst)
    {
        tMrdvrSrvEventType evtType = (tMrdvrSrvEventType)type;
        dlog(DL_MSP_MRDVR, DLOGL_NORMAL, " dispatch event: %d", evtType);
        if (type == eCpeUtilHnPlatLiveCallbackType_TerminateSession)
        {
            evtType = kMrdvrTerminateSessionEvent;
        }
        dlog(DL_MSP_MRDVR, DLOGL_NORMAL, " dispatch event: %d", evtType);
        inst->threadEventQueue->dispatchEvent(evtType, pCallbackSpecific);
        ret = 0;
    }
    else
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, " No MRDvrServer instance");
    }
    return ret;
}

int CurrentVideoMgr::RegisterTerminateSessionCB(void* mrdvrServer)
{
    if (kCpe_NoErr != cpeutil_hnplatlive_RegisterCallback(eCpeUtilHnPlatLiveCallbackType_TerminateSession, mrdvrServer, ServerManagerTerminateCallback))
    {
        dlog(DL_MSP_MRDVR,  DLOGL_ERROR, "Serve Teardown Callback Reg Failed");
        return kMRDvrServer_Error;
    }
    return kCpe_NoErr;
}

int CurrentVideoMgr::UnregisterTerminateSessionCB()
{
    if (kCpe_NoErr != cpeutil_hnplatlive_UnregisterCallback(eCpeUtilHnPlatLiveCallbackType_TerminateSession))
    {
        dlog(DL_MSP_MRDVR,  DLOGL_ERROR, "Serve Teardown Callback UnRegister Failed");
        return kMRDvrServer_Error;
    }
    return kCpe_NoErr;
}

uint32_t CurrentVideoMgr::CurrentVideo_getLiveSessionId()
{
    return m_liveSessId;
}
