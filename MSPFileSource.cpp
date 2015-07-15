#include <time.h>
#include "MSPFileSource.h"
#include <cpe_programhandle.h>
#include <cpe_recmgr.h>
#include "sys/xattr.h"
#include <sail-clm-api.h>


#define LOG(level, msg, args...)  dlog(DL_MSP_DVR, level,"MSPFileSource:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

MSPFileSource::MSPFileSource(std::string aSrcUrl)
{
    mSrcUrl = aSrcUrl;
    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Inside MSPFileSource Constructor %s %s", mSrcUrl.c_str(), aSrcUrl.c_str());
    mProgramNumber = 0;
    mSourceId = 0;
    mCpeSrcHandle = 0;
    mSrcCbIdEOF = 0;
    mSrcCbIdBOF = 0;
    mSrcStateCB = NULL;
    mClientContext = NULL;
    mCurrentFileIndex = 1;
    mIsRewindMode = false;
    mStarted = false;
    buildFileList();
    setFileByIndex(mCurrentFileIndex);
}

MSPFileSource::~MSPFileSource()
{
    if (mCpeSrcHandle != 0)
    {
        int cpeStatus;

        if (mStarted == true)
        {
            cpeStatus = cpe_src_Stop(mCpeSrcHandle);
            if (cpeStatus != kCpe_NoErr)
            {
                LOG(DLOGL_ERROR, "cpe_src_stop error: %d", cpeStatus);
            }
            mStarted = false;
        }

        if (mSrcCbIdEOF != 0)
        {
            cpe_src_UnregisterCallback(mCpeSrcHandle, mSrcCbIdEOF);
        }

        if (mSrcCbIdBOF != 0)
        {
            cpe_src_UnregisterCallback(mCpeSrcHandle, mSrcCbIdBOF);
        }

        cpeStatus = cpe_src_Close(mCpeSrcHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "cpe_src_close failed with error code %d", cpeStatus);
        }

        mCpeSrcHandle = 0;
    }

    std::list<std::string>::iterator it;
    it = mFileNameList.begin();
    if (it != mFileNameList.end())
    {
        mFileNameList.erase(it++);
    }
}

eMspStatus MSPFileSource::load(SourceStateCallback aPlaybackCB, void* aClientContext)
{
    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "MSPFileSource Load\n");

    mSrcStateCB = aPlaybackCB;
    mClientContext = aClientContext;

    return mParseStatus;
}

eMspStatus MSPFileSource::open(eResMonPriority pri)
{
    (void) pri;  // not used for file source

    if (mParseStatus == kMspStatus_Ok)
    {

        int cpeStatus;
        if (mCpeSrcHandle != 0)
        {
            cpeStatus = cpe_src_Close(mCpeSrcHandle);
            if (cpeStatus != kCpe_NoErr)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "cpe_src_close failed with error code %d", cpeStatus);
                return kMspStatus_Error;
            }
            else
                mCpeSrcHandle = 0;
        }
        cpeStatus = cpe_src_Open(eCpeSrcType_PlayFileSource, &mCpeSrcHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            // return an error
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "cpe_src_open failed with Error code %d\n", cpeStatus);
            return kMspStatus_BadSource;
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "MSPFileSource Open %s\n", mFileName.c_str());
            mCurrentSetFileName = mFileName;
            cpeStatus = cpe_src_Set(mCpeSrcHandle, eCpeSrcNames_URL, (void *)mFileName.c_str());
            if (cpeStatus != kCpe_NoErr)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "cpe_src_set failed with error code %d\n", cpeStatus);
                cpe_src_Close(mCpeSrcHandle);
                mCpeSrcHandle = 0;
                return kMspStatus_BadSource;
            }
            // Register RF Callback
            tCpeSrcPlayBkCallBkData typeSpecificData;
            typeSpecificData.type = eCpeSrcPlayBkCallbackTypes_EOF;
            cpeStatus = cpe_src_RegisterCallback(eCpeSrcCallbackTypes_PlayBkCallback, (void *)this, (tCpeSrcCallbackFunction)PlayBkCallbackFunction_EOF, &mSrcCbIdEOF, mCpeSrcHandle, (void *)&typeSpecificData);
            if (cpeStatus != kCpe_NoErr)
            {
                cpe_src_Close(mCpeSrcHandle);
                mCpeSrcHandle = 0;
                return kMspStatus_BadSource;
            }

            typeSpecificData.type = eCpeSrcPlayBkCallbackTypes_BOF;
            cpeStatus = cpe_src_RegisterCallback(eCpeSrcCallbackTypes_PlayBkCallback, (void *)this, (tCpeSrcCallbackFunction)PlayBkCallbackFunction_BOF, &mSrcCbIdBOF, mCpeSrcHandle, (void *)&typeSpecificData);

            if (cpeStatus != kCpe_NoErr)
            {
                /* Unregister earlier callback */
                cpe_src_UnregisterCallback(mCpeSrcHandle, mSrcCbIdEOF);
                mSrcCbIdEOF = 0;
                cpe_src_Close(mCpeSrcHandle);
                mCpeSrcHandle = 0;
                return kMspStatus_BadSource;
            }
            return kMspStatus_Ok;
        }
    }
    else
    {
        return mParseStatus;
    }
}

