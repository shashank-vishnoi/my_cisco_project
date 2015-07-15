#include <cstddef>
#include <string.h>
#include "CurrentVideoMgr.h"

#define UNUSED_PARAM(a) (void)a;

CurrentVideoMgr* CurrentVideoMgr::cvInstance = NULL;

CurrentVideoMgr::CurrentVideoMgr()
{
}

CurrentVideoMgr* CurrentVideoMgr::instance()
{
    return NULL;
}

int CurrentVideoMgr::CurrentVideo_AddLiveSession(void* pLsi)
{
    UNUSED_PARAM(pLsi);
    FNLOG(DL_MSP_MRDVR);
    return 0;
}

int CurrentVideoMgr::CurrentVideo_RemoveLiveSession()
{
    FNLOG(DL_MSP_MRDVR);
    return 0;
}

int CurrentVideoMgr::CurrentVideo_SetAttribute(void* pAttrName, void* attrValue)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(pAttrName);
    UNUSED_PARAM(attrValue);
    return 0;
}

int CurrentVideoMgr::CurrentVideo_Init()
{
    FNLOG(DL_MSP_MRDVR);
    return 0;
}

void CurrentVideoMgr::CurrentVideo_setClientIP(uint32_t clientIP)
{
    UNUSED_PARAM(clientIP);
    return;
}

uint32_t CurrentVideoMgr::CurrentVideo_getClientIP()
{
    return 0;
}

void CurrentVideoMgr::CurrentVideo_SetTsbFileName(char* tsbFileName)
{
    UNUSED_PARAM(tsbFileName);
    return;
}

char* CurrentVideoMgr::CurrentVideo_GetTsbFileName()
{
    return NULL;
}

void CurrentVideoMgr::setCurrentVideoCpeSrcHandle(ServeSessionInfo* tcpeHdl)
{
    UNUSED_PARAM(tcpeHdl);
    return;
}

ServeSessionInfo* CurrentVideoMgr::getCurrentVideoCpeSrcHandle()
{
    return NULL;
}

void CurrentVideoMgr::setHandleTearDownReceived(bool val)
{
    UNUSED_PARAM(val);
}

bool CurrentVideoMgr::getHandleTearDownReceived()
{
    return true;
}

bool CurrentVideoMgr::CurrentVideo_IsCurrentVideoStreaming()
{
    return true;
}

void CurrentVideoMgr::CurrentVideo_SetCurrentVideoStreaming(bool val)
{
    UNUSED_PARAM(val);
}

void
CurrentVideoMgr::CurrentVideo_StopStreaming()
{
}

int CurrentVideoMgr::CurrentVideo_SetCCIAttribute(CCIData& data)
{
    UNUSED_PARAM(data);
    return 0;
}

int CurrentVideoMgr::CurrentVideo_SetServiceAttribute(bool* bp)
{
    UNUSED_PARAM(bp);
    return 0;
}

int CurrentVideoMgr::CurrentVideo_SetOutputAttribute(bool* bp)
{
    UNUSED_PARAM(bp);
    return 0;
}

int CurrentVideoMgr::AddLiveSession(CurrentVideoData& data)
{
    UNUSED_PARAM(data);
    return 0;
}

int CurrentVideoMgr::RegisterTerminateSessionCB(void*)
{
    return kCpe_NoErr;
}

uint32_t CurrentVideoMgr::CurrentVideo_getLiveSessionId()
{
    return 0;
}
int  CurrentVideoMgr::UnregisterTerminateSessionCB(void)
{
    return 0;
}
