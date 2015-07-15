
#if !defined(MspMpEventMgr_H)
#define MspMpEventMgr_H

#include "csci-msp-mediaplayer-api.h"
#include <map>
#include <list>

typedef struct
{
    tCsciMspMpCallbackFunction Callback;
    eCsciMspMpEventType Type;
    void * ClientContext;
} tcallbackData;

typedef std::map<tCsciMspMpRegId, tcallbackData> callbackHandlerMap;

class MspMpEventMgr
{

public:

    static MspMpEventMgr * getMspMpEventMgrInstance();
    MspMpEventMgr();
    ~MspMpEventMgr();
    tCsciMspMpRegId Msp_Mp_RegisterCallback(tCsciMspMpCallbackFunction callback, eCsciMspMpEventType type, void *clientContext);
    eCsciMspStatus  Msp_Mp_UnregisterCallback(tCsciMspMpRegId regId);
    void Msp_Mp_PerformCb(eCsciMspMpEventType type, tCsciMspMpEventSessMsg data);
    void lockMutex(void);
    void unLockMutex(void);


private:

    static MspMpEventMgr *mInstance;
    callbackHandlerMap mCallbackMap;
    tCsciMspMpRegId mRegId;
    pthread_mutex_t  mMutex;
};

#endif

