
/**
   \file shim-CC-cperpv2.cpp
   Implementation file for shim layer for directfb specific implementations for G8

   CC - closed caption(Subtitle)
*/

#include <sawman/sawman.h>
#include "avpm.h"
#include "sail_dfb.h"
#include <dlog.h>
#include <map>
#include <utility>
#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_AVPM, level,"Avpm:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#define UNHIDE_VIDEO 0x40000
#define G8_PREVIEW_POS_Y 570

#define G8_PREVIEW_LARGE_POS_X 450
#define G8_PREVIEW_LARGE_WIN_WIDTH 580
#define G8_PREVIEW_LARGE_WIN_HEIGHT 50

#define G8_PREVIEW_SMALL_POS_X 490
#define G8_PREVIEW_SMALL_WIN_WIDTH 380
#define G8_PREVIEW_SMALL_WIN_HEIGHT 30

#define G8_PREVIEW_STANDARD_POS_X 485
#define G8_PREVIEW_STANDARD_WIN_WIDTH 480
#define G8_PREVIEW_STANDARD_WIN_HEIGHT 50

using namespace std;
static ISaWMan *pSawman = NULL;
static ISaWManManager *pManager = NULL;


extern bool IsDvrSupported();
static DirectResult layer_reconfig(void                *context,
                                   SaWManLayerReconfig *reconfig)
{
    D_ASSERT(reconfig);
    (void)context;
    dlog(DL_MSP_AVPM, DLOGL_NORMAL, "%s Layer (%d) reconfig requested layer size: %dx%d\n", __FUNCTION__, reconfig->layer_id,
         reconfig->config.width, reconfig->config.height);
#if 1
    if (reconfig->layer_id == VANTAGE_HD_CC)
    {
        // If this is the main app layer, force it to 640x480
        reconfig->config.width  = 1280;
        reconfig->config.height = 720;

        // Force to stereo
        // reconfig->config.surface_caps |= DSCAPS_STEREO;
    }
#endif
    return DR_OK;
}

static SaWManCallbacks callbacks;

DirectResult Avpm::app_dfb_SaWManInit(int argc, char** argv)
{
    DirectResult ret;

    dlog(DL_MSP_AVPM, DLOGL_FUNCTION_CALLS, "Enter %s :", __FUNCTION__);

    callbacks.LayerReconfig = layer_reconfig;

    if ((ret = SaWManInit(&argc, &argv)) == DR_OK)
    {
        if ((ret = SaWManCreate(&pSawman)) == DR_OK)
        {
            if ((ret = pSawman->CreateManager(pSawman, (const SaWManCallbacks *)&callbacks, NULL, &pManager)) == DR_OK)
            {
                dlog(DL_MSP_AVPM, DLOGL_NORMAL, "SaWMan Manager Created!");
            }
            else
            {
                dlog(DL_MSP_AVPM, DLOGL_ERROR, "SaWMan Manager Creation failed! (0x%x)", ret);
                pSawman->Release(pSawman);
            }
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "SaWMan interface Creation failed! (0x%x)", ret);
        }
    }
    dlog(DL_MSP_AVPM, DLOGL_FUNCTION_CALLS, "Exit %s :", __FUNCTION__);
    return (ret);
}


