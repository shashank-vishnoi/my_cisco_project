//#include <media-player-session.h>
#include <sail-clm-api.h>
#include "IMediaPlayer.h"
#include "IMediaPlayerSession.h"
#if defined(DMALLOC)
#include "dmalloc.h"
#endif


#include <dlog.h>
#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"IMediaPlayerSession:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

/// constructor
IMediaPlayerSession::IMediaPlayerSession(IMediaPlayerStatusCallback callback, void *pClientContext)
{
    mMediaPlayerCallback = callback;
    mpClientContext = pClientContext;
    mpController = NULL;
    mSessionCCIbyte = 0;
    mScreenRect.x = 0;
    mScreenRect.y = 0;
    mScreenRect.width = 1280;  //updated window size to HD resolution.TODO -this hardcoding had to be removed.
    mScreenRect.height = 720;
    mEnaPicMode = true;
    mEnaAudio = false;
    mInFocus = false;
    mPendingScreenRect.x = 0;
    mPendingScreenRect.y = 0;
    mPendingScreenRect.width = 0;
    mPendingScreenRect.height = 0;
    mPendingEnaPicMode = false;
    mPendingEnaAudio = false;

}

IMediaPlayerSession::~IMediaPlayerSession()
{
    mMediaPlayerCallback = NULL;
    mpClientContext = NULL;
    //??????????????///
    if (mpController)
    {
        delete mpController;
        mpController = NULL;
    }

}


void IMediaPlayerSession::setMediaController(IMediaController *pController)
{
    LOG(DLOGL_REALLY_NOISY, "mMediaPlayerCallback: %p  mpClientContext: %p pController: %p", mMediaPlayerCallback, mpClientContext, pController);
    mpController = pController;

    if (mpController)
    {
        mpController->SetCallback(this, mMediaPlayerCallback, mpClientContext);
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Error: null mpController", __FUNCTION__);
    }
}


void IMediaPlayerSession::clearMediaController()
{
    mpController = NULL;
}


IMediaController * IMediaPlayerSession::getMediaController()
{
    return mpController;
}


IMediaPlayerStatusCallback IMediaPlayerSession::getMediaPlayerCallback()
{
    return mMediaPlayerCallback;
}


void * IMediaPlayerSession::getCBclientContext()
{
    return mpClientContext;
}

void IMediaPlayerSession::SetCCI(uint8_t CCIbyte)
{
    FNLOG(DL_MSP_MPLAYER);
    mSessionCCIbyte = CCIbyte;

}

void IMediaPlayerSession::GetCCI(CCIData &cciData)
{
#if PLATFORM_NAME == IP_CLIENT
    if (mpController)
    {
        mSessionCCIbyte = mpController->GetCciBits();
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: null mpController");
    }
#endif

    cciData.rct = (mSessionCCIbyte & 0x20) >> 5;
    cciData.cit = (mSessionCCIbyte & 0x10) >> 4;
    cciData.aps = (mSessionCCIbyte & 0x0C) >> 2;
    cciData.emi = (mSessionCCIbyte & 0x03);

    LOG(DLOGL_REALLY_NOISY, "%s %s ::: %d", __FUNCTION__, "CCI BYTE = ", mSessionCCIbyte);
    LOG(DLOGL_REALLY_NOISY, "%s %d ::: %s", "EMI = ", cciData.emi, cciEmiString[cciData.emi]);
    LOG(DLOGL_REALLY_NOISY, "%s %d ::: %s", "APS = ", cciData.aps, cciApsString[cciData.aps]);
    LOG(DLOGL_REALLY_NOISY, "%s %d ::: %s", "CIT = ", cciData.cit, cciCitString[cciData.cit]);
    LOG(DLOGL_REALLY_NOISY, "%s %d ::: %s", "RCT = ", cciData.rct, cciCitString[cciData.rct]);
}

eIMediaPlayerStatus IMediaPlayerSession::Play(const char* outputUrl, float nptStartTime, const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_MPLAYER);
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;
    if (mpController)
    {
        time_t now;
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s, %d, audioFocus = %d", __FUNCTION__, __LINE__, mEnaAudio);
        mpController->lockMutex();
        status = mpController->Play(outputUrl, nptStartTime, pMme);
        mpController->unLockMutex();

        time(&now);
    }
    else
    {
        status = kMediaPlayerStatus_Error_OutOfState;
    }
    return status;

}

