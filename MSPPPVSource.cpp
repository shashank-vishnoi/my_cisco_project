////////////////////////////////////////////////////////////////////////////////////////
//
//
// MSPPPVSource.cpp -- Implementation file for re-factored Pay-per-View source class
//
//
//
//
//
////////////////////////////////////////////////////////////////////////////////////////

// standard includes
#include <time.h>

// local includes
#include "MSPPPVSource.h"

#include <csci-clm-api.h>

// CPERP-specific includes
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#include <cpe_error.h>
#endif
#include <pthread_named.h>


std::list<void*> MSPPPVSource::mActiveControllerList;
pthread_mutex_t  MSPPPVSource::mControllerListMutex;
bool             MSPPPVSource::mControllerListInititialized = false;

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
MSPPPVSource::MSPPPVSource(std::string aSrcUrl, IMediaPlayerSession *pIMediaPlayerSession) : MSPRFSource(aSrcUrl, 0, NULL, pIMediaPlayerSession)
{
    FNLOG(DL_MSP_MPLAYER);

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "MSPPPVSource construct %p", this);

    if (!mControllerListInititialized)
    {
        // ensure mutext is only initialized once
        pthread_mutex_init(&mControllerListMutex, NULL);
        mControllerListInititialized = true;
    }

    // init our sub-class specific stuff
    mPPVState = kPPVIdle;
    ppvEvent = NULL;

    // Record current controller in list
    pthread_mutex_lock(&mControllerListMutex);
    mActiveControllerList.push_back(this);
    pthread_mutex_unlock(&mControllerListMutex);
}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
MSPPPVSource::~MSPPPVSource()
{
    ePpvResult status;

    FNLOG(DL_MSP_MPLAYER);
    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "MSPPPVSource destruct %p", this);

    pthread_mutex_lock(&mControllerListMutex);
    mActiveControllerList.remove(this);
    pthread_mutex_unlock(&mControllerListMutex);

    status = UnRegisterTuningInfo(ppvEvent);
    if (status != kPpvResult_Ok)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Error: UnRegisterTuningInfo status: %d", status);
    }
}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
void MSPPPVSource::PPVMgrInstanceCallback(void *ppvData, ePpvStateEnum ppvstate)
{
    FNLOG(DL_MSP_MPLAYER);

    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Got callback from PPV Manager!.Current PPV state is new ppv state is %d %d ppvData is %p", mPPVState, ppvstate, ppvData);
    ppvEvent = ppvData;

    if (!mSrcStateCB)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, " PPVSource callback poniter is NULL to inform Controller:");
        return ;
    }

    switch (ppvstate)
    {
    case kPpvVideoStart :            //PPV event has started streaming
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Got callback from PPV Manager!.Start Video streaming!");
        mPPVState = kPPVStartVideo;
        mSrcStateCB(mClientContext, kSrcPPVStartVideo); //we can think of starting "play" only after its being started in controller.
        break;

    case kPpvVideoStop :            //PPV event has stop streaming
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Got callback from PPV Manager!.Stop Video streaming!");
        mPPVState = kPPVStopVideo;
        mSrcStateCB(mClientContext, kSrcPPVStopVideo);
        break;

    case kPpvInterstitialStart:
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Got callback from PPV Manager!.Starting of Interstitial Video!");
        mPPVState = kPPVInterstitialStart;
        mSrcStateCB(mClientContext, kSrcPPVInterstitialStart);

        break;

    case kPpvInterstitialStop:
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Got callback from PPV Manager!.Stopping of Interstitial Video!");
        mPPVState = kPPVInterstitialStop;
        mSrcStateCB(mClientContext, kSrcPPVInterstitialStop);
        break;

    case kPpvSubscriptionAuthorized:
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Got callback from PPV Manager!.User got PPV authorized!");
        mPPVState = kPPVSubscriptionAuthorized;
        mSrcStateCB(mClientContext, kSrcPPVSubscriptionAuthorized);
        break;

    case kPpvSubscriptionExpired:
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Got callback from PPV Manager!.User's PPV authorization expired!");
        mPPVState = kPPVSubscriptionExpired;
        mSrcStateCB(mClientContext, kSrcPPVSubscriptionExpired);
        break;

    case kPpvContentNotFound:
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Got callback from PPV Manager!.No content found for the specified PPV channel!");
        mPPVState = kPPVContentNotFound;
        mSrcStateCB(mClientContext, kSrcPPVContentNotFound);
        break;

    default:
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Unknown callback from PPV Manager! Code : %d", ppvstate);
        break;
    }

}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
void MSPPPVSource::PPVMgrClassCallback(void *aUserData, void *ppvData, ePpvStateEnum ppvstate)
{

    FNLOG(DL_MSP_MPLAYER);
    MSPPPVSource *inst = (MSPPPVSource *)aUserData;

    pthread_mutex_lock(&mControllerListMutex);
    std::list<void *>::iterator it;
    bool found = false;

    // Ensure the controller is still active (not deleted) before making callback
    for (it = mActiveControllerList.begin(); it != mActiveControllerList.end(); ++it)
    {
        if (*it == inst)
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_SIGNIFICANT_EVENT, "Warning: MSPPPVSource %p no longer active", inst);
    }
    else if ((inst != NULL) && (inst->mSrcStateCB != NULL) && (inst->mClientContext != NULL))
    {
        inst->PPVMgrInstanceCallback(ppvData, ppvstate);
    }
    else
    {
        if (inst == NULL)
        {
            dlog(DL_MSP_PPV, DLOGL_ERROR, "At PPV callback: Warning:PPV source instance is NULL!!!");
        }
        else
        {
            if (inst->mSrcStateCB == NULL)
            {
                dlog(DL_MSP_PPV, DLOGL_ERROR, "At PPV callback: Warning:Controller callback function pointer is NULL!!!");
            }
            if (inst->mClientContext == NULL)
            {
                dlog(DL_MSP_PPV, DLOGL_ERROR, "At PPV callback: Warning:Controller context pointer is NULL!!!");
            }
        }
    }

    pthread_mutex_unlock(&mControllerListMutex);
}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
eMspStatus MSPPPVSource::load(SourceStateCallback aSrcStateCB, void* aClientContext)
{
    FNLOG(DL_MSP_MPLAYER);

    unsigned int pos = mSrcUrl.find("sappv://");
    std::string channelStr;

    if (pos != std::string::npos)
    {

        channelStr = mSrcUrl.substr(pos + (strlen("sappv://")));
        mSrcStateCB = aSrcStateCB;
        mClientContext = aClientContext;
        mClmChannel  = atoi(channelStr.c_str());
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Unsupported mSrcUrl.. \n");
        return kMspStatus_Error;
    }


    return kMspStatus_Ok;
}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
eMspStatus MSPPPVSource::open(eResMonPriority tunerPriority)
{
    ChannelList *channelList = NULL;
    eChannel_Status chanStatus;
    ePpvResult ppv_status = kPpvResult_Ok;
    int chanType;
    time_t now;

    (void) tunerPriority;  // not used - always open with kRMPriority_PpvVideoPlayOrRecording

    FNLOG(DL_MSP_MPLAYER);

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Current state of PPV is %d", mPPVState);

    if (mPPVState == kPPVIdle)
    {
        mPPVState = kPPVLoading;
        ppv_status = RegisterTuningInfoCallback(PPVMgrClassCallback, mClmChannel, (void *)this, &ppvEvent);
        if (ppv_status == kPpvResult_Ok)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Registration with PPV Manager Successful!!!");
            return kMspStatus_Loading;
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Unable to register with PPV Manager,error code = %d", ppv_status);
            return kMspStatus_Error;
        }
    }

    if (mPPVState == kPPVLoading)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "We havent got any event till now so returning Loading");
        return kMspStatus_Loading;
    }

    if ((mPPVState == kPPVStartVideo) || (mPPVState == kPPVInterstitialStart)) //play the channel for normal video or interstitial video
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Getting channel info to play video/interstitial");

        time(&now);

        mTuneParamSet = false;  // we only get sourceid and service id in here

        chanStatus = (eChannel_Status)Ppv_Channel_GetInt(channelList, mClmChannel, now, (ePpvChannelAttributeInt)kPpvSamServiceId, &mServiceId, ppvEvent);
        if (chanStatus != kChannel_OK)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Could not get Service ID. error: %d", chanStatus);
            return kMspStatus_Error;
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:%d PPV Server returned serviceid %d", __FUNCTION__, __LINE__, mServiceId);
        }

        chanStatus = (eChannel_Status)Ppv_Channel_GetInt(channelList, mClmChannel, now, (ePpvChannelAttributeInt)kServiceParameterInt, &mSourceId, ppvEvent);
        if (chanStatus != kChannel_OK)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Could not get channel sourceId. error: %d", chanStatus);
            return kMspStatus_Error;
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:%d PPV Server returned sourceid %d", __FUNCTION__, __LINE__, mSourceId);
        }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// This call SHOULD return kChannelType_SDV for sdv channels and kChannelType_Video for normal channels.
