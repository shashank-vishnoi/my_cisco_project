#if !defined(IMEDIAPLAYER_H)
#define IMEDIAPLAYER_H

#include <list>
#include <stdint.h>
#include <pthread.h>
#include <sail-mediaplayersession-api.h>   // public SAIL header file
#include "IMediaController.h"       // internal definition of medial controller object
#include "eventQueue.h"
#include "MspCommon.h"
#include "MSPEventCallback.h"
#include "MSPDiagPages.h"
#include "csci-resmon-api.h"
#include "csci-resmon-common-api.h"

class IMediaPlayerSession;
class IMediaController;

typedef enum
{
    kMediaPlayerEventNotDefined = -1,
    kMediaPlayerEventCCIUpdated,
    kMediaPlayerEventStopPrimAudioAndStartEas,
    kMediaPlayerEventStartPrimAudio,
    kMediaPlayerEventThreadExit
} eMediaPlayerEvent;

typedef struct MediaPlayerSessionCciData
{
    void* pData;
    uint8_t CCIbyte;
} tMediaPlayerSessionCciData;

class IMediaPlayer
{
private:
    static std::list<IMediaPlayerSession*> mSessionList;
    static IMediaPlayer *mInstance;
    std::list<IMediaPlayerSession *>::iterator getIterator(IMediaPlayerSession * session);
    bool IsSessionRegistered(IMediaPlayerSession* pIMediaPlayerSession);
    IMediaPlayer(); // private for singleton
    eIMediaPlayerStatus updateCCI();
    unsigned int mLiveRecCount ;  //a variable,that keeps track of number of live recordings
    MSPEventCallback mspEventCallback;
    bool mEasAudioActive;

    // Player mutex is for protecting the media player session list, session
    // and controller resources between the below threads
    // Galio Entry
    // SDV handler
    // thread notifying CCI updates
    // Diagnostic pages
    // threads calling media player CSCI interfaces
    // MSP_MRDvr_ServerHandler thread for TSB sharing
    pthread_mutex_t m_PlayerMutex;

    // Event queue monitored by media player thread
    MSPEventQueue* threadEventQueue;
    // Media player thread to monitor the asynchronous events posted onto the
    // media player thread event queue
    pthread_t mEventHandlerThread;

    bool m_bConnected;
    int  m_fd;

public:

    ~IMediaPlayer();
    static IMediaPlayer * getMediaPlayerInstance();

    ///This method creates an IMediaPlayerSession instance.
    eIMediaPlayerStatus IMediaPlayerSession_Create(IMediaPlayerSession ** ppIMediaPlayerSession,
            IMediaPlayerStatusCallback eventStatusCB,
            void* pClientContext);

    ///This method destroys an IMediaPlayerSession.
    eIMediaPlayerStatus IMediaPlayerSession_Destroy(IMediaPlayerSession *pIMediaPlayerSession);

    ///Loads a service into the media player session.
    eIMediaPlayerStatus IMediaPlayerSession_Load(IMediaPlayerSession *pIMediaPlayerSession,
            const char * serviceUrl,
            const MultiMediaEvent ** pMme);

    ///Ejects service from the Media Player Session.
    eIMediaPlayerStatus IMediaPlayerSession_Eject(IMediaPlayerSession *pIMediaPlayerSession);

    ///   This method begins playing the media service. Physical resources get assigned during this operation.
    eIMediaPlayerStatus IMediaPlayerSession_Play(IMediaPlayerSession *pIMediaPlayerSession,
            const char *outputUrl,
            float nptStartTime,
            const MultiMediaEvent **pMme);

    ///      This method starts to record the service to a file specified.
    eIMediaPlayerStatus  IMediaPlayerSession_PersistentRecord(IMediaPlayerSession
            *pIMediaPlayerSession,
            const char *recordUrl,
            float nptRecordStartTime,
            float nptRecordStopTime,
            const MultiMediaEvent **pMme);

    /// This method stops the media service.
    eIMediaPlayerStatus IMediaPlayerSession_Stop(IMediaPlayerSession *pIMediaPlayerSession,
            bool stopPlay,
            bool stopPersistentRecord);

