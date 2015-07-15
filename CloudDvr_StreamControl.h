/** @file CloudDvr_StreamControl.h
 *
 * @brief Cloud Dvr StreamControl header.
 *
 * @author Laliteshwar Prasad Yadav
 *
 * @version 1.0
 *
 * @date 08.05.2013
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#ifndef _CLOUDDVR_STREAMCONTROL_H
#define _CLOUDDVR_STREAMCONTROL_H

#include "VOD_StreamControl.h"
#include "vod.h"
#include "ondemand.h"
#include "liveMedia.hh"
#include <string>
#include "BasicUsageEnvironment.hh"
#include "UsageEnvironment.hh"
#include "GroupsockHelper.hh"

using namespace std;

//live555 RTSP library defines for PLAY and TRICKPLAY
#define RESUME_NPT      			-1.0f   	//StreamPlay
#define PLAY_TO_END 				-1.0  		//StreamPlay
#define NORMAL_SCALE 				1.0f    	//Scale: FF/RR
#define START_NPT       			0.0f		//SteamPlay

#ifndef UNUSED_PARAM
#define UNUSED_PARAM(x) 			(void)x;
#endif
#define VERBOSITY_LEVEL				0			//TODO: Should be changed to 0 when integrated with msp code
#define HUNDRED_MS 					100000
#define RTSP_TIMEOUT 				3			//Maximum time in seconds until the caller thread waits before returning.
#define CLOUD_KEEP_ALIVE_TIMER		5
#define STACK_SIZE					(1024*256)
#define RTSP_TIMEOUT_OFFSET			10
#define LIBEVENT_ADD_EVENT_SUCCESS	0
#define LIBEVENT_TIMEOUT_FD			-1
#define RTSP_RESPONSE_OK			1
#define RTSP_RESPONSE_ERROR			-1
#define RTSP_NO_RESPONSE_YET		0
#define RTSP_RESULT_CODE_OK			0
#define SPEED_SLOW					0.5
#define SPEED_NORMAL				1
#define SPEED_2X_FWD				2
#define SPEED_4X_FWD				4
#define SPEED_8X_FWD				8
#define SPEED_16X_FWD				16
#define SPEED_64X_FWD				64
#define SPEED_2X_REV				-2
#define SPEED_4X_REV				-4
#define SPEED_8X_REV				-8
#define SPEED_16X_REV				-16
#define SPEED_64X_REV				-64

//STB should not query RTSP server for NPT value frequently.
//It would just create a lot of unnecessary traffic over the network.
//So the minimum time after which the client should send a GetParameter request
//to sync both the calculated npt and the npt from the RTSP server
#define RTSP_GETPARAMETER_QUERY_INTERVAL 300 // it is in seconds

class CloudDvr_StreamControl : public VOD_StreamControl
{
public:
    CloudDvr_StreamControl(OnDemand* pOnDemand, void* pClientMetaDataContext, eOnDemandReqType reqType);
    ~CloudDvr_StreamControl();

    /**
     * \brief The following methods would send a RTSP request to the server either to set/get a parameter or to perform a control action on the stream
     */
    eIOnDemandStatus StreamSetup(std::string url, std::string sessionId);
    eIOnDemandStatus StreamPlay(bool usingStartNPT);
    eIOnDemandStatus SendKeepAlive(void);
    eIOnDemandStatus StreamPause(void);
    eIOnDemandStatus StreamGetPosition(float *npt);
    eIOnDemandStatus StreamGetSpeed(int16_t *num, uint16_t *den);
    eIOnDemandStatus StreamTearDown(void);
    void StreamSetSpeed(int16_t num, uint16_t den);
    void StreamSetNPT(float npt);
    eIOnDemandStatus StreamGetParameter();

    /**
     * \brief This thread keeps the StreamController socket open and checks it for any responsed or asynchronous messages from the RTSP servers
     */
    static void *TaskSchedulerThread(void * arg);

    /**
    * \brief    Mutex protection to provide Thread Safety.
    */
    void lockStreamerMutex(void)
    {
        pthread_mutex_lock(&mMutex_cdvr);
    };
    void unlockStreamerMutex(void)
    {
        pthread_mutex_unlock(&mMutex_cdvr);
    };

    void setPauseMode(bool isPaused)
    {
        Pause_Mode = isPaused;
    };

    void HandleStreamResp();
    void HandleInput(void *ptrMessage);
    void DisplayDebugInfo();
    void ReadStreamSocketData();

    /**
     * \brief Timer Callback function for triggering KeepAlive command
     */
    static void SessionKeepAliveTimerCallBack(int fd, short event, void *arg);
