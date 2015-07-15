#ifndef MSP_SOURCE_H
#define MSP_SOURCE_H


#include <string>

#include "MspCommon.h"
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#include "cpe_source.h"
#include "cpe_programhandle.h"
#include "cpe_common.h"
#include "cpe_error.h"
#endif
#if PLATFORM_NAME == IP_CLIENT
#include "avpm_ic.h"
typedef void (*DataReadyCallback)(void *aClientContext);
#endif
#include "csci-resmon-api.h"
#include "csci-resmon-common-api.h"
#include "csci-sdv-api.h"

#include <sail-mediaplayersession-api.h>

#include "MSPDiagPages.h"

#if PLATFORM_NAME == IP_CLIENT
class Psi;
#endif



typedef void (*CCIcallback_t)(void *pData, uint8_t iCCIbyte);

enum eSourceState
{
    kSrcTunerLocked = 0,
    kSrcBOF,
    kSrcEOF,
    kSrcNextFile,
    kSrcSDVServiceLoading,
    kSrcSDVKnown,
    kSrcSDVServiceUnAvailable,
    kSrcSDVKeepAliveNeeded,
    kSrcSDVServiceChanged,
    kSrcSDVServiceCancelled,
    kSrcPPVInterstitialStart,
    kSrcPPVInterstitialStop,
    kSrcPPVSubscriptionAuthorized,
    kSrcPPVSubscriptionExpired,
    kSrcPPVStartVideo,
    kSrcPPVStopVideo,
    kSrcPPVContentNotFound,
    kSrcTunerLost,
    kSrcTunerRegained,
    kAnalogSrcTunerLocked,
    kSrcProblem,
    kSrcPendingSetSpeed,
    kSrcPendingSetPosition,
    kSrcFirstFrameEvent,
    kSrcPMTReadyEvent,
    kSrcPSIReadyEvent,
    kSrcTunerUnlocked
};

typedef void (*SourceStateCallback)(void *aClientContext, eSourceState aSourceState);

/**
   \class MSPSource
   Interface for all source types such as RF, File, PPV & SDV
*/


class MSPSource
{
public:
    MSPSource() {}
    /* This method will populate all tuning information based on Source Url*/
    virtual eMspStatus load(SourceStateCallback aSrcStateCB, void* aClientContext) = 0;

    /* This method will open the source & register Source Ready Callback */
    virtual eMspStatus open(eResMonPriority pri) = 0;

    /* Start the source */
    virtual eMspStatus start() = 0;

    /* Stop the source */
    virtual eMspStatus stop() = 0;

    /* Release the source */
    virtual eMspStatus release() = 0;
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    /* Get method to retrieve source handle */
    virtual tCpeSrcHandle getCpeSrcHandle()const = 0;
#endif
#if PLATFORM_NAME == IP_CLIENT
    /* Get method to retrieve cgmi session handle */
    virtual void* getCgmiSessHandle();
#endif
    /* Get Method to retrieve program number */
    virtual int getProgramNumber()const = 0;

    /* Get Method to retrieve source ID*/
    virtual int getSourceId()const = 0;

    /* Get Method to retrive Source Url */
    virtual std::string getSourceUrl()const = 0;

    /* Get File Name */
    virtual std::string getFileName()const = 0;

    /* Check if source is Dvr type or not */
    virtual bool isDvrSource()const = 0;

    /* Returns true if we can record a source */
    virtual bool canRecord()const = 0;
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    /* Set Speed will be used for File Source */
    virtual eMspStatus setSpeed(tCpePlaySpeed aPlaySpeed, uint32_t aNpt) = 0;
#endif
#if PLATFORM_NAME == IP_CLIENT
    /*  Set Speed will be used for File Source */
    virtual eMspStatus setSpeed(int numerator, unsigned int denominator, uint32_t aNpt) = 0;
#endif
    virtual eMspStatus getPosition(float *aNptTime) = 0;

    virtual eMspStatus setPosition(float aNptTime) = 0;

    virtual eMspStatus setSdv(bool Status, eSdvmServiceAttributeInt attribute);
    virtual eMspStatus setMediaSessionInstance(IMediaPlayerSession *pIMediaPlayerSession);

    virtual bool isSDV(void);

    virtual bool isMusic(void);

    virtual ~MSPSource() {}
    virtual bool isPPV(void) = 0;
    virtual bool isQamrf(void) = 0;

    virtual eMspStatus updateTuningParams(const char *);
    // used to communicate to current priority to resource manager to indicate what we are currently doing
    virtual void setTunerPriority(eResMonPriority);

    virtual bool isAnalogSource()const = 0;
    virtual eCsciMspDiagStatus GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo);


    /* This function will be used in case of VOD Streaming from IP to set FileName in callback of CDS*/
    virtual void setFileName(std::string fileName)
    {
        (void)fileName;
    };
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8

    /* Get method to retrieve source handle - specific for HN streaming source*/
    virtual tCpePgrmHandle getCpeProgHandle() const = 0;

    /* Set method for the streaming session handle - specific for HN streaming source*/
    virtual void SetCpeStreamingSessionID(uint32_t sessionId) = 0;

    virtual eMspStatus InjectCCI(uint8_t CCIbyte);
#endif
#if PLATFORM_NAME == IP_CLIENT
    /* sets the presentation parameters for the video playback screen */
    virtual eMspStatus setPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus);
    /* gets the list of audio and video elementary streams present in the service */
    virtual eMspStatus GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset, Psi *ptrPsi);
    /* sets or enables the audio PID */
    virtual eMspStatus SetAudioPid(uint32_t aPid);
    /* interface to the controller to register a callback with AVPM to receive
     * audio language change notifications */
    virtual void SetAudioLangCB(void *aCbData, AudioLangChangedCB aLangChangedCB);
    /* Updates/Sets the audio language to user preference audio language */
    virtual eMspStatus UpdateAudioLanguage(Psi *ptrPsi);
    /* Sets the video and audio pid to be decoded and presented */
    virtual eMspStatus SetAudioVideoPids(Psi *ptrPsi);
    /* Gets the information about the active streaming in-progress */
    virtual eCsciMspDiagStatus GetMspStreamingInfo(DiagMspStreamingInfo *msgInfo);
    /* This method will query the RAW PMT information from the Source */
    virtual eMspStatus getRawPmt(uint8_t **pPMTData, int *pPMTSize);
    /* This method selects the audio and video components for the media player session */
    virtual eMspStatus SelectAudioVideoComponents(Psi *ptrPsi, bool isEasAudioActive);
    virtual eMspStatus filterAppDataPid(uint32_t aPid, DataReadyCallback aDataReadyCB, void *aClientContext, bool isMusic);
    virtual eMspStatus getApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize);
    virtual bool isMosaic(void);
    /*This method restarts the audio decoding for a session*/
    virtual void RestartAudio(void);
    /*This method stop the audio alone for a session */
    virtual void StopAudio(void);
    /* This method selects the CC language components and update CC Lang stream map for the media player session */
    virtual eMspStatus formulateCCLanguageList(Psi* psi);
    /* Returns the CCI byte value */
    virtual uint8_t GetCciBits(void);
#endif
};

#endif //#ifdef MSP_SOURCE_H
