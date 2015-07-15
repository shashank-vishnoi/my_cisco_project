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

#include "avpm_ic.h"
#include <time.h>
#include "IMediaController.h"
#include "devicesettings/exception.hpp"
///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

#include "videoOutputPort.hpp"
#include "videoOutputPortType.hpp"
#include "videoResolution.hpp"
#include"pthread_named.h"
#include "languageSelection.h"
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

void* Avpm::eventThreadFunc(void *data)
{
    FNLOG(DL_MSP_AVPM);
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

        }
        break;
        case	kAvpmHDStreamAspectChanged:
        {

        }
        break;
        case	kAvpmSDStreamAspectChanged:
        {

        }
        break;
        case kAvpmUpdateCCI:
        {

            CCIData cciData;
            cciData.cit = (inst->mCCIbyte & 0x10) >> 4;
            cciData.aps = (inst->mCCIbyte & 0x0C) >> 2;
            cciData.emi = (inst->mCCIbyte & 0x03);

            eMspStatus ret_value = kMspStatus_Ok;
            LOG(DLOGL_REALLY_NOISY, "SOC value is %x", inst->mIsSoc);
            LOG(DLOGL_REALLY_NOISY, "CCI value In kAvpmUpdateCCI is %u", inst->mCCIbyte);

            inst->SetAPS(cciData.aps);
            inst->SetEMI((uint8_t)cciData.emi);

            //if SOC is enabled call the
            ret_value = inst->applySocSetting(inst->mIsSoc);

            if (ret_value != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "Error in applying SOC setting error:%d", ret_value);
            }
            if (cciData.cit == CIT_ENABLED)
            {
                ret_value = inst->SetCITState(CIT_ENABLED);
            }
            else if (cciData.cit == CIT_DISABLED)
            {
                ret_value = inst->SetCITState(CIT_DISABLED);
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
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "In Case kAvpmRegCiscoSettings");
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
        }
        break;


        case kAvpmApplySDMainPresentationParams:
        {
            LOG(DLOGL_REALLY_NOISY, "Apply SD MAIN Presentation Params");
        }
        break;


        case kAvpmApplyHDPipPresentationParams:
        case kAvpmApplySDPipPresentationParams:
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
    FNLOG(DL_MSP_AVPM);

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
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "In EventUSE_DISCONNECTED\n");
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
    FNLOG(DL_MSP_AVPM);

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
    return event;
}

eMspStatus Avpm::applyAVSetting(eAvpmEvent aEvent, char *pValue)
{
    FNLOG(DL_MSP_AVPM);
    eMspStatus status;

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
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "SAP changed");
            mSapChangeCb(mpSapChangedCbData);
            return kMspStatus_Ok;
        }
        break;
    case kAvpmAudioOutputChanged:
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "in %s , kAvpmAudioOutputChanged\n", __FUNCTION__);

        dsAudioEncoding_t dsAudioEncodingType = dsAUDIO_ENC_PCM;

        if (strncmp((const char *)pValue, "ac3", strlen("ac3")) == 0)
        {
            dsAudioEncodingType = dsAUDIO_ENC_AC3;
        }
        else if (strncmp((const char *)pValue, "uncompressed", strlen("uncompressed")) == 0)
        {
            dsAudioEncodingType = dsAUDIO_ENC_PCM;
        }

        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "in %s , calling setAudioOutputMode with dsAudioEncodingType = %d \n", __FUNCTION__, dsAudioEncodingType);

        setAudioOutputMode(dsAudioEncodingType);
    }
    break;

    case kAvpmAC3AudioRangeChanged:
    {
        //coverity id - 10658
        //Adding a default audio range setting as NORMAL
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Inside Compress Level\n");
        tAvpmAudioRange audio_range;
        if (strncmp((const char *)pValue, "narrow", strlen("narrow")) == 0)
        {
            audio_range = tAvpmAudioRangeNarrow;
        }
        else if (strncmp((const char *)pValue, "normal", strlen("normal")) == 0)
        {
            audio_range = tAvpmAudioRangeNormal;
        }
        else if (strncmp((const char *)pValue, "wide", strlen("wide")) == 0)
        {
            audio_range = tAvpmAudioRangeWide;
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "Not a valid option.Setting default audio range--Normal");
            audio_range = tAvpmAudioRangeNormal;
        }

        setAC3AudioRange((tAvpmAudioRange) audio_range);
    }
    break;
    case kAvpmTVAspectratioChanged:
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "kAvpmTVAspectratioChanged  pValue=%s\n", pValue);

        if (strcmp((const char *)pValue, "4:3") == 0)
        {
            user_aspect_ratio = tAvpmTVAspectRatio4x3;
        }
        else if (strcmp((const char *)pValue, "16:9") == 0)
        {
            user_aspect_ratio = tAvpmTVAspectRatio16x9;
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "In applyAVSettings, case kAvpmVideoOutputChanged, invalid value passed in kAvpmTVAspectratioChanged\n");
        }
    }
    break;
    case kAvpmVideoOutputChanged:
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "case kAvpmVideoOutputChanged, pValue=%s\n", (const char *)pValue);
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
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "case kAvpmVideoOutputChanged, invalid value passed in kAvpmVideoOutputChanged\n");
        }

        eAvpmVideoRects videoRectsType = kAvpmBothHDSD;
        setPictureMode(videoRectsType);
    }
    break;
    case kAvpmrfOutputChannel:
        setRFmodeSetOutputChan();
        break;

    case kAvpmDisplayResolnChanged:
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "kAvpmDisplayResolnChanged, pValue=%s\n", pValue);

        if (strcmp((const char *)pValue, "480i") == 0)
        {
            if (user_aspect_ratio == tAvpmTVAspectRatio16x9)
            {
                dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "tAvpmTVAspectRatio16x9, hence setting 480p resolution");
                dispResolution = tAvpmResolution_480p;
                strcpy(pValue, "480p");
            }
            else
            {
                dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Setting 480i resolution");
                dispResolution = tAvpmResolution_480i;
            }
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
            dispResolution = tAvpmResolution_720p;//Default preferred Mode
        }
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "kAvpmDisplayResolnChanged, calling setDisplayResolutionpValue=%d\n", dispResolution);
        status = setDisplayResolution(pValue);
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
        closedCaption(aEvent, pValue);
    }
    break;

    case kAvpmMasterMute:
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Mute the volume\n");
        if (strcmp((const char *)pValue, "0") == 0)
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "UnMute prev_vol %d\n", prev_vol);
            MasterMute(true);
            setMasterVolume(prev_vol);
        }
        else if (strcmp((const char *)pValue, "1") == 0)
        {
            dlog(DL_MSP_AVPM, DLOGL_NOISE, "Mute the volume\n");
            MasterMute(false);
        }
        return kMspStatus_Ok;
    }
    break;
    case kAvpmMasterVol:
    {
        int vol = atoi((const char *)pValue);
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Volume up/down is %d\n", vol);
        prev_vol = vol;
        MasterMute(true);
        eMspStatus status = setMasterVolume(vol);
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


void Avpm::CalculateScalingRectangle(int maxWidth, int maxHeight)
{
    FNLOG(DL_MSP_AVPM);

    (void) maxWidth;
    (void) maxHeight;
}

void Avpm::setWindow()
{
    FNLOG(DL_MSP_AVPM);

}


void Avpm::SetVideoPresent(bool isPresent)
{
    FNLOG(DL_MSP_AVPM);

    mHaveVideo = isPresent;
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) video present: %d", __FUNCTION__, __LINE__, mHaveVideo);
}

bool Avpm::isSetPresentationParamsChanged()
{
    FNLOG(DL_MSP_AVPM);

    bool status = false;

    return status;
}

eMspStatus Avpm::setPresentationParams(bool audiofocus)
{
    FNLOG(DL_MSP_AVPM);

    (void) audiofocus;

    lockMutex();

    unLockMutex();
    return kMspStatus_Ok;
}

/** *********************************************************
 */
eMspStatus Avpm::setAudioParams()
{
    FNLOG(DL_MSP_AVPM);

    eMspStatus status = kMspStatus_Ok;
    eUseSettingsLevel settingLevel;
    char muteSetting[MAX_AVPM_SETTING_VALUE_SIZE];
    char volumeSetting[MAX_AVPM_SETTING_VALUE_SIZE];
    int vol_level = 50; //Default Volume value
    bool isMute = false; //Default unmute

    lockMutex();
    if (USE_RESULT_OK == Uset_getSettingT(NULL, "mom.system.mute", MAX_AVPM_SETTING_VALUE_SIZE,  muteSetting, &settingLevel))
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Received the mute setting, value= %s  \n", muteSetting);
        isMute = atoi(muteSetting);
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Failed to get the mute setting. Setting to default %d\n", isMute);
    }

    if (USE_RESULT_OK == Uset_getSettingT(NULL, "mom.system.volume", MAX_AVPM_SETTING_VALUE_SIZE,  volumeSetting, &settingLevel))
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Received the volume setting, value= %s  \n", volumeSetting);
        vol_level = atoi(volumeSetting);
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Failed to get the volume level. Setting to default %d\n", prev_vol);
    }

    prev_vol = vol_level;
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "setAudioParams prev_vol %d\n", prev_vol);

    if (isMute)
    {
        MasterMute(false);
    }
    else
    {
        MasterMute(true);
        status = setMasterVolume(prev_vol);
    }

    unLockMutex();
    return status;
}


