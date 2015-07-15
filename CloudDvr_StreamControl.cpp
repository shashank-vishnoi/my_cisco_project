/** @file CloudDvr_StreamControl.cpp
 *
 * @brief Cloud Dvr StreamController: This Class implements the Cloud StreamController which uses the RTSP stream control protocol to
 * play/pause and trick mode operations on the media content in the server.
 *
 * @author Laliteshwar Prasad Yadav
 *
 * @version 1.0
 *
 * @date 08.05.2013
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#include <ctime>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include "dlog.h"
#include <assert.h>
#include "pthread_named.h"
#include "vod.h"
#include "CloudDvr_StreamControl.h"
#include "eventLoop.h"
#include "CCloudDvrConfigFileParser.h"

#define DEFAULT_RTSP_KEEPALIVE_TIMEOUT 1800 //30 minutes in seconds

#ifdef LOG
#error  LOG already defined
#endif
#ifndef LOG
#define LOG(level, msg, args...)  dlog(DL_MSP_ONDEMAND, level,"CloudDvr_StreamControl:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#endif
#define Close(x) do{dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_DEBUG,"%s:%s:%d ZZClose=%d",__FILE__,__FUNCTION__,__LINE__, x); close(x); (x)=-1;}while(0)

using namespace std;

//static methods
void CloudDvr_StreamControl::ResponseGETPOSITION(RTSPClient* rtspClient, int resultCode, char* resultString)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    if ((rtspClient == NULL) || (resultString == NULL))
    {
        LOG(DLOGL_ERROR, "Invalid Parameters. rtspClient:%p resultString:%s", rtspClient, resultString);
    }
    else
    {
        CloudDvr_StreamControl* pCsc = ((RTSPClientInterface*)rtspClient)->pStreamControl;
        if (pCsc)
        {
            pCsc->processResponseGetPos(resultCode, resultString);
        }
        delete[] resultString;
    }
}

void CloudDvr_StreamControl::ResponseGETSPEED(RTSPClient* rtspClient, int resultCode, char* resultString)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    if ((rtspClient == NULL) || (resultString == NULL))
    {
        LOG(DLOGL_ERROR, "Invalid Parameters. rtspClient:%p resultString:%s", rtspClient, resultString);
    }
    else
    {
        CloudDvr_StreamControl* pCsc = ((RTSPClientInterface*)rtspClient)->pStreamControl;
        if (pCsc)
        {
            pCsc->processResponseGetSpeed(resultCode, resultString);
        }
        delete[] resultString;
    }
}

void CloudDvr_StreamControl::ResponseKEEPALIVE(RTSPClient* rtspClient, int resultCode, char* resultString)
{
    UNUSED_PARAM(rtspClient);
    UNUSED_PARAM(resultCode);

    LOG(DLOGL_FUNCTION_CALLS, "");

    if (resultString)
        delete[] resultString;
}

void CloudDvr_StreamControl::ResponsePAUSE(RTSPClient* rtspClient, int resultCode, char* resultString)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    if (rtspClient == NULL)
    {
        LOG(DLOGL_ERROR, "Invalid rtspClient:%p ", rtspClient);
    }
    else
    {
        CloudDvr_StreamControl* pCsc = ((RTSPClientInterface*)rtspClient)->pStreamControl;
        if (pCsc)
        {
            if (pCsc->ptrOnDemand)
            {
                if (resultCode == RTSP_RESULT_CODE_OK)
                {
                    LOG(DLOGL_MINOR_EVENT, "Set the Pause_Mode to true");
                    pCsc->setPauseMode(true);
                    pCsc->ptrOnDemand->queueEvent(kOnDemandRTSPPlaySuccess);
                }
                else
                {
                    LOG(DLOGL_ERROR, "Streamer Error:%d Sending StreamErrorEvent", resultCode);
                    pCsc->ptrOnDemand->queueEvent(kOnDemandStreamErrorEvent);
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "Error CloudDvr_StreamControl->ptrOnDemand:%p", pCsc->ptrOnDemand);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Error CloudDvr_StreamControl:%p Sending StreamErrorEvent", pCsc);
        }
    }

    if (resultString)
        delete[] resultString;
}

void CloudDvr_StreamControl::ResponsePLAY(RTSPClient* rtspClient, int resultCode, char* resultString)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    if (rtspClient == NULL)
    {
        LOG(DLOGL_ERROR, "rtspClient is %p.", rtspClient);
    }
    else
    {
        eOnDemandEvent event = kOnDemandRTSPPlaySuccess;

        if (resultCode != RTSP_RESULT_CODE_OK)
        {
            LOG(DLOGL_ERROR, "Failed to start playing session");
            event = kOnDemandRTSPPlayFailed;
        }

        LOG(DLOGL_SIGNIFICANT_EVENT, "Got a PLAY response from the RTSP streamer");

        EventCallbackData *evt = new EventCallbackData;
        if (evt != NULL)
        {
            CloudDvr_StreamControl* pCsc = ((RTSPClientInterface*)rtspClient)->pStreamControl;
            if (pCsc != NULL)
            {

                pCsc->processPLAYResponse();

                evt->objPtr = pCsc->ptrOnDemand;
                evt->odEvtType = event;
                if (event_base_once(pCsc->ptrOnDemand->GetEventLoop()->getBase(),
                                    LIBEVENT_TIMEOUT_FD,
                                    EV_TIMEOUT,
                                    OnDemand::OnDemandEvtCallback,
                                    (void *)evt,
                                    NULL) != LIBEVENT_ADD_EVENT_SUCCESS)
                {
                    LOG(DLOGL_ERROR, "Adding event to libevent base failed");
                    delete evt;
                }
                //starting RTSP Keep Alive Timer on RTSP PLay Success only.
                if (event == kOnDemandRTSPPlaySuccess)
                {
                    pCsc->StartSessionKeepAliveTimer();
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "pCsc is NULL");
                delete evt;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "evt is NULL");
        }
    }

    if (resultString)
        delete[] resultString;
}

void CloudDvr_StreamControl::processPLAYResponse(void)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    lockStreamerMutex();
    if (mRtspSession)
    {
        nptPosition = mRtspSession->playStartTime();
        LOG(DLOGL_SIGNIFICANT_EVENT, "nptPosition:%f scale:%f", nptPosition, mRtspSession->scale());
        numerator = ((int16_t)mRtspSession->scale()) * 100;
        mStatusPlayResponse = true;
    }
    else
    {
        LOG(DLOGL_ERROR, "mRtspSession is NULL");
    }

    Pause_Mode = false;
    LOG(DLOGL_REALLY_NOISY, "Set the Pause_Mode to false");
    unlockStreamerMutex();
}

CloudDvr_StreamControl::CloudDvr_StreamControl(OnDemand* pOnDemand, void* pClientMetaDataContext,
        eOnDemandReqType reqType): VOD_StreamControl(pOnDemand, reqType), mRtspSession(NULL), mRtspClient(NULL), mEnv(NULL),
    mStatusRespGetPos(RTSP_NO_RESPONSE_YET), mStatusRespGetSpeed(RTSP_NO_RESPONSE_YET)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    if (pthread_mutex_init(&mMutex_cdvr, NULL) != 0)
    {
        LOG(DLOGL_ERROR, "Failed to initialize mMutex_cdvr Error:%m");
    }

    UNUSED_PARAM(pClientMetaDataContext)
    mShutDownFlag = 0;
    mScheduler = BasicTaskScheduler::createNew();
    mIsAssetUpdatedNPTset = true;
    time(&mLastGetPosAccessTime);
    time(&mLastGetPosNetworkQueryTime);
    time(&mNow);
    Pause_Mode = false;
    nptPosition = START_NPT;
    mTempNumerator = numerator = 100;
    mTempDenominator = denominator = 100;

    mStatusPlayResponse = false;
    mEventTimer = NULL;
    mEventLoop = NULL;

    if (pOnDemand != NULL)
    {
        //using existing OnDemand Event Loop
        mEventLoop = pOnDemand->GetEventLoop();

        if (!mEventLoop)
        {
            LOG(DLOGL_ERROR, "Failed to get OnDemand EventLoop for RTSP Keep Alive");
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Invalid OnDemand pointer");
    }

    if (mScheduler == NULL)
    {
        LOG(DLOGL_ERROR, "Failed to create the TaskScheduler for Cloud DVR stream controller");
    }
    else
    {
        mEnv = BasicUsageEnvironment::createNew(*mScheduler);
        if (mEnv == NULL)
        {
            LOG(DLOGL_ERROR, "Failed to create the UsageEnvironment for Cloud DVR stream controller");
        }
        else
        {
            LOG(DLOGL_MINOR_DEBUG, "Created the UsageEnvironment for Cloud DVR stream controller");
            // Create a media session object
            mRtspSession = MediaSession::createNew(*mEnv, NULL);
            if (mRtspSession == NULL)
            {
                LOG(DLOGL_ERROR, "Failed to create the mRtspSession for Cloud DVR stream controller");
            }
        }
    }
}

bool CloudDvr_StreamControl::createTaskSchedulerThread(CloudDvr_StreamControl *pInst)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    bool status = true;
    if (pInst == NULL)
    {
        status = false;
        LOG(DLOGL_ERROR, "Invalid CloudDVR StreamControl Instance.");
    }
    else
    {
        int ret = 0;
        pthread_attr_t  RtspClientThreadAttr;
        pthread_t       RtspClientThread;
        const char threadName[]  = "RTSP Task Scheduler Thread";

        pthread_attr_init(&RtspClientThreadAttr);
        pthread_attr_setstacksize(&RtspClientThreadAttr, STACK_SIZE);
        pthread_attr_setdetachstate(&RtspClientThreadAttr, PTHREAD_CREATE_DETACHED);

        ret = pthread_create(&RtspClientThread, &RtspClientThreadAttr, TaskSchedulerThread, (void*) pInst);
        if (0 != ret)
        {
            LOG(DLOGL_ERROR, ":%m: Error creating TaskSchedulerThread :%d", ret);
            status = false;
        }
        else
        {
            ret = pthread_setname_np(RtspClientThread, threadName);
            if (0 != ret)
            {
                LOG(DLOGL_ERROR, "ERROR: %m: Thread Setname Failed.%d", ret);
                status = false;
            }
        }
        pthread_attr_destroy(&RtspClientThreadAttr);
    }
    return status;
}

//static method
void *CloudDvr_StreamControl::TaskSchedulerThread(void * arg)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    CloudDvr_StreamControl* pInst = (CloudDvr_StreamControl*) arg;
    if (pInst)
    {
        // All subsequent activity takes place within the event loop:
        pInst->mEnv->taskScheduler().doEventLoop(&(pInst->mShutDownFlag));

        LOG(DLOGL_NORMAL, "Exiting the TaskSchedulerThread");
    }

    return NULL;
}

CloudDvr_StreamControl::~CloudDvr_StreamControl()
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    if (mRtspClient)
    {
        LOG(DLOGL_MINOR_DEBUG, "Deleting mRtspClient");
        Medium::close(mRtspClient);
        mRtspClient = NULL;
    }

    if (mRtspSession)
    {
        LOG(DLOGL_MINOR_DEBUG, "Deleting mRtspSession");
        Medium::close(mRtspSession);
        mRtspSession = NULL;
    }

    if (mScheduler)
    {
        LOG(DLOGL_MINOR_DEBUG, "Deleting mScheduler");
        delete mScheduler;
        mScheduler = NULL;
    }

    if (mEnv)
    {
        LOG(DLOGL_MINOR_DEBUG, "env reclaim");
        mEnv->reclaim();
        mEnv = NULL;
    }

    //not freeing mEventLoop. Because
    //just using existing OnDemand EventLoop
    mEventLoop = NULL;

    //destroy mMutex_cdvr
    LOG(DLOGL_MINOR_DEBUG, "Destroying mutex");
    pthread_mutex_destroy(&mMutex_cdvr);
}

eIOnDemandStatus CloudDvr_StreamControl::StreamSetup(std::string url, std::string sessionId)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    eIOnDemandStatus status = ON_DEMAND_OK;

    if (url.empty() || sessionId.empty())
    {
        LOG(DLOGL_ERROR, "NULL url or sessionId");
        status = ON_DEMAND_ERROR;
    }
    else
    {
        if (mRtspClient)
        {
            Medium::close(mRtspClient);
            mRtspClient = NULL;
        }

        portNumBits tunnelOverHTTPPortNum = 0;
        mRtspClient = RTSPClientInterface::createNew(*mEnv, NULL, VERBOSITY_LEVEL, "CloudDVR", tunnelOverHTTPPortNum);
        if (mRtspClient == NULL)
        {
            LOG(DLOGL_ERROR, "Failed to create the rtspClient for Cloud DVR stream controller");
            status = ON_DEMAND_ERROR;
        }
        else
        {
            LOG(DLOGL_MINOR_DEBUG, "rtspClientInterface Available");
            ((RTSPClientInterface*)mRtspClient)->pStreamControl = this;
            LOG(DLOGL_MINOR_DEBUG, " url:%s   sessionId:%s", url.c_str(), sessionId.c_str());

            if (!mRtspClient->setURL((char *) url.c_str()))
            {
                LOG(DLOGL_ERROR, "Couldn't set URL on the RTSP client");
                status = ON_DEMAND_ERROR;
            }
            else
            {
                if (!mRtspClient->setSessionId((char *) sessionId.c_str()))
                {
                    LOG(DLOGL_ERROR, "Couldn't set sessionId on the RTSP client");
                    status = ON_DEMAND_ERROR;
                }
            }

            if ((status != ON_DEMAND_ERROR) && (createTaskSchedulerThread(this) == false))
            {
                LOG(DLOGL_ERROR, "Failed to create the RTSP Task Scheduler Thread for Cloud DVR stream controller");
                status = ON_DEMAND_ERROR;
            }

        }
    }

    return status;
}


eIOnDemandStatus CloudDvr_StreamControl::StreamGetParameter()
{
    // This does not appear to be used
    LOG(DLOGL_FUNCTION_CALLS, "WHO IS CALLING THIS??");
    return ON_DEMAND_ERROR;
}


// set usingStartNPT true to use current play position
eIOnDemandStatus CloudDvr_StreamControl::StreamPlay(bool usingStartNPT)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    eIOnDemandStatus status = ON_DEMAND_ERROR;

    if (mRtspClient)
    {
        double end = PLAY_TO_END;
        float scale = (float) mTempNumerator / mTempDenominator;
        lockStreamerMutex();
        double start = (usingStartNPT == true) ? RESUME_NPT : nptPosition;
        unlockStreamerMutex();

        LOG(DLOGL_NOISE, "startNpt:%f speed = %f", start, scale);
        int ret =  mRtspClient->sendPlayCommand(*mRtspSession, ResponsePLAY, start, end, scale, NULL);
        LOG(DLOGL_REALLY_NOISY, "SendPlayCommand ret:%d", ret);
        if (ret != 0)
        {
            LOG(DLOGL_SIGNIFICANT_EVENT, "Sent a PlayCommand Request to the server");
            status = ON_DEMAND_OK;
            time(&mLastGetPosAccessTime);
            time(&mLastGetPosNetworkQueryTime);
        }
        else
        {
            LOG(DLOGL_ERROR, "SendPlayCommand Error");
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "rtspClientInterface UnAvailable");
    }
    return status;
}


void CloudDvr_StreamControl::HandleInput(void *ptrMessage)
{
    LOG(DLOGL_FUNCTION_CALLS, "ptrMessage: %p", ptrMessage);
}

eIOnDemandStatus CloudDvr_StreamControl::SendKeepAlive()
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    eIOnDemandStatus status = ON_DEMAND_OK;

    if (mRtspClient)
    {
        if (mRtspClient->sendGetParameterCommand(*mRtspSession, ResponseKEEPALIVE, NULL) != 0)
        {
            LOG(DLOGL_NOISE, "KeepAlive Message sent");
        }
        else
        {
            status = ON_DEMAND_ERROR;
            LOG(DLOGL_ERROR, "sendKeepAlive Error");
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "rtspClientInterface UnAvailable");
        status = ON_DEMAND_ERROR;
    }

    return status;
}

void CloudDvr_StreamControl::DisplayDebugInfo()
{
    LOG(DLOGL_FUNCTION_CALLS, "");
}

/*SL  queries the middleware very frequently for speed and position.
 *  This causes heavy network traffic if all of those requests are converted into protocol messages.
 *  To avoid this, approximating algorithm is used which keeps track of the position as soon as the streaming starts.
 *   It needs to keep track of NPT and numerator and denominator in all cases of normal play and trick modes(FF/RW).
 *   A precision update is required from streamer to get the actual NPT values. This results in tremendous reduction of network traffic. */

