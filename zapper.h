/**
   \file zapper.h
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

// SAIL includes
#include <sail-mme-api.h>
#include <sail-mediaplayersession-api.h>

// MSP includes
#include "MspCommon.h"
#include "DisplaySession.h"
#include "IMediaController.h"
#include "psi.h"
#include "MSPSourceFactory.h"
#include "AnalogPsi.h"
// cpe includes
#include <cpe_source.h>
#include <directfb.h>
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
    kZapperStateRendering,
    kZapperStateSuspended
} eZapperState;

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


    eIMediaPlayerStatus Load(tCpeSrcRFTune tuningParams,
                             int programNumber,
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

    tCpePgrmHandle getCpeProgHandle();
    void SetCpeStreamingSessionID(uint32_t sessionId);
    void InjectCCI(uint8_t CCIbyte);
    eIMediaPlayerStatus StopStreaming();
    void startEasAudio(void);
    void SetEasAudioActive(bool active);

protected:
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
        kZapperPmtRevUpdateEvent,
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
        kZapperEventSDVLoaded
    } eZapperEvent;

    eMspStatus queueEvent(eZapperEvent evtyp);
    static void* eventthreadFunc(void *data);
    int  createEventThread();
    virtual bool handleEvent(Event *evt);
    MSPSource *mSource;
    displaySessioncallbacktype displaySessionCallbackFunction;
    std::string GetSourceURL(bool liveSrcOnly = false)const;
    std::string GetDestURL()const;
    bool isLiveSourceUsed() const;
    bool isRecordingPlayback() const;
    bool isLiveRecording()const;

    eIMediaPlayerStatus DoCallback(eIMediaPlayerSignal sig,
                                   eIMediaPlayerStatus stat);
    virtual eIMediaPlayerStatus loadSource();


    static void mediaCB(void *clientInst, tCpeMediaCallbackTypes type);

    static void psiCallback(ePsiCallBackState state,
                            void *data);
    static void sourceReadyCB(void *data, eSourceState aState);
    static void appDataReadyCallbackFn(void *aClientContext);
    void tearDownToRetune();
    void addClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteAllClientSession();

    void SetTunerPriority();
    eZapperState state;
    void tearDown();
    float mNptPendingSeek;
    DFBRectangle screenRect;
    bool enaPicMode;
    bool enaAudio;
    bool isPresentationStarted;	   /**< Music channels require PMT info to be ready before Playing event is sent*/
    DisplaySession *disp_session;  /**< pointer to our display session NULL if not created yet */
    void callbackToClient(eZapperState state);
    void* mCBData;
    Psi *psi;  /**< pointer to our PSI instance, NULL if not created yet */
    AnalogPsi *mPtrAnalogPsi;
    static bool mEasAudioActive;

private:

    std::string dest_url;  /**< destination url as passed to Play */
    MSPEventQueue* threadEventQueue;
    pthread_t eventHandlerThread;
    pthread_mutex_t  mMutex;
    CCIcallback_t mCCICBFn;
    std::list<CallbackInfo*> mCallbackList;
    boost::signals2::connection displaySessioncallbackConnection;
    static void sourceCB(void *data, eSourceState aState);

    // TODO:  possibly implement callback list if callback needed by more clients
    void                    *mCbClientContext;     // client context intended for OnDemand
    zapperCallbackFunction  mCallbackFn;           // callback function intended for OnDemand


    bool mIsVod;   // true if this Zapper belongs to a VOD session
    eMSPSourceType mCurrentSource;
    IMediaPlayerSession *mIMediaPlayerSession;
    std::list<IMediaPlayerClientSession *> mAppClientsList;
};

#endif