/** *********************************************************
 */
eMspStatus Avpm::connectOutput()
{
    FNLOG(DL_MSP_AVPM);

    eMspStatus status = kMspStatus_Ok;
    eUseSettingsLevel settingLevel;
    char aspectRatioStr[MAX_AVPM_SETTING_VALUE_SIZE];
    char audioOutputStr[MAX_AVPM_SETTING_VALUE_SIZE];
    char audioRangeStr[MAX_AVPM_SETTING_VALUE_SIZE];
    tAvpmAudioRange audio_range = tAvpmAudioRangeNormal;

    lockMutex();

    Uset_getSettingT(NULL, "ciscoSg/look/aspect", MAX_AVPM_SETTING_VALUE_SIZE, aspectRatioStr, &settingLevel);

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

    dsAudioEncoding_t dsAudioEncodingType = dsAUDIO_ENC_PCM;

    Uset_getSettingT(NULL, "ciscoSg/audio/audioOutput", MAX_AVPM_SETTING_VALUE_SIZE, audioOutputStr, &settingLevel);

    if (strcmp((const char *)audioOutputStr, "ac3") == 0)
    {
        dsAudioEncodingType = dsAUDIO_ENC_AC3;
    }
    else if (strcmp((const char *)audioOutputStr, "uncompressed") == 0)
    {
        dsAudioEncodingType = dsAUDIO_ENC_PCM;
    }

    setAudioOutputMode(dsAudioEncodingType);

    Uset_getSettingT(NULL, "ciscoSg/audio/audioRange", MAX_AVPM_SETTING_VALUE_SIZE, audioRangeStr, &settingLevel);

    if (strcmp((const char *)audioRangeStr, "narrow") == 0)
    {
        audio_range = tAvpmAudioRangeNarrow;
    }
    else if (strcmp((const char *)audioRangeStr, "normal") == 0)
    {
        audio_range = tAvpmAudioRangeNormal;
    }
    else if (strcmp((const char *)audioRangeStr, "wide") == 0)
    {
        audio_range = tAvpmAudioRangeWide;
    }

    setAC3AudioRange((tAvpmAudioRange) audio_range);

    if (isClosedCaptionEnabled)
    {
        eMspStatus retVal = setCc(isClosedCaptionEnabled);
        if (kMspStatus_Ok != retVal)
        {
            LOG(DLOGL_ERROR, "setCC returned Error - %d", retVal);
        }
    }
    unLockMutex();
    return status;
}

eMspStatus Avpm::stopOutput()
{
    FNLOG(DL_MSP_AVPM);
    eMspStatus status;

    lockMutex();
    status = freezeVideo();
    unLockMutex();

    if (status != kMspStatus_Ok)
    {
        return kMspStatus_AvpmError;
    }

    return kMspStatus_Ok;
}

eMspStatus Avpm::disconnectOutput()
{
    FNLOG(DL_MSP_AVPM);
    eMspStatus status = kMspStatus_Ok;
    lockMutex();
    unLockMutex();
    return status;
}



Avpm * Avpm::getAvpmInstance()
{
    FNLOG(DL_MSP_AVPM);

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
    FNLOG(DL_MSP_AVPM);
    pthread_attr_t attr;

    eMspStatus avpm1394PortStatus;
    // create event queue for scan thread

    mAudioLangChangedCb = NULL;
    mAudioLangCbData = NULL;
    mCcChangedCb = NULL;
    mCcCbData = NULL;
    mDefaultResolution = -1;
    user_aspect_ratio = tAvpmTVAspectRatio16x9;
    renderToSurface = false;
    isPreviewEnabled = false;

    isClosedCaptionEnabled = false;
    prev_vol = 0;

    // create event queue for scan thread
    threadEventQueue = new MSPEventQueue();

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
    avpm1394PortStatus = kMspStatus_NotSupported; //avpm1394PortInit();
    LOG(DLOGL_REALLY_NOISY, " 1394 port initialization is not supported");

    pthread_mutex_init(&mMutex, NULL);

    setRFmodeSetOutputChan();
    initPicMode();
    setAudioParams();
    mCCAtribIsSet = false;
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


    //initialize the SDK...
    //vlGfxInit(0);// Init the DirectFB surface for closed caption on 0 screen layer which we are sharing with ADMINISTRATIVE preference with SDK.
    closedCaptionInit();
    SAILToSDKCCMap();
}

Avpm::~Avpm()
{
    FNLOG(DL_MSP_AVPM);
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

/**
 *   @brief Function to populate the mapping of CC service channels from SAIL to SDK.
 *   		And to populate SAIL colors to appropriate HEX values for SDK.
 */
void Avpm::SAILToSDKCCMap(void)
{
    mSAIL_To_SDK_Map["cc1"] = CCChannel_CC1;
    mSAIL_To_SDK_Map["cc2"] = CCChannel_CC2;
    mSAIL_To_SDK_Map["cc3"] = CCChannel_CC3;
    mSAIL_To_SDK_Map["cc4"] = CCChannel_CC4;
    mSAIL_To_SDK_Map["t1"] = CCChannel_TEXT1;
    mSAIL_To_SDK_Map["t2"] = CCChannel_TEXT2;
    mSAIL_To_SDK_Map["t3"] = CCChannel_TEXT3;
    mSAIL_To_SDK_Map["t4"] = CCChannel_TEXT4;
    mSAIL_To_SDK_Map["d1"] = CCDigitalChannel_DS1;
    mSAIL_To_SDK_Map["d2"] = CCDigitalChannel_DS2;
    mSAIL_To_SDK_Map["d3"] = CCDigitalChannel_DS3;
    mSAIL_To_SDK_Map["d4"] = CCDigitalChannel_DS4;
    mSAIL_To_SDK_Map["d5"] = CCDigitalChannel_DS5;
    mSAIL_To_SDK_Map["d6"] = CCDigitalChannel_DS6;

    mColorCharToInt["red"] = 0xff0000;
    mColorCharToInt["black"] = 0x000000;
    mColorCharToInt["white"] = 0xffffff;
    mColorCharToInt["green"] = 0x00ff00;
    mColorCharToInt["blue"] = 0x0000ff;
    mColorCharToInt["cyan"] = 0x00ffff;
    mColorCharToInt["magenta"] = 0xff00ff;
    mColorCharToInt["yellow"] = 0xffff00;
}

eMspStatus Avpm::initPicMode(void)
{
    FNLOG(DL_MSP_AVPM);

    eUseSettingsLevel settingLevel;
    char picMode[MAX_AVPM_SETTING_VALUE_SIZE] = {0};
    eMspStatus status = kMspStatus_Ok;

    if (USE_RESULT_OK == Uset_getSettingT(NULL, "ciscoSg/look/picSize", MAX_AVPM_SETTING_VALUE_SIZE, picMode, &settingLevel))
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
    char volumeStr[MAX_AVPM_SETTING_VALUE_SIZE];
    char muteSetting[MAX_AVPM_SETTING_VALUE_SIZE];
    bool isMute = false; //Default unmute
    eMspStatus status = kMspStatus_Error;

    if (USE_RESULT_OK == Uset_getSettingT(NULL, "mom.system.mute", MAX_AVPM_SETTING_VALUE_SIZE,  muteSetting, &settingLevel))
    {
        isMute = atoi(muteSetting);
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "Failed to get the mute setting. Setting to default %d\n", isMute);
    }

    if (USE_RESULT_OK == Uset_getSettingT(NULL, "mom.system.volume", MAX_AVPM_SETTING_VALUE_SIZE,  volumeStr, &settingLevel))
    {
        volume = atoi(volumeStr);
    }

    if (isMute)
    {
        status = MasterMute(false);
    }
    else
    {
        MasterMute(true);
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "Setting volume= %d \n", volume);
        status = setMasterVolume(prev_vol);
    }

    return status;
}

eMspStatus Avpm::GetScreenSize(int *width, int *height)
{
    FNLOG(DL_MSP_AVPM);

    if ((width == NULL) || (height == NULL))
    {
        return kMspStatus_Error;
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
        //clearSurfaceFromLayer(VANTAGE_HD_PIP);
        //clearSurfaceFromLayer(VANTAGE_SD_PIP);
    }

}

