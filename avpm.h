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

#include "sail_dfb.h"
// cpe includes
#include "sail-settingsuser-api.h"

#include <directfb.h>
#include <cpe_mediastrmhdlr.h>
#include <cpe_programhandle.h>
#include <cpe_avoutext.h>
#include <cpe_error.h>
#include <cpe_mediamgr.h>
#include <cpe_dfbdefs.h>
// MSP includes
#include "MspCommon.h"
#include "eventQueue.h"
#include "sail-avpm-api.h"
#include "use_threaded.h"
#include <sail-message-api.h>
#include <csci-base-message-api.h>

//These values are to be defined in the Galio MDA codes somewhere?
//Right now we will hard code them here.
#define GALIO_MAX_WINDOW_WIDTH  1280
#define GALIO_MAX_WINDOW_HEIGHT 720
#define MAX_SETTING_VALUE_SIZE 30
#define MAX_SAMPLE_TEXT_LENGTH 128
#define DEFAULT_CC_DIGITAL_OPTION 1
#define TENEIGHTYP_VERTICAL 1080
#define TENEIGHTYP_HORIZONTAL 1920
#define TENEIGHTYP_FRAMERATE 30000
const char VOD1080PSETTING_SUPPORTED[] = "supported";
const char VOD1080PSETTING_NOT_SUPPORTED[] = "notSupported";
const char VOD1080PSETTING_UNKNOWN[] = "unknown";
const char UNISETTING_RESOLUTION[] = "ciscoSg/look/mode";
const char UNISETTING_VOD1080P[] = "ciscoSg/look/vod1080pDisplay";

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
struct ProgramHandleSetting
{
    tCpeMshAssocHandle hdHandle;
    tCpeMshAssocHandle sdHandle;
    tCpeMshAssocHandle cchandle;
    IMediaStreamHandler *msh;
    IVideoStreamHandler *vsh;
    ITextStreamHandler *tsh;
    IAVOutput *avh;
    tCpeMshVideoStreamAttributes attrib;
    tCpeAvoxOutputMode *pMode;
    DFBRectangle rect;
    bool audioFocus;
};

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
    kAvpmVOD1080pDisplay
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

class Avpm
{

public:
    ccFallbackLangMap		mfallBackCCLangMap; // Fallback map for ez reader langs
    ccFallbackServiceMap 	mfallbackCCServiceMap; //Fallback map for lang to service number
    ccPMTLangServNoMap		mCCLanguageStreamMap; // Map to hold available CC languages in PMT along
    static Avpm * getAvpmInstance();
    eMspStatus setPresentationParams(tCpePgrmHandle pgrhandle, DFBRectangle rect, bool audiofocus);
    eMspStatus setAudioParams(tCpePgrmHandle pgrHandle);
    // Sets the Audio Outputmode, enables the input port and play to the surface
    eMspStatus connectOutput(tCpePgrmHandle pgrHandle);
    // Wrapper api for connectOutput
    eMspStatus startOutput(tCpePgrmHandle pgrHandle);
    // Audio Mute and Video freeze
    eMspStatus stopOutput(tCpePgrmHandle pgrHandle);
    eMspStatus pauseVideo(tCpePgrmHandle pgrHandle);
    // Releases IVideoStream and MediaStream handlers
    eMspStatus disconnectOutput(tCpePgrmHandle pgrHandle);
    // To register callbacks from Unified Settings for any user settings updates
    // Copy Protection Settings
    eMspStatus SetHDCP(HDCPState state);
    eMspStatus SetCITState(CITState state);
    eMspStatus SetAPS(MacroVisionAPS state);
    eMspStatus SetEMI(uint8_t emi);
    eMspStatus SetCCIBits(uint8_t ccibits, bool isSOC);
    eMspStatus applyAVSetting(eAvpmEvent aSetting, char* pValue);
    void SetAudioLangCB(void *aCbData, AudioLangChangedCB aLangChangedCB);
    void SetSapChangedCB(void *aCbData, SapChangedCB sapChangeCb);
    static void * eventThreadFunc(void *data);
    int displaySdBarker(bool state);
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
    eMspStatus setAnalogOutputMode(tCpePgrmHandle pgrHandle);

    inline void lockMutex(void)
    {
        pthread_mutex_lock(&mMutex);
    }
    inline void unLockMutex(void)
    {
        pthread_mutex_unlock(&mMutex);
    }
    eMspStatus SetDigitalCCLang(tCpePgrmHandle pgrHandle);
    int getDigitalLangToServiceMap();

    void setIsVod(bool isVod)
    {
        mIsVod = isVod;
    }

private:
    IDirectFB *dfb;
    IDirectFBDisplayLayer *pHDLayer;
    IDirectFBDisplayLayer *pSDLayer;
    IDirectFBSurface *primary_surface;

