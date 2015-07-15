#include <time.h>
#include "MSPRFSource.h"
#include <cpe_error.h>
#include <sstream>
#include <string>
#include <sys/time.h>
#include <assert.h>
#include "MspCommon.h"
#include "csci-base64util-api.h"

#ifdef LOG
#error  LOG already defined
#endif
#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"MSPRFSource(%p):%s:%d " msg, this, __FUNCTION__, __LINE__, ##args);

#define LOG2(level, msg, args...) dlog(DL_MSP_MPLAYER, level,"MSPRFSource:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

using namespace std;

#define DEBUG_REBOOT(msg)

#define REQUIRED_FIELD_IN_QAMRF_URL_MASK_CLEAR      0x0000000f
#define REQUIRED_FIELD_IN_QAMRF_URL_MASK_ENCRYPTED  0x0000001f
#define FREQUENCY_MASK                    0x00000001
#define SYMBOL_RATE_MASK                  0x00000002
#define PROGRAM_NUMBER_MASK               0x00000004
#define MODULATION_TYPE_MASK              0x00000008
#define CA_KEY_MASK                       0x00000010

#define MAX_QAM_FREQUENCY  999000000
#define MIN_QAM_FREQUENCY   57000000
#define MAX_UINT16_VALUE   65535

const char* gEqual          = "=";
const char* gDelimeter      = "&";
const char* gFrequencyHz    = "FrequencyHz=";
const char* gSymbolRate     = "SymbolRate=";
const char* gProgramNumber  = "ProgramNumber=";
const char* gModulationType = "ModulationType=";
const char* gCaKey          = "CAKey=";


eMspStatus MSPRFSource::updateTuningParams(const char *url)
{

    FNLOG(DL_MSP_MPLAYER);
    mSrcUrl = url;

    //Release SDV connection of previous channel,if its unreleased.
    if (mConnectionId)
    {
        Csci_Sdvm_Release(&mConnectionId);
        mConnectionId = 0;
        mState = kSdvIdle;
    }

    int pos = -1;

    if ((pos = mSrcUrl.find("sctetv")) == 0)
    {
        std::string channelStr = mSrcUrl.substr(strlen("sctetv://"));
        if (channelStr.length())
        {
            mClmChannel = atoi(channelStr.c_str());
        }
    }
    else if ((pos = mSrcUrl.find("avfs://item=live/sctetv://")) == 0)
    {
        std::string channelStr = mSrcUrl.substr(strlen("avfs://item=live/sctetv://"));
        if (channelStr.length())
        {
            mClmChannel = atoi(channelStr.c_str());
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "MSPRFSource::updateTuningParams failed - invlid URL %s\n", mSrcUrl.c_str());
        return kMspStatus_BadParameters;
    }

    mChannelList = CLM_GetChannelList("rf:");
    mTuneParamSet = false;  // need new tuning params

    return kMspStatus_Ok;
}

MSPRFSource::MSPRFSource(std::string aSrcUrl, Channel clmChannel, ChannelList *channelList, IMediaPlayerSession *pIMediaPlayerSession)
{
    LOG(DLOGL_NOISE, "aSrcUrl: %s  clmChannel: %d", aSrcUrl.c_str(), clmChannel);

    initData();

    mSrcUrl         = aSrcUrl;
    mClmChannel     = clmChannel;
    mChannelList    = channelList;
    mpIMediaplayerSession = pIMediaPlayerSession;

    if (!mChannelList)
    {
        LOG(DLOGL_NOISE, "RFSource created with Null channelList.  Not an error.");
        DEBUG_REBOOT("null mChannelList");
    }
}


MSPRFSource::MSPRFSource(tCpeSrcRFTune tuningParams,  int programNumber)
{
    LOG(DLOGL_NOISE, "ctor tuningParams and programNumber 0x%x supplied", programNumber);

    initData();

    mTuningParams =  tuningParams;
    mProgramNumber = programNumber;

    mTuneParamSet = true;     // tuning params acquired in constructor

}


MSPRFSource::~MSPRFSource()
{
    mState = kSdvIdle;

    shutdown();

    if (m_bTunerAccessRequested)
    {
        _cancelTunerAccess();
    }

}


void MSPRFSource::initData()
{
    // set from constructor args
    mSrcUrl.clear();
    mClmChannel     = 0;
    mChannelList    = NULL;

    // tuning params
    mTuningParams.mode = eCpeSrcRFMode_QAM256;
    mTuningParams.frequencyHz = 0;     // can be removed for speed - mTuneParamSet qualifies
    mTuningParams.symbolRate  = 0;     // can be removed for speed - mTuneParamSet qualifies

    // sdv params
    mState = kSdvIdle;
    mServiceId = 0;
    mConnectionId = 0;
    sdvSource = false;
    qamrfSource = false;

    musicSource = false;

    mProgramNumber = 0;
    mSourceId = 0;
    mCpeSrcHandle = 0;

    mSrcCbId = 0;
    mSrcStateCB = NULL;
    mClientContext = NULL;

    mTuneParamSet = false;     // tuning params acquired
    mOpened       = false;           // cpe_src_Open called succesfully
    mCallbackSet  = false;      // cpe_src_RegisterCallback called succesfully
    mStarted      = false;          // cpe_src_cpe_src_Start called

    m_bTunerAccessGranted = false;
    m_bTunerAccessRequested = false;

    m_oResMonClient.registerCallback(resouceManagerClassCallback, (void *)this);

    //Added to handle power key encrypted sessions
    mpPwrKeySessHandler = NULL;
    m_bPowerKeySession = false;

}


