/**
   \file ondemand.h
   \class ondemand
*/

#if !defined(ONDEMAND_H)
#define ONDEMAND_H

// standard linux includes
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string>
#include <pthread.h>

// SAIL includes
#include <sail-mme-api.h>
#include <sail-mediaplayersession-api.h>

// MSP includes
#include "MspCommon.h"
#include "DisplaySession.h"
#include "IMediaController.h"
#include "psi.h"
#include "zapper.h"
#include "MSPSourceFactory.h"
#include "MSPDiagPages.h"
// cpe includes
#include <cpe_source.h>
#include <directfb.h>

class DisplaySession;
class MSPEventQueue;

// Amit include
#include "IOnDemandSystem.h"
#include <eventLoop.h>
#include <clientConnect.h>
#include <event.h>
class VOD_StreamControl;
class VOD_SessionControl;
class OnDemand;
class Zapper;
#include "VODFactory.h"



/**
   Define all possible internal states for OnDemand
*/
typedef enum
{
    kOnDemandStateInit,              //  constructor
    kOnDemandStateWaitForLoad,       //  wait for OnDemand:Load
    kOnDemandStatePreparingToView,   //  wait for OnDemand:Play
    kOnDemandStateSessionPending,    //  wait for DSM-CC Session and tuning params
    kOnDemandSessionServerReady,
    kOnDemandControlServerPending,   //  wait for Stream (response from Play)
    kOnDemandControlServerReady,
    kOnDemandTunerPending,           //  wait for tuner and display
    kOnDemandStateStreaming,
    kOnDemandStateStopPending,
    kOnDemandStateStopped,
} eOnDemandState;

typedef enum
{
    kOnDemandTimeOutEvent = -1,
    kOnDemandEventRFCallback,
    kOnDemandPSIReadyEvent,
    kOnDemandPSIUpdateEvent ,
    kOnDemandEventPlay,
    kOnDemandEventStop,
    kOnDemandEventExit,         // 5
    kOnDemandEventSocketFd,
    kOnDemandEventSessKeepAlive,
    kOnDemandAppDataReady,
    kOnDemandSetupRespEvent,
    kOnDemandPlayReqEvent,      //10
    kOnDemandPlayRespEvent,
    kOnDemandPlayDoneEvent,
    kOnDemandPlayBOFEvent,
    kOnDemandPlayEOFEvent,
    kOnDemandReleaseRespEvent, //15
    kOnDemandDisplayRenderingEvent,
    kOnDemandKeepAliveTimerEvent,
    kOnDemandSessionReadEvent,
    kOnDemandStreamReadEvent,
    kOnDemandSessionErrorEvent, //20
    kOnDemandStreamErrorEvent,
    kOnDemandStopControllerEvent,
    kOnDemandRTSPPlaySuccess,
    kOnDemandRTSPPlayFailed
} eOnDemandEvent;


typedef struct
{
    OnDemand *objPtr;
    eOnDemandEvent odEvtType;
} EventCallbackData;


class OnDemandState
{
public:
    OnDemandState();
    void Change(eOnDemandState newState);
    eOnDemandState Get() const
    {
        return mState;
    }

private:
    eOnDemandState mState;
};



/**
   \class OnDemand
   \brief Media Controller component for MediaPlayer used VOD
   Implements the IMediaController interface.
   This is pretty easy as most of the calls just mirror mediaplayersession calls and are passed through
   directly to us.
*/
class OnDemand : public IMediaController
{
public:

