/**
   \file avpm_VOD1080p.cpp
   Implementation file for 1080p related functions of AVPM class.
*/

#include "avpm.h"
#include "sail_dfb.h"
#include <dlog.h>
#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_AVPM, level,"Avpm:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

bool Avpm::isCurrentDisplayResolution1080p(const tCpeAvoxDigDispType displayType)
{
    bool is1080p = false;
    DFBResult result;
    DFBScreenEncoderConfig enc_descriptions;
    IDirectFBScreen *pScreen = NULL;
    tCpeDFBScreenIndex screenIndex = eCpeDFBScreenIndex_SD;

    FNLOG(DL_MSP_AVPM);

    memset(&enc_descriptions, 0, sizeof(enc_descriptions));

    if (tCpeAvoxDigDispType_HDMI == displayType)
    {
        screenIndex = eCpeDFBScreenIndex_HD;
    }

    // Get DFB Screens
    result = dfb->GetScreen(dfb, screenIndex, &pScreen);
    if ((result != DFB_OK) || (pScreen == NULL))
    {
        LOG(DLOGL_ERROR, "Error in getting DFB screen interface. Error code = %d", result);
        return is1080p;
    }

    result = pScreen->GetEncoderConfiguration(pScreen, 0, &enc_descriptions);
    if (result != DFB_OK)
    {
        LOG(DLOGL_ERROR, "Error in getting encoder descriptions. Err: %d", result);
        return is1080p;
    }
    LOG(DLOGL_REALLY_NOISY, "HD encoder res %d scanmode %d tv_std %d", enc_descriptions.resolution, enc_descriptions.scanmode, enc_descriptions.tv_standard);

    if (enc_descriptions.resolution == DSOR_1920_1080 &&
            enc_descriptions.scanmode == DSESM_PROGRESSIVE)
    {
        is1080p = true;
    }
    return is1080p;
}

bool Avpm::isSource1080p(tCpePgrmHandle pgrHandle)
{
    tCpeMediaStreamData streamData;
    bool isSrc1080p = false;

    FNLOG(DL_MSP_AVPM);

    // Fetch stream characteristics.
    int retStatus = cpe_media_Get(pgrHandle, eCpeMediaGetSetNames_StreamData, &streamData, sizeof(streamData));
    if (0 != retStatus)
    {
        LOG(DLOGL_ERROR, "Could not get stream data!!!");
        return isSrc1080p;
    }

    LOG(DLOGL_ERROR, "pgrHandle: %p verticalSize : %d horizontalSize : %d  progressiveSequence : %d frameRate : %d",
        pgrHandle, streamData.verticalSize, streamData.horizontalSize, streamData.progressiveSequence, streamData.frameRate);

    // check if source is 1920x1080 progressive.
    if ((streamData.verticalSize >= TENEIGHTYP_VERTICAL) &&
            (streamData.horizontalSize >= TENEIGHTYP_HORIZONTAL) &&
            streamData.progressiveSequence)
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "Channel source is 1080p.");
        isSrc1080p = true;

        if (streamData.frameRate >= TENEIGHTYP_FRAMERATE)
        {
            LOG(DLOGL_ERROR, "Frame rate is high: %d", streamData.frameRate);
        }
    }
    else
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "Channel source is NOT 1080p.");
    }
    return isSrc1080p;
}

void Avpm::updateVOD1080pSetting(const char* value)
{
    eUse_StatusCode statusCode;
    char useemValue[MAX_SETTING_VALUE_SIZE] = {'0'};
    snprintf(useemValue, MAX_SETTING_VALUE_SIZE * sizeof(char), value);
    statusCode = Uset_setSettingT(NULL, UNISETTING_VOD1080P, MAX_SETTING_VALUE_SIZE, useemValue, eUseSettingsLevel_User);
    if (USE_RESULT_OK != statusCode)
    {
        LOG(DLOGL_ERROR, "Failed to set %s value as %s in Unified Settings. Error: %d", UNISETTING_VOD1080P, value, statusCode);
    }
}

