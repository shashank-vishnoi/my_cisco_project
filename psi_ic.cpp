/**
   \file psi_ic.cpp
   \class psi

Implementation file for psi class
*/


///////////////////////////////////////////////////////////////////////////
//                          Includes
///////////////////////////////////////////////////////////////////////////

#include "psi_ic.h"

#if defined(DMALLOC)
#include "dmalloc.h"
#endif

#include <dlog.h>
#include "crc32.h"

#define UNUSED_PARAM(a) (void)a;

#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_PSI, level,"Psi:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

///////////////////////////////////////////////////////////////////////////////
//                      Member functions implementation
///////////////////////////////////////////////////////////////////////////////

/******************************************************************************
 * constructor
 */
Psi::Psi()
{
    FNLOG(DL_MSP_PSI);

    mPmt = new Pmt();

    pthread_mutex_init(&mPsiMutex, NULL);
    dlog(DL_MSP_PSI, DLOGL_NOISE, "Inited mutx %p", &mPsiMutex);

}


/******************************************************************************
 * Destructor
 */
Psi::~Psi()
{
    FNLOG(DL_MSP_PSI);
    freePsi();
}

/******************************************************************************
 * Clean up of PSI object held data structures during PSI object destruction
 */
void Psi::freePsi()
{
    if (mPmt)
    {
        delete mPmt;
        mPmt = NULL;
    }
}

/******************************************************************************
 * Start PSI starts populating the SAIL PMT structure information from the RAW
 * PMT queried from the source.
 *
 * Note - Source is expected to receive the RAW PMT from SDK as a payload from
 * the event NOTIFY_PMT_READY (refer to MSPHTTPSource_ic.cpp).
 */
