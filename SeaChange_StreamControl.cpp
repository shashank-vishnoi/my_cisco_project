/** @file SeaChange_StreamControl.cpp
 *
 * @brief SeaChange StreamControl implementation.
 *
 * @author Manu Mishra
 *
 * @version 1.0
 *
 * @date 28.12.2010
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */
#include <ctime>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
//#include "../../sldk_include/conflictapi.h"


#include <time.h>
#include <sstream>
#include <iostream>
#include "dlog.h"
using namespace std;

#define UNUSED_PARAM(a) (void)a;


#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_ONDEMAND, level,"SeaChange_StreamControl:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

#include <assert.h>

#include "vod.h"
#include "SeaChange_StreamControl.h"

pthread_mutex_t mutex_sr;

#define Close(x) do{dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_DEBUG,"%s:%s:%d ZZClose=%d",__FILE__,__FUNCTION__,__LINE__, x); close(x); (x)=-1;}while(0)

std::string SeaChange_StreamControl::GetNPTFromRange()
{
    std::string ret = "";
    return ret;
}
eIOnDemandStatus SeaChange_StreamControl::StreamSetup(std::string url, std::string sessionId)
{
    UNUSED_PARAM(url)
    UNUSED_PARAM(sessionId)
    eIOnDemandStatus status = ON_DEMAND_ERROR;

    dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS,
         "%s:%i  StreamSetup() Unsupported.\n", __FUNCTION__, __LINE__);

    return status;
}


eIOnDemandStatus SeaChange_StreamControl::StreamGetParameter()
{
    // This does not appear to be used
    LOG(DLOGL_FUNCTION_CALLS, "WHO IS CALLING THIS??");
    return ON_DEMAND_ERROR;
}


// set usingStartNPT true to use current play position
eIOnDemandStatus SeaChange_StreamControl::StreamPlay(bool usingStartNPT)
{
    LOG(DLOGL_FUNCTION_CALLS, "usingStartNPT: %d", usingStartNPT);

    eIOnDemandStatus status = ON_DEMAND_OK;
    isAssetUpdatedNPTset = FALSE;
    isPrecisoNset = FALSE;

    if (!isStreamParametersSet && ptrOnDemand)
    {
        LOG(DLOGL_MINOR_DEBUG, "setting stream parameters");

        streamControlURL = (char *)ptrOnDemand->GetStreamServerIPAddress();
        streamControlPort = ptrOnDemand->GetStreamServerPort();
        socketFd = lscpBaseObj->GetSocket(streamControlURL,
                                          streamControlPort);

        dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_DEBUG, "%s:%s:%d ZZOpen call GetSocket to get fd %d", __FILE__, __FUNCTION__, __LINE__, socketFd);

        isStreamParametersSet = true;

        LOG(DLOGL_REALLY_NOISY, "stream streamControlURL:%s ", streamControlURL.c_str());
        LOG(DLOGL_REALLY_NOISY, "stream streamControlPort:%x \n ", streamControlPort);
        LOG(DLOGL_REALLY_NOISY, "stream socketfd:%d", socketFd);
        LOG(DLOGL_REALLY_NOISY, "stream stream handle:%x \n", ptrOnDemand->GetStreamHandle());
    }

    i32 nptStartPosition;

    if (usingStartNPT)
    {
        nptStartPosition = NPT_CURRENT;
    }
    else
    {
        pthread_mutex_lock(&mutex_sr);
        nptStartPosition = (uint32_t) nptPosition;   //Command NPT position
        pthread_mutex_unlock(&mutex_sr);
    }

    LOG(DLOGL_NORMAL, "commanded nptStartPosition: %d  numerator: %d  demoninator: %d",
        nptStartPosition, numerator, denominator);

    if (NULL == ptrOnDemand)
    {
        LOG(DLOGL_ERROR, "On Demand pointer is NULL");
        return ON_DEMAND_ERROR;
    }

    VodLscp_play *setupObject = new VodLscp_play(nptStartPosition,
            NPT_END,
            numerator,
            denominator,
            LSC_PROTOCOL_VERSION,
            VodLscp_Base::getTransId(),
            LSC_PLAY,
            LSC_STATUS_CODE,
            ptrOnDemand->GetStreamHandle());

    if (setupObject)
    {
        uint8_t *data;
        uint32_t length = 0;

        setupObject->PackLscpMessageBody((uint8_t **) &data, &length);

        if (socketFd != -1)
        {
            setupObject->SendMessage(socketFd, data, length);
        }
        else
        {
            LOG(DLOGL_ERROR, "warning: socketFd == -1");
        }

        delete setupObject;
        setupObject = NULL;
    }
    else
    {
        LOG(DLOGL_ERROR, "warning: new VodLscp_play failed");
    }


    return status;
}