eMspStatus MSPFileSource::start()
{
    // Start the source
    FNLOG(DL_MSP_MPLAYER);
    if (mCpeSrcHandle != 0)
    {
        int status = kCpe_NoErr;
        if (!mStarted && !strcmp(mFileName.c_str(), mCurrentSetFileName.c_str()))
        {
            status = cpe_src_Start(mCpeSrcHandle);
        }
        if (status != kCpe_NoErr)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "cpe_src_start error %d", status);
            if (mSrcCbIdEOF)
            {
                status = cpe_src_UnregisterCallback(mCpeSrcHandle, mSrcCbIdEOF);
                mSrcCbIdEOF = 0;
            }
            if (mSrcCbIdBOF)
            {
                status = cpe_src_UnregisterCallback(mCpeSrcHandle, mSrcCbIdBOF);
                mSrcCbIdBOF = 0;
            }
            if (mCpeSrcHandle)
            {
                cpe_src_Close(mCpeSrcHandle);
                mCpeSrcHandle = 0;
            }
            return kMspStatus_BadSource;
        }
        mStarted = true;
        return kMspStatus_Ok;
    }
    else
    {
        return kMspStatus_StateError;
    }
}

eMspStatus MSPFileSource::stop()
{
    // Stop the source
    FNLOG(DL_MSP_MPLAYER);
    int cpeStatus;
    eMspStatus status = kMspStatus_Ok;

    if (mCpeSrcHandle != 0)
    {
        if (mSrcCbIdEOF)
        {
            cpe_src_UnregisterCallback(mCpeSrcHandle, mSrcCbIdEOF);
            mSrcCbIdEOF = 0;
        }
        if (mSrcCbIdBOF)
        {
            cpe_src_UnregisterCallback(mCpeSrcHandle, mSrcCbIdBOF);
            mSrcCbIdBOF = 0;
        }

        if (mStarted == true)
        {
            cpeStatus = cpe_src_Stop(mCpeSrcHandle);
            if (cpeStatus != kCpe_NoErr)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "cpe_src_stop failed with error code %d", cpeStatus);
            }
            mStarted = false;
        }
    }
    return status;
}

