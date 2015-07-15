/**
   \file zapper_ic.h
   \class zapper
*/

#if !defined(ZAPPER_H)
#define ZAPPER_H

// standard linux includes
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string>
#include <pthread.h>
#include <sys/time.h>

// SAIL includes
#include <sail-mme-api.h>
#include <sail-mediaplayersession-api.h>

// MSP includes
#include "MspCommon.h"
#include "IMediaController.h"
#include "MSPSourceFactory.h"
#include "psi_ic.h"

#if RTT_TIMER_RETRY
#include "MediaRTT_ic.h"
#endif

#include "csci-mrdvr-sched-api.h"
#include "csci-websvcs-ipclient-streaming-api.h"

class DisplaySession;
class MSPEventQueue;
class Event;


typedef enum
{
    kZapperStateIdle,
    kZapperStateProcessingRequest,
    kZapperWaitForTuner,
    kZapperWaitTuningParams,
    kZapperWaitSourceReady,
    kZapperTunerLocked,
    kZapperStateStop,
    kZapperStateRendering
} eZapperState;

typedef enum
{
    kSessionTunerRevoked,
    kSessionTunerConflict,
    kSessionTunerDenied,
    kSessionTunerGranted,
    kSessionDeAuthorized,
    kSessionAuthorized,
    kSessionLoading,
    kSessionStarted,
    kSessionIdle,
} eSessionState;

//PayLoad Structure to store WSF data and dispatch
#ifdef __cplusplus
extern "C" {
#endif
    typedef struct MediaSsePayload
    {
        eIMediaPlayerSignal tSignal;
        eIMediaPlayerStatus tStatus;
        char tDesc[MAX_MEDIASTREAM_NOTIFY_DESC_LENGTH];
    } tMediaSsePayload;

#ifdef __cplusplus
}
#endif
typedef void (*zapperCallbackFunction)(eZapperState state, void *clientData);

/**
   \class Zapper
   \brief Media Controller component for MediaPlayer used on non-DVR STB's
   Implements the IMediaController interface.
   This is pretty easy as most of the calls just mirror mediaplayersession calls and are passed through
   directly to us.
*/
class Zapper : public IMediaController
{
public:

    eIMediaPlayerStatus Load(const char* serviceUrl,
                             const MultiMediaEvent **pMme);


    void Eject();
    eIMediaPlayerStatus Play(const char* outputUrl,
                             float nptStartTime,
                             const MultiMediaEvent **pMme);
    eIMediaPlayerStatus PersistentRecord(const char* recordUrl,
                                         float nptRecordStartTime,
                                         float nptRecordStopTime,
                                         const MultiMediaEvent **pMme);
    eIMediaPlayerStatus Stop(bool stopPlay,
                             bool stopPersistentRecord);
    virtual eIMediaPlayerStatus SetSpeed(int numerator,
                                         unsigned int denominator);
    virtual eIMediaPlayerStatus GetSpeed(int* pNumerator,
                                         unsigned int* pDenominator);
    virtual eIMediaPlayerStatus SetPosition(float nptTime);
    virtual eIMediaPlayerStatus GetPosition(float* pNptTime);
    eIMediaPlayerStatus IpBwGauge(const char *sTryServiceUrl,
                                  unsigned int *pMaxBwProvision,
                                  unsigned int *pTryServiceBw,
                                  unsigned int *pTotalBwCoynsumption);
    eIMediaPlayerStatus SetPresentationParams(tAvRect *vidScreenRect,
            bool enablePictureModeSetting,
            bool enableAudioFocus);

