/**
*  \file pmt.cpp
*  \class pmt

Implementation file for psi class
*/


///////////////////////////////////////////////////////////////////////////
//                          Includes
///////////////////////////////////////////////////////////////////////////

#include "psi.h"
#include "assert.h"

#include <cpe_error.h>
#include <cpe_sectionfilter.h>
#include "cpe_common.h"
#if defined(DMALLOC)
#include "dmalloc.h"
#endif

#include <dlog.h>
#include "eventQueue.h"

#include"pthread_named.h"
#include <vector>

///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

#define UNUSED_PARAM(a) (void)a;

#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_PSI, level,"Pmt:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#define HEREIAM  LOG(DLOGL_REALLY_NOISY, "HEREIAM")

#define PRINTD(msg, args...)  dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Pmt: " msg, ##args);

///////////////////////////////////////////////////////////////////////////
//                      Member functions implementation
///////////////////////////////////////////////////////////////////////////


/** *********************************************************
*/
void Pmt::printPmtInfo()
{
    FNLOG(DL_MSP_PSI);

    PRINTD(" -----------------PMT Structure Info------------------------- ");
    PRINTD("   Version No      :0x%x ", mPmtInfo.versionNumber);
    PRINTD("   Clock PID       :0x%x ", mPmtInfo.clockPid);

    // display contents of Program level descriptor array

    PRINTD("   Pgm Descs Count :%d",    mPmtInfo.pgmDescCount);
    for (int pgmDescIdx = 0; pgmDescIdx < mPmtInfo.pgmDescCount; pgmDescIdx++)
    {
        tCpePgrmHandleMpegDesc* pgmDesc = mPmtInfo.ppPgmDesc[pgmDescIdx];

        PRINTD("  +- Pgm Desc[%d]------", pgmDescIdx);
        PRINTD("  | Tag      :0x%02x ",   pgmDesc->tag);
        PRINTD("  | Data Len :0x%02x ",   pgmDesc->dataLen);
        PRINTD("  | Data     :%s ",       pgmDesc->data);
    }
    PRINTD("  +- Pgm Desc End------");

    // Display contents of Elementary stream loop
    PRINTD("   ES Data Count :%u", mPmtInfo.esCount);
    for (int esIdx = 0; esIdx < mPmtInfo.esCount; esIdx++)
    {
        tCpePgrmHandleEsData *esData = mPmtInfo.ppEsData[esIdx];

        PRINTD("  ...ES Data[%u]------",   esIdx + 1);
        PRINTD("  . Stream Type :0x%02x ", esData->streamType);
        PRINTD("  . PID         :0x%x ",   esData->pid);
        PRINTD("  . Reserved    :%s",      esData->reserved);

        // Display contents of descriptors for each elementary stream
        PRINTD("  .ES Desc Count :%u", esData->descCount);
        for (int esDescIdx = 0; esDescIdx < esData->descCount; esDescIdx++)
        {
            tCpePgrmHandleMpegDesc *esDesc = esData->ppEsDesc[esDescIdx];

            PRINTD("  .+- ES Desc[%u]------", esDescIdx + 1);
            PRINTD("  .| Tag      :0x%02x ",  esDesc->tag);
            PRINTD("  .| Data Len :0x%02x ",  esDesc->dataLen);
            PRINTD("  .| Data     :%s ",      esDesc->data);
        }
        PRINTD("  .+- ES Desc End------");
    }

    PRINTD("  ... ES Data End------");

    PRINTD(" ---------------END OF PMT Structure Info--------------------");
}


/** *********************************************************
 */
std::list<tPid> * Pmt::getVideoPidList(void)
{
    return &mVideoPid;
}

/** *********************************************************
 */
std::list<tPid> * Pmt::getAudioPidList(void)
{
    return  &mAudioPid;
}

/** *********************************************************
 */
