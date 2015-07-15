/** @file VOD_SessionControl.cpp
 *
 * @brief VOD SessionControl abstract class implementation.
 *
 * @author Amit Patel
 *
 * @version 1.0
 *
 * @date 12.27.2010
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */
#include <assert.h>
#include <ctime>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "dlog.h"
#include "VOD_SessionControl.h"
#include "vod.h"


#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_ONDEMAND, level,"VOD_SessCntl(TID:%lx):%s:%d " msg, pthread_self(), __FUNCTION__, __LINE__, ##args);
#define UNUSED_PARAM(a) (void)a;

int              VOD_SessionControl::mPrevVodSessionId;
ActiveVodSessionMap  VOD_SessionControl::mActiveVodSessionMap;
pthread_mutex_t  VOD_SessionControl::mVodSessionListMutex;
bool             VOD_SessionControl::mVodSessionListInitialized;
int32_t          VOD_SessionControl::mSocketFd = -1;

VOD_SessionControl::VOD_SessionControl(OnDemand *pOnDemand, eOnDemandReqType reqType)
{

    if (!mVodSessionListInitialized)
    {
        pthread_mutex_init(&mVodSessionListMutex, NULL);
        mVodSessionListInitialized = true;
    }


    pthread_mutex_lock(&mVodSessionListMutex);
    if (mPrevVodSessionId > 0)
    {
        mPrevVodSessionId++;
    }
    else
    {
        srand(time(0));
        mPrevVodSessionId =  rand() % 100;
    }
    mVodSessionId = mPrevVodSessionId;
    mActiveVodSessionMap[mVodSessionId] = pOnDemand;
    LOG(DLOGL_NORMAL, "mVodSessionId:%d pOnDemand:%p ", mVodSessionId, pOnDemand);

    pthread_mutex_unlock(&mVodSessionListMutex);

    LOG(DLOGL_NORMAL, "mVodSessionId: %d  started", mVodSessionId);

    this->reqType = reqType;
    this->ptrOnDemand = pOnDemand;
    keepAliveTimeOut = RTSP_KEEP_ALIVE_TIME_OUT_DEFAULT; //three minutes
    StreamServerIPAdd = NULL;
    //get mac id of set-op box
    OnDemandSystemClient::getInstance()->GetStbMacAddressAddress(stbMacAddr);

    //get srm port id
    OnDemandSystemClient::getInstance()->GetSrmPort(&srmPort);
    LOG(DLOGL_NORMAL, "srmPort:%d ", srmPort);

    if (mSocketFd == -1)
    {
        mSocketFd = VodDsmcc_Base::GetSocket(srmPort);
    }


    //get srm ip address
    OnDemandSystemClient::getInstance()->GetSrmIpAddress(&srmIpAddr);
    LOG(DLOGL_NORMAL, "srmIpAddr: %x ", srmIpAddr);

    mCakresp = 0;
    StreamServerPort = 0;
    StreamHandle = 0;
    mFrequency = 0;
    mMode = eCpeSrcRFMode_QAM64;
    mSymbolRate = 0;
    mSourceId = 0;
    mProgramNumber = 0;
    endPosition = 0;
    mTransSessionId = 0;
    caEID[EID_SIZE - 1] = '\0';
    mTransId[TRANSID_SIZE - 1] = '\0';
    mEncrypted = false;
}

VOD_SessionControl::~VOD_SessionControl()
{
    LOG(DLOGL_REALLY_NOISY, " destruct mVodSessionId: %d ", mVodSessionId);

    pthread_mutex_lock(&mVodSessionListMutex);


    mActiveVodSessionMap.erase(mVodSessionId);


    LOG(DLOGL_REALLY_NOISY, "sendMsgInfoList.size(): %d", sendMsgInfoList.size());

    EventLoop* evtLoop = ptrOnDemand->GetEventLoop();

    // Remove all event timers and messages from send list
    if (!sendMsgInfoList.empty())
    {
        list<SendMsgInfo *>::iterator itr;
        itr = sendMsgInfoList.begin();
        while (itr != sendMsgInfoList.end())
        {
            EventTimer *evt = (*itr)->evtTimer;
            LOG(DLOGL_REALLY_NOISY, "evtLoop->delTimer: %p", evt);
            if (evtLoop)
            {
                evtLoop->delTimer(evt);
            }
            delete *itr;
            itr = sendMsgInfoList.erase(itr);
        }
    }

    pthread_mutex_unlock(&mVodSessionListMutex);
    LOG(DLOGL_REALLY_NOISY, "exit");
}