void SeaChange_StreamControl::HandleInput(void *ptrMessage)
{
    LOG(DLOGL_FUNCTION_CALLS, "ptrMessage: %p", ptrMessage);

    VodLscp_Base *message = (VodLscp_Base *) ptrMessage;

    if (!message)
    {
        LOG(DLOGL_ERROR, "error null ptrMessage");
        return;
    }

    ui8 opcode = message->GetMessageId();

    LOG(DLOGL_NOISE, " Opcode recieved from VOD server is %d", opcode);

    switch (opcode)

    {
    case LSC_DONE:
    {
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "LSC done recieved");
        LOG(DLOGL_REALLY_NOISY, "LSC_DONE");
        pthread_mutex_lock(&mutex_sr);
        nptPosition = message->GetNPT();
        //This is required to maintain the Pause case
        //numerator = message->GetNum();
        //denominator = message->GetDen();
        //TODO: Get the Vod server team for the done event Response in case of Pause Time-Out
        rettransid = message->GetReturnTransId();
        retstatus = message->GetReturnStatusCode();
        server_mode = message->GetMode();
        streamStarted = FALSE;
        pthread_mutex_unlock(&mutex_sr);

        numerator < 0 ? ptrOnDemand->queueEvent(kOnDemandPlayBOFEvent) : ptrOnDemand->queueEvent(kOnDemandPlayEOFEvent);
        /*Need to notify OnDemand that streaming is ended. Ondemand needs to take appropriate action*/
    }
    break;

    case LSC_PLAY_REPLY:
    case LSC_PAUSE_REPLY:
    {
        time(&start);
        pthread_mutex_lock(&mutex_sr);
        streamStarted = TRUE;
        if (opcode == LSC_PLAY_REPLY)
        {
            LOG(DLOGL_REALLY_NOISY, "LSC_PLAY_REPLY");
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "LSC_PAUSE_REPLY");
        }
        nptPosition = message->GetNPT();
        numerator = message->GetNum();
        denominator = message->GetDen();
        rettransid = message->GetReturnTransId();
        retstatus = message->GetReturnStatusCode();
        pthread_mutex_unlock(&mutex_sr);
        LOG(DLOGL_NOISE, "Updating values for transaction-id %d having Return status code %d", rettransid, retstatus);
        LOG(DLOGL_NOISE, "Updated NPT to %f", nptPosition);
        LOG(DLOGL_NOISE, "Updated numerator to %d", numerator);
        LOG(DLOGL_NOISE, "Updated denominator to %d", denominator);
        if (ptrOnDemand)
            ptrOnDemand->queueEvent(kOnDemandPlayRespEvent);
    }
    break;

    default:
        time(&start);
        pthread_mutex_lock(&mutex_sr);
        streamStarted = TRUE;
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "Updating NPT, Num, Den received from Server");

        LOG(DLOGL_MINOR_DEBUG, "warning opcode %d rcvd - no action taken", opcode)
        ;
        nptPosition = message->GetNPT();
        numerator = message->GetNum();
        denominator = message->GetDen();
        rettransid = message->GetReturnTransId();
        retstatus = message->GetReturnStatusCode();
        pthread_mutex_unlock(&mutex_sr);
        LOG(DLOGL_NOISE, "Updating values for transaction-id %d having Return status code %d",
            rettransid, retstatus);
        LOG(DLOGL_NOISE, "Updated NPT to %f", nptPosition);
        LOG(DLOGL_NOISE, "Updated numerator to %d", numerator);
        LOG(DLOGL_NOISE, "Updated denominator to %d", denominator);

        break;
    }
}

eIOnDemandStatus SeaChange_StreamControl::SendKeepAlive()
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;

    return status;

}