void MSPRFSource::shutdown()
{
    FNLOG(DL_MSP_MPLAYER);

    // stop source if started
    if (mStarted)
    {
        logTuningParams(DLOGL_NOISE, "close src");
        int cpeErr = cpe_src_Stop(mCpeSrcHandle);
        if (!cpeErr)
        {
            LOG(DLOGL_REALLY_NOISY, "CPE Source stop success for source handle %p", mCpeSrcHandle);
            mStarted = false;
        }
        else
        {
            LOG(DLOGL_ERROR, "CPE Source stop failed for source handle %p with error code %d", mCpeSrcHandle, cpeErr);
        }
    }

    // unregister callback if callback registered
    if (mCallbackSet)
    {
        int cpeErr = cpe_src_UnregisterCallback(mCpeSrcHandle, mSrcCbId);
        if (!cpeErr)
        {
            LOG(DLOGL_REALLY_NOISY, "CPE source unregister callback success for source handle %p", mCpeSrcHandle);
            mCallbackSet = false;
            mSrcCbId = 0;           // can be removed for speeed
        }
        else
        {
            LOG(DLOGL_ERROR, "CPE source unregister callback failed for source handle %p with error code  %d", mCpeSrcHandle, cpeErr);
        }
    }

    // close source if opened
    if (mOpened)
    {
        int cpeErr = cpe_src_Close(mCpeSrcHandle);
        if (!cpeErr || cpeErr == kCpe_InvalidHandleErr)
        {
            LOG(DLOGL_REALLY_NOISY, "CPE source close success for source handle %p", mCpeSrcHandle);
        }
        else if (cpeErr == kCpe_InUseErr)
        {
            LOG(DLOGL_ERROR, "CPE source close failed for source handle %p with kCpe_InUseErr", mCpeSrcHandle);
        }
        else
        {
            LOG(DLOGL_ERROR, "CPE source close failed for source handle %p with error code  %d", mCpeSrcHandle, cpeErr);
        }

        mOpened = false;
        mCpeSrcHandle = 0;    // can be removed for speed
    }


    if ((mConnectionId) && (mState != kSdvLoading))
    {
        Csci_Sdvm_Release(&mConnectionId);
        mState = kSdvIdle;
        mConnectionId = 0;
    }


    if (m_bTunerAccessGranted)
    {
        _releaseTunerAccess();
    }

    if (NULL != mpPwrKeySessHandler)
    {
        mpPwrKeySessHandler->CiscoCak_SessionFinalize();
        delete mpPwrKeySessHandler;
        mpPwrKeySessHandler = NULL;
    }
    mpIMediaplayerSession = NULL;
}



eMspStatus MSPRFSource::load(SourceStateCallback aSrcReadyCB, void* aClientContext)
{
    eMspStatus status;
    eChannel_Status chanStatus;
    time_t now;
    int channelType;

    // load -  saves callback parameters
    //         gets tuning parameters

    FNLOG(DL_MSP_MPLAYER);

    // save callback parameters
    mSrcStateCB     = aSrcReadyCB;
    mClientContext  = aClientContext;

    status = kMspStatus_Ok;  // assume success
    qamrfSource = false;

    if (mSrcUrl.find(QAMRF_SOURCE_URI_PREFIX) == 0)
    {
        if (parseQamrfSourceUrl(mSrcUrl) == kMspStatus_Ok)
        {
            qamrfSource = true;
            mTuneParamSet = true;     // tuning params acquired in constructor
        }
        else
        {
            status = kMspStatus_BadParameters;
            LOG(DLOGL_ERROR, "parsing failed for:%s", mSrcUrl.c_str());
            return status;
        }
    }
    else
    {
        if (mTuneParamSet && !mChannelList)
        {
            LOG(DLOGL_SIGNIFICANT_EVENT, "tuning parameters set in constructor");
        }
        else
        {
            if (mTuneParamSet)
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "warning: mTuneParamSet is already set - will reaquire for url %s", mSrcUrl.c_str());
            }

            time(&now);
            chanStatus = Channel_GetInt(mChannelList, mClmChannel, now, kChannelType, &channelType);
            LOG(DLOGL_REALLY_NOISY, "%s:%d CLM Channel type returns %d", __FUNCTION__, __LINE__, channelType);

            sdvSource = false;
            musicSource = false;

            switch (channelType)
            {
            case kChannelType_SDV:
                sdvSource = true;
                break;
            case kChannelType_Music:
                musicSource = true;
                break;
            }

            chanStatus = Channel_GetInt(mChannelList, mClmChannel, now, kServiceParameterInt, &mSourceId);
            if (chanStatus != kChannel_OK)
            {
                status = kMspStatus_BadParameters;
                mState = kSdvBadParams;
            }
            chanStatus = Channel_GetInt(mChannelList, mClmChannel, now, kSamServiceId, &mServiceId);
            if (chanStatus != kChannel_OK)
            {
                status = kMspStatus_BadParameters;
                mState = kSdvBadParams;
            }
        }
    }

    return status;
}


