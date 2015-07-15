
/**
   \file shim-CC-cperpv1.cpp
   Implementation file for shim layer for directfb specific implementations for G6

   CC - closed caption(Subtitle)
*/

#include "avpm.h"
#include "sail_dfb.h"
#include <dlog.h>
#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_AVPM, level,"Avpm:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#define UNUSED_PARAM(a) (void)a;
#define G6_PREVIEW_POS_X 10
#define G6_PREVIEW_POS_Y 88

extern bool IsDvrSupported();

DirectResult app_dfb_SaWManInit(int argc, char** argv)
{
    DirectResult ret = DR_OK;
    UNUSED_PARAM(argc)
    UNUSED_PARAM(argv)

    dlog(DL_MSP_AVPM, DLOGL_FUNCTION_CALLS, "%s Less Than G9(For G6 4k and 8k Not requried)", __FUNCTION__);
    return (ret);
}


int app_dfb_SaWManUninit(void)
{
    dlog(DL_MSP_AVPM, DLOGL_FUNCTION_CALLS, "%s Less Than G9(For G6 4k and 8k Not requried)", __FUNCTION__);
    return 0;
}

//
// bulk of dfb initialization is done by base services. Since it needs to be setup before Galio runs
//
eMspStatus Avpm::dfbInit()
{
    DFBResult result;
    DFBDisplayLayerConfig  dlc;
    IDirectFBScreen *pHD;
    IDirectFBScreen *pSD;
    DFBScreenMixerConfig     conf;


    FNLOG(DL_MSP_AVPM);

    result = DirectFBCreate(&dfb);
    if ((result != DFB_OK) || (dfb == NULL))
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error: DirectFBCreate Failed\n");
        return kMspStatus_Error;
    }

    result = dfb->GetDisplayLayer(dfb, VANTAGE_HD_VIDEO, &pHDLayer);  // TODO rename as HDVideo Layer
    if ((result != DFB_OK) || (pHDLayer == NULL))
    {

        return kMspStatus_AvpmError;
    }

    // Get HD Screen
    result = dfb->GetScreen(dfb, eCpeDFBScreenIndex_HD, &pHD);
    if ((result != DFB_OK) || (pHD == NULL))
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in getting DFB HD screen interface. Error code = %d\n", result);
        return kMspStatus_Error;
    }


    pHD->GetSize(pHD, &mMaxOutputScreenWidthHD, &mMaxOutputScreenHeightHD);
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "HD Screen  w=%d, h=%d\n", mMaxOutputScreenWidthHD, mMaxOutputScreenHeightHD);

    conf.flags = (DFBScreenMixerConfigFlags)(DSMCONF_BACKGROUND | DSMCONF_LAYERS);
    conf.background.a = 0xff;
    conf.background.r = conf.background.g = conf.background.b = 0x00;
    DFB_DISPLAYLAYER_IDS_EMPTY(conf.layers);
    DFB_DISPLAYLAYER_IDS_ADD(conf.layers, VANTAGE_HD_VIDEO);
    DFB_DISPLAYLAYER_IDS_ADD(conf.layers, VANTAGE_HD_MAINAPP);

    if (IsDvrSupported())
    {
        DFB_DISPLAYLAYER_IDS_ADD(conf.layers, VANTAGE_HD_PIP);
    }

    DFB_DISPLAYLAYER_IDS_ADD(conf.layers, VANTAGE_HD_CC);

    result = pHD->SetMixerConfiguration(pHD, 0, &conf);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error enabling HD Graphics Plane. Error code = %d\n", result);
        return kMspStatus_Error;
    }

    pHD->Release(pHD);


    clearSurfaceFromLayer(VANTAGE_HD_VIDEO);

    result = dfb->GetDisplayLayer(dfb, VANTAGE_SD_VIDEO, &pSDLayer);    // TODO: ditto
    if ((result != DFB_OK) || (pSDLayer == NULL))
    {
        return kMspStatus_AvpmError;
    }

    // Get SD Screen
    result = dfb->GetScreen(dfb, eCpeDFBScreenIndex_SD, &pSD);
    if ((result != DFB_OK) || (pSD == NULL))
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in getting DFB SD screen interface. Error code = %d\n", result);
        return kMspStatus_Error;
    }

    pSD->GetSize(pSD, &mMaxOutputScreenWidthSD, &mMaxOutputScreenHeightSD);

    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "SD Screen  w=%d, h=%d\n", mMaxOutputScreenWidthSD, mMaxOutputScreenHeightSD);

    conf.flags = (DFBScreenMixerConfigFlags)(DSMCONF_BACKGROUND | DSMCONF_LAYERS);
    conf.background.a = 0xff;
    conf.background.r = conf.background.g = conf.background.b = 0x00;
    DFB_DISPLAYLAYER_IDS_EMPTY(conf.layers);
    DFB_DISPLAYLAYER_IDS_ADD(conf.layers, VANTAGE_SD_VIDEO);
    DFB_DISPLAYLAYER_IDS_ADD(conf.layers, VANTAGE_SD_MAINAPP);

    if (IsDvrSupported())
    {
        DFB_DISPLAYLAYER_IDS_ADD(conf.layers, VANTAGE_SD_PIP);

    }
    result = pSD->SetMixerConfiguration(pSD, 0, &conf);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error enabling SD Graphics Plane. Error code = %d\n", result);
        return kMspStatus_Error;
    }
    pSD->Release(pSD);




    clearSurfaceFromLayer(VANTAGE_SD_VIDEO);

    // create and clear HD closed caption layer
    result = dfb->GetDisplayLayer(dfb, VANTAGE_HD_CC, &pHDClosedCaptionDisplayLayer);

    if ((result != DFB_OK) || (pHDClosedCaptionDisplayLayer == NULL))
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error inTEXT getting Display Layer interface. Error code %d ", result);
        return kMspStatus_AvpmError;
    }

    result = pHDClosedCaptionDisplayLayer->SetCooperativeLevel(pHDClosedCaptionDisplayLayer, DLSCL_ADMINISTRATIVE);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error inTEXT setting Cooperative. Error code = %d", result);
    }

    result = pHDClosedCaptionDisplayLayer->SetScreenLocation(pHDClosedCaptionDisplayLayer, 0.1, 0.1, 0.8, 0.8);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error inTEXT setting Screen Location. Error code = %d", result);
    }

    memset(&dlc, 0, sizeof(dlc));
    dlc.flags = DFBDisplayLayerConfigFlags(DLCONF_WIDTH | DLCONF_HEIGHT | DLCONF_PIXELFORMAT | DLCONF_BUFFERMODE  | DLCONF_SURFACE_CAPS);
    dlc.width  = 682;
    dlc.height = 384;
    dlc.pixelformat = DSPF_ARGB;
    dlc.buffermode = DLBM_BACKVIDEO;
    dlc.surface_caps = DSCAPS_VIDEOONLY;

    result = pHDClosedCaptionDisplayLayer->SetConfiguration(pHDClosedCaptionDisplayLayer, &dlc);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in displayLayer->SetConfiguration. Error code = %d", result);
    }

    result = pHDClosedCaptionDisplayLayer->GetSurface(pHDClosedCaptionDisplayLayer, &pHdClosedCaptionTextSurface);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in displayLayer->GetSurface. Error code = %d", result);
    }

    clearSurfaceFromLayer(VANTAGE_HD_CC);

    if (IsDvrSupported())
    {
        LOG(DLOGL_NORMAL, "Creating HD and SD GFX handles, as this is a 8k box");
        setupSDPIPPOPLayer();
        setupHDPIPPOPLayer();
    }
    else
    {
        LOG(DLOGL_NORMAL, "NOT Creating HD and SD GFX handles, as this is a 4k box");
    }

    logLayerInfo(VANTAGE_HD_MAINAPP);
    logLayerInfo(VANTAGE_SD_MAINAPP);
    if (IsDvrSupported())
    {
        logLayerInfo(VANTAGE_HD_PIP);
        logLayerInfo(VANTAGE_SD_PIP);
    }

    logLayerInfo(VANTAGE_HD_CC);

    logLayerInfo(VANTAGE_HD_VIDEO);
    logLayerInfo(VANTAGE_SD_VIDEO);

    return kMspStatus_Ok;
}
void Avpm::ClearCCsurface()
{
    LOG(DLOGL_NORMAL, "G6 Clearing of No Window pgm!!!");
}