    eIMediaPlayerStatus GetPresentationParams(tAvRect *vidScreenRect,
            bool *pEnablePictureModeSetting,
            bool *pEnableAudioFocus);
    virtual eIMediaPlayerStatus GetStartPosition(float* pNptTime);
    eIMediaPlayerStatus GetEndPosition(float* pNptTime);
    eIMediaPlayerStatus RegisterCCICallback(void* data,
                                            CCIcallback_t cb);
    eIMediaPlayerStatus UnRegisterCCICallback();
    virtual eIMediaPlayerStatus SetApplicationDataPid(uint32_t aPid);
    virtual eIMediaPlayerStatus SetApplicationDataPidExt(IMediaPlayerClientSession *ApplnClient);
    virtual eIMediaPlayerStatus GetComponents(tComponentInfo *info,
            uint32_t infoSize,
            uint32_t *cnt,
            uint32_t offset);
    eIMediaPlayerStatus GetApplicationData(uint32_t bufferSize,
                                           uint8_t *buffer,
                                           uint32_t *dataSize);

    uint32_t GetSDVClentContext(IMediaPlayerClientSession *ApplnClient);

    eIMediaPlayerStatus GetApplicationDataExt(IMediaPlayerClientSession *ApplnClient, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize);

    virtual eIMediaPlayerStatus SetAudioPid(uint32_t pid);


    eIMediaPlayerStatus SetCallback(IMediaPlayerSession *pIMediaPlayerSession,
                                    IMediaPlayerStatusCallback cb,
                                    void* pClientContext);
    eIMediaPlayerStatus DetachCallback(IMediaPlayerStatusCallback cb);
    eIMediaPlayerStatus CloseDisplaySession();
    eIMediaPlayerStatus StartDisplaySession();
    bool isBackground(void);
    static void audioLanguageChangedCB(void *cbData);
    static void SapChangedCB(void *aCbData);
    void lockMutex(void);
    void unLockMutex(void);
    void registerZapperCallback(zapperCallbackFunction cb_fun, void* clientData);
    eCsciMspDiagStatus GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo);
    Zapper(bool isVod = false, IMediaPlayerSession *pIMediaPlayerSession = NULL);
    void StopAudio(void);
    void RestartAudio(void);
    virtual ~Zapper();
    eCsciMspDiagStatus GetMspStreamingInfo(DiagMspStreamingInfo *msgInfo);
    /* Returns the CCI byte value */
    uint8_t GetCciBits(void);

    /*
     * This function will be used to register for receiving last change event for vod specific CDS objects.
     */
    static void vodCdsEventCallbackFunc(tVodClientInfo clientInfo, void* pContext);

    /*
     * This function will be used to set vodContext info received after successful session setup
     */
    eIMediaPlayerStatus setVodClientInfo(tVodClientInfo clientInfo);

    tAvRect mVidScreenRect;
    /**
    * @param status [IN] Media player status sent by the gateway via SSE for the current source
    * @param signal [IN] Media player signal sent by the gateway via SSE for the current source
    * @param desc [IN] Descriptive message sent be the gateway to identify the type of error event
    * @return None
    * @brief callback function handler that will handle media player error events gracefully and take respective actions as required
    */
    void MediaCallbackFuncHandler(eIMediaPlayerSignal signal, eIMediaPlayerStatus status, char* desc);

    void StopDeletePSI();

    void dispatchWebServiceEvent(tMediaSsePayload *pData);

    void startEasAudio(void);
    void SetEasAudioActive(bool active);