eMspStatus MSPRFSource::getTuningParamsFromChannelList()
{
    eMspStatus status;  // status for return value
    time_t now;

    FNLOG(DL_MSP_MPLAYER);
    time(&now);

    mTuneParamSet = true;   // assume good and set false if problem getting

    // Get MPEG-TS program number - note: mChannelList and mClmChannel set in constructor
    eChannel_Status channelStatus = Channel_GetInt(mChannelList, mClmChannel, now, kMPEGTSProgramNumberInt, &mProgramNumber);
    if (channelStatus != kChannel_OK)
    {
        LOG(DLOGL_ERROR, "error getting kMPEGTSProgramNumberInt");
        mTuneParamSet = false;
    }

    // Get source ID        TODO:  move this to load since we do it for both RF and SDV source
    channelStatus = Channel_GetInt(mChannelList, mClmChannel, now, kServiceParameterInt, &mSourceId);
    if (channelStatus != kChannel_OK)
    {
        LOG(DLOGL_ERROR, "error getting kServiceParameterInt");
        mTuneParamSet = false;
    }

    // Get frequency
    channelStatus = Channel_GetInt(mChannelList, mClmChannel, now, kFrequencyInt, (int32_t *)&mTuningParams.frequencyHz);
    if (channelStatus == kChannel_OK)
    {
        mTuningParams.frequencyHz = mTuningParams.frequencyHz * (1000 * 1000);
    }
    else
    {
        LOG(DLOGL_ERROR, "error getting kFrequencyInt");
        mTuneParamSet = false;
    }

    // Get symbol rate
    channelStatus = Channel_GetInt(mChannelList, mClmChannel, now, kSymbolRateInt, (int32_t *)&mTuningParams.symbolRate);
    if (channelStatus != kChannel_OK)
    {
        LOG(DLOGL_ERROR, "error getting kSymbolRateInt");
        mTuneParamSet = false;
    }
    // Get mode(modulation type) for ananlog chan
    if (mTuningParams.symbolRate == 0)
    {
        channelStatus = Channel_GetInt(mChannelList, mClmChannel, now, kModulationTypeInt, (int32_t *)&mTuningParams.mode);
        if (channelStatus != kChannel_OK)
        {
            LOG(DLOGL_ERROR, "error kModulationTypeInt");
            mTuneParamSet = false;
        }
    }
    else
        mTuningParams.mode = eCpeSrcRFMode_QAM256;

    if (mTuneParamSet)
    {
        LOG(DLOGL_REALLY_NOISY, "tuning params acquired");
        status = kMspStatus_Ok;
    }
    else
    {
        status = kMspStatus_BadParameters;
    }

    return status;
}


eMspStatus MSPRFSource::getTuningParamsFromSDV()
{
    eSdvmStatus sdvmStatus;

    uint32_t countAttribTagValue = 0;
    tSdvmServiceAttribTagValue pAttribTagValue ;
    // Both prioity values (PPV/VOD, Play/Recording) are same.

    FNLOG(DL_MSP_MPLAYER);

    switch (mCurrentPriority)
    {
    case kRMPriority_PpvVideoPlayOrRecording :
        pAttribTagValue.tag = kSdvmService_IsPpv;
        pAttribTagValue.value = false ;
        countAttribTagValue++;
        dlog(DL_MSP_SDV, DLOGL_NOISE, " kRMPriority_PpvVideoPlayOrRecording / VodVideoPlayOrRecording Priority Set to SDV Acquire ");
        break;

    case kRMPriority_BackgroundRecording :
        pAttribTagValue.tag = kSdvmService_IsRecording;
        pAttribTagValue.value = false;
        countAttribTagValue++;
        dlog(DL_MSP_SDV, DLOGL_NOISE, " kRMPriority_BackgroundRecording Priority Set to SDV Acquire ");
        break;

    default :

        dlog(DL_MSP_SDV, DLOGL_NOISE, " No Priority Set to SDV Acquire ");

    }

    LOG(DLOGL_NOISE, "mState: %d", mState);

    if (mState == kSdvIdle)
    {
        mState = kSdvLoading;
        LOG(DLOGL_NOISE, "Acquire clmChannel %d Sourceid %d serviceid %d", mClmChannel, mSourceId, mServiceId);
        if ((Csci_Sdvm_AcquireExt(mClmChannel, mSourceId, mServiceId, (Sdvm_Tuning_ParameterUpateCallback)SDVCallbackFunction, (void *)this, countAttribTagValue, mpIMediaplayerSession, (tSdvmServiceAttribTagValue *)&pAttribTagValue, &mConnectionId)) != kSdvm_Ok)
        {
            LOG(DLOGL_ERROR, "Csci_Sdvm_Acquire Error");
            mState = kSdvUnavailable;
            return kMspStatus_SdvError;
        }
        else
        {
            LOG(DLOGL_NOISE, "mState: kMspStatus_Loading");
            return kMspStatus_Loading;
        }
    }
    else if (mState == kSdvUnavailable || mState == kSdvCancelled)
    {
        return kMspStatus_SdvError;
    }
    else if (mState == kSdvLoading || mState == kSdvInDiscovery)
    {
        return kMspStatus_Loading;
    }
    else if (mState == kSdvKnown || mState == kSdvChanged)
    {
        sdvmStatus = Csci_Sdvm_GetTunerInt(mConnectionId, kTsid, &mProgramNumber);
        if (sdvmStatus != kSdvm_Ok)
        {
            LOG(DLOGL_ERROR, "SDV Error getting programNumber. error: %d", sdvmStatus);
            return kMspStatus_SdvError;
        }
        sdvmStatus = Csci_Sdvm_GetTunerInt(mConnectionId, kFrequencyint, (int32_t *)&mTuningParams.frequencyHz);
        if (sdvmStatus != kSdvm_Ok)
        {
            LOG(DLOGL_ERROR, "SDV Error getting frequency. error: %d", sdvmStatus);
            return kMspStatus_SdvError;
        }

        mTuningParams.frequencyHz = mTuningParams.frequencyHz * (1000 * 1000);
        sdvmStatus = Csci_Sdvm_GetTunerInt(mConnectionId, kSymbolRateint, (int32_t *)&mTuningParams.symbolRate);
        if (sdvmStatus != kSdvm_Ok)
        {
            LOG(DLOGL_ERROR, "SDV Error getting symbolrate. error: %d\n", sdvmStatus);
            return kMspStatus_SdvError;
        }
        mTuneParamSet = true;
        return kMspStatus_Ok;
    }
    else
    {
        return kMspStatus_SdvError;
    }

    return kMspStatus_Ok;
}

