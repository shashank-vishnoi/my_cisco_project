#include "MspMpEventMgr.h"
#include <pthread.h>

MspMpEventMgr * MspMpEventMgr::mInstance = NULL;

MspMpEventMgr::MspMpEventMgr()
{
    mRegId = 0;
    pthread_mutex_init(&mMutex, NULL);
}

MspMpEventMgr::~MspMpEventMgr()
{
    pthread_mutex_destroy(&mMutex);
}

void MspMpEventMgr::lockMutex(void)
{
    pthread_mutex_lock(&mMutex);
}

void MspMpEventMgr::unLockMutex(void)
{
    pthread_mutex_unlock(&mMutex);
}

MspMpEventMgr * MspMpEventMgr::getMspMpEventMgrInstance()
{
    if (mInstance == NULL)
    {
        mInstance = new MspMpEventMgr();
    }

    return mInstance;
}

tCsciMspMpRegId MspMpEventMgr::Msp_Mp_RegisterCallback(tCsciMspMpCallbackFunction callback, eCsciMspMpEventType type, void *clientContext)
{
    callbackHandlerMap:: iterator iter;

    for (iter = mCallbackMap.begin(); iter != mCallbackMap.end(); iter++)
    {
        if (((*iter).second.Type == type) && ((*iter).second.ClientContext == clientContext))
        {
            return (*iter).first;
        }
    }

    tcallbackData data;
    data.Callback = callback;
    data.Type = type;
    data.ClientContext = clientContext;
    mRegId++;

    mCallbackMap[mRegId] = data;

    return mRegId;
}
eCsciMspStatus  MspMpEventMgr::Msp_Mp_UnregisterCallback(tCsciMspMpRegId regId)
{
    eCsciMspStatus status = kCsciMspStatus_Failed;
    callbackHandlerMap:: iterator iter;

    iter = 	mCallbackMap.find(regId);

    if (iter != mCallbackMap.end())
    {
        mCallbackMap.erase(iter);
        status = kCsciMspStatus_OK;;
    }

    return status;
}

void MspMpEventMgr::Msp_Mp_PerformCb(eCsciMspMpEventType type, tCsciMspMpEventSessMsg data)
{
    lockMutex();
    callbackHandlerMap:: iterator iter;

    std::list<tCsciMspMpRegId> tempRegIdList;

    for (iter = mCallbackMap.begin(); iter != mCallbackMap.end(); iter++)
    {
        if ((*iter).second.Type == type)
        {
            tempRegIdList.push_back((*iter).first);
        }
    }

    std::list<tCsciMspMpRegId>::iterator listIter;

    for (listIter = tempRegIdList.begin(); listIter != tempRegIdList.end(); listIter++)
    {
        iter = mCallbackMap.find((*listIter));

        if (iter != mCallbackMap.end())
        {
            tCsciMspMp_CbData cbData;
            cbData.ownerContext = (*iter).second.ClientContext;
            cbData.eventType = (*iter).second.Type;
            cbData.eventSessMsg = data;
            unLockMutex();
            (*iter).second.Callback(cbData);
            lockMutex();
        }
    }

    unLockMutex();
}

tCsciMspMpRegId Csci_Msp_Mp_RegisterCallback(tCsciMspMpCallbackFunction callback, eCsciMspMpEventType type, void *clientContext)
{
    tCsciMspMpRegId regId = 0;

    MspMpEventMgr * eventMgr = MspMpEventMgr::getMspMpEventMgrInstance();
    if (eventMgr)
    {
        eventMgr->lockMutex();
        regId = eventMgr->Msp_Mp_RegisterCallback(callback, type, clientContext);
        eventMgr->unLockMutex();
    }

    return regId;
}

eCsciMspStatus Csci_Msp_Mp_UnregisterCallback(tCsciMspMpRegId regId)
{
    eCsciMspStatus status = kCsciMspStatus_Failed;

    MspMpEventMgr * eventMgr = MspMpEventMgr::getMspMpEventMgrInstance();
    if (eventMgr)
    {
        eventMgr->lockMutex();
        status = eventMgr->Msp_Mp_UnregisterCallback(regId);
        eventMgr->unLockMutex();
    }

    return status;
}