int Avpm::app_dfb_SaWManUninit(void)
{
    dlog(DL_MSP_AVPM, DLOGL_FUNCTION_CALLS, "Enter %s :", __FUNCTION__);

    if (pManager)
        pManager->Release(pManager);
    if (pSawman)
        pSawman->Release(pSawman);
    dlog(DL_MSP_AVPM, DLOGL_FUNCTION_CALLS, "Exit %s :", __FUNCTION__);
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
    IDirectFBWindow  * ccwindow = NULL;
    DFBWindowDescription wind_dsc;


    FNLOG(DL_MSP_AVPM);

    result = DirectFBCreate(&dfb);
    if ((result != DFB_OK) || (dfb == NULL))
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error: DirectFBCreate Failed\n");
        return kMspStatus_Error;
    }

    DirectResult ret = app_dfb_SaWManInit(0, NULL);
    LOG(DLOGL_NORMAL, "SawManInit result %d", ret);

    result = dfb->GetDisplayLayer(dfb, VANTAGE_HD_VIDEO, &pHDLayer);  // TODO rename as HDVideo Layer
    LOG(DLOGL_NORMAL, "result %d", result);
    if ((result != DFB_OK) || (pHDLayer == NULL))
    {

        return kMspStatus_AvpmError;
    }

    // Get HD Screen
    result = dfb->GetScreen(dfb, eCpeDFBScreenIndex_HD, &pHD);
    LOG(DLOGL_NORMAL, "result %d", result);
    if ((result != DFB_OK) || (pHD == NULL))
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in getting DFB HD screen interface. Error code = %d\n", result);
        return kMspStatus_Error;
    }


    pHD->GetSize(pHD, &mMaxOutputScreenWidthHD, &mMaxOutputScreenHeightHD);
    dlog(DL_MSP_AVPM, DLOGL_NORMAL, "HD Screen  w=%d, h=%d\n", mMaxOutputScreenWidthHD, mMaxOutputScreenHeightHD);

    conf.flags = (DFBScreenMixerConfigFlags)(DSMCONF_BACKGROUND | DSMCONF_LAYERS);
    conf.background.a = 0xff;
    conf.background.r = conf.background.g = conf.background.b = 0x00;
    DFB_DISPLAYLAYER_IDS_EMPTY(conf.layers);
    DFB_DISPLAYLAYER_IDS_ADD(conf.layers, VANTAGE_HD_VIDEO);
    DFB_DISPLAYLAYER_IDS_ADD(conf.layers, VANTAGE_HD_MAINAPP);
    LOG(DLOGL_NORMAL, "result %d", result);
    if (IsDvrSupported())
    {
        DFB_DISPLAYLAYER_IDS_ADD(conf.layers, VANTAGE_HD_PIP);
    }
    LOG(DLOGL_NORMAL, "result %d", result);
    DFB_DISPLAYLAYER_IDS_ADD(conf.layers, VANTAGE_HD_CC);

    result = pHD->SetMixerConfiguration(pHD, 0, &conf);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error enabling HD Graphics Plane. Error code = %d\n", result);
        return kMspStatus_Error;
    }

    pHD->Release(pHD);
    LOG(DLOGL_NORMAL, "result %d", result);

    clearSurfaceFromLayer(VANTAGE_HD_VIDEO);

    result = dfb->GetDisplayLayer(dfb, VANTAGE_SD_VIDEO, &pSDLayer);    // TODO: ditto
    if ((result != DFB_OK) || (pSDLayer == NULL))
    {
        return kMspStatus_AvpmError;
    }
    LOG(DLOGL_NORMAL, "result %d", result);
    // Get SD Screen
    result = dfb->GetScreen(dfb, eCpeDFBScreenIndex_SD, &pSD);
    if ((result != DFB_OK) || (pSD == NULL))
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in getting DFB SD screen interface. Error code = %d\n", result);
        return kMspStatus_Error;
    }

    pSD->GetSize(pSD, &mMaxOutputScreenWidthSD, &mMaxOutputScreenHeightSD);

    dlog(DL_MSP_AVPM, DLOGL_NORMAL, "SD Screen  w=%d, h=%d\n", mMaxOutputScreenWidthSD, mMaxOutputScreenHeightSD);

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
    LOG(DLOGL_NORMAL, "result %d", result);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error enabling SD Graphics Plane. Error code = %d\n", result);
        return kMspStatus_Error;
    }
    pSD->Release(pSD);
    LOG(DLOGL_NORMAL, "result %d", result);

    clearSurfaceFromLayer(VANTAGE_SD_VIDEO);

    // create and clear HD closed caption layer

    result = dfb->GetDisplayLayer(dfb, VANTAGE_HD_CC, &pHDClosedCaptionDisplayLayer);
    LOG(DLOGL_NORMAL, "result %d", result);

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
    LOG(DLOGL_NORMAL, "result %d", result);
    memset(&dlc, 0, sizeof(dlc));
    dlc.flags = DFBDisplayLayerConfigFlags(DLCONF_WIDTH | DLCONF_HEIGHT | DLCONF_PIXELFORMAT | DLCONF_BUFFERMODE  | DLCONF_SURFACE_CAPS);
    dlc.width  = GALIO_MAX_WINDOW_WIDTH;
    dlc.height = GALIO_MAX_WINDOW_HEIGHT;
    dlc.pixelformat = DSPF_ARGB;
    dlc.buffermode = DLBM_BACKVIDEO;
    dlc.surface_caps = DSCAPS_VIDEOONLY;

    result = pHDClosedCaptionDisplayLayer->SetConfiguration(pHDClosedCaptionDisplayLayer, &dlc);
    LOG(DLOGL_NORMAL, "result %d", result);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in displayLayer->SetConfiguration. Error code = %d", result);
    }

    memset(&dlc, 0, sizeof(dlc));
    result = pHDClosedCaptionDisplayLayer->GetConfiguration(pHDClosedCaptionDisplayLayer, &dlc);
    LOG(DLOGL_NORMAL, "result %d", result);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in displayLayer->SetConfiguration. Error code = %d", result);

    }

    result = pHDClosedCaptionDisplayLayer->SetScreenLocation(pHDClosedCaptionDisplayLayer, 0, 0, 1.0, 1.0);
    LOG(DLOGL_NORMAL, "result %d", result);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error inTEXT setting Screen Location. Error code = %d", result);
    }

    wind_dsc.posx = dlc.width / 10;
    wind_dsc.posy = dlc.height / 10;
    wind_dsc.width  = dlc.width - (2 * wind_dsc.posx);
    wind_dsc.height = dlc.height - (2 * wind_dsc.posy);
    wind_dsc.flags = (DFBWindowDescriptionFlags)(DWDESC_WIDTH | DWDESC_HEIGHT | DWDESC_POSX | DWDESC_POSY | DWDESC_CAPS | DWDESC_PIXELFORMAT | DWDESC_SURFACE_CAPS);
    wind_dsc.caps = (DFBWindowCapabilities)(DWCAPS_ALPHACHANNEL | DWCAPS_DOUBLEBUFFER);
    wind_dsc.pixelformat = DSPF_ARGB;
    wind_dsc.surface_caps = DSCAPS_DOUBLE;

    result = pHDClosedCaptionDisplayLayer->SetCooperativeLevel(pHDClosedCaptionDisplayLayer, DLSCL_SHARED);
    LOG(DLOGL_NORMAL, "result %d", result);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error inTEXT setting Cooperative. Error code = %d", result);
    }

    result = pHDClosedCaptionDisplayLayer->CreateWindow(pHDClosedCaptionDisplayLayer, &wind_dsc, &ccwindow);
    LOG(DLOGL_NORMAL, "result %d", result);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in CreateWindow. Error code = %d", result);
    }
    if (NULL == ccwindow)
    {
        LOG(DLOGL_ERROR, "Error: ccwindow is NULL, returning from here");
        //If window creation failed, we must return.
        return kMspStatus_AvpmError;
    }
    result = ccwindow->GetSurface(ccwindow, &pHdClosedCaptionTextSurface);
    LOG(DLOGL_NORMAL, "result %d", result);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in ccwindow->GetSurface. Error code = %d", result);
    }

    pHdClosedCaptionTextSurface->Clear(pHdClosedCaptionTextSurface, 0, 0, 0, 0);
    pHdClosedCaptionTextSurface->Flip(pHdClosedCaptionTextSurface, NULL, DSFLIP_NONE);
    pHdClosedCaptionTextSurface->Clear(pHdClosedCaptionTextSurface, 0, 0, 0, 0);

    result = ccwindow->Raise(ccwindow);
    LOG(DLOGL_NORMAL, "result %d", result);
    result = ccwindow->SetOpacity(ccwindow, 0xff);
    LOG(DLOGL_NORMAL, "result %d", result);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in window2->SetOpacity. Error code = %d", result);
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
    LOG(DLOGL_NORMAL, "result %d", result);
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
    FNLOG(DL_MSP_AVPM);
    pHdClosedCaptionTextSurface->Clear(pHdClosedCaptionTextSurface, 0, 0, 0, 0);
    pHdClosedCaptionTextSurface->Flip(pHdClosedCaptionTextSurface, NULL, DSFLIP_NONE);
    pHdClosedCaptionTextSurface->Clear(pHdClosedCaptionTextSurface, 0, 0, 0, 0);
}