void MSPRFSource::RFAnalogCallbackFunction(tCpeSrcCallbackTypes aType, void *aUserData, void *pCallbackSpecific)
{
    FNLOG(DL_MSP_MPLAYER);
    aType = aType;
    pCallbackSpecific = pCallbackSpecific;
    MSPRFSource *inst = (MSPRFSource *)aUserData;

    LOG2(DLOGL_NORMAL, "CB type %d, lockstate %d", aType, *(bool *)pCallbackSpecific);

    if (aType == eCpeSrcCallbackTypes_LockedStateCallback)
    {
        bool *lockState = (bool *) pCallbackSpecific;   // true indicates tuner is locked

        if (*lockState)
        {
            LOG2(DLOGL_NOISE, "Analog Tuner Locked");
            if (inst)
            {
                if (inst->mSrcStateCB)
                {
                    inst->mSrcStateCB(inst->mClientContext, kAnalogSrcTunerLocked);
                }
            }
        }
    }
}

void MSPRFSource::logTuningParams(int logLevel, const char *msg)
{
    dlog(DL_MSP_MPLAYER, logLevel, "%s ", msg);
    dlog(DL_MSP_MPLAYER, logLevel, "srcHandle:  %p ", mCpeSrcHandle);
    dlog(DL_MSP_MPLAYER, logLevel, "channel:    %d ", mClmChannel);
    dlog(DL_MSP_MPLAYER, logLevel, "params set: %s ", mTuneParamSet ? "Yes" : "No");
    dlog(DL_MSP_MPLAYER, logLevel, "programNum: %d ", mProgramNumber);
    dlog(DL_MSP_MPLAYER, logLevel, "sourceId:   %d ", mSourceId);
    dlog(DL_MSP_MPLAYER, logLevel, "mode:       %d ", mTuningParams.mode);
    dlog(DL_MSP_MPLAYER, logLevel, "freq:       %d ", mTuningParams.frequencyHz);
    dlog(DL_MSP_MPLAYER, logLevel, "rate:       %d ", mTuningParams.symbolRate);
}


// open - request RF tuner source,
//        set tuning parameters
//        set callback waiting for tuner lock

eMspStatus MSPRFSource::open(eResMonPriority tunerPriority)
{
    eMspStatus status = kMspStatus_Ok;
    eResMonStatus tunerStatus;
    int cpeErr = -1;

    FNLOG(DL_MSP_MPLAYER);

    LOG(DLOGL_REALLY_NOISY, "mTuneParamSet: %d  tunerPriority: %d", mTuneParamSet, tunerPriority);

    mCurrentPriority = tunerPriority;

    // get tuning parameters
    if (!mTuneParamSet)
    {
        if (sdvSource)
        {
            // TODO: Should behavior be differnt is SDV is loading or not available?
            status = getTuningParamsFromSDV();
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "warning unable to get tuning params from SDV");
                return status;
            }
        }
        else
        {
            status = getTuningParamsFromChannelList();
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "warning unable to get tuning params from CNL");
                return status;
            }
        }
    }



    if (!m_bTunerAccessRequested)
    {
        LOG(DLOGL_REALLY_NOISY, "request tuner access with priority %d ", tunerPriority);
        tunerStatus = _requestTunerAccess(tunerPriority);
        if (tunerStatus != kResMon_Ok)
        {
            LOG(DLOGL_ERROR, "request for tuner failed");
            return kMspStatus_Error;
        }
        else
        {
            LOG(DLOGL_NOISE, "request for tuner success");
            return kMspStatus_WaitForTuner;
        }
    }

    if (!m_bTunerAccessGranted)
    {
        LOG(DLOGL_NOISE, "tuner access not granted yet");
        return kMspStatus_WaitForTuner;
    }


    if (mStarted)
    {
        cpeErr = cpe_src_Stop(mCpeSrcHandle);
        if (cpeErr)
        {
            LOG(DLOGL_ERROR, "CPE source stop fails,on attempting to reusing the tuner with error code %d", cpeErr);
            return kMspStatus_BadSource;
        }
        mStarted = false;
    }


    if (!mOpened)
    {

        // Open RF tuner source and get source handle
        cpeErr = cpe_src_Open(eCpeSrcType_RFTunerSource, &mCpeSrcHandle);
        if (!cpeErr)
        {
            LOG(DLOGL_NOISE, " CPE RF source open success with handle %p", mCpeSrcHandle);
            mOpened = true;
        }
        else
        {
            LOG(DLOGL_ERROR, " CPE RF source open failed with  error code %d", cpeErr);
            _releaseTunerAccess();
            m_oResMonClient.unregisterCallback(resouceManagerClassCallback, (void *)this);
        }

        // Register callback
        if (!cpeErr)
        {
            LOG(DLOGL_REALLY_NOISY, "register callback");

            if (mTuningParams.mode != eCpeSrcRFMode_Analog)
            {
                cpeErr = cpe_src_RegisterCallback(eCpeSrcCallbackTypes_LockedStateCallback, (void *)this, (tCpeSrcCallbackFunction)RFCallbackFunction, &mSrcCbId, mCpeSrcHandle, NULL);
                LOG(DLOGL_REALLY_NOISY, "registered Digital RF CB");
            }
            else
            {
                cpeErr = cpe_src_RegisterCallback(eCpeSrcCallbackTypes_LockedStateCallback, (void *)this, (tCpeSrcCallbackFunction)RFAnalogCallbackFunction, &mSrcCbId, mCpeSrcHandle, NULL);
                LOG(DLOGL_REALLY_NOISY, "registered ANALOG RF CB");
            }

            if (!cpeErr)
            {
                LOG(DLOGL_REALLY_NOISY, "CPE RF source register callback for tuner lock state success");
                mCallbackSet = true;
            }
            else
            {
                LOG(DLOGL_ERROR, "CPE RF source register callback for tuner lock state with handle %p failed with error code %d", mCpeSrcHandle, cpeErr);
            }
        }
    }
    // Set tuning params
    if (!cpeErr)
    {
        logTuningParams(DLOGL_NOISE, "open src");
        cpeErr = cpe_src_Set(mCpeSrcHandle, eCpeSrcNames_RFTune, &mTuningParams);
        if (cpeErr)
        {
            LOG(DLOGL_ERROR, "CPE source set for tuning parameters with source handle %p failed with error code  %d", mCpeSrcHandle, cpeErr);
        }
    }

    if (!cpeErr)
    {
        // all is good!
        LOG(DLOGL_REALLY_NOISY, "param values mTuneParamSet = %d ,mOpened = %d , mCallbackSet = %d ", mTuneParamSet, mOpened, mCallbackSet);
        status = kMspStatus_Ok;
        LOG(DLOGL_MINOR_EVENT, "tuning requested and lock callback set")
    }
    else
    {
        status = kMspStatus_BadSource;
        shutdown();
    }

    return status;
}


