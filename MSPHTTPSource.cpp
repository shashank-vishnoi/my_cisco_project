#include <time.h>
#include <sys/time.h>
#include "MSPHTTPSource.h"
#include <cpe_programhandle.h>
#include <cpe_recmgr.h>
#include <sail-clm-api.h>
#include <time.h>


#define LOG(level, msg, args...)  dlog(DL_MSP_MRDVR,level,"MSPHTTPSource:%s:%d " msg, __FUNCTION__, __LINE__, ##args);



MSPHTTPSource::MSPHTTPSource(std::string aSrcUrl)
{
    FNLOG(DL_MSP_MRDVR);
    mSrcUrl = aSrcUrl;
    LOG(DLOGL_NORMAL, "MSPHTTPSource source URL %s %s", mSrcUrl.c_str(), aSrcUrl.c_str());
    buildFileName();
    mProgramNumber = 0;
    mSourceId = 0;
    mCpeSrcHandle = 0;
    mSrcCbIdStreamError = 0;
    mSrcCbIdNetBuffError = 0;
    mSrcCbIdHttpState = 0;
    mSrcStateCB = NULL;
    mClientContext = NULL;
    mHTTPSrcState = kHTTPSrcNotOpened;
    mCurrentSpeedMode =  eCpePlaySpeedMode_Forward;
    /* Initialize mutex variable*/
    pthread_mutex_init(&mrdvr_player_mutex, NULL);
    mPendingPause       = false;
    mPendingPlay        = false;
    mPendingSetSpeed    = false;
    mPendingSetPosition = false;
    currentSpeedNum     = 1;
    currentSpeedDen     = 1;

    buildFileName();

    mParseStatus = kMspStatus_Ok;
}


MSPHTTPSource::~MSPHTTPSource()
{
    unsigned int timeout_ms = 0;
    int cpeStatus;
    FNLOG(DL_MSP_MRDVR);

    if (mCpeSrcHandle != 0)
    {

        cpeStatus = cpe_src_Stop(mCpeSrcHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, " cpe_src_stop failed with error code %d", cpeStatus);
        }

        while ((mHTTPSrcState != kHTTPSrcStopped) && (timeout_ms < 300)) //not waiting more than 30 secs.
        {
            LOG(DLOGL_REALLY_NOISY, "waiting for HTTP source stop callback....");
            timeout_ms += 1; //polling up for every 100 mS
            usleep(100000);
        }

        if (timeout_ms == 300)
        {
            LOG(DLOGL_ERROR, "timeout on expecting CPERP HTTP source stop CB...");
        }

        if (mSrcCbIdStreamError != 0)
        {
            cpe_src_UnregisterCallback(mCpeSrcHandle, mSrcCbIdStreamError);
        }

        if (mSrcCbIdNetBuffError != 0)
        {
            cpe_src_UnregisterCallback(mCpeSrcHandle, mSrcCbIdNetBuffError);
        }

        if (mSrcCbIdHttpState)
        {
            cpe_src_UnregisterCallback(mCpeSrcHandle, mSrcCbIdHttpState);
        }

        cpeStatus = cpe_src_Close(mCpeSrcHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, " cpe_src_close failed with error code %d", cpeStatus);
        }

        mCpeSrcHandle = 0;
    }
    pthread_mutex_destroy(&mrdvr_player_mutex);
}




eMspStatus MSPHTTPSource::load(SourceStateCallback aPlaybackCB, void* aClientContext)
{
    FNLOG(DL_MSP_MRDVR);
    LOG(DLOGL_NORMAL, " MSPHTTPSource Load parse status:%d", mParseStatus);

    mSrcStateCB = aPlaybackCB;
    mClientContext = aClientContext;

    return mParseStatus;
}