eMspStatus Pmt::getDescriptor(tCpePgrmHandleMpegDesc *descriptor, uint16_t pid)
{
    int pgmdescount, escount, esdescount;
    FNLOG(DL_MSP_MPLAYER);
    eMspStatus status = kMspStatus_Error;

    if (descriptor == NULL)
    {
        LOG(DLOGL_ERROR, "Error: Null tCpePgrmHandleMpegDesc");
        return status;
    }

    if (pid == kPid)
    {
        for (pgmdescount = 0; pgmdescount < mPmtInfo.pgmDescCount; pgmdescount++)
        {
            if (mPmtInfo.ppPgmDesc[pgmdescount]->tag == descriptor->tag)
            {
                descriptor->dataLen = mPmtInfo.ppPgmDesc[pgmdescount]->dataLen;
                descriptor->data = (uint8_t *)malloc(descriptor->dataLen);
                memcpy(descriptor->data, mPmtInfo.ppPgmDesc[pgmdescount]->data, descriptor->dataLen);
                status = kMspStatus_Ok;
            }
        }
    }
    else
    {
        for (escount = 0; escount < mPmtInfo.esCount; escount++)
        {
            if (mPmtInfo.ppEsData[escount]->pid == pid)
            {
                for (esdescount = 0; esdescount < mPmtInfo.ppEsData[escount]->descCount; esdescount++)
                {
                    if (mPmtInfo.ppEsData[escount]->ppEsDesc[esdescount]->tag == descriptor->tag)
                    {
                        descriptor->dataLen = mPmtInfo.ppEsData[escount]->ppEsDesc[esdescount]->dataLen;
                        descriptor->data = (uint8_t *)malloc(descriptor->dataLen);
                        memcpy(descriptor->data, mPmtInfo.ppEsData[escount]->ppEsDesc[esdescount]->data, descriptor->dataLen);
                        status = kMspStatus_Ok;
                    }
                }
            }
        }
    }

    LOG(DLOGL_REALLY_NOISY, "pid: %d  status: %d",  pid, status);

    return status;
}


/** *********************************************************
 */
void Pmt::releaseDescriptor(tCpePgrmHandleMpegDesc *descriptor)
{
    FNLOG(DL_MSP_PSI);

    if (descriptor)
    {
        if (descriptor->data)
        {
            free(descriptor->data);
            descriptor->data = NULL;
        }
    }
}



/** *********************************************************
 */
uint16_t Pmt::getPcrpid()
{
    return mPmtInfo.clockPid;
}

/** *********************************************************
 */
Pmt::Pmt()
{
    memset(&mPmtInfo, 0, sizeof(mPmtInfo));
    memset(&mTsParams, 0, sizeof(mTsParams));
}

/** *********************************************************
 */
Pmt::~Pmt()
{
    freePmtInfo();
    mVideoPid.clear();
    mAudioPid.clear();

}

/** *********************************************************
 */
eMspStatus Pmt::getPmtInfo(tCpePgrmHandlePmt* pmtInfo)
{
    FNLOG(DL_MSP_MPLAYER);

    if (pmtInfo)
    {
        memset(pmtInfo, 0, sizeof(tCpePgrmHandlePmt));
        memcpy(pmtInfo, &mPmtInfo, sizeof(tCpePgrmHandlePmt));
        return  kMspStatus_Ok;
    }

    else
    {
        LOG(DLOGL_ERROR, "Error: passes null pmtInfo");
        return kMspStatus_BadParameters;
    }
}

/** *********************************************************
 */
uint32_t Pmt::getProgramNumber(void)
{
    return mTsParams.progNumber;
}

/** *********************************************************
 */
uint32_t Pmt::getTransportID(void)
{
    return mTsParams.transportID;
}


/** *********************************************************
 */
