#include "MSPSource.h"

#define UNUSED_PARAM(a) (void)a;

eMspStatus MSPSource::setSdv(bool Status, eSdvmServiceAttributeInt attribute)
{
    UNUSED_PARAM(Status);
    UNUSED_PARAM(attribute);

    return kMspStatus_Ok;
}

bool MSPSource::isSDV(void)
{
    return false;
}

bool MSPSource::isMusic(void)
{
    return false;
}

// used to communicate current state to resource monitor
void MSPSource::setTunerPriority(eResMonPriority pri)
{
    UNUSED_PARAM(pri);

}


eMspStatus MSPSource::updateTuningParams(const char *url)
{
    UNUSED_PARAM(url);

    return kMspStatus_Ok;
}


eCsciMspDiagStatus MSPSource::GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo)
{
    UNUSED_PARAM(msgInfo);
    return kCsciMspDiagStat_NoData;
}


eMspStatus MSPSource::setMediaSessionInstance(IMediaPlayerSession *pIMediaPlayerSession)
{
    UNUSED_PARAM(pIMediaPlayerSession);
    return kMspStatus_Ok;
}

#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
tCpePgrmHandle MSPSource::getCpeProgHandle()const
{
    return 0;
}

void MSPSource::SetCpeStreamingSessionID(uint32_t sessionId)
{
    UNUSED_PARAM(sessionId);
    return;
}

eMspStatus MSPSource::InjectCCI(uint8_t CCIbyte)
{
    UNUSED_PARAM(CCIbyte);
    return kMspStatus_Error;
}

#endif
#if PLATFORM_NAME == IP_CLIENT

/* sets the presentation parameters for the video playback screen */
eMspStatus MSPSource::setPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus)
{
    UNUSED_PARAM(vidScreenRect);
    UNUSED_PARAM(enablePictureModeSetting);
    UNUSED_PARAM(enableAudioFocus);
    return kMspStatus_Ok;
}

/* gets the list of audio and video elementary streams present in the service */
eMspStatus MSPSource::GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset, Psi *ptrPsi)
{
    UNUSED_PARAM(info);
    UNUSED_PARAM(infoSize);
    UNUSED_PARAM(count);
    UNUSED_PARAM(offset);
    UNUSED_PARAM(ptrPsi);
    return kMspStatus_Ok;
}

/* sets or enables the audio PID */
eMspStatus MSPSource::SetAudioPid(uint32_t aPid)
{
    UNUSED_PARAM(aPid);
    return kMspStatus_Ok;
}

/* interface to the controller to register a callback with AVPM to receive
 * audio language change notifications */
void MSPSource::SetAudioLangCB(void *aCbData, AudioLangChangedCB aLangChangedCB)
{
    UNUSED_PARAM(aCbData);
    UNUSED_PARAM(aLangChangedCB);
}

/* Updates/Sets the audio language to user preference audio language */
eMspStatus MSPSource::UpdateAudioLanguage(Psi *ptrPsi)
{
    (void) ptrPsi;
    return kMspStatus_Ok;
}

/* Sets the video and audio pid to be decoded and presented */
eMspStatus MSPSource::SetAudioVideoPids(Psi *ptrPsi)
{
    (void) ptrPsi;
    return kMspStatus_Ok;
}

/* Gets the information about the active streaming in-progress */
eCsciMspDiagStatus MSPSource::GetMspStreamingInfo(DiagMspStreamingInfo *msgInfo)
{
    UNUSED_PARAM(msgInfo);
    return kCsciMspDiagStat_NoData;
}

/* Gets the PMT table from the source of the current stream in-progress */
eMspStatus MSPSource::getRawPmt(uint8_t **pPMTData, int *pPMTSize)
{
    UNUSED_PARAM(pPMTData);
    UNUSED_PARAM(pPMTSize);
    return kMspStatus_Ok;
}

/* Selects the AUDIO and VIDEO components to presented as part of the current playback */
eMspStatus MSPSource::SelectAudioVideoComponents(Psi *ptrPsi, bool isEasAudioActive)
{
    UNUSED_PARAM(ptrPsi);
    UNUSED_PARAM(isEasAudioActive);
    return kMspStatus_Ok;
}

/* Filter the application data PID */
eMspStatus MSPSource::filterAppDataPid(uint32_t aPid, DataReadyCallback aDataReadyCB, void *aClientContext, bool isMusic)
{
    UNUSED_PARAM(aPid);
    UNUSED_PARAM(aDataReadyCB);
    UNUSED_PARAM(aClientContext);
    UNUSED_PARAM(isMusic);
    return kMspStatus_Ok;
}

/* Retrieves the application data after filtering for the application data PID */
eMspStatus MSPSource::getApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    UNUSED_PARAM(bufferSize);
    UNUSED_PARAM(buffer);
    UNUSED_PARAM(dataSize);
    return kMspStatus_Ok;
}

/* Get method to retrieve cgmi session handle */
void* MSPSource::getCgmiSessHandle()
{
    return NULL;
}

bool MSPSource::isMosaic(void)
{
    return false;
}
/*Restarts the audio decoding for a session*/
void MSPSource::RestartAudio(void)
{
    return;
}

/*Stops the audio alone for a session*/
void MSPSource::StopAudio(void)
{
    return;
}

eMspStatus MSPSource::formulateCCLanguageList(Psi *ptrPsi)
{

    UNUSED_PARAM(ptrPsi);
    return kMspStatus_Ok;
}
/* Returns the CCI byte value */
uint8_t MSPSource::GetCciBits(void)
{
    return 0;
}
#endif
