#include <time.h>
#include "MrdvrRecStreamer.h"
#include <cpe_recmgr.h>
#include <sail-clm-api.h>
#include "cpe_hnservermgr.h"

#define SCOPELOG(section, scopename)  dlogns::ScopeLog __xscopelog(section, scopename, __FILE__, __LINE__, DLOGL_FUNCTION_CALLS)
#define FNLOG(section)  dlogns::ScopeLog __xscopelog(section, __PRETTY_FUNCTION__, __FILE__, __LINE__, DLOGL_FUNCTION_CALLS)
#ifdef LOG
#error  LOG already defined
#endif
#define LOG(level, msg, args...)  dlog(DL_MSP_MRDVR, level, "MrdvrRecStreamer:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#define UNUSED_PARAM(a) (void)a;

bool MrdvrRecStreamer::mEasAudioActive = false;

MrdvrRecStreamer::MrdvrRecStreamer(IMediaPlayerSession *pIMediaPlayerSession)
{
    mIMediaPlayerSession = pIMediaPlayerSession;
    mDestUrl = "";
    mSourceUrl = "";
    pthread_mutex_init(&mMutex, NULL);
    mPtrRecSource = NULL;
    mSessionId = 0;
}

MrdvrRecStreamer::~MrdvrRecStreamer()
{
    std::list<CallbackInfo*>::iterator iter;
    LOG(DLOGL_NORMAL, "SIZE=%d", mCallbackList.size());

    for (iter = mCallbackList.begin(); iter != mCallbackList.end(); iter++)
    {
        delete(*iter);
        *iter = NULL;
    }

    mCallbackList.clear();
    LOG(DLOGL_NORMAL, "AFTER SIZE=%d", mCallbackList.size());

    if (mPtrRecSource != NULL)
    {
        mPtrRecSource->stop();
        delete mPtrRecSource;
        mPtrRecSource = NULL;
    }
}

eIMediaPlayerStatus MrdvrRecStreamer::Load(const char* serviceUrl, const MultiMediaEvent **pMme)
{
    UNUSED_PARAM(pMme)

    eIMediaPlayerStatus mediaPlayerStatus = kMediaPlayerStatus_Ok;
    mSourceUrl = serviceUrl;
    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "%s: Source URL is %s", __FUNCTION__, mSourceUrl.c_str());

    /* Check for valid source URL */
    if (serviceUrl == NULL)
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s: NULL Source URL\n", __FUNCTION__);
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    /* Check for non-existence of source with the controller */
    if (mPtrRecSource == NULL)
    {
        mCurrentSource = MSPSourceFactory::getMSPSourceType(serviceUrl);
        mPtrRecSource  = MSPSourceFactory::getMSPSourceInstance(mCurrentSource, serviceUrl, mIMediaPlayerSession);
    }
    else
    {
        mediaPlayerStatus = kMediaPlayerStatus_Error_OutOfState;
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s: Failed %d\n", __FUNCTION__, mediaPlayerStatus);
        return mediaPlayerStatus;
    }

    /* Load the source */
    mediaPlayerStatus = loadSource();

    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "%s: returning %d\n", __FUNCTION__, mediaPlayerStatus);

    return mediaPlayerStatus;
}

eIMediaPlayerStatus MrdvrRecStreamer::loadSource()
{
    eMspStatus status = kMspStatus_Ok;

    eIMediaPlayerStatus mediaPlayerStatus = kMediaPlayerStatus_Ok;

    /* Check for valid source */
    if (!mPtrRecSource)
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s: error: NULL source\n", __FUNCTION__);
        return kMediaPlayerStatus_Error_Unknown;
    }

    /* Load source */
    status = mPtrRecSource->load(sourceCB, (void*) this);
    if (status != kMspStatus_Ok)
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s: Source load failed %d\n", __FUNCTION__, status);
        mediaPlayerStatus = kMediaPlayerStatus_Error_Unknown;
    }

    return mediaPlayerStatus;
}

eIMediaPlayerStatus MrdvrRecStreamer::Play(const char* outputUrl, float nptStartTime, const MultiMediaEvent **pMme)
{
    UNUSED_PARAM(pMme)
    UNUSED_PARAM(outputUrl)
    UNUSED_PARAM(nptStartTime)

    eIMediaPlayerStatus mediaPlayerStatus = kMediaPlayerStatus_Ok;

    /* Check for valid source */
    if (!mPtrRecSource)
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s: NULL Source\n", __FUNCTION__);
        return kMediaPlayerStatus_Error_Unknown;
    }

    /* Open Source */
    eMspStatus status = mPtrRecSource->open(kRMPriority_VideoWithAudioFocus);
    if (status != kMspStatus_Ok)
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s: Source open failed %d\n", __FUNCTION__, status);
        mediaPlayerStatus = kMediaPlayerStatus_ServerError;
    }
    else
    {
        /* Start Source */
        status = mPtrRecSource->start();
        if (status != kMspStatus_Ok)
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s: Source start failed %d\n", __FUNCTION__, status);
            mediaPlayerStatus = kMediaPlayerStatus_ServerError;
        }
    }

    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "%s: returning status %d\n", __FUNCTION__, mediaPlayerStatus);

    return mediaPlayerStatus;
}

