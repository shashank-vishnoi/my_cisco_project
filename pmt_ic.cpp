/**
*  \file pmt_ic.cpp
*  \class pmt

Implementation file for psi class
*/


///////////////////////////////////////////////////////////////////////////
//                          Includes
///////////////////////////////////////////////////////////////////////////

#include "psi_ic.h"
#include "assert.h"

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

///////////////////////////////////////////////////////////////////////////
//                      Member functions implementation
///////////////////////////////////////////////////////////////////////////

/*********************************************************************************
* Prints the PMT Structure information - An utility function for debugging purpose
*/
void Pmt::printPmtInfo()
{
    FNLOG(DL_MSP_PSI);

    LOG(DLOGL_REALLY_NOISY, " -----------------PMT Structure Info------------------------- ");
    LOG(DLOGL_REALLY_NOISY, "   Version No      :0x%x ", mPmtInfo.versionNumber);
    LOG(DLOGL_REALLY_NOISY, "   Clock PID       :0x%x ", mPmtInfo.clockPid);

    // display contents of Program level descriptor array

    LOG(DLOGL_REALLY_NOISY, "   Pgm Descs Count :%d",    mPmtInfo.pgmDescCount);
    for (int pgmDescIdx = 0; pgmDescIdx < mPmtInfo.pgmDescCount; pgmDescIdx++)
    {
        tMpegDesc* pgmDesc = mPmtInfo.ppPgmDesc[pgmDescIdx];

        LOG(DLOGL_REALLY_NOISY, "  +- Pgm Desc[%d]------", pgmDescIdx);
        LOG(DLOGL_REALLY_NOISY, "  | Tag      :0x%02x ",   pgmDesc->tag);
        LOG(DLOGL_REALLY_NOISY, "  | Data Len :0x%02x ",   pgmDesc->dataLen);
        LOG(DLOGL_REALLY_NOISY, "  | Data     :%s ",       pgmDesc->data);
    }
    LOG(DLOGL_REALLY_NOISY, "  +- Pgm Desc End------");

    // Display contents of Elementary stream loop
    LOG(DLOGL_REALLY_NOISY, "   ES Data Count :%u", mPmtInfo.esCount);
    for (int esIdx = 0; esIdx < mPmtInfo.esCount; esIdx++)
    {
        tEsData *esData = mPmtInfo.ppEsData[esIdx];

        LOG(DLOGL_REALLY_NOISY, "  ...ES Data[%u]------",   esIdx + 1);
        LOG(DLOGL_REALLY_NOISY, "  . Stream Type :0x%02x ", esData->streamType);
        LOG(DLOGL_REALLY_NOISY, "  . PID         :0x%x ",   esData->pid);
        LOG(DLOGL_REALLY_NOISY, "  . Reserved    :%s",      esData->reserved);

        // Display contents of descriptors for each elementary stream
        LOG(DLOGL_REALLY_NOISY, "  .ES Desc Count :%u", esData->descCount);
        for (int esDescIdx = 0; esDescIdx < esData->descCount; esDescIdx++)
        {
            tMpegDesc *esDesc = esData->ppEsDesc[esDescIdx];

            LOG(DLOGL_REALLY_NOISY, "  .+- ES Desc[%u]------", esDescIdx + 1);
            LOG(DLOGL_REALLY_NOISY, "  .| Tag      :0x%02x ",  esDesc->tag);
            LOG(DLOGL_REALLY_NOISY, "  .| Data Len :0x%02x ",  esDesc->dataLen);
            LOG(DLOGL_REALLY_NOISY, "  .| Data     :%s ",      esDesc->data);
        }
        LOG(DLOGL_REALLY_NOISY, "  .+- ES Desc End------");
    }

    LOG(DLOGL_REALLY_NOISY, "  ... ES Data End------");

    LOG(DLOGL_REALLY_NOISY, " ---------------END OF PMT Structure Info--------------------");

    LOG(DLOGL_REALLY_NOISY, " ---------------BEGIN of VIDEO and AUDIO stream Info--------------------");

    std::list<tPid>::iterator iter1;

    for (iter1 = mVideoPid.begin(); iter1 != mVideoPid.end(); iter1++)
    {
        LOG(DLOGL_REALLY_NOISY, "VIDEO > Pid is %d, stream type is %d", (*iter1).pid, (*iter1).streamType);
    }

    for (iter1 = mAudioPid.begin(); iter1 != mAudioPid.end(); iter1++)
    {
        LOG(DLOGL_REALLY_NOISY, "AUDIO > Pid is %d, stream type is %d", (*iter1).pid, (*iter1).streamType);
    }

    LOG(DLOGL_REALLY_NOISY, " ---------------END of VIDEO and AUDIO stream Info--------------------");

}