eMspStatus MSPRFSource::start()
{
    eMspStatus status = kMspStatus_Ok;

    FNLOG(DL_MSP_MPLAYER);

    if (!mCallbackSet || !mOpened || !mCpeSrcHandle)
    {
        LOG(DLOGL_ERROR, "%s:%d Error: source not open correctly cb:%d op:%d src:%p", __FUNCTION__, __LINE__, mCallbackSet, mOpened, mCpeSrcHandle);
        return kMspStatus_StateError;
    }

    if (!mStarted)
    {
        int cpeErr = cpe_src_Start(mCpeSrcHandle);
        if (!cpeErr)
        {
            LOG(DLOGL_REALLY_NOISY, "CPE RF source start success with source handle %p", mCpeSrcHandle);
            mStarted = true;
            if (m_bPowerKeySession)
            {
                //Copy Decoded EMM Bytes into an Array of unsigned Characters
                uint8_t decodedEmmBytes[CISCO_CAK_KEY_SIZE];
                memcpy(decodedEmmBytes, mDecodedEmmString.c_str(), CISCO_CAK_KEY_SIZE);

                //Passing decodedEmmBytes to powerKeyHandler module
                mpPwrKeySessHandler = new CiscoCakSessionHandler(decodedEmmBytes);
                if (mpPwrKeySessHandler)
                {
                    status = mpPwrKeySessHandler->CiscoCak_SessionInitialize();
                    if (kMspStatus_Ok != status)
                    {
                        LOG(DLOGL_ERROR, "Error: CiscoCak_SessionInitialize  FAILED !!! status = %d", status);
                    }
                }
                else  //Memory Allocation Failed
                {
                    LOG(DLOGL_ERROR, "Error: CiscoCakSessionHandler class allocation  FAILED !!!");
                    status = kMspStatus_OutofMemory;
                }
            }
        }
        else   //cpeErr
        {
            LOG(DLOGL_ERROR, "cpe_src_start error %d", cpeErr);
            shutdown();
            status =  kMspStatus_BadSource;
        }
    }

    return status;
}


eMspStatus MSPRFSource::stop()
{
    FNLOG(DL_MSP_MPLAYER);
    shutdown();

    return kMspStatus_Ok;
}


eMspStatus MSPRFSource::release()
{
    FNLOG(DL_MSP_MPLAYER);
    _cancelTunerAccess();
    return kMspStatus_Ok;
}




void MSPRFSource::RFCallbackFunction(tCpeSrcCallbackTypes aType, void *aUserData, void *pCallbackSpecific)
{
    FNLOG(DL_MSP_MPLAYER);

    if (aType == eCpeSrcCallbackTypes_LockedStateCallback)
    {
        bool *lockState = (bool *) pCallbackSpecific;   // true indicates tuner is locked

        if (*lockState)
        {
            LOG2(DLOGL_NOISE, "Tuner Locked");
            MSPRFSource *inst = (MSPRFSource *)aUserData;
            if (inst)
            {
                if (inst->mSrcStateCB)
                {
                    inst->mSrcStateCB(inst->mClientContext, kSrcTunerLocked);
                }
                else
                {
                    LOG2(DLOGL_ERROR, "Error: null inst->mSrcStateCB - no callback");
                }
            }
            else
            {
                LOG2(DLOGL_ERROR, "Error: null inst - no callback");
            }
        }
        else
        {
            // TODO: Should action be taken in response to unlock event?
            //Sending the unlocked event so that the client streaming session is aware of the error and pauses till recovery.
            LOG2(DLOGL_NORMAL, "Tuner UnLocked");
            MSPRFSource *inst = (MSPRFSource *)aUserData;
            if (inst)
            {
                if (inst->mSrcStateCB)
                {
                    inst->mSrcStateCB(inst->mClientContext, kSrcTunerUnlocked);
                }
                else
                {
                    LOG2(DLOGL_ERROR, "Error: null inst->mSrcStateCB - no callback");
                }
            }
            else
            {
                LOG2(DLOGL_ERROR, "Error: null inst - no callback");
            }
        }
    }
    else
    {
        LOG2(DLOGL_ERROR, "Error: unexpected event: %d", aType);
    }
}