eIMediaPlayerStatus MrdvrRecStreamer::Stop(bool stopPlay, bool stopPersistentRecord)
{
    UNUSED_PARAM(stopPlay)
    UNUSED_PARAM(stopPersistentRecord)

    eIMediaPlayerStatus mediaPlayerStatus = kMediaPlayerStatus_Ok;
    eMspStatus ret_value = kMspStatus_Ok;

    /* Check for valid source */
    if (!mPtrRecSource)
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s: NULL Source\n", __FUNCTION__);
        return kMediaPlayerStatus_Error_Unknown;
    }

    /* Stop the streaming source that is doing HN Live Streaming from TSB source */
    ret_value = mPtrRecSource->stop();
    if (ret_value != kMspStatus_Ok)
    {
        mediaPlayerStatus = kMediaPlayerStatus_ServerError;
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s: Source Stop failed %d\n", __FUNCTION__, ret_value);
    }

    /* delete the source */
    delete mPtrRecSource;
    mPtrRecSource = NULL;

    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "%s: returning status %d\n", __FUNCTION__, mediaPlayerStatus);

    return  mediaPlayerStatus;
}

void MrdvrRecStreamer::Eject()
{
    return;
}

void MrdvrRecStreamer::sourceCB(void *data, eSourceState aSrcState)
{
    UNUSED_PARAM(data)
    UNUSED_PARAM(aSrcState)
}

