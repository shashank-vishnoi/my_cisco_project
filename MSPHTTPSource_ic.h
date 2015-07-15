#ifndef MSP_HTTP_SOURCE_IC_H
#define MSP_HTTP_SOURCE_IC_H

#include <string>
#include <list>
#include <queue>

#include "MSPSource.h"
#include "MspCommon.h"
#include "CDvrCltAdapter_ic.h"
#include "psi_ic.h"
#include <pthread.h>
#include "cgmiPlayerApi.h"
#include <sail-clm-api.h>
#include "ApplicationData.h"
#include "MusicAppData.h"

#include <ClosedCaption.h>
// standard linux includes
#include <stdint.h>
#include <stdbool.h>
#include <cstring>
#include "avpm_ic.h"


typedef enum
{
    kHTTPSrcNotOpened,
    kHTTPSrcPlaying,
    kHTTPSrcPaused,
    kHTTPSrcStopped,
    kHTTPSrcInvalid
} eHTTPSrcState;

class MSPHTTPSource: public MSPSource
{
    tcgmi_CciData mCci;
    Channel mClmChannel;
    ChannelList *mChannelList;
    uint32_t     mFrequencyHz;  ///< frequency in Hz
    uint32_t     mSymbolRate;   ///< tuner symbol rate in kSymbols/second
    tSrcRFMode   mMode;         ///< modulation mode
    eMspStatus mParseStatus;
    std::string mFileName;
    std::string mcmUuid;
    eHTTPSrcState mHTTPSrcState;
    bool mPendingPause;
    bool mPendingPlay;
    bool mPendingSetSpeed;
    bool mPendingSetPosition;
    int mCurrentSpeedNum;
    unsigned int mCurrentSpeedDen;
    int mChannelType;
    void *mPtrCBData;
    CCIcallback_t mCCICBFn;

protected:
    void *mClientContext;
    SourceStateCallback mSrcStateCB;
    std::string mSrcUrl;
    int mSourceId;
    int mProgramNumber;
    uint8_t *mpPMTData;
    int      mPMTDataSize;
    uint16_t              mVideoPid;
    uint16_t              mAudioPid;

    bool mSdvSource;
    bool mMusicSource;
    bool mMosaicSource;
    void *mpFilterId;
    bool mbSfStarted;
    bool mCgmiFirstPTSReceived;
    pthread_mutex_t mCgmiDataMutex;
    static const int maxApplicationDataSize = 65536;
    DataReadyCallback mDataReadyCB;
    void* mDataReadyContext;
    BaseAppData *mAppData;
    pthread_mutex_t mAppDataMutex;

public:
    MSPHTTPSource(std::string aSrcUrl);

    ~MSPHTTPSource();

    eMspStatus load(SourceStateCallback aSrcStateCB, void* aClientContext);

    /* This function open source handle & registers Callback for EOF & BOF */
    eMspStatus open(eResMonPriority pri);

    /* Start the source */
    eMspStatus start();

    /* Stop source handle & unregisters File Playback Callbacks */
    eMspStatus stop();

    /* Release source */
    eMspStatus release();

    int getSourceId()const;

    int getProgramNumber()const;

    /* Required for PIP Swap */
    std::string getSourceUrl()const;

    /* Get File Name */
    std::string getFileName()const;

    bool isDvrSource()const;

    bool canRecord()const;

    eMspStatus setSpeed(int numerator, unsigned int denominator, uint32_t aNpt);

    void handleClosedCaptions(float trickSpeed);

    eMspStatus getPosition(float *aNptTime);

    eMspStatus setPosition(float aNptTime);

    /* This function will be used in case of VOD Streaming from IP to set FileName in callback of CDS*/
    void setFileName(std::string fileName);

    bool isPPV(void);

    bool isQamrf(void);

    bool isAnalogSource()const
    {
        return false;
    };

    eMspStatus setPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus);
    eMspStatus GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset, Psi *ptrPsi);
    eMspStatus SetAudioPid(uint32_t aPid);
    void SetAudioLangCB(void *aCbData, AudioLangChangedCB aLangChangedCB);
    eMspStatus UpdateAudioLanguage(Psi *ptrPsi);
    eCsciMspDiagStatus GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo);
    eCsciMspDiagStatus GetMspStreamingInfo(DiagMspStreamingInfo *msgInfo);
    static void ccChangedCB(void *cbData, bool ccSet);
    void SetCcChangedCB(void *aCbData, CcChangedCB ccChangedCB);
    eMspStatus SelectAudioVideoComponents(Psi *ptrPsi, bool isEasAudioActive);
    eMspStatus formulateCCLanguageList(Psi* psi);
    eMspStatus getCCServiceDescriptor(Psi* psi, tMpegDesc * pCcDescr);
    eMspStatus getRawPmt(uint8_t **pPMTData, int *pPMTSize);
    void RestartAudio(void);
    void StopAudio(void);
    bool isSDV(void);
    bool isMusic(void);
    bool isMosaic(void);
    eMspStatus filterAppDataPid(uint32_t aPid, DataReadyCallback aDataReadyCB, void *aClientContext, bool isMusic);
    eMspStatus stopAppDataFilter();
    eMspStatus closeAppDataFilter();
    eMspStatus getApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize);
    /* Get method to retrieve cgmi session handle */
    void* getCgmiSessHandle();
    /* Returns the CCI byte value */
    uint8_t GetCciBits(void);

    static void videoZoomChangedCB(void *cbData, tAvpmPictureMode zoomType);
    void SetVideoZoomCB(void *aCbData, VideoZoomCB videoZoomCB);
    cgmi_Status iterateAndSetDfc(int, char*);
    eMspStatus ApplyZoom(tAvRect*);
    tAvpmPictureMode videoZoomType;
    static int mStreamResolutionWidth;
    static int mStreamResolutionHeight;
    static tAvRect mVidScreenRect;

    int mZoom25Params[4];
    int mZoom50Params[4];

    static void *mpSessionId;
    static pthread_mutex_t mrdvr_player_mutex;

private:
    void buildFileName();
    void buildClmChannel();
    eMspStatus getTuningParamsFromChannelList();

    /* CGMI callback function  */
    static void CgmiPlayBkCallbackFunction(void *pUserData, void *pSession, tcgmi_Event event, tcgmi_Data *pData);

    /* Section filter callbacks */
    static cgmi_Status cgmi_SectionBufferCallback(void *pUserData, void *pFilterPriv, void *pFilterId, cgmi_Status sectionStatus, char *pSection, int sectionSize);
    static cgmi_Status cgmi_QueryBufferCallback(void *pUserData, void *pFilterPriv, void *pFilterId, char **ppBuffer, int *pBufferSize);

    std::string cgmi_ErrorString(cgmi_Status stat);
    std::string cgmi_EventString(tcgmi_Event event);
    void  buildCmUUIDfromCDSUrl(const char * cdsUrl);
};

#endif // #ifndef MSP_HTTP_SOURCE_H