eMspStatus MSPRFSource::setSpeed(tCpePlaySpeed aPlaySpeed, uint32_t aNpt)
{
    aPlaySpeed = aPlaySpeed;
    aNpt = aNpt;
    return kMspStatus_Ok;
}

void MSPRFSource::SDVCallbackFunction(eSdvmTunerState state, void *aUserData)
{

    FNLOG(DL_MSP_MPLAYER);

    MSPRFSource *inst = (MSPRFSource *)aUserData;

    dlog(DL_MSP_SDV, DLOGL_NOISE, "SDV Callback came State is %d, context %p\n", state, inst);
    if (inst)
    {
        if (inst->mSrcStateCB)
        {

            switch (state)
            {

            case kSdvm_Known:
                inst->mState = kSdvKnown;
                inst->mSrcStateCB(inst->mClientContext, kSrcSDVKnown);
                break;

            case kSdvm_Changed:
                inst->mState = kSdvChanged;
                inst->mSrcStateCB(inst->mClientContext, kSrcSDVServiceChanged);
                break;

            case kSdvm_Canceled:
                inst->mState = kSdvCancelled;
                inst->mSrcStateCB(inst->mClientContext, kSrcSDVServiceCancelled);
                break;

            case kSdvm_Unavailable:
                inst->mState = kSdvUnavailable;
                inst->mSrcStateCB(inst->mClientContext, kSrcSDVServiceUnAvailable);
                break;
            case kSdvm_InDiscovery:
                inst->mSrcStateCB(inst->mClientContext, kSrcSDVServiceLoading);
                break;
            case kSdvm_KeepAliveNeeded:
                inst->mSrcStateCB(inst->mClientContext, kSrcSDVKeepAliveNeeded);
                break;
            default:
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "UnKnow State from SDVcallback");
                break;
            }
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, " Invalid Instace in SDVcallback: inst->mSrcStateCB %p", inst->mSrcStateCB);
        }
    }

    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, " Invalid Instace in SDVcallback: inst %p", inst);
    }

}

bool MSPRFSource::isAnalogSource()const
{
    if (mTuningParams.mode == eCpeSrcRFMode_Analog)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "MSPRFSource::%s, Analog Source", __FUNCTION__);
        return true;
    }
    else
        return false;

}

eMspStatus MSPRFSource::getPosition(float *aNptTime)
{
    *aNptTime = 0;
    return kMspStatus_Ok;
}

eMspStatus MSPRFSource::setPosition(float aNptTime)
{
    aNptTime = aNptTime;
    return kMspStatus_Ok;
}


///////////////////////////////////////////////////////////////////////
//              Following functions are to support resource manager
///////////////////////////////////////////////////////////////////////

// used to communicate current state to resource monitor
void MSPRFSource::setTunerPriority(eResMonPriority pri)
{
    FNLOG(DL_MSP_MPLAYER);

    // if we are a VOD or PPV source, don't update to new priority.  AVPM and DVR know nothing of VOD/PPV
    // they will try to reset this.  Not crazy about this -- but would require lots more code in zapper/dvr/avpm
    if ((mCurrentPriority != kRMPriority_VodVideoPlayOrRecording) && (mCurrentPriority != kRMPriority_PpvVideoPlayOrRecording))
    {
        mCurrentPriority = pri;
    }

    LOG(DLOGL_NOISE, "channel: %s  url: %d  priority: %d", mSrcUrl.c_str(), mClmChannel, (int)mCurrentPriority);

    m_oResMonClient.setPriority(mCurrentPriority);
}


// class callback function that gets the event callbacks from res mgr
// just turns the call into an instance specific call
void MSPRFSource::resouceManagerClassCallback(eResMonCallbackResult reason, void *ctx)
{
    FNLOG(DL_MSP_MPLAYER);

    MSPRFSource *inst = (MSPRFSource *)ctx;

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "reason: %d  MSPRFSource inst: %p", reason, inst);

    if (inst)
    {
        inst->resouceManagerInstanceCallback(ctx, reason);
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Error null inst for reason: %d", reason);
    }
}


// used to communicate current state to resource monitor
// called in context of ResMon thread
void MSPRFSource::resouceManagerInstanceCallback(void *ctx, eResMonCallbackResult res)
{
    FNLOG(DL_MSP_MPLAYER);

    (void)ctx;

    LOG(DLOGL_REALLY_NOISY, " RESMON callback.  Tuner state is %d", res);
    if ((mClientContext != NULL) && (mSrcStateCB != NULL))
    {
        if (res == kResMon_OkToRetry)
        {
            LOG(DLOGL_NOISE, " RESMON says.Ok to retry. Trying to re request and regain the tuner");
            m_bTunerAccessRequested = false;
            mSrcStateCB(mClientContext, kSrcTunerRegained);
        }
        else if (res == kResMon_Revoked)
        {
            LOG(DLOGL_NOISE, " RESMON says.Tuner revoked.Shutdown and Wait till RESMON gives ok to retry ");
            mSrcStateCB(mClientContext, kSrcTunerLost);
            //New connection Id is needed to communicate with sdv process, as it is set to Zero while
            //Tuner was revoked. Hence tune again on sdv channel, to establish a new connection.
            if (sdvSource)
            {
                LOG(DLOGL_NOISE, "####  Setting mTuneParamSet to False for SDV channel.####");
                mTuneParamSet = false;
            }
        }
        else if (res == kResMon_Granted)
        {
            m_bTunerAccessGranted = true;
            // signal main thread we have it
            LOG(DLOGL_NOISE, " RESMON says.Tuner granted.Retune to the video");
            mSrcStateCB(mClientContext, kSrcTunerRegained);
        }
        else if (res == kResMon_Denied)
        {
            LOG(DLOGL_NOISE, " RESMON says.Tuner denied.Wait till Ok to Retry arrives");
        }
    }
}