eMspStatus Psi::psiStart(MSPSource *aSource)
{
    eMspStatus status = kMspStatus_Ok;

    FNLOG(DL_MSP_PSI);
    if (mPmt == NULL)
    {
        LOG(DLOGL_ERROR, "Error: null mPmt");
        return kMspStatus_Error;
    }

    lockMutex();

    // Clear the previous PMT MetaData if any, since we are calling psiStart more than once in same session
    mPmt->freePmtInfo();
    mPmt->mVideoPid.clear();
    mPmt->mAudioPid.clear();

    if (!aSource)
    {
        LOG(DLOGL_ERROR, "Warning: null source");
        status = kMspStatus_BadParameters;
    }
    else
    {
        uint8_t          *pRawPmt = NULL;	/**< PMT info obtained from the stream in MPEG syntax */
        int               RawPmtSize = 0;	/**< Size of the PMT info obtained from the stream */

        status = aSource->getRawPmt(&pRawPmt, &RawPmtSize);
        if (status == kMspStatus_Ok)
        {
            if (pRawPmt != NULL && RawPmtSize != 0)
            {
                status = BuildPmtTableStructure(pRawPmt, RawPmtSize);
                if (status == kMspStatus_Ok)
                {
                    mPmt->printPmtInfo();
                }
                else
                {
                    LOG(DLOGL_ERROR, "Error: Building PMT Table structure out of PMT failed.");
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "Error: Either NULL PMT or PMT of size 0 received. Hence ignoring.");
                status = kMspStatus_PsiError;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Error: Unable to get the RAW PMT from the source.");
            LOG(DLOGL_ERROR, "Error: Check whether Source received PMT from platform.");
        }
    }

    unlockMutex();

    return  status;
}


/******************************************************************************
 * Builds the SAIL PMT table structure from the RAW PMT table data pointer
 * (*pPmtPsiPacket) which is pointer to a memory of size PMTSize
 *
 * The high level syntax of the SAIL PMT table structure
 * typedef struct
 * {
 *	   uint32_t                   versionNumber;   ///< Version Number
 *	   uint16_t                   clockPid;        ///< Clock PID.
 *	   uint8_t                    pgmDescCount;    ///< Program level descriptor loop count
 *	   uint8_t                    esCount;         ///< Number of elementary stream
 *	   tMpegDesc                  **ppPgmDesc;     ///< Program level descriptor array
 *	   tEsData                    **ppEsData;      ///< Elementary stream loop
 *	} tPmt;
 */
eMspStatus Psi::BuildPmtTableStructure(uint8_t *pPmtPsiPacket, int PMTSize)
{
    uint8_t  esInfoStartIndex = 0;
    uint16_t sectionLength = 0;
    uint16_t program_info_length = 0;

    uint16_t       esInfoLen;
    uint8_t        descTag;
    uint8_t        descLen;
    tMpegDesc      *pDescInfo;
    tEsData        *pEsDataInfo;

    LOG(DLOGL_REALLY_NOISY, "%s: data ptr = %p data size = %d\n", __FUNCTION__, pPmtPsiPacket, PMTSize);

    if (mPmt == NULL)
    {
        LOG(DLOGL_ERROR, "%sError: Null pmt data ", __FILE__);
        return kMspStatus_Error;
    }

    memset(&(mPmt->mPmtInfo), 0, sizeof(tPmt));

    if (PMTSize != 0 && pPmtPsiPacket != NULL)
    {

        LOG(DLOGL_REALLY_NOISY, "%s: tableId - %d \n", __FUNCTION__, pPmtPsiPacket[0]);
        LOG(DLOGL_REALLY_NOISY, "%s: section_syntax_indicator - %d \n", __FUNCTION__, (pPmtPsiPacket[1] & 0x80));
        LOG(DLOGL_REALLY_NOISY, "%s: zero field - %d \n", __FUNCTION__, (pPmtPsiPacket[1] & 0x40));
        LOG(DLOGL_REALLY_NOISY, "%s: reserved - %d \n", __FUNCTION__, (pPmtPsiPacket[1] & 0x30));

        sectionLength = (((((uint16_t)pPmtPsiPacket[1]) & 0x000F) << 8) | ((uint16_t) pPmtPsiPacket[2]));
        LOG(DLOGL_REALLY_NOISY, "%s: section_length - %d \n", __FUNCTION__, sectionLength);
        LOG(DLOGL_REALLY_NOISY, "%s: prog_no - %d \n", __FUNCTION__, (((pPmtPsiPacket[3]) << 8) | (pPmtPsiPacket[4] & 0xFF)));
        LOG(DLOGL_REALLY_NOISY, "%s: reserved - %d \n", __FUNCTION__, (pPmtPsiPacket[5] & 0xC0));
        LOG(DLOGL_REALLY_NOISY, "%s: version no - %d \n", __FUNCTION__, (pPmtPsiPacket[5] & 0x3E));
        LOG(DLOGL_REALLY_NOISY, "%s: current_next_indicator - %d \n", __FUNCTION__, (pPmtPsiPacket[5] & 0x01));
        LOG(DLOGL_REALLY_NOISY, "%s: section no - %d \n", __FUNCTION__, (pPmtPsiPacket[6]));
        LOG(DLOGL_REALLY_NOISY, "%s: last_section_no - %d \n", __FUNCTION__, (pPmtPsiPacket[7]));
        LOG(DLOGL_REALLY_NOISY, "%s: reserved - %d \n", __FUNCTION__, (pPmtPsiPacket[8] & 0xE0));
        LOG(DLOGL_REALLY_NOISY, "%s: PCR PID - %d \n", __FUNCTION__, (((pPmtPsiPacket[8] & 0x1F) << 8) | (pPmtPsiPacket[9])));

        // Clock Pid
        mPmt->mPmtInfo.clockPid = (((pPmtPsiPacket[8] & 0x1F) << 8) | (pPmtPsiPacket[9]));
        dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "PCR PID : 0x%x\n", mPmt->mPmtInfo.clockPid);

        LOG(DLOGL_REALLY_NOISY, "%s: reserved - %d \n", __FUNCTION__, (pPmtPsiPacket[10] & 0xF0));

        program_info_length = (((((uint16_t)pPmtPsiPacket[10]) & 0x000F) << 8) | ((uint16_t) pPmtPsiPacket[11]));
        LOG(DLOGL_REALLY_NOISY, "%s: prog_info_length - %d \n", __FUNCTION__, program_info_length);

        uint8_t *pPgmInfoStart = pPmtPsiPacket + kAppPmtHeaderLength;
        uint8_t *pPgmInfoEnd   = pPgmInfoStart + program_info_length;
        mPmt->mPmtInfo.pgmDescCount = 0;
        if (program_info_length == 0)
        {
            mPmt->mPmtInfo.ppPgmDesc = NULL;
            dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "%s:%d pPmtInfo->ppPgmDesc = NULL; \n", __FUNCTION__, __LINE__);
        }
        else
        {
            mPmt->mPmtInfo.ppPgmDesc = (tMpegDesc **)calloc(kAppMaxProgramDesciptors, sizeof(tMpegDesc *));
            LOG(DLOGL_REALLY_NOISY, "Program descriptor base pointer is %p", mPmt->mPmtInfo.ppPgmDesc);
            while ((pPgmInfoStart < pPgmInfoEnd) && (mPmt->mPmtInfo.pgmDescCount < kAppMaxProgramDesciptors))
            {
                descTag = *pPgmInfoStart++;
                descLen = *pPgmInfoStart++;
                pDescInfo = (tMpegDesc *)calloc(1, sizeof(tMpegDesc));
                if (pDescInfo)
                {
                    pDescInfo->tag = descTag;
                    pDescInfo->dataLen = descLen;
                    pDescInfo->data = (uint8_t *)calloc(1, descLen);
                    memcpy(pDescInfo->data, pPgmInfoStart, descLen);

                    mPmt->mPmtInfo.ppPgmDesc[mPmt->mPmtInfo.pgmDescCount] = pDescInfo;
                    mPmt->mPmtInfo.pgmDescCount++;
                    pPgmInfoStart += descLen;
                }
                else
                {
                    LOG(DLOGL_EMERGENCY, "%sError: Null pDescInfo ", __FILE__);
                    return kMspStatus_PsiError;
                }
            }
        }

        esInfoStartIndex = kAppPmtHeaderLength + program_info_length;
        LOG(DLOGL_REALLY_NOISY, "%s: total length to skip - %d \n\n", __FUNCTION__, esInfoStartIndex);

        mPmt->mPmtInfo.esCount = 0;
        mPmt->mPmtInfo.ppEsData = (tEsData **)calloc(kAppMaxEsCount, sizeof(tEsData *));

        uint8_t *pEsInfoStart = (pPmtPsiPacket + esInfoStartIndex);
        uint8_t *pEsInfoEnd   = (pPmtPsiPacket + kAppPmtTableLengthMinusSectionLength + sectionLength  - kAppTableCRCSize);

        while ((pEsInfoStart < pEsInfoEnd) && (mPmt->mPmtInfo.esCount < kAppMaxEsCount))
        {
            uint16_t pid;

            pEsDataInfo = (tEsData *)calloc(1, sizeof(tEsData));
            if (pEsDataInfo == NULL)
            {
                LOG(DLOGL_EMERGENCY, "%sError: Null pEsDataInfo ", __FILE__);
                return kMspStatus_PsiError;
            }
            uint8_t streamType = *pEsInfoStart++;
            pEsDataInfo->reserved[0] = *pEsInfoStart >> 5;
            pid = (*pEsInfoStart++ & 0x1F) << 8;
            pid |= *pEsInfoStart++ & 0xFF;
            pEsDataInfo->streamType = streamType;
            pEsDataInfo->pid = pid;

            dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Stream Type : 0x%x -  PID : 0x%x\n", streamType, pid);

            // ES_info_length
            pEsDataInfo->reserved[1] = *pEsInfoStart >> 4;
            esInfoLen = (*pEsInfoStart++ & 0xF) << 8;
            esInfoLen |= *pEsInfoStart++;
            pEsDataInfo->reserved[2] = 0; // ??

            pEsDataInfo->descCount = 0;
            if (esInfoLen == 0)
            {
                pEsDataInfo->ppEsDesc = NULL;
            }
            else
            {
                uint8_t* pDsInfoEnd = NULL;
                pDsInfoEnd = pEsInfoStart + esInfoLen;
                pEsDataInfo->ppEsDesc = (tMpegDesc **)calloc(kMaxESDescriptors, sizeof(tMpegDesc *));
                while ((pEsInfoStart < pDsInfoEnd) && (pEsDataInfo->descCount < kMaxESDescriptors))
                {
                    descTag = *pEsInfoStart++;
                    descLen = *pEsInfoStart++;
                    pDescInfo = (tMpegDesc *)calloc(1, sizeof(tMpegDesc));
                    pDescInfo->tag = descTag;
                    pDescInfo->dataLen = descLen;
                    pDescInfo->data = (uint8_t *)calloc(1, descLen);
                    memcpy(pDescInfo->data, pEsInfoStart, descLen);

                    pEsInfoStart += descLen;

                    pEsDataInfo->ppEsDesc[pEsDataInfo->descCount] = pDescInfo;
                    pEsDataInfo->descCount++;
                }
            }
            mPmt->mPmtInfo.ppEsData[mPmt->mPmtInfo.esCount] = pEsDataInfo;
            mPmt->mPmtInfo.esCount = mPmt->mPmtInfo.esCount + 1;
        }

        mPmt->createAudioVideoListsFromPmtInfo();
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid->NULL tcgmi_Data parameter \n", __FUNCTION__);
    }

    return kMspStatus_Ok;
}

