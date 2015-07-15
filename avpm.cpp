/**
   \file Avpm.cpp
   Implementation file for avpm class

   AVPM - Audio Video Port Manager
   AVPM is a module which handles the Audio and Video settings from Display session and
   Unifeid seetings by accessing DirectFB as an interface to Hardware
*/


///////////////////////////////////////////////////////////////////////////
//                    SAIL Includes
///////////////////////////////////////////////////////////////////////////

#include <dlog.h>

//////////////////////////////////////////////////////////////////////////
//                         Local Includes
///////////////////////////////////////////////////////////////////////////

#include "avpm.h"
#include <time.h>
#include "IMediaController.h"
#include "languageSelection.h"

///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////
#if PLATFORM_NAME == G8 || PLATFORM_NAME == IP_CLIENT
#include <utilities/cpeutil_drm.h>//Added for G8 cross compile
#endif
#if PLATFORM_NAME == G6
#include <utilities/cpe_drm.h>//G8 port change
#endif

#include"pthread_named.h"
#include <dlog.h>
#ifdef LOG
#error  LOG already defined
#endif


#define LOG(level, msg, args...)  dlog(DL_MSP_AVPM, level,"Avpm:%s:%d " msg, __FUNCTION__, __LINE__, ##args);


//#define DEBUG_LATER 1

#define UNUSED_PARAM(a) (void)a;



#define frameRate_16x19 "1.33"
#define frameRate_4x3 "1.78"
#define frameRate_16x19_4x3 1070
#define MAX_FRAMERATE_SIZE 100
#define SIZE_OF_COMP 5
#define DEFAULT_VIDEO_W 720
#define DEFAULT_VIDEO_H 482

// #define MSP_DISPLAY_SD_BARKER

extern bool IsDvrSupported();

Avpm *Avpm::m_pInstance = NULL;


pthread_mutex_t Avpm::mMutex;
tCpeVshScaleRects Avpm::HDRects;
tCpeVshScaleRects Avpm::SDRects;
float Avpm::frame_asp;
float Avpm::frame_aspPIP;
#define VOL_STEP_SIZE 5
#define GALIO_MENU_W 456
#define PIP_W 640
#define PIP_H 720

#define PIP_WINDOW_WIDTH 546
#define PIP_WINDOW_HEIGHT 408

#define MAX_VOL 500  //to fix the vol_table boundary
#define HD_CONNECTOR_MASK    0x00060
#define SD_CONNECTOR_MASK    0x1000C
//#define SD_PIP_MIRRORING 1


static pthread_mutex_t  sMutex = PTHREAD_MUTEX_INITIALIZER;

///////////////////////////////////////////////////////////////////////////
//                      Member implementation
///////////////////////////////////////////////////////////////////////////

/** *********************************************************
*/
static int vol_table[101] =
{
    60, 29, 27, 26, 25, 23, 22, 20, 17, 15, 12, 9, 7, 4, 2, 1, 1, 1, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

void* Avpm::eventThreadFunc(void *data)
{
    bool done = false;
    data = data;
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Event thread AVPM");
    Avpm *inst = (Avpm*)data;
    while (!done)
    {
        Event *evt = NULL;
        evt = inst->threadEventQueue->popEventQueue();
        if (!evt)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error: Null event instance", __FUNCTION__, __LINE__);
            return NULL;
        }
        inst->lockMutex();
        switch (evt->eventType)
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "event in event thread %d\n", evt->eventType);
        case kAvpmStreamAspectChanged:
        {
            if (inst->mMainScreenPgrHandle)
            {
                ProgramHandleSetting* pgrHandleSetting = inst->getProgramHandleSettings(inst->mMainScreenPgrHandle);
                if (!pgrHandleSetting)
                {
                    dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in getting the ProgramHandleSetting", __FUNCTION__, __LINE__);
                }
                else
                {
                    inst->setPictureMode(pgrHandleSetting->vsh, pgrHandleSetting->hdHandle, pgrHandleSetting->sdHandle);
                }
            }
        }
        break;
        case	kAvpmHDStreamAspectChanged:
        {
            if (inst->mMainScreenPgrHandle)
            {
                ProgramHandleSetting* pgrHandleSetting = inst->getProgramHandleSettings(inst->mMainScreenPgrHandle);
                if (!pgrHandleSetting)
                {
                    dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in getting the ProgramHandleSetting", __FUNCTION__, __LINE__);
                }
                else
                {
                    inst->setPictureMode(pgrHandleSetting->vsh, pgrHandleSetting->hdHandle, pgrHandleSetting->sdHandle, kAvpmHDVideo);
                }
            }
        }
        break;
        case	kAvpmSDStreamAspectChanged:
        {
            if (inst->mMainScreenPgrHandle)
            {
                ProgramHandleSetting* pgrHandleSetting = inst->getProgramHandleSettings(inst->mMainScreenPgrHandle);
                if (!pgrHandleSetting)
                {
                    dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in getting the ProgramHandleSetting", __FUNCTION__, __LINE__);
                }
                else
                {
                    inst->setPictureMode(pgrHandleSetting->vsh, pgrHandleSetting->hdHandle, pgrHandleSetting->sdHandle, kAvpmSDVideo);
                }
            }
        }
        break;
        case kAvpmUpdateCCI:
        {
            CCIData cciData;
            cciData.cit = (inst->mCCIbyte & 0x10) >> 4;
            cciData.aps = (inst->mCCIbyte & 0x0C) >> 2;
            cciData.emi = (inst->mCCIbyte & 0x03);
            LOG(DLOGL_REALLY_NOISY, "kAvpmUpdateCCI event is received with CCI value %u ", inst->mCCIbyte);

            int status = cpe_drm_set(eCpeDRMGetSetNames_CCIbits, inst->mCCIbyte);
            if (status == kCpe_NoErr)
            {
                unsigned int value;
                cpe_drm_get(eCpeDRMGetSetNames_CCIbits, &value);
                LOG(DLOGL_REALLY_NOISY, "New Set value is %d\n", value);

                inst->SetAPS((MacroVisionAPS)cciData.aps);      // for setting macrovision
                inst->SetCITState((CITState)cciData.cit); // for setting Cit
                inst->SetEMI((uint8_t)cciData.emi);
            }
            else
            {
                LOG(DLOGL_ERROR, "Error in DRM SetCCIBits %d", status);
            }

            eMspStatus ret_value = kMspStatus_Ok;
            LOG(DLOGL_REALLY_NOISY, "SOC value is %x", inst->mIsSoc);

            ret_value = inst->applySocSetting(inst->mIsSoc);
            if (ret_value != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "Error in applying SOC setting error:%d", ret_value);
            }


        }
        break;
        case kAvpmTVAspectratioChanged:
        case kAvpmAudioOutputChanged:
        case kAvpmAC3AudioRangeChanged:
        case kAvpmVideoOutputChanged:
        case kAvpmDisplayResolnChanged:
        case kAvpmDigitalCCEnable:
        case kAvpmAnalogCCEnable:
        case kAvpmCCCharColor:
        case kAvpmBackgroundColor:
        case kAvpmBackgroundStyle:
        case kAvpmCCSetByProgram:
        case kAvpmCCPenSize:
        case kAvpmCCCharStyle:
        case kAvpmCCCharEdge:
        case kAvpmCCCharFont:
        case kAvpmCCWindowColor:
        case kAvpmCCWindowStyle:
        case kAvpmMasterVol:
        case kAvpmMasterMute:
        case kAvpmSkinSize:
        case kAvpmrfOutputChannel:
        case kAvpmVOD1080pDisplay:
        {
            UseIpcMsg *pMsg = (UseIpcMsg *)evt->eventData;
            UseIpcMsg appliedMsg;
            UseAttributes attributes;
            if (pMsg)
            {
                eMspStatus status = inst->applyAVSetting((eAvpmEvent)evt->eventType, pMsg->setting.value);
                appliedMsg.msgType = USE_APPLIED;
                appliedMsg.pTag.authority.length = 1;
                appliedMsg.pTag.authority.value = "";
                appliedMsg.pTag.path.length = pMsg->pTag.path.length;
                appliedMsg.pTag.path.value = (char *)malloc(pMsg->pTag.path.length);
                strcpy(appliedMsg.pTag.path.value, pMsg->pTag.path.value);
                appliedMsg.setting.length = 1;
                appliedMsg.setting.value = "";
                memset(&attributes, 0, sizeof(attributes));
                appliedMsg.attributes = attributes;

                if (status == kMspStatus_Ok)
                {
                    appliedMsg.msgStatus = USE_SUCCESS;
                }
                else
                {
                    appliedMsg.msgStatus = USE_FAILURE;
                }
                dlog(DL_MSP_AVPM, DLOGL_NOISE, "Applied Message = %s\n", appliedMsg.pTag.path.value);
                dlog(DL_MSP_AVPM, DLOGL_NOISE, "Received Message Message = %s Length = %d\n", pMsg->pTag.path.value, pMsg->pTag.path.length);
                Uset_appliedT(&appliedMsg);
                free(appliedMsg.pTag.path.value);
                free(pMsg->pTag.authority.value);
                free(pMsg->pTag.path.value);
                free(pMsg->setting.value);
                delete pMsg;
                pMsg = NULL;
            }
            else
            {
                dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s:%d pmsg is null ", __FUNCTION__, __LINE__);
            }
        }
        break;
        case kAvpmAudioLangChanged:
        case kAvpmSapChanged:
        case kAvpmDVSChanged:
        {
            UseIpcMsg *pMsg = (UseIpcMsg *)evt->eventData;
            UseIpcMsg appliedMsg;
            UseAttributes attributes;
            if (pMsg)
            {
                /* In this case we need to send applied message first & then change the PID as per language */
                appliedMsg.msgType = USE_APPLIED;
                appliedMsg.pTag.authority.length = 1;
                appliedMsg.pTag.authority.value = "";
                appliedMsg.pTag.path.length = pMsg->pTag.path.length;
                appliedMsg.pTag.path.value = (char *)malloc(pMsg->pTag.path.length);
                strcpy(appliedMsg.pTag.path.value, pMsg->pTag.path.value);
                appliedMsg.setting.length = 1;
                appliedMsg.setting.value = "";
                memset(&attributes, 0, sizeof(attributes));
                appliedMsg.attributes = attributes;
                appliedMsg.msgStatus = USE_SUCCESS;
                Uset_appliedT(&appliedMsg);
                free(appliedMsg.pTag.path.value);
                inst->applyAVSetting((eAvpmEvent)evt->eventType, pMsg->setting.value);
                free(pMsg->pTag.authority.value);
                free(pMsg->pTag.path.value);
                free(pMsg->setting.value);
                delete pMsg;
                pMsg = NULL;
            }

        }
        break;
        case kAvpmRegSettings:
        {
            dlog(DL_MSP_AVPM, DLOGL_NORMAL, "In Case kAvpmRegCiscoSettings");
            UseIpcMsg *lpMsg = (UseIpcMsg *)evt->eventData;
            inst->registerAVSettings(lpMsg->pTag.path.value);
            free(lpMsg->pTag.path.value);
            delete 	lpMsg;
            lpMsg = NULL;
        }
        break;
        case kAvpmThreadExit:
        {
            done = true;
        }
        break;

        case kAvpmApplyHDMainPresentationParams:
        {
            LOG(DLOGL_REALLY_NOISY, "Apply HD MAIN Presentation Params");
            if (inst->mMainScreenPgrHandle)
            {
                ProgramHandleSetting* pgrHandleSetting = inst->getProgramHandleSettings(inst->mMainScreenPgrHandle);
                if (!pgrHandleSetting)
                {
                    LOG(DLOGL_ERROR, "Error in getting the ProgramHandleSetting");
                }
                else
                {
                    inst->setWindow(pgrHandleSetting->rect, pgrHandleSetting->vsh, eCpeDFBScreenIndex_HD, pgrHandleSetting->hdHandle);
                    inst->setPictureMode(pgrHandleSetting->vsh, pgrHandleSetting->hdHandle, pgrHandleSetting->sdHandle, kAvpmHDVideo);
                }
            }
        }
        break;


        case kAvpmApplySDMainPresentationParams:
        {
            LOG(DLOGL_REALLY_NOISY, "Apply SD MAIN Presentation Params");
            if (inst->mMainScreenPgrHandle)
            {
                ProgramHandleSetting* pgrHandleSetting = inst->getProgramHandleSettings(inst->mMainScreenPgrHandle);
                if (!pgrHandleSetting)
                {
                    LOG(DLOGL_ERROR, "Error in getting the ProgramHandleSetting");
                }
                else
                {
                    inst->setWindow(pgrHandleSetting->rect, pgrHandleSetting->vsh, eCpeDFBScreenIndex_SD, pgrHandleSetting->sdHandle);
                    inst->setPictureMode(pgrHandleSetting->vsh, pgrHandleSetting->hdHandle, pgrHandleSetting->sdHandle, kAvpmSDVideo);
                }
            }
        }
        break;


        case kAvpmApplyHDPipPresentationParams:
        {
            LOG(DLOGL_NORMAL, "Apply HD PIP Presentation Params");
            if (inst->mMainScreenPgrHandle)
            {
                ProgramHandleSetting* pgrHandleSetting = inst->getProgramHandleSettings(inst->mPipScreenPgrHandle);
                if (!pgrHandleSetting)
                {
                    LOG(DLOGL_ERROR, "Error in getting the ProgramHandleSetting");
                }
                else
                {
                    inst->setWindow(pgrHandleSetting->rect, pgrHandleSetting->vsh, eCpeDFBScreenIndex_HD, pgrHandleSetting->hdHandle);
                    inst->clearSurfaceFromLayer(VANTAGE_HD_PIP);
                }
            }
        }
        break;



        case kAvpmApplySDPipPresentationParams:
        {
            LOG(DLOGL_NORMAL, "Apply SD PIP Presentation Params");
            if (inst->mMainScreenPgrHandle)
            {
                ProgramHandleSetting* pgrHandleSetting = inst->getProgramHandleSettings(inst->mPipScreenPgrHandle);
                if (!pgrHandleSetting)
                {
                    LOG(DLOGL_ERROR, "Error in getting the ProgramHandleSetting");
                }
                else
                {
                    inst->setWindow(pgrHandleSetting->rect, pgrHandleSetting->vsh, eCpeDFBScreenIndex_SD, pgrHandleSetting->sdHandle);
                    inst->clearSurfaceFromLayer(VANTAGE_SD_PIP);
                }
            }
        }
        break;

        default:
            break;
        }
        inst->threadEventQueue->freeEvent(evt);
        inst->unLockMutex();
    }
    pthread_exit(NULL);
    return NULL;

}