void Pmt::freePmtInfo()
{
    int i, j;
    tCpePgrmHandleMpegDesc  *pDescInfo;
    tCpePgrmHandleEsData    *pEsInfo;
    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Freeing PMT Info");

    if (mPmtInfo.ppPgmDesc)
    {
        for (i = 0; i < mPmtInfo.pgmDescCount; i++)
        {
            pDescInfo = mPmtInfo.ppPgmDesc[i];
            if (pDescInfo)
            {
                if (pDescInfo->data)
                {
                    free(pDescInfo->data);
                    pDescInfo->data = NULL;
                }
                free(pDescInfo);
                mPmtInfo.ppPgmDesc[i] = NULL;
            }
        }
        free(mPmtInfo.ppPgmDesc);
        mPmtInfo.ppPgmDesc = NULL;
    }
    else
    {
        LOG(DLOGL_NOISE, "PMT program descriptor is NULL");
    }

    if (mPmtInfo.ppEsData)
    {
        for (i = 0; i < mPmtInfo.esCount; i++)
        {
            pEsInfo = mPmtInfo.ppEsData[i];
            if (pEsInfo)
            {
                if (pEsInfo->ppEsDesc)
                {
                    for (j = 0; j < pEsInfo->descCount; j++)
                    {
                        pDescInfo = pEsInfo->ppEsDesc[j];
                        if (pDescInfo)
                        {
                            if (pDescInfo->data)
                            {
                                free(pDescInfo->data);
                                pDescInfo->data = NULL;
                            }
                            free(pDescInfo);
                            pEsInfo->ppEsDesc[j] = NULL;
                        }
                    }
                    free(pEsInfo->ppEsDesc);
                    pEsInfo->ppEsDesc = NULL;
                }
                else
                {
                    LOG(DLOGL_NOISE, " PMT ES descriptor is NULL for the PMT es element no %d", i);
                }
                free(pEsInfo);
                mPmtInfo.ppEsData[i] = NULL;
            }
            else
            {
                LOG(DLOGL_NOISE, "PMT EsInfo is NULL for the PMT es element no %d", i);
            }
        }
        free(mPmtInfo.ppEsData);
        mPmtInfo.ppEsData = NULL;
    }
    else
    {
        LOG(DLOGL_NOISE, " PMT ES descriptor pointer is NULL");
    }

    mPmtInfo.pgmDescCount = 0;
    mPmtInfo.esCount = 0;
}


const uint32_t MSP_PMT_INVALID_STREAM_TYPE  = 0xfff;    // invalid value
void Pmt::createAudioVideoListsFromPmtInfo()
{
    LOG(DLOGL_REALLY_NOISY, "mPmtInfo.esCount: %d", mPmtInfo.esCount);

    // the below loop is same an for populateFromMspMetaData
    for (int escount = 0; escount <  mPmtInfo.esCount; escount++)
    {
        tPid avpid;

        LOG(DLOGL_REALLY_NOISY, "[%d]  streamType: 0x%x", escount, mPmtInfo.ppEsData[escount]->streamType);

        switch (mPmtInfo.ppEsData[escount]->streamType)
        {
        case kCpeStreamType_MPEG1_Video:
        case kCpeStreamType_MPEG2_Video:
        case kCpeStreamType_H264_Video:
        case kCpeStreamType_GI_Video:
        case kCpeStreamType_VC1_Video:

            avpid.pid        = mPmtInfo.ppEsData[escount]->pid;
            avpid.streamType =  mPmtInfo.ppEsData[escount]->streamType;
            mVideoPid.push_back(avpid);
            break;

        case kCpeStreamType_MPEG1_Audio:
        case kCpeStreamType_MPEG2_Audio:
        case kCpeStreamType_AAC_Audio:
        case kCpeStreamType_AACplus_Audio:
        case kCpeStreamType_DDPlus_Audio:
        case kCpeStreamType_GI_Audio:

            avpid.pid = mPmtInfo.ppEsData[escount]->pid;
            avpid.streamType =  mPmtInfo.ppEsData[escount]->streamType;
            mAudioPid.push_back(avpid);
            break;

        default:
            LOG(DLOGL_ERROR, "Error: unknown streamType: 0x%x", mPmtInfo.ppEsData[escount]->streamType);
        }
    }
}