//   this depends on the SAM service being set up correctly on the headend
        chanStatus = Csci_Clm_ServiceGetInt("", mServiceId, kChannelType, &chanType);
        if (chanStatus != kChannel_OK)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Could not get channeltype from CLM. Error: %d", chanStatus);
            return kMspStatus_Error;
        }

        sdvSource = false;

        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:%d CLM returned chantype %d for serviceid %d", __FUNCTION__, __LINE__, chanType, mServiceId);
        if (chanType == kChannelType_SDV)
        {
            sdvSource = true;
        }

        if (!sdvSource)
        {
            // network id is currently ignored, which is OK since we don't have it here anyway
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
            chanStatus = Csci_Clm_ServiceGetInt("", mServiceId, kFrequencyInt, (int *)&mTuningParams.frequencyHz);
            if (chanStatus != kChannel_OK)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Could not get frequency. error: %d", chanStatus);
                return kMspStatus_Error;
            }
            else
            {
                mTuningParams.frequencyHz = mTuningParams.frequencyHz * (1000 * 1000);
                dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:%d CLM returned frequency %d for serviceid %d", __FUNCTION__, __LINE__, mTuningParams.frequencyHz, mServiceId);
            }

            chanStatus = Csci_Clm_ServiceGetInt("", mServiceId, kSymbolRateInt, (int *)&mTuningParams.symbolRate);
            if (chanStatus != kChannel_OK)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Could not get symbol rate. error: %d", chanStatus);
                return kMspStatus_Error;
            }
            else
            {
                dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:%d CLM returned symbolrate %d for serviceid %d", __FUNCTION__, __LINE__, mTuningParams.symbolRate, mServiceId);
            }

            chanStatus = Csci_Clm_ServiceGetInt("", mServiceId, kModulationTypeInt, reinterpret_cast<int *>(&mTuningParams.mode));
            if (chanStatus != kChannel_OK)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Could not get modulation type. error: %d", chanStatus);
                return kMspStatus_Error;
            }
            else
            {
                dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:%d CLM returned modulationtype %d for serviceid %d", __FUNCTION__, __LINE__, mTuningParams.mode, mServiceId);
            }