eMspStatus Avpm::setAudioOutputMode(dsAudioEncoding_t dsAudioEncodingType)
{
    FNLOG(DL_MSP_AVPM);
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d dsAudioEncodingType=%d", __FUNCTION__, __LINE__, dsAudioEncodingType);

    device::List<device::AudioOutputPort> aPorts = device::Host::getInstance().getAudioOutputPorts();
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "In %s, Audio Ports Size=%d ", __FUNCTION__, aPorts.size());
    for (size_t i = 0; i < aPorts.size(); i++)
    {
        try
        {
            device::AudioOutputPort &aPort = aPorts.at(i);
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "In %s, AUDIO Port Name=%s ", __FUNCTION__, aPort.getName().c_str());
            aPort.setEncoding(dsAudioEncodingType);
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d dsAudioEncodingType APPLIED =%d", __FUNCTION__, __LINE__, dsAudioEncodingType);
        }

        catch (const device::Exception e)
        {
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s Exception during setting Audio Output: Message: %s, Code: %d", __FUNCTION__, e.getMessage().c_str() , e.getCode());
        }
    }

    return kMspStatus_Ok;
}


/**
 *   @param cc_event 	- notifies type of the CC attribute user changed
 *   @param pValue      - new value of the CC attribute user selected
 *
 *   @return kMspStatus_Ok
 *   @brief Function to change the CC attributes requested by user.
 *          This function sets the CC attributes like font size, CC colour,
 *          CC background colour and CC background style.
 *          Also this function allows the user to select a particular analog
 *          and digital channel among the available CC services.
 */
