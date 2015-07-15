/**
   \file HnOnDemandStreamer.h
   \class HnOnDemandStreamer
*/

#if !defined(HNONDEMANDSTREAMER_H)
#define HNONDEMANDSTREAMER_H

#include "ondemand.h"
#include "MSPRFSource.h"
#include "MSPHnOnDemandStreamerSource.h"
#include "cpe_cam.h"
#include "InMemoryStream.h"


typedef enum
{
    kZapperTimeOutEvent = -1,
    kZapperTunerLockedEvent,
    kZapperEventRFAnalogCallback,
    kZapperPSIReadyEvent,
    kZapperAnalogPSIReadyEvent,
    kZapperPSIUpdateEvent,
    kZapperEventPlay,
    kZapperEventStop,
    kZapperEventExit,
    kZapperTunerLost,
    kZapperTunerRestored
} eZapperEvent;

/**
   \class HnOnDemandStreamer
   \brief Media Controller component for MediaPlayer used by IP Client VOD
   Implements the IMediaController interface.
*/
class HnOnDemandStreamer : public OnDemand
{
public:

    HnOnDemandStreamer();
    ~HnOnDemandStreamer();
    tCpePgrmHandle getCpeProgHandle();
    void SetCpeStreamingSessionID(uint32_t sessionId);

    eIMediaPlayerStatus Load(tCpeSrcRFTune tuningParams,
                             int programNumber,
                             const MultiMediaEvent **pMme);
    eIMediaPlayerStatus Load(const char* serviceUrl,
                             const MultiMediaEvent **pMme);
    eIMediaPlayerStatus Stop(bool stopPlay, bool stopPersistentRecord);
    void         		Eject();
    void 				tearDown(void);

    eMspStatus queueEvent(eZapperEvent evtyp);
    bool handleEvent(Event *evt);
    bool isBackground(void);
    eIMediaPlayerStatus StopStreaming();

private:

    eIMediaPlayerStatus loadSource(void);
    void HandleCallback(eOnDemandEvent onDemandEvt);
    eIMediaPlayerStatus loadTuningParamsAndPlay();

    eIMediaPlayerStatus SetCallback(IMediaPlayerSession *pIMediaPlayerSession, IMediaPlayerStatusCallback cb, void* pClientContext);
    eIMediaPlayerStatus DetachCallback(IMediaPlayerStatusCallback cb);

    int  createEventThread(void);
    static void* eventthreadFunc(void *data);
    static void psiCallback(ePsiCallBackState state, void *data);
    static void hnOnDemandRFSourceCB(void *data, eSourceState aSrcState);
    eIMediaPlayerStatus RegisterCCICallback(void* data, CCIcallback_t cb);
    eIMediaPlayerStatus UnRegisterCCICallback();
    void StartVodInMemoryStreaming(void);
    void InjectCCI(uint8_t CCIbyte);
    MSPSource *mPtrHnOnDemandRFSource;
    MSPHnOnDemandStreamerSource *mPtrHnOnDemandStreamerSource;
    eZapperState mOndemandZapperState;
    MSPEventQueue* mThreadEventQueue;
    pthread_t mEventHandlerThread;
    Psi *mPsi;  /**< pointer to our PSI instance, NULL if not created yet */
    AnalogPsi *mPtrAnalogPsi;
    uint32_t mSessionId;
    void *m_pCamHandle;
    int mSourceId;
    tCpePgrmHandle mSflHandle;
    tCpeCamCaHandle *mCamHandle;
    InMemoryStream *mptrcaStream;
    int mregid ;
    void* mCBData;
    CCIcallback_t mCCICBFn;
    int mEntitleid;
    uint8_t m_CCIbyte;
    tCpeCamCaHandle mCamCaHandle;
};

#endif //HNONDEMANDSTREAMER_H