eMspStatus MSPHTTPSource::open(eResMonPriority pri)
{

    FNLOG(DL_MSP_MRDVR);
    (void) pri;  // not used for HTTP source

    if (mParseStatus != kMspStatus_Ok)
    {
        return mParseStatus;
    }

    int cpeStatus;
    cpeStatus = cpe_src_Open(eCpeSrcType_HTTPPlaySource, &mCpeSrcHandle);
    if (cpeStatus != kCpe_NoErr)
    {
        // return an error
        LOG(DLOGL_ERROR, " cpe_src_open failed with Error code %d", cpeStatus);
        return kMspStatus_BadSource;
    }

    cpeStatus = cpe_src_RegisterCallback(eCpeSrcCallbackTypes_StreamErrorCallback, (void*)this, (tCpeSrcCallbackFunction)PlayBkCallbackFunction, &mSrcCbIdStreamError, mCpeSrcHandle, NULL);

    if (cpeStatus != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, " CPERP register callback for MRDVR HTTP Source stream playback stream error   failed with error code %d", cpeStatus);
        return kMspStatus_BadSource;
    }

    cpeStatus = cpe_src_RegisterCallback(eCpeSrcCallbackTypes_NetworkBuffer, (void*)this, (tCpeSrcCallbackFunction)PlayBkCallbackFunction, &mSrcCbIdNetBuffError, mCpeSrcHandle, NULL);

    if (cpeStatus != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, " CPERP register callback for MRDVR HTTP Source Network buffer failed with error code %d", cpeStatus);
        return kMspStatus_BadSource;
    }


    cpeStatus = cpe_src_RegisterCallback(eCpeSrcCallbackTypes_HTTPSrcStateCallback, (void*)this, (tCpeSrcCallbackFunction)PlayBkCallbackFunction, &mSrcCbIdHttpState, mCpeSrcHandle, NULL);

    if (cpeStatus != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, " CPERP register callback for MRDVR HTTP Source state failed with error code %d", cpeStatus);
        return kMspStatus_BadSource;
    }


    cpeStatus = cpe_src_Set(mCpeSrcHandle, eCpeSrcNames_URL, (void *)mFileName.c_str());
    if (cpeStatus != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, " CPERP source set for MRDVR  HTTP Source stream playback failed with error code %d", cpeStatus);
        return kMspStatus_BadSource;
    }

    LOG(DLOGL_NORMAL, " MSPHTTPSource open return success");

    return kMspStatus_Ok;
}




eMspStatus MSPHTTPSource::start()
{
// Start the source
    FNLOG(DL_MSP_MPLAYER);
    if (mCpeSrcHandle != 0)
    {
        int status;
        status = cpe_src_Start(mCpeSrcHandle);
        if (status != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "cpe_src_start error %d for HTTP Source start", status);
            return kMspStatus_BadSource;
        }

        return kMspStatus_Ok;
    }
    else
    {
        return kMspStatus_StateError;
    }
}




eMspStatus MSPHTTPSource::stop()
{
    // Stop the source
    int cpeStatus;
    eMspStatus status = kMspStatus_Ok;
    unsigned int timeout_ms = 0;

    FNLOG(DL_MSP_MPLAYER);


    if (mCpeSrcHandle != 0)
    {
        cpeStatus = cpe_src_Stop(mCpeSrcHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "cpe_src_stop failed with error code %d", cpeStatus);
        }
        else
        {
            LOG(DLOGL_NORMAL, "cpe_src_stop success");
        }


        while ((mHTTPSrcState != kHTTPSrcStopped) && (timeout_ms < 300)) //not waiting more than 30 secs.
        {
            timeout_ms += 1; //polling up for every 100 mS
            LOG(DLOGL_NORMAL, "HTTP Stop polling....\n");
            usleep(100000);
        }

        if (timeout_ms == 300)
        {
            LOG(DLOGL_ERROR, "timeout on expecting CPERP HTTP source stop CB...");
        }


        if (mSrcCbIdStreamError)
        {
            cpe_src_UnregisterCallback(mCpeSrcHandle, mSrcCbIdStreamError);
            mSrcCbIdStreamError = 0;
        }

        if (mSrcCbIdNetBuffError)
        {
            cpe_src_UnregisterCallback(mCpeSrcHandle, mSrcCbIdNetBuffError);
            mSrcCbIdNetBuffError = 0;
        }

        if (mSrcCbIdHttpState)
        {
            cpe_src_UnregisterCallback(mCpeSrcHandle, mSrcCbIdHttpState);
            mSrcCbIdHttpState = 0;
        }


        cpeStatus = cpe_src_Close(mCpeSrcHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "cpe_src_close failed with error code %d", cpeStatus);
            status = kMspStatus_Error;
        }
        else
        {
            LOG(DLOGL_NORMAL, "cpe_src_close success");
            mCpeSrcHandle = 0;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "NUll source handle.Skipping stop,close and unregistering");
    }


    return status;
}