    IDirectFBDisplayLayer *sdGfx2Layer;
    IDirectFBSurface *sdGfx2surface;
    IDirectFBDisplayLayer *hdGfx2Layer;
    IDirectFBSurface *hdGfx2surface;

    IDirectFBSurface *pHdClosedCaptionTextSurface;
    IDirectFBSurface *pHdClosedCaptionSampleTextSurface;
    IDirectFBWindow *samplePreviewTextWindow;
    IDirectFBDisplayLayer *pHDClosedCaptionDisplayLayer;
    IDirectFBSurface* getsurface(int LayerIndex);
    void logLayerInfo(int layerIndex);
    static void logScalingRects(char *msg, tCpeVshScaleRects rect);

    static void DisplayCallback_SD(void *ctx, tCpeVshDisplayToFlags *flags, tCpeVshFrameAttributes *frameAttrib,
                                   IDirectFBSurface *frameSurface, IDirectFBSurface *altSurface, DFBDimension screenSize, tCpeVshScaleRects *scaleRects);
    static void DisplayCallback_SDPIP(void *ctx, tCpeVshDisplayToFlags *flags, tCpeVshFrameAttributes *frameAttrib,
                                      IDirectFBSurface *frameSurface, IDirectFBSurface *altSurface, DFBDimension screenSize, tCpeVshScaleRects *scaleRects);

    bool renderToSurface;
    tAvpmAudioSrc mAvpmAudioSrc;
    bool isClosedCaptionEnabled;
    bool isPreviewEnabled;
    bool isCaptionEnabledBeforePreview;
    bool mbSapEnabled;
    tCpeTshAttributes mStreamAttrib;
    tCpeTshAttributes mPreviewStreamAttrib;
    tCpeTshAttributes mStreamLanguageAttrib;
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
    DFBRectangle galio_rect;
    AudioLangChangedCB mAudioLangChangedCb;
    SapChangedCB mSapChangeCb;
    void *mAudioLangCbData;
    void *mpSapChangedCbData;
    int mDefaultResolution;  // resolution set at boot time
    map<tCpePgrmHandle, ProgramHandleSetting*> mMap;
    static Avpm *m_pInstance;
    tCpePgrmHandle mMainScreenPgrHandle;
    tCpePgrmHandle mPipScreenPgrHandle;
    int prev_vol;
    bool mHaveVideo;
    tAvpmTVAspectRatio user_aspect_ratio;
    tAvpmPictureMode picture_mode;
    static int callback;
    static tCpeVshScaleRects HDRects, SDRects;


    char mccUserDigitalSetting[MAX_SETTING_VALUE_SIZE];


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

    // Member functions to interact with DirectFB
    eMspStatus dfbInit();
    void dfbExit();
    void setupSDPIPPOPLayer(void);
    void setupHDPIPPOPLayer(void);
    DirectResult app_dfb_SaWManInit(int argc, char** argv);
    int app_dfb_SaWManUninit(void);
    void ClearCCsurface();


    eMspStatus GetScreenSize(tCpeDFBScreenIndex index, int *width, int *height);
    void CalculateScalingRectangle(DFBRectangle& final, DFBRectangle& request, int maxWidth, int maxHeight);
    bool isSetPresentationParamsChanged(DFBRectangle newRect, DFBRectangle oldRect);

    // List of functions which gets the current setting values from unified settings
    static void* eventthreadFunc(void *data);
    static void SettingUpdated(void *pClientContext, const char *pTag, const tSettingsProfileDescriptor *pProfileDesc);
    // List of functions which sets the settings through DFB
    eMspStatus setAudioOutputMode(void);
    eMspStatus playToVideoLayer(tCpePgrmHandle pgrHandle);
    eMspStatus playPIPVideo(tCpePgrmHandle pgrHandle);
    eMspStatus MasterMute(tCpePgrmHandle pgrHandle, bool mute);
    eMspStatus mute(tCpePgrmHandle pgrHandle, bool mute);
    eMspStatus freezeVideo(tCpePgrmHandle pgrHandle);
    eMspStatus release(tCpePgrmHandle pgrHandle);

    eMspStatus closedCaption(tCpePgrmHandle pgrHandle, eAvpmEvent aEvent, char *pValue);
    eMspStatus setccStyle(char* cc_opacity_in, tCpeTshOpacity *aOpacity);
    eMspStatus setccColor(char* cc_color_in, DFBColor *aColor);
    eMspStatus setccFontFace(char* cc_font_face, tCpeTshFontFace *aFontFace);
    void ClearCcRenderSurface(IDirectFBSurface *pTextSurface);