eMspStatus Pmt::populateCaptionServiceMetadata(uint8_t *buffer, uint32_t size)
{
    eMspStatus status = kMspStatus_Ok;

    FNLOG(DL_MSP_PSI);

    LOG(DLOGL_NORMAL, "Buffer %p, Input size %d for CSD; Current es count %d in PMT", buffer, size, mPmtInfo.esCount);
    if (buffer && size)
    {
        int i = 0;
        for (i = 0; i < mPmtInfo.esCount; i++)
        {
            if (mPmtInfo.ppEsData[i]->streamType == kCpeStreamType_GI_Video || mPmtInfo.ppEsData[i]->streamType == kCpeStreamType_VC1_Video
                    || mPmtInfo.ppEsData[i]->streamType == kCpeStreamType_H264_Video)
            {
                LOG(DLOGL_NORMAL, "Found video pid 0x%x, will add CSD for it, size %d", mPmtInfo.ppEsData[i]->pid, size);

                // add one elementary stream for language
                mPmtInfo.ppEsData[i]->descCount = 1;
                mPmtInfo.ppEsData[i]->ppEsDesc = (tCpePgrmHandleMpegDesc **)malloc(sizeof(tCpePgrmHandleMpegDesc *));

                //  esData->descCount = 1;
                //   esData->ppEsDesc = (tCpePgrmHandleMpegDesc **) malloc(sizeof(tCpePgrmHandleMpegDesc *));

                // Create mpeg descriptor for the audio language
                tCpePgrmHandleMpegDesc *mpegDesc = (tCpePgrmHandleMpegDesc *)malloc(sizeof(tCpePgrmHandleMpegDesc));
                if (mpegDesc == NULL)
                {
                    LOG(DLOGL_ERROR, "Error: Null mpegDesc");
                    return kMspStatus_Error;
                }

                mPmtInfo.ppEsData[i]->ppEsDesc[0] = mpegDesc;


                mpegDesc->tag     =  0x86;
                mpegDesc->dataLen =  size;
                mpegDesc->data    = (uint8_t *)malloc(size);
                if (mpegDesc->data == NULL)
                {
                    LOG(DLOGL_ERROR, "Error: Null mpegDesc");
                    return kMspStatus_Error;
                }
                memcpy(mpegDesc->data, buffer, size);

                break;
            }
        }
    }
    else
    {
        status = kMspStatus_BadParameters;
        LOG(DLOGL_ERROR, "Invalid parameter !");
    }
    return status;
}


/** *********************************************************
 */
