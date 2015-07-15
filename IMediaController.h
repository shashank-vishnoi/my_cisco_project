
/**
   \file IMediaController.h
   \class IMediaController
   \brief Abstract class used as the base class for all media controllers.

*/

#include <sail-mediaplayersession-api.h>
// CAM include
#include "Cam.h"
#include <string>
#if !defined(IMEDIACONTROLLER_H)
#define IMEDIACONTROLLER_H

#include "MSPDiagPages.h"
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#include "ApplicationDataExt.h"
#include "cpe_programhandle.h"
#endif
#if PLATFORM_NAME == IP_CLIENT
#include "ApplicationDataExt_ic.h"
#endif

#if PLATFORM_NAME == G8
#include "csci-msp-mrdvrsrv-api.h"
#endif

typedef struct
{
    int cit;
    int aps;
    int emi;
    int rct;
} CCIData;

class CallbackInfo
{
public:

    IMediaPlayerSession* mpSession;
    IMediaPlayerStatusCallback mCallback;
    void *mClientContext;
    CallbackInfo()
    {
        mpSession = NULL;
        mCallback = NULL;
        mClientContext = NULL;
    }
};

class IMediaController
{
public:
    IMediaController() {}
    virtual eIMediaPlayerStatus Load(const char* serviceUrl, const MultiMediaEvent **pMme) = 0;
    virtual void                Eject() = 0;
    virtual eIMediaPlayerStatus Play(const char* outputUrl, float nptStartTime, const MultiMediaEvent **pMme) = 0;
    virtual eIMediaPlayerStatus PersistentRecord(const char* recordUrl, float nptRecordStartTime, float nptRecordStopTime, const MultiMediaEvent **pMme) = 0;
    virtual eIMediaPlayerStatus Stop(bool stopPlay, bool stopPersistentRecord) = 0;
    virtual eIMediaPlayerStatus SetSpeed(int numerator, unsigned int denominator) = 0;
    virtual eIMediaPlayerStatus GetSpeed(int* pNumerator, unsigned int* pDenominator) = 0;
    virtual eIMediaPlayerStatus SetPosition(float nptTime) = 0;
    virtual eIMediaPlayerStatus GetPosition(float* pNptTime) = 0;
    virtual eIMediaPlayerStatus IpBwGauge(const char *sTryServiceUrl, unsigned int *pMaxBwProvision, unsigned int *pTryServiceBw, unsigned int *pTotalBwCoynsumption) = 0;
    virtual eIMediaPlayerStatus SetPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus) = 0;
    virtual eIMediaPlayerStatus GetStartPosition(float* pNptTime) = 0;
    virtual eIMediaPlayerStatus GetEndPosition(float* pNptTime) = 0;
    virtual eIMediaPlayerStatus SetCallback(IMediaPlayerSession* pIMediaPlayerSession, IMediaPlayerStatusCallback cb, void* pClientContext) = 0;
    virtual eIMediaPlayerStatus DetachCallback(IMediaPlayerStatusCallback cb) = 0;
    virtual eIMediaPlayerStatus RegisterCCICallback(void* data, CCIcallback_t cb) = 0;
    virtual eIMediaPlayerStatus UnRegisterCCICallback() = 0;
    virtual std::string GetSourceURL(bool liveSrcOnly = false)const = 0;
    virtual std::string GetDestURL()const = 0;
    virtual bool isRecordingPlayback()const = 0;
    virtual bool isLiveRecording()const = 0;
    virtual bool isLiveSourceUsed() const = 0; // {return true;    };
    virtual eIMediaPlayerStatus SetApplicationDataPid(uint32_t aPid) = 0;
    virtual uint32_t GetSDVClentContext(IMediaPlayerClientSession *ApplnClient) = 0;
    virtual eIMediaPlayerStatus SetApplicationDataPidExt(IMediaPlayerClientSession *ApplnClient) = 0;
    virtual eIMediaPlayerStatus GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset) = 0;
    virtual eIMediaPlayerStatus GetApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize) = 0;
    virtual eIMediaPlayerStatus GetApplicationDataExt(IMediaPlayerClientSession *ApplnClient, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize) = 0;
    virtual eIMediaPlayerStatus SetAudioPid(uint32_t pid) = 0;
    virtual eIMediaPlayerStatus CloseDisplaySession() = 0;
    virtual eIMediaPlayerStatus StartDisplaySession() = 0;
    virtual eCsciMspDiagStatus GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo) = 0;
    virtual bool isBackground(void) = 0;
    virtual void lockMutex(void) = 0;
    virtual void unLockMutex(void) = 0;
    virtual void addClientSession(IMediaPlayerClientSession *pClientSession) = 0;
    virtual void deleteClientSession(IMediaPlayerClientSession *pClientSession) = 0;
    virtual void deleteAllClientSession() = 0;
    virtual ~IMediaController() {};
    virtual void StopAudio(void) = 0;
    virtual void RestartAudio(void) = 0;
    virtual void startEasAudio(void) = 0;
    virtual void SetEasAudioActive(bool active) = 0;

#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    virtual tCpePgrmHandle getCpeProgHandle(void) = 0;   /* HN Streaming session specific */
    virtual void SetCpeStreamingSessionID(uint32_t sessionId) = 0; /* HN Streaming session specific */
    virtual void InjectCCI(uint8_t CCIbyte) = 0; /* HN Streaming session specific */
    virtual eIMediaPlayerStatus StopStreaming(void) = 0; /* HN Streaming session specific */
#endif
#if PLATFORM_NAME == IP_CLIENT
    virtual eCsciMspDiagStatus GetMspStreamingInfo(DiagMspStreamingInfo *streamingInfo) = 0; /* IP Client Live Streaming specific */
    virtual uint8_t GetCciBits(void) = 0; /* IP Client Session CCI information */
#endif




};

#endif