void Avpm::settingChangedCB(eUse_StatusCode result, UseIpcMsg *pMsg, void *pClientContext)
{
    UseIpcMsg appliedMsg;
    UseAttributes attributes;
    Avpm *inst = (Avpm *)pClientContext;
    dlog(DL_MSP_AVPM, DLOGL_NOISE, "Setting is changed |retStatus=%d %d\n", result, pMsg->msgType);
    switch (pMsg->msgType)
    {
    case USE_REGISTER_INTEREST:
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Event type USE_REGISTER_INTEREST");
        break;

    case USE_CANCEL_INTEREST:
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Event type USE_CANCEL_INTEREST");
        break;

    case USE_NOTIFY:
    case USE_VERIFY:
        dlog(DL_MSP_AVPM, DLOGL_NOISE, " USE_APPLIED Msg Received from USE-EM");
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Setting changed is %s %s\n", pMsg->pTag.path.value, pMsg->setting.value);
        if (inst)
        {
            eAvpmEvent event = inst->mapUsetPtagToAVEvent(pMsg->pTag.path.value);
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "Event enum value is %d\n", event);
            if (event == kAvpmNotDefined)
            {
                appliedMsg.msgType = USE_APPLIED;
                appliedMsg.msgStatus = USE_FAILURE;
                appliedMsg.pTag.authority.length = 1;
                appliedMsg.pTag.authority.value = "";
                appliedMsg.pTag.path.length = pMsg->pTag.path.length;
                appliedMsg.pTag.path.value = (char *)malloc(pMsg->pTag.path.length);
                strcpy(appliedMsg.pTag.path.value, pMsg->pTag.path.value);
                appliedMsg.setting.length = 1;
                appliedMsg.setting.value = "";
                memset(&attributes, 0, sizeof(attributes));
                appliedMsg.attributes = attributes;
                Uset_appliedT(&appliedMsg);
                free(appliedMsg.pTag.path.value);
            }
            else
            {
                // copy data

                UseIpcMsg *msg = new UseIpcMsg();
                msg->msgType = pMsg->msgType;
                msg->msgStatus = pMsg->msgStatus;
                msg->setting.length = pMsg->setting.length;
                msg->setting.value = NULL;
                if (msg->setting.length != 0)
                {
                    msg->setting.value = (char *)malloc(msg->setting.length + 1);
                    strcpy(msg->setting.value, pMsg->setting.value);
                }
                msg->pTag.authority.length = pMsg->pTag.authority.length;
                msg->pTag.authority.value = NULL;
                if (msg->pTag.authority.length != 0)
                {
                    msg->pTag.authority.value  = (char *)malloc(msg->pTag.authority.length + 1);
                    strcpy(msg->pTag.authority.value, pMsg->pTag.authority.value);
                }
                msg->pTag.path.length = pMsg->pTag.path.length;
                msg->pTag.path.value = NULL;
                if (msg->pTag.path.length != 0)
                {
                    msg->pTag.path.value = (char *)malloc(msg->pTag.path.length + 1);
                    strcpy(msg->pTag.path.value, pMsg->pTag.path.value);
                }
                msg->attributes.settingsLevel = pMsg->attributes.settingsLevel;
                msg->attributes.userPermitted = pMsg->attributes.userPermitted;

                inst->threadEventQueue->dispatchEvent(event, msg);

            }
        }
        break;
    case USE_DISCONNECTED:
    {
        dlog(DL_MSP_AVPM, DLOGL_NORMAL, "In EventUSE_DISCONNECTED\n");
        UseIpcMsg *msg = new UseIpcMsg();
        msg->pTag.path.value = (char*)malloc(strlen(pMsg->pTag.path.value) + 1); /*Coverity 20166 20191 20212*/
        memset(msg->pTag.path.value, 0, (strlen(pMsg->pTag.path.value) + 1));
        strcpy(msg->pTag.path.value, pMsg->pTag.path.value);
        inst->threadEventQueue->dispatchEvent(kAvpmRegSettings, msg);

    }
    break;
    default:
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "No Need to handle");
        break;
    }
    dlog(DL_MSP_AVPM, DLOGL_NOISE, "Exiting callback\n");
}


eAvpmEvent Avpm::mapUsetPtagToAVEvent(const char *aPtag)
{
    eAvpmEvent event = (eAvpmEvent) - 1;
    if (strcmp(aPtag, "ciscoSg/audio/audioOutput") == 0)
    {
        event = kAvpmAudioOutputChanged ;
    }
    else if (strcmp(aPtag, "ciscoSg/audio/audioLangList") == 0)
    {
        event = kAvpmAudioLangChanged;
    }
    else if (strcmp(aPtag, "ciscoSg/audio/audioRange") == 0)
    {
        event = kAvpmAC3AudioRangeChanged;
    }
    else if (strcmp(aPtag, "ciscoSg/audio/audioDescribed") == 0)
    {
        event = kAvpmDVSChanged;
    }
    else if (strcmp(aPtag, "ciscoSg/cc/ccCharacterSize") == 0)
    {
        event = kAvpmCCPenSize;
    }
    else if (strcmp(aPtag, "ciscoSg/cc/ccCharacterColor") == 0)
    {
        event = kAvpmCCCharColor;
    }
    else if (strcmp(aPtag, "ciscoSg/cc/ccSourceDigital") == 0)
    {
        event = kAvpmDigitalCCEnable;
    }
    else if (strcmp(aPtag, "ciscoSg/cc/ccSourceAnalog") == 0)
    {
        event = kAvpmAnalogCCEnable;
    }
    else if (strcmp(aPtag, "ciscoSg/cc/ccBgColor") == 0)
    {
        event = kAvpmBackgroundColor;
    }
    else if (strcmp(aPtag, "ciscoSg/cc/ccBgStyle") == 0)
    {
        event = kAvpmBackgroundStyle;
    }
    else if (strcmp(aPtag, "ciscoSg/look/aspect") == 0)
    {
        event = kAvpmTVAspectratioChanged;
    }
    else if (strcmp(aPtag, "ciscoSg/look/mode") == 0)
    {
        event = kAvpmDisplayResolnChanged;
    }
    else if (strcmp(aPtag, "ciscoSg/look/picSize") == 0)
    {
        event = kAvpmVideoOutputChanged;
    }
    else if (strcmp(aPtag, "ciscoSg/sys/rfOutputChannel") == 0)
    {
        event = kAvpmrfOutputChannel;
    }
    else if (strcmp(aPtag, "ciscoSg/cc/ccSetByProgram") == 0)
    {
        event = kAvpmCCSetByProgram;
    }
    else if (strcmp(aPtag, "mom.system.mute") == 0)
    {
        event = kAvpmMasterMute;
    }
    else if (strcmp(aPtag, "mom.system.volume") == 0)
    {
        event = kAvpmMasterVol;
    }
    else if (strcmp(aPtag, "ciscoSg/audio/sap") == 0 || strcmp(aPtag, "ciscoSg/audio/sapSupppt") == 0)
    {
        event = kAvpmSapChanged;
    }
    else if (strcmp(aPtag, "ciscoSg/cc/ccCharacterStyle") == 0)
    {
        event = kAvpmCCCharStyle;
    }
    else if (strcmp(aPtag, "ciscoSg/cc/ccCharacterEdge") == 0)
    {
        event = kAvpmCCCharEdge;
    }
    else if (strcmp(aPtag, "ciscoSg/cc/ccCharacterFont") == 0)
    {
        event = kAvpmCCCharFont;
    }
    else if (strcmp(aPtag, "ciscoSg/cc/ccWindowColor") == 0)
    {
        event = kAvpmCCWindowColor;
    }
    else if (strcmp(aPtag, "ciscoSg/cc/ccWindowStyle") == 0)
    {
        event = kAvpmCCWindowStyle;
    }
    else if (strcmp(aPtag, UNISETTING_VOD1080P) == 0)
    {
        event = kAvpmVOD1080pDisplay;
    }
    return event;
}

eMspStatus Avpm::applyAVSetting(eAvpmEvent aEvent, char *pValue)
{
    eAvpmResolution dispResolution;
    eMspStatus status;

    FNLOG(DL_MSP_AVPM);
    dlog(DL_MSP_AVPM, DLOGL_NOISE, "In applyAVSettings %d %s\n", aEvent, (const char *)pValue);

    switch (aEvent)
    {
    case kAvpmAudioLangChanged:
    case kAvpmDVSChanged:
    {
        if (mAudioLangChangedCb)
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "Inside Language Changed\n");
            mAudioLangChangedCb(mAudioLangCbData);
            return kMspStatus_Ok;
        }
    }
    break;
    case kAvpmSapChanged:
        if (mSapChangeCb)
        {
            dlog(DL_MSP_AVPM, DLOGL_NORMAL, "SAP changed");
            mSapChangeCb(mpSapChangedCbData);
            return kMspStatus_Ok;
        }
        break;
    case kAvpmAudioOutputChanged:
        setAudioOutputMode();
        break;

    case kAvpmAC3AudioRangeChanged:
    {
        //coverity id - 10658
        //Adding a default audio range setting as NORMAL
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Inside Compress Level\n");
        tAvpmAudioRange audio_range;
        if (strcmp((const char *)pValue, "narrow") == 0)
        {
            audio_range = tAvpmAudioRangeNarrow;
        }
        else if (strcmp((const char *)pValue, "normal") == 0)
        {
            audio_range = tAvpmAudioRangeNormal;
        }
        else if (strcmp((const char *)pValue, "wide") == 0)
        {
            audio_range = tAvpmAudioRangeWide;
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "Not a valid option.Setting default audio range--Normal");
            audio_range = tAvpmAudioRangeNormal;
        }

        setAC3AudioRange(mMainScreenPgrHandle, (tAvpmAudioRange) audio_range);
    }
    break;
    case kAvpmTVAspectratioChanged:
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Inside Aspect Ratio apply AV setting\n");
        if (mMainScreenPgrHandle)
        {
            if (strcmp((const char *)pValue, "4:3") == 0)
            {
                user_aspect_ratio = tAvpmTVAspectRatio4x3;
            }
            else if (strcmp((const char *)pValue, "16:9") == 0)
            {
                user_aspect_ratio = tAvpmTVAspectRatio16x9;
            }

            ProgramHandleSetting *pgrSettings = getProgramHandleSettings(mMainScreenPgrHandle);

            if (pgrSettings)
            {
                eMspStatus status = setTVAspectRatio(pgrSettings->vsh, pgrSettings->hdHandle, pgrSettings->sdHandle);
                return status;
            }
            else
            {
                return kMspStatus_AvpmError;
            }
        }
        else
        {
            return kMspStatus_AvpmError;
        }
    }
    break;
    case kAvpmVideoOutputChanged:
    {
        if (mMainScreenPgrHandle)
        {
            //picture_mode = (tAvpmPictureMode)atoi((const char *)pValue);
            if (strcmp((const char *)pValue, "normal") == 0)
            {
                picture_mode = tAvpmPictureMode_Normal;
            }
            else if (strcmp((const char *)pValue, "stretch") == 0)
            {
                picture_mode = tAvpmPictureMode_Stretch;
            }
            else if (strcmp((const char *)pValue, "zoom") == 0)
            {
                picture_mode = tAvpmPictureMode_Zoom25;
            }
            else if (strcmp((const char *)pValue, "zoom2") == 0)
            {
                picture_mode = tAvpmPictureMode_Zoom50;
            }

            ProgramHandleSetting *pgrSettings = getProgramHandleSettings(mMainScreenPgrHandle);

            if (pgrSettings)
            {
                eMspStatus status = setPictureMode(pgrSettings->vsh, pgrSettings->hdHandle, pgrSettings->sdHandle);
                return status;
            }
        }
        else
            return kMspStatus_AvpmError;
    }
    break;
    case kAvpmrfOutputChannel:
        setRFmodeSetOutputChan();
        break;

    case kAvpmDisplayResolnChanged:
        if (strcmp((const char *)pValue, "480i") == 0)
        {
            LOG(DLOGL_REALLY_NOISY, "Setting 480i resolution");
            dispResolution = tAvpmResolution_480i;
        }
        else if (strcmp((const char *)pValue, "480p") == 0)
        {
            LOG(DLOGL_REALLY_NOISY, "Setting 480p resolution");
            dispResolution = tAvpmResolution_480p;
        }
        else if (strcmp((const char *)pValue, "720p") == 0)
        {
            LOG(DLOGL_REALLY_NOISY, "Setting 720p resolution");
            dispResolution = tAvpmResolution_720p;
        }
        else if (strcmp((const char *)pValue, "1080i") == 0)
        {
            LOG(DLOGL_REALLY_NOISY, "Setting 1080i resolution");
            dispResolution = tAvpmResolution_1080i;
        }
        else if (strcmp((const char *)pValue, "1080p") == 0)
        {
            LOG(DLOGL_REALLY_NOISY, "Setting 1080p resolution ");
            dispResolution = tAvpmResolution_1080p;
        }
        else
        {
            //default resolution state as 720p
            dispResolution = tAvpmResolution_720p;//Defalut preffered Mode
        }
        status = setDisplayResolution(dispResolution);
        break;

    case kAvpmVOD1080pDisplay:
        if ((strcmp((const char *)pValue, VOD1080PSETTING_NOT_SUPPORTED) == 0)
                && mIs1080pModeActive)
        {
            LOG(DLOGL_REALLY_NOISY, "vod1080p: Revert the resolution now.");
            setDefaultDisplayResolution();
        }
        return kMspStatus_Ok;
        break;
    case kAvpmDigitalCCEnable:
    case kAvpmAnalogCCEnable:
    case kAvpmCCCharColor:
    case kAvpmCCPenSize:
    case kAvpmBackgroundColor:
    case kAvpmBackgroundStyle:
    case kAvpmCCSetByProgram:
    case kAvpmCCCharStyle:
    case kAvpmCCCharEdge:
    case kAvpmCCCharFont:
    case kAvpmCCWindowColor:
    case kAvpmCCWindowStyle:
    {
        closedCaption(mMainScreenPgrHandle, aEvent, pValue);
    }
    break;

    case kAvpmMasterMute:
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Mute the volume\n");
        if (strcmp((const char *)pValue, "0") == 0)
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "UnMute prev_vol %d\n", prev_vol);
            MasterMute(mMainScreenPgrHandle, true);
            setMasterVolume(mMainScreenPgrHandle, prev_vol);
        }
        else if (strcmp((const char *)pValue, "1") == 0)
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "Mute the volume\n");
            MasterMute(mMainScreenPgrHandle, false);
        }
        return kMspStatus_Ok;
    }
    break;
    case kAvpmMasterVol:
    {
        int vol = atoi((const char *)pValue);
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Volume up/down is %d\n", vol);
        prev_vol = vol;
        MasterMute(mMainScreenPgrHandle, true);
        eMspStatus status = setMasterVolume(mMainScreenPgrHandle, vol);
        return status;
    }
    break;
    case kAvpmSkinSize:
    {
        char *skinSizeStr = (char *)pValue;
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "kAvpmSkinSize: %s (no MSP action taken)", skinSizeStr);

#ifdef MSP_DISPLAY_SD_BARKER

        if (strcmp(skinSizeStr, "640x480") == 0)
        {
            displaySdBarker(false);
        }
        else
        {
            displaySdBarker(true);
        }
#endif

        return kMspStatus_Ok;
    }
    break;
    default:
    {
        return kMspStatus_AvpmError;
    }
    break;
    }
    return kMspStatus_AvpmError;
}


void Avpm::CalculateScalingRectangle(DFBRectangle& final, DFBRectangle& request, int maxWidth, int maxHeight)
{
    FNLOG(DL_MSP_AVPM);

    final.h = (maxHeight * request.h) / GALIO_MAX_WINDOW_HEIGHT;
    final.y = (maxHeight * request.y) / GALIO_MAX_WINDOW_HEIGHT;
    if ((maxHeight - final.y) < final.h)
    {
        final.y = maxHeight - final.h;
    }

    final.w = (maxWidth * request.w) / GALIO_MAX_WINDOW_WIDTH;
    final.x = (maxWidth * request.x) / GALIO_MAX_WINDOW_WIDTH;
    if ((maxWidth - final.x) < final.w)
    {
        final.x = maxWidth - final.w;
    }
}

void Avpm::setWindow(DFBRectangle rectRequest, IVideoStreamHandler *vsh, tCpeDFBScreenIndex screenIndex, tCpeMshAssocHandle &handle)
{

    tCpeVshScaleRects rectOutput;
    memset(&rectOutput, 0, sizeof(tCpeVshScaleRects));
    DFBResult status;
    int maxPipSurfaceWidth;
    int maxPipSurfaceHeight;

    FNLOG(DL_MSP_AVPM);

    galio_rect = rectRequest;

    if (vsh == NULL)
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d) Stream has no video. Returning", __PRETTY_FUNCTION__, __LINE__);
        return;
    }

    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) rectRequest x %d y %d w %d h %d handle %x", __FUNCTION__, __LINE__, rectRequest.x, rectRequest.y, rectRequest.w, rectRequest.h, handle);

    GetScreenSize(screenIndex, &maxPipSurfaceWidth, &maxPipSurfaceHeight);
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) screensize w %d h %d", __FUNCTION__, __LINE__, maxPipSurfaceWidth, maxPipSurfaceHeight);

    CalculateScalingRectangle(rectOutput.videoRect, rectRequest, maxPipSurfaceWidth, maxPipSurfaceHeight);
    rectOutput.opaqueRect = rectOutput.videoRect;
    rectOutput.scaleRect = rectOutput.videoRect;

    logScalingRects("Setting following params from setwindow", rectOutput);

    rectOutput.flags = tCpeVshScaleRectsFlags_All;
    setStereoDepth(rectOutput);
    if (rectRequest.w < DEFAULT_VIDEO_W || rectRequest.h < DEFAULT_VIDEO_H)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) Applying new params", __FUNCTION__, __LINE__);
        status =  vsh->SetScalingRects(vsh, handle, &rectOutput);
        if (status != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error setting scaling rects: %d", __FUNCTION__, __LINE__, status);
        }
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d) Not Applying here beacuse its not less than actual width", __FUNCTION__, __LINE__);
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d)   videoRect.x  %d y %d, w %d h %d", __FUNCTION__, __LINE__, rectOutput.videoRect.x, rectOutput.videoRect.y, rectOutput.videoRect.w, rectOutput.videoRect.h);
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d)   opaqueRect.x %d y %d, w %d h %d", __FUNCTION__, __LINE__, rectOutput.opaqueRect.x, rectOutput.opaqueRect.y, rectOutput.opaqueRect.w, rectOutput.opaqueRect.h);
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d)   scaleRect.x  %d y %d, w %d h %d", __FUNCTION__, __LINE__, rectOutput.scaleRect.x, rectOutput.scaleRect.y, rectOutput.scaleRect.w, rectOutput.scaleRect.h);
    }
}