eIMediaPlayerStatus IMediaPlayerSession::SetPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus)
{
    FNLOG(DL_MSP_MPLAYER);

    if (!vidScreenRect)
    {
        LOG(DLOGL_ERROR, "Error: null vidScreenRect");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    mScreenRect = *vidScreenRect;
    mEnaPicMode = enablePictureModeSetting;
    updateAudioEnabled(enableAudioFocus);
    LOG(DLOGL_NOISE, "%s, %s, %d, audioFocus = %d", __FILE__, __FUNCTION__, __LINE__, enableAudioFocus);
    eIMediaPlayerStatus status;

    if (mpController)
    {
        mpController->lockMutex();
        status = mpController->SetPresentationParams(vidScreenRect, enablePictureModeSetting, enableAudioFocus);
        mpController->unLockMutex();
        mInFocus = enableAudioFocus;
        status = kMediaPlayerStatus_Ok;
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: null mpController");
        status = kMediaPlayerStatus_Error_OutOfState;
    }

    return status;
}

eIMediaPlayerStatus IMediaPlayerSession::GetPresentationParams(tAvRect *vidScreenRect, bool *pEnablePictureModeSetting, bool *pEnableAudioFocus)
{
    FNLOG(DL_MSP_MPLAYER);

    if ((vidScreenRect == NULL) || (pEnablePictureModeSetting == NULL) || (pEnableAudioFocus == NULL))
    {
        LOG(DLOGL_ERROR, "Error: NULL param in GetPresentationParams");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    *vidScreenRect = mScreenRect;
    *pEnablePictureModeSetting = mEnaPicMode;
    *pEnableAudioFocus = mEnaAudio;

    return  kMediaPlayerStatus_Ok;

}

eIMediaPlayerStatus IMediaPlayerSession::ConfigurePresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus)
{
    FNLOG(DL_MSP_MPLAYER);
    if (vidScreenRect)
    {
        // Width & Height can be zero when we are resetting configured params
        mPendingScreenRect = *vidScreenRect;
        mPendingEnaPicMode = enablePictureModeSetting;
        mPendingEnaAudio   = enableAudioFocus;
        return kMediaPlayerStatus_Ok;
    }
    else
    {
        LOG(DLOGL_ERROR, "Invalid parameter");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

}

eIMediaPlayerStatus IMediaPlayerSession::GetPendingPresentationParams(tAvRect *vidScreenRect, bool *enablePictureModeSetting, bool *enableAudioFocus)
{
    FNLOG(DL_MSP_MPLAYER);
    if (vidScreenRect && enablePictureModeSetting && enableAudioFocus)
    {
        vidScreenRect->x = mPendingScreenRect.x;
        vidScreenRect->y = mPendingScreenRect.y;
        vidScreenRect->width = mPendingScreenRect.width;
        vidScreenRect->height = mPendingScreenRect.height;
        *enablePictureModeSetting = mPendingEnaPicMode;
        *enableAudioFocus = mPendingEnaAudio;
        return kMediaPlayerStatus_Ok;
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: Invalid parameter");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

}

eIMediaPlayerStatus IMediaPlayerSession::CommitPresentationParams()
{
    FNLOG(DL_MSP_MPLAYER);
    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;
    if (mpController)
    {
        mpController->SetPresentationParams(&mPendingScreenRect, mPendingEnaPicMode, mPendingEnaAudio);

        mScreenRect = mPendingScreenRect;
        mEnaPicMode = mPendingEnaPicMode;
        updateAudioEnabled(mPendingEnaAudio);

        // clear pending parameters
        mPendingScreenRect.x = 0;
        mPendingScreenRect.y = 0;
        mPendingScreenRect.width = 0;
        mPendingScreenRect.height = 0;
        mPendingEnaPicMode = false;
        mPendingEnaAudio = false;
        status = kMediaPlayerStatus_Ok;
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: null mpController");
        status = kMediaPlayerStatus_Error_OutOfState;
    }
    return status;

}


void IMediaPlayerSession::addClientSession(IMediaPlayerClientSession *pClientSession)
{
    FNLOG(DL_MSP_MPLAYER);
    if (mpController)
    {
        mpController->addClientSession(pClientSession);
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Error: null mpController", __FUNCTION__);
    }
}

void IMediaPlayerSession::deleteClientSession(IMediaPlayerClientSession *pClientSession)
{
    FNLOG(DL_MSP_MPLAYER);
    if (mpController)
    {
        mpController->deleteClientSession(pClientSession);
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Error: null mpController", __FUNCTION__);
    }
}

void IMediaPlayerSession::deleteAllClientSession()
{
    FNLOG(DL_MSP_MPLAYER);
    if (mpController)
    {
        mpController->deleteAllClientSession();
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Error: null mpController", __FUNCTION__);
    }
}


void IMediaPlayerSession::SetServiceUrl(const char * serviceUrl)
{
    if (!serviceUrl)
    {
        LOG(DLOGL_ERROR, "warning null serviceUrl");
        mServiceUrl.erase();
    }
    else
    {
        mServiceUrl = serviceUrl;
    }
}


/*
void IMediaPlayerSession::Display()
{
    LOG(DLOGL_ERROR, "session: %p  mServiceUrl: %s   mEnaAudio: %d", this, mServiceUrl.c_str(), mEnaAudio);
} */



void
IMediaPlayerSession::StopAudio(void)
{
    if (mpController)
    {
        mpController->lockMutex();
        mpController->StopAudio();
        mpController->unLockMutex();
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Error: null mpController", __FUNCTION__);
    }
}

void
IMediaPlayerSession::RestartAudio(void)
{
    if (mpController)
    {
        mpController->lockMutex();
        mpController->RestartAudio();
        mpController->unLockMutex();
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Error: null mpController", __FUNCTION__);
    }
}

void
IMediaPlayerSession::updateAudioEnabled(bool enabled)
{
    // update local member
    mEnaAudio = enabled;

    // tell the media player object that audio focus has changed
    IMediaPlayer* instance = IMediaPlayer::getMediaPlayerInstance();
    instance->SignalAudioFocusChange(enabled);
}



