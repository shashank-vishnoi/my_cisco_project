#include <time.h>
#include <stdint.h>
//#include <stdbool.h>
#include <sail-mediaplayersession-api.h>
#include "IMediaPlayer.h"
#if defined(DMALLOC)
#include "dmalloc.h"
#endif

#include "fpanel.h"

#include <dlog.h>

eIMediaPlayerStatus IMediaPlayerSession_Create(IMediaPlayerSession **ppIMediaPlayerSession, IMediaPlayerStatusCallback eventStatusCB, void *pClientContext)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();

    if (player)
    {
        return player->IMediaPlayerSession_Create(ppIMediaPlayerSession, eventStatusCB,  pClientContext);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayerSession_Destroy(IMediaPlayerSession *pIMediaPlayerSession)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_Destroy(pIMediaPlayerSession);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayerSession_Load(IMediaPlayerSession *pIMediaPlayerSession, const char *serviceUrl, const MultiMediaEvent **pMme)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_Load(pIMediaPlayerSession, serviceUrl, pMme);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayerSession_Eject(IMediaPlayerSession *pIMediaPlayerSession)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_Eject(pIMediaPlayerSession);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayerSession_Play(IMediaPlayerSession *pIMediaPlayerSession, const char *outputUrl,
        float nptStartTime, const MultiMediaEvent **pMme)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_Play(pIMediaPlayerSession, outputUrl, nptStartTime, pMme);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayerSession_PersistentRecord(IMediaPlayerSession *pIMediaPlayerSession, const char *recordUrl,
        float nptRecordStartTime, float nptRecordStopTime, const MultiMediaEvent **pMme)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    eIMediaPlayerStatus ret = kMediaPlayerStatus_Error_Unknown;
    if (player)
    {
        ret = player->IMediaPlayerSession_PersistentRecord(pIMediaPlayerSession, recordUrl, nptRecordStartTime, nptRecordStopTime, pMme);
        if (ret == kMediaPlayerStatus_Ok)
            fp_SetRecordLed(true);
    }
    return ret;
}

eIMediaPlayerStatus IMediaPlayerSession_Stop(IMediaPlayerSession *pIMediaPlayerSession, bool stopPlay, bool stopPersistentRecord)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        if (stopPersistentRecord)
            fp_SetRecordLed(false);
        return player->IMediaPlayerSession_Stop(pIMediaPlayerSession, stopPlay, stopPersistentRecord);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayerSession_SetSpeed(IMediaPlayerSession *pIMediaPlayerSession, int numerator, unsigned int denominator)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_SetSpeed(pIMediaPlayerSession, numerator, denominator);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayerSession_GetSpeed(IMediaPlayerSession *pIMediaPlayerSession, int *pNumerator, unsigned int *pDenominator)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_GetSpeed(pIMediaPlayerSession, pNumerator, pDenominator);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayerSession_SetPosition(IMediaPlayerSession *pIMediaPlayerSession, float nptTime)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_SetPosition(pIMediaPlayerSession, nptTime);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayerSession_GetPosition(IMediaPlayerSession *pIMediaPlayerSession, float *pNptTime)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_GetPosition(pIMediaPlayerSession, pNptTime);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayerSession_SetPresentationParams(IMediaPlayerSession *pIMediaPlayerSession, tAvRect *vidScreenRect,
        bool enablePictureModeSetting, bool enableAudioFocus)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_SetPresentationParams(pIMediaPlayerSession, vidScreenRect,
                enablePictureModeSetting, enableAudioFocus);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayerSession_GetPresentationParams(IMediaPlayerSession *pIMediaPlayerSession, tAvRect *vidScreenRect,
        bool *pEnablePictureModeSetting, bool *pEnableAudioFocus)
{
    //  pIMediaPlayerSession = pIMediaPlayerSession;
    // vidScreenRect = vidScreenRect;
//   pEnablePictureModeSetting = pEnablePictureModeSetting;
//   pEnableAudioFocus = pEnableAudioFocus;

    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_GetPresentationParams(pIMediaPlayerSession, vidScreenRect,
                (bool *)pEnablePictureModeSetting, (bool *)pEnableAudioFocus);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayer_Swap(IMediaPlayerSession *pIMediaPlayerSession1, IMediaPlayerSession *pIMediaPlayerSession2,
                                      bool swapAudioFocus, bool swapDisplaySettings)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_Swap(pIMediaPlayerSession1, pIMediaPlayerSession2,
                                                swapAudioFocus,  swapDisplaySettings);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayerSession_ConfigurePresentationParams(IMediaPlayerSession *pIMediaPlayerSession, tAvRect *vidScreenRect,
        bool pendingPictureModeSetting, bool pendingAudioFocus)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_ConfigurePresentationParams(pIMediaPlayerSession, vidScreenRect,
                pendingPictureModeSetting, pendingAudioFocus);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayerSession_GetPendingPresentationParams(IMediaPlayerSession *pIMediaPlayerSession, tAvRect *vidScreenRect,
        bool *pPendingPictureModeSetting, bool *pPendingAudioFocus)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_GetPendingPresentationParams(pIMediaPlayerSession, vidScreenRect,
                (bool *)pPendingPictureModeSetting, (bool *)pPendingAudioFocus);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayer_CommitPresentationParams(void)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_CommitPresentationParams();
    }
    return kMediaPlayerStatus_Error_Unknown;
}