eMspStatus Avpm::closedCaption(eAvpmEvent cc_event, char *pValue)
{
    FNLOG(DL_MSP_AVPM);

    int retValSDK = 0;
    short type = 0;
    mrcc_Error ccError = CC_EINVAL;
    eUseSettingsLevel settingLevel;
    char ccDigitalSetting[MAX_AVPM_SETTING_VALUE_SIZE] = {0};
    char ccAnalogSetting[MAX_AVPM_SETTING_VALUE_SIZE] = {0};
    char ccSetByProgram[MAX_AVPM_SETTING_VALUE_SIZE] = {0};
    memset(&mCcAttribute, 0, sizeof(gsw_CcAttributes));
    LOG(DLOGL_REALLY_NOISY, "%s ::: cc_event = %d  ccSetDigitalChannel - %s", __func__, cc_event, ccDigitalSetting);
    if (cc_event == kAvpmDigitalCCEnable)
    {
        strncpy(ccDigitalSetting, pValue, MAX_AVPM_SETTING_VALUE_SIZE);
        ccDigitalSetting[MAX_AVPM_SETTING_VALUE_SIZE - 1] = '\0';
        LOG(DLOGL_REALLY_NOISY, "%s :::: updated frm pValue ccSourceDigital - %s", __func__, ccDigitalSetting);
    }
    else
    {
        Uset_getSettingT(NULL, "ciscoSg/cc/ccSourceDigital", MAX_AVPM_SETTING_VALUE_SIZE, ccDigitalSetting, &settingLevel);
        LOG(DLOGL_REALLY_NOISY, "%s :::: Usetting ccSourceDigital - %s", __func__, ccDigitalSetting);
    }

    CCDigitalChannel_t userSelectionDigital = (CCDigitalChannel_t)getDigitalLangToServiceMap(ccDigitalSetting);
    retValSDK = ccSetDigitalChannel(userSelectionDigital);
    if (CC_SUCCESS != retValSDK)
    {
        LOG(DLOGL_ERROR, " %s ::: ERROR setting ccSetDigitalChannel - %d", __func__, retValSDK);
    }
    else
        LOG(DLOGL_REALLY_NOISY, " %s ::: Success setting ccSetDigitalChannel - %d", __func__, retValSDK);

    if (cc_event == kAvpmAnalogCCEnable)
    {
        strncpy(ccAnalogSetting, pValue, MAX_AVPM_SETTING_VALUE_SIZE);
        ccAnalogSetting[MAX_AVPM_SETTING_VALUE_SIZE - 1] = '\0';
    }
    else
    {
        Uset_getSettingT(NULL, "ciscoSg/cc/ccSourceAnalog", MAX_AVPM_SETTING_VALUE_SIZE, ccAnalogSetting, &settingLevel);
    }
    CCAnalogChannel_t userSelectionAnalog = (CCAnalogChannel_t) mSAIL_To_SDK_Map[(string) ccAnalogSetting];
    retValSDK = ccSetAnalogChannel(userSelectionAnalog);
    if (CC_SUCCESS != retValSDK)
    {
        LOG(DLOGL_ERROR, "ERROR setting ccSetAnalogChannel - %d", retValSDK);
    }


    if (cc_event == kAvpmCCSetByProgram)
    {
        LOG(DLOGL_REALLY_NOISY, "%s: CC Setting changed is set by program value %s", __FUNCTION__, pValue);
        strncpy(ccSetByProgram, pValue, MAX_AVPM_SETTING_VALUE_SIZE);
        ccSetByProgram[MAX_AVPM_SETTING_VALUE_SIZE - 1] = '\0';
    }
    else
    {
        Uset_getSettingT(NULL, "ciscoSg/cc/ccSetByProgram", MAX_AVPM_SETTING_VALUE_SIZE, ccSetByProgram, &settingLevel);
        LOG(DLOGL_NOISE, "CC Read from unified setting %s", ccSetByProgram);
    }

    if (strcmp(ccSetByProgram, "2"))
    {
        LOG(DLOGL_REALLY_NOISY, "CC set by viewer");

        char fontSize[MAX_AVPM_SETTING_VALUE_SIZE] = {0};
        char charColor[MAX_AVPM_SETTING_VALUE_SIZE] = {0};
        char backgroundColor[MAX_AVPM_SETTING_VALUE_SIZE] = {0};
        char windowColor[MAX_AVPM_SETTING_VALUE_SIZE] = {0};
        char charFgStyle[MAX_AVPM_SETTING_VALUE_SIZE] = {0};
        char charBgStyle[MAX_AVPM_SETTING_VALUE_SIZE] = {0};
        char windowStyle[MAX_AVPM_SETTING_VALUE_SIZE] = {0};
        char charFontStyle[MAX_AVPM_SETTING_VALUE_SIZE] = {0};
        char charEdgeType[MAX_AVPM_SETTING_VALUE_SIZE] = {0};

        if (cc_event == kAvpmCCPenSize)
        {
            strncpy(fontSize, pValue, MAX_AVPM_SETTING_VALUE_SIZE);
            fontSize[MAX_AVPM_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccCharacterSize", MAX_AVPM_SETTING_VALUE_SIZE, fontSize, &settingLevel);
        }

        if (cc_event == kAvpmCCCharColor)
        {
            strncpy(charColor, pValue, MAX_AVPM_SETTING_VALUE_SIZE);
            charColor[MAX_AVPM_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccCharacterColor", MAX_AVPM_SETTING_VALUE_SIZE, charColor, &settingLevel);
        }

        if (cc_event == kAvpmBackgroundColor)
        {
            strncpy(backgroundColor, pValue, MAX_AVPM_SETTING_VALUE_SIZE);
            backgroundColor[MAX_AVPM_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccBgColor", MAX_AVPM_SETTING_VALUE_SIZE, backgroundColor, &settingLevel);
        }

        if (cc_event == kAvpmCCWindowColor)
        {
            strncpy(windowColor, pValue, MAX_AVPM_SETTING_VALUE_SIZE);
            windowColor[MAX_AVPM_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccWindowColor", MAX_AVPM_SETTING_VALUE_SIZE, windowColor, &settingLevel);
        }

        if (cc_event == kAvpmBackgroundStyle)
        {
            strncpy(charBgStyle, pValue, MAX_AVPM_SETTING_VALUE_SIZE);
            charBgStyle[MAX_AVPM_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccBgStyle", MAX_AVPM_SETTING_VALUE_SIZE, charBgStyle, &settingLevel);
        }

        if (cc_event == kAvpmCCCharStyle)
        {
            strncpy(charFgStyle, pValue, MAX_AVPM_SETTING_VALUE_SIZE);
            charFgStyle[MAX_AVPM_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccCharacterStyle", MAX_AVPM_SETTING_VALUE_SIZE, charFgStyle, &settingLevel);
        }

        if (cc_event == kAvpmCCWindowStyle)
        {
            strncpy(windowStyle, pValue, MAX_AVPM_SETTING_VALUE_SIZE);
            windowStyle[MAX_AVPM_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccWindowStyle", MAX_AVPM_SETTING_VALUE_SIZE, windowStyle, &settingLevel);
        }

        if (cc_event == kAvpmCCCharFont)
        {
            strncpy(charFontStyle, pValue, MAX_AVPM_SETTING_VALUE_SIZE);
            charFontStyle[MAX_AVPM_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccCharacterFont", MAX_AVPM_SETTING_VALUE_SIZE, charFontStyle, &settingLevel);
        }

        if (cc_event == kAvpmCCCharEdge)
        {
            strncpy(charEdgeType, pValue, MAX_AVPM_SETTING_VALUE_SIZE);
            charEdgeType[MAX_AVPM_SETTING_VALUE_SIZE - 1] = '\0';
        }
        else
        {
            Uset_getSettingT(NULL, "ciscoSg/cc/ccCharacterEdge", MAX_AVPM_SETTING_VALUE_SIZE, charEdgeType, &settingLevel);
        }


        //Use the stream properties.

        setccFontSize(&mCcAttribute, fontSize);
        setccStyle(&(mCcAttribute.charFgOpacity), charFgStyle);
        setccStyle(&(mCcAttribute.charBgOpacity), charBgStyle);
        setccStyle(&(mCcAttribute.winOpacity), windowStyle);
        mCcAttribute.charFgColor.rgb = mColorCharToInt[(string)charColor];
        mCcAttribute.charBgColor.rgb = mColorCharToInt[(string)backgroundColor];
        mCcAttribute.winColor.rgb = mColorCharToInt[(string)windowColor];
        setccFontStyle(&(mCcAttribute.fontStyle), charFontStyle);
        setCcEdgeType(&(mCcAttribute.edgeType), charEdgeType);

        type = gsw_CcAttribType(GSW_CC_ATTRIB_FONT_SIZE | GSW_CC_ATTRIB_FONT_COLOR |  GSW_CC_ATTRIB_FONT_OPACITY | GSW_CC_ATTRIB_BACKGROUND_COLOR |  GSW_CC_ATTRIB_WIN_COLOR | GSW_CC_ATTRIB_BACKGROUND_OPACITY | GSW_CC_ATTRIB_WIN_OPACITY | GSW_CC_ATTRIB_FONT_STYLE | GSW_CC_ATTRIB_EDGE_TYPE);
    }
    else
    {
        LOG(DLOGL_ERROR, "CC is set by program, Skip all settings");
    }

    LOG(DLOGL_REALLY_NOISY, "setting the attributes - Font size - %d, Char Opacity - %d, BG opacity - %d, char color - %d, BG Color - %d, Window color - %d, Window Opacity - %d, Font Style - %s Font Edge - %d and type - %d",
        mCcAttribute.fontSize, mCcAttribute.charFgOpacity, mCcAttribute.charBgOpacity, mCcAttribute.charFgColor.rgb, mCcAttribute.charBgColor.rgb, mCcAttribute.winColor.rgb, mCcAttribute.winOpacity, mCcAttribute.fontStyle, mCcAttribute.edgeType, type);

    /*1.No need of calling ccSetAttributes for attribute changes.Preview will take care of setting the attributes
      2.ccSetAttributes needs to be called incase of user changing the rendering style(kAvpmCCSetByProgram) and presses exit folowed by save when the window prompts.
        In this case preview will not be invoked to reflect the changes.
    */
    if ((pValue == NULL) || (cc_event == kAvpmCCSetByProgram))
    {
        if (!isPreviewEnabled)
        {
            ccError = ccSetAttributes(&mCcAttribute, type, GSW_CC_TYPE_DIGITAL);
            if (ccError != CC_SUCCESS)
            {
                LOG(DLOGL_ERROR, "ccSetAttributes DIGITAL Failed with %d", ccError);
            }

            ccError = ccSetAttributes(&mCcAttribute, type, GSW_CC_TYPE_ANALOG);
            if (ccError != CC_SUCCESS)
            {
                LOG(DLOGL_ERROR, "ccSetAttributes ANALOG Failed with %d", ccError);
            }
        }
    }
    return kMspStatus_Ok;
}

/**
 *   @param aOpacity 	- pointer to CC style attribute
 *   @param cc_color_in     - CC font/character background style
 *
 *   @return kMspStatus_Ok
 *   @brief Function to map the SAIL CC font background style to RDK font
 *          background style, and then to fill the font background style
 *          attribute of the RDK CC attributes structure gsw_CcAttributes.
 */
eMspStatus Avpm::setccStyle(gsw_CcOpacity *aOpacity, char* cc_opacity_in)
{
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) cc_opacity_in %s", __FUNCTION__, __LINE__, cc_opacity_in);

    if (strcmp(cc_opacity_in, "clear") == 0)
    {
        *aOpacity = GSW_CC_OPACITY_TRANSPARENT;
    }
    else if (strcmp(cc_opacity_in, "slight") == 0)
    {
        *aOpacity = GSW_CC_OPACITY_TRANSLUCENT;
    }
    else
    {
        *aOpacity = GSW_CC_OPACITY_SOLID;
    }

    return kMspStatus_Ok;
}

/**
 *   @param setEdgeType 	- pointer to CC Font Edge type attribute
 *   @param char_edge_type  - CC font edge type
 *
 *   @return kMspStatus_Ok
 *   @brief Function to map the SAIL CC font edge type to RDK font
 *          edge type, and then to fill the font edge type
 *          attribute of the RDK CC attributes structure gsw_CcAttributes.
 */
eMspStatus Avpm::setCcEdgeType(gsw_CcEdgeType *setEdgeType, char* char_edge_type)
{
    LOG(DLOGL_NOISE, "ccCharacterEdge: %s", char_edge_type);
    if (strcmp(char_edge_type, "raisedEdges") == 0)
    {
        *setEdgeType = GSW_CC_EDGE_TYPE_RAISED;
    }
    else if (strcmp(char_edge_type, "depressedEdges") == 0)
    {
        *setEdgeType = GSW_CC_EDGE_TYPE_DEPRESSED;
    }
    else if (strcmp(char_edge_type, "leftDropShadowedEdges") == 0)
    {
        *setEdgeType = GSW_CC_EDGE_TYPE_SHADOW_LEFT;
    }
    else if (strcmp(char_edge_type, "rightDropShadowedEdges") == 0)
    {
        *setEdgeType = GSW_CC_EDGE_TYPE_SHADOW_RIGHT;
    }
    else if (strcmp(char_edge_type, "uniformEdges") == 0)
    {
        *setEdgeType = GSW_CC_EDGE_TYPE_UNIFORM;
    }
    else
    {
        *setEdgeType = GSW_CC_EDGE_TYPE_NONE;
    }
    return kMspStatus_Ok;
}

/**
 *   @param setFontStyle 	- pointer to CC Font style attribute
 *   @param cc_font_style  - CC Font style
 *
 *   @return kMspStatus_Ok
 *   @brief Function to map the SAIL CC font style to RDK font
 *          style, and then to fill the font style
 *          attribute of the RDK CC attributes structure gsw_CcAttributes.
 */
eMspStatus Avpm::setccFontStyle(gsw_CcFontStyle *setFontStyle, char* cc_font_style)
{
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) cc_font_style %s", __FUNCTION__, __LINE__, cc_font_style);
    if (strcmp(cc_font_style, "serifMonospaced") == 0)
    {
        strcpy(*setFontStyle, GSW_CC_FONT_STYLE_MONOSPACED_SERIF);
    }
    else if (strcmp(cc_font_style, "sansSerifMonospaced") == 0)
    {
        strcpy(*setFontStyle, GSW_CC_FONT_STYLE_MONOSPACED_SANSSERIF);
    }
    else if (strcmp(cc_font_style, "sansSerifProportional") == 0)
    {
        strcpy(*setFontStyle, GSW_CC_FONT_STYLE_PROPORTIONAL_SANSSERIF);
    }
    else if (strcmp(cc_font_style, "casual") == 0)
    {
        strcpy(*setFontStyle, GSW_CC_FONT_STYLE_CASUAL);
    }
    else if (strcmp(cc_font_style, "cursive") == 0)
    {
        strcpy(*setFontStyle, GSW_CC_FONT_STYLE_CURSIVE);
    }
    else if (strcmp(cc_font_style, "smallCapitals") == 0)
    {
        strcpy(*setFontStyle, GSW_CC_FONT_STYLE_SMALL_CAPITALS);
    }
    else
    {
        strcpy(*setFontStyle, GSW_CC_FONT_STYLE_PROPORTIONAL_SERIF);
    }
    return kMspStatus_Ok;
}

/**
 *   @param pCcAttribute 	- pointer to CC attributes structure
 *   @param cc_fontsize_in   - font size
 *
 *   @return kMspStatus_Ok
 *   @brief Function to map the SAIL CC font size to RDK font size, and
 *          then to fill the font size attribute of the RDK CC attributes
 *          structure gsw_CcAttributes.
 */
eMspStatus Avpm::setccFontSize(gsw_CcAttributes *pCcAttribute, char* cc_fontsize_in)
{
    FNLOG(DL_MSP_AVPM);

    LOG(DLOGL_NOISE, "cc_fontsize_in %s", cc_fontsize_in);

    if (pCcAttribute)
    {
        if (strcmp(cc_fontsize_in, "small") == 0)
        {
            pCcAttribute->fontSize = GSW_CC_FONT_SIZE_SMALL;
        }
        else if (strcmp(cc_fontsize_in, "large") == 0)
        {
            pCcAttribute->fontSize = GSW_CC_FONT_SIZE_LARGE;
        }
        else
        {
            pCcAttribute->fontSize = GSW_CC_FONT_SIZE_STANDARD;		// If the cc_fontsize_in matches standard or unknown elements default to STANDARD size
        }
    }

    return kMspStatus_Ok;
}

void Avpm::logScalingRects(char *msg)
{
    FNLOG(DL_MSP_AVPM);

    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s(%d) %s", __FUNCTION__, __LINE__, msg);
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
eMspStatus Avpm::MasterMute(bool mute)
{
    FNLOG(DL_MSP_AVPM);
    mute = !mute;
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "In %s, Inverting the value of mute because erdk uses it the other way: mute=true means set to mute. value of mute post inversion = %d", __PRETTY_FUNCTION__ , mute);

    try
    {
        device::List<device::VideoOutputPort> vPorts = device::Host::getInstance().getVideoOutputPorts();
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "In %s, vPorts.size() = %d ", __FUNCTION__, vPorts.size());

        for (size_t i = 0; i < vPorts.size(); i++)
        {
            device::AudioOutputPort &aPort = vPorts.at(i).getAudioOutputPort();
            aPort.setMuted(mute);
        }
    }
    catch (const device::Exception e)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s Exception in MasterMute: Message: %s, Code: %d", __FUNCTION__, e.getMessage().c_str() , e.getCode());
        return kMspStatus_AvpmError;
    }
    return kMspStatus_Ok;
}

eMspStatus Avpm::setAnalogOutputMode()
{
    FNLOG(DL_MSP_AVPM);

    eMspStatus error = kMspStatus_Ok;

    return error;
}

eMspStatus Avpm::mute(bool mute)
{
    FNLOG(DL_MSP_AVPM);

    UNUSED_PARAM(mute);

    return kMspStatus_Ok;
}

eMspStatus Avpm::pauseVideo()
{
    FNLOG(DL_MSP_AVPM);

    eMspStatus status = kMspStatus_Ok;
    return status;
}




eMspStatus Avpm::freezeVideo()
{
    FNLOG(DL_MSP_AVPM);

    eMspStatus status = kMspStatus_Ok;

    return status;
}

eMspStatus Avpm::release()
{
    FNLOG(DL_MSP_AVPM);

    return kMspStatus_Ok;
}
eMspStatus Avpm::setMasterVolume(int vol)
{
    FNLOG(DL_MSP_AVPM);

    if (vol > MAX_VOL)
    {
        vol = MAX_VOL;
    }

    try
    {
        device::List<device::VideoOutputPort> vPorts = device::Host::getInstance().getVideoOutputPorts();
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "In %s, vPorts.size() = %d ", __FUNCTION__, vPorts.size());
        for (size_t i = 0; i < vPorts.size(); i++)
        {
            device::VideoOutputPort &vPort = vPorts.at(i);

            /* If port type is not HDMI,then only we should control the volume level */
            if ((strstr(vPort.getType().getName().c_str(), "HDMI") == NULL))
            {
                device::AudioOutputPort &aPort = vPort.getAudioOutputPort();
                aPort.setLevel((float)vol);
            }
            else
            {
                dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s: Not setting volume level to HDMI port", __FUNCTION__);
            }
        }
    }
    catch (const device::Exception e)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s Exception in setMasterVolume: Message: %s, Code: %d", __FUNCTION__, e.getMessage().c_str() , e.getCode());
        return kMspStatus_BadSource;
    }

    return kMspStatus_Ok;

}

eMspStatus Avpm::setdigitalAudioSetMode(void)
{
    FNLOG(DL_MSP_AVPM);
    return kMspStatus_Ok;
}

eMspStatus Avpm::setAC3AudioRange(tAvpmAudioRange audio_range)
{
    FNLOG(DL_MSP_AVPM);

    dsAudioCompression_t audio_compression = dsAUDIO_CMP_NONE;
    if (audio_range == tAvpmAudioRangeNarrow)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "In %s, tAvpmAudioRangeNarrow ->  dsAUDIO_CMP_HEAVY", __FUNCTION__);
        audio_compression = dsAUDIO_CMP_HEAVY;
    }
    else if (audio_range == tAvpmAudioRangeNormal)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "In %s, tAvpmAudioRangeNormal ->  dsAUDIO_CMP_MEDIUM", __FUNCTION__);
        audio_compression = dsAUDIO_CMP_MEDIUM;
    }
    else if (audio_range == tAvpmAudioRangeWide)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "In %s, tAvpmAudioRangeWide -> dsAUDIO_CMP_NONE", __FUNCTION__);
        audio_compression = dsAUDIO_CMP_NONE;
    }

    device::List<device::AudioOutputPort> aPorts = device::Host::getInstance().getAudioOutputPorts();
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "In %s, Audio Ports Size=%d ", __FUNCTION__, aPorts.size());
    for (size_t i = 0; i < aPorts.size(); i++)
    {
        try
        {
            device::AudioOutputPort &aPort = aPorts.at(i);
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "In %s, AUDIO Port Name=%s ", __FUNCTION__, aPort.getName().c_str());
            aPort.setCompression(audio_compression);
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d audio_compression APPLIED =%d", __FUNCTION__, __LINE__, audio_compression);
        }

        catch (const device::Exception e)
        {
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s Exception during setting Audio Compression mode: Message: %s, Code: %d", __FUNCTION__, e.getMessage().c_str() , e.getCode());
        }
    }

    return kMspStatus_Ok;
}