void Avpm::ConfigureEncoderSettings(DFBScreenEncoderConfig *enc_descriptions, eAvpmResolution mode)
{
    FNLOG(DL_MSP_AVPM);
    enc_descriptions->flags = DFBScreenEncoderConfigFlags(DSECONF_RESOLUTION | DSECONF_SCANMODE);
    switch (mode)
    {
    case tAvpmResolution_480i:
        enc_descriptions->resolution = DSOR_720_480;
        enc_descriptions->scanmode = DSESM_INTERLACED;
        break;
    case tAvpmResolution_480p:
        enc_descriptions->resolution = DSOR_720_480;
        enc_descriptions->scanmode = DSESM_PROGRESSIVE;
        break;
    case tAvpmResolution_720p:
        enc_descriptions->resolution = DSOR_1280_720;
        enc_descriptions->scanmode = DSESM_PROGRESSIVE;
        break;
    case tAvpmResolution_1080i:
        enc_descriptions->resolution = DSOR_1920_1080;
        enc_descriptions->scanmode = DSESM_INTERLACED;
        break;
    case tAvpmResolution_1080p:
        LOG(DLOGL_MINOR_DEBUG, "Setting resolution type to progressive. Setting o/p freq: %d ratio: %d", DSEF_24HZ, DSOSB_16x9);
        enc_descriptions->flags = DFBScreenEncoderConfigFlags(enc_descriptions->flags | DSECONF_FREQUENCY | DSECONF_SLOW_BLANKING);
        enc_descriptions->slow_blanking = DSOSB_16x9;
        enc_descriptions->frequency = DSEF_24HZ;    // G6 supports only 24Hz and 30Hz with 1080p. More displays support 24Hz.
        enc_descriptions->resolution = DSOR_1920_1080;
        enc_descriptions->scanmode = DSESM_PROGRESSIVE;
        break;
    default:
        LOG(DLOGL_ERROR, "unsupported resolution type");
        break;
    }
}

