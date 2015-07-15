#if !defined(IMEDIAPLAYERSESSION_H)
#define IMEDIAPLAYERSESSION_H

#include <time.h>
#include <stdint.h>
#include <sail-mediaplayersession-api.h>   // public SAIL API
#include "IMediaController.h"

using namespace std;

typedef enum {UNDEFINED_PLAY, LIVE_PLAY, DVR_PLAY, VOD_PLAY, PPV_PLAY} eSessionType;

class IMediaPlayerSession
{
private:
    IMediaController * mpController;
    IMediaPlayerStatusCallback mMediaPlayerCallback;
    void *mpClientContext;
    uint8_t mSessionCCIbyte;

    tAvRect mPendingScreenRect;
    bool mPendingEnaPicMode;
    bool mPendingEnaAudio;

    tAvRect mScreenRect;
    bool mEnaPicMode;
    bool mEnaAudio;

    string mServiceUrl;

public:
    IMediaPlayerSession(IMediaPlayerStatusCallback callback, void *pClientContext);
    ~IMediaPlayerSession();
    void setMediaController(IMediaController *);
    eIMediaPlayerStatus SetPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus);
    eIMediaPlayerStatus GetPresentationParams(tAvRect *vidScreenRect, bool *pEnablePictureModeSetting, bool *pEnableAudioFocus);
    eIMediaPlayerStatus ConfigurePresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus);
    eIMediaPlayerStatus GetPendingPresentationParams(tAvRect *vidScreenRect, bool *enablePictureModeSetting, bool *enableAudioFocus);
    eIMediaPlayerStatus Play(const char* outputUrl, float nptStartTime, const MultiMediaEvent **pMme);
    eIMediaPlayerStatus CommitPresentationParams();

    void clearMediaController();
    IMediaController * getMediaController();
    IMediaPlayerStatusCallback getMediaPlayerCallback();
    void *getCBclientContext();
    void SetCCI(uint8_t CCIbyte);
    void GetCCI(CCIData &cciData);
    bool mInFocus;

    void addClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteClientSession(IMediaPlayerClientSession *pClientSession);
    void deleteAllClientSession();

    void SetServiceUrl(const char * serviceUrl);
    void Display();
    void StopAudio(void);
    void RestartAudio(void);

    string GetServiceUrl()
    {
        return mServiceUrl;
    }

    bool HasAudioFocus()
    {
        return mEnaAudio;
    }

    void SetAudioFocus(bool focus)
    {
        mEnaAudio = focus;
    }

private:
    void updateAudioEnabled(bool enabled);
};

#endif
