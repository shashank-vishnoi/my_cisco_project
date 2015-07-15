#ifndef AVPM_H
#define AVPM_H

/**
   \file Avpm.h
   AVPM - Audio Video Port Manager
   AVPM is a module which handles the Audio and Video settings from Display session and
   Unifeid seetings by accessing DirectFB as an interface to Hardware
*/

// standard linux includes
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string>
#include <pthread.h>
#include <map>
using namespace std;


#include "sail-settingsuser-api.h"

// MSP includes
#include "MspCommon.h"
#include "eventQueue.h"
#include "sail-avpm-api.h"
#include "use_threaded.h"
#include "pmt_ic.h"
#include <manager.hpp>
#include <host.hpp>
#include <ClosedCaption.h>
//These values are to be defined in the Galio MDA codes somewhere?
//Right now we will hard code them here.
#define GALIO_MAX_WINDOW_WIDTH  1280
#define GALIO_MAX_WINDOW_HEIGHT 720
#define MAX_SETTING_VALUE_SIZE 20
#define MAX_AVPM_SETTING_VALUE_SIZE 30
#define MAX_SAMPLE_TEXT_LENGTH 128
#define DEFAULT_CC_DIGITAL_OPTION 1

typedef unsigned int paramType;
typedef void *ParamValue;
// Map used for storing  fallback lang mapping e.g. eng_ez to eng
typedef std::map<std::string , std::string> ccFallbackLangMap;
// Map used for storing  fallback lang  to default service map e.z eng to d1
typedef std::map<std::string , int> ccFallbackServiceMap;
// Map for lang code to service number match
typedef std::map<std::string , int> ccPMTLangServNoMap;

class MSPEventQueue;
class Event;
class Avpm;

typedef enum
{
    HDCP_OFF = 0,
    HDCP_ON
} HDCPState;
/* Enum specifying if Constrained Image Trigger is Enabled or not */
typedef enum
{
    CIT_DISABLED = 0,
    CIT_ENABLED
} CITState;

typedef enum
{
    APS_OFF = 0,
    APS_ON_1,
    APS_ON_2,
    APS_ON_3
} MacroVisionAPS;

typedef enum
{
    COPY_FREELY = 0,
    COPY_NO_MORE,
    COPY_ONCE,
    COPY_NEVER
} EMIState;
/* Copy Protection related defines end */
typedef enum
{
    tAvpmTVAspectRatio4x3,
    tAvpmTVAspectRatio16x9,
    tAvpmTVAspectRatio4x3LetterBox,
    tAvpmTVAspectRatio14x9
} tAvpmTVAspectRatio;

typedef enum
{
    tAvpmPictureMode_Normal,
    tAvpmPictureMode_Stretch,
    tAvpmPictureMode_Zoom25,
    tAvpmPictureMode_Zoom50

} tAvpmPictureMode;


typedef enum
{
    tAvpmAudioRangeNarrow,
    tAvpmAudioRangeNormal,
    tAvpmAudioRangeWide,
} tAvpmAudioRange;


typedef enum
{
    tAvpmDigitalAudioOutputDisabled,
    tAvpmDigitalAudioOutputCompressed,
    tAvpmDigitalAudioOutputUncompressed
} tAvpmDigitalAudioOutputMode;

typedef enum
{
    tAvpmAudioSrcDigital,
    tAvpmAudioSrcAnalog
} tAvpmAudioSrc;

//valid background and foreground character property
typedef enum
{
    kCCEia708Color_Black,
    kCCEia708Color_White,
    kCCEia708Color_Red,
    kCCEia708Color_Green,
    kCCEia708Color_Blue,
    kCCEia708Color_Yellow,
    kCCEia708Color_Magenta,
    kCCEia708Color_Cyan
} eCCEia708Color;



//output connectors
typedef enum
{
    tAvpmConnector_HDMI,
    tAvpmConnector_Component,
    tAvpmConnector_Composite,
    tAvpmConnector_All
} eAvpmConnector;


// Display resolution property
typedef enum
{
    tAvpmResolution_480i,
    tAvpmResolution_480p,
    tAvpmResolution_720p,
    tAvpmResolution_1080i,
    tAvpmResolution_1080p
} eAvpmResolution;


