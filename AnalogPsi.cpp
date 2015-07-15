/**
   \file psi.cpp
   \class psi

Implementation file for psi class
*/


///////////////////////////////////////////////////////////////////////////
//                          Includes
///////////////////////////////////////////////////////////////////////////

#include "AnalogPsi.h"

#include <cpe_error.h>
#include <cpe_sectionfilter.h>
#include "cpe_common.h"
#if defined(DMALLOC)
#include "dmalloc.h"
#endif

#include <dlog.h>
#include "eventQueue.h"

///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

#define UNUSED_PARAM(a) (void)a;
#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"AnalogPsi:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

/**
 * This PSI start is performed when the video needs to be played from a TSB or file source so that the
 * media can be started with the necessary Meta Data containing PIDs for TSB.
 * @param recordUrl
 * @return
 */
eMspStatus AnalogPsi::psiStart(std::string recordUrl)
{
    tCpeRecDataBase *metabuf;
    unsigned int metasize;
    FILE *fp;
    std::string recfile;
    int fileOffset = 0;
    FNLOG(DL_MEDIAPLAYER);

    recfile = recordUrl.substr(strlen("avfs://"));

    //TODO: Remove the filename Hack,  get it from Source URl
    fp = fopen(recfile.c_str(), "rb");
    if (fp == NULL)
    {
        dlog(DL_MEDIAPLAYER, DLOGL_ERROR, "Metadata file opening error");
        return kMspStatus_Error;
    }


    // obtain file size:
    fseek(fp, 0, SEEK_END);
    fileOffset = ftell(fp);
    if (-1 == fileOffset)
    {
        dlog(DL_MEDIAPLAYER, DLOGL_ERROR, "File end not found");
        fclose(fp);
        return kMspStatus_Error;
    }
    else
    {
        metasize = fileOffset;
    }
    metabuf = (tCpeRecDataBase *)malloc(metasize + 1024); // TODO: check this
    mMetaBuf = (uint8_t *)malloc(metasize + 1024);
    if (metabuf == NULL)
    {
        dlog(DL_MEDIAPLAYER, DLOGL_ERROR, "%s:%d Getting PmtInfo from Stored file failed \n", __FUNCTION__, __LINE__);
        fclose(fp);
        return kMspStatus_OutofMemory;
    }

    rewind(fp);
    fread(metabuf, 1, metasize, fp);
    fclose(fp);

    pSource = kPlayFileSrc;

    dlog(DL_MEDIAPLAYER, DLOGL_REALLY_NOISY, "%s:%d DB section count is %d", __FUNCTION__, __LINE__, metabuf->dbHdr.dbCounts);
    for (int i = 0; i < metabuf->dbHdr.dbCounts; i++)
    {

    }
    mMetaSize = metasize;
    memcpy(mMetaBuf, metabuf, metasize);

    free(metabuf);

    return kMspStatus_Ok;
}

eMspStatus AnalogPsi::getComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset)
{
    LOG(DLOGL_NOISE, "info %p, infoSize %d, offset %d", info, infoSize, offset);
    if (info && count && mSrcHandle && mPids)
    {
        mPids->numOfPids = 3;
        int err = cpe_src_Get(mSrcHandle, eCpeSrcNames_PIDNumbers, (void*)mPids, (sizeof(tCpeSrcPFPidDef) + sizeof(tCpeSrcPFPidStruct) * 3));
        if (err)
        {
            LOG(DLOGL_ERROR, "cpe_src_Get() err %d", err);
            return kMspStatus_Error;
        }

        *count = 0;
        for (uint32_t i = offset; i < 3 && *count < infoSize; i++)
        {
            // tCpePgrmHandleEsData *esData = pmtInfo.ppEsData[i];
            // if (esData)
            {
                info[i - offset].pid = mPids->pids->pid;
                info[i - offset].streamType = mPids->pids->type;
                memcpy(info[i - offset].langCode, mPids->pids->langCode , ISO_639_CODE_LENGTH);
                LOG(DLOGL_NOISE, "i= %d, pid= %d, type= %d",
                    i, info[i - offset].pid, info[i - offset].streamType);

                /*   if (isAudioStreamType(info[i - offset].streamType))
                   {
                       for (int j = 0 ; j < esData->descCount; j++)
                       {
                           tCpePgrmHandleMpegDesc *ppEsDesc = esData->ppEsDesc[j];
                           if (ppEsDesc && ppEsDesc->tag == ISO_639_LANG_TAG)
                           {
                               memcpy(info[i-offset].langCode, ppEsDesc->data, ISO_639_CODE_LENGTH);
                               break;
                           }
                       }
                   } */
                *count = *count + 1;
            }
        }
        return kMspStatus_Ok;
    }
    else
    {
        LOG(DLOGL_ERROR, "mSrcHandle %p, mPids %p", mSrcHandle, mPids);
        return kMspStatus_Error;
    }

}

/**
 * This PSI is started when playing from a Live Source so the Media Manager can have the PIDs
 * supported by the decoder.
 * @param pgmNo
 * @param srcHandle
 * @return
 */