eIMediaPlayerStatus IMediaPlayerSession_IpBwGauge(IMediaPlayerSession *pIMediaPlayerSession, const int8_t *sTryServiceUrl,
        uint32_t *pMaxBwProvision, uint32_t *pTryServiceBw, uint32_t *pTotalBwConsumption)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_IpBwGauge(pIMediaPlayerSession, sTryServiceUrl,
                pMaxBwProvision, pTryServiceBw, pTotalBwConsumption);
    }
    return kMediaPlayerStatus_Error_Unknown;
}
IMediaPlayerSession* IMediaPlayerSession_FindSessionFromServiceUrl(const char *srvUrl)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_FindSessionFromServiceUrl(srvUrl);
    }
    return NULL;

}

eIMediaPlayerStatus IMediaPlayerSession_AttachCallback(IMediaPlayerSession *pIMediaPlayerSession, IMediaPlayerStatusCallback eventStatusCB, void *pClientContext)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_AttachCallback(pIMediaPlayerSession, eventStatusCB, pClientContext);
    }
    return kMediaPlayerStatus_Error_Unknown;

}

eIMediaPlayerStatus IMediaPlayerSession_DetachCallback(IMediaPlayerSession *pIMediaPlayerSession, IMediaPlayerStatusCallback eventStatusCB)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_DetachCallback(pIMediaPlayerSession, eventStatusCB);
    }
    return kMediaPlayerStatus_Error_Unknown;

}


eIMediaPlayerStatus IMediaPlayerSession_GetStartPosition(IMediaPlayerSession *pIMediaPlayerSession, float *pNptTime)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_GetStartPosition(pIMediaPlayerSession, pNptTime);
    }
    return kMediaPlayerStatus_Error_Unknown;
}


eIMediaPlayerStatus IMediaPlayerSession_GetEndPosition(IMediaPlayerSession *pIMediaPlayerSession, float *pNptTime)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_GetEndPosition(pIMediaPlayerSession, pNptTime);
    }
    return kMediaPlayerStatus_Error_Unknown;
}


eIMediaPlayerStatus IMediaPlayerSession_SetApplicationDataPid(IMediaPlayerSession *pIMediaPlayerSession, uint32_t pid)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_SetApplicationDataPid(pIMediaPlayerSession, pid);
    }
    return kMediaPlayerStatus_Error_Unknown;

}

eIMediaPlayerStatus IMediaPlayerSession_SetApplicationDataPidExt(IMediaPlayerSession *pIMediaPlayerSession, IMediaPlayerStatusCallback
        eventStatusCB, void *pEventStatusCBClientContext, uint32_t pid, const tIMediaPlayerSfltFilterGroup *sfltGroup, IMediaPlayerClientSession ** ppIMediaPlayerClientSession)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_SetApplicationDataPidExt(pIMediaPlayerSession, eventStatusCB, pEventStatusCBClientContext, pid, sfltGroup, ppIMediaPlayerClientSession);
    }
    return kMediaPlayerStatus_Error_Unknown;

}

eIMediaPlayerStatus IMediaPlayerSession_GetApplicationData(IMediaPlayerSession *pIMediaPlayerSession, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_GetApplicationData(pIMediaPlayerSession, bufferSize, buffer, dataSize);
    }
    return kMediaPlayerStatus_Error_Unknown;

}

eIMediaPlayerStatus IMediaPlayerSession_GetApplicationDataExt(IMediaPlayerClientSession * pIMediaPlayerClientSession,
        IMediaPlayerSession *pIMediaPlayerSession, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_GetApplicationDataExt(pIMediaPlayerClientSession, pIMediaPlayerSession, bufferSize, buffer, dataSize);
    }
    return kMediaPlayerStatus_Error_Unknown;

}

eIMediaPlayerStatus IMediaPlayerSession_SetAudioPid(IMediaPlayerSession *pIMediaPlayerSession, uint32_t pid)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_SetAudioPid(pIMediaPlayerSession, pid);
    }
    return kMediaPlayerStatus_Error_Unknown;

}
eIMediaPlayerStatus IMediaPlayerSession_GetComponents(IMediaPlayerSession *pIMediaPlayerSession, tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayerSession_GetComponents(pIMediaPlayerSession, info, infoSize, count, offset);
    }
    return kMediaPlayerStatus_Error_Unknown;

}

eIMediaPlayerStatus IMediaPlayerSession_GetTypePosition(IMediaPlayerSession *pIMediaPlayerSession,
        eNptTimeType type,
        float *pNptTime)
{
    (void)pIMediaPlayerSession;
    (void)type;
    (void)pNptTime;
    return kMediaPlayerStatus_Error_NotSupported;
}

///This method returns total and used number of resources of input resource type
eIMediaPlayerStatus IMediaPlayer_GetResourceUsage(eResourceType type, int32_t *cfgQuantity, int32_t *used)
{
    IMediaPlayer * player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        return player->IMediaPlayer_GetResourceUsage(type, cfgQuantity, used);
    }
    return kMediaPlayerStatus_Error_Unknown;
}