eResMonStatus MSPRFSource::_requestTunerAccess(eResMonPriority pri)
{
    eResMonStatus status;

    FNLOG(DL_MSP_MPLAYER);

    if (m_bTunerAccessGranted)
    {
        LOG(DLOGL_ERROR, "Error: access already granted - check logic");
        DEBUG_REBOOT("Error: access already granted");
        return kResMon_Failure;
    }

    if (m_bTunerAccessRequested)
    {
        LOG(DLOGL_ERROR, "Tuner has been requested already");
        return kResMon_Failure;
    }

    status = m_oResMonClient.requestTunerAccess(pri);
    if (status != kResMon_Ok)
    {
        LOG(DLOGL_ERROR, "Error requesting tuner access");
        return status;
    }
    m_bTunerAccessRequested = true;

    return status;
}


eResMonStatus MSPRFSource::_releaseTunerAccess()
{
    eResMonStatus status;
    FNLOG(DL_MSP_MPLAYER);

    status = m_oResMonClient.releaseTunerAccess();

    if (status != kResMon_Ok)
    {
        LOG(DLOGL_ERROR, "Error releasing tuner access");
        return status;
    }

    m_bTunerAccessGranted = false;
    return status;
}

eResMonStatus MSPRFSource::_cancelTunerAccess()
{
    eResMonStatus status;
    FNLOG(DL_MSP_MPLAYER);

    LOG(DLOGL_NOISE, "cancel tuner");
    status = m_oResMonClient.cancelTunerAccess();
    if (status != kResMon_Ok)
    {
        LOG(DLOGL_ERROR, "Error Canceling tuner access");
        return status;
    }

    m_bTunerAccessGranted = false;
    m_bTunerAccessRequested = false;
    return status;
}

bool MSPRFSource::isPPV(void)
{
    return false;
}

bool MSPRFSource::isQamrf(void)
{
    return qamrfSource;
}

bool MSPRFSource::isSDV(void)
{
    return sdvSource;
}

bool MSPRFSource::isMusic(void)
{
    return musicSource;
}

eMspStatus MSPRFSource::setSdv(bool Status, eSdvmServiceAttributeInt attribute)
{
    eSdvmStatus sdvmStatus;

    if (sdvSource && mConnectionId)
    {
        LOG(DLOGL_NORMAL, " SetSdv called SDV Src Status = %d && attribute = %d && !Status = %d", Status, attribute, !Status);

        if (Status)
        {
            sdvmStatus = Csci_Sdvm_SetServiceInt(mConnectionId, attribute, !Status);
            if (sdvmStatus != kSdvm_Ok)
            {
                LOG(DLOGL_ERROR, " Failed to set Csci_Sdvm_SetInt. error: %d", sdvmStatus);
                return kMspStatus_SdvError;
            }
        }
        else
        {
            sdvmStatus = Csci_Sdvm_SetServiceInt(mConnectionId, attribute, !Status);
            if (sdvmStatus != kSdvm_Ok)
            {
                LOG(DLOGL_ERROR, "Failed to set Csci_Sdvm_SetInt. error: %d", sdvmStatus);
                return kMspStatus_SdvError;
            }
        }

    }

    else
    {
        LOG(DLOGL_NORMAL, " SetSdv called for non SDV Src sdvSource = %d && mConnectionId = %d ", sdvSource, mConnectionId);

    }

    return kMspStatus_Ok;
}

eCsciMspDiagStatus MSPRFSource::GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo)
{
    if (msgInfo)
    {
        msgInfo->ChanNo     = mClmChannel;
        msgInfo->SourceId   = mSourceId;
        msgInfo->frequency  = mTuningParams.frequencyHz;
        msgInfo->mode       = mTuningParams.mode;
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: Null msgInfo");
        return kCsciMspDiagStat_NoData;
    }
    return kCsciMspDiagStat_OK;
}



eMspStatus MSPRFSource::setMediaSessionInstance(IMediaPlayerSession *pIMediaPlayerSession)
{
    FNLOG(DL_MSP_MPLAYER);
    eSdvmStatus sdvmStatus;
    LOG(DLOGL_NORMAL, "Session pointer to be set is %p", pIMediaPlayerSession);

    sdvmStatus = Csci_Sdvm_Set_MSP_Session_Instance(mConnectionId, pIMediaPlayerSession);
    if (sdvmStatus != kSdvm_Ok)
    {
        LOG(DLOGL_ERROR, "unable to set MSP seesion in SDV %s", eSdvmStatusString[sdvmStatus]);
        return kMspStatus_Error;
    }
    return kMspStatus_Ok;
}

eMspStatus MSPRFSource::parseTag(string strURL, const char * tag , string& value, bool isNumberString)
{
    FNLOG(DL_MSP_MPLAYER);
    eMspStatus status = kMspStatus_Error;
    const char *tagPosition, *startPosition, *endPosition = NULL;

    tagPosition = strcasestr(strURL.c_str(), tag);
    if (tagPosition != NULL)
    {
        startPosition = strcasestr(tagPosition, gEqual) + 1;
        endPosition = strcasestr(tagPosition, gDelimeter);
        value.assign(startPosition, ((endPosition) ? (endPosition - startPosition) : strlen(startPosition)));
        status = kMspStatus_Ok;

        if (isNumberString)
        {
            for (uint32_t i = 0; i < value.size(); i++)
            {
                if (!isdigit(value[i]))
                {
                    LOG(DLOGL_ERROR, " input string is not Numeric string:%s ", value.c_str());
                    status = kMspStatus_Error;
                    break;
                }
            }
        }
    }
    else
    {
        LOG(DLOGL_ERROR, " parsing failed for tag:%s in url:%s", tag, strURL.c_str());
    }
    return status;
}