void Avpm::sendSAILMessage(eVOD1080pMsgType msg)
{
    Sail_Message mess;
    memset((void *)&mess, 0, sizeof(Sail_Message));
    mess.message_type = SAIL_MESSAGE_USER;
    mess.data[0] = kSailMsgEvt_VOD_1080p;		// VOD 1080p Message type
    mess.data[1] = msg;                        // 1 - Show user confirmation modal, 2 - Inform user that display is not 1080p capable.
    mess.strings = NULL;

    LOG(DLOGL_MINOR_DEBUG, "BEFORE SEND SAIL MSG.");
    Csci_BaseMessage_Send(&mess);
    LOG(DLOGL_MINOR_DEBUG, "AFTER SEND SAIL MSG.");
}

void Avpm::setOptimalDisplayResolution()
{
    FNLOG(DL_MSP_AVPM);
    eMspStatus resStatus;

    // Find out the display type and it's capabilities.
    if (determineDisplayCapabilities())
    {
        mIs1080pModeActive = true;
        LOG(DLOGL_MINOR_DEBUG, "mIs1080pModeActive: %d", mIs1080pModeActive);
        LOG(DLOGL_SIGNIFICANT_EVENT, "Optimal resolution determined, setting 1080p display resolution.");

        if (mDisplayTypeIsHDMI)
        {
            // If Display type is HDMI and if it supports 1080p (as per the result of determineDisplayCapabilities()):
            // 1. Set 1080p output resolution.
            // 2. No need to persist the 1080p support because, display capability check
            //    will be performed for every 1080p VOD asset playback.
            resStatus = setDisplayResolution(tAvpmResolution_1080p);
            LOG(DLOGL_MINOR_DEBUG, "Result of setDisplayResolution() is %d", resStatus);
            return;
        }
        else
        {
            // If display type is non-HDMI display.
            // 1. Set 1080p output resolution.
            // 2. Get user confirmation to know whether 1080p is properly displayed or not.
            // 3. Persist the 1080p support and use it for further 1080p VOD asset playbacks,
            //    to avoid user interaction every time 1080p asset is played.
            LOG(DLOGL_MINOR_DEBUG, "mIs1080pModeActive: %d", mIs1080pModeActive);
            eUseSettingsLevel settingLevel;
            char vodSetting[MAX_SETTING_VALUE_SIZE] = {0};
            if (USE_RESULT_OK != Uset_getSettingT(NULL, UNISETTING_VOD1080P, MAX_SETTING_VALUE_SIZE, vodSetting, &settingLevel))
            {
                LOG(DLOGL_ERROR, "Unable to read value of %s. Considering default value.", UNISETTING_VOD1080P);
                // If we cannot read the setting, consider NULL value and perform 1080p test.
                strncpy(vodSetting, VOD1080PSETTING_UNKNOWN, MAX_SETTING_VALUE_SIZE);
            }

            LOG(DLOGL_MINOR_DEBUG, "Value from settings: %s: %s", UNISETTING_VOD1080P, vodSetting);

            // If vod1080p setting is "false", that means:
            // 1. Display does not support 1080p or
            // 2. User was unable to see 1080p or was unable to give a confirmation that 1080p is watchable
            // So, do not set 1080p this time.
            if (strcmp((const char *)vodSetting, VOD1080PSETTING_NOT_SUPPORTED) == 0)
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "vod1080pDisplay = notSupported. User could not see 1080p / does not want 1080p.");
                mIs1080pModeActive = false;
                LOG(DLOGL_MINOR_DEBUG, "mIs1080pModeActive: %d", mIs1080pModeActive);
                return;
            }

            // If vod1080p setting is "true", that means:
            // 1. Display supports 1080p. or
            // 2. User confirmed that 1080p is supported by the display.
            // So, set 1080p display resolution this time, without any further checks.
            if (strcmp((const char *)vodSetting, VOD1080PSETTING_SUPPORTED) == 0)
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "vod1080pDisplay = supported, setting 1080p display resolution.");
                resStatus = setDisplayResolution(tAvpmResolution_1080p);
                LOG(DLOGL_MINOR_DEBUG, "Result of setDisplayResolution() is: %d", resStatus);
                return;
            }

            // If vos1080p setting is neither "true" nor "false", then it should be "NULL", which means:
            // 1. This is the first time we are testing 1080p on the connected display or
            // 2. User wants to perform the 1080p test once again, for any of the below reasons:
            //    a. The display is changed.
            //	  b. The user could not select Cancel/Save Setting on the confirmation barker,
            //       in the previous 1080p test.
            // Determine and set 1080p output resolution with optimal frequency, based on display and STB capabilities.
            LOG(DLOGL_SIGNIFICANT_EVENT, "vod1080pDisplay = unknown, setting 1080p display resolution.");
            resStatus = setDisplayResolution(tAvpmResolution_1080p);
            LOG(DLOGL_MINOR_DEBUG, "Result of setDisplayResolution() is: %d", resStatus);
            LOG(DLOGL_NORMAL, "Setting 1080p for the first time on NON-HDMI display, get user confirmation for 1080p display");
            sendSAILMessage(kVOD1080pMsgType_Confirmation_Barker);
        }
    }
    else
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "Display resolution is already optimal. No change required.");
    }
}