eMspStatus Avpm::setTVAspectRatio(eAvpmVideoRects videoRectsType)
{
    FNLOG(DL_MSP_AVPM);

    (void) videoRectsType;

    char framebuff[MAX_FRAMERATE_SIZE] = {0};
    int  convertFrameRate = *reinterpret_cast<int*>(&frame_asp);

    snprintf(framebuff, SIZE_OF_COMP, "%d", convertFrameRate);
    convertFrameRate = atoi(framebuff);
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, " user_aspect_ratio =  %d  frame_asp =  %f frame_buff=%s reinterpret_cast<frame_asp>=%d", user_aspect_ratio, frame_asp, framebuff, convertFrameRate);
    // really need to take into account PIP/POP here
    if (user_aspect_ratio == tAvpmTVAspectRatio16x9)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "user_aspect_ratio == tAvpmTVAspectRatio16X9 galio_rect.w ");

        if ((convertFrameRate < frameRate_16x19_4x3) && (picture_mode != tAvpmPictureMode_Stretch))
        {
            //HDTV,SD channel
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "f=1.33 uasp=16*9 fullscreen galio_width ");
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, " f=16*9 uasp=16*9 ");
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "f=16*9 uasp=16*9 full screen galio_width ");
        }
    }
    else if (user_aspect_ratio == tAvpmTVAspectRatio4x3)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "user_aspect_ratio == tAvpmTVAspectRatio4x3 ");

        if ((convertFrameRate > frameRate_16x19_4x3) && (picture_mode != tAvpmPictureMode_Stretch))
        {
            //SDTV,HD channel
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "fr_asp 16*9 us_asp 4x3 full screen galio_rect.w ");
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "fr_asp 4*3 us_asp 4x3 full screen galio_rect.w ");
        }

    }

    return kMspStatus_Ok;
}
void Avpm::SetVideoRects(eAvpmVideoRects videoRectsType)
{
    FNLOG(DL_MSP_AVPM);

    switch (videoRectsType)
    {
    case kAvpmSDVideo:
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s:%d:For Applying to SD only", __FUNCTION__, __LINE__);
    }
    break;
    case kAvpmHDVideo:
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s:%d:For Applying to HD only", __FUNCTION__, __LINE__);
    }
    break;
    case kAvpmBothHDSD:
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s:%d:For Applying to HD and SD", __FUNCTION__, __LINE__);
    }
    break;
    case kAvpmHDCpy:
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s:%d:COPYING to HD only", __FUNCTION__, __LINE__);
        break;
    case  kAvpmSDCpy:
    {
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s:%d:Copying to SD only", __FUNCTION__, __LINE__);
    }
    break;

    default:
        dlog(DL_MSP_AVPM, DLOGL_NOISE, "%s:%d:ERROR Must not be here", __FUNCTION__, __LINE__);
        break;

    }
}


eMspStatus Avpm::setPictureMode(eAvpmVideoRects videoRectsType)
{
    FNLOG(DL_MSP_AVPM);

    dlog(DL_MSP_AVPM, DLOGL_NOISE, " User Entered Zoom Type = %d  ", videoRectsType);

    if (mVideoZoomCB == NULL)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) mVideoZoomCB is NULL", __FUNCTION__, __LINE__);
        return kMspStatus_AvpmError;
    }

    switch (picture_mode)
    {
    case tAvpmPictureMode_Normal :
        mVideoZoomCB(mVideoZoomCBData, tAvpmPictureMode_Normal);
        break;

    case tAvpmPictureMode_Stretch :
        mVideoZoomCB(mVideoZoomCBData, tAvpmPictureMode_Stretch);
        break;

    case tAvpmPictureMode_Zoom25 :
        mVideoZoomCB(mVideoZoomCBData, tAvpmPictureMode_Zoom25);
        break;

    case tAvpmPictureMode_Zoom50 :
        mVideoZoomCB(mVideoZoomCBData, tAvpmPictureMode_Zoom50);
        break;

    default:
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Invalid Zoom Type  ", __FUNCTION__, __LINE__);
        break;
    }
    return kMspStatus_Ok;
}