eMspStatus Pmt::populateFromSaraMetaData(uint8_t *buffer, uint32_t size)
{
    FNLOG(DL_MSP_PSI);

    LOG(DLOGL_REALLY_NOISY, "buffer: %p  size: %d", buffer, size);

    if (!buffer || !size)
    {
        LOG(DLOGL_ERROR, "Error: zero size  buffer: %p  size: %d", buffer, size);
        return kMspStatus_BadParameters;
    }

    tCpeRecDataBasePidTable *pt = (tCpeRecDataBasePidTable*)buffer;
    if (pt == NULL)
    {
        LOG(DLOGL_ERROR, "Error: Null tCpeRecDataBasePidTable");
        return kMspStatus_BadParameters;
    }
    uint32_t numPID = pt->numPID;
    uint32_t useAC3 = pt->useAC3;

    LOG(DLOGL_NOISE, "numPID: %d, useAC3 0x%x", numPID, useAC3);

    // This info is provided by SARA metadata but not used in RTN
    // bool useAC3 = (pt->attributes & kAVF_AudioFormat) ? pt->useAC3 : true;
    // LOG(DLOGL_NOISE,"useAC3: %d", useAC3);

    for (uint32_t index = 0; index < numPID; index++)
    {
        uint32_t pid   = pt->pids[index].pid;
        tCpeRecDataBasePidType type  = (tCpeRecDataBasePidType) pt->pids[index].type;
        uint32_t langCode  = pt->pids[index].langCode;

        LOG(DLOGL_NOISE, "SARA pid[%d] pid:  0x%x", index, pid);
        LOG(DLOGL_NOISE, "SARA pid[%d] type: 0x%x (dec: %d)", index, type, type);
        LOG(DLOGL_NOISE, "SARA pid[%d] langCode: 0x%x", index, langCode);
    }

    // ** Populate  mPmtInfo
    //    Not populating mPmtInfo.versionNumber - zero by default
    //    Not populating mPmtInfo.pgmDescCount - zero by default
    //    Not allocating any ppPgmDesc - zero by default

    std::vector<tCpePgrmHandleEsData *> esDataList;
    int j = 0; // index of audioPid
    for (uint32_t saraPidIndex = 0; saraPidIndex < numPID; saraPidIndex++)
    {
        tCpeRecDataBasePidType tvpStreamType  = (tCpeRecDataBasePidType) pt->pids[saraPidIndex].type;
        LOG(DLOGL_NOISE, "saraPidIndex: %d, type 0x%x, j= %d", saraPidIndex, tvpStreamType, j);

        if (pt->pids[saraPidIndex].pid == 0)
        {
            LOG(DLOGL_NOISE, "PAT Pid.Ignoring it, as its of no significance for middleware");
            continue;
        }

        if (pt->pids[saraPidIndex].type == eCpeRecDataBasePidType_PMT)
        {
            LOG(DLOGL_NOISE, "PMT Pid.Ignoring it, as its of no significance for middleware");
        }


        if (tvpStreamType == eCpeRecDataBasePidType_Pcr)
        {
            mPmtInfo.clockPid = pt->pids[saraPidIndex].pid;
            LOG(DLOGL_NOISE, "mPmtInfo.clockPid: %d", mPmtInfo.clockPid);
        }
        else
        {
            uint32_t cpeStreamType = MSP_PMT_INVALID_STREAM_TYPE;   //= getCpeStreamType(tvpStreamType);
            switch (tvpStreamType)
            {
            case eCpeRecDataBasePidType_Video:   // 0x3e8
                cpeStreamType = kCpeStreamType_GI_Video;  // 0x80
                break;

            case eCpeRecDataBasePidType_VideoH264:
                cpeStreamType = kCpeStreamType_H264_Video;
                break;

            case eCpeRecDataBasePidType_VideoVC1:
                cpeStreamType = kCpeStreamType_VC1_Video;
                break;

            case eCpeRecDataBasePidType_AudioDDplus:
                cpeStreamType = kCpeStreamType_DDPlus_Audio;
                j++;
                break;

            case eCpeRecDataBasePidType_Audio:   // 0x3e9
                if (useAC3 & (1 << j))
                {
                    cpeStreamType = kCpeStreamType_GI_Audio;
                }
                else
                    cpeStreamType = kCpeStreamType_MPEG2_Audio; // 0x4
                j++;
                break;

            case eCpeRecDataBasePidType_AudioAAC:
                cpeStreamType = kCpeStreamType_AAC_Audio;
                j++;
                break;

            case eCpeRecDataBasePidType_AudioAACplus:
                cpeStreamType = kCpeStreamType_AACplus_Audio;
                j++;
                break;

            case eCpeRecDataBasePidType_AudioLPCM:
                cpeStreamType = kCpeStreamType_LPCM;
                j++;
                break;

            default:
                LOG(DLOGL_ERROR, "Error: unknown tvpStreamType: 0x%x", tvpStreamType);
                break;
            }

            LOG(DLOGL_NOISE, "cpeStreamType: 0x%x", cpeStreamType);

            if (cpeStreamType != MSP_PMT_INVALID_STREAM_TYPE)
            {
                // calloc ensures reserved bytes are zeroed out and other values are zero
                tCpePgrmHandleEsData *esData = (tCpePgrmHandleEsData *)calloc(1, sizeof(tCpePgrmHandleEsData));
                if (esData == NULL)
                {
                    LOG(DLOGL_ERROR, "Error: Null tCpePgrmHandleEsData");
                    return kMspStatus_Error;
                }

                esDataList.push_back(esData);

                esData->streamType = cpeStreamType;
                esData->pid  = pt->pids[saraPidIndex].pid;

                if ((cpeStreamType == kCpeStreamType_GI_Audio) || (cpeStreamType == kCpeStreamType_DDPlus_Audio) ||
                        (cpeStreamType == kCpeStreamType_MPEG2_Audio) ||  cpeStreamType == kCpeStreamType_AAC_Audio ||
                        cpeStreamType == kCpeStreamType_AACplus_Audio || cpeStreamType == kCpeStreamType_LPCM)

                {
                    // add one elementary stream for language
                    esData->descCount = 1;
                    esData->ppEsDesc = (tCpePgrmHandleMpegDesc **) malloc(sizeof(tCpePgrmHandleMpegDesc *));

                    // Create mpeg descriptor for the audio language
                    tCpePgrmHandleMpegDesc *mpegDesc = (tCpePgrmHandleMpegDesc *)malloc(sizeof(tCpePgrmHandleMpegDesc));
                    if (mpegDesc == NULL)
                    {
                        LOG(DLOGL_ERROR, "Error: Null mpegDesc");
                        return kMspStatus_Error;
                    }

                    esData->ppEsDesc[0] = mpegDesc;

                    const int AUDIO_LANG_BYTE_SIZE  = 4;    // size of lang code (three chars plus null byte)
                    const int AUDIO_LANG_TAG        = 0x0a; // from inspecting RTN metadata

                    mpegDesc->tag     =  AUDIO_LANG_TAG;
                    mpegDesc->dataLen =  AUDIO_LANG_BYTE_SIZE;
                    mpegDesc->data    = (uint8_t *)malloc(AUDIO_LANG_BYTE_SIZE);
                    if (mpegDesc->data == NULL)
                    {
                        LOG(DLOGL_EMERGENCY, "Error: Null mpegDesc->data");
                        return kMspStatus_Error;
                    }

                    uint8_t *langBuffer = (uint8_t *)&pt->pids[saraPidIndex].langCode;

                    // the following manipulation to SARA data must be done for languages to work

                    mpegDesc->data[0] = langBuffer[0];
                    mpegDesc->data[1] = langBuffer[1];
                    mpegDesc->data[2] = langBuffer[2];
                    mpegDesc->data[3] = 0;

                    for (int k = 0; k < AUDIO_LANG_BYTE_SIZE ; k++)
                    {
                        LOG(DLOGL_REALLY_NOISY, "audio mpegDesc->data[%d]: 0x%x %c", k, mpegDesc->data[k],  mpegDesc->data[k]);
                    }
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "Erro: Unhandled SARA cpeStreamType: 0x%x", cpeStreamType);
            }
        }
    }

    mPmtInfo.esCount = esDataList.size();

    LOG(DLOGL_NOISE, "mPmtInfo.esCount: %d", mPmtInfo.esCount);

    //allocating memory for esdesc ptrs
    mPmtInfo.ppEsData = (tCpePgrmHandleEsData **)calloc(1, (mPmtInfo.esCount * sizeof(tCpePgrmHandleEsData *)));

    for (int i = 0; i < mPmtInfo.esCount; i++)
    {
        mPmtInfo.ppEsData[i] = esDataList[i];
        LOG(DLOGL_NOISE, "mPmtInfo.ppEsData[%d]: %p", i, mPmtInfo.ppEsData[i]);
    }

    createAudioVideoListsFromPmtInfo();

    printPmtInfo();

    return kMspStatus_Ok;
}