eIOnDemandStatus CloudDvr_StreamControl::StreamGetPosition(float *pNpt)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    uint32_t waitCount = 0;
    float nptPos = 0;
    int16_t num = 0, den = 0;
    uint32_t  time_elapsed = 1;

    LOG(DLOGL_FUNCTION_CALLS, "");

    pthread_mutex_lock(&mMutex_cdvr);
    nptPos = nptPosition;
    num = numerator;
    den = denominator;

    pthread_mutex_unlock(&mMutex_cdvr);

    int speed = 0;

    if (pNpt == NULL || mRtspClient == NULL)
    {
        LOG(DLOGL_ERROR, " ERROR: Invalid input parameters pNpt(%p), mRtspClient(%p)", pNpt, mRtspClient);
        status = ON_DEMAND_INVALID_INPUT_ERROR;
    }
    else
    {
        time(&mNow);

        if (mIsAssetUpdatedNPTset == true)
        {
            status = ON_DEMAND_OK;
            speed =  num / den;

            time_elapsed = mNow - mLastGetPosAccessTime;

            LOG(DLOGL_REALLY_NOISY, "speed(%d) npt(%f) time_elapsed(%d)", speed, nptPos, time_elapsed);
            //Pause_Mode is a base class member
            if (Pause_Mode)
            {
                *pNpt = nptPos;
            }//handled for +/- 1/2/4/8/16/64x
            else //Not checking if the speed is supported since unsupported speed modes are not allowed to even be set
            {
                int deltaNpt = (speed * time_elapsed);
                *pNpt = (nptPos + deltaNpt);
            }

            pthread_mutex_lock(&mMutex_cdvr);
            nptPosition = *pNpt;
            pthread_mutex_unlock(&mMutex_cdvr);

            LOG(DLOGL_REALLY_NOISY, "speed:%d npt=%f", speed, nptPosition);
            time(&mLastGetPosAccessTime);
        }
        else
        {
            if (mRtspClient->sendGetParameterCommand(*mRtspSession, ResponseGETPOSITION, "position" , NULL) != 0)
            {
                time(&mLastGetPosNetworkQueryTime);
                LOG(DLOGL_SIGNIFICANT_EVENT, "Sent a GetPosition command to the RTSP server..Waiting for the response from server..");
                while (waitCount < RTSP_TIMEOUT * RTSP_TIMEOUT_OFFSET)
                {
                    if (mStatusRespGetPos == RTSP_RESPONSE_OK)
                    {
                        lockStreamerMutex();
                        *pNpt = nptPosition;
                        unlockStreamerMutex();
                        status = ON_DEMAND_OK;
                        mIsAssetUpdatedNPTset = true;
                        time(&mLastGetPosAccessTime);
                        break;
                    }
                    else if (mStatusRespGetPos == RTSP_RESPONSE_ERROR)
                    {
                        LOG(DLOGL_ERROR, "response error for GetPosition");
                        break;
                    }

                    waitCount++;
                    usleep(HUNDRED_MS);
                }

                lockStreamerMutex();
                mStatusRespGetPos = RTSP_NO_RESPONSE_YET;		//reset it back for use on next transaction
                unlockStreamerMutex();
            }
            else
            {
                LOG(DLOGL_ERROR, "Error: StreamGetPosition\n");
                status = ON_DEMAND_ERROR;
            }
        }

        //Querying the RTSP server with GETPARAMETER for every RTSP_GETPARAMETER_QUERY_INTERVAL seconds
        if ((mNow - mLastGetPosNetworkQueryTime) > RTSP_GETPARAMETER_QUERY_INTERVAL)
        {
            mIsAssetUpdatedNPTset = false;
        }
    }

    return status;
}