void SeaChange_StreamControl::DisplayDebugInfo()
{
    dlog(DL_MSP_ONDEMAND, DLOGL_SIGNIFICANT_EVENT,
         "SeaChange_StreamControl::Numerator %d", numerator);
    dlog(DL_MSP_ONDEMAND, DLOGL_SIGNIFICANT_EVENT,
         "SeaChange_StreamControl::Numerator %d", denominator);
    dlog(DL_MSP_ONDEMAND, DLOGL_SIGNIFICANT_EVENT,
         "SeaChange_StreamControl::nptPosition %f", nptPosition);
    // dlog(DL_MSP_ONDEMAND,DLOGL_SIGNIFICANT_EVENT,"SeaChange_StreamControl::mediaUnicastPort %d\n",ptrOnDemand->GetMediaUnicastPort());
    dlog(DL_MSP_ONDEMAND, DLOGL_SIGNIFICANT_EVENT,
         "SeaChange_StreamControl::streamControlSessionId %s",
         streamControlSessionId.c_str());
}

SeaChange_StreamControl::SeaChange_StreamControl(OnDemand* ptrOnDemand,
        void* pClientMetaDataContext, eOnDemandReqType reqType) :
    VOD_StreamControl(ptrOnDemand, reqType)
{

    lscpBaseObj = new VodLscp_Base();
    pClientMetaDataContext = pClientMetaDataContext;
    isPrecisoNset = FALSE;
    isAssetUpdatedNPTset = FALSE;
    StreamFailCount = 0;
    start = 0;
    end = 0;
}

SeaChange_StreamControl::~SeaChange_StreamControl()
{
    if (NULL != lscpBaseObj)
    {
        delete lscpBaseObj;
        lscpBaseObj = NULL;
    }
    Close(socketFd);
    socketFd = -1;
}
/*SL  queries the middle ware very frequently for speed and position.
 *  This causes heavy network traffic if all of those requests are converted into protocol messages.
 *  To avoid this, approximating algorithm is used which keeps track of the position as soop as the streaming starts.
 *   It needs to keep track of NPT and numerator and denominator in all cases of normal play and trick modes(FF/RW).
 *   A precision update is required from VOD server to get the actual NPT values. This results in tremendous reduction of network traffic. */