    virtual eIMediaPlayerStatus Load(const char* serviceUrl, const MultiMediaEvent **pMme);
    virtual void                Eject();
    eIMediaPlayerStatus Play(const char* outputUrl, float nptStartTime, const MultiMediaEvent **pMme);
    eIMediaPlayerStatus PersistentRecord(const char* recordUrl, float nptRecordStartTime, float nptRecordStopTime, const MultiMediaEvent **pMme);
    virtual eIMediaPlayerStatus Stop(bool stopPlay, bool stopPersistentRecord);
    eIMediaPlayerStatus SetSpeed(int numerator, unsigned int denominator);
    eIMediaPlayerStatus GetSpeed(int* pNumerator, unsigned int* pDenominator);
    eIMediaPlayerStatus SetPosition(float nptTime);
    eIMediaPlayerStatus GetPosition(float* pNptTime);
    eIMediaPlayerStatus IpBwGauge(const char *sTryServiceUrl, unsigned int *pMaxBwProvision, unsigned int *pTryServiceBw, unsigned int *pTotalBwCoynsumption);
    eIMediaPlayerStatus SetPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus);
    eIMediaPlayerStatus GetStartPosition(float* pNptTime);
    eIMediaPlayerStatus GetEndPosition(float* pNptTime);
    virtual eIMediaPlayerStatus RegisterCCICallback(void* data, CCIcallback_t cb);
    virtual eIMediaPlayerStatus UnRegisterCCICallback();
    eIMediaPlayerStatus SetApplicationDataPid(uint32_t aPid);
    eIMediaPlayerStatus SetApplicationDataPidExt(IMediaPlayerClientSession *ApplnClient);
    eIMediaPlayerStatus GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset);
    eIMediaPlayerStatus GetApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize);
    eIMediaPlayerStatus GetApplicationDataExt(IMediaPlayerClientSession *ApplnClient, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize);
    uint32_t GetSDVClentContext(IMediaPlayerClientSession *ApplnClient);
    eIMediaPlayerStatus SetAudioPid(uint32_t pid);
    eCsciMspDiagStatus GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo);


    std::string GetSourceURL(bool liveSrcOnly = false)const;
    std::string GetDestURL()const;
    bool isRecordingPlayback()const;
    bool isLiveSourceUsed() const;
    bool isLiveRecording()const;
    virtual eIMediaPlayerStatus SetCallback(IMediaPlayerSession *pIMediaPlayerSession, IMediaPlayerStatusCallback cb, void* pClientContext);
    virtual eIMediaPlayerStatus DetachCallback(IMediaPlayerStatusCallback cb);
    eIMediaPlayerStatus CloseDisplaySession();
    eIMediaPlayerStatus StartDisplaySession();
    void addClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteAllClientSession();
    virtual bool isBackground(void);
    void lockMutex(void);
    void unLockMutex(void);
    void StopAudio(void);
    void RestartAudio(void);

    static bool checkOnDemandInstActive(EventCallbackData *event);
    static void OnDemandEvtCallback(int32_t fd, int16_t event, void *arg);
    static void ReadStreamEvtCallback(int32_t fd, int16_t event, void *arg);
    eIMediaPlayerStatus queueEvent(eOnDemandEvent evtyp);

    eOnDemandState  GetOnDemandState() const;
    EventLoop* GetEventLoop()
    {
        return mEventLoop;
    };
    uint8_t * GetStreamServerIPAddress();
    uint16_t GetStreamServerPort();
    uint32_t GetStreamHandle();

    eIOnDemandStatus SetupCloudDVRStream(void);

    OnDemand();
    virtual ~OnDemand();

    static void GetMspVodInfo(DiagMspVodInfo *msgInfo);

    tCpePgrmHandle getCpeProgHandle();
    virtual void SetCpeStreamingSessionID(uint32_t sessionId);
    void InjectCCI(uint8_t CCIbyte);
    void handleReadSessionData(void *pMsg);
    eIMediaPlayerStatus StopStreaming();
    void startEasAudio(void);
    void SetEasAudioActive(bool active);
protected:

    eIMediaPlayerStatus DoCallback(eIMediaPlayerSignal sig, eIMediaPlayerStatus stat);


    static std::list<void*> mActiveControllerList;
    static pthread_mutex_t  mControllerListMutex;
    static bool             mIsActControlListInititialized;

    static void* evtloopthreadFunc(void *data);
    int startEventThread();
    void stopEventThread();


    static void dummyLongTime_cb(evutil_socket_t fd, short event, void *arg);

    virtual void HandleCallback(eOnDemandEvent onDemandEvt);
    void HandleSocketReadEvent(eOnDemandEvent onDemandEvt);
    void StartSessionKeepAliveTimer();

    static void SessionKeepAliveTimerCallBack(int fd, short event, void *arg);
    static void stream_read_callback(evutil_socket_t fd, short event, void *arg);
    static void sourceReadyCB(void *data, eSourceState aState);

    Zapper* tunerController;
    eIMediaPlayerStatus setupSessionAndStreamControllers(const char* serviceUrl);
    static void tunerCallback(eZapperState state, void *data);

    virtual eIMediaPlayerStatus loadTuningParamsAndPlay();

    std::string mSrcUrl;  /**< url of source as passed to load */
    std::string mDestUrl;  /**< destination url as passed to Play */

    float mStartNptMs;   // NPT in milliseconds

    OnDemandState onDemandState; /**< internal state of OnDemand */

    int programNumber;     // progrm number
    int frequency;     // for RF source freq in MHz
    int symbolRate;        // for RF source only
    tCpeSrcRFMode mode;        // for RF source only
    int sourceId;              // needed for CAM for now - may remove later


    uint8_t sessionSetupRetryCount;
    tm *mSessionActivatedTime;

    IMediaPlayerStatusCallback  callback;  /**< services layer callback function */
    void *mpSession;
    void *callbackContext;    /**< client context for callback passed in from SL */
    tCpeSrcCallbackID srcCbId;

    tAvRect mPendingScreenRect;
    bool mPendingEnaPicMode;
    bool mPendingEnaAudio;

    int mCurrentNumerator;
    unsigned int mCurrentDenominator;
    float mCurrentSetPosition;
    bool mPendingPlayResponse;
    bool mPendingSpeed, mPendingPosition;

    // pthread_t eventHandlerThread;
    static pthread_t evtloopHandlerThread;
    pthread_mutex_t  mMutex;

    std::list<CallbackInfo*> mCallbackList;

    MSPSource *mSource;
    boost::signals2::connection displaySessioncallbackConnection;
    VOD_StreamControl  *vodStreamContrl;
    VOD_SessionControl *vodSessContrl;


    static EventLoop* mEventLoop;
    EventTimer* mDummyLongTimer;

    EventTimer* mEventTimer;

    struct event* mStreamSocketEvent;


    pthread_mutex_t  mStopMutex;
    pthread_cond_t mStopCond;
    static bool mEasAudioActive;
};

#endif




