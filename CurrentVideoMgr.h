/***************************************************/
/* Class:   CurrentVideoMgr                        */
/* Purpose: This class encapsulates all the APIs   */
/*          of current video feature.              */
/* Scope:   Singleton                              */
/***************************************************/
#include "cpe_programhandle.h"
#include <dlog.h>
#include "mrdvrserver.h"
#include "IMediaController.h"
#include "avpm.h"
#include "dvr.h"
#define TSB_MAX_FILENAME_SIZE 25
#define UNUSED_PARAM(a) (void)a;

using namespace std;

class CurrentVideoMgr
{
private:
    bool m_isCurrentVideoAdded;
    uint32_t m_liveSessId;
    char m_tsbFileName[TSB_MAX_FILENAME_SIZE];
    static CurrentVideoMgr* cvInstance;
    ServeSessionInfo* mCurrentVideoCpeSrcHandle;
    CurrentVideoMgr();
    CurrentVideoMgr(const CurrentVideoMgr& cvm);
    CurrentVideoMgr& operator=(const CurrentVideoMgr& cvm);
    bool bTearDownRcvd;
    bool isStreamingOnClient;
    uint32_t m_clientIP;

public:
    static CurrentVideoMgr* instance();
    int CurrentVideo_AddLiveSession(void* pLsi);
    int CurrentVideo_RemoveLiveSession();
    int CurrentVideo_SetAttribute(void* pAttrName, void* attrValue);
    int CurrentVideo_Init();
    void CurrentVideo_SetTsbFileName(char* tsbFileName);
    char* CurrentVideo_GetTsbFileName();
    void setCurrentVideoCpeSrcHandle(ServeSessionInfo* tcpeHdl);
    ServeSessionInfo* getCurrentVideoCpeSrcHandle();
    void setHandleTearDownReceived(bool val);
    bool getHandleTearDownReceived();
    bool CurrentVideo_IsCurrentVideoStreaming();
    void CurrentVideo_SetCurrentVideoStreaming(bool val);
    void CurrentVideo_StopStreaming();
    int CurrentVideo_SetCCIAttribute(CCIData& data);
    int CurrentVideo_SetServiceAttribute(bool* bp);
    int AddLiveSession(CurrentVideoData& data);
    int CurrentVideo_SetOutputAttribute(bool* bp);
    int RegisterTerminateSessionCB(void*);
    int UnregisterTerminateSessionCB();
    void CurrentVideo_setClientIP(uint32_t);
    uint32_t CurrentVideo_getClientIP();
    uint32_t CurrentVideo_getLiveSessionId();
};