bool CloudDvr_StreamControl::IsCurrentSpeedSupported(float speed)
{
    if (speed == SPEED_NORMAL
            || speed == SPEED_SLOW 	// Would be enabled when server starts to supports slowplay
            || speed == SPEED_2X_FWD
            || speed == SPEED_4X_FWD
            || speed == SPEED_8X_FWD
            || speed == SPEED_16X_FWD
            || speed == SPEED_64X_FWD
            || speed == SPEED_2X_REV
            || speed == SPEED_4X_REV
            || speed == SPEED_8X_REV
            || speed == SPEED_16X_REV
            || speed == SPEED_64X_REV)
    {
        LOG(DLOGL_REALLY_NOISY, "Speed:%0.02f supported", speed);
        return true;
    }
    else
    {
        LOG(DLOGL_ERROR, "Unsupported Speed:%0.02f ", speed);
        return false;
    }
}

void CloudDvr_StreamControl::StreamSetSpeed(signed short num, unsigned short den)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    if ((num == 0) || (den == 0))
    {
        LOG(DLOGL_ERROR, "Invalid values : num=%d den=%d", num, den);
    }
    else
    {
        float scale = (float) num / den;

        if (IsCurrentSpeedSupported(scale))
        {
            uint32_t waitCount = 0;
            bool gotPlayResp = false;
            lockStreamerMutex();
            mTempNumerator = num;
            mTempDenominator = den;
            unlockStreamerMutex();
            LOG(DLOGL_MINOR_EVENT, "Speed set to:%f num=%d den=%d", scale, mTempNumerator, mTempDenominator);
            StreamPlay(true);
            while (waitCount < RTSP_TIMEOUT * RTSP_TIMEOUT_OFFSET)
            {
                lockStreamerMutex();
                gotPlayResp = mStatusPlayResponse;
                unlockStreamerMutex();

                if (gotPlayResp == true) break;

                waitCount++;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Unsupported speed mode:%0.02f num=%d den=%d", scale, num, den);
        }
    }

    lockStreamerMutex();
    mStatusPlayResponse = false;
    unlockStreamerMutex();
}
eIOnDemandStatus CloudDvr_StreamControl::StreamGetSpeed(int16_t *num, uint16_t *den)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    eIOnDemandStatus status = ON_DEMAND_ERROR;
    uint32_t waitCount = 0;

    if ((num == NULL) || (den == NULL) || mRtspClient == NULL)
    {
        LOG(DLOGL_ERROR, "Invalid Parameters num:%p den:%p or mRtspClient:%p", num, den, mRtspClient);
    }
    else
    {
        if (mRtspClient->sendGetParameterCommand(*mRtspSession, ResponseGETSPEED, "scale" , NULL) != 0)
        {
            LOG(DLOGL_SIGNIFICANT_EVENT, "Sent a GetSpeed command to the RTSP server..Waiting for the response from server..");
            while (waitCount < RTSP_TIMEOUT * RTSP_TIMEOUT_OFFSET)
            {
                if (mStatusRespGetSpeed == RTSP_RESPONSE_OK)
                {
                    lockStreamerMutex();
                    *num = numerator;
                    *den = denominator;
                    unlockStreamerMutex();
                    status = ON_DEMAND_OK;
                    break;
                }
                else if (mStatusRespGetSpeed == RTSP_RESPONSE_ERROR)
                {
                    LOG(DLOGL_ERROR, "response error for GetSpeed");
                    break;
                }
                waitCount++;
                usleep(HUNDRED_MS);
            }
            lockStreamerMutex();
            mStatusRespGetSpeed = RTSP_NO_RESPONSE_YET;		//reset it back for use on next transaction
            unlockStreamerMutex();
        }
        else
        {
            LOG(DLOGL_ERROR, "%s:[%d] StreamGetSpeed Error", __FUNCTION__, __LINE__);
        }
    }
    return status;
}