void Avpm::SetVideoPresent(bool isPresent)
{
    mHaveVideo = isPresent;
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) video present: %d", __FUNCTION__, __LINE__, mHaveVideo);
}

bool Avpm::isSetPresentationParamsChanged(DFBRectangle newRect, DFBRectangle oldRect)
{
    bool status = false;

    if (newRect.x != oldRect.x)
    {
        status = true;
    }
    else if (newRect.y != oldRect.y)
    {
        status = true;
    }
    else if (newRect.w != oldRect.w)
    {
        status = true;
    }
    else if (newRect.h != oldRect.h)
    {
        status = true;
    }

    return status;
}

eMspStatus Avpm::setPresentationParams(tCpePgrmHandle pgrHandle, DFBRectangle rect, bool audiofocus)
{
    IMediaStreamHandler *msh = NULL;
    IVideoStreamHandler *vsh = NULL;

    FNLOG(DL_MSP_AVPM);
    lockMutex();

    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) rect: %d %d %d %d aud: %d", __FUNCTION__, __LINE__, rect.x, rect.y, rect.w, rect.h, audiofocus);

    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(pgrHandle);

    if (!pgrHandleSetting)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in getting the ProgramHandleSetting", __FUNCTION__, __LINE__);
        unLockMutex();
        return kMspStatus_AvpmError;
    }
    else
    {
        msh = pgrHandleSetting->msh;
        vsh = pgrHandleSetting->vsh;
    }
    if (isSetPresentationParamsChanged(rect, pgrHandleSetting->rect))
    {

        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s (%d) setPresentationParams are different, Please apply", __FUNCTION__, __LINE__);
        pgrHandleSetting->rect = rect;
        pgrHandleSetting->audioFocus = audiofocus;

        if (pgrHandleSetting->hdHandle || pgrHandleSetting->sdHandle)
        {

            setWindow(rect, vsh, eCpeDFBScreenIndex_HD, pgrHandleSetting->hdHandle);
            setWindow(rect, vsh, eCpeDFBScreenIndex_SD, pgrHandleSetting->sdHandle);


            if (audiofocus)
            {
                setPictureMode(vsh, pgrHandleSetting->hdHandle, pgrHandleSetting->sdHandle);
            }
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "Warning- Video Not Yet Started, To set Window");
        }

        if (audiofocus)
        {
            setPictureMode(vsh, pgrHandleSetting->hdHandle, pgrHandleSetting->sdHandle);
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "SetPresentationParam main Window");
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "SetPresentationParam PIP Window");
            setCc(false);
            setWindow(rect, vsh, eCpeDFBScreenIndex_HD, pgrHandleSetting->hdHandle);
            setWindow(rect, vsh, eCpeDFBScreenIndex_SD, pgrHandleSetting->sdHandle);
            clearSurfaceFromLayer(VANTAGE_HD_PIP);
            clearSurfaceFromLayer(VANTAGE_SD_PIP);
        }

    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s (%d) setPresentationParams are same, No need to apply", __FUNCTION__, __LINE__);
    }

    unLockMutex();
    return kMspStatus_Ok;
}

/** *********************************************************
 */
eMspStatus Avpm::setAudioParams(tCpePgrmHandle pgrHandle)
{
    eMspStatus status = kMspStatus_Ok;
    eUseSettingsLevel settingLevel;
    char muteSetting[MAX_SETTING_VALUE_SIZE];
    char volumeSetting[MAX_SETTING_VALUE_SIZE];
    int vol_level = 50; //Default Volume value
    bool isMute = false; //Default unmute

    FNLOG(DL_MSP_AVPM);

    lockMutex();
    if (USE_RESULT_OK == Uset_getSettingT(NULL, "mom.system.mute", MAX_SETTING_VALUE_SIZE,  muteSetting, &settingLevel))
    {
        isMute = atoi(muteSetting);
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Failed to get the mute setting. Setting to default %d\n", isMute);
    }

    if (USE_RESULT_OK == Uset_getSettingT(NULL, "mom.system.volume", MAX_SETTING_VALUE_SIZE,  volumeSetting, &settingLevel))
    {
        vol_level = atoi(volumeSetting);
    }
    else
    {

        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Failed to get the volume level. Setting to default %d\n", prev_vol);
    }

    prev_vol = vol_level;
    dlog(DL_MSP_AVPM, DLOGL_NOISE, "setAudioParams prev_vol %d\n", prev_vol);

    if (isMute)
    {
        MasterMute(pgrHandle, false);
    }
    else
    {
        MasterMute(pgrHandle, true);
        status = setMasterVolume(pgrHandle, prev_vol);
    }

    unLockMutex();
    return status;
}


/** *********************************************************
 */
eMspStatus Avpm::connectOutput(tCpePgrmHandle pgrHandle)
{
    eMspStatus status;
    IVideoStreamHandler *vsh = NULL;
    eUseSettingsLevel settingLevel;
    char aspectRatioStr[MAX_SETTING_VALUE_SIZE];

    FNLOG(DL_MSP_AVPM);

    // Set 1080p output resolution for 1080p VOD content.
    LOG(DLOGL_NOISE, "mIsVod : %d mIs1080pModeActive: %d", mIsVod, mIs1080pModeActive);
    if (!mIs1080pModeActive && mIsVod && isSource1080p(pgrHandle))
    {
        setOptimalDisplayResolution();
    }

    lockMutex();

    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(pgrHandle);

    if (!pgrHandleSetting)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in getting the ProgramHandleSetting", __FUNCTION__, __LINE__);
        unLockMutex();
        return kMspStatus_AvpmError;
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) ENTER DISPLAY HANDLES: %x %x", __FUNCTION__, __LINE__, pgrHandleSetting->hdHandle, pgrHandleSetting->sdHandle);
        vsh = pgrHandleSetting->vsh;
    }

    Uset_getSettingT(NULL, "ciscoSg/look/aspect", MAX_SETTING_VALUE_SIZE, aspectRatioStr, &settingLevel);

    if (strcmp((const char *)aspectRatioStr, "4:3") == 0)
    {
        user_aspect_ratio = tAvpmTVAspectRatio4x3;
    }
    else if (strcmp((const char *)aspectRatioStr, "16:9") == 0)
    {
        user_aspect_ratio = tAvpmTVAspectRatio16x9;
    }
    else
    {
        user_aspect_ratio = tAvpmTVAspectRatio16x9;
    }

    if (pgrHandleSetting->audioFocus)
    {
        mMainScreenPgrHandle = pgrHandle;
        status = avpm1394PortStreaming(pgrHandle, TRUE);
        if (status == kMspStatus_Ok)
        {
            LOG(DLOGL_NORMAL, "1394 port Streaming Succeeded");
        }
        else if (status == kMspStatus_NotSupported)
        {
            LOG(DLOGL_NOISE, "1394 port Streaming not supported");
        }
        else
        {
            LOG(DLOGL_ERROR, "1394 port Streaming failed");
        }

        if (false == mIsOutputStartedForMain)
        {
            status = playToVideoLayer(pgrHandle);
            if (status != kMspStatus_Ok)
            {
                unLockMutex();
                return kMspStatus_AvpmError;
            }
            if (isClosedCaptionEnabled)
            {
                setCc(true);
            }
            mIsOutputStartedForMain = true;
            LOG(DLOGL_NOISE, "Main Output Started");
        }
    }
    else
    {
        //PIP Video
        if (false == mIsOutputStartedForPIP)
        {
            mPipScreenPgrHandle = pgrHandle;
            status = playPIPVideo(pgrHandle);
            if (status != kMspStatus_Ok)
            {
                unLockMutex();
                return kMspStatus_AvpmError;
            }
            mIsOutputStartedForPIP = true;
            LOG(DLOGL_NORMAL, "PIP Output Started");
        }
    }

    unLockMutex();
    return kMspStatus_Ok;
}

eMspStatus Avpm::startOutput(tCpePgrmHandle pgrHandle)
{
    eMspStatus status;

    FNLOG(DL_MSP_AVPM);

    status = connectOutput(pgrHandle);
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "connectOutput failed");
        return kMspStatus_AvpmError;
    }

    return kMspStatus_Ok;
}

eMspStatus Avpm::stopOutput(tCpePgrmHandle pgrHandle)
{
    eMspStatus status;

    FNLOG(DL_MSP_AVPM);

    lockMutex();
    status = freezeVideo(pgrHandle);
    unLockMutex();

    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "freezeVideo failed");
        return kMspStatus_AvpmError;
    }

    return kMspStatus_Ok;
}

void Avpm::releaseProgramHandleSettings(tCpePgrmHandle pgrHandle)
{
    ProgramHandleSetting* p = NULL;
    FNLOG(DL_MSP_AVPM);

    map<tCpePgrmHandle, ProgramHandleSetting*>::iterator iter;
    iter = mMap.find(pgrHandle);
    if (iter != mMap.end())
    {
        p = (ProgramHandleSetting*) iter->second;
    }

    mMap.erase(pgrHandle);
    if (p)
    {
        delete p;
        p = NULL;
    }
}

eMspStatus Avpm::disconnectOutput(tCpePgrmHandle pgrHandle)
{
    FNLOG(DL_MSP_AVPM);
    eMspStatus status;
    lockMutex();
    if (mMainScreenPgrHandle == pgrHandle)
    {
        mMainScreenPgrHandle = NULL;
        LOG(DLOGL_NOISE, "About to invoke avpm1394PortStreaming to Disable port");
        status = avpm1394PortStreaming(pgrHandle, FALSE);
        if (status == kMspStatus_Ok)
        {
            LOG(DLOGL_NORMAL, " 1394 Port Disabled.");
        }
        else if (status == kMspStatus_NotSupported)
        {
            LOG(DLOGL_NOISE, " 1394 port disabling fails, as feature is not supported");
        }
        else
        {
            LOG(DLOGL_ERROR, " Disabling of 1394 port failed.");
        }
    }

    status = release(pgrHandle);
    releaseProgramHandleSettings(pgrHandle);
    unLockMutex();
    return kMspStatus_Ok;
}



Avpm * Avpm::getAvpmInstance()
{
    pthread_mutex_lock(&sMutex);

    if (m_pInstance == NULL)
    {
        m_pInstance = new Avpm();
    }

    pthread_mutex_unlock(&sMutex);

    return m_pInstance;
}


Avpm::Avpm()
{
    pthread_attr_t attr;
    int error;

    FNLOG(DL_MSP_AVPM);
    eMspStatus avpm1394PortStatus;
    // create event queue for scan thread

    mMainScreenPgrHandle = NULL;
    mAudioLangChangedCb = NULL;
    mAudioLangCbData = NULL;
    mDefaultResolution = -1;
    user_aspect_ratio = tAvpmTVAspectRatio16x9;
    renderToSurface = false;

    dfb = NULL;
    pHDLayer = NULL;
    pSDLayer = NULL;
    primary_surface = NULL;
    sdGfx2Layer = NULL;
    sdGfx2surface = NULL;
    hdGfx2Layer = NULL;
    hdGfx2surface = NULL;
    pHdClosedCaptionTextSurface = NULL;
    pHdClosedCaptionSampleTextSurface = NULL;
    samplePreviewTextWindow = NULL;
    pHDClosedCaptionDisplayLayer = NULL;
    isClosedCaptionEnabled = false;
    isPreviewEnabled = false;
    eAvpmEvent aEvent = kAvpmNotDefined;
    prev_vol = 0;
    bzero(mccUserDigitalSetting, MAX_SETTING_VALUE_SIZE);
    dfbInit();
    LOG(DLOGL_EMERGENCY, "DFB init done");

    // create event queue for scan thread
    threadEventQueue = new MSPEventQueue();

    // DRM Initialization.
    error  = cpe_drm_init();
    if (error != kCpe_NoErr)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in DRM Init %d", error);
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "DRM init successful");
    }

    //  int thread_create;
    pthread_mutex_init(&mMutex, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 32 * 1024);

    int thread_create = pthread_create(&mEventHandlerThread, &attr , eventThreadFunc, (void *) this);
    if (thread_create)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "*****Error in thread create");
    }

    thread_create = pthread_setname_np(mEventHandlerThread, "AVPM Event handler");
    if (0 != thread_create)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Thread Setname Failed:%d", thread_create);
    }

    // Register with Unified Settings
    bool status = Is_useInitializedT();
    if (status)
    {
        registerAVSettings();
        registerOpaqueSettings();
    }
    else
    {
        eUse_StatusCode ErrSuccess = USE_INVALID_STATUS;
        ErrSuccess = Uset_initClientT();
        if (USE_RESULT_OK == ErrSuccess)
        {
            registerAVSettings();
            registerOpaqueSettings();
        }
    }

    /* Getting the number of instances of 1394 Ports */
    avpm1394PortStatus = avpm1394PortInit();
    if (avpm1394PortStatus == kMspStatus_Ok)
    {
        LOG(DLOGL_NOISE, " 1394 port initialization succeeds");
    }
    else if (avpm1394PortStatus == kMspStatus_NotSupported)
    {
        LOG(DLOGL_NOISE, " 1394 port initialization is not supported");
    }
    else
    {
        LOG(DLOGL_ERROR, "Error getting number of instances of 1394 Port!");
    }

    pthread_mutex_init(&mMutex, NULL);

    setRFmodeSetOutputChan();
    initPicMode();
    setAudioOutputMode();
    setInitialVolume();
    closedCaption(mMainScreenPgrHandle, aEvent, NULL);
    mSapChangeCb = NULL;
    mbSapEnabled = false;
    mfallBackCCLangMap.insert(std::make_pair(ENGLISH_EZ, ENGLISH));
    mfallBackCCLangMap.insert(std::make_pair(ENGLISH, ENGLISH_EZ));
    mfallBackCCLangMap.insert(std::make_pair(FRENCH_OTHER_EZ, FRENCH_OTHER));
    mfallBackCCLangMap.insert(std::make_pair(FRENCH_OTHER, FRENCH_OTHER_EZ));
    mfallBackCCLangMap.insert(std::make_pair(SPANISH_EZ, SPANISH));
    mfallBackCCLangMap.insert(std::make_pair(SPANISH, SPANISH_EZ));

    mfallbackCCServiceMap.insert(std::make_pair(ENGLISH_EZ, PRIMARY_DIGITAL_SERVICE_NO));
    mfallbackCCServiceMap.insert(std::make_pair(ENGLISH, PRIMARY_DIGITAL_SERVICE_NO));
    mfallbackCCServiceMap.insert(std::make_pair(FRENCH_OTHER_EZ, SECONDARY_DIGITAL_SERVICE_NO));
    mfallbackCCServiceMap.insert(std::make_pair(FRENCH_OTHER, SECONDARY_DIGITAL_SERVICE_NO));
    mfallbackCCServiceMap.insert(std::make_pair(SPANISH_EZ, SECONDARY_DIGITAL_SERVICE_NO));
    mfallbackCCServiceMap.insert(std::make_pair(SPANISH, SECONDARY_DIGITAL_SERVICE_NO));

    mIsOutputStartedForMain = false;
    mIsOutputStartedForPIP = false;
    mIsVod = false;
    mDisplayTypeIsHDMI = false;
    mIs1080pModeActive = false;
}

Avpm::~Avpm()
{
    FNLOG(DL_MSP_MPLAYER);
    dfbExit();
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "delete threadEventQ\n");
    Uset_cancelValidatorT("ciscoSg/look/");
    Uset_cancelValidatorT("ciscoSg/audio");
    Uset_cancelValidatorT("ciscoSg/cc");
    Uset_cancelInterestT("mom.system.mute", settingChangedCB);
    Uset_cancelInterestT("mom.system.volume", settingChangedCB);
    Uset_closeClientT();

    if ((threadEventQueue != NULL) && (mEventHandlerThread))
    {
        threadEventQueue->dispatchEvent(kAvpmThreadExit);
        pthread_mutex_unlock(&sMutex);
        pthread_join(mEventHandlerThread, NULL);         // wait for event thread to exit
        pthread_mutex_lock(&sMutex);
        mEventHandlerThread = 0;
        threadEventQueue = NULL;
    }
}


