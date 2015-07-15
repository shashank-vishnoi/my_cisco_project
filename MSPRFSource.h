#ifndef  MSP_RF_SOURCE_H
#define MSP_RF_SOURCE_H

#include <string>
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#include <cpe_source.h>
#endif

#include "MSPSource.h"
#include "MspCommon.h"
#include <sail-clm-api.h>
#include "csci-sdv-api.h"

#include "MSPResMonClient.h"
#include "CiscoCakSessionHandler.h"

class MSPRFSource : public MSPSource
{
// if doing SDV, possible acquisition source states
    typedef enum
    {
        kSdvIdle,
        kSdvBadParams,
        kSdvLoading,
        kSdvKnown,
        kSdvInDiscovery,
        kSdvCancelled,
        kSdvChanged,
        kSdvUnavailable,
    } eSdvState;

protected:
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    tCpeSrcHandle mCpeSrcHandle;
#endif
    std::string mSrcUrl;
    Channel mClmChannel;
    ChannelList *mChannelList;
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    tCpeSrcRFTune mTuningParams;
#endif

    int mSourceId;
    int mProgramNumber;
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    tCpeSrcCallbackID mSrcCbId; // callback id will be set if load is successful
#endif
// SDV support
    eSdvState mState;
    int mServiceId;
    uint32_t mConnectionId;
    bool sdvSource;             // I don't like doing it this way, but it makes PPV much easier
    bool qamrfSource;

    /* SDV Response Callback */
    static void SDVCallbackFunction(eSdvmTunerState state, void *aUserData);

    bool musicSource;  // flag for Galaxy and DMX music channels

    // Higher level should enforce state
    bool mTuneParamSet;     // tuning params acquired
    bool mOpened;           // cpe_src_Open called succesfully
    bool mCallbackSet;      // cpe_src_RegisterCallback called succesfully
    bool mStarted;          // cpe_src_cpe_src_Start called

    SourceStateCallback mSrcStateCB;
    void *mClientContext;
    IMediaPlayerSession *mpIMediaplayerSession;

public:
    MSPRFSource(std::string aSrcUrl, Channel clmChannel, ChannelList *channelList, IMediaPlayerSession *pIMediaPlayerSession);
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    MSPRFSource(tCpeSrcRFTune tuningParams, int programNumber);
#endif
    virtual ~MSPRFSource();

    /*This method will extract all tuning information by using srcUrl & CLM Apis */
    eMspStatus load(SourceStateCallback aSrcReadyCB, void* aClientContext);

    /* This function open source handle & registers Source Ready Callback */
    eMspStatus open(eResMonPriority tunerPriority);

    /* Start the source */
    eMspStatus start();

    /* Stop source handle & unregisters RF Callback*/
    eMspStatus stop();

    /* Release source */
    eMspStatus release();

    /* Getter Methods for Media Controller, Display Session & PSI */
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    tCpeSrcHandle getCpeSrcHandle()const
    {
        return mCpeSrcHandle;
    }
#endif

    int getSourceId()const
    {
        return mSourceId;
    }

    int getProgramNumber()const
    {
        return mProgramNumber;
    }

    std::string getSourceUrl()const
    {
        return mSrcUrl;    // required for PIP swap
    }

    std::string getFileName()const
    {
        return mSrcUrl;
    }

    bool        isDvrSource()const
    {
        return false;
    }

    bool        canRecord()const
    {
        return true ;
    }

    bool isAnalogSource()const;

    bool isPPV(void);
    bool isQamrf(void);
    bool isSDV(void);
    bool isMusic(void);
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    eMspStatus setSpeed(tCpePlaySpeed aPlaySpeed, uint32_t aNpt);
#endif
#if PLATFORM_NAME == IP_CLIENT
    eMspStatus setSpeed(int numerator, unsigned int denominator, uint32_t aNpt)
    {
        (void)(numerator);
        (void)(denominator);
        (void)(aNpt);
        return kMspStatus_Ok;
    }
#endif

    eMspStatus getPosition(float *aNptTime);

    eMspStatus setPosition(float aNptTime);

    eMspStatus setSdv(bool Status, eSdvmServiceAttributeInt attribute);
    eMspStatus setMediaSessionInstance(IMediaPlayerSession *pIMediaPlayerSession);

#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    /* Tuner Locked Callback */
    static void RFCallbackFunction(tCpeSrcCallbackTypes aType, void *aUserData, void *pCallbackSpecific);
    static void RFAnalogCallbackFunction(tCpeSrcCallbackTypes aType, void *aUserData, void *pCallbackSpecific);
#endif

// resource/tuner management support
    void setTunerPriority(eResMonPriority pri);

    static void resouceManagerClassCallback(eResMonCallbackResult, void *ctx);
    virtual eMspStatus updateTuningParams(const char *);
    eCsciMspDiagStatus GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo);
    //This function can be used to parse qamrf:// source url.
    eMspStatus parseQamrfSourceUrl(std::string srcUrl);

#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    tCpePgrmHandle getCpeProgHandle()const;
    void SetCpeStreamingSessionID(uint32_t sessionId);
#endif

private:

    void initData();

    eMspStatus getTuningParamsFromChannelList();
    eMspStatus getTuningParamsFromSDV();
    void logTuningParams(int logLevel, const char *msg);
    void shutdown();
    //This function will be used to parse value for given tag from given source URL. If isNumberString flag passed
    //  true and if tag value is non-numeric string then it will return false.
    eMspStatus parseTag(std::string strURL, const char *tag , std::string& value, bool isNumberString);

// resource/tuner management support
    // used to communicate params to resource monitor tuner conflict resolver
    void resouceManagerInstanceCallback(void *ctx, eResMonCallbackResult res);

    // Provides connectivity to Resource Monitor (ResMon) in order to coordinate access to the tuner
    eResMonStatus _requestTunerAccess(eResMonPriority pri);
    eResMonStatus _releaseTunerAccess();
    eResMonStatus _cancelTunerAccess();

    bool m_bTunerAccessGranted;  // Maintains whether ResMon has granted access to the tuner
    bool m_bTunerAccessRequested;  //have we requested a tuner yet

    CiscoCakSessionHandler *mpPwrKeySessHandler;  //Handle PowerKey Session
    bool m_bPowerKeySession;
    std::string mDecodedEmmString;

protected:
    MSPResMonClient m_oResMonClient;
    eResMonPriority mCurrentPriority;
};

#endif // #ifndef MSP_RF_SOURCE_H