void Avpm::DisplayCallback_HD(void                    *ctx,
                              tCpeVshDisplayToFlags   *flags,
                              tCpeVshFrameAttributes  *frameAttrib,
                              IDirectFBSurface        *frameSurface,
                              IDirectFBSurface        *altSurface,
                              DFBDimension            screenSize,
                              tCpeVshScaleRects       *scaleRects)
{
    FNLOG(DL_MSP_AVPM);
    float newAspect = 0.0;
    Avpm *inst = (Avpm*)getAvpmInstance();
    tCpePgrmHandle temp_handle = (tCpePgrmHandle)ctx;
    if (frameAttrib)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d ctx %p", __FUNCTION__, __LINE__, ctx);

        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d Callback flags %x surf %p altsurf %p,scrw %d scrh %d", __FUNCTION__, __LINE__,
             *flags, frameSurface, altSurface, screenSize.w, screenSize.h);

        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d attribe flags %x stream flags %x type %x", __FUNCTION__, __LINE__,
             frameAttrib->flags, frameAttrib->stream.flags, frameAttrib->frameType);

        logScalingRects("Current presentation params at  DisplayCallback_HD", *scaleRects);

        if (temp_handle == inst->mMainScreenPgrHandle)
        {
            inst->threadEventQueue->dispatchEvent(kAvpmApplyHDMainPresentationParams);
        }
        else
        {
            inst->threadEventQueue->dispatchEvent(kAvpmApplyHDPipPresentationParams);
        }

        newAspect = frameAttrib->stream.aspect;
    }
    else
    {
        newAspect = 0.0f;
    }

    if (newAspect != frame_asp)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "Updated AR old %f new %f ", frame_asp, newAspect);
        if (inst->threadEventQueue != NULL)
        {
            inst->threadEventQueue->dispatchEvent(kAvpmStreamAspectChanged);
        }
        frame_asp = newAspect;
    }

}
int Avpm::GetVshDisplayToFlags(void)
{

    return (tCpeVshDisplayToFlags_OnAspect);
}