eMspStatus Avpm::initPicMode(void)
{
    eUseSettingsLevel settingLevel;
    char picMode[MAX_SETTING_VALUE_SIZE] = {0};
    eMspStatus status = kMspStatus_Ok;

    if (USE_RESULT_OK == Uset_getSettingT(NULL, "ciscoSg/look/picSize", MAX_SETTING_VALUE_SIZE, picMode, &settingLevel))
    {

        if (strcmp((const char *)picMode, "normal") == 0)
        {
            picture_mode = tAvpmPictureMode_Normal;
        }
        else if (strcmp((const char *)picMode, "stretch") == 0)
        {
            picture_mode = tAvpmPictureMode_Stretch;
        }
        else if (strcmp((const char *)picMode, "zoom") == 0)
        {
            picture_mode = tAvpmPictureMode_Zoom25;
        }
        else if (strcmp((const char *)picMode, "zoom2") == 0)
        {
            picture_mode = tAvpmPictureMode_Zoom50;
        }
        else
        {
            picture_mode = tAvpmPictureMode_Normal;
        }
    }
    else
    {
        picture_mode = tAvpmPictureMode_Normal;
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Failed to get the ciscoSg/look/picSize i.e picture_mode to default %d\n", picture_mode);
        status = kMspStatus_AvpmError;
    }

    return status;

}

eMspStatus Avpm::setInitialVolume(void)
{
    FNLOG(DL_MSP_AVPM);
    uint32_t volume = 50; //TODO:Default Volume value verify with Arch team
    eUseSettingsLevel settingLevel;
    char volumeStr[MAX_SETTING_VALUE_SIZE];
    char muteSetting[MAX_SETTING_VALUE_SIZE];
    bool isMute = false; //Default unmute
    eMspStatus status = kMspStatus_Error;

    if (USE_RESULT_OK == Uset_getSettingT(NULL, "mom.system.mute", MAX_SETTING_VALUE_SIZE,  muteSetting, &settingLevel))
    {
        isMute = atoi(muteSetting);
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Failed to get the mute setting. Setting to default %d\n", isMute);
    }

    if (USE_RESULT_OK == Uset_getSettingT(NULL, "mom.system.volume", MAX_SETTING_VALUE_SIZE,  volumeStr, &settingLevel))
    {
        volume = atoi(volumeStr);
    }

    if (isMute)
    {
        status = MasterMute(mMainScreenPgrHandle, false);
    }
    else
    {
        MasterMute(mMainScreenPgrHandle, true);
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Setting volume= %d \n", volume);
        status = setMasterVolume(mMainScreenPgrHandle, prev_vol);
    }

    return status;
}

eMspStatus Avpm::GetScreenSize(tCpeDFBScreenIndex index, int *width, int *height)
{
    DFBResult result;
    DirectResult status;
    IDirectFBScreen *pScreen;

    if ((width == NULL) || (height == NULL))
    {
        return kMspStatus_Error;
    }
    if (dfb)
    {
        result = dfb->GetScreen(dfb, index, &pScreen);
        if ((result != DFB_OK) || (pScreen == NULL))
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in getting DFB screen interface. Error code = %d", result);
            return kMspStatus_Error;
        }

        result = pScreen->GetSize(pScreen, width, height);
        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in getting DFB screen size. Error code = %d", result);
            pScreen->Release(pScreen);
            return kMspStatus_Error;
        }

        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Screenindex %d w=%d, h=%d", index, *width, *height);

        status = pScreen->Release(pScreen);
        if (status != DR_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error releasing DFB screen. Error code = %d", result);
            return kMspStatus_Error;
        }
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error: Null data frame buffer");
    }
    return kMspStatus_Ok;
}

// used during PIP/POP to make sure that display surface is cleared once first video frame is drawn to it.
// fixes a timing issue where first frame is decoded very quickly
void Avpm::clearVideoSurface()
{
    FNLOG(DL_MSP_AVPM);

    if (renderToSurface)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d Clear display surface", __FUNCTION__, __LINE__);
        clearSurfaceFromLayer(VANTAGE_HD_PIP);
        clearSurfaceFromLayer(VANTAGE_SD_PIP);
    }

}




void Avpm::logLayerInfo(int layerIndex)
{
    DFBDisplayLayerDescription layerDesc;
    DFBSurfaceCapabilities surfaceCaps;
    DFBSurfacePixelFormat surfaceFmt;
    DFBResult result;
    int width, height;
    IDirectFBDisplayLayer *pLayer;
    IDirectFBSurface *pSurface;
    if (dfb)
    {
        result = dfb->GetDisplayLayer(dfb, layerIndex, &pLayer);
        if (pLayer == NULL)
        {
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Could not 'get' layer_id = %d", layerIndex);
            return;
        }
        memset(&layerDesc, 0, sizeof(layerDesc));
        result = pLayer->GetDescription(pLayer, &layerDesc);

        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "layer_id = %d", layerIndex);
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "  ===================");
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "  type         = %d\n",   layerDesc.type);
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "  caps         = 0x%x\n", layerDesc.caps);
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "  name         = %s\n",   layerDesc.name);
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "  level        = %d\n",   layerDesc.level);
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "  regions      = %d\n",   layerDesc.regions);
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "  sources      = %d\n",   layerDesc.sources);
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "  clip_regions = %d\n",   layerDesc.clip_regions);

        result = pLayer->GetSurface(pLayer, &pSurface);
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "  surface = %p\n", pSurface);

        if (pSurface)
        {
            pSurface->GetCapabilities(pSurface, &surfaceCaps);
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "    caps       = 0x%x\n", surfaceCaps);

            pSurface->GetPixelFormat(pSurface, &surfaceFmt);
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "    fmt        = 0x%x\n", surfaceFmt);

            pSurface->GetSize(pSurface, &width, &height);
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "    size       = %dx%d\n", width, height);

            pSurface->Release(pSurface);
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "    NO INFO");
        }
        pLayer->Release(pLayer);
    }
}


void Avpm::dfbExit()
{
    FNLOG(DL_MSP_AVPM);
    if (dfb)
    {
        dfb->Release(dfb);
    }
}

// List of functions which sets the settings through DFB
eMspStatus Avpm::setAudioOutputMode(void)
{
    FNLOG(DL_MSP_AVPM);
    eMspStatus status = kMspStatus_Ok;
    DFBResult result;
    tCpeAvoxOutputMode pMode;

    ProgramHandleSetting *pgrSettings = getProgramHandleSettings(mMainScreenPgrHandle);
    if (pgrSettings && (pgrSettings->avh != NULL))
    {
        eUseSettingsLevel settingLevel;
        char audioOutput[MAX_SETTING_VALUE_SIZE] = {0};
        Uset_getSettingT(NULL, "ciscoSg/audio/audioOutput", MAX_SETTING_VALUE_SIZE, audioOutput, &settingLevel);

        pMode.flags = eCpeAvoxOutputModeFlags_Digital;
        pMode.programHandle = mMainScreenPgrHandle;

        if (strcmp((const char *)audioOutput, "ac3") == 0)
        {
            pMode.digitalMode = eCpeAvoxDigitalOutputMode_AC3;
        }
        else if (strcmp((const char *)audioOutput, "compressed") == 0)
        {
            pMode.digitalMode = eCpeAvoxDigitalOutputMode_AC3;
        }
        else if (strcmp((const char *)audioOutput, "uncompressed") == 0)
        {
            pMode.digitalMode = eCpeAvoxDigitalOutputMode_PCM;
        }

        result = pgrSettings->avh->SetAudioOutputMode(pgrSettings->avh, &pMode);


        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in setting the SetAudioOutputMode---Ignore %d", __PRETTY_FUNCTION__, __LINE__, result);
            status = kMspStatus_AvpmError;

        }
    }

    else
    {
        status = kMspStatus_AvpmError;
    }

    return status;
}


ProgramHandleSetting* Avpm::getProgramHandleSettings(tCpePgrmHandle pgrHandle)
{
    ProgramHandleSetting* p = NULL;
    IAVOutput *avh = NULL;
    IMediaStreamHandler *msh = NULL;
    IVideoStreamHandler *vsh = NULL;
    ITextStreamHandler *tsh = NULL;

    DFBResult result;
    tCpeMshVideoStreamAttributes attrib;
    tCpeMshTextStreamAttributes attrib_text;

    map<tCpePgrmHandle, ProgramHandleSetting*>::iterator iter;
    iter = mMap.find(pgrHandle);
    if (iter != mMap.end())
    {
        p = (ProgramHandleSetting*) iter->second;
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s, %d, Found existing prg setting %p for handle %p",
             __FUNCTION__, __LINE__, p, pgrHandle);
    }
    else
    {
        if (dfb == NULL)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Null dfb instance.", __FUNCTION__, __LINE__);
            return NULL;
        }

        result = dfb->GetInterface(dfb, "IMediaStreamHandler", "default", pgrHandle, (void**)&msh);
        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Could not get IMediaStreamHandler I/F.", __FUNCTION__, __LINE__);
            return NULL;
        }
// this is to work around a CPERP bug. Calling GetVideoStreamHandler on a msh that has no video
// stream handler causes a reboot. When this is fixed, we should be able to remove the mHaveVideo
// variable completely and depend on GetVideoStreamHandler just returning NULL
        if (msh && mHaveVideo)
        {
            attrib.flags = eCpeMshVideoStreamAttribFlag_None;
            result = msh->GetVideoStreamHandler(msh, &attrib, &vsh);

            if (result != DFB_OK)
            {
                dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Could not get IVideoStreamHandler.", __FUNCTION__, __LINE__);
                return NULL;
            }

            attrib_text.flags = eCpeMshTextStreamAttribFlag_None;
            result = msh->GetTextStreamHandler(msh, &attrib_text, &tsh);

            if (result != DFB_OK)
            {
                dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Could not get ITextStreamHandler.", __FUNCTION__, __LINE__);
                return NULL;
            }
        }

        result = dfb->GetInterface(dfb, "IAVOutput", "default", pgrHandle, (void**)&avh);
        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Call get interface error %d", __FUNCTION__, __LINE__, result);
            return NULL;
        }

        p = new ProgramHandleSetting;
        p->hdHandle = 0;
        p->sdHandle = 0;
        p->cchandle = 0;
        p->msh = msh;
        p->vsh = vsh;
        p->avh = avh;
        p->tsh = tsh;
        p->attrib = attrib;
        mMap.insert(pair<tCpePgrmHandle, ProgramHandleSetting*>(pgrHandle, p));
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s,%d, inserted handle %p, setting %p, avh %p", __FUNCTION__, __LINE__, pgrHandle, p, avh);
    }

    return p;
}


eMspStatus Avpm::playToVideoLayer(tCpePgrmHandle pgrHandle)
{
    DFBResult result;
    IVideoStreamHandler *vsh = NULL;
    tCpeMshAssocHandle hdHandle;
    tCpeMshAssocHandle sdHandle;

    FNLOG(DL_MSP_AVPM);

    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(pgrHandle);

    if (!pgrHandleSetting)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in getting the ProgramHandleSetting", __FUNCTION__, __LINE__);
        return kMspStatus_AvpmError;
    }
    else
    {
        vsh = pgrHandleSetting->vsh;
    }

    if (vsh == NULL)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error vsh is NULL", __FUNCTION__, __LINE__);
        return kMspStatus_AvpmError;
    }

    if (pHDLayer)
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, " HDLayer DisplayToDisplayLayer ");
        result = vsh->DisplayToDisplayLayer(vsh, (tCpeVshDisplayToFlags)GetVshDisplayToFlags(), pHDLayer, DisplayCallback_HD, (void *)pgrHandle, &hdHandle);
        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Null dfb instance.", __FUNCTION__, __LINE__);
            return kMspStatus_AvpmError;
        }
        else
        {
            pgrHandleSetting->hdHandle = hdHandle;
        }

    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "DirectFB, pHDLayer is NULL");
        return kMspStatus_AvpmError;
    }

    if (pSDLayer)
    {

        dlog(DL_MSP_AVPM, DLOGL_NOISE, " SDLayer DisplayToDisplayLayer ");
        result = vsh->DisplayToDisplayLayer(vsh, (tCpeVshDisplayToFlags)GetVshDisplayToFlags(), pSDLayer, DisplayCallback_SD, (void *)pgrHandle, &sdHandle);
        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Null dfb instance.", __FUNCTION__, __LINE__);
            return kMspStatus_AvpmError;
        }
        else
        {
            pgrHandleSetting->sdHandle = sdHandle;
        }
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "DirectFB, pSDLayer is NULL");
        return kMspStatus_AvpmError;
    }

    setPictureMode(vsh, hdHandle, sdHandle);

    return kMspStatus_Ok;
}

eMspStatus Avpm::setccStyle(char* cc_opacity_in, tCpeTshOpacity *aOpacity)
{
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) cc_opacity_in %s", __FUNCTION__, __LINE__, cc_opacity_in);

    if (strcmp(cc_opacity_in, "flashing") == 0)
    {
        *aOpacity = CpeTshOpacity_Flashing;
    }
    else if (strcmp(cc_opacity_in, "clear") == 0)
    {
        *aOpacity = CpeTshOpacity_Transparent;
    }
    else if (strcmp(cc_opacity_in, "slight") == 0)
    {
        *aOpacity = CpeTshOpacity_Translucent;
    }
    else
    {
        *aOpacity = CpeTshOpacity_Solid;
    }
    return kMspStatus_Ok;
}

eMspStatus Avpm::setccFontFace(char* cc_font_face, tCpeTshFontFace *aFontFace)
{
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) cc_font_face %s", __FUNCTION__, __LINE__, cc_font_face);
    if (strcmp(cc_font_face, "serifMonospaced") == 0)
    {
        *aFontFace = tCpeTshFontFace_MonoSpacedSerifs;
    }
    else if (strcmp(cc_font_face, "sansSerifMonospaced") == 0)
    {
        *aFontFace = tCpeTshFontFace_MonoSpaced;
    }
    else if (strcmp(cc_font_face, "sansSerifProportional") == 0)
    {
        *aFontFace = tCpeTshFontFace_PropSpaced;
    }
    else if (strcmp(cc_font_face, "casual") == 0)
    {
        *aFontFace = tCpeTshFontFace_Casual;
    }
    else if (strcmp(cc_font_face, "cursive") == 0)
    {
        *aFontFace = tCpeTshFontFace_Cursive;
    }
    else if (strcmp(cc_font_face, "smallCapitals") == 0)
    {
        *aFontFace = tCpeTshFontFace_SmallCapitals;
    }
    else
    {
        *aFontFace = tCpeTshFontFace_PropSpacedSerifs;
    }
    return kMspStatus_Ok;
}

eMspStatus Avpm::setccPenSize(char* cc_pen_size, tCpeTshPenSize *aPenSize)
{
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) cc_pen_size %s", __FUNCTION__, __LINE__, cc_pen_size);
    if (strcmp(cc_pen_size, "small") == 0)
    {
        *aPenSize = tCpeTshPenSize_Small;
    }
    else if (strcmp(cc_pen_size, "large") == 0)
    {
        *aPenSize = tCpeTshPenSize_Large;
    }
    else
    {
        *aPenSize = tCpeTshPenSize_Regular;
    }
    return kMspStatus_Ok;
}