eMspStatus MSPRFSource::parseQamrfSourceUrl(std::string srcUrl)
{
    FNLOG(DL_MSP_MPLAYER);
    uint32_t parsedTagsTracking = 0;
    uint32_t reqQamrfFieldMask = REQUIRED_FIELD_IN_QAMRF_URL_MASK_CLEAR;
    eMspStatus status = kMspStatus_Error;

    LOG(DLOGL_NORMAL, " sourceUri :%s", srcUrl.c_str());

    //parse only if prefix is QAMRF_SOURCE_URI_PREFIX
    if (srcUrl.find(QAMRF_SOURCE_URI_PREFIX) == 0)
    {
        string value;
        if (parseTag(srcUrl, gFrequencyHz, value, true) == kMspStatus_Ok)
        {
            mTuningParams.frequencyHz = strtoul(value.c_str(), 0, 0);
            if (mTuningParams.frequencyHz >= MIN_QAM_FREQUENCY && mTuningParams.frequencyHz <= MAX_QAM_FREQUENCY)
            {
                parsedTagsTracking |= FREQUENCY_MASK;
                LOG(DLOGL_NORMAL, " mTuningParams.frequencyHz :%d", mTuningParams.frequencyHz);
            }
            else
            {
                LOG(DLOGL_ERROR, " tuning frequency is not in range mTuningParams.frequencyHz:%d", mTuningParams.frequencyHz);
            }
        }

        //parse SYMBOL_RATE_TAG tag
        if (parseTag(srcUrl, gSymbolRate, value, true) == kMspStatus_Ok)
        {
            mTuningParams.symbolRate = strtoul(value.c_str(), 0, 0);
            if (mTuningParams.symbolRate > 0 &&  mTuningParams.symbolRate <= MAX_UINT16_VALUE)
            {
                parsedTagsTracking |= SYMBOL_RATE_MASK;
                LOG(DLOGL_NORMAL, " mTuningParams.symbolRate :%d", mTuningParams.symbolRate);
            }
            else
            {
                LOG(DLOGL_ERROR, " symbolrate is not in range mTuningParams.symbolRate:%d", mTuningParams.symbolRate);
            }
        }

        //parse MODULATION_TYPE_TAG tag
        if (parseTag(srcUrl, gModulationType, value, true) == kMspStatus_Ok)
        {
            uint16_t modeType = strtoul(value.c_str(), 0, 0);
            if (MspCommon::getCpeModeFormat(modeType, mTuningParams.mode))
            {
                parsedTagsTracking |= MODULATION_TYPE_MASK;
                LOG(DLOGL_NORMAL, " mTuningParams.mode %d", mTuningParams.mode);
            }
            else
            {
                LOG(DLOGL_ERROR, " modulation type  is not in range modeType:%d", modeType);
            }
        }

        //parse PROGRAM_NUMBER_TAG tag
        if (parseTag(srcUrl, gProgramNumber, value, true) == kMspStatus_Ok)
        {
            mProgramNumber = strtoul(value.c_str(), 0, 0);
            LOG(DLOGL_NORMAL, " mProgramNumber %d", mProgramNumber);
            if (mProgramNumber > 0 && mProgramNumber <= MAX_UINT16_VALUE)
            {
                parsedTagsTracking |= PROGRAM_NUMBER_MASK;
            }
            else
            {
                LOG(DLOGL_ERROR, " program number is not in range mProgramNumber:%d", mProgramNumber);
            }
        }

        //parse CAKEY_TAG tag
        if (parseTag(srcUrl, gCaKey, value, false) == kMspStatus_Ok)
        {
            LOG(DLOGL_NORMAL, "Encoded CAKey :%s", value.c_str());

            //Decode The EMM Key Only if it is not empty
            if (!value.empty())
            {
                reqQamrfFieldMask = REQUIRED_FIELD_IN_QAMRF_URL_MASK_ENCRYPTED;
                //Call csciBase64Decoder to decode the Encrypted string passed by UI
                eCsciBase64Status ret = Csci_Base64Util_Decode(value, mDecodedEmmString);
                if (kCsciBase64Status_Ok == ret) //In case of error returned from Csci_Base64 Decoder
                {
                    //Decoded Emm Key has Either correct size OR empty. Not a problem
                    if (mDecodedEmmString.size() == CISCO_CAK_KEY_SIZE)
                    {
                        parsedTagsTracking |= CA_KEY_MASK;
                        m_bPowerKeySession = true;
                    }
                }
                else
                {
                    LOG(DLOGL_ERROR, "Error: Base64Decoder Failed !!!. returned val = %d", ret);
                }
            }
        }

        if ((parsedTagsTracking & reqQamrfFieldMask) == reqQamrfFieldMask)
        {
            status = kMspStatus_Ok;
        }
        else
        {
            LOG(DLOGL_ERROR, "qamrf URL parse error : All required tags not present in URL ");
        }

    }
    return status;
}

#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
tCpePgrmHandle MSPRFSource::getCpeProgHandle()const
{
    return 0;
}

void MSPRFSource::SetCpeStreamingSessionID(uint32_t sessionId)
{
    (void) sessionId;
    return;
}
#endif
