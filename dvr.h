/**
   \file dvr.h
   \class dvr
*/

#if !defined(DVR_H)
#define DVR_H

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
#include "IMediaController.h"
#include "psi.h"
#include "AnalogPsi.h"
#include "RecordSession.h"
#include "DisplaySession.h"
#include "MSPSourceFactory.h"
#include "ApplicationDataExt.h"

// cpe includes
#include <cpe_source.h>
#include <directfb.h>
#include <cpe_common.h>
class DisplaySession;
class MSPRecordSession;
class MSPEventQueue;
class Event;
class MSPFileSource;
#define TSB_DwellTime 10  //dwell time 10 seconds

/**
   Define all possible internal states for dvr
   TODO:  may add to this during implementation
*/

typedef enum
{
    kTsbNotCreated,
    kTsbCreated,
    kTsbStarted
} eTsbState;

typedef enum
{
    kDvrNoSrc,
    kDvrLiveSrc,
    kDvrFileSrc,
    kDvrOtherSrc
} eDvrSource;

typedef enum
{
    kDvrStateIdle,
    kDvrWaitForTuner,
    kDvrWaitTuningParams,
    kDvrWaitSourceReady,
    kDvrSourceReady,
    kDvrStateRendering,
    kDvrStateStop,
    kDvrStateSuspended,
    kDvrStateStreaming
} eDvrState;

typedef enum
{
    kPersistentRecordNotStarted,
    kPersistentRecordWaitingPMT,
    kPersistentRecordWaitingTSB,
    kPersistentRecordStarted
} ePersistentRecordState;

typedef struct
{
    tCpePgrmHandle mediaHandle;
    char* recFileName;
    tCpePgrmHandle recordingHandle;
    tCpeSrcHandle srcHandle;
} CurrentVideoData;

typedef enum
{
    kDvrTimeOutEvent = -1,
    kDvrEventRFCallback,
    kDvrEventTuningUpdate,
    kDvrEventPlayBkCallback,
    kDvrPSIReadyEvent,
    kDvrPlayBkPSIReadyEvent,
    kDvrPSIUpdateEvent,
    kDvrPmtRevUpdateEvent,
    kDvrEventPlay,
    kDvrNextFilePlay,
    kDvrDwellTimeExpiredEvent,
    kDvrTSBStartEvent,
    kDvrRecordingStoppedEvent,
    kDvrRecordingAborted,
    kDvrRecordingDiskFull,
    kDvrEventStop,
    kDvrEventExit,
    kDvrSchedRecEvent,
    kDvrPpvInstStart,
    kDvrPpvInstStop,
    kDvrPpvSubscnAuth,
    kDvrPpvSubscnExprd,
    kDvrPpvStartVideo,
    kDvrPpvStopVideo,
    kDvrPpvContentNotFound,
    kDvrAppDataReady,
    kDvrEventRFAnalogCallback,
    kDvrAnalogPSIReadyEvent,
    kDvrLiveFwd,
    kDvrFirstFrameEvent,
    kDvrTunerLost,
    kDvrTunerRestored,
    kDvrEventBOF,
    kDvrEventEOF,
    kDvrEventSDVLoading,
    kDvrEventSDVUnavailable,
    kDvrEventSDVCancelled,
    kDvrEventSDVKeepAliveNeeded,
    kDvrEventServiceAuthorized,
    kDvrEventServiceDeAuthorized,
    kDvrEventAudioLangChangeCb,
    kDvrEventSDVLoaded,
    kDvrEventTunerUnlocked

} eDvrEvent;

/**
   \class Dvr
   \brief Media Controller component for MediaPlayer used on DVR STB's
   Implements the IMediaController interface.
   This is pretty easy as most of the calls just mirror mediaplayersession calls and are passed through
   directly to us.
*/
class Dvr : public IMediaController
{
public:
    eIMediaPlayerStatus Load(const char* serviceUrl, const MultiMediaEvent **pMme);
    void                Eject();
    eIMediaPlayerStatus Play(const char* outputUrl, float nptStartTime, const MultiMediaEvent **pMme);
    eIMediaPlayerStatus PersistentRecord(const char* recordUrl, float nptRecordStartTime, float nptRecordStopTime, const MultiMediaEvent **pMme);
    eIMediaPlayerStatus Stop(bool stopPlay, bool stopPersistentRecord);
    eIMediaPlayerStatus SetSpeed(int numerator, unsigned int denominator);
    eIMediaPlayerStatus GetSpeed(int* pNumerator, unsigned int* pDenominator);
    eIMediaPlayerStatus SetPosition(float nptTime);
    eIMediaPlayerStatus GetPosition(float* pNptTime);
    eIMediaPlayerStatus IpBwGauge(const char *sTryServiceUrl, unsigned int *pMaxBwProvision, unsigned int *pTryServiceBw, unsigned int *pTotalBwCoynsumption);
    eIMediaPlayerStatus SetPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus);
    eIMediaPlayerStatus GetStartPosition(float* pNptTime);
    eIMediaPlayerStatus GetEndPosition(float* pNptTime);
    eIMediaPlayerStatus RegisterCCICallback(void* data, CCIcallback_t cb);
    eIMediaPlayerStatus UnRegisterCCICallback();
    std::string GetSourceURL(bool liveSrcOnly = false)const;
    std::string GetDestURL()const;
    bool isRecordingPlayback()const;
    bool isLiveRecording()const;
    eIMediaPlayerStatus SetCallback(IMediaPlayerSession *pIMediaPlayerSession, IMediaPlayerStatusCallback cb, void* pClientContext);
    eIMediaPlayerStatus DetachCallback(IMediaPlayerStatusCallback cb) ;


    eIMediaPlayerStatus SetApplicationDataPid(uint32_t aPid);
    eIMediaPlayerStatus SetApplicationDataPidExt(IMediaPlayerClientSession *ApplnClient);
    eIMediaPlayerStatus GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset);
    eIMediaPlayerStatus GetApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize);
    eIMediaPlayerStatus GetApplicationDataExt(IMediaPlayerClientSession *ApplnClient, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize);
    uint32_t GetSDVClentContext(IMediaPlayerClientSession *ApplnClient);
    eIMediaPlayerStatus SetAudioPid(uint32_t pid);
    eIMediaPlayerStatus CloseDisplaySession();
    eIMediaPlayerStatus StartDisplaySession();
    eCsciMspDiagStatus GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo);
    bool isBackground(void);
    void lockMutex(void);
    void unLockMutex(void);
    void StopAudio(void);
    void RestartAudio(void);
    bool isLiveSourceUsed() const;
    int bump_packets(void);
    void addClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteAllClientSession();

    Dvr(IMediaPlayerSession *pIMediaPlayerSession);
    ~Dvr();

    tCpePgrmHandle getCpeProgHandle();
    void SetCpeStreamingSessionID(uint32_t sessionId);
    uint8_t mCCIbyte ;
    void InjectCCI(uint8_t CCIbyte);
    eIMediaPlayerStatus StopStreaming();
    void startEasAudio(void);
    void SetEasAudioActive(bool active);