eMspStatus Avpm::setccColor(char* cc_color_in, DFBColor *aColor)
{

    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) cc_color_in %s", __FUNCTION__, __LINE__, cc_color_in);
    if (aColor)
    {
        if (strcmp(cc_color_in, "red") == 0)
        {
            aColor->a = 0x80;
            aColor->r = 255;
            aColor->g = 0;
            aColor->b = 0;
        }
        else if (strcmp(cc_color_in, "black") == 0)
        {
            aColor->a = 0x80;
            aColor->r = 0;
            aColor->g = 0;
            aColor->b = 0;
        }

        else if (strcmp(cc_color_in, "white") == 0)
        {
            aColor->a = 0x80;
            aColor->r = 255;
            aColor->g = 255;
            aColor->b = 255;
        }
        else if (strcmp(cc_color_in, "green") == 0)
        {
            aColor->a = 0x80;
            aColor->r = 0;
            aColor->g = 255;
            aColor->b = 0;
        }
        else if (strcmp(cc_color_in, "blue") == 0)
        {
            aColor->a = 0x80;
            aColor->r = 0;
            aColor->g = 0;
            aColor->b = 255;
        }
        else if (strcmp(cc_color_in, "cyan") == 0)
        {
            aColor->a = 0x80;
            aColor->r = 0;
            aColor->g = 255;
            aColor->b = 255;
        }
        else if (strcmp(cc_color_in, "magenta") == 0)
        {
            aColor->a = 0x80;
            aColor->r = 255;
            aColor->g = 0;
            aColor->b = 255;
        }
        else if (strcmp(cc_color_in, "yellow") == 0)
        {
            aColor->a = 0x80;
            aColor->r = 255;
            aColor->g = 255;
            aColor->b = 0;
        }
    }
    return kMspStatus_Ok;
}

void Avpm::logScalingRects(char *msg, tCpeVshScaleRects rect)
{

    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) %s", __FUNCTION__, __LINE__, msg);
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d)   videoRect.x  %d y %d, w %d h %d", __FUNCTION__, __LINE__, rect.videoRect.x, rect.videoRect.y, rect.videoRect.w, rect.videoRect.h);
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d)   opaqueRect.x %d y %d, w %d h %d", __FUNCTION__, __LINE__, rect.opaqueRect.x, rect.opaqueRect.y, rect.opaqueRect.w, rect.opaqueRect.h);
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d)   scaleRect.x  %d y %d, w %d h %d", __FUNCTION__, __LINE__, rect.scaleRect.x, rect.scaleRect.y, rect.scaleRect.w, rect.scaleRect.h);
}


IDirectFBSurface* Avpm::getsurface(int LayerIndex)
{
    IDirectFBSurface *surf = NULL;

    switch (LayerIndex)
    {
    case VANTAGE_HD_PIP:
        surf = hdGfx2surface;
        break;
    case VANTAGE_SD_PIP:
        surf = sdGfx2surface;
        break;
    case VANTAGE_HD_CC:
        surf = pHdClosedCaptionTextSurface;
        break;
    }

    return surf;
}


eMspStatus Avpm::clearSurfaceFromLayer(int graphicsLayerIndex)
{
    DFBResult res;
    IDirectFBSurface *surf;
    eMspStatus status = kMspStatus_Ok;

    surf = getsurface(graphicsLayerIndex);

    if (surf)
    {
        res = surf->Clear(surf, 0, 0, 0, 0);
        if (res != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d) Error clearing surf. Err: %d", __FUNCTION__, __LINE__, res);
        }

        res = surf->Flip(surf, NULL, DSFLIP_NONE);
        if (res != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d) Error flipping surf. Err: %d", __FUNCTION__, __LINE__, res);
        }

    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d) Surface is NULL ", __FUNCTION__, __LINE__);
        status  = kMspStatus_AvpmError;
    }
    LOG(DLOGL_NOISE, "result %d", status);
    return status;
}
//when mute == "true" The following three outputs are set to "UN-MUTE"
//      1. Component
//      2. HDMI
//      3. SPDIF
//********************************
//when mute == "false" The following three outputs are set to "MUTE"
//      1. Component
//      2. HDMI
//      3. SPDIF
eMspStatus Avpm::MasterMute(tCpePgrmHandle pgrHandle, bool mute)
{

    FNLOG(DL_MSP_AVPM);

    DFBResult result;

    IAVOutput *avh = NULL;

    tCpeAvoxVolume volume;


    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(pgrHandle);



    if (!pgrHandleSetting)

    {

        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in getting the ProgramHandleSetting", __FUNCTION__, __LINE__);

        return kMspStatus_AvpmError;

    }

    else

    {

        avh = pgrHandleSetting->avh;

    }



    volume.flags = eCpeAvoxVolumeFlags_PremixMute;

    if (avh)

    {

        result = avh->GetVolume(avh, &volume);

        if (result != DFB_OK)

        {

            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Could not get output volume(mute). Error: %d", __FUNCTION__, __LINE__, result);

        }



        volume.flags = tCpeAvoxVolumeFlags(volume.flags | eCpeAvoxVolumeFlags_SpdifEnable | eCpeAvoxVolumeFlags_HdmiEnable);

        volume.spdifEnable = volume.hdmiEnable = DFBBoolean(mute);
        volume.premixMute = DFBBoolean(!mute);



        result = avh->SetVolume(avh , &volume);

        if (result != DFB_OK)

        {

            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Could not set output volume. Error: %d", __FUNCTION__, __LINE__, result);

            return kMspStatus_AvpmError;

        }

    }

    return kMspStatus_Ok;

}

eMspStatus
Avpm::setAnalogOutputMode(tCpePgrmHandle pgrHandle)
{
    eMspStatus error = kMspStatus_AvpmError;
    DFBResult result;
    IAVOutput *avh = NULL;

    // get the program handle setting and error check
    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(pgrHandle);
    if (pgrHandleSetting)
    {
        // form a pointer to the output interface
        avh = pgrHandleSetting->avh;

        // if an analog source
        if (mAvpmAudioSrc == tAvpmAudioSrcAnalog && !mbSapEnabled)
        {
            // set analog stereo output and error check
            tCpeAvoxOutputMode pMode;
            pMode.flags = eCpeAvoxOutputModeFlags_Analog;
            pMode.analogMode = eCpeAvoxAnalogOutputMode_Stereo;
            pMode.programHandle = pgrHandle;
            dlog(DL_MSP_AVPM, DLOGL_NORMAL, "%s, %d, SetAudioOutputMode", __FUNCTION__, __LINE__);
            result = avh->SetAudioOutputMode(avh, &pMode);
            if (result != DFB_OK)
            {
                dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s() error: %d when setting analog output mode", __FUNCTION__, result);
            }
            else
            {
                // indicate success
                error = kMspStatus_Ok;
            }
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s() Error, not analog source", __FUNCTION__);
        }
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s() Error getting the ProgramHandleSetting", __FUNCTION__);
    }

    return error;
}

eMspStatus Avpm::mute(tCpePgrmHandle pgrHandle, bool mute)
{
    FNLOG(DL_MSP_AVPM);
    DFBResult result;
    IAVOutput *avh = NULL;
    tCpeAvoxVolume volume;
    UNUSED_PARAM(mute);
    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(pgrHandle);

    if (!pgrHandleSetting)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in getting the ProgramHandleSetting", __FUNCTION__, __LINE__);
        return kMspStatus_AvpmError;
    }
    else
    {
        avh = pgrHandleSetting->avh;
    }

    {

        avh = pgrHandleSetting->avh;
    }

    if (mAvpmAudioSrc == tAvpmAudioSrcAnalog && !mbSapEnabled)
    {
        tCpeAvoxOutputMode pMode;
        pMode.flags = eCpeAvoxOutputModeFlags_Analog;
        pMode.analogMode = eCpeAvoxAnalogOutputMode_Stereo;

        pMode.programHandle = pgrHandle;
        dlog(DL_MSP_AVPM, DLOGL_NORMAL, "%s, %d, SetAudioOutputMode", __FUNCTION__, __LINE__);
        result = avh->SetAudioOutputMode(avh, &pMode);
    }

    volume.flags = eCpeAvoxVolumeFlags_PremixMute;
    if (avh)
    {
        result = avh->GetVolume(avh, &volume);
        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Could not get output volume(mute). Error: %d", __FUNCTION__, __LINE__, result);
        }
//   volume.premixMute = DFBBoolean (!volume.premixMute);
        volume.premixMute = DFBBoolean(mute);
        result = avh->SetVolume(avh , &volume);
        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Could not set output volume. Error: %d", __FUNCTION__, __LINE__, result);
            return kMspStatus_AvpmError;
        }
    }
    return kMspStatus_Ok;
}

eMspStatus Avpm::pauseVideo(tCpePgrmHandle pgrHandle)
{
    FNLOG(DL_MSP_AVPM);
    eMspStatus status = kMspStatus_Ok;
    DFBResult result = DFB_OK;
    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(pgrHandle);

    if (pgrHandleSetting)
    {
        if (pgrHandleSetting->vsh)
        {
            if (pgrHandleSetting->hdHandle)
            {
                result = pgrHandleSetting->vsh->Freeze(pgrHandleSetting->vsh, DFB_TRUE);
                if (result != DFB_OK)
                {
                    dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Call msh->GetVideoStreamHandler HD error %d", __FUNCTION__, __LINE__, result);
                    status  = kMspStatus_AvpmError;
                }
            }

            if (pgrHandleSetting->sdHandle)
            {
                result = pgrHandleSetting->vsh->Freeze(pgrHandleSetting->vsh, DFB_TRUE);
                if (result != DFB_OK)
                {
                    dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Call msh->GetVideoStreamHandler SD error %d", __FUNCTION__, __LINE__, result);
                    status  = kMspStatus_AvpmError;
                }
            }
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d) NULL pointer video streamHandle", __FUNCTION__, __LINE__);
            status  = kMspStatus_AvpmError;
        }
        if (pgrHandleSetting->tsh && pgrHandleSetting->cchandle != 0)
        {
            result = pgrHandleSetting->tsh->Stop(pgrHandleSetting->tsh, pgrHandleSetting->cchandle);
            if (result != DFB_OK)
            {

                dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Call tsh->Stop HD error %d", __FUNCTION__, __LINE__, result);
                status  = kMspStatus_AvpmError;
            }

            pgrHandleSetting->cchandle = 0;
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d) NULL pointer ITextStreamHandle", __FUNCTION__, __LINE__);
            // No need to throw an error here as for PIP window cchandle will be always 0
        }

    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Logic ERROR!!!", __FUNCTION__, __LINE__);
        status  = kMspStatus_AvpmError;
    }
    return status;
}




eMspStatus Avpm::freezeVideo(tCpePgrmHandle pgrHandle)
{
    eMspStatus status = kMspStatus_Ok;
    DFBResult result = DFB_OK;

    FNLOG(DL_MSP_AVPM);

    LOG(DLOGL_NOISE, "mIs1080pModeActive: %d", mIs1080pModeActive);
    if (mIs1080pModeActive && mIsVod && isSource1080p(pgrHandle))
    {
        setDefaultDisplayResolution();
    }

    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(pgrHandle);

    if (pgrHandleSetting)
    {
        if (pgrHandleSetting->audioFocus)
        {
            if (false == mIsOutputStartedForMain)
            {
                LOG(DLOGL_ERROR, "Main Output is already Stopped, returning from here");
                return status;
            }
            LOG(DLOGL_NOISE, "Main Output Stopped");
            mIsOutputStartedForMain = false;
        }
        else
        {
            if (false == mIsOutputStartedForPIP)
            {
                LOG(DLOGL_ERROR, "PIP Output is already Stopped, returning from here");
                return status;
            }
            LOG(DLOGL_NOISE, "PIP Output Stopped");
            mIsOutputStartedForPIP = false;
        }

        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) freeze video pgrHandleSetting->cchandle %d", __FUNCTION__, __LINE__, pgrHandleSetting->cchandle);
        if (pgrHandleSetting->tsh && pgrHandleSetting->cchandle != 0)
        {
            result = pgrHandleSetting->tsh->Stop(pgrHandleSetting->tsh, pgrHandleSetting->cchandle);
            if (result != DFB_OK)
            {
                dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Call tsh->Stop HD error %d", __FUNCTION__, __LINE__, result);
                status  = kMspStatus_AvpmError;
            }
            else
            {
                pgrHandleSetting->cchandle = 0;
            }
            if (pHdClosedCaptionTextSurface != NULL)
            {
                pHdClosedCaptionTextSurface->Clear(pHdClosedCaptionTextSurface, 0, 0, 0, 0);

            }
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d) NULL pointer ITextStreamHandle", __FUNCTION__, __LINE__);
            // No need to throw an error here as for PIP window cchandle will be always 0
        }
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "freeze video after pgrHandleSetting->cchandle after render %d", pgrHandleSetting->cchandle);
        if (pgrHandleSetting->vsh)
        {
            if (pgrHandleSetting->hdHandle)
            {
                result = pgrHandleSetting->vsh->Stop(pgrHandleSetting->vsh, pgrHandleSetting->hdHandle);
                if (result != DFB_OK)
                {
                    dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Call msh->GetVideoStreamHandler HD error %d", __FUNCTION__, __LINE__, result);
                    status  = kMspStatus_AvpmError;
                }
            }

            if (pgrHandleSetting->sdHandle)
            {
                result = pgrHandleSetting->vsh->Stop(pgrHandleSetting->vsh, pgrHandleSetting->sdHandle);
                if (result != DFB_OK)
                {
                    dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Call msh->GetVideoStreamHandler SD error %d", __FUNCTION__, __LINE__, result);
                    status  = kMspStatus_AvpmError;
                }
            }
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d) NULL pointer video streamHandle", __FUNCTION__, __LINE__);
            status  = kMspStatus_AvpmError;

        }

    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Logic ERROR!!!", __FUNCTION__, __LINE__);
        status  = kMspStatus_AvpmError;
    }
    return status;
}

eMspStatus Avpm::release(tCpePgrmHandle pgrHandle)
{
    FNLOG(DL_MSP_AVPM);

    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(pgrHandle);

    if (pgrHandleSetting)
    {
        if (!(pgrHandleSetting->audioFocus))
        {
            clearSurfaceFromLayer(VANTAGE_HD_PIP);
            clearSurfaceFromLayer(VANTAGE_SD_PIP);
            renderToSurface = false;
        }

        if (pgrHandleSetting->vsh)
        {
            pgrHandleSetting->vsh->Release(pgrHandleSetting->vsh);
            pgrHandleSetting->vsh = NULL;
        }

        if (pgrHandleSetting->msh)
        {
            pgrHandleSetting->msh->Release(pgrHandleSetting->msh);
            pgrHandleSetting->msh = NULL;
        }

        if (pgrHandleSetting->avh)
        {
            pgrHandleSetting->avh->Release(pgrHandleSetting->avh);
            pgrHandleSetting->avh = NULL;
        }
        if (pgrHandleSetting->tsh)
        {
            //Checking for cc handle and stopping cc render, Making sure, CC rendering is stopped, before release,
            //as CC can be enabled through AVPM port manager in concurrence.
            if (pgrHandleSetting->cchandle != 0)
            {
                DFBResult result = pgrHandleSetting->tsh->Stop(pgrHandleSetting->tsh, pgrHandleSetting->cchandle);
                ClearCCsurface();
                if (result != DFB_OK)
                {
                    LOG(DLOGL_ERROR, "Call tsh->Stop HD error %d", result);
                }
                else
                {
                    LOG(DLOGL_NORMAL, "Disabled CC Success");
                    pgrHandleSetting->cchandle = 0;
                }
            }
            pgrHandleSetting->tsh->Release(pgrHandleSetting->tsh);
            pgrHandleSetting->tsh = NULL;
        }
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Logic ERROR!!!", __FUNCTION__, __LINE__);
    }

    return kMspStatus_Ok;

}
eMspStatus Avpm::setMasterVolume(tCpePgrmHandle pgrHandle, int vol)
{
    FNLOG(DL_MSP_AVPM);
    DFBResult result;
    IAVOutput *avh = NULL;
    tCpeAvoxVolume volume;
    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(pgrHandle);

    if (!pgrHandleSetting)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in getting the ProgramHandleSetting", __FUNCTION__, __LINE__);
        return kMspStatus_AvpmError;
    }
    else
    {
        avh = pgrHandleSetting->avh;
    }

    int avol = 0;

    if (mAvpmAudioSrc == tAvpmAudioSrcAnalog && !mbSapEnabled)
    {
        tCpeAvoxOutputMode pMode;
        pMode.flags = eCpeAvoxOutputModeFlags_Analog;
        pMode.analogMode = eCpeAvoxAnalogOutputMode_Stereo;
        pMode.programHandle = pgrHandle;
        dlog(DL_MSP_AVPM, DLOGL_NORMAL, "%s, %d, SetAudioOutputMode", __FUNCTION__, __LINE__);
        result = avh->SetAudioOutputMode(avh, &pMode);
    }

    volume.flags = eCpeAvoxVolumeFlags_StereoVolume;
    if (avh)
    {
        if (DFB_OK != (result = avh->GetVolume(avh, &volume)))
        {
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Couldn't Get Volume result %d ", result);
        }
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Get Volume %d result %d ", vol, result);
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Get Volume falgs %d result %d ", volume.flags, result);

        if (vol > MAX_VOL)
        {
            vol = MAX_VOL;
        }
        avol = -1 * vol_table[vol / 5];
        volume.stereoVolumeLevel = avol;
        if (DFB_OK != (result = avh->SetVolume(avh, &volume)))
        {
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, " Setting Volume result %d ", result);
        }
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Set Volume %d result %d ", avol, result);
    }
    return kMspStatus_Ok;

}