void CloudDvr_StreamControl::StreamSetNPT(float npt)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    lockStreamerMutex();
    nptPosition = npt / 1000;	//convert from milliseconds to seconds
    unlockStreamerMutex();
    StreamPlay(false);
}

eIOnDemandStatus CloudDvr_StreamControl::StreamPause()
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    eIOnDemandStatus status = ON_DEMAND_OK;

    if (mRtspClient != NULL)
    {
        if (mRtspClient->sendPauseCommand(*mRtspSession, ResponsePAUSE) != 0)
        {
            LOG(DLOGL_SIGNIFICANT_EVENT, "Sent a Pause command to the RTSP server");
        }
        else
        {
            status = ON_DEMAND_ERROR;
            LOG(DLOGL_ERROR, "StreamPause Error");
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "mRtspClient is %p", mRtspClient);
        status = ON_DEMAND_ERROR;
    }

    return status;
}

eIOnDemandStatus CloudDvr_StreamControl::StreamTearDown()
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    eIOnDemandStatus status = ON_DEMAND_OK;
    status = StreamPause();
    mShutDownFlag = 1;

    //deleting the mEventTimer
    if (mEventTimer && mEventLoop)
    {
        LOG(DLOGL_MINOR_DEBUG, "Deleting RTSP KeepAlive Timer. mEventTimer %p", mEventTimer);
        mEventLoop->delTimer(mEventTimer);
        mEventTimer = NULL;
    }
    return status;
}