/******************************************************************************
 * Returns the LIST of video pids
 */
std::list<tPid> * Pmt::getVideoPidList(void)
{
    return &mVideoPid;
}

/******************************************************************************
 * Returns the LIST of audio pids
 */
std::list<tPid> * Pmt::getAudioPidList(void)
{
    return  &mAudioPid;
}

/******************************************************************************
 * Returns the descriptor array <array of elementary stream descriptors> for a
 * given PID.
 *
 * Note - For getting the program level descriptors caller has to pass the PID
 * number as 0x1FFF.
 *
 * Note - It's the responsibility of the caller to free the memory allocated
 * here for the descriptors.
 */
eMspStatus Pmt::getDescriptor(tMpegDesc *descriptor, uint16_t pid)
{
    int pgmdescount, escount, esdescount;
    FNLOG(DL_MSP_MPLAYER);
    eMspStatus status = kMspStatus_Error;

    if (descriptor == NULL)
    {
        LOG(DLOGL_ERROR, "Error: Null tMpegDesc");
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


/******************************************************************************
 * Utility function to free the memory allocated to the descriptors using the
 * function getDescriptor
 */
void Pmt::releaseDescriptor(tMpegDesc *descriptor)
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

/******************************************************************************
 * Returns the PMT / Clock PID of the current stream
 */
uint16_t Pmt::getPcrpid()
{
    return mPmtInfo.clockPid;
}

/******************************************************************************
 * Constructor
 */
Pmt::Pmt()
{
    memset(&mPmtInfo, 0, sizeof(mPmtInfo));
    mVideoPid.clear();
    mAudioPid.clear();
}

/******************************************************************************
 * Destructor
 */
Pmt::~Pmt()
{
    freePmtInfo();
    mVideoPid.clear();
    mAudioPid.clear();
}

/******************************************************************************
 * Returns the PMT structure information to the caller
 */
eMspStatus Pmt::getPmtInfo(tPmt* pmtInfo)
{
    FNLOG(DL_MSP_MPLAYER);

    if (pmtInfo)
    {
        memset(pmtInfo, 0, sizeof(tPmt));
        memcpy(pmtInfo, &mPmtInfo, sizeof(tPmt));
        return  kMspStatus_Ok;
    }

    else
    {
        LOG(DLOGL_ERROR, "Error: passes null pmtInfo");
        return kMspStatus_BadParameters;
    }
}

/******************************************************************************
 * Frees the memory allocated to the PMT structure information
 * > It frees the memory allocated to both the program level descriptors and
 * > elementary stream level descriptors.
 */
void Pmt::freePmtInfo()
{
    int i, j;
    tMpegDesc  *pDescInfo;
    tEsData    *pEsInfo;
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

/******************************************************************************
 * Populates the Audio PID list and Video PID list for the elementary stream
 * information from the PMT structure information.
 */
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
        case kElementaryStreamType_MPEG1_Video:
        case kElementaryStreamType_MPEG2_Video:
        case kElementaryStreamType_H264_Video:
        case kElementaryStreamType_GI_Video:
        case kElementaryStreamType_VC1_Video:
            avpid.pid        = mPmtInfo.ppEsData[escount]->pid;
            avpid.streamType =  mPmtInfo.ppEsData[escount]->streamType;
            mVideoPid.push_back(avpid);
            break;

        case kElementaryStreamType_MPEG1_Audio:
        case kElementaryStreamType_MPEG2_Audio:
        case kElementaryStreamType_AAC_Audio:
        case kElementaryStreamType_AACplus_Audio:
        case kElementaryStreamType_DDPlus_Audio:
        case kElementaryStreamType_GI_Audio:
            avpid.pid = mPmtInfo.ppEsData[escount]->pid;
            avpid.streamType =  mPmtInfo.ppEsData[escount]->streamType;
            mAudioPid.push_back(avpid);
            break;

        default:
            LOG(DLOGL_ERROR, "Error: unknown streamType: 0x%x", mPmtInfo.ppEsData[escount]->streamType);
        }
    }
}

