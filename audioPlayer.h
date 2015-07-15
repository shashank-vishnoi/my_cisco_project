
/**
   \file audioPlayer.h
   \class AudioPlayer
   \brief An IMediaController which plays back PCM encoded audio originating from a file.

*/

#if !defined(AUDIOPLAYER_H)
#define AUDIOPLAYER_H

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
#include "IMediaPlayer.h"
#include "IMediaController.h"
#include "psi.h"
#include "MSPSourceFactory.h"

#include "cpe_source.h"
#include "cpe_programhandle.h"
#include "itimer.h"

class MSPEventQueue;
class Event;


class AudioPlayer : public IMediaController
{
public:
    AudioPlayer(IMediaPlayerSession *pIMediaPlayerSession);
    ~AudioPlayer(void);
    eIMediaPlayerStatus Load(const char* serviceUrl, const MultiMediaEvent **pMme);
    eIMediaPlayerStatus Play(const char* outputUrl, float nptStartTime, const MultiMediaEvent **pMme);
    eIMediaPlayerStatus Stop(bool stopPlay, bool stopPersistentRecord);
    void Eject(void);
    void lockMutex(void);
    void unLockMutex(void);
    eIMediaPlayerStatus SetCallback(IMediaPlayerSession* pIMediaPlayerSession, IMediaPlayerStatusCallback cb, void* pClientContext);
    eIMediaPlayerStatus DetachCallback(IMediaPlayerStatusCallback cb);
    eIMediaPlayerStatus PersistentRecord(const char* recordUrl, float nptRecordStartTime, float nptRecordStopTime, const MultiMediaEvent **pMme);
    eIMediaPlayerStatus SetSpeed(int numerator, unsigned int denominator);
    eIMediaPlayerStatus GetSpeed(int* pNumerator, unsigned int* pDenominator);
    eIMediaPlayerStatus SetPosition(float nptTime);
    eIMediaPlayerStatus GetPosition(float* pNptTime);
    eIMediaPlayerStatus IpBwGauge(const char *sTryServiceUrl, unsigned int *pMaxBwProvision, unsigned int *pTryServiceBw, unsigned int *pTotalBwConsumption);
    eIMediaPlayerStatus SetPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus);
    eIMediaPlayerStatus GetStartPosition(float* pNptTime);
    eIMediaPlayerStatus GetEndPosition(float* pNptTime);
    eIMediaPlayerStatus RegisterCCICallback(void* data, CCIcallback_t cb);
    eIMediaPlayerStatus UnRegisterCCICallback(void);
    std::string GetSourceURL(bool liveSrcOnly = false) const;
    std::string GetDestURL(void) const;
    bool isRecordingPlayback(void) const;
    bool isLiveRecording(void) const;
    eIMediaPlayerStatus SetApplicationDataPid(uint32_t aPid);
    eIMediaPlayerStatus SetApplicationDataPidExt(IMediaPlayerClientSession *ApplnClient);
    eIMediaPlayerStatus GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset);
    eIMediaPlayerStatus GetApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize);
    eIMediaPlayerStatus GetApplicationDataExt(IMediaPlayerClientSession *ApplnClient, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize);
    uint32_t GetSDVClentContext(IMediaPlayerClientSession *ApplnClient);
    eIMediaPlayerStatus SetAudioPid(uint32_t pid);
    eIMediaPlayerStatus CloseDisplaySession(void);
    eIMediaPlayerStatus StartDisplaySession(void);
    eCsciMspDiagStatus GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo);
    bool isBackground(void);
    void StopAudio(void);
    void RestartAudio(void);
    bool isLiveSourceUsed() const
    {
        return false;
    }

    tCpePgrmHandle getCpeProgHandle();
    void SetCpeStreamingSessionID(uint32_t sessionId);

    void InjectCCI(uint8_t CCIbyte);
    eIMediaPlayerStatus StopStreaming();

private:
    typedef enum
    {
        kPrepareSourceEvent,
        kPlaySourceEvent,
        kTimerEvent,
        kAudioEvent,
        kExitThreadEvent
    } eAudioPlayerEvent;

    typedef struct
    {
        uint32_t payload;
    } AudioPlayerEventData;

    typedef enum
    {
        kState_Idle,
        kState_PrepareSource,
        kState_PlaySource,
        kState_Delay,
        kState_Stopping,
        kState_Stopped,
        kState_Error
    } eAudioPlayerState;

    // miscellaneous constants
    enum
    {
        kMaxUrlSize = 128,
        kStackSize = (64 * 1024),
        kAudioHeaderSize = 154
    };

    char fileUrl[kMaxUrlSize];
    int repeatDelay;
    int mutexStack;
    eAudioPlayerState currentState;
    tCpeSrcCallbackID callbackId;
    tCpePgrmHandle mediaHandle;
    tCpeSrcHandle sourceHandle;
    tCpeSrcMemBuffer sourceBuffer;
    FILE* audioFp;
    uint32_t soundDataSize;
    intptr_t timerId;
    tCpeAudioInfo aiffAudioInfo;
    IMediaPlayer* mediaPlayerInstance;
    std::string srcUrl;

    MSPEventQueue* apQueue;
    pthread_t apThread;
    pthread_mutex_t apMutex;
    std::list<CallbackInfo*> callbackList;
    IMediaPlayerSession *mIMediaPlayerSession;
    static bool mEasAudioActive;

    static void* threadFunction(void* data);
    static void timerCallback(evutil_socket_t fd, short what, void* data);
    static int audioCallback(tCpeSrcCallbackTypes type,
                             void *userdata,
                             void *pCallbackSpecific);

    eIMediaPlayerStatus doCallback(eIMediaPlayerSignal sig, eIMediaPlayerStatus stat);
    void queueEvent(eAudioPlayerEvent type);
    void queueEventPlusPayload(eAudioPlayerEvent type, uint32_t data);
    void createThread(void);
    bool processEvent(Event* event);

    void enterPrepareSourceState(void);
    void enterPlaySourceState(void);
    void enterDelayState(void);
    void enterStoppingState(void);

    void startTimer(int seconds);
    void cancelTimer(void);
    void processTimerEvent(void);
    void processAudioEvent(void* eventData);

    void startAudioTeardown(void);
    void completeAudioTeardown(void);
    void completeStopSequence(void);

    void stopPrimaryAudioAndStartEasAudio(void);
    void startEasAudio(void);
    void SetEasAudioActive(bool active);
    void restartPrimaryAudio(void);
    int prepareMemorySource(void);
    bool readAudioInfo(void);
    bool readSoundData(uint8_t** ppBuff, uint32_t* bufSize);
    int32_t floatToInteger(uint16_t exponent, uint16_t fraction);

    //base class function
    void addClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteAllClientSession();
};

#endif