eMspStatus Avpm::setDisplayResolution(const char* mode)
{
    FNLOG(DL_MSP_AVPM);
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, " %s , new resolution = %s", __PRETTY_FUNCTION__ , mode);

    device::List<device::VideoOutputPort> vPorts = device::Host::getInstance().getVideoOutputPorts();
    for (size_t i = 0; i < vPorts.size(); i++)
    {
        device::VideoOutputPort vPort = vPorts.at(i);
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s (%d) the port name is - %s", __FUNCTION__, i, vPort.getName().c_str());
        try
        {
            if (strcmp(vPort.getName().c_str(), "Baseband0") == 0)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s hard coded setting for bb0 ", __FUNCTION__);
                vPort.setResolution("480i");
            }
            else
            {
                dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s mode setting ", __FUNCTION__);
                vPort.setResolution(mode);
            }
        }
        catch (const device::Exception e)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s Exception in setDisplayResolution: Message: %s, Code: %d", __FUNCTION__, e.getMessage().c_str() , e.getCode());
        }
    }
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Set Display Mode: %s", mode);
    return kMspStatus_Ok;
}

eMspStatus Avpm::setRFmodeSetOutputChan(void)
{
    FNLOG(DL_MSP_AVPM);

    eMspStatus status = kMspStatus_Ok;
    unsigned int rf_channel = 3;

    eUseSettingsLevel settingLevel;
    char rfchannel[MAX_AVPM_SETTING_VALUE_SIZE] = {0};
    Uset_getSettingT(NULL, "ciscoSg/sys/rfOutputChannel", MAX_AVPM_SETTING_VALUE_SIZE, rfchannel, &settingLevel);

    if (strcmp((const char *)rfchannel, "channel3") == 0)
    {
        rf_channel = 3;
    }
    else if (strcmp((const char *)rfchannel, "channel4") == 0)
    {
        rf_channel = 4;
    }

    return status;
}


/* Copy Control Information Settings Start */

// This function will be called at boot up time.

eMspStatus Avpm::SetHDCP(HDCPState state)
{
    FNLOG(DL_MSP_AVPM);
    (void) state;
    return kMspStatus_Ok;
}

eMspStatus Avpm::SetCITState(CITState state)
{
    FNLOG(DL_MSP_AVPM);
    LOG(DLOGL_REALLY_NOISY, "Avpm::SetCITState is called with the state %d", state);
    device::List<device::VideoOutputPort> vPorts = device::Host::getInstance().getVideoOutputPorts();
    for (size_t i = 0; i < vPorts.size(); i++)
    {
        device::VideoOutputPort &vPort = vPorts.at(i);
        //Decimation is supported only for component port from platform(RDK)
        if ((strstr(vPort.getType().getName().c_str(), "Comp") != NULL))
        {
            try
            {
                LOG(DLOGL_REALLY_NOISY, "Setting the image constraint for Component port");
                vPort.setVideoDecimation(state);
            }
            catch (const device::Exception e)
            {
                dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s Exception is caught while Setting the image constraint for componentport. Message: %s, Code: %d", __FUNCTION__, e.getMessage().c_str() , e.getCode());
            }
            break;
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "iterating the next port");
        }

    }
    return kMspStatus_Ok;
}

eMspStatus Avpm::SetAPS(int state)
{
    FNLOG(DL_MSP_AVPM);
    eMspStatus status = kMspStatus_Ok;
    LOG(DLOGL_REALLY_NOISY, "Avpm::macrovision is called with the value %d", state);
    device::List<device::VideoOutputPort> vPorts = device::Host::getInstance().getVideoOutputPorts();
    for (size_t i = 0; i < vPorts.size(); i++)
    {

        device::VideoOutputPort &vPort = vPorts.at(i);

        if ((strstr(vPort.getType().getName().c_str(), "Base") != NULL) || (strstr(vPort.getType().getName().c_str(), "Comp") != NULL))
        {
            try
            {
                vPort.setVideoMacroVision(state);
                LOG(DLOGL_REALLY_NOISY, "Setting the macro vision for Analog port %s", vPort.getType().getName().c_str());
            }
            catch (const device::Exception e)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s Exception is caught while Setting the macro vision for Analog port. Message: %s, Code: %d", __FUNCTION__, e.getMessage().c_str() , e.getCode());
            }
        }
    }
    return status;

}

eMspStatus Avpm::SetEMI(uint8_t emi)
{
    LOG(DLOGL_REALLY_NOISY, "IPC: Avpm::%s called with emi %d -- NOT Supported yet", __FUNCTION__, emi);
    return kMspStatus_Ok;
}

eMspStatus Avpm::SetCCIBits(uint8_t ccibits, bool isSOC)
{
    FNLOG(DL_MSP_AVPM);

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
    FNLOG(DL_MSP_AVPM);

    mAudioLangChangedCb = aLangChangedCB;
    mAudioLangCbData = aCbData;
}

void Avpm::SetSapChangedCB(void *aCbData, SapChangedCB sapChangeCb)
{
    FNLOG(DL_MSP_AVPM);

    mSapChangeCb = sapChangeCb;
    mpSapChangedCbData = aCbData;
}