void Avpm::ConfigureEncoderSettings(DFBScreenEncoderConfig *enc_descriptions, eAvpmResolution mode)
{
    FNLOG(DL_MSP_AVPM);

    enc_descriptions->flags = (DFBScreenEncoderConfigFlags)(DSECONF_TV_STANDARD | DSECONF_RESOLUTION |
                              DSECONF_SCANMODE | DSECONF_FREQUENCY |
                              DSECONF_FRAMING |  DSECONF_SLOW_BLANKING);
    enc_descriptions->tv_standard = DSETV_DIGITAL;
    if (user_aspect_ratio == tAvpmTVAspectRatio4x3)
        enc_descriptions->slow_blanking = DSOSB_4x3;
    else
        enc_descriptions->slow_blanking = DSOSB_16x9;

    enc_descriptions->frequency  = DSEF_59_94HZ;
    enc_descriptions->framing = DSEPF_MONO;

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

        logScalingRects("Before Current presentation params at  DisplayCallback_HD", *scaleRects);

        newAspect = frameAttrib->stream.aspect;
        frame_asp = newAspect;

        if (temp_handle == inst->mMainScreenPgrHandle)
        {

            ProgramHandleSetting* pgrHandleSetting = inst->getProgramHandleSettings(inst->mMainScreenPgrHandle);
            inst->setPictureMode(pgrHandleSetting->vsh, pgrHandleSetting->hdHandle, pgrHandleSetting->sdHandle, kAvpmHDCpy);
            memcpy(scaleRects, &HDRects, sizeof(tCpeVshScaleRects));
            *flags = (tCpeVshDisplayToFlags)(*flags & ~UNHIDE_VIDEO);
            inst->threadEventQueue->dispatchEvent(kAvpmApplyHDMainPresentationParams);
        }
        else
        {
            *flags = (tCpeVshDisplayToFlags)(*flags & ~UNHIDE_VIDEO);
            inst->threadEventQueue->dispatchEvent(kAvpmApplyHDPipPresentationParams);
        }

    }
    else
    {
        newAspect = 0.0f;
    }

    if (newAspect != frame_asp)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%d HD Updated AR old %f new %f ", __LINE__, frame_asp, newAspect);
        if (inst->threadEventQueue != NULL)
        {

            inst->threadEventQueue->dispatchEvent(kAvpmHDStreamAspectChanged);
        }
    }
}