int MSPHTTPSource::PlayBkCallbackFunction(tCpeSrcCallbackTypes aType, void *aUserData, void *pCallbackSpecific)
{
    // Call Registered callback
    FNLOG(DL_MSP_MPLAYER);

    LOG(DLOGL_NORMAL, "MSP MRDVR source callback with type %d", aType);

    MSPHTTPSource *inst = (MSPHTTPSource *)aUserData;
    if (NULL == inst)
    {
        LOG(DLOGL_ERROR, "Unknown callback received");
        return -1;
    }
    else
    {
        switch (aType)
        {
        case eCpeSrcCallbackTypes_NetworkBuffer:
        {
            tCpeSrcNetBufCallbackSpecific *pNetBuf = (tCpeSrcNetBufCallbackSpecific *)pCallbackSpecific;

            switch (pNetBuf->reason)
            {
            case eCpeSrcNetBuf_Starting:
                LOG(DLOGL_NORMAL, "%s: Network Buffer Status: Buffering Started", __FUNCTION__);
                break;

            case eCpeSrcNetBuf_Progress:
                LOG(DLOGL_NORMAL, "%s: Network Buffer Status: Buffering in progress", __FUNCTION__);
                break;

            case eCpeSrcNetBuf_Complete:
                LOG(DLOGL_NORMAL, "%s: Network Buffer Status: Buffering complete", __FUNCTION__);
                break;

            case eCpeSrcNetBuf_Underflow:
                LOG(DLOGL_NORMAL, "%s: Network Buffer Status: Underflow", __FUNCTION__);
                break;

            case eCpeSrcNetBuf_EOF:
                LOG(DLOGL_NOISE, "Network EOF callback received.Ignoring them");
                break;

            case eCpeSrcNetBuf_BOF:
                LOG(DLOGL_NOISE, "Network BOF callback received.Ignoring them");
                break;

            default:
                LOG(DLOGL_NORMAL, "%s: UNKNOWN Network Buffer Status: %d\n", __FUNCTION__, pNetBuf->reason);
                break;
            }
            break;
        }

        case eCpeSrcCallbackTypes_StreamErrorCallback:
            LOG(DLOGL_ERROR, "Stream error occured");
            inst->mSrcStateCB(inst->mClientContext, kSrcProblem);
            break;

        case eCpeSrcCallbackTypes_HTTPSrcStateCallback:
        {
            LOG(DLOGL_NORMAL, "HTTP Source state callback");
            tCpeHttpSrcStateExt *HTTPSrc = (tCpeHttpSrcStateExt *)pCallbackSpecific;

            switch (HTTPSrc->state)
            {
            case eCpeSrcHttpState_Stopped:
                LOG(DLOGL_NORMAL, "HTTP Source at stopped state");

                switch (HTTPSrc->reason)
                {
                case eCpeSrcStateReason_EOF:
                    LOG(DLOGL_NORMAL, "HTTP Src EOF callback received with HTTP state as Stopped");
                    inst->mSrcStateCB(inst->mClientContext, kSrcEOF);
                    break;

                case eCpeSrcStateReason_BOF:
                    LOG(DLOGL_NORMAL, "HTTP Src  BOF callback received with HTTP state as Stopped ");
                    pthread_mutex_lock(&(inst->mrdvr_player_mutex));
                    LOG(DLOGL_NORMAL, "Clearing off pending speed  and position settings");
                    inst->mPendingPause  = false;
                    inst->mPendingPlay   = false;
                    inst->mPendingSetSpeed    = false;
                    inst->mPendingSetPosition = false;
                    inst->mSrcStateCB(inst->mClientContext, kSrcBOF);
                    pthread_mutex_unlock(&(inst->mrdvr_player_mutex));
                    break;

                default:
                    LOG(DLOGL_NORMAL, " UNKNOWN HTTP reason: %d with HTTP state as Stopped", HTTPSrc->reason);
                    break;

                }
                inst->mHTTPSrcState = kHTTPSrcStopped;
                break;

            case eCpeSrcHttpState_Playing:
                pthread_mutex_lock(&(inst->mrdvr_player_mutex));
                LOG(DLOGL_NORMAL, " HTTP Source at playing state");

                switch (HTTPSrc->reason)
                {
                case eCpeSrcStateReason_EOF:
                    LOG(DLOGL_NORMAL, "HTTP Src EOF callback received with HTTP state as Playing");
                    inst->mSrcStateCB(inst->mClientContext, kSrcEOF);
                    break;

                case eCpeSrcStateReason_BOF:
                    LOG(DLOGL_NORMAL, "HTTP Src  BOF callback received with HTTP state as Playing ");
                    pthread_mutex_lock(&(inst->mrdvr_player_mutex));
                    LOG(DLOGL_NORMAL, "Clearing off pending speed  and position settings");
                    inst->mPendingPause  = false;
                    inst->mPendingPlay   = false;
                    inst->mPendingSetSpeed    = false;
                    inst->mPendingSetPosition = false;
                    inst->mSrcStateCB(inst->mClientContext, kSrcBOF);
                    pthread_mutex_unlock(&(inst->mrdvr_player_mutex));
                    break;

                default:
                    LOG(DLOGL_NORMAL, " UNKNOWN HTTP reason: %d with HTTP state as Playing", HTTPSrc->reason);
                    if ((inst->currentSpeedNum > 1) && (inst->mPendingPlay == false))
                    {
                        LOG(DLOGL_NORMAL, "Previous trick was fast forward, hence triggereing EOF");
                        inst->mSrcStateCB(inst->mClientContext, kSrcEOF);
                    }
                    break;
                }
                inst->mHTTPSrcState = kHTTPSrcPlaying;

                if (inst->mPendingSetPosition == true)
                {
                    inst->mSrcStateCB(inst->mClientContext, kSrcPendingSetPosition);
                    inst->mPendingSetPosition = false;
                }

                if (inst->mPendingSetSpeed == true)
                {
                    inst->mSrcStateCB(inst->mClientContext, kSrcPendingSetSpeed);
                    inst->mPendingSetSpeed = false;
                }

                inst->mPendingPlay = false;

                tCpePlaySpeed aPlaySpeed;
                int  cpeStatus;
                cpeStatus = cpe_src_Get(inst->mCpeSrcHandle, eCpeSrcNames_PlaySpeed, (void *)&aPlaySpeed, sizeof(tCpePlaySpeed));
                if (cpeStatus == kCpe_NoErr)
                {
                    inst->currentSpeedNum = aPlaySpeed.scale.numerator;
                    inst->currentSpeedDen = aPlaySpeed.scale.denominator;
                    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, " inst->currentSpeedNum:%d inst->currentSpeedDen:%d", inst->currentSpeedNum, inst->currentSpeedDen);
                }
                else
                {
                    dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "cpe_src_Get for eCpeSrcNames_PlaySpeed  cpeStatus:%d", cpeStatus);
                }
                pthread_mutex_unlock(&(inst->mrdvr_player_mutex));
                break;

            case eCpeSrcHttpState_Paused:
                pthread_mutex_lock(&(inst->mrdvr_player_mutex));
                LOG(DLOGL_NORMAL, " HTTP Source at paused state ");

                switch (HTTPSrc->reason)
                {
                case eCpeSrcStateReason_EOF:
                    LOG(DLOGL_NORMAL, "HTTP Src EOF callback received with HTTP state as Paused");
                    inst->mSrcStateCB(inst->mClientContext, kSrcEOF);
                    break;

                case eCpeSrcStateReason_BOF:
                    LOG(DLOGL_NORMAL, "HTTP Src  BOF callback received with HTTP state as Paused");
                    pthread_mutex_lock(&(inst->mrdvr_player_mutex));
                    LOG(DLOGL_NORMAL, "Clearing off pending speed  and position settings");
                    inst->mPendingPause  = false;
                    inst->mPendingPlay   = false;
                    inst->mPendingSetSpeed    = false;
                    inst->mPendingSetPosition = false;
                    inst->mSrcStateCB(inst->mClientContext, kSrcBOF);
                    pthread_mutex_unlock(&(inst->mrdvr_player_mutex));
                    break;

                default:
                    LOG(DLOGL_NORMAL, " UNKNOWN HTTP reason: %d with HTTP state as Paused", HTTPSrc->reason);
                    break;
                }

                if (inst->mPendingSetPosition == true)
                {
                    inst->mSrcStateCB(inst->mClientContext, kSrcPendingSetPosition);
                    inst->mPendingSetPosition = false;
                }
                inst->mHTTPSrcState = kHTTPSrcPaused;
                if (inst->mPendingSetSpeed == true)
                {
                    inst->mSrcStateCB(inst->mClientContext, kSrcPendingSetSpeed);
                    inst->mPendingSetSpeed = false;
                }
                inst->mPendingPause = false;
                pthread_mutex_unlock(&(inst->mrdvr_player_mutex));
                break;

            case eCpeSrcHttpState_MimeAcquired:
                LOG(DLOGL_NORMAL, " HTTP Source at Mime Acquired state ");
                break;

            case eCpeSrcHttpState_Invalid:
                LOG(DLOGL_NORMAL, " HTTP Source at Invalid state ");
                inst->mHTTPSrcState = kHTTPSrcInvalid;
                break;

            default:
                LOG(DLOGL_NORMAL, "HTTP Source default state %d", HTTPSrc->reason);
                inst->mHTTPSrcState = kHTTPSrcInvalid;
                break;
            }
            break;
        }
        default:
            LOG(DLOGL_ERROR, "Unknown handled callback event %d", aType);
            break;

        }
    }
    return 0;
}