int32_t VOD_SessionControl::GetFD()
{
    FNLOG(DL_MSP_ONDEMAND);
    if (mSocketFd == -1)
    {
        uint16_t port;
        OnDemandSystemClient::getInstance()->GetSrmPort(&port);
        LOG(DLOGL_NORMAL, "port: %d", port);

        mSocketFd = VodDsmcc_Base::GetSocket(port);
        LOG(DLOGL_NORMAL, "mSocketFd: %d", mSocketFd);

    }
    return mSocketFd;
}

unsigned int  VOD_SessionControl::getSessionNumber(void  *ptrMessage)
{
    VodDsmcc_Base * message = (VodDsmcc_Base *)ptrMessage;
    unsigned char *ptrSessId = NULL;
    unsigned int sessId = 0;

    if (NULL == message)
    {
        LOG(DLOGL_ERROR, "passed message as NULL");
        return ON_DEMAND_ERROR;
    }

    LOG(DLOGL_NOISE, "response message id: %d", message->GetMessageId());
    switch (message->GetMessageId())
    {
    case dsmcc_ClientSessionSetUpConfirm:
    {
        VodDsmcc_ClientSessionConfirm *sessConfirm = (VodDsmcc_ClientSessionConfirm *) message;
        ptrSessId =  sessConfirm->GetSessionId().GetSessionId();
    }
    break;
    case dsmcc_ClientReleaseIndication:
    case dsmcc_ClientReleaseConfirm:
    {
        VodDsmcc_ClientReleaseGeneric *releaseGeneric = (VodDsmcc_ClientReleaseGeneric *) message;
        ptrSessId =  releaseGeneric->GetSessionId().GetSessionId();
    }
    break;

    default:
        LOG(DLOGL_ERROR, "un-handled message id: %d", message->GetMessageId());
        break;
    }

    LOG(DLOGL_MINOR_EVENT, "ptrSessId:%p", ptrSessId);
    if (ptrSessId)
    {
        Utils::Get4Byte(ptrSessId + MAC_ID_SIZE, &sessId);
    }

    LOG(DLOGL_MINOR_EVENT, "sessId:%d", sessId);
    return sessId;
}