/******************************************************************************
 * Returns the PMT object
 */
Pmt* Psi::getPmtObj(void)
{
    return  mPmt;
}

eMspStatus Psi::getComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset)
{
    Pmt *pmt = getPmtObj();
    if (pmt)
    {
        tPmt pmtInfo;
        pmt->getPmtInfo(&pmtInfo);
        dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Elementary Stream Count %d %d %d %d\n", pmtInfo.esCount, infoSize, offset, *count);
        *count = 0;
        for (uint32_t i = offset; i < pmtInfo.esCount && *count < infoSize; i++)
        {
            tEsData *esData = pmtInfo.ppEsData[i];
            if (esData)
            {
                info[i - offset].pid = esData->pid;
                info[i - offset].streamType = esData->streamType;
                memset(info[i - offset].langCode, 0 , ISO_639_CODE_LENGTH);
                if (isAudioStreamType(info[i - offset].streamType))
                {
                    for (int j = 0 ; j < esData->descCount; j++)
                    {
                        tMpegDesc *ppEsDesc = esData->ppEsDesc[j];
                        if (ppEsDesc && ppEsDesc->tag == ISO_639_LANG_TAG)
                        {
                            memcpy(info[i - offset].langCode, ppEsDesc->data, ISO_639_CODE_LENGTH);
                            break;
                        }
                    }
                }
                *count = *count + 1;
            }
        }
        return kMspStatus_Ok;
    }
    else
    {
        return kMspStatus_Error;
    }

}

/******************************************************************************
 * Lock the mutex
 */
void Psi::lockMutex(void)
{
    dlog(DL_MSP_PSI, DLOGL_NOISE, "Locking mutx %p", &mPsiMutex);
    pthread_mutex_lock(&mPsiMutex);
}

/******************************************************************************
 * Unlock the mutex
 */
void Psi::unlockMutex(void)
{
    dlog(DL_MSP_PSI, DLOGL_NOISE, "unLocking mutx %p", &mPsiMutex);
    pthread_mutex_unlock(&mPsiMutex);
}

/******************************************************************************
 * Returns whether a given stream type is of AUDIO or not
 */
bool Psi::isAudioStreamType(uint16_t aStreamType)
{
    return (aStreamType == kElementaryStreamType_MPEG1_Audio ||
            aStreamType == kElementaryStreamType_MPEG2_Audio ||
            aStreamType == kElementaryStreamType_AAC_Audio ||
            aStreamType == kElementaryStreamType_AACplus_Audio ||
            aStreamType == kElementaryStreamType_DDPlus_Audio ||
            aStreamType == kElementaryStreamType_GI_Audio);
}