    /// This method sets the playback speed of the media service.
    eIMediaPlayerStatus IMediaPlayerSession_SetSpeed(IMediaPlayerSession *pIMediaPlayerSession,
            int numerator,
            unsigned int denominator);

    ///This method gets the current playback speed on an IMediaPlayerSession. Support speeds vary across plat-
    ///     form and service. Typical values: +/- x2, +/- x4, +/- x16, +/- x32, +/- x64, +/- x128.
    eIMediaPlayerStatus  IMediaPlayerSession_GetSpeed(IMediaPlayerSession *pIMediaPlayerSession,
            int *pNumerator,
            unsigned int *pDenominator);

    ///This method sets an approximated current NPT position.
    eIMediaPlayerStatus IMediaPlayerSession_SetPosition(IMediaPlayerSession *pIMediaPlayerSession,
            float nptTime);

    ///This method gets an approximated current NPT position. NPT is relative to beginning of stream.
    eIMediaPlayerStatus IMediaPlayerSession_GetPosition(IMediaPlayerSession *pIMediaPlayerSession,
            float *pNptTime);

    ///This function is used to set the presentation parameters of the media player session.
    eIMediaPlayerStatus  IMediaPlayerSession_SetPresentationParams(IMediaPlayerSession *pIMediaPlayerSession,
            tAvRect *vidScreenRect,
            bool enablePictureModeSetting,
            bool enableAudioFocus);

    /// This function is used to get the presentation parameters of the media player session.
    eIMediaPlayerStatus  IMediaPlayerSession_GetPresentationParams(IMediaPlayerSession *pIMediaPlayerSession,
            tAvRect *vidScreenRect,
            bool *pEnablePictureModeSetting,
            bool *pEnableAudioFocus);

    /*** This method swaps the audio and/ or display control of two media player sessions. This is a convenience
            function provided for picture-in-picture (PIP) and picture-outside-picture (POP) user interfaces.
      **/
    eIMediaPlayerStatus IMediaPlayerSession_Swap(IMediaPlayerSession *pIMediaPlayerSession1,
            IMediaPlayerSession *pIMediaPlayerSession2,
            bool swapAudioFocus,
            bool swapDisplaySettings);

    ///This method allows the services layer to configure presentation parameters for the media player session.
    eIMediaPlayerStatus IMediaPlayerSession_ConfigurePresentationParams(IMediaPlayerSession *pIMediaPlayerSession,
            tAvRect *vidScreenRect,
            bool pendingPictureModeSetting,
            bool pendingAudioFocus);

    /// This method is used to query the configured pending presentation params for the specified media player session.                                                                                             File Documentation
    eIMediaPlayerStatus IMediaPlayerSession_GetPendingPresentationParams(IMediaPlayerSession *pIMediaPlayerSession,
            tAvRect *vidScreenRect,
            bool *pPendingPictureModeSetting,
            bool *pPendingAudioFocus);

    ///This function applies configured presentation parameters for all media player sessions that have been created.
    eIMediaPlayerStatus IMediaPlayerSession_CommitPresentationParams(void);

    IMediaPlayerSession* IMediaPlayerSession_FindSessionFromServiceUrl(const char *srvUrl);

    bool IMediaPlayerSession_IsServiceUrlActive();

    eIMediaPlayerStatus IMediaPlayerSession_AttachCallback(IMediaPlayerSession *pIMediaPlayerSession,
            IMediaPlayerStatusCallback eventStatusCB,
            void *pClientContext);

    eIMediaPlayerStatus IMediaPlayerSession_DetachCallback(IMediaPlayerSession *pIMediaPlayerSession,
            IMediaPlayerStatusCallback eventStatusCB);

    ///This function is used to create an IP Bandwidth Gauge display.
    eIMediaPlayerStatus IMediaPlayerSession_IpBwGauge(IMediaPlayerSession *pIMediaPlayerSession,
            const int8_t *sTryServiceUrl,
            uint32_t *pMaxBwProvision,
            uint32_t *pTryServiceBw,
            uint32_t *pTotalBwConsumption);