eIOnDemandStatus SeaChange_StreamControl::StreamGetPosition(float *npt)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;

    float nptPos = 0;
    int16_t num = 0, den = 0;
    bool strmStart;
    pthread_mutex_lock(&mutex_sr);
    nptPos = nptPosition;
    num = numerator;
    den = denominator;
    strmStart = streamStarted;
    static int prevNum = 1;
    static int prevDen = 1;

    pthread_mutex_unlock(&mutex_sr);

    if (strmStart == TRUE)
    {

        status = ON_DEMAND_OK;
        if (num != prevNum)
        {
            num = prevNum;
            den = prevDen;
        }

        if ((num == 1) && (den == 1))
        {
            time(&end);
            *npt = (float)(nptPos + 1000 * (difftime(end, start))) / 1000;
        }
        else if ((num == 0))
        {
            *npt = (float) nptPos / 1000;
        }
        else if ((num == 15) && (den == 2))
        {
            time(&end);
            *npt = (float)(nptPos + 7500 * (difftime(end, start))) / 1000;
        }
        else if ((num == -15) && (den == 2))
        {
            time(&end);
            *npt = (float)(nptPos - 7500 * (difftime(end, start))) / 1000;
        }
        else
        {
            LOG(DLOGL_SIGNIFICANT_EVENT, "Warning: VOD Server returned UnKnown");
            status = ON_DEMAND_ERROR;
        }

        float endNPT = 0;
        ptrOnDemand->GetEndPosition(&endNPT);

        if (((endNPT - (*npt)) < (PRECISION_UPDATE) && (isPrecisoNset == FALSE))
                || (((*npt) < PRECISION_UPDATE) && (isPrecisoNset == FALSE))) //added braces for G8 Porting
        {
            pthread_mutex_lock(&mutex_sr);
            streamStarted = FALSE;
            isPrecisoNset = TRUE;
            pthread_mutex_unlock(&mutex_sr);

        }
        if (((endNPT > ASSET_UP_NPT) && ((endNPT / (*npt)) < 2))
                && (isAssetUpdatedNPTset == FALSE))
        {
            pthread_mutex_lock(&mutex_sr);
            streamStarted = FALSE;
            isAssetUpdatedNPTset = TRUE;
            pthread_mutex_unlock(&mutex_sr);

        }
        pthread_mutex_lock(&mutex_sr);
        nptPosition = (*npt) * 1000;
        pthread_mutex_unlock(&mutex_sr);
        time(&start);

    }
    else
    {
        uint8_t *data;
        uint32_t length = 0;
        uint8_t waitCount = 0;
        uint8_t localtransid = 0;

        VodLscp_status *setupObject = new VodLscp_status(LSC_PROTOCOL_VERSION,
                VodLscp_Base::getTransId(), LSC_STATUS, LSC_STATUS_CODE,
                ptrOnDemand->GetStreamHandle());

        if (NULL != setupObject)
        {
            setupObject->PackLscpMessageBody((uint8_t **) &data, &length);

            if (socketFd != -1)
            {
                setupObject->SendMessage(socketFd, data, length);
                localtransid = setupObject->GetReturnTransId();
            }

            delete setupObject;
            setupObject = NULL;

            while (waitCount < LSC_TIMEOUT * 10)
            {
                if (localtransid == rettransid)
                {
                    if (retstatus == LSC_OK)
                    {
                        pthread_mutex_lock(&mutex_sr);
                        //Converting npt time from ms to second
                        if (nptPosition != NPT_END)
                        {
                            *npt = (float) nptPosition / 1000;
                        }
                        else
                        {
                            float endNPT = 0;
                            ptrOnDemand->GetEndPosition(&endNPT);
                            *npt = endNPT;
                            nptPosition = (*npt) * 1000;
                            LOG(DLOGL_MINOR_EVENT, "EOF reached so current Position same as endPosition nptPosition:%f", nptPosition);
                        }
                        pthread_mutex_unlock(&mutex_sr);
                        status = ON_DEMAND_OK;
                    }
                    else if (retstatus == LSC_NO_PERMISSION)
                        status = ON_DEMAND_ERROR;

                    else if (retstatus == LSC_SERVER_ERROR)
                        status = ON_DEMAND_ERROR;
                    else
                        status = ON_DEMAND_ERROR;

                    return status;
                }

                //TODO:  Why is this waiting??  Is there an event?
                //Answer: As it is a synchronous SAIL call , hence it has to wait
                waitCount++;
                //100ms wait
                usleep(HUNDRED_MS);
            }
        }

        LOG(DLOGL_SIGNIFICANT_EVENT, "Warning: VOD Server did not respond");
    }
    prevNum = numerator;
    prevDen = denominator;
    return status;
}


void SeaChange_StreamControl::StreamSetSpeed(signed short num, unsigned
        short den)
{
    float speed = num / den;

    if (speed == 1)
    {
        numerator   = 1;
        denominator = 1;
    }
    else if (speed >= 1)
    {
        numerator   = 15;
        denominator = 2;
    }
    else
    {
        numerator   = -15;
        denominator = 2;
    }

    StreamPlay(true);
}
eIOnDemandStatus SeaChange_StreamControl::StreamGetSpeed(int16_t *num, uint16_t *den)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;


    int16_t nm = 0, dn = 0;
    bool strmStart;
    pthread_mutex_lock(&mutex_sr);
    nm = numerator;
    dn = denominator;
    strmStart = streamStarted;
    pthread_mutex_unlock(&mutex_sr);

    if (strmStart == TRUE)
    {
        *num = nm;
        *den = dn;
        status = ON_DEMAND_OK;

    }

    else
    {
        uint8_t *data;
        uint32_t length = 0;
        uint8_t waitCount = 0;
        ui8 localtransid = 0;

        VodLscp_status *setupObject = new VodLscp_status(LSC_PROTOCOL_VERSION,
                VodLscp_Base::getTransId(), LSC_STATUS, LSC_STATUS_CODE,
                ptrOnDemand->GetStreamHandle());

        if (NULL != setupObject)
        {
            setupObject->PackLscpMessageBody((uint8_t **) &data, &length);

            if (socketFd != -1)
            {
                setupObject->SendMessage(socketFd, data, length);
                localtransid = setupObject->GetReturnTransId();
            }

            delete setupObject;
            setupObject = NULL;

            while (waitCount < LSC_TIMEOUT * 10)
            {

                if (localtransid == rettransid)
                {

                    if (retstatus == LSC_OK)
                    {
                        *num = numerator;
                        *den = denominator;
                        status = ON_DEMAND_OK;
                    }
                    else if (retstatus == LSC_NO_PERMISSION)
                        status = ON_DEMAND_ERROR;

                    else if (retstatus == LSC_SERVER_ERROR)
                        status = ON_DEMAND_ERROR;
                    else
                        status = ON_DEMAND_ERROR;

                    return status;
                }

                //TODO:  Fix waiting
                //Answer: As it is a synchronous SAIL call , hence it has to wait

                waitCount++;
                //100ms wait
                usleep(HUNDRED_MS);

            }
        }
    }
    LOG(DLOGL_SIGNIFICANT_EVENT, "Warning: VOD Server did not respond");

    return status;
}