void VOD_SessionControl::ReadSessionSocketCallback(int32_t fd, int16_t event, void *arg)
{
    FNLOG(DL_MSP_ONDEMAND);

    UNUSED_PARAM(fd)
    UNUSED_PARAM(event)
    UNUSED_PARAM(arg)

    uint8_t *msgData = NULL;
    uint32_t msgLen = 0;
    VodDsmcc_Base *dsmccObj = NULL;

    LOG(DLOGL_NOISE, " session response received on fd:%d ", fd);

    int sockFd = GetFD();
    if (sockFd != -1)
    {
        bool readStatus = VodDsmcc_Base::ReadMessageFromSocket(sockFd, &msgData,
                          &msgLen);
        if (readStatus)
        {
            LOG(DLOGL_NORMAL, "%s:%d  msgLen:%d ", __FUNCTION__, __LINE__, msgLen);
            //get dsmcc response message type
            dsmccObj = VodDsmcc_Base::GetMessageTypeObject(msgData, msgLen);

            if (dsmccObj)
            {
                //parse dsmcc response
                status parseStatus = dsmccObj->ParseDsmccMessageBody(msgData, msgLen);
                if (parseStatus == E_TRUE)
                {

                    LOG(DLOGL_REALLY_NOISY, "DSMCC- message id:%x : transId %x ", dsmccObj->GetMessageId(), dsmccObj->GetTransactionId());

                    bool found = false;
                    unsigned int sessId = VOD_SessionControl::getSessionNumber(dsmccObj);
                    ActiveVodSessionMap::iterator itr;

                    for (itr = mActiveVodSessionMap.begin(); itr != mActiveVodSessionMap.end(); ++itr)
                    {
                        LOG(DLOGL_NOISE, " itr->first %d ", itr->first);
                        if (itr->first == sessId)
                        {
                            found = true;
                            break;
                        }
                    }

                    LOG(DLOGL_NOISE, "sessId: %d  found: %d", sessId, found);
                    if (found)
                    {
                        itr->second->handleReadSessionData(dsmccObj);
                    }
                }
                else
                {
                    LOG(DLOGL_ERROR, " ParseDsmccMessageBody failed");
                }
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "warning ReadMessageFromSocket failed");
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Invalid sockFd ");
    }
}



string VOD_SessionControl::GetStreamControlUrl()
{
    return streamControlURL;
}


uint32_t VOD_SessionControl::GetKeepAliveTimeOut()
{
    //According to the spec, the interval time out should be equal to 80% of the timeout parameter
    return ((uint32_t)(0.8 * keepAliveTimeOut));
}


eIOnDemandStatus VOD_SessionControl::AddMessageToSendList(SendMsgInfo *msgInfo)
{
    //create a timer call back
    uint32_t tMsg = 0;

    pthread_mutex_lock(&mVodSessionListMutex);

    EventLoop* evtLoop = ptrOnDemand->GetEventLoop();
    OnDemandSystemClient::getInstance()->GetmessageTimer(&tMsg);

    //if retry count is more then one then add message to send list
    if ((evtLoop != NULL) && (msgInfo->msgRetryCount > 0))
    {
        EventTimer* evtTimer = evtLoop->addTimer(EVENTTIMER_TIMEOUT,
                               tMsg,
                               0,
                               VOD_SessionControl::ProcessTimeoutCallBack, (void *)msgInfo);


        LOG(DLOGL_REALLY_NOISY, "evtLoop->addTimer: evtTimer: %p  transId: %d time: %d secs", evtTimer, msgInfo->transId,  tMsg);

        // add to queue
        msgInfo->evtTimer = evtTimer;
        msgInfo->objPtr = this;
        msgInfo->ptrOnDemand = ptrOnDemand;
        msgInfo->sessionId = mVodSessionId;
        LOG(DLOGL_REALLY_NOISY, " msginfo:%p  evtTimer: %p ", msgInfo,  msgInfo->evtTimer);
        sendMsgInfoList.push_back(msgInfo);

        LOG(DLOGL_REALLY_NOISY, "sendMsgInfoList.size(): %d", sendMsgInfoList.size());
    }
    else
    {
        delete msgInfo;
    }


    pthread_mutex_unlock(&mVodSessionListMutex);

    return ON_DEMAND_OK;
}

void VOD_SessionControl::RemoveMessageFromSendList(SendMsgInfo *msgInfo)
{
    pthread_mutex_lock(&mVodSessionListMutex);
    LOG(DLOGL_REALLY_NOISY, "RemoveMessageFromSendList sendMsgInfoList.size(): %d", sendMsgInfoList.size());

    if (!sendMsgInfoList.empty())
    {
        list<SendMsgInfo *>::iterator itr;
        for (itr = sendMsgInfoList.begin(); itr != sendMsgInfoList.end(); ++itr)
        {
            if ((*itr)->transId == msgInfo->transId)
            {
                EventLoop* evtLoop = ptrOnDemand->GetEventLoop();
                if (evtLoop)
                {
                    evtLoop->delTimer((*itr)->evtTimer);
                }
                LOG(DLOGL_REALLY_NOISY, "deleting Message with address: %p", *itr);
                delete *itr;
                *itr = NULL;

                sendMsgInfoList.erase(itr);
                break;
            }
        }
    }

    pthread_mutex_unlock(&mVodSessionListMutex);
}


void VOD_SessionControl::ProcessTimeoutCallBack(int32_t fd, short event, void *arg)
{
    (void) fd;
    (void) event;

    pthread_mutex_lock(&mVodSessionListMutex);

    EventTimer* pEvt = (EventTimer*) arg;
    if (NULL != pEvt)
    {

        SendMsgInfo *msgInfo = (SendMsgInfo *) pEvt->getUserData();
        if (NULL != msgInfo)
        {
            unsigned int sessId = msgInfo->sessionId;

            LOG(DLOGL_REALLY_NOISY, " MessageTimeoutCallBack : %d ", msgInfo->transId);

            // TODO: this is dangerous because VOD_SessionControl object may be gone
            //       Is there way for onDemand controller to handle this?

            ActiveVodSessionMap::iterator it;
            bool found = false;

            for (it = mActiveVodSessionMap.begin(); it != mActiveVodSessionMap.end(); ++it)
            {
                if (it->first == sessId)
                {
                    found = true;
                    break;
                }
            }

            LOG(DLOGL_MINOR_EVENT, "sessId: %d  found: %d", sessId, found);

            if (found)
            {
                //if message exist in queue then resend
                list<SendMsgInfo *>::iterator itr;

                VOD_SessionControl *objPtr = msgInfo->objPtr;
                for (itr = objPtr->sendMsgInfoList.begin(); itr != objPtr->sendMsgInfoList.end(); ++itr)
                {
                    if (msgInfo->transId == (*itr)->transId)
                    {
                        LOG(DLOGL_REALLY_NOISY, "SendPendingMessage: evtTimer: %p  transId: %d", pEvt, msgInfo->transId);

                        if (((*itr)->msgRetryCount <= 1))
                        {
                            objPtr->sendMsgInfoList.remove(*itr);
                            delete *itr;
                            *itr = NULL;

                            if (msgInfo->ptrOnDemand)
                            {
                                LOG(DLOGL_ERROR, " NOT RECEIVED SERVER RESPONSE  - sending error to service layer !!");
                                msgInfo->ptrOnDemand->queueEvent(kOnDemandSessionErrorEvent);
                            }
                        }
                        else
                        {
                            //resend the message
                            objPtr->SendPendingMessage(*itr);
                            (*itr)->msgRetryCount--;
                            LOG(DLOGL_NORMAL, "Add message timer event for retry msgRetryCount::%d", (*itr)->msgRetryCount);
                            EventLoop* evtLoop = (*itr)->ptrOnDemand->GetEventLoop();

                            if (evtLoop != NULL)
                            {
                                uint32_t tMsg = 0;
                                OnDemandSystemClient::getInstance()->GetmessageTimer(&tMsg);
                                EventTimer* evtTimer = evtLoop->addTimer(EVENTTIMER_TIMEOUT,
                                                       tMsg,
                                                       0,
                                                       VOD_SessionControl::ProcessTimeoutCallBack, (void *)(*itr));
                                (*itr)->evtTimer = evtTimer;
                            }
                        }
                        break;
                    }
                }
            }
            else
            {
                LOG(DLOGL_MINOR_EVENT, "Warning: no action - sessId: %d no longer active", sessId);
            }
        }
        delete pEvt;
        pEvt = NULL;
    }
    pthread_mutex_unlock(&mVodSessionListMutex);
}



uint8_t * VOD_SessionControl::IPAddressIntToStr(uint32_t ipadd)
{
    char temp[4];
    uint8_t *addBuf = new uint8_t[15];
    memset(addBuf, 0, 15);
    uint8_t * pointer = addBuf;
    sprintf((char *)temp, "%d", ((ipadd >> 24) & 0xFF));
    pointer = Utils::InsertBuffer(pointer, (uint8_t *)temp, strlen(temp));
    pointer = Utils::Put1Byte(pointer, '.');
    sprintf((char *)temp, "%d", ((ipadd >> 16) & 0xFF));
    pointer = Utils::InsertBuffer(pointer, (uint8_t *)temp, strlen(temp));
    pointer = Utils::Put1Byte(pointer, '.');
    sprintf((char *)temp, "%d", ((ipadd >> 8) & 0xFF));
    pointer = Utils::InsertBuffer(pointer, (uint8_t *)temp, strlen(temp));
    pointer = Utils::Put1Byte(pointer, '.');
    sprintf((char *)temp, "%d", ((ipadd >> 0) & 0xFF));
    Utils::InsertBuffer(pointer, (uint8_t *)temp, strlen(temp));
    return addBuf;
}