    eMspStatus setMasterVolume(tCpePgrmHandle pgrHandle, int);
    eMspStatus setDisplayResolution(eAvpmResolution);
    void ConfigureEncoderSettings(DFBScreenEncoderConfig *enc_descriptions, eAvpmResolution mode);
    eMspStatus setdigitalAudioSetMode(void);
    eMspStatus setAC3AudioRange(tCpePgrmHandle pgrHandle, tAvpmAudioRange audio_range);
    eMspStatus setTVAspectRatio(IVideoStreamHandler *vsh, tCpeMshAssocHandle hdHandle, tCpeMshAssocHandle sdHandle, eAvpmVideoRects videoRectsType = kAvpmBothHDSD);
    eMspStatus setPictureMode(IVideoStreamHandler *vsh, tCpeMshAssocHandle hdHandle, tCpeMshAssocHandle sdHandle, eAvpmVideoRects videoRectsType = kAvpmBothHDSD);
    void SetVideoRects(IVideoStreamHandler *vsh, tCpeMshAssocHandle hdHandle, tCpeMshAssocHandle sdHandle,
                       tCpeVshScaleRects HDrect, tCpeVshScaleRects SDrect, eAvpmVideoRects videoRectsType = kAvpmBothHDSD);
    static void setStereoDepth(tCpeVshScaleRects& rects);
    //eMspStatus setDisplayResolution(IdirectFBScreen *dfbScreen);
    eMspStatus setRFmodeSetOutputChan(void);
    eMspStatus initPicMode(void);
    eMspStatus setInitialVolume(void);
    void registerAVSettings(char *pTag); //Overloaded function to re register with unified settings
    void registerAVSettings();

    void registerOpaqueSettings();
    eAvpmEvent mapUsetPtagToAVEvent(const char *aPtag);

    // This function shall find out the number of instances of 1394 ports on the DHCT.
    eMspStatus avpm1394PortInit(void);
    // Function to enable and disable the streaming on 1394 port during channel change.
    eMspStatus avpm1394PortStreaming(tCpePgrmHandle mediaHandle, bool enable);

    int mAvailable1394Instances;

    ProgramHandleSetting* getProgramHandleSettings(tCpePgrmHandle pgrHandle);
    void releaseProgramHandleSettings(tCpePgrmHandle pgrHandle);
    void setWindow(DFBRectangle rectRequest, IVideoStreamHandler *vsh, tCpeDFBScreenIndex screenIndex, tCpeMshAssocHandle &handle);
    static void settingChangedCB(eUse_StatusCode result, UseIpcMsg *pMsg, void *pClientContext);
    eMspStatus clearSurfaceFromLayer(int graphicsLayerIndex);
    static void DisplayCallback_HD(void *ctx,
                                   tCpeVshDisplayToFlags   *flags,
                                   tCpeVshFrameAttributes  *frameAttrib,
                                   IDirectFBSurface        *frameSurface,
                                   IDirectFBSurface        *altSurface,
                                   DFBDimension            screenSize,
                                   tCpeVshScaleRects       *scaleRects);
    static void DisplayCallback_HDPIP(void *ctx,
                                      tCpeVshDisplayToFlags   *flags,
                                      tCpeVshFrameAttributes  *frameAttrib,
                                      IDirectFBSurface        *frameSurface,
                                      IDirectFBSurface        *altSurface,
                                      DFBDimension            screenSize,
                                      tCpeVshScaleRects       *scaleRects);
    static int GetVshDisplayToFlags(void);
    eMspStatus applySocSetting(bool  isSoc);
    eMspStatus  showPreviewSample(bool setSamplePreview);
    void setCcPreviewEdge(tAVPMCcEdge previewEdge, tCpeTshEdgeEffect *setEdge);
    void setCcPreviewColor(tAVPMCcColor  previewColor, DFBColor *setColor);
    void setCcPreviewOpacity(tAVPMCcOpacity previewOpacity, tCpeTshOpacity *setOpacity);
    eMspStatus setCcEdge(char* cc_char_edge, tCpeTshEdgeEffect *aCharEdge);
    void setCcPreviewFont(tAVPMCcFont previewFont, tCpeTshFontFace *setFontFace);
    eMspStatus setccPenSize(char* cc_pen_size, tCpeTshPenSize *aPenSize);

    bool mIsOutputStartedForMain;
    bool mIsOutputStartedForPIP;

    bool mIsVod; // If true, set 1080p o/p for 1080p source.
    bool mDisplayTypeIsHDMI;
    bool mIs1080pModeActive;
    bool isSource1080p(const tCpePgrmHandle pgrHandle);
    bool isCurrentDisplayResolution1080p(const tCpeAvoxDigDispType displayType);
    void setOptimalDisplayResolution();
    void setDefaultDisplayResolution();
    bool determineDisplayCapabilities();
    bool getResolutionFromMode(eAvpmResolution &dispResolution);
    void sendSAILMessage(eVOD1080pMsgType msg);
    bool doesTVSupport1080p(const tCpeAvoxDigDispInfo &dispInfo);
    void updateVOD1080pSetting(const char* value);
};
//Creates instance
#endif //AVPM_H