/**
   \class Avpm
*/
typedef enum
{
    kAvpmNotDefined = -1,
    kAvpmMasterMute = 0,
    kAvpmMasterVol,
    kAvpmAudioOutputChanged,
    kAvpmAC3AudioRangeChanged,
    kAvpmAudioLangChanged,
    kAvpmVolumeControlChanged,
    kAvpmTVAspectratioChanged,
    kAvpmVideoOutputChanged,
    kAvpmDisplayResolnChanged,
    kAvpmDigitalCCEnable,
    kAvpmAnalogCCEnable,
    kAvpmCCCharColor,
    kAvpmCCPenSize,
    kAvpmBackgroundColor,
    kAvpmBackgroundStyle,
    kAvpmCCSetByProgram,
    kAvpmCCOutputEnable,
    kAvpmStreamAspectChanged,
    kAvpmUpdateCCI,
    kAvpmSkinSize,
    kAvpmrfOutputChannel,
    kAvpmDVSChanged,
    kAvpmSapChanged,
    kAvpmRegSettings, //Added for Reregister event from unified settings
    kAvpmThreadExit,
    kAvpmApplyHDMainPresentationParams,
    kAvpmApplySDMainPresentationParams,
    kAvpmApplyHDPipPresentationParams,
    kAvpmApplySDPipPresentationParams,
    kAvpmHDStreamAspectChanged,
    kAvpmSDStreamAspectChanged,
    kAvpmCCCharStyle,
    kAvpmCCCharEdge,
    kAvpmCCCharFont,
    kAvpmCCWindowColor,
    kAvpmCCWindowStyle,
} eAvpmEvent;

typedef enum
{
    kAvpmSDVideo,
    kAvpmHDVideo,
    kAvpmBothHDSD,
    kAvpmHDCpy,
    kAvpmSDCpy,
} eAvpmVideoRects;


typedef void (*AudioLangChangedCB)(void *data);
typedef void (*SapChangedCB)(void *data);
typedef void (*CcChangedCB)(void *data, bool ccSet);

typedef void (*VideoZoomCB)(void *data, tAvpmPictureMode zoomType);

class Avpm
{

public:
    ccFallbackLangMap		mfallBackCCLangMap; // Fallback map for ez reader langs
    ccFallbackServiceMap 	mfallbackCCServiceMap; //Fallback map for lang to service number
    ccPMTLangServNoMap		mCCLanguageStreamMap; // Map to hold available CC languages in PMT along
    static Avpm * getAvpmInstance();
    eMspStatus setPresentationParams(bool audiofocus);
    eMspStatus setAudioParams();
    // Sets the Audio Outputmode, enables the input port and play to the surface
    eMspStatus connectOutput();
    // Audio Mute and Video freeze
    eMspStatus stopOutput();
    eMspStatus pauseVideo();
    // Releases IVideoStream and MediaStream handlers
    eMspStatus disconnectOutput();
    // To register callbacks from Unified Settings for any user settings updates
    // Copy Protection Settings
    eMspStatus SetHDCP(HDCPState state);
    eMspStatus SetCITState(CITState state);
    eMspStatus SetAPS(int state);
    eMspStatus SetEMI(uint8_t emi);
    eMspStatus SetCCIBits(uint8_t ccibits, bool isSOC);
    eMspStatus applyAVSetting(eAvpmEvent aSetting, char* pValue);
    void SetAudioLangCB(void *aCbData, AudioLangChangedCB aLangChangedCB);
    void SetSapChangedCB(void *aCbData, SapChangedCB sapChangeCb);
    static void * eventThreadFunc(void *data);
    void clearVideoSurface();
    eMspStatus setPreviewStatus(tAVPMCcPreview preview);
    void SetVideoPresent(bool isPresent);
    void setAvpmAudioSrc(tAvpmAudioSrc src)
    {
        mAvpmAudioSrc = src;
    }
    void setAvpmSapEnabled(bool bSapEnabled)
    {
        mbSapEnabled = bSapEnabled;
    }
    eMspStatus setCc(bool isCaptionEnabled);
    eMspStatus getCc(bool *pIsCaptionEnabled);
    /*static Avpm& GetInstance()
    {
        return m_pInstance;
    } */

    // The function is used to switch the 1394 port between Analog & Digital mode during EAS.
    eMspStatus avpmSwitch1394PortMode(bool enable);
    eMspStatus avpm1394PortInit(void);
    // Function to enable and disable the streaming on 1394 port during channel change.
    eMspStatus avpm1394PortStreaming(bool enable);
    eMspStatus setAnalogOutputMode();

    inline void lockMutex(void)
    {
        pthread_mutex_lock(&mMutex);
    }
    inline void unLockMutex(void)
    {
        pthread_mutex_unlock(&mMutex);
    }

    void SetVideoZoomCB(void *aVzData, VideoZoomCB videoZoomCB);
    tAvpmPictureMode getZoomType();
    eAvpmResolution dispResolution;
    void SetCcChangedCB(void *aCbData, CcChangedCB ccChangedCB);

    int getDigitalLangToServiceMap(char* ccDigitalSetting);
    /*Method to update CC Language stream map for the session*/
    void updateCCLangStreamMap(tMpegDesc *pCcDescr);
private:

    void logLayerInfo(int layerIndex);
    static void logScalingRects(char *msg);

    bool renderToSurface;
    tAvpmAudioSrc mAvpmAudioSrc;
    bool isClosedCaptionEnabled;
    bool isPreviewEnabled;
    bool isCaptionEnabledBeforePreview;
    bool mbSapEnabled;
    gsw_CcAttributes mCcAttribute;
    gsw_CcAttributes mCcPreviewAttribute;