void SeaChange_StreamControl::StreamSetNPT(float npt)
{
    pthread_mutex_lock(&mutex_sr);
    nptPosition = (int32_t) npt;
    pthread_mutex_unlock(&mutex_sr);

    numerator = 1;
    denominator = 1;

    StreamPlay(false);
}

eIOnDemandStatus SeaChange_StreamControl::StreamPause()
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;

    uint8_t *data;
    uint32_t length = 0;

    VodLscp_pause *setupObject = new VodLscp_pause(NPT_CURRENT, LSC_PROTOCOL_VERSION, VodLscp_Base::getTransId(),
            LSC_PAUSE, LSC_STATUS_CODE, ptrOnDemand->GetStreamHandle());

    if (setupObject)
    {
        setupObject->PackLscpMessageBody((uint8_t **) &data, &length);

        if (socketFd != -1)
        {
            setupObject->SendMessage(socketFd, data, length);
            status = ON_DEMAND_OK;
        }

        delete setupObject;
        setupObject = NULL;
    }

    return status;
}

void SeaChange_StreamControl::ReadStreamSocketData()
{
    uint8_t *msgData = NULL;
    uint32_t msgLen  = 0;
    VodLscp_Base *lscpObj;

    LOG(DLOGL_REALLY_NOISY, " Current Fail count value is %d", StreamFailCount);

    if (ptrOnDemand && lscpBaseObj)
    {
        if (StreamFailCount > 100)
        {
            LOG(DLOGL_ERROR, "Read Stream Socket Data error");
            ptrOnDemand->queueEvent(kOnDemandStreamErrorEvent);
        }

        bool readStatus = lscpBaseObj->ReadMessageFromSocket(GetFD(), &msgData, &msgLen);

        if (readStatus)
        {
            //Reset it every time we get a successful Socket read
            StreamFailCount = 0;


            LOG(DLOGL_REALLY_NOISY, "lscpBaseObj->GetMessageTypeObject");
            lscpObj = lscpBaseObj->GetMessageTypeObject(msgData, msgLen);

            if (msgData && msgLen)
            {
                if (lscpObj)
                {
                    //parse lscp response

                    LOG(DLOGL_REALLY_NOISY, "lscpObj->ParseLscpMessageBody");
                    lscpObj->ParseLscpMessageBody(msgData, msgLen);
                    //Handle lscp response type
                    HandleInput((void *)lscpObj);
                }
                else
                {
                    LOG(DLOGL_ERROR, "warning null lscpObj");
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "warning msgData: %p  msgLen: %d ", msgData, msgLen);
            }
        }
        else
        {

            LOG(DLOGL_ERROR, "warning ReadMessageFromSocket for stream failed");
            StreamFailCount++;
        }
    }


}

void SeaChange_StreamControl::HandleStreamResp()
{
    //TODO: Handle Any specific requirements
    //This function is Only for testing
    int16_t a = 0;
    uint16_t b = 0;
    float npt = 20000.00;

    for (int i = 0; i < 10; i++)
    {
        if (i == 1)
        {
            sleep(1);
            sleep(1);
            StreamSetNPT(npt);
            dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "StreamSetNPT in loop, value NPT asked to set is :  %f", npt);
        }

        if (i == 2)
        {
            sleep(1);
            npt = 0;
            StreamGetPosition(&npt);
            dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "StreamGetPosition in loop, value NPT:  %f", npt);
        }

        if (i == 3)
        {
            StreamGetSpeed(&a, &b);
            dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "StreamGetSpeed in loop, values are num: %d, den: %d", a, b);
        }
    }
}