void MSPFileSource::PlayBkCallbackFunction_BOF(tCpeSrcCallbackTypes aType, void *aUserData, void *pCallbackSpecific)
{
    // Call Registered callback
    FNLOG(DL_MSP_MPLAYER);
    aType = aType;
    pCallbackSpecific = pCallbackSpecific;
    MSPFileSource *inst = (MSPFileSource *)aUserData;
    if (inst)
    {
        if (inst->mIsRewindMode && inst->mCurrentFileIndex > 1)
        {
            --(inst->mCurrentFileIndex);
            inst->setFileByIndex(inst->mCurrentFileIndex);
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "We are in Rewind Mode. Next File to play is %s\n", inst->mFileName.c_str());
            inst->mSrcStateCB(inst->mClientContext, kSrcNextFile);

        }
        else
        {
            if (inst->mSrcStateCB)
            {
                inst->mSrcStateCB(inst->mClientContext, kSrcBOF);
            }
        }
    }
}

void MSPFileSource::PlayBkCallbackFunction_EOF(tCpeSrcCallbackTypes aType, void *aUserData, void *pCallbackSpecific)
{
    // Call Registered callback
    FNLOG(DL_MSP_MPLAYER);
    aType = aType;
    pCallbackSpecific = pCallbackSpecific;
    MSPFileSource *inst = (MSPFileSource *)aUserData;
    if (inst)
    {
        if (inst->mSrcStateCB)
        {
            if (inst->mCurrentFileIndex < inst->mFileNameList.size())
            {
                dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Still more files to play\n");
                ++(inst->mCurrentFileIndex);
                inst->setFileByIndex(inst->mCurrentFileIndex);
                dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Next File to play is %s\n", inst->mFileName.c_str());
                inst->mSrcStateCB(inst->mClientContext, kSrcNextFile);
            }
            else
            {
                inst->mSrcStateCB(inst->mClientContext, kSrcEOF);
            }
        }
    }
}



tCpeSrcHandle MSPFileSource::getCpeSrcHandle()const
{
    return mCpeSrcHandle;
}

int MSPFileSource::getProgramNumber()const
{
    return mProgramNumber;
}

int MSPFileSource::getSourceId()const
{
    return mSourceId;
}

std::string MSPFileSource::getSourceUrl()const
{
    return mSrcUrl;
}

std::string MSPFileSource::getFileName()const
{
    return mFileName;
}

bool MSPFileSource::isDvrSource()const
{
    return true;
}

bool MSPFileSource::canRecord()const
{
    return false;
}

bool MSPFileSource::isAnalogSource()const
{
    return false;
}

eMspStatus MSPFileSource::setSpeed(tCpePlaySpeed aPlaySpeed, uint32_t aNpt)
{
    FNLOG(DL_MSP_MPLAYER);
    int cpeStatus;
    if (mCpeSrcHandle)
    {
        if (aPlaySpeed.mode == eCpePlaySpeedMode_Rewind)
        {
            dlog(DL_MSP_DVR, DLOGL_NOISE, "Currently in Rewind Mode\n");
            mIsRewindMode = true;
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_NOISE, "Not in Rewind Mode");
            mIsRewindMode = false;
        }
        cpeStatus = cpe_src_Set(mCpeSrcHandle, eCpeSrcNames_PlaySpeed, (void *)&aPlaySpeed);
        if (cpeStatus != kCpe_NoErr)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "cpe_src_set for play speed error code %d", cpeStatus);
            cpe_src_Close(mCpeSrcHandle);
            mCpeSrcHandle = 0;
            return kMspStatus_BadSource;
        }

        if (aNpt)
        {
            cpeStatus = cpe_src_Set(mCpeSrcHandle, eCpeSrcNames_CurrentNPT, (void *)&aNpt);
            if (cpeStatus != kCpe_NoErr)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "cpe_src_set for NPT error code %d", cpeStatus);
                return kMspStatus_BadSource;
            }
        }
        return kMspStatus_Ok;
    }
    else
    {
        return kMspStatus_Error;
    }
}

