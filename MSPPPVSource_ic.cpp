////////////////////////////////////////////////////////////////////////////////////////
//
//
// MSPPPVSource_ic.cpp -- Implementation file for re-factored Pay-per-View source class
//
//
//
//
//
////////////////////////////////////////////////////////////////////////////////////////

// standard includes
#include <time.h>

// local includes
#include "MSPPPVSource_ic.h"

#include <csci-clm-api.h>

// CPERP-specific includes
#include <pthread_named.h>

#define LOG(level, msg, args...)  dlog(DL_MSP_MRDVR,level,"MSPPPVSource:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

std::list<void*> MSPPPVSource::mActiveControllerList;
pthread_mutex_t  MSPPPVSource::mControllerListMutex;
bool             MSPPPVSource::mControllerListInititialized = false;

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
MSPPPVSource::MSPPPVSource(std::string aSrcUrl, IMediaPlayerSession *pIMediaPlayerSession) : MSPHTTPSource(aSrcUrl)
{
    FNLOG(DL_MSP_MPLAYER);
    UNUSED(pIMediaPlayerSession);
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
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "mSrcUrl is supported\n");
        mClmChannel  = atoi(channelStr.c_str());
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "kdbg Unsupported mSrcUrl.. \n");
        return kMspStatus_Error;
    }

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Loading HTTPSource  \n");
    mSrcStateCB = aSrcStateCB;
    mClientContext = aClientContext;
    return kMspStatus_Ok;
}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
eMspStatus MSPPPVSource::open(eResMonPriority tunerPriority)
{
    ePpvResult ppv_status = kPpvResult_Ok;

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
        if (mpSessionId == NULL)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "No valid Session Id found; Go ahead and create a new session now");
            MSPHTTPSource::load(mSrcStateCB, mClientContext);
        }
        return MSPHTTPSource::open(kRMPriority_PpvVideoPlayOrRecording); // finally
    }
    else if (mPPVState == kPPVContentNotFound) //Do we need to return different kind of error code??,kMspStatus_unavailable ???
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "no content found");
        return kMspStatus_Error;
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Just returning kMspStatus_Error");
        return kMspStatus_Error;
    }

    dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Returning kMspStatus_Ok");

    return kMspStatus_Ok;
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