/** *********************************************************
 */
eMspStatus Pmt::populateMSPMetaData(uint8_t *buffer, uint32_t size)
{
    FNLOG(DL_MSP_PSI);

    int i, j;

    UNUSED_PARAM(size);

    uint8_t *metaptr = buffer;

    //allocating and copying the base mpmtinfo structure
    unsigned int data_size = sizeof(tCpePgrmHandlePmt);
    memcpy(&mPmtInfo, metaptr, data_size);

    metaptr += data_size;


    PRINTD(" -----------------PMT Structure Info------------------------- ");
    PRINTD("   Version No      :0x%x ", mPmtInfo.versionNumber);
    PRINTD("   Clock PID       :0x%x ", mPmtInfo.clockPid);
    PRINTD("   Pgm Descs Count :%d",    mPmtInfo.pgmDescCount);


    //allocating and copying program descriptors
    data_size = (sizeof(tCpePgrmHandleMpegDesc));   //copying program descriptors
    mPmtInfo.ppPgmDesc = (tCpePgrmHandleMpegDesc **)malloc(mPmtInfo.pgmDescCount * sizeof(tCpePgrmHandleMpegDesc *)); //allocating memory for pgmdesc ptrs memory
    for (i = 0; i < mPmtInfo.pgmDescCount; i++)
    {
        mPmtInfo.ppPgmDesc[i] = (tCpePgrmHandleMpegDesc *)malloc(data_size);
        memcpy((void *)mPmtInfo.ppPgmDesc[i], (void *)metaptr, data_size);
        metaptr += data_size;
        mPmtInfo.ppPgmDesc[i]->data = (uint8_t *)malloc(mPmtInfo.ppPgmDesc[i]->dataLen);
        memcpy((void *)mPmtInfo.ppPgmDesc[i]->data, (void *)metaptr, mPmtInfo.ppPgmDesc[i]->dataLen); //copying program descriptor's data
        metaptr += mPmtInfo.ppPgmDesc[i]->dataLen;


        PRINTD("  +- Pgm Desc[%u]------", i + 1);
        PRINTD("  | Tag      :0x%02x ", mPmtInfo.ppPgmDesc[i]->tag);
        PRINTD("  | Data Len :0x%02x ", mPmtInfo.ppPgmDesc[i]->dataLen);
    }

    data_size = sizeof(tCpePgrmHandleEsData);
    mPmtInfo.ppEsData = (tCpePgrmHandleEsData **)malloc(mPmtInfo.esCount * sizeof(tCpePgrmHandleEsData *)); //allocating memory for esdesc ptrs

    for (i = 0; i < mPmtInfo.esCount; i++)
    {
        mPmtInfo.ppEsData[i] = (tCpePgrmHandleEsData *)malloc(data_size);
        memcpy((void *)mPmtInfo.ppEsData[i], (void *)metaptr, data_size);    //copying ES data


        PRINTD("  ...ES Data[%u]------", i + 1);
        PRINTD("  . Stream Type   :0x%02x ", mPmtInfo.ppEsData[i]->streamType);
        PRINTD("  . PID           :0x%xu ", mPmtInfo.ppEsData[i]->pid);
        PRINTD("  . Reserved      :%s", mPmtInfo.ppEsData[i]->reserved);
        PRINTD("  . ES Desc Count :%u", mPmtInfo.ppEsData[i]->descCount);

        metaptr += data_size;
        if (mPmtInfo.ppEsData[i]->ppEsDesc != NULL)
        {
            mPmtInfo.ppEsData[i]->ppEsDesc = (tCpePgrmHandleMpegDesc **)malloc(mPmtInfo.ppEsData[i]->descCount * sizeof(tCpePgrmHandleMpegDesc *));
            for (j = 0; j < mPmtInfo.ppEsData[i]->descCount; j++)
            {
                mPmtInfo.ppEsData[i]->ppEsDesc[j] = (tCpePgrmHandleMpegDesc *)malloc(sizeof(tCpePgrmHandleMpegDesc));
                memcpy((void *)mPmtInfo.ppEsData[i]->ppEsDesc[j], (void *)metaptr, sizeof(tCpePgrmHandleMpegDesc)); //copying ES descriptor
                metaptr += sizeof(tCpePgrmHandleMpegDesc);

                mPmtInfo.ppEsData[i]->ppEsDesc[j]->data = (uint8_t *)malloc(mPmtInfo.ppEsData[i]->ppEsDesc[j]->dataLen);
                memcpy((void *)mPmtInfo.ppEsData[i]->ppEsDesc[j]->data, (void *)metaptr, mPmtInfo.ppEsData[i]->ppEsDesc[j]->dataLen); //copying ES descriptor data;
                metaptr += mPmtInfo.ppEsData[i]->ppEsDesc[j]->dataLen;
            }
        }
    }

    createAudioVideoListsFromPmtInfo();

    dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Buffer data reading MSP metadata %p %p %d", metaptr, buffer, size);
    if ((unsigned int)(metaptr - buffer) >  size)
    {
        dlog(DL_MSP_PSI, DLOGL_ERROR, "Buffer overrun reading MSP metadata %p %p %d", metaptr, buffer, size);
    }

    printPmtInfo();

    return kMspStatus_Ok;
}