tCpeSrcHandle MSPHTTPSource::getCpeSrcHandle()const
{
    return mCpeSrcHandle;
}

int MSPHTTPSource::getProgramNumber()const
{
    return mProgramNumber;
}

int MSPHTTPSource::getSourceId()const
{
    return mSourceId;
}

std::string MSPHTTPSource::getSourceUrl()const
{
    return mSrcUrl;
}

std::string MSPHTTPSource::getFileName()const
{
    return mFileName;
}

bool MSPHTTPSource::isDvrSource()const
{
    return true;
}

bool MSPHTTPSource::canRecord()const
{
    return false;
}

eMspStatus MSPHTTPSource::setSpeed(tCpePlaySpeed aPlaySpeed, uint32_t aNpt)
{
    FNLOG(DL_MSP_MPLAYER);
    int cpeStatus;

    if (mCpeSrcHandle)
    {

        pthread_mutex_lock(&mrdvr_player_mutex);


        if ((currentSpeedNum == aPlaySpeed.scale.numerator) && (currentSpeedDen == aPlaySpeed.scale.denominator))
        {
            dlog(DL_MSP_MRDVR, DLOGL_NORMAL, " Request to set 100/100 forward speed, when player is already in that state.Ignoring it");
            pthread_mutex_unlock(&mrdvr_player_mutex);
            return kMspStatus_Ok;
        }

        if ((mPendingPause == true) || (mPendingPlay == true))
        {
            dlog(DL_MSP_MRDVR, DLOGL_NORMAL, "\"Paused\" or \"Playing \" callback from platform is pending. Delaying SetSpeed settings");
            mPendingSetSpeed = true;
            pthread_mutex_unlock(&mrdvr_player_mutex);
            return kMspStatus_Loading;
        }

        if (aPlaySpeed.mode == eCpePlaySpeedMode_Pause)
        {
            mPendingPause = true;
        }
        else
        {
            mPendingPlay = true;
        }

        pthread_mutex_unlock(&mrdvr_player_mutex);

        currentSpeedNum = aPlaySpeed.scale.numerator;
        currentSpeedDen = aPlaySpeed.scale.denominator;


        cpeStatus = cpe_src_Set(mCpeSrcHandle, eCpeSrcNames_PlaySpeed, (void *)&aPlaySpeed);
        if (cpeStatus != kCpe_NoErr)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "cpe_src_set for play speed error code %d", cpeStatus);
            return kMspStatus_BadSource;
        }

        if (aNpt)
        {
            cpeStatus = cpe_src_Set(mCpeSrcHandle, eCpeSrcNames_CurrentNPT, (void *)&aNpt);
            if (cpeStatus != kCpe_NoErr)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "cpe_src_set for NPT error code %d", cpeStatus);
                return kMspStatus_BadSource;
            }
        }
        mCurrentSpeedMode = aPlaySpeed.mode;
        return kMspStatus_Ok;
    }
    else
    {
        return kMspStatus_Error;
    }
}