void Avpm::DisplayCallback_SD(void *ctx,
                              tCpeVshDisplayToFlags *flags,
                              tCpeVshFrameAttributes *frameAttrib,
                              IDirectFBSurface *frameSurface,
                              IDirectFBSurface *altSurface,
                              DFBDimension screenSize,
                              tCpeVshScaleRects *scaleRects)
{
    UNUSED_PARAM(ctx);
    UNUSED_PARAM(flags);
    UNUSED_PARAM(altSurface);
    UNUSED_PARAM(screenSize);
    UNUSED_PARAM(scaleRects);
    float newAspect = 0.0f;
    Avpm *inst = (Avpm*)getAvpmInstance();
    tCpePgrmHandle temp_handle = (tCpePgrmHandle)ctx;

    if (frameAttrib)
    {
        FNLOG(DL_MSP_AVPM);
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d ctx %p", __FUNCTION__, __LINE__, ctx);

        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d Callback flags %x surf %p altsurf %p,scrw %d scrh %d", __FUNCTION__, __LINE__,
             *flags, frameSurface, altSurface, screenSize.w, screenSize.h);

        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d attribe flags %x stream flags %x type %x", __FUNCTION__, __LINE__,
             frameAttrib->flags, frameAttrib->stream.flags, frameAttrib->frameType);

        logScalingRects("Current presentation params at DisplayCallback_SD ", *scaleRects);

        if (temp_handle == inst->mMainScreenPgrHandle)
        {
            inst->threadEventQueue->dispatchEvent(kAvpmApplySDMainPresentationParams);
        }
        else
        {
            inst->threadEventQueue->dispatchEvent(kAvpmApplySDPipPresentationParams);
        }

        newAspect = frameAttrib->stream.aspect;
    }
    else
    {
        newAspect = 0.0f;
    }

    if (newAspect != frame_asp)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "Updated AR old %f new %f ", frame_asp, newAspect);
        if (inst->threadEventQueue != NULL)
        {
            inst->threadEventQueue->dispatchEvent(kAvpmStreamAspectChanged);
        }
        frame_asp = newAspect;
    }

}

eMspStatus Avpm::setCcEdge(char* cc_char_edge, tCpeTshEdgeEffect *aCharEdge)
{
    LOG(DLOGL_NOISE, "ccCharacterEdge: %s", cc_char_edge);
    if (strcmp(cc_char_edge, "raisedEdges") == 0)
    {
        *aCharEdge = tCpeTshEdgeEffect_Raised;
    }
    else if (strcmp(cc_char_edge, "depressedEdges") == 0)
    {
        *aCharEdge = tCpeTshEdgeEffect_Depressed;
    }
    else if (strcmp(cc_char_edge, "outline") == 0)
    {
        *aCharEdge = tCpeTshEdgeEffect_Outline;
    }
    else if (strcmp(cc_char_edge, "leftDropShadowedEdges") == 0)
    {
        *aCharEdge = tCpeTshEdgeEffect_LeftDropShadow;
    }
    else if (strcmp(cc_char_edge, "rightDropShadowedEdges") == 0)
    {
        *aCharEdge = tCpeTshEdgeEffect_RightDropShadow;
    }
    else if (strcmp(cc_char_edge, "uniformEdges") == 0)
    {
        *aCharEdge = tCpeTshEdgeEffect_Uniform;
    }
    else
    {
        *aCharEdge = tCpeTshEdgeEffect_None;
    }
    return kMspStatus_Ok;
}

