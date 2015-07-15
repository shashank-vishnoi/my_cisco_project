
/**
   \file UnifiedSettings.cpp
   Implementation file for UnifiedSettings class


*/


///////////////////////////////////////////////////////////////////////////
//                    SAIL Includes
///////////////////////////////////////////////////////////////////////////

#include <dlog.h>

///////////////////////////////////////////////////////////////////////////
//                         Local Includes
///////////////////////////////////////////////////////////////////////////

#include "UnifiedSetting.h"
#include "directfb.h"



#define UNUSED_PARAM(a) (void)a;


///////////////////////////////////////////////////////////////////////////
//                      Member implementation///////////////////////////////////////////////////////////////////////////

/** *********************************************************/

UnifiedSetting* UnifiedSetting::mSettings = 0;

UnifiedSetting::UnifiedSetting()
{
    mDisplayResolution = DSOR_1920_1080;
    mPictureMode = 0;// Normal Picture Mode
    mAudioRange = 1; // Normal Audio Range
    mDigitalAudioMode = 2 ; //tAvpmDigitalAudioOutputUnCompressed
    mOutputMode = 0;
    mRFModeChannel = 3; // Default RF Channel
    mAspectRatio = 1; // Aspect ratio 16x9
    mMute = 0;
    mVolumeLevel = -20;
    mClosedCaption = false;
    mAudioLanguage = "eng,spa,zho,fra,deu,ita,jpn,kor,ara";

//	mDisplayResolution = DSOR_640_480;
//	mPictureMode = tAvpmPictureMode_Normal;
//	mAudioRange = tAvpmAudioRangeNormal;
//	mDigitalAudioMode = tAvpmDigitalAudioOutputCompressed;
    //mOutputMode = ;
    //mRFModeChannel = ;
//	mAspectRatio = tAvpmTVAspectRatio16x9;
//	mMute = 1;
//	mVolumeLevel =5;
}

UnifiedSetting::~UnifiedSetting()
{
    // Erase all map entries.
}

UnifiedSetting* UnifiedSetting:: getInstance()
{
    if (NULL == mSettings)
    {
        mSettings = new UnifiedSetting();
    }
    return mSettings;
}

int UnifiedSetting::registerSettingCB(SettingChangedCB cb, void *aUserData)
{
    if (!cb)
    {
        return -1;
    }
    else
    {
        mMap.insert(pair<void*, SettingChangedCB>(aUserData, cb));
        return 0;
    }
}

int UnifiedSetting::unRegisterSettingCB(void *aUserData)
{
    mMap.erase(aUserData);
    return  0;
}

int UnifiedSetting::getDisplayResolution()const
{
    return mDisplayResolution;
}

int UnifiedSetting::getPictureMode()const
{
    return mPictureMode;
}

int UnifiedSetting::getAudioRange()const
{
    return mAudioRange;
}

int UnifiedSetting::getDigitalAudioMode()const
{
    //tAvpmDigitalAudioOutputMode Audio_Output = eCpeAvoxDigitalOutputMode_AC3;
    return mDigitalAudioMode;
}

int UnifiedSetting::getOutputMode()const
{
    return mOutputMode;
}

int UnifiedSetting::getRFModeChannel()const
{
    return mRFModeChannel;
}

int UnifiedSetting::getAspectRatio()const
{
    return mAspectRatio;
}

bool UnifiedSetting::getMute()const
{

    return mMute;
}

int UnifiedSetting::getVolumeLevel()const
{
    return mVolumeLevel;
}

bool UnifiedSetting::getClosedCaption()const
{
    return mClosedCaption;
}

void UnifiedSetting::setDisplayResolution(int aDisplayResolution)
{
    mDisplayResolution = aDisplayResolution;
    sendCallback(kConfigParam_DisplayResolutionUpdate);
}
void UnifiedSetting::setPictureMode(int aPictureMode)
{
    mPictureMode = aPictureMode;
    sendCallback(kConfigParam_PictureModeUpdate);
}

void UnifiedSetting::setAudioRange(int aAudioRange)
{
    mAudioRange = aAudioRange;
    sendCallback(kConfigParam_AC3AudiorangeUpdate);
}

void UnifiedSetting::setDigitalAudioMode(int aDigitalAudioMode)
{
    mDigitalAudioMode = aDigitalAudioMode;
    sendCallback(kConfigParam_DigitalAudioModeUpdate);
}

void UnifiedSetting::setOutputMode(int aOutputMode)
{
    mOutputMode = aOutputMode;
    sendCallback(kConfigParam_OutputModeUpdate);
}

void UnifiedSetting::setRFModeChannel(int aRFModeChannel)
{
    mRFModeChannel = aRFModeChannel;
    sendCallback(kConfigParam_RFModeoutputChannelUpdate);
}

void UnifiedSetting::setAspectRatio(int aAspectRatio)
{
    mAspectRatio = aAspectRatio;
    sendCallback(kConfigParam_TVAspectRatio);
}

void UnifiedSetting::setMute(int aMute)
{
    mMute = aMute;
    sendCallback(kConfigParam_MasterMuteUpdate);
}

void UnifiedSetting::setVolumeLevel(int aVolumeLevel)
{
    mVolumeLevel = aVolumeLevel;
    sendCallback(kConfigParam_MasterVolUpdate);
}

void UnifiedSetting::setClosedCaption(bool aClosedCaption)
{
    mClosedCaption = aClosedCaption;
    sendCallback(kConfigParam_ClosedCaptionUpdate);
}

void UnifiedSetting::sendCallback(paramType aParamType)
{
    map<void *, SettingChangedCB>::iterator iter = mMap.begin();
    while (iter != mMap.end())
    {
        void *userData = iter->first;
        SettingChangedCB cb = iter->second;
        cb(aParamType, userData);
        ++iter;
    }
}
void UnifiedSetting::setAudioLanguage(char *lang)
{
    mAudioLanguage = lang;
}

const char* UnifiedSetting::getAudioLanguage() const
{
    return mAudioLanguage;
}