eMspStatus Avpm::setdigitalAudioSetMode(void)
{
    FNLOG(DL_MSP_AVPM);
    return kMspStatus_Ok;
}

eMspStatus Avpm::setAC3AudioRange(tCpePgrmHandle pgrHandle, tAvpmAudioRange audio_range)
{
    FNLOG(DL_MSP_MPLAYER);
    tCpeAvoxVolume ac3_volume;
    IAVOutput *avh = NULL;
    DFBResult result;

    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(pgrHandle);
    if (!pgrHandleSetting)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d) Error in getting the ProgramHandleSetting", __FUNCTION__, __LINE__);
        return kMspStatus_AvpmError;
    }
    else
    {
        avh = pgrHandleSetting->avh;
    }


    ac3_volume.flags = eCpeAvoxVolumeFlags_AC3DRCmode;

    if (audio_range == tAvpmAudioRangeNarrow)
    {
        ac3_volume.AC3DRCmode = tCpeAvoxAC3DRCmode_High;
    }
    if (audio_range == tAvpmAudioRangeNormal)
    {
        ac3_volume.AC3DRCmode = tCpeAvoxAC3DRCmode_Medium;
    }
    if (audio_range == tAvpmAudioRangeWide)
    {
        ac3_volume.AC3DRCmode = tCpeAvoxAC3DRCmode_None;
    }

    if (avh)
    {
        result = avh->SetVolume(avh , &ac3_volume);
        if (result != DFB_OK)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d) Could not set output ac3 volume. Error: %d", __FUNCTION__, __LINE__, result);
            return kMspStatus_AvpmError;
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Set Range successfully");
        }
    }
    return kMspStatus_Ok;
}

eMspStatus Avpm::setTVAspectRatio(IVideoStreamHandler *vsh, tCpeMshAssocHandle hdHandle, tCpeMshAssocHandle sdHandle, eAvpmVideoRects videoRectsType)
{
    tCpeVshScaleRects rectHd;
    tCpeVshScaleRects rectSd;


    memset(&rectHd, 0, sizeof(tCpeVshScaleRects));
    memset(&rectSd, 0, sizeof(tCpeVshScaleRects));

    char framebuff[MAX_FRAMERATE_SIZE] = {0};
    int  convertFrameRate = *reinterpret_cast<int*>(&frame_asp);

    rectHd.flags = tCpeVshScaleRectsFlags_All;
    rectSd.flags = tCpeVshScaleRectsFlags_All;
    setStereoDepth(rectHd);
    setStereoDepth(rectSd);


    FNLOG(DL_MSP_AVPM);

    if (vsh == NULL)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error: Null vsh ");
        return kMspStatus_AvpmError;
    }
    snprintf(framebuff, SIZE_OF_COMP, "%d", convertFrameRate);
    convertFrameRate = atoi(framebuff);
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, " user_aspect_ratio =  %d  frame_asp =  %f frame_buff=%s reinterpret_cast<frame_asp>=%d", user_aspect_ratio, frame_asp, framebuff, convertFrameRate);
    // really need to take into account PIP/POP here
    if (user_aspect_ratio == tAvpmTVAspectRatio16x9)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "user_aspect_ratio == tAvpmTVAspectRatio16X9 galio_rect.w %d ", galio_rect.w);

        if ((convertFrameRate < frameRate_16x19_4x3) && (picture_mode != tAvpmPictureMode_Stretch))
        {
            //HDTV,SD channel
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "f=1.33 uasp=16*9 fullscreen galio_width %d ", galio_rect.w);

            int width = rectHd.videoRect.w = (3 * mMaxOutputScreenWidthHD) / 4;
            rectHd.videoRect.x = (mMaxOutputScreenWidthHD - width) / 2;
            int sd_width = rectSd.videoRect.w = (3 * mMaxOutputScreenWidthSD) / 4;
            rectSd.videoRect.x = (mMaxOutputScreenWidthSD - sd_width) / 2;
            rectHd.videoRect.y = 0;
            rectHd.videoRect.h = mMaxOutputScreenHeightHD;

            rectSd.videoRect.y = 0;
            rectSd.videoRect.h = mMaxOutputScreenHeightSD;

            rectHd.opaqueRect = rectHd.videoRect;
            rectHd.scaleRect = rectHd.videoRect;
            rectSd.opaqueRect = rectSd.videoRect;
            rectSd.scaleRect = rectSd.videoRect;

            rectHd.flags = tCpeVshScaleRectsFlags_All;
            rectSd.flags = tCpeVshScaleRectsFlags_All;

        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, " f=16*9 uasp=16*9 ");
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "f=16*9 uasp=16*9 full screen galio_width %d ", galio_rect.w);
            rectHd.videoRect.h = mMaxOutputScreenHeightHD;
            rectHd.videoRect.y = mMaxOutputScreenHeightHD;
            if ((mMaxOutputScreenHeightHD - rectHd.videoRect.y) < rectHd.videoRect.h)
            {
                rectHd.videoRect.y = mMaxOutputScreenHeightHD - rectHd.videoRect.h;
            }

            rectHd.videoRect.w = mMaxOutputScreenWidthHD;
            rectHd.videoRect.x = mMaxOutputScreenWidthHD;
            if ((mMaxOutputScreenWidthHD - rectHd.videoRect.x) < rectHd.videoRect.w)
            {
                rectHd.videoRect.x = mMaxOutputScreenWidthHD - rectHd.videoRect.w;
            }

            rectHd.opaqueRect = rectHd.videoRect;
            rectHd.scaleRect = rectHd.videoRect;
            rectSd.videoRect.h = mMaxOutputScreenHeightSD;
            rectSd.videoRect.y = mMaxOutputScreenHeightSD;
            if ((mMaxOutputScreenHeightSD - rectSd.videoRect.y) < rectSd.videoRect.h)
            {
                rectSd.videoRect.y = mMaxOutputScreenHeightSD - rectSd.videoRect.h;
            }

            rectSd.videoRect.w = mMaxOutputScreenWidthSD;
            rectSd.videoRect.x = mMaxOutputScreenWidthSD;
            if ((mMaxOutputScreenWidthSD - rectSd.videoRect.x) < rectSd.videoRect.w)
            {
                rectSd.videoRect.x = mMaxOutputScreenWidthSD - rectSd.videoRect.w;
            }

            rectSd.opaqueRect = rectSd.videoRect;
            rectSd.scaleRect = rectSd.videoRect;
            rectHd.flags = tCpeVshScaleRectsFlags_All;
            rectSd.flags = tCpeVshScaleRectsFlags_All;
        }
    }
    else if (user_aspect_ratio == tAvpmTVAspectRatio4x3)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "user_aspect_ratio == tAvpmTVAspectRatio4x3 ");

        if ((convertFrameRate > frameRate_16x19_4x3) && (picture_mode != tAvpmPictureMode_Stretch))
        {
            //SDTV,HD channel
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "fr_asp 16*9 us_asp 4x3 full screen galio_rect.w %d ", galio_rect.w);
            int height = rectHd.videoRect.h = (3 * mMaxOutputScreenHeightHD) / 4;
            rectHd.videoRect.y = (mMaxOutputScreenHeightHD - height) / 2;
            int sd_height = rectSd.videoRect.h = (3 * mMaxOutputScreenHeightSD) / 4;
            rectSd.videoRect.y = (mMaxOutputScreenHeightSD - sd_height) / 2;

            rectHd.videoRect.x = 0;
            rectHd.videoRect.w = mMaxOutputScreenWidthHD;
            rectSd.videoRect.x = 0;
            rectSd.videoRect.w = mMaxOutputScreenWidthSD;
            rectHd.opaqueRect = rectHd.videoRect;
            rectHd.scaleRect = rectHd.videoRect;

            rectSd.opaqueRect = rectSd.videoRect;
            rectSd.scaleRect = rectSd.videoRect;
            rectHd.flags = tCpeVshScaleRectsFlags_All;
            rectSd.flags = tCpeVshScaleRectsFlags_All;

        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "fr_asp 4*3 us_asp 4x3 full screen galio_rect.w %d ", galio_rect.w);
            rectHd.videoRect.h = mMaxOutputScreenHeightHD;
            rectHd.videoRect.y = mMaxOutputScreenHeightHD;
            if ((mMaxOutputScreenHeightHD - rectHd.videoRect.y) < rectHd.videoRect.h)
            {
                rectHd.videoRect.y = mMaxOutputScreenHeightHD - rectHd.videoRect.h;
            }

            rectHd.videoRect.w = mMaxOutputScreenWidthHD;
            rectHd.videoRect.x = mMaxOutputScreenWidthHD;
            if ((mMaxOutputScreenWidthHD - rectHd.videoRect.x) < rectHd.videoRect.w)
            {
                rectHd.videoRect.x = mMaxOutputScreenWidthHD - rectHd.videoRect.w;
            }

            rectHd.opaqueRect = rectHd.videoRect;
            rectHd.scaleRect = rectHd.videoRect;
            rectSd.videoRect.h = mMaxOutputScreenHeightSD;
            rectSd.videoRect.y = mMaxOutputScreenHeightSD;
            if ((mMaxOutputScreenHeightSD - rectSd.videoRect.y) < rectSd.videoRect.h)
            {
                rectSd.videoRect.y = mMaxOutputScreenHeightSD - rectSd.videoRect.h;
            }

            rectSd.videoRect.w = mMaxOutputScreenWidthSD;
            rectSd.videoRect.x = mMaxOutputScreenWidthSD;
            if ((mMaxOutputScreenWidthSD - rectSd.videoRect.x) < rectSd.videoRect.w)
            {
                rectSd.videoRect.x = mMaxOutputScreenWidthSD - rectSd.videoRect.w;
            }

            rectSd.opaqueRect = rectSd.videoRect;
            rectSd.scaleRect = rectSd.videoRect;
            rectHd.flags = tCpeVshScaleRectsFlags_All;
            rectSd.flags = tCpeVshScaleRectsFlags_All;
        }

    }
    SetVideoRects(vsh, hdHandle, sdHandle, rectHd, rectSd, videoRectsType);
    logScalingRects("setTVAspectRatio", rectHd);

    return kMspStatus_Ok;
}
void Avpm::SetVideoRects(IVideoStreamHandler *vsh, tCpeMshAssocHandle hdHandle, tCpeMshAssocHandle sdHandle,
                         tCpeVshScaleRects HDrect, tCpeVshScaleRects SDrect, eAvpmVideoRects videoRectsType)
{
    FNLOG(DL_MSP_AVPM);

    switch (videoRectsType)
    {
    case kAvpmSDVideo:
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s:%d:For Applying to SD only", __FUNCTION__, __LINE__);
        memcpy(&SDRects, &SDrect, sizeof(tCpeVshScaleRects));
        vsh->SetScalingRects(vsh, sdHandle, &SDrect);
    }
    break;
    case kAvpmHDVideo:
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s:%d:For Applying to HD only", __FUNCTION__, __LINE__);
        memcpy(&HDRects, &HDrect, sizeof(tCpeVshScaleRects));
        vsh->SetScalingRects(vsh, hdHandle, &HDrect);
    }
    break;
    case kAvpmBothHDSD:
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s:%d:For Applying to HD and SD", __FUNCTION__, __LINE__);
        logScalingRects("In kAvpmBothHDSD setTVAspectRatio", HDrect);
        vsh->SetScalingRects(vsh, hdHandle, &HDrect);
        vsh->SetScalingRects(vsh, sdHandle, &SDrect);
    }
    break;
    case kAvpmHDCpy:
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s:%d:COPYING to HD only", __FUNCTION__, __LINE__);
        memcpy(&HDRects, &HDrect, sizeof(tCpeVshScaleRects));
        break;
    case  kAvpmSDCpy:
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s:%d:Copying to SD only", __FUNCTION__, __LINE__);
        memcpy(&SDRects, &SDrect, sizeof(tCpeVshScaleRects));
    }
    break;

    default:
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s:%d:ERROR Must not be here", __FUNCTION__, __LINE__);
        break;

    }
}


eMspStatus Avpm::setPictureMode(IVideoStreamHandler *vsh, tCpeMshAssocHandle hdHandle, tCpeMshAssocHandle sdHandle, eAvpmVideoRects videoRectsType)
{
    tCpeVshScaleRects rectHd;
    tCpeVshScaleRects rectSd;

    memset(&rectHd, 0, sizeof(tCpeVshScaleRects));
    memset(&rectSd, 0, sizeof(tCpeVshScaleRects));

    rectHd.flags = tCpeVshScaleRectsFlags_All;
    rectSd.flags = tCpeVshScaleRectsFlags_All;

    setStereoDepth(rectHd);
    setStereoDepth(rectSd);


    FNLOG(DL_MSP_AVPM);

    if (vsh == NULL)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error: Null vsh ");
        return kMspStatus_AvpmError;
    }

    if (galio_rect.w != GALIO_MENU_W && galio_rect.w != PIP_WINDOW_WIDTH)
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d) Full Screen ", __FUNCTION__, __LINE__);

        switch (picture_mode)
        {

        case tAvpmPictureMode_Normal :
        case tAvpmPictureMode_Stretch :
            setTVAspectRatio(vsh, hdHandle, sdHandle, videoRectsType);
            break;

        case tAvpmPictureMode_Zoom25 :
        {
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Avpm Zoom25");
            rectHd.videoRect.w = mMaxOutputScreenWidthHD ;
            rectHd.videoRect.h = mMaxOutputScreenHeightHD;
            rectHd.videoRect.x = 0;
            rectHd.videoRect.y = 0;
            rectHd.opaqueRect = rectHd.videoRect;
            rectHd.scaleRect.x = -150;
            rectHd.scaleRect.y = -150;
            rectHd.scaleRect.w = mMaxOutputScreenWidthHD + 480;
            rectHd.scaleRect.h = mMaxOutputScreenHeightHD + 480;

            rectSd.videoRect.w = mMaxOutputScreenWidthSD;
            rectSd.videoRect.h = mMaxOutputScreenWidthSD;
            rectSd.videoRect.x = 0;
            rectSd.videoRect.y = 0;
            rectSd.opaqueRect = rectSd.videoRect;
            rectSd.scaleRect.x = -150;
            rectSd.scaleRect.y = -150;
            rectSd.scaleRect.w = mMaxOutputScreenWidthSD + 200;
            rectSd.scaleRect.h = mMaxOutputScreenHeightSD + 200;

            rectHd.flags = tCpeVshScaleRectsFlags_All;
            rectSd.flags = tCpeVshScaleRectsFlags_All;
            SetVideoRects(vsh, hdHandle, sdHandle, rectHd, rectSd, videoRectsType);
        }
        break;

        case tAvpmPictureMode_Zoom50 :
        {
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Avpm Zoom50");
            rectHd.videoRect.w = mMaxOutputScreenWidthHD ;
            rectHd.videoRect.h = mMaxOutputScreenHeightHD;
            rectHd.videoRect.x = 0;
            rectHd.videoRect.y = 0;
            rectHd.opaqueRect = rectHd.videoRect;
            rectHd.scaleRect.x = -400;
            rectHd.scaleRect.y = -400;
            rectHd.scaleRect.w = mMaxOutputScreenWidthHD + 960;
            rectHd.scaleRect.h = mMaxOutputScreenHeightHD + 960;

            rectSd.videoRect.w = mMaxOutputScreenWidthSD;
            rectSd.videoRect.h = mMaxOutputScreenWidthSD;
            rectSd.videoRect.x = 0;
            rectSd.videoRect.y = 0;
            rectSd.opaqueRect = rectSd.videoRect;
            rectSd.scaleRect.x = -150;
            rectSd.scaleRect.y = -150;
            rectSd.scaleRect.w = mMaxOutputScreenWidthSD + 360;
            rectSd.scaleRect.h = mMaxOutputScreenHeightSD + 360;

            rectHd.flags = tCpeVshScaleRectsFlags_All;
            rectSd.flags = tCpeVshScaleRectsFlags_All;
            SetVideoRects(vsh, hdHandle, sdHandle, rectHd, rectSd, videoRectsType);
        }
        break;

        }
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "After HD videoRect.x %d,y %d,w %d,h %d", rectHd.videoRect.x, rectHd.videoRect.y, rectHd.videoRect.w, rectHd.videoRect.h);
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "HDopaqueRect.x %d,y %d,w %d,h %d", rectHd.opaqueRect.x, rectHd.opaqueRect.y, rectHd.opaqueRect.w, rectHd.opaqueRect.h);
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "HDscaleRect.x %d,y %d,w %d,h %d", rectHd.scaleRect.x, rectHd.scaleRect.y, rectHd.scaleRect.w, rectHd.scaleRect.h);

    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d) Not Full Screen ", __FUNCTION__, __LINE__);
        ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(mMainScreenPgrHandle);
        if (!pgrHandleSetting)
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s(%d) Error in getting the ProgramHandleSetting", __FUNCTION__, __LINE__);
            return kMspStatus_AvpmError;
        }
        setWindow(pgrHandleSetting->rect, pgrHandleSetting->vsh, eCpeDFBScreenIndex_HD, pgrHandleSetting->hdHandle);
        setWindow(pgrHandleSetting->rect, pgrHandleSetting->vsh, eCpeDFBScreenIndex_SD, pgrHandleSetting->sdHandle);
    }

    return kMspStatus_Ok;
}