eMspStatus Avpm::closedCaption(tCpePgrmHandle pgrHandle, eAvpmEvent cc_event, char *pValue)
{

    FNLOG(DL_MSP_AVPM);

    if (dfb == NULL)
    {
        LOG(DLOGL_ERROR, "Error null dfb");
        return kMspStatus_AvpmError;
    }

    eUseSettingsLevel settingLevel;
    char ccAnalogSetting[MAX_SETTING_VALUE_SIZE] = {0};
    // Store the user preference for digital and analog source
    // so that a call to unified setting is not required on every channel
    // change
    if (cc_event == kAvpmDigitalCCEnable)
    {
        strncpy(mccUserDigitalSetting, pValue, MAX_SETTING_VALUE_SIZE);
        mccUserDigitalSetting[MAX_SETTING_VALUE_SIZE - 1] = '\0';
    }
    else if (0 == strlen(mccUserDigitalSetting))
    {
        Uset_getSettingT(NULL, "ciscoSg/cc/ccSourceDigital", MAX_SETTING_VALUE_SIZE, mccUserDigitalSetting, &settingLevel);
    }

    if (cc_event == kAvpmAnalogCCEnable)
    {
        strncpy(ccAnalogSetting, pValue, MAX_SETTING_VALUE_SIZE);
        ccAnalogSetting[MAX_SETTING_VALUE_SIZE - 1] = '\0';
    }
    else
    {
        Uset_getSettingT(NULL, "ciscoSg/cc/ccSourceAnalog", MAX_SETTING_VALUE_SIZE, ccAnalogSetting, &settingLevel);
    }


    memset(&mStreamAttrib, 0, sizeof(mStreamAttrib));

    mStreamAttrib.flags = tCpeTshAttribFlags(mStreamAttrib.flags | eCpeTshAttribFlag_AnalogService);

    if (strncmp(ccAnalogSetting, "cc", 2))      // true if t1-t4
    {
        mStreamAttrib.analogService = tCpeTshAnalogService(atoi(&ccAnalogSetting[1]) + 3);
    }
    else
    {
        mStreamAttrib.analogService = tCpeTshAnalogService(atoi(&ccAnalogSetting[2]) - 1);
    }

    mStreamAttrib.flags = tCpeTshAttribFlags(mStreamAttrib.flags | eCpeTshAttribFlag_DigitalService);
    mStreamAttrib.digitalService = tCpeTshDigitalService(getDigitalLangToServiceMap());

    char ccSetByProgram[MAX_SETTING_VALUE_SIZE] = {0};

    if (cc_event == kAvpmCCSetByProgram)
    {
        LOG(DLOGL_REALLY_NOISY, "Setting changed is set by program value %s", pValue);
        strncpy(ccSetByProgram, pValue, MAX_SETTING_VALUE_SIZE);
        ccSetByProgram[MAX_SETTING_VALUE_SIZE - 1] = '\0';
    }
    else
    {
        Uset_getSettingT(NULL, "ciscoSg/cc/ccSetByProgram", MAX_SETTING_VALUE_SIZE, ccSetByProgram, &settingLevel);
        LOG(DLOGL_REALLY_NOISY, "CC Read from unified setting %s\n", ccSetByProgram);
    }

    if (strcmp(ccSetByProgram, "2"))
    {
        LOG(DLOGL_NORMAL, "CC set by viewer");

        char charColor[MAX_SETTING_VALUE_SIZE];
        char penSize[MAX_SETTING_VALUE_SIZE];
        char charStyle[MAX_SETTING_VALUE_SIZE];
        char charFontFace[MAX_SETTING_VALUE_SIZE];
        char charEdge[MAX_SETTING_VALUE_SIZE];
        char windowStyle[MAX_SETTING_VALUE_SIZE];
        char windowColor[MAX_SETTING_VALUE_SIZE];
        char txtBackgroundColor[MAX_SETTING_VALUE_SIZE];
        char txtBackgroundStyle[MAX_SETTING_VALUE_SIZE];


        if (cc_event == kAvpmCCCharColor)
        {
            strncpy(charColor, pValue, MAX_SETTING_VALUE_SIZE);
            charColor[MAX_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccCharacterColor", MAX_SETTING_VALUE_SIZE, charColor, &settingLevel);
        }

        if (cc_event == kAvpmCCPenSize)
        {
            strncpy(penSize, pValue, MAX_SETTING_VALUE_SIZE);
            penSize[MAX_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccCharacterSize", MAX_SETTING_VALUE_SIZE, penSize, &settingLevel);
        }

        if (cc_event == kAvpmCCCharStyle)
        {
            strncpy(charStyle, pValue, MAX_SETTING_VALUE_SIZE);
            charStyle[MAX_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccCharacterStyle", MAX_SETTING_VALUE_SIZE, charStyle, &settingLevel);
        }

        if (cc_event == kAvpmCCCharFont)
        {
            strncpy(charFontFace, pValue, MAX_SETTING_VALUE_SIZE);
            charFontFace[MAX_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccCharacterFont", MAX_SETTING_VALUE_SIZE, charFontFace, &settingLevel);
        }

        if (cc_event == kAvpmCCCharEdge)
        {
            strncpy(charEdge, pValue, MAX_SETTING_VALUE_SIZE);
            charEdge[MAX_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccCharacterEdge", MAX_SETTING_VALUE_SIZE, charEdge, &settingLevel);
        }

        if (cc_event == kAvpmCCWindowColor)
        {
            strncpy(windowColor, pValue, MAX_SETTING_VALUE_SIZE);
            windowColor[MAX_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccWindowColor", MAX_SETTING_VALUE_SIZE, windowColor, &settingLevel);
        }

        if (cc_event == kAvpmCCWindowStyle)
        {
            strncpy(windowStyle, pValue, MAX_SETTING_VALUE_SIZE);
            windowStyle[MAX_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccWindowStyle", MAX_SETTING_VALUE_SIZE, windowStyle, &settingLevel);
        }

        if (cc_event == kAvpmBackgroundColor)
        {
            strncpy(txtBackgroundColor, pValue, MAX_SETTING_VALUE_SIZE);
            txtBackgroundColor[MAX_SETTING_VALUE_SIZE - 1] = '\0';
            LOG(DLOGL_NORMAL, "txtBackgroundColor:%s ", txtBackgroundColor);
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccBgColor", MAX_SETTING_VALUE_SIZE, txtBackgroundColor, &settingLevel);
            LOG(DLOGL_NORMAL, "txtBackgroundColor:%s ", txtBackgroundColor);
        }

        if (cc_event == kAvpmBackgroundStyle)
        {
            strncpy(txtBackgroundStyle, pValue, MAX_SETTING_VALUE_SIZE);
            txtBackgroundStyle[MAX_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccBgStyle", MAX_SETTING_VALUE_SIZE, txtBackgroundStyle, &settingLevel);
        }

        //Use the stream properties.
        mStreamAttrib.flags = tCpeTshAttribFlags(mStreamAttrib.flags | eCpeTshAttribFlag_TxtColor | eCpeTshAttribFlag_PenSize | eCpeTshAttribFlag_WinColor | eCpeTshAttribFlag_WinOpacity | eCpeTshAttribFlag_TextOpacity | eCpeTshAttribFlag_FontFace | eCpeTshAttribFlag_EdgeEffect | eCpeTshAttribFlag_TextBgColor | eCpeTshAttribFlag_TextBgOpacity);
        //Setting character color
        setccColor(charColor, &(mStreamAttrib.txtColor));
        //Setting text background color
        setccColor(txtBackgroundColor, &(mStreamAttrib.txtBgColor));
        //Setting text back ground style
        setccStyle(txtBackgroundStyle, &(mStreamAttrib.txtBgOpacity));
        // Setting window color
        setccColor(windowColor, &(mStreamAttrib.winColor));
        // Setting window opacity
        setccStyle(windowStyle, &(mStreamAttrib.winOpacity));
        //Setting Character Size
        setccPenSize(penSize, &(mStreamAttrib.penSize));
        //Setting Character style
        setccStyle(charStyle, &(mStreamAttrib.txtOpacity));
        //Setting Font face
        setccFontFace(charFontFace, &(mStreamAttrib.fontFace));
        //Set CC Edge
        setCcEdge(charEdge, &(mStreamAttrib.edgeEffect));
    }
    else
    {
        LOG(DLOGL_NORMAL, "CC set by program");
    }

    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(pgrHandle);
    if (!pgrHandleSetting)
    {
        LOG(DLOGL_ERROR, "Error null pgrHandleSetting");

        return kMspStatus_AvpmError;
    }
    LOG(DLOGL_REALLY_NOISY, "Enable Closed Captions pgrHandleSetting->cchandle %d", pgrHandleSetting->cchandle);

    //Restarting cc to apply user requested settings, if cc is going on
    if (pgrHandleSetting->tsh != NULL && (pgrHandleSetting->cchandle != 0))
    {
        if (!isPreviewEnabled)
        {
            DFBResult result = pgrHandleSetting->tsh->Stop(pgrHandleSetting->tsh, pgrHandleSetting->cchandle);
            if (result != DFB_OK)
            {
                LOG(DLOGL_ERROR, "Call tsh->Stop HD error %d", result);
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "Disabled CC Success to apply user updated settings");
                pgrHandleSetting->cchandle = 0;
            }

            clearSurfaceFromLayer(VANTAGE_HD_CC);
            result = pgrHandleSetting->tsh->RenderTo(pgrHandleSetting->tsh, pHdClosedCaptionTextSurface, &mStreamAttrib, &pgrHandleSetting->cchandle);

            if (DFB_OK != result)
            {
                LOG(DLOGL_ERROR, "Error RenderTo error: %d", result);
            }
            else
            {
                LOG(DLOGL_NOISE, "RenderTo Success!");
                pgrHandleSetting->cchandle = 1;
            }
        }
        else
        {
            LOG(DLOGL_NORMAL, "Preview window is enabled %d , Not Rendering cc", isPreviewEnabled);
        }
    }

    return kMspStatus_Ok;
}


eMspStatus Avpm::showPreviewSample(bool setSamplePreview)
{
    DFBResult result;
    eMspStatus retStatus = kMspStatus_Ok;
    unsigned char sampleText[MAX_SAMPLE_TEXT_LENGTH];
    unsigned int sampleTextLen = 0;

    memset(sampleText, 0x0, MAX_SAMPLE_TEXT_LENGTH);

    Avpm *inst = (Avpm*)getAvpmInstance();
    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(inst->mMainScreenPgrHandle);
    if (!pgrHandleSetting)
    {
        LOG(DLOGL_ERROR, "Error null pgrHandleSetting");
        return kMspStatus_AvpmError;
    }

    if (setSamplePreview == 1)
    {
        isPreviewEnabled = true;
        isCaptionEnabledBeforePreview = isClosedCaptionEnabled;
        LOG(DLOGL_REALLY_NOISY, "before preview isClosedCaptionEnabled =%d isCaptionEnabledBeforePreview %d", isClosedCaptionEnabled, isCaptionEnabledBeforePreview);

        LOG(DLOGL_NORMAL, "Stopping the caption rendering before preview");
        setCc(false);

        LOG(DLOGL_NORMAL, " Launching Preview for the requested configuration");

        LOG(DLOGL_REALLY_NOISY, " Dump of settings applied for preview");
        LOG(DLOGL_REALLY_NOISY, " Tsh flag value is %x", mPreviewStreamAttrib.flags);
        LOG(DLOGL_REALLY_NOISY, " Tsh opacity value is %x", mPreviewStreamAttrib.txtOpacity);
        LOG(DLOGL_REALLY_NOISY, " Window opacity value is %x", mPreviewStreamAttrib.winOpacity);
        LOG(DLOGL_REALLY_NOISY, " Edge effect  is %x", mPreviewStreamAttrib.edgeEffect);
        LOG(DLOGL_REALLY_NOISY, " Pen size   is %x", mPreviewStreamAttrib.penSize);

        if (pgrHandleSetting->tsh != NULL && pgrHandleSetting->cchandle != 1)
        {
            clearSurfaceFromLayer(VANTAGE_HD_CC);
            result = (pgrHandleSetting->tsh)->RenderTo(pgrHandleSetting->tsh, pHdClosedCaptionTextSurface, &mPreviewStreamAttrib, &pgrHandleSetting->cchandle);
            if (DFB_OK != result)
            {
                LOG(DLOGL_ERROR, "RenderTo error %d ...", result);
                retStatus = kMspStatus_AvpmError;
            }
            else
            {
                LOG(DLOGL_NOISE, "RenderTo Success!");
                pgrHandleSetting->cchandle = 1;
            }
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "CC is in Rendering State");
        }

        strcpy((char *)sampleText, "Closed Caption Sample Text");
        sampleTextLen = strlen((char *)sampleText);

        if (pgrHandleSetting->tsh != NULL)
        {
            result = (pgrHandleSetting->tsh)->RenderSample(pgrHandleSetting->tsh, sampleText, true, G6_PREVIEW_POS_X, G6_PREVIEW_POS_Y);
            if (DFB_OK != result)
            {
                LOG(DLOGL_ERROR, "RenderSample error %d ...", result);
                retStatus = kMspStatus_AvpmError;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "RenderSample Failed pgrHandleSetting->tsh is NULL");
        }
    }
    else
    {
        LOG(DLOGL_NORMAL, " Stopping Preview");
        isPreviewEnabled = false;
        if (pgrHandleSetting->tsh != NULL && (pgrHandleSetting->cchandle != 0))
        {
            result = (pgrHandleSetting->tsh)->Stop(pgrHandleSetting->tsh, pgrHandleSetting->cchandle);
            ClearCCsurface();
            if (DFB_OK != result)
            {
                LOG(DLOGL_ERROR, "Stop error %d  ...", result);
                retStatus = kMspStatus_AvpmError;
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "Sample text Disable Success");
                pgrHandleSetting->cchandle = 0;
            }
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, " Sample text is not rendering");
        }

    }

    LOG(DLOGL_REALLY_NOISY, "isCaptionEnabledBeforePreview =%d", isCaptionEnabledBeforePreview);
    if (isCaptionEnabledBeforePreview)
    {
        setCc(true);
        LOG(DLOGL_REALLY_NOISY, "enabled Closed Caption");
    }

    return retStatus;
}

bool Avpm::doesTVSupport1080p(const tCpeAvoxDigDispInfo &dispInfo)
{
    FNLOG(DL_MSP_AVPM);

    // G6 supports only 24Hz and 30Hz output frequency with 1080p, only 24Hz will be used here.
    // Testing multiple TVs, it is found that more TVs support 1080p 24Hz than 1080p 30Hz.
    // So, 1080p 24Hz is considered standard for G6.
    // If TV supports 1080p 24Hz, return true. Else, log it and return false.
    // Few TVs support only 1080p 60Hz and do not support 1080p 24Hz/30Hz, G6 cannot support 1080p resolution for such TVs.
    LOG(DLOGL_NORMAL, "1080p 30Hz: %s",
        ((dispInfo.explicitModes & tCpeAvoxVideoOutputModes_1080p30_16x9) == tCpeAvoxVideoOutputModes_1080p30_16x9) ? "YES" : "NO");
    if ((dispInfo.explicitModes & tCpeAvoxVideoOutputModes_1080p24_16x9) == tCpeAvoxVideoOutputModes_1080p24_16x9)
    {
        LOG(DLOGL_NORMAL, "1080p 24Hz: YES");
    }
    else
    {
        LOG(DLOGL_ERROR, "1080p 24Hz: NO");
        return false;
    }
    return true;
}