    ///This method gets an approximated start NPT position. NPT is relative to beginning of stream.
    eIMediaPlayerStatus IMediaPlayerSession_GetStartPosition(IMediaPlayerSession *pIMediaPlayerSession,
            float *pNptTime);

    ///This method gets an approximated end NPT position. NPT is relative to beginning of stream.
    eIMediaPlayerStatus IMediaPlayerSession_GetEndPosition(IMediaPlayerSession *pIMediaPlayerSession,
            float *pNptTime);

    eIMediaPlayerStatus IMediaPlayerSession_SetApplicationDataPid(IMediaPlayerSession *pIMediaPlayerSession,
            uint32_t pid);

    ///extension APIs will be used to get Mini Carousel Data
    eIMediaPlayerStatus IMediaPlayerSession_SetApplicationDataPidExt(IMediaPlayerSession *pIMediaPlayerSession,
            IMediaPlayerStatusCallback eventStatusCB, void *pEventStatusCBClientContext, uint32_t pid, const tIMediaPlayerSfltFilterGroup *sfltGroup,
            IMediaPlayerClientSession ** ppClientSession);

    eIMediaPlayerStatus IMediaPlayerSession_GetApplicationData(IMediaPlayerSession *pIMediaPlayerSession,
            uint32_t bufferSize,
            uint8_t *buffer,
            uint32_t *dataSize);

    ///extension APIs will be used to get Mini Carousel Data
    eIMediaPlayerStatus IMediaPlayerSession_GetApplicationDataExt(IMediaPlayerClientSession * pClientSession,
            IMediaPlayerSession *pIMediaPlayerSession,
            uint32_t bufferSize,
            uint8_t *buffer,
            uint32_t *dataSize);

    eIMediaPlayerStatus IMediaPlayerSession_SetAudioPid(IMediaPlayerSession *pIMediaPlayerSession,
            uint32_t pid);

    eIMediaPlayerStatus IMediaPlayerSession_GetComponents(IMediaPlayerSession *pIMediaPlayerSession,
            tComponentInfo *info,
            uint32_t infoSize,
            uint32_t *cnt,
            uint32_t offset);

    ///This method returns total and used number of resources of input resource type
    eIMediaPlayerStatus IMediaPlayer_GetResourceUsage(eResourceType type,
            int32_t *cfgQuantity,
            int32_t *used);

    static void CCIUpdated(void *pData, uint8_t CCIbyte);

    const char* GetServiceUrlWithAudioFocus();

    void StopInFocusAudioAndStartEasAudio(IMediaPlayerSession *pIMediaPlayerSession);
    void ProcessStopInFocusAudioAndStartEasAudio(IMediaPlayerSession *pIMediaPlayerSession);
    void RestartInFocusAudio(void);
    void ProcessRestartInFocusAudio(void);

    int RegisterMspEventCallback(tCsciMspCallbackFunction callback,
                                 tCsciMspEvent type,
                                 void *clientContext);
    static eCsciMspDiagStatus GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo);

    int UnregisterMspEventCallback(tCsciMspCallbackFunction callback);

    bool IsEasAudioActive(void);
    void SetEasAudioActive(bool active);
    void SignalAudioFocusChange(bool active);

    static eCsciMspDiagStatus GetMspCopyProtectionInfo(DiagCCIData *msgCCIInfo);

    static eCsciMspDiagStatus GetMspOutputsInfo(DiagOutputsInfo_t *msgOutputsInfo);

    static eCsciMspDiagStatus GetMspComponentInfo(uint32_t* pCount, DiagComponentsInfo_t **ppCompInfo);

#if PLATFORM_NAME == IP_CLIENT
    static eCsciMspDiagStatus GetMspStreamingInfo(DiagMspStreamingInfo *streamingInfo);
#endif

    void lockplayermutex();
    void unlockplayermutex();

    // Media player thread entry function
    static void* eventThreadFunc(void *data);
    // Queues an event onto the queue monitored by Media player thread
    eIMediaPlayerStatus queueEvent(eMediaPlayerEvent evtyp, void* pData)	;
    // Process the CCI Updated event
    void ProcessCCIUpdated(void *pData, uint8_t CCIbyte);

};

#endif