eMspStatus Avpm::setDisplayResolution(eAvpmResolution mode)
{
    DFBResult result;
    DFBScreenEncoderConfig enc_descriptions;
    IDirectFBScreen *pHD = NULL;
    IDirectFBScreen *pSD = NULL;

    FNLOG(DL_MSP_AVPM);

    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Set Display Mode: %d", mode);

    memset(&enc_descriptions, 0, sizeof(enc_descriptions));

// Get DFB Screens
    result = dfb->GetScreen(dfb, eCpeDFBScreenIndex_HD, &pHD);
    if ((result != DFB_OK) || (pHD == NULL))
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in getting DFB HD screen interface. Error code = %d", result);
        return kMspStatus_Error;
    }

    result = dfb->GetScreen(dfb, eCpeDFBScreenIndex_SD, &pSD);
    if ((result != DFB_OK) || (pSD == NULL))
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in getting DFB SD screen interface. Error code = %d", result);
        pHD->Release(pHD);
        return kMspStatus_Error;
    }


    result = pHD->GetEncoderConfiguration(pHD, 0, &enc_descriptions);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in getting encoder descriptions. Err: %d", __FUNCTION__, __LINE__, result);
    }
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "HD encoder res %d scanmode %d tv_std %d", enc_descriptions.resolution, enc_descriptions.scanmode, enc_descriptions.tv_standard);

    result = pSD->GetEncoderConfiguration(pSD, 0, &enc_descriptions);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in getting encoder descriptions. Err: %d", __FUNCTION__, __LINE__, result);
    }
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "SD encoder res %d scan mode %d tv std %d", enc_descriptions.resolution, enc_descriptions.scanmode, enc_descriptions.tv_standard);

    if (pSD && pHD)
    {
        ConfigureEncoderSettings(&enc_descriptions, mode);

        result = pHD->SetEncoderConfiguration(pHD, 0, &enc_descriptions);
        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in setting HD encoder descriptions. Err: %d", __FUNCTION__, __LINE__, result);
        }

        result = pSD->SetEncoderConfiguration(pSD, 0, &enc_descriptions);
        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in setting SD encoder descriptions. Err: %d", __FUNCTION__, __LINE__, result);
        }

        pHD->Release(pHD);
        pSD->Release(pSD);

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


        GetScreenSize(eCpeDFBScreenIndex_HD, &mMaxOutputScreenWidthHD, &mMaxOutputScreenHeightHD);
        GetScreenSize(eCpeDFBScreenIndex_SD, &mMaxOutputScreenWidthSD, &mMaxOutputScreenHeightSD);



        if (mMainScreenPgrHandle)
        {
            ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(mMainScreenPgrHandle);
            IVideoStreamHandler *vsh = NULL;
            if (!pgrHandleSetting)
            {
                dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in getting the ProgramHandleSetting", __FUNCTION__, __LINE__);
                return kMspStatus_AvpmError;
            }
            else
            {
                vsh = pgrHandleSetting->vsh;

            }
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Resolution:StWindowmain");
            setPictureMode(pgrHandleSetting->vsh, pgrHandleSetting->hdHandle, pgrHandleSetting->sdHandle);
            setWindow(pgrHandleSetting->rect, pgrHandleSetting->vsh, eCpeDFBScreenIndex_HD, pgrHandleSetting->hdHandle);
            setWindow(pgrHandleSetting->rect, pgrHandleSetting->vsh, eCpeDFBScreenIndex_SD, pgrHandleSetting->sdHandle);
        }
    }
    return kMspStatus_Ok;

}

eMspStatus Avpm::setRFmodeSetOutputChan(void)
{
    FNLOG(DL_MSP_AVPM);
    eMspStatus status = kMspStatus_Ok;
    DFBResult result;
    unsigned int rf_channel = 3;

    ProgramHandleSetting *pgrSettings = getProgramHandleSettings(mMainScreenPgrHandle);
    if (pgrSettings)
    {
        if (pgrSettings->avh != NULL)
        {
            eUseSettingsLevel settingLevel;
            char rfchannel[MAX_SETTING_VALUE_SIZE] = {0};
            Uset_getSettingT(NULL, "ciscoSg/sys/rfOutputChannel", MAX_SETTING_VALUE_SIZE, rfchannel, &settingLevel);

            if (strcmp((const char *)rfchannel, "channel3") == 0)
            {
                rf_channel = 3;
            }
            else if (strcmp((const char *)rfchannel, "channel4") == 0)
            {
                rf_channel = 4;
            }

            result = pgrSettings->avh->SetRFOutputChannel(pgrSettings->avh, rf_channel);
            if (result != DFB_OK)
            {
                dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in setting the SetRFOutputChannel---Ignore %d", __PRETTY_FUNCTION__, __LINE__, result);
                status = kMspStatus_AvpmError;
            }

        }
        else
        {
            status = kMspStatus_AvpmError;
        }

    }
    return status;
}


/* Copy Control Information Settings Start */

// This function will be called at boot up time.

eMspStatus Avpm::SetHDCP(HDCPState state)
{
    int status = cpe_drm_set(eCpeDRMGetSetNames_HDCP, state);
    if (status == kCpe_NoErr)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Error in DRM SetHDCP %d", status);
        return kMspStatus_Ok;
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in DRM SetHDCP %d", status);
        return kMspStatus_Error;
    }
}

eMspStatus Avpm::SetCITState(CITState state)
{
    LOG(DLOGL_REALLY_NOISY, " Avpm::SetCITState(Image Constraint) is called with the value %u", state);
    int status = cpe_drm_set(eCpeDRMGetSetNames_CIT, state);
    if (status == kCpe_NoErr)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Success in setting the image constraint ");
        return kMspStatus_Ok;
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error in DRM SetCITState %d", status);
        return kMspStatus_Error;
    }
}

eMspStatus Avpm::SetAPS(MacroVisionAPS state)
{
    LOG(DLOGL_REALLY_NOISY, " Avpm::SetAPS(MacroVisionAPS) is called with the value %u", state);
    int status = cpe_drm_set(eCpeDRMGetSetNames_Macrovision, state);
    if (status == kCpe_NoErr)
    {
        LOG(DLOGL_REALLY_NOISY, " SetAPS eCpeDRMGetSetNames_Macrovision success!! ");
        return kMspStatus_Ok;
    }
    else
    {
        LOG(DLOGL_ERROR, "Error in DRM SetAPS %d", status);
        return kMspStatus_Error;
    }
}

eMspStatus Avpm::SetEMI(uint8_t emi)
{
    LOG(DLOGL_REALLY_NOISY, "Avpm::%s called with emi %d -- NOT Supported yet", __FUNCTION__, emi);
    return kMspStatus_Ok;
}

eMspStatus Avpm::SetCCIBits(uint8_t ccibits, bool isSOC)
{
    mCCIbyte = ccibits;
    mIsSoc = isSOC;
    if (threadEventQueue)
    {
        threadEventQueue->dispatchEvent(kAvpmUpdateCCI);
    }
    return kMspStatus_Ok;
}

/* Copy Control Information Settings Ends */

void Avpm::SetAudioLangCB(void *aCbData, AudioLangChangedCB aLangChangedCB)
{
    mAudioLangChangedCb = aLangChangedCB;
    mAudioLangCbData = aCbData;
}

void Avpm::SetSapChangedCB(void *aCbData, SapChangedCB sapChangeCb)
{
    mSapChangeCb = sapChangeCb;
    mpSapChangedCbData = aCbData;
}

void Avpm::registerAVSettings()
{
    eUse_StatusCode status = Uset_registerValidatorT(settingChangedCB, "ciscoSg/look", this);
    if (status != USE_RESULT_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Failed to register ciscoSg/look/aspect");
    }
    status = Uset_registerValidatorT(settingChangedCB, "ciscoSg/audio", this);
    if (status != USE_RESULT_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Failed to register ciscoSg/audio/");
    }

    status = Uset_registerValidatorT(settingChangedCB, "ciscoSg/cc", this);
    if (status != USE_RESULT_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Failed to register ciscoSg/cc/");
    }
    status = Uset_registerValidatorT(settingChangedCB, "ciscoSg/sys", this);
    if (status != USE_RESULT_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Failed to register ciscoSg/sys/");
    }

}

void Avpm::registerAVSettings(char *pTag)
{
    char *CiscoTag = "ciscoSg";
    eUse_StatusCode status;
    dlog(DL_MSP_AVPM, DLOGL_FUNCTION_CALLS, "%d Entering %s", __LINE__, __FUNCTION__);
    if (NULL != pTag)
    {
        if (strncmp(pTag, CiscoTag, strlen(CiscoTag)) == 0)
        {

            dlog(DL_MSP_AVPM, DLOGL_NORMAL, "%d:%s The Tag=%s", __LINE__, __FUNCTION__, pTag);

            status = Uset_registerValidatorT(settingChangedCB, pTag, this);
            if (status != USE_RESULT_OK)
            {

                dlog(DL_MSP_AVPM, DLOGL_ERROR, "%d:%s:Failed to register %s \n", __LINE__, __FUNCTION__, pTag);
            }
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_NORMAL, "%d:%s The Tag=%s", __LINE__, __FUNCTION__, pTag);

            eUse_StatusCode status = Uset_registerInterestT(settingChangedCB, pTag, this);
            if (status != USE_RESULT_OK)
            {

                dlog(DL_MSP_AVPM, DLOGL_ERROR, "%d:%s:Failed to register %s \n", __LINE__, __FUNCTION__, pTag);
            }
        }
    }
    dlog(DL_MSP_AVPM, DLOGL_FUNCTION_CALLS, "%d Exiting %s\n", __LINE__, __FUNCTION__);
}


void Avpm::registerOpaqueSettings()
{
    eUse_StatusCode status = Uset_registerInterestT(settingChangedCB, "mom.system.mute", this);

    if (status != USE_RESULT_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Failed to register mom.system.mute");
    }
    status = Uset_registerInterestT(settingChangedCB, "mom.system.volume", this);
    if (status != USE_RESULT_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Failed to register mom.system.volume");
    }
}


int Avpm :: displaySdBarker(bool state)
{
    // TODO:  This code is not called in MSP tag 04.12.00.  Do we need to keep it around?

    dlog(DL_MSP_AVPM, DLOGL_NOISE, "Display SD Barker %d\n", state);
    DFBDisplayLayerConfig config;
    IDirectFBDisplayLayer *sdLayer = NULL;
    IDirectFBImageProvider *imageProvider = NULL;
    IDirectFBSurface *sdSurf = NULL;
    DFBResult ret;

    if (dfb == NULL)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error: Null dfb");
        return kMspStatus_Error;
    }
    // get the SD display layer to change configuration
    ret = dfb->GetDisplayLayer(dfb, VANTAGE_SD_MAINAPP, &sdLayer);
    if ((ret != DFB_OK) || (sdLayer == NULL))
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error: dfb->GetDisplayLayer ret: %d", ret);
        dfb->Release(dfb);
        return kMspStatus_Error;
    }

    // set cooperative level to DLSCL_ADMINISTRATIVE to change level configuration
    ret = sdLayer->SetCooperativeLevel(sdLayer, DLSCL_ADMINISTRATIVE);
    if (ret != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Error: dfb->SetCooperativeLevel ret: %d", ret);
        sdLayer->Release(sdLayer);
        dfb->Release(dfb);
        return kMspStatus_Error;
    }

    // TODO:  This should be wrapped in class which handles initialization

    memset(&config, 0, sizeof(config));
    config.flags = (DFBDisplayLayerConfigFlags)(DLCONF_PIXELFORMAT | DLCONF_WIDTH | DLCONF_HEIGHT | DLCONF_BUFFERMODE | DLCONF_SOURCE);
    config.width = 640;
    config.height = 480;
    config.buffermode = DLBM_FRONTONLY;
    config.pixelformat =  DSPF_ARGB;

    if (state)
    {
        config.source = 0;  // unique buffer not mirror of HD
    }
    else
    {
        config.source = 1;  // mirror of HD
    }

    dlog(DL_MSP_AVPM, DLOGL_NOISE, "config.source: %d", config.source);

    ret = sdLayer->SetConfiguration(sdLayer, &config);
    if (ret != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "SetConfiguration fails %dx%dx%d (0x%x flags=0x%x)!!!!!!!!!!!", config.width, config.height, config.pixelformat, ret, config.flags);
    }

    if (state)
    {
        // create image provider for SD screen
        ret = dfb->CreateImageProvider(dfb, "/sd_screen.jpg", &imageProvider);
        if (ret != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%d -- CreateImageProvider failed code %d", __LINE__, ret);
        }

        if (imageProvider)
        {
            ret = sdLayer->GetSurface(sdLayer, &sdSurf);
            if ((ret != DFB_OK) || (sdSurf == NULL))
            {
                dlog(DL_MSP_AVPM, DLOGL_ERROR, "GetSurface fails (0x%x)", ret);
            }
            else
            {
                ret = imageProvider->RenderTo(imageProvider, sdSurf, NULL);
                if (ret != DFB_OK)
                {
                    dlog(DL_MSP_AVPM, DLOGL_ERROR, "%d -- Render to SD surface failed code %d", __LINE__, ret);
                }
            }
        }
    }

    if (imageProvider)
    {
        imageProvider->Release(imageProvider);
    }

    sdLayer->Release(sdLayer);

    return kMspStatus_Ok;
}


eMspStatus Avpm ::setCc(bool isCaptionEnabled)
{
    DFBResult result;
    tCpeMshTextStreamAttributes attrib_text;

    FNLOG(DL_MSP_AVPM);

    isClosedCaptionEnabled = isCaptionEnabled;

    if (dfb == NULL)
    {
        LOG(DLOGL_ERROR, "Error null dfb");
        return kMspStatus_AvpmError;
    }


    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(mMainScreenPgrHandle);
    if (!pgrHandleSetting)
    {
        LOG(DLOGL_ERROR, "Error null pgrHandleSetting");
        return kMspStatus_AvpmError;
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
    if (isCaptionEnabled)
    {
        if (pgrHandleSetting->tsh != NULL && pgrHandleSetting->cchandle != 1)
        {
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
            LOG(DLOGL_REALLY_NOISY, " CC is in Rendering State");
        }
    }
    else
    {
        if (pgrHandleSetting->tsh != NULL && (pgrHandleSetting->cchandle != 0))
        {
            DFBResult result = pgrHandleSetting->tsh->Stop(pgrHandleSetting->tsh, pgrHandleSetting->cchandle);
            ClearCCsurface();
            if (result != DFB_OK)
            {
                LOG(DLOGL_ERROR, "Call tsh->Stop HD error %d", result);
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "Disabled CC Success");
                pgrHandleSetting->cchandle = 0;
            }
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, " CC is not rendering");
        }
    }

    LOG(DLOGL_REALLY_NOISY, "pgrHandleSetting->cchandle = %d", pgrHandleSetting->cchandle);

    return kMspStatus_Ok;
}

