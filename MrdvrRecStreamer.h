using namespace std;

#if !defined(MRDVR_REC_STREAMER_H)
#define MRDVR_REC_STREAMER_H

#include "MspCommon.h"
#include "DisplaySession.h"
#include "IMediaController.h"
#include "MSPSource.h"
#include "MSPSourceFactory.h"

class MrdvrRecStreamer : public IMediaController
{
public:

    MrdvrRecStreamer(IMediaPlayerSession *pIMediaPlayerSession);
    ~MrdvrRecStreamer();

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
    eIMediaPlayerStatus SetCallback(IMediaPlayerSession* pIMediaPlayerSession, IMediaPlayerStatusCallback cb, void* pClientContext);
    eIMediaPlayerStatus DetachCallback(IMediaPlayerStatusCallback cb);
    eIMediaPlayerStatus RegisterCCICallback(void* data, CCIcallback_t cb);
    eIMediaPlayerStatus UnRegisterCCICallback();
    std::string GetSourceURL(bool liveSrcOnly)const;
    std::string GetDestURL()const;
    bool isRecordingPlayback()const;
    bool isLiveRecording()const;
    bool isLiveSourceUsed() const; // {return true;    };
    eIMediaPlayerStatus SetApplicationDataPid(uint32_t aPid);
    uint32_t GetSDVClentContext(IMediaPlayerClientSession *ApplnClient);
    eIMediaPlayerStatus SetApplicationDataPidExt(IMediaPlayerClientSession *ApplnClient);
    eIMediaPlayerStatus GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset);
    eIMediaPlayerStatus GetApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize);
    eIMediaPlayerStatus GetApplicationDataExt(IMediaPlayerClientSession *ApplnClient, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize);
    eIMediaPlayerStatus SetAudioPid(uint32_t pid);
    eIMediaPlayerStatus CloseDisplaySession();
    eIMediaPlayerStatus StartDisplaySession();
    eCsciMspDiagStatus GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo);
    bool isBackground(void);
    void lockMutex(void);
    void unLockMutex(void);
    void addClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteAllClientSession();
    void StopAudio(void);
    void RestartAudio(void);

    eIMediaPlayerStatus loadSource();
    static void sourceCB(void *data, eSourceState aSrcState);
    tCpePgrmHandle getCpeProgHandle();
    void SetCpeStreamingSessionID(uint32_t sessionId);
    void InjectCCI(uint8_t CCIbyte);
    eIMediaPlayerStatus StopStreaming();
    void startEasAudio(void);
    void SetEasAudioActive(bool active);
private:

    IMediaPlayerSession *mIMediaPlayerSession;
    MSPSource *mPtrRecSource;
    eMSPSourceType mCurrentSource;
    std::string mSourceUrl;
    std::string mDestUrl;
    pthread_mutex_t  mMutex;
    uint32_t mSessionId;
    std::list<CallbackInfo*> mCallbackList;
    static bool mEasAudioActive;

};

#endif // #ifndef MRDVR_REC_STREAMER_H