private:


    /**
     * \brief The following varibales keep track of the time the last getposotion query was sent to the server
     * so that after RTSP_GETPARAMETER_QUERY_INTERVAL seconds another query can be sent to sync the npt value with the server's
     */
    time_t mLastGetPosNetworkQueryTime;
    bool mIsAssetUpdatedNPTset;

    /**
     * \brief This is used to calculate the npt in the approximation algorithm
     */
    time_t mNow;
    time_t mLastGetPosAccessTime;


    //Member variables that maintain the Session related information
    MediaSession*		mRtspSession;
    RTSPClient*			mRtspClient;
    TaskScheduler* 		mScheduler;
    UsageEnvironment* 	mEnv;

    //Response status variables 1=OK,-1=ERROR,0=NO RESPONSE YET
    int16_t 	mStatusRespGetPos;
    int16_t 	mStatusRespGetSpeed;

    //Speed variables: holds speed values requested by the Services Layer
    //The actual values would be the ones from the Server responses
    int16_t mTempNumerator;
    uint16_t mTempDenominator;

    //Play response variables used during trickplay
    //Helps in updating the current npt position
    bool 		mStatusPlayResponse;

    //Used to control the taskscheduler event loop
    char mShutDownFlag;

    pthread_mutex_t mMutex_cdvr;;

    //EventLoop and Timer variable used to add timer for KeepAlive
    EventLoop*  mEventLoop;
    EventTimer* mEventTimer;

    /**
     * \brief Function to create the taskscheduler thread
     */
    bool createTaskSchedulerThread(CloudDvr_StreamControl *pInst);

    /**
     * \brief RTSP 'response handlers'
     */
    static void ResponseKEEPALIVE(RTSPClient* rtspClient, int resultCode, char* resultString);
    static void ResponsePLAY(RTSPClient* rtspClient, int resultCode, char* resultString);
    static void ResponsePAUSE(RTSPClient* rtspClient, int resultCode, char* resultString);
    static void ResponseGETPOSITION(RTSPClient* rtspClient, int resultCode, char* resultString);
    static void ResponseGETSPEED(RTSPClient* rtspClient, int resultCode, char* resultString);
    void processResponseGetSpeed(int resultCode, char* resultString);
    void processResponseGetPos(int resultCode, char* resultString);
    void processPLAYResponse(void);

    /**
     * \brief Supported Speed mode check
     * Supported modes are +/- 1/2/4/8/16/64x
     */
    bool IsCurrentSpeedSupported(float speed);

    /**
     * \brief Timer to trigger the KeepAlive command to have the session active
     */
    void StartSessionKeepAliveTimer();
};

class RTSPClientInterface : public RTSPClient
{
public:
    static RTSPClientInterface* createNew(UsageEnvironment& env, char const* rtspURL, int verbosityLevel = 0,
                                          char const* applicationName = NULL, portNumBits tunnelOverHTTPPortNum = 0);
protected:
    RTSPClientInterface(UsageEnvironment& env, char const* rtspURL, int verbosityLevel,
                        char const* applicationName, portNumBits tunnelOverHTTPPortNum);
    // called only by createNew();
    virtual ~RTSPClientInterface();
public:
    CloudDvr_StreamControl* pStreamControl;
};
#endif