protected:

    std::string dest_url;  /**< destination url as passed to Play */

    /**
       Define all possible internal states for zapper
    */

    typedef enum
    {
        kZapperTimeOutEvent = -1,
        kZapperTunerLockedEvent,
        kZapperEventRFAnalogCallback,
        kZapperEventPlayBkCallback,
        kZapperPSIDataEvent,
        kZapperPSIReadyEvent,
        kZapperAnalogPSIReadyEvent,
        kZapperPSIUpdateEvent,
        kZapperEventTuningUpdate,
        kZapperEventPlay,
        kZapperEventStop,
        kZapperAppDataReady,
        kZapperEventExit,
        kZapperPpvInstStart,
        kZapperPpvInstStop,
        kZapperPpvSubscnAuth,
        kZapperPpvSubscnExprd,
        kZapperPpvStartVideo,
        kZapperPpvStopVideo,
        kZapperFirstFrameEvent,
        kZapperTunerLost,
        kZapperTunerRestored,
        kZapperSrcBOF,
        kZapperSrcEOF,
        kZapperSrcProblem,
        kZapperSrcPendingSetSpeed,
        kZapperSrcPendingSetPosition,
        kDvrEventBOF,
        kZapperEventEOF,
        kZapperEventSDVLoading,
        kZapperEventSDVUnavailable,
        kZapperEventSDVCancelled,
        kZapperEventSDVKeepAliveNeeded,
        kZapperEventServiceAuthorized,
        kZapperEventServiceDeAuthorized,
        kZapperEventAudioLangChangeCb,
        kZapperEventSDVLoaded,
        kZapperPMTReadyEvent,
#if RTT_TIMER_RETRY
        kZapperWebSvcEvent,
        kZapperEventRTTRetry //Introducing new event for MediaRTT Retry
#else
        kZapperWebSvcEvent
#endif
    } eZapperEvent;

    eMspStatus queueEvent(eZapperEvent evtyp);

    eMspStatus queueEvent(eZapperEvent evtyp, void* pData);

    static void* eventthreadFunc(void *data);
    int  createEventThread();
    virtual bool handleEvent(Event *evt);
    MSPSource *mSource;
    std::string GetSourceURL(bool liveSrcOnly = false)const;
    std::string GetDestURL()const;
    bool isLiveSourceUsed() const;
    bool isRecordingPlayback() const;
    bool isLiveRecording()const;

    eIMediaPlayerStatus DoCallback(eIMediaPlayerSignal sig,
                                   eIMediaPlayerStatus stat);
    virtual eIMediaPlayerStatus loadSource();

    static void sourceReadyCB(void *data, eSourceState aState);
    static void appDataReadyCallbackFn(void *aClientContext);
    void tearDownToRetune();
    void addClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteAllClientSession();

    void SetTunerPriority();
    eZapperState state;
    eSessionState mSessionState;
    void tearDown();
    float mNptPendingSeek;
    bool enaPicMode;
    bool enaAudio;
    bool isPresentationStarted;	   /**< Music channels require PMT info to be ready before Playing event is sent*/
    void callbackToClient(eZapperState state);
    void* mCBData;
    Psi *mPsi;  /**< pointer to our PSI instance, NULL if not created yet */
    static bool mEasAudioActive;
    void Retry();

private:

    MSPEventQueue* threadEventQueue;
    pthread_t eventHandlerThread;
    pthread_mutex_t  mMutex;
    CCIcallback_t mCCICBFn;
    std::list<CallbackInfo*> mCallbackList;
    static void sourceCB(void *data, eSourceState aState);

#if RTT_TIMER_RETRY
    static void RTTRetryCallback(void *data);
#endif

    // TODO:  possibly implement callback list if callback needed by more clients
    void                    *mCbClientContext;     // client context intended for OnDemand
    zapperCallbackFunction  mCallbackFn;           // callback function intended for OnDemand


    bool mIsVod;   // true if this Zapper belongs to a VOD session
    eMSPSourceType mCurrentSource;
    IMediaPlayerSession *mIMediaPlayerSession;
    std::list<IMediaPlayerClientSession *> mAppClientsList;

    /*
     * This variable will differentiate between live play and vod play. Idea is to use same Zapper controller
     * for live streaming as well as vod streaming. Live streaming and Vod streaming seems to share same functionality
     * except that Vod needs CDS processing.
     */
    bool mIsVodPlay;
    char mVodSessionId[MAX_VOD_SESSION_ID_LEN + 1];
    tVodClientInfo mVodClientInfo;

    int mCurrentNumerator;
    unsigned int mCurrentDenominator;
    float mEndPosition;
    float mPosition;
    struct timeval mTsbStart, mTsbEnd;
};

#endif




