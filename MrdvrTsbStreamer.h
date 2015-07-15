/**
   \file dvr.h
   \class dvr
*/

#if !defined(MRDVR_TSB_STREAMER_H)
#define MRDVR_TSB_STREAMER_H

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
#include "MSPHnOnDemandStreamerSource.h"
#include "ApplicationDataExt.h"
// cpe includes
#include <cpe_source.h>
#include <directfb.h>
#include <cpe_common.h>
#include "dvr.h"
#include "cpe_cam.h"
class DisplaySession;
class MSPRecordSession;
class MSPEventQueue;
class Event;
class MSPFileSource;
class InMemoryStream;
#define TSB_DwellTime 10  //dwell time 10 seconds

typedef enum
{
    kHnSessionIdle,
    kHnSessionAuth,
    kHnSessionDeAuth,
    kHnSessionUnlocked,
    kHnSessionLocked,
    kHnSessionPsiTimeOut,
    kHnSessionPsiAcquired,
    kHnSessionPsiUpdate,
    kHnSessionTsbCreated,
    kHnSessionTimedOut,
    kHnSessionStarted
} HnSessionState;

/**
   Define all possible internal states for dvr
   TODO:  may add to this during implementation
*/

/**
   \class MrdvrTsbStreamer
   \brief Media Controller component for MediaPlayer used on DVR STB's
   Implements the IMediaController interface.
   This is pretty easy as most of the calls just mirror media player session calls and are passed through
   directly to us.
*/
class MrdvrTsbStreamer : public IMediaController
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
    void addClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteAllClientSession();

    MrdvrTsbStreamer(IMediaPlayerSession *pIMediaPlayerSession);
    ~MrdvrTsbStreamer();

    void StartPSITimeout();
    static void* PsiTimerFunc(void *aData);


    tCpePgrmHandle getCpeProgHandle();
    void SetCpeStreamingSessionID(uint32_t sessionId);
    void ValidateSessionState();
    void InjectCCI(uint8_t CCIbyte);
    void StartTunerStreaming();
    void StreamingEntitlementCallback(pEntitlementStatus entStatus);
    static void InMemoryStreamingEntitlemntCallback(void *pData, pEntitlementStatus entStatus);
    bool IsInMemoryStreaming();
    void NotifyServeFailure();
    eIMediaPlayerStatus StopStreaming();
    void startEasAudio(void);
    void SetEasAudioActive(bool active);
private:

    eMspStatus parseSource(const char *aServiceUrl);
    eDvrState getDvrState(void);
    static void* eventthreadFunc(void *data);
    static void sourceCB(void *aData, eSourceState aSrcState);
    static void psiCallback(ePsiCallBackState state, void *data);
    static void analogPsiCallback(ePsiCallBackState state, void *data); // For Analog support
    static void recordSessionCallback(tCpeRecCallbackTypes type, void *data);
    static void appDataReadyCallbackFn(void *aClientContext);
    eIMediaPlayerStatus DoCallback(eIMediaPlayerSignal sig, eIMediaPlayerStatus stat);
    eMspStatus StartTSBRecordSession(std::string first_rec_fragment);
    eMspStatus StopRecordSession();
    void tearDownToRetune();
    void tearDownClientSession();
    void StopDeletePSI();
    void StopDeleteAnalogPSI();
    bool dispatchEvent(Event *evt);
    eMspStatus queueEvent(eDvrEvent evtyp);
    tCpeCamCaHandle mCamCaHandle;
    int mregid ;
    int mEntitleid;
    std::string mDestUrl;  /**< destination url as passed to Play */
    bool mIsBackground;
    bool mPsiReady;
    MSPRecordSession *mPtrRecSession;
    unsigned int mTsbNumber;
    int mTsbHardDrive;  // -1 unspecified, 0 - internal, 1 - external
    eResMonPriority mTunerPriority;
    Psi *mPtrPsi;  /**< pointer to our PSI instance, NULL if not created yet */
    AnalogPsi *mPtrAnalogPsi; // For Analog support, NULL if not created yet.
    eDvrState mState; /**< internal state of dvr */
    eDvrSource mCurrentSourceType;
    eTsbState mTsbState;
    MSPSource *mPtrLiveSource;
    MSPSource *mPtrTsbStreamerSource;
    MSPSource *mPtrHnOnDemandRFSource;
    MSPHnOnDemandStreamerSource *mPtrHnOnDemandStreamerSource;
    InMemoryStream *mptrcaStream;
    uint32_t mSessionId;
    MSPEventQueue* mThreadEventQueue;
    pthread_t mEventHandlerThread;
    pthread_mutex_t  mMutex;
    std::list<CallbackInfo*> mCallbackList;
    boost::signals2::connection mRecordSessioncallbackConnection;
    recordSessioncallbacktype recordSessionCallbackFunction;
    eMSPSourceType mCurrentSource;
    IMediaPlayerSession *mIMediaPlayerSession;

    HnSessionState mReqState;
    pthread_t mPsiTimeoutThread;
    void* mCBData;
    CCIcallback_t mCCICBFn;
    uint8_t m_CCIbyte;
    std::list<IMediaPlayerClientSession *> mAppClientsList;
    static bool mEasAudioActive;

};

#endif