void Avpm::registerAVSettings()
{
    FNLOG(DL_MSP_AVPM);

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

            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%d:%s The Tag=%s", __LINE__, __FUNCTION__, pTag);

            status = Uset_registerValidatorT(settingChangedCB, pTag, this);
            if (status != USE_RESULT_OK)
            {

                dlog(DL_MSP_AVPM, DLOGL_ERROR, "%d:%s:Failed to register %s \n", __LINE__, __FUNCTION__, pTag);
            }
        }
        else
        {
            dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%d:%s The Tag=%s", __LINE__, __FUNCTION__, pTag);

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


/**
 *   @param isCaptionEnabled - to enable or disable the closed captions
 *
 *   @return kMspStatus_Ok
 *   @brief Function to enable or disable the closed caption.
 *          This triggers the callback function that is registered by the
 *          media players session source (MSPHTTPSource for IP client)
 */
eMspStatus Avpm ::setCc(bool isCaptionEnabled)
{
    FNLOG(DL_MSP_AVPM);
    eMspStatus retVal = kMspStatus_Error;
    isClosedCaptionEnabled = isCaptionEnabled;

    if (mCcChangedCb)
    {

        mCcChangedCb(mCcCbData, isCaptionEnabled);
        LOG(DLOGL_REALLY_NOISY, "%s ::::Triggered CC callback   mCCAtribIsSet = %d mCCLanguageStreamMap.size() = %d\n", __func__, mCCAtribIsSet, mCCLanguageStreamMap.size());
        // The attribute settings is valid in SDK only after we start the CC atleast once.
        // So when the call comes from the constructor of avpm to closedCaption, the settings will not be saved.
        // This has to be done during the first time the CC is set.
        // Hence using mCCAtribIsSet to set the attributes
        if ((true == isClosedCaptionEnabled) && ((false == mCCAtribIsSet) || (0 != mCCLanguageStreamMap.size())))
        {
            eAvpmEvent aEvent = kAvpmNotDefined;
            retVal = closedCaption(aEvent, NULL);
            mCCAtribIsSet = true;
        }
        else
        {

            retVal = kMspStatus_Ok;
        }
    }
    else
    {
        retVal = kMspStatus_Error;
        LOG(DLOGL_ERROR, "CC callback not registered by media player\n");
    }

    return retVal;
}

/**
 *   @param preview - specifies the desired attributes and their values for the preview
 *                    Also has the information to enable or disable the preview.
 *
 *   @return kMspStatus_Ok
 *   @brief Function to disable or enable preview with the specific attributes.
 */
eMspStatus Avpm ::setPreviewStatus(tAVPMCcPreview preview)
{
    eMspStatus retStatus = kMspStatus_Ok;
    unsigned int x = 100;
    unsigned int y = 10;
    unsigned int height = 2;
    unsigned int width = 30;
    mrcc_Error ccError = CC_EINVAL;

    short type = gsw_CcAttribType(GSW_CC_ATTRIB_FONT_SIZE | GSW_CC_ATTRIB_FONT_COLOR | GSW_CC_ATTRIB_BACKGROUND_COLOR | GSW_CC_ATTRIB_FONT_OPACITY |  GSW_CC_ATTRIB_WIN_COLOR |  GSW_CC_ATTRIB_BACKGROUND_OPACITY | GSW_CC_ATTRIB_WIN_OPACITY | GSW_CC_ATTRIB_FONT_STYLE | GSW_CC_ATTRIB_EDGE_TYPE);
    //Preview status one, show preview window
    if (preview.previewStatus == 1)
    {
        if (isPreviewEnabled != 1)
        {
            isCaptionEnabledBeforePreview = isClosedCaptionEnabled;
            isPreviewEnabled = true;
            ccError = ccSetCCState(CCStatus_OFF, 0); // Enables CC UI
            if (ccError != CC_SUCCESS)
            {
                LOG(DLOGL_ERROR, "ccSetCCState ON Failed with %d", ccError);
            }
        }

        memset(&mCcPreviewAttribute, 0, sizeof(mCcPreviewAttribute));
        mCcPreviewAttribute = mCcAttribute;

        //Assign values based on flags set
        if (preview.backgroundFlags)
        {
            if ((preview.backgroundFlags) & (eAVPMCcBackgroundOpacity))
            {
                setCcPreviewOpacity(preview.backgroundSettings.backgroundOpacity, &(mCcPreviewAttribute.charBgOpacity));
            }

            if ((preview.backgroundFlags) & (eAVPMCcBackgroundColor))
            {
                setCcPreviewColor(preview.backgroundSettings.backgroundColor, &(mCcPreviewAttribute.charBgColor));
            }

            if ((preview.backgroundFlags) & (eAVPMCcWindowColor))
            {
                setCcPreviewColor(preview.backgroundSettings.windowColor, &(mCcPreviewAttribute.winColor));
            }

            if ((preview.backgroundFlags) & (eAVPMCcWindowOpacity))
            {
                setCcPreviewOpacity(preview.backgroundSettings.windowOpacity, &(mCcPreviewAttribute.winOpacity));
            }
        }

        if (preview.characterFlags)
        {
            if ((preview.characterFlags) & (eAVPMCcCharacterColor))
            {
                setCcPreviewColor(preview.characterSettings.characterColor, &(mCcPreviewAttribute.charFgColor));
            }

            if ((preview.characterFlags) & (eAVPMCcCharacterOpacity))
            {
                setCcPreviewOpacity(preview.characterSettings.characterOpacity, &(mCcPreviewAttribute.charFgOpacity));
            }

            if ((preview.characterFlags) & (eAVPMCcCharacterSize))
            {
                if (preview.characterSettings.characterSize == eAVPMCcSizeSmall)
                {
                    mCcPreviewAttribute.fontSize = GSW_CC_FONT_SIZE_SMALL;
                }
                else if (preview.characterSettings.characterSize == eAVPMCcSizeLarge)
                {
                    mCcPreviewAttribute.fontSize = GSW_CC_FONT_SIZE_LARGE;
                }
                else
                {
                    mCcPreviewAttribute.fontSize = GSW_CC_FONT_SIZE_STANDARD;
                }
            }

            if ((preview.characterFlags) & (eAVPMCcCharacterEdge))
            {
                setCcPreviewEdge(preview.characterSettings.characterEdge, &(mCcPreviewAttribute.edgeType));
            }

            if ((preview.characterFlags) & (eAVPMCcCharacterFont))
            {
                setCcPreviewFont(preview.characterSettings.characterFont, &(mCcPreviewAttribute.fontStyle));
            }
        }

        ccError = ccSetAttributes(&mCcPreviewAttribute, type, GSW_CC_TYPE_DIGITAL);
        if (ccError != CC_SUCCESS)
        {

            LOG(DLOGL_ERROR, "ccSetAttributes DIGITAL Failed with %d", ccError);
        }

        ccError = ccSetAttributes(&mCcPreviewAttribute, type, GSW_CC_TYPE_ANALOG);
        if (ccError != CC_SUCCESS)
        {
            LOG(DLOGL_ERROR, "ccSetAttributes ANALOG Failed with %d", ccError);
        }


        ccError = ccShowPreview("Closed Caption Sample Text", x, y, height, width);
        if (ccError != CC_SUCCESS)
        {
            LOG(DLOGL_ERROR, "Preview enabling failed %d", ccError);
        }
    }

    if (preview.previewStatus != 1 && isPreviewEnabled == 1)
    {
        ccError = ccHidePreview();
        if (ccError != CC_SUCCESS)
        {
            LOG(DLOGL_ERROR, "Preview Hiding failed %d", ccError);
        }
        isPreviewEnabled = false;
    }

    if (!isPreviewEnabled)
    {
        LOG(DLOGL_REALLY_NOISY, "isCaptionEnabledBeforePreview =%d", isCaptionEnabledBeforePreview);
        if (isCaptionEnabledBeforePreview)
        {
            eAvpmEvent aEvent = kAvpmNotDefined;
            closedCaption(aEvent, NULL);
            ccError = ccSetCCState(CCStatus_ON, 0); // Enables CC UI
            if (ccError != CC_SUCCESS)
            {
                LOG(DLOGL_ERROR, "ccSetCCState ON Failed with %d", ccError);
            }
            LOG(DLOGL_REALLY_NOISY, "enabled Closed Caption");
        }
    }

    return retStatus;
}

/**
 *   @param previewFont    - CC font style
 *   @param setFontStyle   - pointer to CC Font style attribute
 *
 *   @return void
 *   @brief Function to check the font style and set font style
 *    attribute of the RDK CC attributes of structure gsw_CcAttributes
 *    for preview.
 */
void Avpm ::setCcPreviewFont(tAVPMCcFont previewFont, gsw_CcFontStyle *setFontStyle)
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
    setccFontStyle(setFontStyle, cc_char_font);
}

/**
 *   @param previewEdge    - CC font Edge Type
 *   @param setEdgeType   - pointer to CC Font Edge type attribute
 *
 *   @return void
 *   @brief Function to check the font Edge type and set font Edge type
 *    attribute of the RDK CC attributes of structure gsw_CcAttributes
 *    for preview.
 */
void Avpm ::setCcPreviewEdge(tAVPMCcEdge previewEdge, gsw_CcEdgeType *setEdgeType)
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
        LOG(DLOGL_ERROR, "Invalid edge attribute, setting to default");
        cc_char_edge = "noEdges";
    }
    setCcEdgeType(setEdgeType, cc_char_edge);
}

/**
 *   @param previewColor  - CC colour for font/bg/window attribute
 *   @param setColor      - pointer to CC font/bg/window color attribute
 *
 *   @return void
 *   @brief Function to check the color and set font/bg/window color
 *    attribute of the RDK CC attributes of structure gsw_CcAttributes
 *    for preview respectively.
 */
void Avpm ::setCcPreviewColor(tAVPMCcColor  previewColor, gsw_CcColor *setColor)
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
        LOG(DLOGL_ERROR, "Invalid color attribute, setting default color");
        cc_color = "white";
    }

    //Set CC preview attribute colour
    setColor->rgb = mColorCharToInt[(string)cc_color];
}

/**
 *   @param previewOpacity  - CC opacity type for font/bg/window attribute
 *   @param setOpacity      - pointer to CC font/bg/window opacity attribute
 *
 *   @return void
 *   @brief Function to check the opacity type and set font/bg/window opacity
 *    attribute of the RDK CC attributes of structure gsw_CcAttributes
 *    for preview respectively.
 */
void Avpm ::setCcPreviewOpacity(tAVPMCcOpacity previewOpacity, gsw_CcOpacity *setOpacity)
{
    if (previewOpacity == eAVPMCcOpacityTransparent)
    {
        *setOpacity   = GSW_CC_OPACITY_TRANSPARENT;
    }
    else if (previewOpacity  == eAVPMCcOpacityTransculent)
    {
        *setOpacity = GSW_CC_OPACITY_TRANSLUCENT;
    }
    else
    {
        *setOpacity = GSW_CC_OPACITY_SOLID;
    }
}

/**
 *   @param pIsCaptionEnabled 	  - Output param indicating the current state of Closed Caption
 *
 *   @return kMspStatus_Ok	-	Successfully returned the CC status
 *			 kMspStatus_BadParameters  -  Invalid pointer
 *   @brief Getter method indicating the current state of CC
 *
 */
eMspStatus Avpm ::getCc(bool *pIsCaptionEnabled)
{
    FNLOG(DL_MSP_AVPM);

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
    FNLOG(DL_MSP_AVPM);
    eMspStatus status = kMspStatus_Ok;

    device::List<device::VideoOutputPort> vPorts = device::Host::getInstance().getVideoOutputPorts();
    for (size_t i = 0; i < vPorts.size(); i++)
    {

        device::VideoOutputPort &vPort = vPorts.at(i);

        /* If port type is not HDMI,then enable SOC for all the analog ports */
        LOG(DLOGL_REALLY_NOISY, "port type while iterating is %s", vPort.getType().getName().c_str());
        if ((strstr(vPort.getType().getName().c_str(), "HDMI") == NULL))
        {
            if (isSoc)
            {
                try
                {
                    LOG(DLOGL_REALLY_NOISY, "SOC is set for Analog port by calling setvideomute");
                    vPort.setVideoMute(true);
                }
                catch (const device::Exception e)
                {
                    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s Exception is caught while setting video mute. Message: %s, Code: %d", __FUNCTION__, e.getMessage().c_str() , e.getCode());
                }
            }
            else
            {
                try
                {
                    LOG(DLOGL_REALLY_NOISY, "SOC is unset for Analog port by calling setvideo mute ");
                    vPort.setVideoMute(false);
                }
                catch (const device::Exception e)
                {
                    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s Exception is caught while unsetting video mute. Message: %s, Code: %d", __FUNCTION__, e.getMessage().c_str() , e.getCode());
                }
            }
        }
    }
    return status;
}