void MSPFileSource::buildFileList()
{
    int pos;
    std::string temp1, temp2;

    pos = mSrcUrl.find(",");
    if (pos == -1)  //regular single file recording
    {
        pos = mSrcUrl.find("sadvr://");
        if (pos == 0)
        {
            temp1 = mSrcUrl.substr(strlen("sadvr://"));
            temp1 = "avfs://" + temp1;
            LOG(DLOGL_REALLY_NOISY, "Single file is present %s", temp1.c_str());
            mFileNameList.push_back(temp1);
        }
        else
        {
            pos = mSrcUrl.find("avfs://");
            if (pos != 0)
            {
                mParseStatus = kMspStatus_BadParameters;
            }
            else
            {
                mFileNameList.push_back(mSrcUrl);
                LOG(DLOGL_REALLY_NOISY, "Single file with AVFS %s", mSrcUrl.c_str());
                mParseStatus = kMspStatus_Ok;
            }

        }
    }
    else  //comma seperated files
    {
        LOG(DLOGL_REALLY_NOISY, "We have comma separated file list\n");
        std::string url = mSrcUrl;
        while (pos != -1)
        {
            temp1 = url.substr((pos + 1)); //remaining string
            temp2 = url.substr(0, pos);
            temp2 = temp2.substr(strlen("sadvr://"));
            temp2 = "avfs://" + temp2;
            LOG(DLOGL_REALLY_NOISY, "File Pushed is %s", temp2.c_str());
            mFileNameList.push_back(temp2);
            url = temp1;
            pos = url.find(",");
        }
        url = url.substr(strlen("sadvr://"));
        url  = "avfs://" + url;
        mFileNameList.push_back(url);
        LOG(DLOGL_REALLY_NOISY, "url: %s", url.c_str());

    }

    mParseStatus = kMspStatus_Ok;

}

eMspStatus MSPFileSource::setFileByIndex(uint32_t aFileIndex)
{
    std::list<std::string>::iterator it;
    uint32_t i = 1;
    it = mFileNameList.begin();
    dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Inside Set File By Index %d %d\n", aFileIndex, mFileNameList.size());
    if (aFileIndex <= mFileNameList.size())
    {
        while (i < aFileIndex)
        {
            it++;
            i++;
        }
        mFileName = *it;
        return kMspStatus_Ok;
    }
    else
    {
        return kMspStatus_Error;
    }
}

eMspStatus MSPFileSource::getPosition(float *pNptTime)
{

    uint32_t npt;

    FNLOG(DL_MSP_DVR);

    if (pNptTime)
    {
        dlog(DL_MSP_DVR, DLOGL_NOISE, " GetPosition Current Index = %d\n",  mCurrentFileIndex);
        if ((cpe_src_Get(mCpeSrcHandle, eCpeSrcNames_CurrentNPT, (void *)&npt, sizeof(uint32_t))) == kCpe_NoErr)
        {
            dlog(DL_MSP_DVR, DLOGL_NOISE, "GetPosition is in File Source %d\n", npt);
            *(float *)pNptTime = (float)(npt / 1000);
            unsigned int i = 1;
            std::list<std::string>::iterator it;
            it = mFileNameList.begin();
            while (i < mCurrentFileIndex)
            {
                // Get Duration of ith file & then add in npt
                tCpeRecordedFileInfo recordingExtendedInfo;
                std::string filename = *it;
                filename = filename.substr(strlen("avfs://"));
                /*Read the attibutes from fuse FS*/
                int ret = getxattr(filename.c_str(), kCpeRec_ExtendedAttr, (void*)&recordingExtendedInfo, sizeof(tCpeRecordedFileInfo));
                if (ret == -1)
                {
                    dlog(DL_MSP_DVR, DLOGL_ERROR, "Error reading duration of Recording File : %s, retVal = %d", filename.c_str(), ret);
                    return kMspStatus_Error;
                }
                else
                {
                    dlog(DL_MSP_DVR, DLOGL_NOISE, "File Duration Length %d\n", recordingExtendedInfo.lengthInSeconds);
                    *(float *)pNptTime = *(float *)pNptTime + recordingExtendedInfo.lengthInSeconds;
                }
                ++i;
                it++;
            }
            dlog(DL_MSP_DVR, DLOGL_NOISE, " GetPosition in MSPFileSource pNptTime = %f  \n", *pNptTime);
            return  kMspStatus_Ok;
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "cpe_src_Get failed to get eCpeSrcNames_CurrentNPT");
            return kMspStatus_Error;
        }
    }
    else
    {
        return kMspStatus_Error;
    }

}