#endif

            chanStatus = Csci_Clm_ServiceGetInt("", mServiceId, kMPEGTSProgramNumberInt, &mProgramNumber);
            if (chanStatus != kChannel_OK)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Could not get mpeg prog number. error: %d", chanStatus);
                return kMspStatus_Error;
            }
            else
            {
                dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:%d CLM returned prog. number %d for serviceid %d", __FUNCTION__, __LINE__, mProgramNumber, mServiceId);
            }
            mTuneParamSet = true;
        }

        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "PPV sourceId :%d serviceId %d issdv %d tunset %d", mSourceId, mServiceId, sdvSource, mTuneParamSet);

        return MSPRFSource::open(kRMPriority_PpvVideoPlayOrRecording); // finally
    }
    else if (mPPVState == kPPVContentNotFound) //Do we need to return different kind of error code??,kMspStatus_unavailable ???
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "no content found");
        return kMspStatus_Error;
    }
    else
    {
        return kMspStatus_Error;
    }

    return kMspStatus_Ok;
}




void MSPPPVSource::setTunerPriority(eResMonPriority pri)
{
    eResMonPriority priority;

    if (kRMPriority_PpvVideoPlayOrRecording < pri)
    {
        priority = kRMPriority_PpvVideoPlayOrRecording;
    }
    else
    {
        priority = pri;
    }

    dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "PPV setPriority %d", priority);

    MSPRFSource::setTunerPriority(priority);
}

bool MSPPPVSource::isPPV(void)
{
    return true;
}

bool MSPPPVSource::isQamrf(void)
{
    return false;
}

bool MSPPPVSource::isAnalogSource()const
{
    return false;
}