eMspStatus AnalogPsi::psiStart(const MSPSource *aSource)
{

    uint16_t i;
    eMspStatus status = kMspStatus_Ok;
    int err;

    FNLOG(DL_MSP_MPLAYER);
    if (aSource == NULL)
    {
        return kMspStatus_StateError;
    }

    mPgmNo    = aSource->getProgramNumber();
    mSrcHandle = aSource->getCpeSrcHandle();

    mPids->numOfPids = 3;
    err = cpe_src_Get(mSrcHandle, eCpeSrcNames_PIDNumbers, (void*)mPids, (sizeof(tCpeSrcPFPidDef) + sizeof(tCpeSrcPFPidStruct) * 3));
    if (err != kCpe_NoErr)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "RecordSession::%s:%d No void in call Rathna:Call cpe_src_Get(...) return error %d  ", __FUNCTION__, __LINE__, err);
        status = kMspStatus_CpeRecordError;
        return status;
    }
    dlog(DL_MSP_DVR, DLOGL_NORMAL, "%s:%d Rathna:cpe_src_Get(...) Success, %d pids", __FUNCTION__, __LINE__,  mPids->numOfPids);

    for (i = 0; i < mPids->numOfPids; i++)
    {
        dlog(DL_MSP_DVR, DLOGL_NORMAL, "%s:%d Pid %d, type %d", __FUNCTION__, __LINE__, mPids->pids[i].pid, mPids->pids[i].type);
    }
#if 0
    sParams = (tCpeMediaPidTable*) malloc(sizeof(tCpeMediaPidTable));

    for (i = 0; i < mPids->numOfPids; i++)
    {
        switch (mPids->pids[i].type)
        {
        case eCpeSrcPFPidType_VidMpeg2Pid:
            sParams->videoPid = mPids->pids[i].pid;
            sParams->videoStreamType = kCpeStreamType_MPEG2_Video;
            break;
        case eCpeSrcPFPidType_VidMPEG1Pid:
            sParams->videoPid = mPids->pids[i].pid;
            sParams->videoStreamType = kCpeStreamType_MPEG1_Video;
            break;
        case eCpeSrcPFPidType_VidH264Pid:
            sParams->videoPid = mPids->pids[i].pid;
            sParams->videoStreamType = kCpeStreamType_H264_Video;
            break;
        case eCpeSrcPFPidType_VidVC1Pid:
            sParams->videoPid = mPids->pids[i].pid;
            sParams->videoStreamType = kCpeStreamType_VC1_Video;
            break;
        case eCpeSrcPFPidType_AudMpegPid:
            sParams->audioPid = mPids->pids[i].pid;
            sParams->audioStreamType = kCpeStreamType_MPEG2_Audio;
            break;
        case eCpeSrcPFPidType_AudAACPid:
            sParams->audioPid = mPids->pids[i].pid;
            sParams->audioStreamType = kCpeStreamType_AAC_Audio;
            break;
        case eCpeSrcPFPidType_AudMP3Pid:
            sParams->audioPid = mPids->pids[i].pid;
            sParams->audioStreamType = kCpeStreamType_MPEG2_Audio;
            break;
        case eCpeSrcPFPidType_AudAC3Pid:
            sParams->audioPid = mPids->pids[i].pid;
            sParams->audioStreamType = kCpeStreamType_AAC_Audio;
            break;
        case eCpeSrcPFPidType_AudLPCMPid:
            sParams->audioPid = mPids->pids[i].pid;
            sParams->audioStreamType = kCpeStreamType_LPCM;
            break;
        case eCpeSrcPFPidType_OtherPid:
            if (mPids->pids[i].hasPCR == true)
            {
                sParams->clockPid = mPids->pids[i].pid;
            }
            break;
        default:
            break;
        }
    }

    sParams->amolPid = 0;
#endif
    return  status;

}


/** *********************************************************
*/
eMspStatus AnalogPsi::psiStop()
{
    eMspStatus status = kMspStatus_Ok;
    FNLOG(DL_MSP_MPLAYER);

    pSource = kPlayNoSrc;

    return  status;
}

/** *********************************************************
*/
uint16_t AnalogPsi::getProgramNo(void)
{
    FNLOG(DL_MSP_MPLAYER);
    return  mPgmNo;
}

/***********************************************************
*/
AnalogPsi::AnalogPsi()
{
    FNLOG(DL_MSP_MPLAYER);

    mPgmNo = 0;
    mVideoPid = 0;
    mAudioPid = 0;
    mClockPid = 0;
    mAmolPid = 0;
    mSrcHandle      = NULL;
    pSource = kPlayNoSrc;

    //mPids = NULL;
    //sParams = NULL;
    mMetaBuf = NULL;

    mPids = (tCpeSrcPFPidDef *)malloc(sizeof(tCpeSrcPFPidDef) + sizeof(tCpeSrcPFPidStruct) * 3);
    if (mPids == NULL)
    {
        dlog(DL_MSP_DVR, DLOGL_ERROR, "%s:%d Could not allocate mPids", __FUNCTION__, __LINE__);
    }
    mMetaSize = 0;
}

/** *********************************************************
*/
AnalogPsi::~AnalogPsi()
{
    FNLOG(DL_MSP_MPLAYER);

    if (mPids != NULL)
        free(mPids);

// if(  sParams != NULL)
//      free(sParams);

    if (mMetaBuf != NULL)
        free(mMetaBuf);

    mPids = NULL;
}