eIMediaPlayerStatus MrdvrRecStreamer::PersistentRecord(const char* recordUrl, float nptRecordStartTime, float nptRecordStopTime, const MultiMediaEvent **pMme)
{
    UNUSED_PARAM(recordUrl)
    UNUSED_PARAM(nptRecordStartTime)
    UNUSED_PARAM(nptRecordStopTime)
    UNUSED_PARAM(pMme)

    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::SetSpeed(int numerator, unsigned int denominator)
{
    UNUSED_PARAM(numerator)
    UNUSED_PARAM(denominator)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::GetSpeed(int* pNumerator, unsigned int* pDenominator)
{
    UNUSED_PARAM(pNumerator)
    UNUSED_PARAM(pDenominator)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::SetPosition(float nptTime)
{
    UNUSED_PARAM(nptTime)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::GetPosition(float* pNptTime)
{
    UNUSED_PARAM(pNptTime)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::IpBwGauge(const char *sTryServiceUrl, unsigned int *pMaxBwProvision, unsigned int *pTryServiceBw, unsigned int *pTotalBwCoynsumption)
{
    UNUSED_PARAM(sTryServiceUrl)
    UNUSED_PARAM(pMaxBwProvision)
    UNUSED_PARAM(pTryServiceBw)
    UNUSED_PARAM(pTotalBwCoynsumption)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::SetPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus)
{
    UNUSED_PARAM(vidScreenRect)
    UNUSED_PARAM(enablePictureModeSetting)
    UNUSED_PARAM(enableAudioFocus)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::GetStartPosition(float* pNptTime)
{
    UNUSED_PARAM(pNptTime)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::GetEndPosition(float* pNptTime)
{
    UNUSED_PARAM(pNptTime)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::SetCallback(IMediaPlayerSession* pIMediaPlayerSession, IMediaPlayerStatusCallback cb, void* pClientContext)
{
    CallbackInfo *cbInfo = new CallbackInfo();
    if (cbInfo)
    {
        cbInfo->mpSession = pIMediaPlayerSession;
        cbInfo->mCallback = cb;
        cbInfo->mClientContext = pClientContext;
        mCallbackList.push_back(cbInfo);
    }

    return  kMediaPlayerStatus_Ok;

}


eIMediaPlayerStatus MrdvrRecStreamer::DetachCallback(IMediaPlayerStatusCallback cb)
{
    UNUSED_PARAM(cb)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::RegisterCCICallback(void* data, CCIcallback_t cb)
{
    UNUSED_PARAM(data)
    UNUSED_PARAM(cb)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::UnRegisterCCICallback()
{
    return  kMediaPlayerStatus_Error_NotSupported;
}

std::string MrdvrRecStreamer::GetSourceURL(bool liveSrcOnly)const
{
    UNUSED_PARAM(liveSrcOnly)
    return  mSourceUrl;
}

std::string MrdvrRecStreamer::GetDestURL()const
{
    return  mDestUrl;
}

bool MrdvrRecStreamer::isRecordingPlayback()const
{
    return  false;
}

bool MrdvrRecStreamer::isLiveRecording()const
{
    return  false;
}

bool MrdvrRecStreamer::isLiveSourceUsed() const
{
    return  false;
}

eIMediaPlayerStatus MrdvrRecStreamer::SetApplicationDataPid(uint32_t aPid)
{
    UNUSED_PARAM(aPid)
    return  kMediaPlayerStatus_Error_NotSupported;
}

uint32_t MrdvrRecStreamer::GetSDVClentContext(IMediaPlayerClientSession *ApplnClient)
{
    UNUSED_PARAM(ApplnClient)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::SetApplicationDataPidExt(IMediaPlayerClientSession *ApplnClient)
{
    UNUSED_PARAM(ApplnClient)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset)
{
    UNUSED_PARAM(info)
    UNUSED_PARAM(infoSize)
    UNUSED_PARAM(count)
    UNUSED_PARAM(offset)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::GetApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    UNUSED_PARAM(bufferSize)
    UNUSED_PARAM(buffer)
    UNUSED_PARAM(dataSize)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::GetApplicationDataExt(IMediaPlayerClientSession *ApplnClient, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    UNUSED_PARAM(ApplnClient)
    UNUSED_PARAM(bufferSize)
    UNUSED_PARAM(buffer)
    UNUSED_PARAM(dataSize)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::SetAudioPid(uint32_t pid)
{
    UNUSED_PARAM(pid)
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::CloseDisplaySession()
{
    return  kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus MrdvrRecStreamer::StartDisplaySession()
{
    return  kMediaPlayerStatus_Error_NotSupported;
}

eCsciMspDiagStatus MrdvrRecStreamer::GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo)
{
    UNUSED_PARAM(msgInfo)
    return  kCsciMspDiagStat_NoData;
}

bool MrdvrRecStreamer::isBackground(void)
{
    return  true;
}

void MrdvrRecStreamer::lockMutex(void)
{
    pthread_mutex_lock(&mMutex);
}

void MrdvrRecStreamer::unLockMutex(void)
{
    pthread_mutex_unlock(&mMutex);
}

void MrdvrRecStreamer::addClientSession(IMediaPlayerClientSession *pClientSession)
{
    UNUSED_PARAM(pClientSession)
    return;
}

void MrdvrRecStreamer::deleteClientSession(IMediaPlayerClientSession *pClientSession)
{
    UNUSED_PARAM(pClientSession)
    return;
}

void MrdvrRecStreamer::deleteAllClientSession()
{
    return;
}

void MrdvrRecStreamer::StopAudio(void)
{
    return;
}

void MrdvrRecStreamer::RestartAudio(void)
{
    return;
}

tCpePgrmHandle MrdvrRecStreamer::getCpeProgHandle()
{
    if (mPtrRecSource)
    {
        return mPtrRecSource->getCpeProgHandle();
    }

    return 0;
}

void MrdvrRecStreamer::SetCpeStreamingSessionID(uint32_t sessionId)
{
    mSessionId = sessionId;
    if (mPtrRecSource)
    {
        mPtrRecSource->SetCpeStreamingSessionID(sessionId);
    }
    return;
}


void MrdvrRecStreamer::InjectCCI(uint8_t CCIbyte)
{
    if (mPtrRecSource)
    {

        eMspStatus Status = mPtrRecSource->InjectCCI(CCIbyte);
        if (kMspStatus_Ok == Status)
        {
            dlog(DL_MSP_MRDVR, DLOGL_NORMAL, "%s: InjectCCI returns success from streamer source\n", __FUNCTION__);
        }
        else
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s:Injectcci value is failed\n", __FUNCTION__);
        }
    }
    else
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s: InjectCCI is called but mPtrRecSource  is NULL\n", __FUNCTION__);

    }
}

/// This method stops the streaming source associated with this controller
eIMediaPlayerStatus MrdvrRecStreamer::StopStreaming()
{
    FNLOG(DL_MSP_MRDVR);
    eIMediaPlayerStatus playerStatus = kMediaPlayerStatus_Ok;
    eMspStatus sourceStatus = kMspStatus_Ok;
    if (mPtrRecSource != NULL)
    {
        sourceStatus = mPtrRecSource->release();
        if (sourceStatus != kMspStatus_Ok)
        {
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s:Streamer Source release failed\n", __FUNCTION__);
            playerStatus = kMediaPlayerStatus_Error_OutOfState;
        }
    }
    else
    {
        dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s:Null Streamer Source", __FUNCTION__);
        playerStatus = kMediaPlayerStatus_Error_OutOfState;
    }
    return playerStatus;
}

// Responsible for starting the EAS audio playback
// This API is applicable for the controllers associated with EAS audio playback session
// For the controllers associated with non EAS audio playback sessions simply returned
// Since MrdvrRecStreamer is not the controller associated with EAS audio playback session,
// API is returning here without any action performed
void MrdvrRecStreamer::startEasAudio(void)
{
    dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(), is not responsible for controlling EAS", __FUNCTION__);
    return;
}

// Sets the EAS audio playback active status
// True  - EAS audi playback is in progress
// False - EAS audio playback is not in progress
void MrdvrRecStreamer::SetEasAudioActive(bool active)
{
    mEasAudioActive = active;
}