eMspStatus MSPFileSource::setPosition(float aNptTime)
{
    unsigned int i = 1;
    std::list<std::string>::iterator it;
    it = mFileNameList.begin();
    float duration = 0;
    uint32_t actualNpt = (uint32_t)aNptTime;
    dlog(DL_MSP_DVR, DLOGL_NOISE, "Set Position Npt time is %f", aNptTime);

    if (i != mFileNameList.size())
    {
        while (i <= mFileNameList.size())
        {
            // Get Duration of ith file & then add in npt
            tCpeRecordedFileInfo recordingExtendedInfo;
            std::string filename = *it;
            filename = filename.substr(strlen("avfs://"));
            /*Read the attibutes from fuse FS*/
            int ret = getxattr(filename.c_str(), kCpeRec_ExtendedAttr, (void*)&recordingExtendedInfo, sizeof(tCpeRecordedFileInfo));
            if (ret == -1)
            {
                dlog(DL_MSP_DVR, DLOGL_ERROR, "Error reading duration of Recording File : %s, retVal = %d", filename.c_str(), ret);
                return kMspStatus_Error;
            }
            else
            {
                dlog(DL_MSP_DVR, DLOGL_NOISE, "File Duration Length %d\n", recordingExtendedInfo.lengthInSeconds);
                duration = duration + recordingExtendedInfo.lengthInSeconds * 1000;
            }
            if (duration >= aNptTime)
            {
                break;
            }
            actualNpt = actualNpt - (recordingExtendedInfo.lengthInSeconds * 1000);
            ++i;
            it++;
        }

        if (i > mFileNameList.size())
        {
            i = mFileNameList.size();
        }
    }
    if (mCurrentFileIndex == i)
    {
        // Set Record Position
        dlog(DL_MSP_DVR, DLOGL_NOISE, "Actual Npt set is %d\n", actualNpt);
        int status = cpe_src_Set(mCpeSrcHandle, eCpeSrcNames_CurrentNPT, (void *)&actualNpt);
        if (status != kCpe_NoErr)
        {
            dlog(DL_MSP_DVR, DLOGL_ERROR, "cpe_src_Set failed to set eCpeSrcNames_CurrentNPT, Error = %d\n", status);
            return kMspStatus_Error;
        }
        else
        {
            dlog(DL_MSP_DVR, DLOGL_NOISE, " cpe_src_Set(...) success !!! %d\n", actualNpt);
            return kMspStatus_Ok;
        }
    }
    else
    {
        if (mSrcStateCB)
        {
            mCurrentFileIndex = i;
            setFileByIndex(mCurrentFileIndex);
            dlog(DL_MSP_DVR, DLOGL_REALLY_NOISY, "Next File to play is %s\n", mFileName.c_str());
            mSrcStateCB(mClientContext, kSrcNextFile);
        }
        return kMspStatus_Loading;
    }
}

bool MSPFileSource::isPPV(void)
{
    return false;
}

bool MSPFileSource::isQamrf(void)
{
    return false;
}



eMspStatus MSPFileSource::release()
{
    // release the source
    FNLOG(DL_MSP_MPLAYER);
    return kMspStatus_Ok;
}

tCpePgrmHandle MSPFileSource::getCpeProgHandle()const
{
    return 0;
}

void MSPFileSource::SetCpeStreamingSessionID(uint32_t sessionId)
{
    (void) sessionId;
    return;
}