void MSPHTTPSource::buildFileName()
{
    int pos;
    std::string temp;
    FNLOG(DL_MSP_MRDVR);

    pos = mSrcUrl.find("mrdvr://");
    if (pos == 0)
    {
        temp = mSrcUrl.substr(strlen("mrdvr://"));
        mFileName = temp;
        LOG(DLOGL_NORMAL, "MRDVR  file name  %s", mFileName.c_str());
        mParseStatus = kMspStatus_Ok;
    }

}




eMspStatus MSPHTTPSource::getPosition(float *pNptTime)
{

    uint32_t npt;

    FNLOG(DL_MSP_MRDVR);

    if (pNptTime)
    {
        if ((cpe_src_Get(mCpeSrcHandle, eCpeSrcNames_CurrentNPT, (void *)&npt, sizeof(uint32_t))) == kCpe_NoErr)
        {
            *(float *)pNptTime = (float)(npt / 1000);
            LOG(DLOGL_NORMAL, "  GetPosition in MSPHTTPSource pNptTime = %f ", *pNptTime);
            return  kMspStatus_Ok;
        }
        else
        {
            LOG(DLOGL_ERROR, " cpe_src_Get failed to get eCpeSrcNames_CurrentNPT");
            return kMspStatus_Error;
        }

    }
    else
    {
        return kMspStatus_Error;
    }

}