bool Avpm::determineDisplayCapabilities()
{
    FNLOG(DL_MSP_AVPM);
    IAVOutput *pAvox = NULL;

    // Query the HMDI info
    if (DFB_OK != dfb->GetInterface(dfb, "IAVOutput", "default", NULL, (void**)&pAvox))
    {
        LOG(DLOGL_ERROR, "IDirectFB::GetInterface() failed. Cannot determine optimal resolution.");
        return false;
    }

    tCpeAvoxDigDispInfo info;
    info.flags = eCpeAvoxDigDispInfoFlags_All;
    if (DFB_OK != pAvox->GetDigitalDisplayInfo(pAvox, eCpeDFBScreenIndex_HD, &info))
    {
        LOG(DLOGL_ERROR, "IAVOutput::GetDigitalDisplayInfo() failed. Cannot determine optimal resolution.");
        pAvox->Release(pAvox);
        return false;
    }
    pAvox->Release(pAvox);

    LOG(DLOGL_REALLY_NOISY, "=================================\n");
    LOG(DLOGL_REALLY_NOISY, "      Digital Display Info\n");
    LOG(DLOGL_REALLY_NOISY, "=================================\n");
    LOG(DLOGL_REALLY_NOISY, "name      = %c%c%c\n",   info.name[0], info.name[1], info.name[2]);
    LOG(DLOGL_REALLY_NOISY, "product   = %d\n",       info.product);
    LOG(DLOGL_REALLY_NOISY, "serialNum = %d\n",       info.serialNum);
    LOG(DLOGL_REALLY_NOISY, "mfgWeek   = %d\n",       info.mfgWeek);
    LOG(DLOGL_REALLY_NOISY, "mfgYear   = %d\n",       info.mfgYear);
    LOG(DLOGL_REALLY_NOISY, "type      = %d\n",       info.type);
    LOG(DLOGL_REALLY_NOISY, "AR        = %2.3f\n",    info.aspectRatio);
    LOG(DLOGL_REALLY_NOISY, "Modes:\n");
    LOG(DLOGL_REALLY_NOISY, "all       = %.8x\n",    info.allModes);
    LOG(DLOGL_REALLY_NOISY, "implicit  = %.8x\n",    info.implicitModes);
    LOG(DLOGL_REALLY_NOISY, "explicit  = %.8x\n",    info.explicitModes);
    LOG(DLOGL_REALLY_NOISY, "native    = %.8x\n",    info.nativeModes);
    LOG(DLOGL_REALLY_NOISY, "preferred = %.8x\n",    info.preferredModes);

    // If current output resolution is 1080p, do nothing, that is the best possible resolution supported.
    if (isCurrentDisplayResolution1080p(info.type))
    {
        LOG(DLOGL_NORMAL, "Current Display Resolution is 1080p. Do nothing.");
        updateVOD1080pSetting(VOD1080PSETTING_NOT_SUPPORTED);
        return false;
    }

    // If HDMI is connected, query capabilities of the TV.
    // If displays are connected to both AV/Component and HDMI, then only HDMI will be considered.
    if (info.type == tCpeAvoxDigDispType_HDMI)
    {
        mDisplayTypeIsHDMI = true;
        LOG(DLOGL_SIGNIFICANT_EVENT, "Display type is HDMI. Determining display capabilities..");
        bool tvSupport = doesTVSupport1080p(info);
        // If display does not support 1080p, return false, resolution need not be changed to 1080p.
        if (false == tvSupport)
        {
            LOG(DLOGL_ERROR, "TV does not support particular frequency over HDMI. Cannot set 1080p display resolution.");
            return tvSupport;
        }
    }
    else
    {
        // If not HDMI, composite(AV)/component may be connected.
        // We cannot distinguish between composite and component, capabilities cannot be programatically determined.
        // Set 1080p and get confirmation from user.
        LOG(DLOGL_SIGNIFICANT_EVENT, "Display type is not HDMI. User confirmation required for 1080p support");
        mDisplayTypeIsHDMI = false;
    }
    return true;
}