private:

    eMspStatus loadRFSrc(void);
    eMspStatus parseSource(const char *aServiceUrl);
    eMspStatus setDisplay(void);
    eMspStatus setSpeedMode(int num, unsigned int den, tCpePlaySpeed *pPlaySpeed);
    eDvrState getDvrState(void);

    static void* dwellTimerFun(void *data);
    static void* eventthreadFunc(void *data);
    static void sourceCB(void *aData, eSourceState aSrcState);
    static void mediaCB(void *clientInst, tCpeMediaCallbackTypes type);
    static void psiCallback(ePsiCallBackState state, void *data);
    static void analogPsiCallback(ePsiCallBackState state, void *data); // For Analog support
    static void recordSessionCallback(tCpeRecCallbackTypes type, void *data);
    static void audioLanguageChangedCB(void *aCbData);
    static void SapChangedCB(void *aCbData);
    static void appDataReadyCallbackFn(void *aClientContext);
    eIMediaPlayerStatus DoCallback(eIMediaPlayerSignal sig, eIMediaPlayerStatus stat);
    eMspStatus StartTSBRecordSession(std::string first_rec_fragment);
    eMspStatus StopRecordSession();
    eMspStatus SetDisplaySession();
    void tearDownToRetune();
    void StopDeletePSI();
    void StopDeleteAnalogPSI();
    bool dispatchEvent(Event *evt);
    eMspStatus queueEvent(eDvrEvent evtyp);
    eIMediaPlayerStatus GetNptStopTimeMs(uint32_t *convertedStopNpt);
    eIMediaPlayerStatus GetNptStartTimeMs(uint32_t *convertedStartNptMs, uint32_t *tsbDurationMs);
    void SetRecordDrive();
    void SetRecordFilename();
    void StartDwellTimerForTsb();
    eIMediaPlayerStatus StartRecording();
    eMspStatus StartPersistentRecording();
    void AddLiveSession();
    std::string mDestUrl;  /**< destination url as passed to Play */
    std::string mRecFile;  /**< persistent record file */
    float mRecStartTime;
    float mRecStopTime;
    float mNptSetPos;
    bool mIsBackground;
    bool mIsPatPmtInjected;
    eResMonPriority mPendingRecPriority;
    int mSpeedNumerator;
    unsigned int mSpeedDenominator;
    DisplaySession *mPtrDispSession;  /**< pointer to our display session NULL if not created yet */
    MSPRecordSession *mPtrRecSession;
    unsigned int mTsbNumber;
    int mTsbHardDrive;  // -1 unspecified, 0 - internal, 1 - external
    eResMonPriority mTunerPriority;
    Psi *mPtrPsi;  /**< pointer to our PSI instance, NULL if not created yet */
    AnalogPsi *mPtrAnalogPsi; // For Analog support, NULL if not created yet.
    eDvrState mState; /**< internal state of dvr */
    ePersistentRecordState mPersistentRecordState;
    eDvrSource mCurrentSourceType;
    eTsbState mTsbState;
    bool mPendingTrickPlay;
    int mTrickNum;
    unsigned int mTrickDen;
    MSPSource *mPtrLiveSource;
    MSPSource *mPtrFileSource;
    std::string mFileName;
    DFBRectangle mScreenRect;
    bool mEnaPicMode;
    bool mEnaAudio;
    int mPackets;  // number of packets sent to JS
    MSPEventQueue* mThreadEventQueue;
    pthread_t mEventHandlerThread;
    pthread_t mDwellTimerThread;
    pthread_mutex_t  mMutex;
    std::list<CallbackInfo*> mCallbackList;
    void* mPtrCBData;
    CCIcallback_t mCCICBFn;
    displaySessioncallbacktype displaySessionCallbackFunction;
    boost::signals2::connection mDisplaySessioncallbackConnection;
    eMSPSourceType mCurrentSource;
    IMediaPlayerSession *mIMediaPlayerSession;
    std::list<IMediaPlayerClientSession *> mAppClientsList;
    static bool mEasAudioActive;
};

#endif