    // Adding for Unified Settings Stubs
    MSPEventQueue* threadEventQueue;
    pthread_t mUsetHandlerThread;
    pthread_t mEventHandlerThread;
    int mMaxOutputScreenWidthHD;
    int mMaxOutputScreenHeightHD;
    int mMaxOutputScreenWidthSD;
    int mMaxOutputScreenHeightSD;
    static pthread_mutex_t  mMutex;
    static float frame_asp, frame_aspPIP;
    AudioLangChangedCB mAudioLangChangedCb;
    SapChangedCB mSapChangeCb;
    VideoZoomCB mVideoZoomCB;
    CcChangedCB mCcChangedCb;
    void *mAudioLangCbData;
    void *mpSapChangedCbData;
    void *mVideoZoomCBData;
    void *mCcCbData;
    int mDefaultResolution;  // resolution set at boot time
    static Avpm *m_pInstance;
    int prev_vol;
    bool mHaveVideo;
    bool mCCAtribIsSet;
    tAvpmTVAspectRatio user_aspect_ratio;
    tAvpmPictureMode picture_mode;
    static int callback;

    std::map<string, int> mSAIL_To_SDK_Map;
    std::map<string, int> mColorCharToInt;
    int mCCIbyte;
    int mIsSoc;
    // Constructor
    Avpm() ;
    // copy constructor
    Avpm(const Avpm&) {};
    // assignment operator
    void operator=(const Avpm&);
    //Destructor
    ~Avpm();

    void ClearCCsurface();
    void SAILToSDKCCMap();

    eMspStatus GetScreenSize(int *width, int *height);
    void CalculateScalingRectangle(int maxWidth, int maxHeight);
    bool isSetPresentationParamsChanged();

    // List of functions which gets the current setting values from unified settings
    static void* eventthreadFunc(void *data);
    static void SettingUpdated(void *pClientContext, const char *pTag, const tSettingsProfileDescriptor *pProfileDesc);

    // List of functions which sets the settings through DFB
    eMspStatus setAudioOutputMode(dsAudioEncoding_t dsAudioEncodingType);
    eMspStatus playToVideoLayer();
    eMspStatus playPIPVideo();
    eMspStatus MasterMute(bool mute);
    eMspStatus mute(bool mute);
    eMspStatus freezeVideo();
    eMspStatus release();

    eMspStatus closedCaption(eAvpmEvent aEvent, char *pValue);
    eMspStatus setccFontSize(gsw_CcAttributes *pCcAttribute, char* cc_fontsize_in);
    eMspStatus setccFontStyle(gsw_CcFontStyle *setFontStyle, char* cc_font_style);
    eMspStatus setCcEdgeType(gsw_CcEdgeType *setEdgeType, char* char_edge_type);
    eMspStatus setccStyle(gsw_CcOpacity *aOpacity, char* cc_opacity_in);
    void setCcPreviewFont(tAVPMCcFont previewFont, gsw_CcFontStyle *setFontStyle);
    void setCcPreviewEdge(tAVPMCcEdge previewEdge, gsw_CcEdgeType *setEdgeType);
    void setCcPreviewOpacity(tAVPMCcOpacity previewOpacity, gsw_CcOpacity *setOpacity);
    void setCcPreviewColor(tAVPMCcColor  previewColor, gsw_CcColor *setColor);
    void ClearCcRenderSurface();

    eMspStatus setMasterVolume(int);
    eMspStatus setDisplayResolution(const char*);//eAvpmResolution);
    void ConfigureEncoderSettings(eAvpmResolution mode);
    eMspStatus setdigitalAudioSetMode(void);
    eMspStatus setAC3AudioRange(tAvpmAudioRange audio_range);
    eMspStatus setTVAspectRatio(eAvpmVideoRects videoRectsType = kAvpmBothHDSD);
    eMspStatus setPictureMode(eAvpmVideoRects videoRectsType = kAvpmBothHDSD);
    //eMspStatus setPictureMode();
    void SetVideoRects(eAvpmVideoRects videoRectsType = kAvpmBothHDSD);
    static void setStereoDepth();
    eMspStatus setRFmodeSetOutputChan(void);
    eMspStatus initPicMode(void);
    eMspStatus setInitialVolume(void);
    void registerAVSettings(char *pTag); //Overloaded function to re register with unified settings
    void registerAVSettings();

    void registerOpaqueSettings();
    eAvpmEvent mapUsetPtagToAVEvent(const char *aPtag);

    // This function shall find out the number of instances of 1394 ports on the DHCT.
    //eMspStatus avpm1394PortInit(void);
    // Function to enable and disable the streaming on 1394 port during channel change.
    //eMspStatus avpm1394PortStreaming(bool enable);

    int mAvailable1394Instances;

    void setWindow();
    static void settingChangedCB(eUse_StatusCode result, UseIpcMsg *pMsg, void *pClientContext);
    eMspStatus clearSurfaceFromLayer(int graphicsLayerIndex);
    static int GetVshDisplayToFlags(void);
    eMspStatus applySocSetting(bool  isSoc);
};
//Creates instance
#endif //AVPM_H