int Avpm::GetVshDisplayToFlags(void)
{

    return (tCpeVshDisplayToFlags_OnAspect | tCpeVshDisplayToFlags_Hide);
}


void Avpm::DisplayCallback_SD(void *ctx,
                              tCpeVshDisplayToFlags *flags,
                              tCpeVshFrameAttributes *frameAttrib,
                              IDirectFBSurface *frameSurface,
                              IDirectFBSurface *altSurface,
                              DFBDimension screenSize,
                              tCpeVshScaleRects *scaleRects)
{
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

        logScalingRects("Before Current presentation params at DisplayCallback_SD ", *scaleRects);
        newAspect = frameAttrib->stream.aspect;
        frame_asp = newAspect;


        if (temp_handle == inst->mMainScreenPgrHandle)
        {

            ProgramHandleSetting* pgrHandleSetting = inst->getProgramHandleSettings(inst->mMainScreenPgrHandle);
            inst->setPictureMode(pgrHandleSetting->vsh, pgrHandleSetting->hdHandle, pgrHandleSetting->sdHandle, kAvpmSDCpy);
            memcpy(scaleRects, &SDRects, sizeof(tCpeVshScaleRects));
            *flags = (tCpeVshDisplayToFlags)(*flags & ~UNHIDE_VIDEO);
            inst->threadEventQueue->dispatchEvent(kAvpmApplySDMainPresentationParams);
        }
        else
        {
            *flags = (tCpeVshDisplayToFlags)(*flags & ~UNHIDE_VIDEO);
            inst->threadEventQueue->dispatchEvent(kAvpmApplySDPipPresentationParams);

        }

    }
    else
    {
        newAspect = 0.0f;
    }

    if (newAspect != frame_asp)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%d SD Updated AR old %f new %f ", __LINE__, frame_asp, newAspect);
        if (inst->threadEventQueue != NULL)
        {

            inst->threadEventQueue->dispatchEvent(kAvpmSDStreamAspectChanged);
        }

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
        *aCharEdge = tCpeTshEdgeEffect_DropShadowLeft;
    }
    else if (strcmp(cc_char_edge, "rightDropShadowedEdges") == 0)
    {
        *aCharEdge = tCpeTshEdgeEffect_DropShadowRight;
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
    tCpeMshTextStreamAttributes attrib_text;
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
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccBgColor", MAX_SETTING_VALUE_SIZE, txtBackgroundColor, &settingLevel);
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

        mStreamAttrib.flags = tCpeTshAttribFlags(mStreamAttrib.flags | eCpeTshAttribFlag_TxtColor | eCpeTshAttribFlag_PenSize | eCpeTshAttribFlag_TextBgColor | eCpeTshAttribFlag_TextBgOpacity | eCpeTshAttribFlag_WinColor | eCpeTshAttribFlag_WinOpacity | eCpeTshAttribFlag_TextOpacity | eCpeTshAttribFlag_FontFace | eCpeTshAttribFlag_EdgeEffect);

        //Setting text background color
        setccColor(txtBackgroundColor, &(mStreamAttrib.txtBgColor));
        //Setting wind color
        setccColor(windowColor, &(mStreamAttrib.winColor));
        //Setting CC character colour
        setccColor(charColor, &(mStreamAttrib.txtColor));
        //Setting CC Font Face
        setccFontFace(charFontFace, &(mStreamAttrib.fontFace));
        //Setting Charater Size
        setccPenSize(penSize, &(mStreamAttrib.penSize));
        //Set CC Edge
        setCcEdge(charEdge, &(mStreamAttrib.edgeEffect));
        //Setting charater style
        setccStyle(charStyle, &(mStreamAttrib.txtOpacity));
        //Setting text background style
        setccStyle(txtBackgroundStyle, &(mStreamAttrib.txtBgOpacity));
        //Setting window style
        setccStyle(windowStyle, &(mStreamAttrib.winOpacity));
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
    if (NULL == pgrHandleSetting->tsh)
    {
        attrib_text.flags = eCpeMshTextStreamAttribFlag_None;
        DFBResult result = (pgrHandleSetting->msh)->GetTextStreamHandler(pgrHandleSetting->msh, &attrib_text, &(pgrHandleSetting->tsh));
        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Could not get ITextStreamHandler.", __FUNCTION__, __LINE__);
            return kMspStatus_AvpmError;
        }
    }
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
    IDirectFBDisplayLayer *displayLayer;
    DFBDisplayLayerConfig  dlc;
    DFBResult result;
    DFBWindowDescription window_desc;
    IDirectFBScreen *pHD;
    unsigned char sampleText[MAX_SAMPLE_TEXT_LENGTH];
    unsigned int sampleTextLen = 0;
    int screen_width, screen_height;
    tCpeMshTextStreamAttributes attrib_text;

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
        isCaptionEnabledBeforePreview = isClosedCaptionEnabled;
        LOG(DLOGL_REALLY_NOISY, "before sample preview isClosedCaptionEnabled =%d isCaptionEnabledBeforePreview %d", isClosedCaptionEnabled, isCaptionEnabledBeforePreview);

        LOG(DLOGL_NORMAL, "Stopping the caption rendering before preview");
        setCc(false);
        LOG(DLOGL_NORMAL, " Launching Preview for the requested configuration");
        isPreviewEnabled = true;

        result = dfb->GetScreen(dfb, eCpeDFBScreenIndex_HD, &pHD);
        if (result != DFB_OK)
        {
            LOG(DLOGL_ERROR, "Error in getting DFB HD screen interface. Error code = %d", result);
        }

        result = dfb->GetDisplayLayer(dfb, VANTAGE_HD_CC, &displayLayer);
        if (result != DFB_OK)
        {
            LOG(DLOGL_ERROR, "GetDisplayLayer returned error %d", result);
        }

        result = displayLayer->SetCooperativeLevel(displayLayer, DLSCL_SHARED);
        if (result != DFB_OK)
        {
            LOG(DLOGL_ERROR, "SetCooperativeLevel returned error %d", result);
        }

        dlc.flags = (DFBDisplayLayerConfigFlags)(DLCONF_WIDTH | DLCONF_HEIGHT);
        result = displayLayer->GetConfiguration(displayLayer, &dlc);
        if (result != DFB_OK)
        {
            LOG(DLOGL_ERROR, "GetConfiguration returned error %d", result);
        }
        screen_width  = dlc.width;
        screen_height = dlc.height;

        if (result != DFB_OK)
        {
            LOG(DLOGL_ERROR, "Error in getting DFB HD screen interface. Error code = %d", result);
        }

        window_desc.flags = (DFBWindowDescriptionFlags)(DWDESC_CAPS | DWDESC_WIDTH | DWDESC_HEIGHT | DWDESC_PIXELFORMAT | DWDESC_POSX | DWDESC_POSY);
        window_desc.posy = G8_PREVIEW_POS_Y;
        //Positioning the preview window based on the character size
        if (tCpeTshPenSize_Small == mPreviewStreamAttrib.penSize)
        {
            window_desc.posx = G8_PREVIEW_SMALL_POS_X;
            window_desc.width  = G8_PREVIEW_SMALL_WIN_WIDTH;
            window_desc.height = G8_PREVIEW_SMALL_WIN_HEIGHT;
        }
        else if (tCpeTshPenSize_Regular == mPreviewStreamAttrib.penSize)
        {
            window_desc.posx = G8_PREVIEW_STANDARD_POS_X;
            window_desc.width  = G8_PREVIEW_STANDARD_WIN_WIDTH;
            window_desc.height = G8_PREVIEW_STANDARD_WIN_HEIGHT;
        }
        else if (tCpeTshPenSize_Large == mPreviewStreamAttrib.penSize)
        {
            window_desc.posx = G8_PREVIEW_LARGE_POS_X;
            window_desc.width  = G8_PREVIEW_LARGE_WIN_WIDTH;
            window_desc.height = G8_PREVIEW_LARGE_WIN_HEIGHT;
        }

        window_desc.pixelformat = DSPF_ARGB;
        window_desc.caps = DWCAPS_ALPHACHANNEL;

        LOG(DLOGL_NORMAL, "Creating Window at xpos=%d, ypos=%d, width=%d height=%d",
            window_desc.posx, window_desc.posy, window_desc.width, window_desc.height);

        result = displayLayer->CreateWindow(displayLayer, &window_desc, &samplePreviewTextWindow);
        if (result != DFB_OK)
        {
            LOG(DLOGL_ERROR, "CreateWindow returned error %d ", result);
        }
        if (NULL == samplePreviewTextWindow)
        {
            LOG(DLOGL_ERROR, "Error: samplePreviewTextWindow is NULL, returning from here");
            //If window creation failed, we must return.
            return kMspStatus_AvpmError;
        }

        result = samplePreviewTextWindow->GetSurface(samplePreviewTextWindow, &pHdClosedCaptionSampleTextSurface);
        if (result != DFB_OK)
        {
            LOG(DLOGL_ERROR, "GetSurface returned error %d", result);
        }

        pHdClosedCaptionSampleTextSurface->Clear(pHdClosedCaptionSampleTextSurface, 0, 0, 0, 0);
        pHdClosedCaptionSampleTextSurface->Flip(pHdClosedCaptionSampleTextSurface, NULL, DSFLIP_NONE);
        pHdClosedCaptionSampleTextSurface->Clear(pHdClosedCaptionSampleTextSurface, 0, 0, 0, 0);

        result = samplePreviewTextWindow->Raise(samplePreviewTextWindow);
        if (result != DFB_OK)
        {
            LOG(DLOGL_ERROR, "RaiseToTop returned error %d", result);
        }
        if (NULL == pgrHandleSetting->tsh)
        {
            attrib_text.flags = eCpeMshTextStreamAttribFlag_None;
            result = (pgrHandleSetting->msh)->GetTextStreamHandler(pgrHandleSetting->msh, &attrib_text, &(pgrHandleSetting->tsh));
            if (result != DFB_OK)
            {
                dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Could not get ITextStreamHandler.", __FUNCTION__, __LINE__);
                return kMspStatus_AvpmError;
            }
        }
        samplePreviewTextWindow->SetOpacity(samplePreviewTextWindow, 0xff);

        LOG(DLOGL_REALLY_NOISY, " Dump of settings applied for preview");
        LOG(DLOGL_REALLY_NOISY, " Tsh flag value is %x", mPreviewStreamAttrib.flags);
        LOG(DLOGL_REALLY_NOISY, " Tsh opacity value is %x", mPreviewStreamAttrib.txtOpacity);
        LOG(DLOGL_REALLY_NOISY, " Window opacity value is %x", mPreviewStreamAttrib.winOpacity);
        LOG(DLOGL_REALLY_NOISY, " Edge effect  is %x", mPreviewStreamAttrib.edgeEffect);
        LOG(DLOGL_REALLY_NOISY, " Pen size   is %x", mPreviewStreamAttrib.penSize);

        if (pgrHandleSetting->tsh != NULL && pgrHandleSetting->cchandle != 1)
        {
            clearSurfaceFromLayer(VANTAGE_HD_CC);
            result = (pgrHandleSetting->tsh)->RenderTo(pgrHandleSetting->tsh, pHdClosedCaptionSampleTextSurface, &mPreviewStreamAttrib, &pgrHandleSetting->cchandle);
            if (DFB_OK != result)
            {
                LOG(DLOGL_ERROR, "RenderTo error %d, continuing...", result);
            }
            else
            {
                LOG(DLOGL_NOISE, "RenderTo Success!");
                pgrHandleSetting->cchandle = 1;
            }
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, " CC is in Rendering State");
        }

        strcpy((char *)sampleText, "Closed Caption Sample Text");
        sampleTextLen = strlen((char *)sampleText);

        if (pgrHandleSetting->tsh != NULL)
        {
            result = (pgrHandleSetting->tsh)->RenderSample(pgrHandleSetting->tsh, sampleText, sampleTextLen);
            if (DFB_OK != result)
            {
                LOG(DLOGL_ERROR, "RenderSample error %d, continuing...", result);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "RenderSample failed pgrHandleSetting->tsh is NULL");
        }
    }
    else
    {
        LOG(DLOGL_NORMAL, " Stopping Preview");
        isPreviewEnabled = false;

        if (pgrHandleSetting->tsh != NULL && (pgrHandleSetting->cchandle != 0))
        {
            //If samplePreviewTextWindow is not present, proceeding ahead leads to Reboot.
            //CSCuq07067 : G8: Box reboot during Ch+/- /Exit on Mosiac Ch with shim-dfb-cperpv2.cpp
            if (samplePreviewTextWindow)
            {
                samplePreviewTextWindow->SetOpacity(samplePreviewTextWindow, 0x00);

                result = (pgrHandleSetting->tsh)->Stop(pgrHandleSetting->tsh, pgrHandleSetting->cchandle);
                ClearCCsurface();
                if (DFB_OK != result)
                {
                    LOG(DLOGL_ERROR, "RenderTo error %d, continuing...", result);
                }
                else
                {
                    LOG(DLOGL_REALLY_NOISY, "Sample text Disable Success");
                    pgrHandleSetting->cchandle = 0;
                }

                if (NULL != pgrHandleSetting->tsh)
                {
                    LOG(DLOGL_ERROR, "TSH is not NULL, releasing TSH");
                    pgrHandleSetting->tsh->Release(pgrHandleSetting->tsh);
                    pgrHandleSetting->tsh = NULL;
                }

                pHdClosedCaptionSampleTextSurface->Clear(pHdClosedCaptionSampleTextSurface, 0, 0, 0, 0);
                pHdClosedCaptionSampleTextSurface->Flip(pHdClosedCaptionSampleTextSurface, NULL, DSFLIP_NONE);
                pHdClosedCaptionSampleTextSurface->Clear(pHdClosedCaptionSampleTextSurface, 0, 0, 0, 0);

                result = samplePreviewTextWindow->Close(samplePreviewTextWindow);
                if (result != DFB_OK)
                {
                    LOG(DLOGL_ERROR, "text win Close returned error %d", result);
                }
                result = samplePreviewTextWindow->Destroy(samplePreviewTextWindow);
                if (result != DFB_OK)
                {
                    LOG(DLOGL_ERROR, "text win Destroy returned error %d", result);
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "Error: samplePreviewTextWindow is NULL.");
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


    return kMspStatus_Ok;
}

bool Avpm::doesTVSupport1080p(const tCpeAvoxDigDispInfo &dispInfo)
{
    FNLOG(DL_MSP_AVPM);

    // G8 supports 1080p 24, 30 and 60 Hz. 60Hz is considered standard for 1080p in G8.
    // If TV supports 1080p 60Hz, return true. Else, return false.
    LOG(DLOGL_NORMAL, "1080p 24Hz: %s",
        (((dispInfo.explicitModes & tCpeAvoxVideoOutputModes_1080p24_16x9) == tCpeAvoxVideoOutputModes_1080p24_16x9)) ? "YES" : "NO");
    LOG(DLOGL_NORMAL, "1080p 30Hz: %s",
        (((dispInfo.explicitModes & tCpeAvoxVideoOutputModes_1080p30_16x9) == tCpeAvoxVideoOutputModes_1080p30_16x9)) ? "YES" : "NO");

    if ((dispInfo.explicitModes & tCpeAvoxVideoOutputModes_1080p60_16x9) == tCpeAvoxVideoOutputModes_1080p60_16x9)
    {
        LOG(DLOGL_NORMAL, "1080p 60Hz: YES");
    }
    else
    {
        LOG(DLOGL_ERROR, "1080p 60Hz: NO");
        return false;
    }
    return true;
}