void Avpm::setDefaultDisplayResolution()
{
    FNLOG(DL_MSP_AVPM);
    eAvpmResolution optResolution;

    if (!getResolutionFromMode(optResolution))
    {
        LOG(DLOGL_ERROR, "Unsupported mode!!!");
        return;
    }

    eMspStatus status = setDisplayResolution(optResolution);
    LOG(DLOGL_MINOR_DEBUG, "Result of setDisplayResolution() is %d", status);
    if (kMspStatus_Ok == status)
    {
        mIs1080pModeActive = false;
        LOG(DLOGL_NOISE, "mIs1080pModeActive: %d", mIs1080pModeActive);
    }
}

bool Avpm::getResolutionFromMode(eAvpmResolution &dispResolution)
{
    bool status = true;
    eUseSettingsLevel settingLevel;
    char resMode[MAX_SETTING_VALUE_SIZE] = {0};
    if (USE_RESULT_OK != Uset_getSettingT(NULL, UNISETTING_RESOLUTION, MAX_SETTING_VALUE_SIZE, resMode, &settingLevel))
    {
        LOG(DLOGL_ERROR, "Unable to read value of: %s", UNISETTING_RESOLUTION);
        return false;
    }

    if (strcmp((const char *)resMode, "480i") == 0)
    {
        LOG(DLOGL_REALLY_NOISY, "Setting 480i resolution");
        dispResolution = tAvpmResolution_480i;
    }
    else if (strcmp((const char *)resMode, "480p") == 0)
    {
        LOG(DLOGL_REALLY_NOISY, "Setting 480p resolution");
        dispResolution = tAvpmResolution_480p;
    }
    else if (strcmp((const char *)resMode, "720p") == 0)
    {
        LOG(DLOGL_REALLY_NOISY, "Setting 720p resolution");
        dispResolution = tAvpmResolution_720p;
    }
    else if (strcmp((const char *)resMode, "1080i") == 0)
    {
        LOG(DLOGL_REALLY_NOISY, "Setting 1080i resolution");
        dispResolution = tAvpmResolution_1080i;
    }
    else if (strcmp((const char *)resMode, "1080p") == 0)
    {
        LOG(DLOGL_REALLY_NOISY, "Setting 1080p resolution ");
        dispResolution = tAvpmResolution_1080p;
    }
    else
    {
        //default resolution state as 720p
        dispResolution = tAvpmResolution_720p;//Defalut preffered Mode
    }
    return status;
}