eMspStatus MSPHTTPSource::setPosition(float aNptTime)
{
    uint32_t actualNpt = (uint32_t)aNptTime;
    std::string filename = mFileName;
    FNLOG(DL_MSP_MRDVR);

    actualNpt = (uint32_t)(aNptTime * 1000);


    pthread_mutex_lock(&mrdvr_player_mutex);
    if ((mPendingPause == true) || (mPendingPlay == true))
    {
        dlog(DL_MSP_MRDVR, DLOGL_NORMAL, "\"Paused\" or \"Playing \" callback from platform is pending. Delaying SetPosition settings");
        mPendingSetPosition = true;
        pthread_mutex_unlock(&mrdvr_player_mutex);
        return kMspStatus_Loading;
    }
    pthread_mutex_unlock(&mrdvr_player_mutex);

    // Set Record Position
    LOG(DLOGL_REALLY_NOISY, " Actual Npt set is %d", actualNpt);
    int status = cpe_src_Set(mCpeSrcHandle, eCpeSrcNames_CurrentNPT, (void *)&actualNpt);
    if (status != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, " cpe_src_Set failed to set eCpeSrcNames_CurrentNPT, Error = %d\n", status);
        return kMspStatus_Error;
    }
    else
    {
        LOG(DLOGL_NORMAL, " cpe_src_Set(...) success  %d", actualNpt);
    }

    return kMspStatus_Ok;
}




bool MSPHTTPSource::isPPV(void)
{
    return false;
}

bool MSPHTTPSource::isQamrf(void)
{
    return false;
}

eMspStatus MSPHTTPSource::release()
{
    FNLOG(DL_MSP_MRDVR);
    // release the source
    return kMspStatus_Ok;
}

tCpePgrmHandle MSPHTTPSource::getCpeProgHandle()const
{
    return 0;
}

void MSPHTTPSource::SetCpeStreamingSessionID(uint32_t sessionId)
{
    (void) sessionId;
    return;
}