void Avpm ::setCcPreviewOpacity(tAVPMCcOpacity previewOpacity, tCpeTshOpacity *setOpacity)
{
    if (previewOpacity == eAVPMCcOpacityFlashing)
    {
        *setOpacity = CpeTshOpacity_Flashing;
    }
    else if (previewOpacity == eAVPMCcOpacityTransparent)
    {
        *setOpacity   = CpeTshOpacity_Transparent;
    }
    else if (previewOpacity  == eAVPMCcOpacityTransculent)
    {
        *setOpacity = CpeTshOpacity_Translucent;
    }
    else
    {
        *setOpacity = CpeTshOpacity_Solid;
    }
}

void Avpm ::setCcPreviewColor(tAVPMCcColor  previewColor, DFBColor *setColor)
{
    char *cc_color = NULL;
    if (previewColor == eAVPMCcColorWhite)
    {
        cc_color = "white";
    }
    else if (previewColor == eAVPMCcColorRed)
    {
        cc_color = "red";
    }
    else if (previewColor == eAVPMCcColorGreen)
    {
        cc_color = "green";
    }
    else if (previewColor == eAVPMCcColorBlue)
    {
        cc_color = "blue";
    }
    else if (previewColor == eAVPMCcColorCyan)
    {
        cc_color = "cyan";
    }
    else if (previewColor == eAVPMCcColorMagenta)
    {
        cc_color = "magenta";
    }
    else if (previewColor == eAVPMCcColorYellow)
    {
        cc_color = "yellow";
    }
    else if (previewColor == eAVPMCcColorBlack)
    {
        cc_color = "black";
    }
    else
    {
        LOG(DLOGL_ERROR, "Invalid color attribute");
    }

    //Set CC preview attribute colour
    setccColor(cc_color, setColor);

}

void Avpm ::setCcPreviewEdge(tAVPMCcEdge previewEdge, tCpeTshEdgeEffect *setEdge)
{
    char *cc_char_edge = NULL;
    if (previewEdge == eAVPMCcEdgeRaised)
    {
        cc_char_edge = "raisedEdges";
    }
    else if (previewEdge == eAVPMCcEdgeDepressed)
    {
        cc_char_edge = "depressedEdges";
    }
    else if (previewEdge == eAVPMCcEdgeOutline)
    {
        cc_char_edge = "outline";
    }
    else if (previewEdge == eAVPMCcEdgeDropShadowLeft)
    {
        cc_char_edge = "leftDropShadowedEdges";
    }
    else if (previewEdge == eAVPMCcEdgeDropShadowRight)
    {
        cc_char_edge = "rightDropShadowedEdges";
    }
    else if (previewEdge == eAVPMCcEdgeUniform)
    {
        cc_char_edge = "uniformEdges";
    }
    else if (previewEdge == eAVPMCcEdgeNone)
    {
        cc_char_edge = "noEdges";
    }
    else
    {
        LOG(DLOGL_ERROR, "Invalid edge attribute");
    }
    setCcEdge(cc_char_edge, setEdge);
}

void Avpm ::setCcPreviewFont(tAVPMCcFont previewFont, tCpeTshFontFace *setFontFace)
{
    char* cc_char_font = NULL;
    if (previewFont == eAVPMCcFontMonoSerifs)
    {
        cc_char_font = "serifMonospaced";
    }
    else if (previewFont == eAVPMCcFontPropSerifs)
    {
        cc_char_font = "serifProportional";
    }
    else if (previewFont == eAVPMCcFontMono)
    {
        cc_char_font = "sansSerifMonospaced";
    }
    else if (previewFont == eAVPMCcFontProp)
    {
        cc_char_font = "sansSerifProportional";
    }
    else if (previewFont == eAVPMCcFontCasual)
    {
        cc_char_font = "casual";
    }
    else if (previewFont == eAVPMCcFontCursive)
    {
        cc_char_font = "cursive";
    }
    else if (previewFont == eAVPMCcFontSmallCapitals)
    {
        cc_char_font = "smallCapitals";
    }
    else
    {
        LOG(DLOGL_ERROR, "Invalid Font attribute");
    }
    setccFontFace(cc_char_font, setFontFace);
}

eMspStatus Avpm ::setPreviewStatus(tAVPMCcPreview preview)
{
    char *cc_pen_size = NULL;
    //Preview status one, show preview window
    if (1 == preview.previewStatus)
    {
        ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(mMainScreenPgrHandle);
        if (!pgrHandleSetting)
        {
            LOG(DLOGL_ERROR, "Error null pgrHandleSetting");
            return kMspStatus_AvpmError;
        }
        memset(&mPreviewStreamAttrib, 0, sizeof(mPreviewStreamAttrib));

        mPreviewStreamAttrib = mStreamAttrib;

        mPreviewStreamAttrib.flags = tCpeTshAttribFlags(mPreviewStreamAttrib.flags | eCpeTshAttribFlag_AnalogService |
                                     eCpeTshAttribFlag_DigitalService | eCpeTshAttribFlag_TxtColor |
                                     eCpeTshAttribFlag_PenSize | eCpeTshAttribFlag_WinColor |
                                     eCpeTshAttribFlag_WinOpacity | eCpeTshAttribFlag_EdgeEffect |
                                     eCpeTshAttribFlag_FontFace | eCpeTshAttribFlag_SampleService |
                                     eCpeTshAttribFlag_TextOpacity | eCpeTshAttribFlag_TextBgOpacity |
                                     eCpeTshAttribFlag_TextBgColor);

        //Assign values based on flags set
        if (preview.backgroundFlags)
        {
            if ((preview.backgroundFlags) & (eAVPMCcBackgroundOpacity))
            {
                setCcPreviewOpacity(preview.backgroundSettings.backgroundOpacity, &(mPreviewStreamAttrib.txtBgOpacity));
            }

            if ((preview.backgroundFlags) & (eAVPMCcBackgroundColor))
            {
                setCcPreviewColor(preview.backgroundSettings.backgroundColor, &(mPreviewStreamAttrib.txtBgColor));
            }

            if ((preview.backgroundFlags) & (eAVPMCcWindowColor))
            {
                setCcPreviewColor(preview.backgroundSettings.windowColor, &(mPreviewStreamAttrib.winColor));
            }

            if ((preview.backgroundFlags) & (eAVPMCcWindowOpacity))
            {
                setCcPreviewOpacity(preview.backgroundSettings.windowOpacity, &(mPreviewStreamAttrib.winOpacity));
            }
        }

        if (preview.characterFlags)
        {
            if ((preview.characterFlags) & (eAVPMCcCharacterColor))
            {
                setCcPreviewColor(preview.characterSettings.characterColor, &(mPreviewStreamAttrib.txtColor));
            }

            if ((preview.characterFlags) & (eAVPMCcCharacterOpacity))
            {
                setCcPreviewOpacity(preview.characterSettings.characterOpacity, &(mPreviewStreamAttrib.txtOpacity));
            }

            if ((preview.characterFlags) & (eAVPMCcCharacterSize))
            {
                if (preview.characterSettings.characterSize == eAVPMCcSizeSmall)
                {
                    cc_pen_size = "small";
                }
                else if (preview.characterSettings.characterSize == eAVPMCcSizeLarge)
                {
                    cc_pen_size = "large";
                }
                else
                {
                    cc_pen_size = "standard";
                }
                setccPenSize(cc_pen_size, &(mPreviewStreamAttrib.penSize));
            }

            if ((preview.characterFlags) & (eAVPMCcCharacterEdge))
            {
                setCcPreviewEdge(preview.characterSettings.characterEdge, &(mPreviewStreamAttrib.edgeEffect));
            }

            if ((preview.characterFlags) & (eAVPMCcCharacterFont))
            {
                setCcPreviewFont(preview.characterSettings.characterFont, &(mPreviewStreamAttrib.fontFace));
            }
        }

    }

    //display preview window
    eMspStatus retStatus = showPreviewSample(preview.previewStatus);
    if (kMspStatus_Ok != retStatus)
    {
        LOG(DLOGL_ERROR, "Setting the CC preview error code %d", retStatus)
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "Setting CC preview success %d", retStatus)
    }

    return retStatus;
}

eMspStatus Avpm ::getCc(bool *pIsCaptionEnabled)
{

    if (pIsCaptionEnabled)
    {
        *pIsCaptionEnabled = isClosedCaptionEnabled;

    }
    else
    {
        LOG(DLOGL_ERROR, "Warning null pointer pIsCaptionEnabled")
        return kMspStatus_BadParameters;

    }

    return kMspStatus_Ok;

}

eMspStatus Avpm::applySocSetting(bool  isSoc)
{
    IDirectFBScreen *pHD = NULL;
    IDirectFBScreen *pSD = NULL;

    eMspStatus status = kMspStatus_Ok;
    DFBResult result = DFB_OK;

    // Get HD Screen
    result = pHDLayer->GetScreen(pHDLayer, &pHD);
    if ((result != DFB_OK) || (pHD == NULL))
    {
        LOG(DLOGL_ERROR, "Error in getting DFB HD screen interface. Error code = %d", result);
        status = kMspStatus_Error;
    }

    // Get SD Screen
    result = pSDLayer->GetScreen(pSDLayer, &pSD);
    if ((result != DFB_OK) || (pSD == NULL))
    {
        LOG(DLOGL_ERROR, "Error in getting DFB SD screen interface. Error code = %d", result);
        status = kMspStatus_Error;
    }


    if (pHD || pSD)
    {

        DFBScreenOutputConnectors connector_platform;
        if (isSoc)
        {
            connector_platform  = (DFBScreenOutputConnectors)(DSOC_HDMI | DSOC_YC | DSOC_SCART | DSOC_VGA | DSOC_CVBS | DSOC_SCART2 | DSOC_RF);
        }
        else
        {
            connector_platform  = (DFBScreenOutputConnectors)(DSOC_HDMI | DSOC_COMPONENT | DSOC_YC | DSOC_SCART | DSOC_VGA | DSOC_CVBS | DSOC_SCART2 | DSOC_RF);
        }

        DFBScreenEncoderConfig enc_descriptions;

        if (pHD)
        {
            memset(&enc_descriptions, 0, sizeof(enc_descriptions));
            enc_descriptions.out_connectors = (DFBScreenOutputConnectors)(connector_platform & HD_CONNECTOR_MASK);
            enc_descriptions.flags = DSECONF_CONNECTORS;
            LOG(DLOGL_NOISE, " HD screen Flag: %x", enc_descriptions.out_connectors);
            result = pHD->SetEncoderConfiguration(pHD, 0, &enc_descriptions);
            if (result != DFB_OK)
            {
                LOG(DLOGL_ERROR, "Error in configuring output ports for HD screen Err: %d", result);
                status = kMspStatus_Error;
            }
            pHD->Release(pHD);
        }

        if (pSD)
        {
            DFBScreenMixerConfig     conf;

            memset(&enc_descriptions, 0, sizeof(enc_descriptions));
            enc_descriptions.out_connectors = (DFBScreenOutputConnectors)(connector_platform & SD_CONNECTOR_MASK);
            enc_descriptions.flags = DSECONF_CONNECTORS;
            LOG(DLOGL_NOISE, " SD screen Flag: %x", enc_descriptions.out_connectors);
            result = pSD->SetEncoderConfiguration(pSD, 0, &enc_descriptions);
            if (result != DFB_OK)
            {
                LOG(DLOGL_ERROR, "Error in configuring output ports for SD screen Err: %d", result);
                status = kMspStatus_Error;
            }

            memset(&conf, 0, sizeof(conf));
            result = pSD->GetMixerConfiguration(pSD, 0, &conf);
            if (result != DFB_OK)
            {
                LOG(DLOGL_ERROR, "Error in getting SD GetMixerConfiguration. Err: %d", result);
                status = kMspStatus_Error;
            }
            else
            {
                if (isSoc)
                {
                    LOG(DLOGL_NOISE, "   tAvpmConnector_HDMI ");
                    conf.flags = (DFBScreenMixerConfigFlags)(DSMCONF_BACKGROUND | DSMCONF_LAYERS);
                    conf.background.a = 0xff;
                    conf.background.r = conf.background.g = conf.background.b = 0x00;
                    DFB_DISPLAYLAYER_IDS_EMPTY(conf.layers);
                }
                else
                {
                    LOG(DLOGL_NOISE, "   tAvpmConnector_ALL");
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
                }

                result = pSD->SetMixerConfiguration(pSD, 0, &conf);
                if (result != DFB_OK)
                {
                    LOG(DLOGL_ERROR, "Error in configuring SetMixerConfiguration Err: %d", result);
                    status = kMspStatus_Error;
                }
            }
            pSD->Release(pSD);
        }
    }
    return status;
}

int Avpm::getDigitalLangToServiceMap()
{
    ccPMTLangServNoMap::iterator ccIter;
    ccFallbackLangMap::iterator defLangIter;
    ccFallbackServiceMap::iterator defServiceIter;

    int retVal = DEFAULT_CC_DIGITAL_OPTION;
    std::string stringToSearch = mccUserDigitalSetting;
    //Determine the digital languange to be set
    //Logic: First check if there is exact match of user preference in the caption descriptor
    // If not found check the fallback service number
    LOG(DLOGL_REALLY_NOISY, "user pref language %s", mccUserDigitalSetting);
    if (0 <  mCCLanguageStreamMap.size())
    {
        if (mCCLanguageStreamMap.end() != (ccIter = mCCLanguageStreamMap.find(stringToSearch.c_str())))
        {
            LOG(DLOGL_REALLY_NOISY, "Found matching CC Lang Service No %d n descriptor map for %s", ccIter->second, mccUserDigitalSetting);
            return (ccIter->second);
        }
        //Else check if there is an entry available for fallback language in PMT
        defLangIter = mfallBackCCLangMap.find(stringToSearch.c_str());
        if (mfallBackCCLangMap.end() != defLangIter)
        {
            if (mCCLanguageStreamMap.end() != (ccIter = mCCLanguageStreamMap.find(defLangIter->second.c_str())))
            {
                LOG(DLOGL_REALLY_NOISY, " Fallback CC Lang Service No %d n descriptor map for %s", ccIter->second, mccUserDigitalSetting);
                return (ccIter->second);
            }

        }

    }
    // By setting it to DEFAULT_CC_DIGITAL_OPTION the migration case is also handled. Since in older version of code the
    // values would be "d1-d6" it would fall to the default DEFAULT_CC_DIGITAL_OPTION. The UI code as well would show
    // "ENG" since it wouldn't find a match for "dx" in the new list of "eng/spa/fre/eng_ez/spa_ez/fre_ez"
    retVal = (mfallbackCCServiceMap.end() != (defServiceIter = mfallbackCCServiceMap.find(stringToSearch.c_str()))) ? defServiceIter->second : DEFAULT_CC_DIGITAL_OPTION ;
    LOG(DLOGL_REALLY_NOISY, "No  matching Lang Service Found .Using Default Service No: %d for %s", retVal, mccUserDigitalSetting);
    return (retVal);

}

eMspStatus Avpm::SetDigitalCCLang(tCpePgrmHandle pgrHandle)
{
    FNLOG(DL_MSP_AVPM);

    if (dfb == NULL)
    {
        LOG(DLOGL_ERROR, "Error null dfb");
        return kMspStatus_AvpmError;
    }

    eUseSettingsLevel settingLevel;

    // Store the user preference for digital and analog source
    // so that a call to unified setting is not required on every channel
    // change
    if (0 == strlen(mccUserDigitalSetting))
    {
        Uset_getSettingT(NULL, "ciscoSg/cc/ccSourceDigital", MAX_SETTING_VALUE_SIZE, mccUserDigitalSetting, &settingLevel);
    }

    memset(&mStreamLanguageAttrib, 0, sizeof(mStreamLanguageAttrib));

    mStreamLanguageAttrib.flags = tCpeTshAttribFlags(mStreamLanguageAttrib.flags | eCpeTshAttribFlag_DigitalService);

    mStreamLanguageAttrib.digitalService = tCpeTshDigitalService(getDigitalLangToServiceMap());


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
        result = pgrHandleSetting->tsh->RenderTo(pgrHandleSetting->tsh, pHdClosedCaptionTextSurface, &mStreamLanguageAttrib, &pgrHandleSetting->cchandle);

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

    return kMspStatus_Ok;
}