void CloudDvr_StreamControl::ReadStreamSocketData()
{
    LOG(DLOGL_FUNCTION_CALLS, "");
}

void CloudDvr_StreamControl::HandleStreamResp()
{
    LOG(DLOGL_FUNCTION_CALLS, "");
}

void CloudDvr_StreamControl::processResponseGetPos(int resultCode, char* resultString)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    if (!ptrOnDemand)
    {
        LOG(DLOGL_ERROR, "OnDemand Instance:%p", ptrOnDemand);
        return;
    }

    if ((resultCode != RTSP_RESULT_CODE_OK) || (resultString == NULL))
    {
        LOG(DLOGL_ERROR, "Invalid Parameters. resultCode:%d resultString:%s", resultCode, resultString);
        lockStreamerMutex();
        mStatusRespGetPos = RTSP_RESPONSE_ERROR;
        unlockStreamerMutex();

        ptrOnDemand->queueEvent(kOnDemandStreamErrorEvent);
    }
    else
    {
        lockStreamerMutex();
        nptPosition = atof(resultString);
        LOG(DLOGL_REALLY_NOISY, "nptPosition %f", nptPosition);
        mStatusRespGetPos = RTSP_RESPONSE_OK;
        unlockStreamerMutex();

        ptrOnDemand->queueEvent(kOnDemandRTSPPlaySuccess);
    }
}


