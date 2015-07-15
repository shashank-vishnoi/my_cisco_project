
/**
   \file audioPlayer_ic.h
   \class AudioPlayer
   \brief An IMediaController which plays back PCM encoded audio originating from a file.

*/

#if !defined(AUDIOPLAYER_IC_H)
#define AUDIOPLAYER_IC_H

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
#include "IMediaPlayer.h"
#include "IMediaController.h"
#include "MSPSourceFactory.h"
#include "cgmiPlayerApi.h"
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
    eCsciMspDiagStatus GetMspStreamingInfo(DiagMspStreamingInfo *streamingInfo);
    /* Returns the CCI byte value */
    uint8_t GetCciBits(void);
    bool isBackground(void);
    void StopAudio(void);
    void RestartAudio(void);
    bool isLiveSourceUsed() const
    {
        return false;
    }

private:
    typedef enum
    {
        kPlaySourceEvent,
        kTimerEvent,
        kRestartAudioEvent,
        kExitThreadEvent
    } eAudioPlayerEvent;


    typedef enum
    {
        kState_Idle,
        kState_PlaySource,
        kState_Delay,
        kState_Stopped,
        kState_Error
    } eAudioPlayerState;

    // miscellaneous constants
    enum
    {
        kMaxUrlSize = 128,
        kStackSize = (64 * 1024),
    };


    char fileUrl[kMaxUrlSize];
    int repeatDelay;
    int mutexStack;
    eAudioPlayerState currentState;
    intptr_t timerId;
    IMediaPlayer* mediaPlayerInstance;
    std::string srcUrl;

    MSPEventQueue* apQueue;
    pthread_t apThread;
    pthread_mutex_t apMutex;
    std::list<CallbackInfo*> callbackList;
    void* mpSessionId;
    IMediaPlayerSession *mIMediaPlayerSession;
    static bool mEasAudioActive;

    static void* threadFunction(void* data);
    static void timerCallback(evutil_socket_t fd, short what, void* data);
    static void audioCallback(void *pUserData, void *pSession, tcgmi_Event event, tcgmi_Data *pData);

    eIMediaPlayerStatus doCallback(eIMediaPlayerSignal sig, eIMediaPlayerStatus stat);
    void queueEvent(eAudioPlayerEvent type);
    void createThread(void);
    bool processEvent(Event* event);

    void enterPlaySourceState(void);
    void enterDelayState(void);
    void enterStoppingState(void);

    void startTimer(int seconds);
    void cancelTimer(void);
    void processTimerEvent(void);
    void processRestartAudioEvent();

    void completeAudioTeardown(void);

    void stopPrimaryAudioAndStartEasAudio(void);
    void startEasAudio(void);
    void SetEasAudioActive(bool active);
    void restartPrimaryAudio(void);

    //base class function
    void addClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteAllClientSession();
};

#endif