/**
 *   @param aCbData 	  - callback user data
 *   @param ccChangedCB   - callback function
 *
 *   @return None
 *   @brief Helper Function to register a function to receive the change in
 *          closed caption enable/disable status.
 *
 *          Example - MSPHTTPSource registers a callback function with AVPM
 *          and AVPM notifies MSPHTTPSource whenever there is a change in
 *          closed caption on/off state.
 */
void Avpm::SetCcChangedCB(void *aCbData, CcChangedCB ccChangedCB)
{
    FNLOG(DL_MSP_AVPM);

    mCcChangedCb = ccChangedCB;
    mCcCbData = aCbData;
}


/**
 *   @param aVzData 	  - callback user data
 *   @param ccChangedCB   - pointer to callback function
 *
 *   @return None
 *   @brief Helper Function to register a function to receive the change in
 *          closed caption enable/disable status.
 *
 *          Example - MSPHTTPSource registers a callback function with AVPM
 *          and AVPM notifies MSPHTTPSource whenever there is a change in
 *          Zoom type.
 */
void Avpm::SetVideoZoomCB(void *aVzData, VideoZoomCB videoZoomCB)
{
    FNLOG(DL_MSP_AVPM);
    mVideoZoomCB = videoZoomCB;
    mVideoZoomCBData = aVzData;
}

tAvpmPictureMode Avpm::getZoomType()
{
    dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "Returning picture_mode=%d ", picture_mode);
    return picture_mode;
}

/*Method returns the Service no for the digital Lang*/
int Avpm::getDigitalLangToServiceMap(char* ccDigitalSetting)
{
    FNLOG(DL_MSP_AVPM);
    ccPMTLangServNoMap::iterator ccIter;
    ccFallbackLangMap::iterator defLangIter;
    ccFallbackServiceMap::iterator defServiceIter;

    int retVal = DEFAULT_CC_DIGITAL_OPTION;
    std::string stringToSearch = ccDigitalSetting;
    //Determine the digital languange to be set
    //Logic: First check if there is exact match of user preference in the caption descriptor
    // If not found check the fallback service number
    LOG(DLOGL_REALLY_NOISY, "%s ::: user pref language %s", __func__, ccDigitalSetting);
    if (0 <  mCCLanguageStreamMap.size())
    {
        if (mCCLanguageStreamMap.end() != (ccIter = mCCLanguageStreamMap.find(stringToSearch.c_str())))
        {
            LOG(DLOGL_REALLY_NOISY, "%s ::: Found matching CC Lang Service No %d n descriptor map for %s", __func__, ccIter->second, ccDigitalSetting);
            return(ccIter->second);
        }
        //Else check if there is an entry available for fallback language in PMT
        defLangIter = mfallBackCCLangMap.find(stringToSearch.c_str());
        if (mfallBackCCLangMap.end() != defLangIter)
        {
            if (mCCLanguageStreamMap.end() != (ccIter = mCCLanguageStreamMap.find(defLangIter->second.c_str())))
            {
                LOG(DLOGL_REALLY_NOISY, " Fallback CC Lang Service No %d n descriptor map for %s", ccIter->second, ccDigitalSetting);
                return(ccIter->second);
            }

        }

    }
    else
    {
        LOG(DLOGL_ERROR, "%s :::: mCCLanguageStreamMap is EMPTY!!!! ", __func__);
    }

    // By setting it to DEFAULT_CC_DIGITAL_OPTION the migration case is also handled. Since in older version of code the
    // values would be "d1-d6" it would fall to the default DEFAULT_CC_DIGITAL_OPTION. The UI code as well would show
    // "ENG" since it wouldn't find a match for "dx" in the new list of "eng/spa/fre/eng_ez/spa_ez/fre_ez"
    retVal = (mfallbackCCServiceMap.end() != (defServiceIter = mfallbackCCServiceMap.find(stringToSearch.c_str()))) ? defServiceIter->second : DEFAULT_CC_DIGITAL_OPTION ;
    LOG(DLOGL_REALLY_NOISY, "No  matching Lang Service Found .Using Default Service No: %d for %s", retVal, ccDigitalSetting);
    return (retVal);

}

int countSetBits(int n)
{
    unsigned int count = 0;
    while (n)
    {
        n &= (n - 1) ;
        count++;
    }
    return count;
}

/*Method to update CC Language stream map for the session*/
void Avpm::updateCCLangStreamMap(tMpegDesc* pCcDescr)
{

    FNLOG(DL_MSP_AVPM);
    eMspStatus status = kMspStatus_Ok;
    unsigned char noOfServices = 0;
    mCCLanguageStreamMap.clear();
    uint8_t *buff = pCcDescr->data;
    caption_service_descriptor *stCsd = (caption_service_descriptor *) buff;
    unsigned char reserved1 = stCsd->reserved;
    int count_setBits_reserved1 = countSetBits(reserved1);
    LOG(DLOGL_NOISE, "%s(%d): reserved1: = %d No of 1's in reserved1 = %d", __FUNCTION__, __LINE__, reserved1, count_setBits_reserved1);
    if (count_setBits_reserved1 != 3)       //reserved1 has 3 bits. Checking if all reserved1 bits are 1 or not.
    {
        LOG(DLOGL_ERROR, "%s(%d): All reserved bits in reserved1 are not equal to 1. Error in Stream.  ", __FUNCTION__, __LINE__);
        status = kMspStatus_Error;
    }
    noOfServices = stCsd->number_of_services;

    LOG(DLOGL_REALLY_NOISY, "%s(%d): noOfServices: %d stCsd->number_of_services: %d & buff[0] %02x \n", __FUNCTION__, __LINE__, noOfServices, stCsd->number_of_services, buff[0]);
    if (((noOfServices * 6) + 1) != (int)pCcDescr->dataLen)
    {
        LOG(DLOGL_ERROR, "%s(%d): Number of services in the stream has inappropriate number of bytes in the stream...Error in Stream.. Returning kMspStatus_Error ", __FUNCTION__, __LINE__);
        status = kMspStatus_Error;
    }

    for (unsigned int i = 0; i < noOfServices ; i++)
    {
        caption_service_entry *stCse = (caption_service_entry *) &buff[1];
        unsigned char reserved2 = stCse->reserved;
        LOG(DLOGL_NOISE, "%s(%d): reserved2: = %d ", __FUNCTION__, __LINE__, reserved2);
        if (reserved2 != 1) //reserved2 has 1 bit.Checking if reserved2 bit is 1 or not.
        {
            LOG(DLOGL_ERROR, "%s(%d): Reserved bit in reserved2 is not equal to 1. Error in Stream.  ", __FUNCTION__, __LINE__);
            status = kMspStatus_Error;
        }
        if (true == stCse->digital_cc)
        {
            char langCode[CAPTION_LANG_SIZE] = {0};
            snprintf(langCode, CC_LANG_BCK_COMP_ISO_LENGTH + 1, "%s%s", BACKWARD_COMPATIBLE_STR, stCse->language);
            string insertString = langCode;
            caption_easy_reader *stCer = (caption_easy_reader *)(buff + CAPTION_SERVICE_OFFSET);
            bitswap16((uint8_t*)stCer);
            LOG(DLOGL_NOISE, " EZ_R = %d  ", stCer->easy_reader);
            if (stCer->easy_reader)
            {
                insertString += EZ_READER_SUFFIX;

            }
            uint16_t reserved3 = stCer->reserved1;
            int count_setBits_reserved3 = countSetBits(reserved3);
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): reserved3: = %d No of 1's in reserved3 = %d", __FUNCTION__, __LINE__, reserved3, count_setBits_reserved3);
            if (count_setBits_reserved3 != 14) //reserved3 has 14 bits. Checking if all reserved3 bits are 1 or not.
            {
                LOG(DLOGL_ERROR, "%s(%d): All reserved bits in reserved3 are not equal to 1. Error in Stream.  ", __FUNCTION__, __LINE__);
                status = kMspStatus_Error;
            }
            LOG(DLOGL_REALLY_NOISY, "%s ::: Lang:%s d_cc %d EZ_R %d  captionServiceNumber %d ", __func__, insertString.c_str(), stCse->digital_cc, stCer->easy_reader, stCse->captionServiceNumber);

            mCCLanguageStreamMap.insert(std::make_pair(insertString, stCse->captionServiceNumber));

        }
        buff += CAPTION_SERVICE_SKIP_LENGTH;

    }
}