void CloudDvr_StreamControl::processResponseGetSpeed(int resultCode, char* resultString)
{
    LOG(DLOGL_FUNCTION_CALLS, "");

    if (!ptrOnDemand)
    {
        LOG(DLOGL_ERROR, "OnDemand Instance:%p", ptrOnDemand);
        return;
    }

    if ((resultCode != RTSP_RESULT_CODE_OK) || (resultString == NULL))
    {
        LOG(DLOGL_ERROR, "Invalid Parameters. resultCode:%d resultString:%s", resultCode, resultString);
        lockStreamerMutex();
        mStatusRespGetSpeed = RTSP_RESPONSE_ERROR;
        unlockStreamerMutex();

        ptrOnDemand->queueEvent(kOnDemandStreamErrorEvent);
    }
    else
    {
        float scale = atof(resultString);
        LOG(DLOGL_REALLY_NOISY, "scale %f", scale);
        lockStreamerMutex();
        denominator = 100;
        numerator = (int16_t)scale * 100;
        mStatusRespGetSpeed = RTSP_RESPONSE_OK;
        unlockStreamerMutex();

        ptrOnDemand->queueEvent(kOnDemandRTSPPlaySuccess);
    }
}

// Implementation of "RTSPClientInterface":
RTSPClientInterface* RTSPClientInterface::createNew(UsageEnvironment& env, char const* rtspURL,
        int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum)
{
    return new RTSPClientInterface(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum);
}

RTSPClientInterface::RTSPClientInterface(UsageEnvironment& env, char const* rtspURL,
        int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum)
    : RTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum, -1)
{
    pStreamControl = NULL;
}

RTSPClientInterface::~RTSPClientInterface()
{
    LOG(DLOGL_FUNCTION_CALLS, "");
    pStreamControl = NULL;
}

void CloudDvr_StreamControl::StartSessionKeepAliveTimer()
{
    LOG(DLOGL_FUNCTION_CALLS, "StartSessionKeepAliveTimer");
    CCloudDvrConfigFileParser *pCDvrCfp = CCloudDvrConfigFileParser::getInstance();

    if (pCDvrCfp)
    {
        uint32_t keepAliveTime = pCDvrCfp->getRtspKeepAliveTimeout();

        //if keepAliveTime is not set, falling back to default value
        if (keepAliveTime == 0)
        {
            keepAliveTime = DEFAULT_RTSP_KEEPALIVE_TIMEOUT;
        }

        LOG(DLOGL_MINOR_DEBUG, "SessionTimeout Value=%d", keepAliveTime);

        // Add the timer to send keepAlive at the interval defined in config.ini(session_timeout)
        if (mEventLoop)
        {
            mEventTimer = mEventLoop->addTimer(EVENTTIMER_PERSIST,
                                               keepAliveTime,
                                               0,
                                               SessionKeepAliveTimerCallBack,
                                               this);
            if (!mEventTimer)
            {
                LOG(DLOGL_ERROR, "Failed to add RTSP KeepAlive timer");
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Invalid RTSP KeepAlive EventLoop: %p", mEventLoop);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Invalid CCloudDvrConfigFileParser Instance pCDvrCfp: %p", pCDvrCfp);
    }
}

void CloudDvr_StreamControl::SessionKeepAliveTimerCallBack(int fd, short event, void *arg)
{
    LOG(DLOGL_FUNCTION_CALLS, "SessionKeepAliveTimerCallBack");
    UNUSED_PARAM(fd);
    UNUSED_PARAM(event);

    EventTimer* pTimer = reinterpret_cast<EventTimer*>(arg);
    if (pTimer)
    {
        CloudDvr_StreamControl* pCsc = reinterpret_cast<CloudDvr_StreamControl*>(pTimer->getUserData());
        if (pCsc)
        {
            //send the RTSP KeepAlive command to keep the session active
            eIOnDemandStatus status = pCsc->SendKeepAlive();
            if (status != ON_DEMAND_OK)
            {
                LOG(DLOGL_ERROR, "RTSP SendKeepAlive returned with Error: %d", status);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Invalid pCsc: %p", pCsc);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Invalid RTSP Keep Alive pTimer: %p", pTimer);
    }
}
