#include <time.h>
#include "psi.h"
#include "languageSelection.h"
#include "MSPHnOnDemandStreamerSource.h"


#define SCOPELOG(section, scopename)  dlogns::ScopeLog __xscopelog(section, scopename, __FILE__, __LINE__, DLOGL_FUNCTION_CALLS)
#define FNLOG(section)  dlogns::ScopeLog __xscopelog(section, __PRETTY_FUNCTION__, __FILE__, __LINE__, DLOGL_FUNCTION_CALLS)

#define LOG(level, msg, args...)  dlog(DL_MSP_ONDEMAND, level,"MSPHnOnDemandStreamerSource:%s:%d " msg, __FUNCTION__, __LINE__, ##args);


#define kAppTableHeaderFromSectionLength  5  ///< Table Header Size after section Length field
#define kAppPmtHeaderLength               12 ///< Table length prior to program level descriptors
#define kAppPmtTableLengthMinusSectionLength 3  ///< PMT Table length - Section Length
///< 3 bytes = (table_id + section_syntax_indicator + section length)>
#define kAppPmtElmStreamInfoLength        5  ///< Elementary stream info length <type, PID and ES Info Length>
#define kAppTableCRCSize                  4  ///< Table CRC size in bytes

#define kAppMaxProgramDesciptors 10
#define kAppMaxEsCount  10
#define kMaxESDescriptors  10

// Standard Defines
#define kPid        0x1FFF
#define ISO_639_LANG_TAG 10
#define PID_INVALID        (0xffff)


MSPHnOnDemandStreamerSource::MSPHnOnDemandStreamerSource(std::string aSrcUrl, tCpeSrcHandle srcHandle)
{
    FNLOG(DL_MSP_ONDEMAND);
    mSrcUrl = aSrcUrl;
    mSessionId = 0;
    mProgramNumber = 0;
    mSourceId = 0;
    mCpeSrcHandle = srcHandle;
    mSrcStateCB = NULL;
    mClientContext = NULL;
    mPgrmHandle = 0;
    mInjectPat = NULL;
    mInjectPmt = NULL;
    m_CCIbyte = 0;
    memset(&mPmtInfo, 0, sizeof(tCpePgrmHandlePmt));
    mRawPmtPtr = NULL;
    mRawPatPtr = NULL;
    mPmtPid = PID_INVALID;
    mRawPmtSize = 0;
}

void MSPHnOnDemandStreamerSource::SetCpeStreamingSessionID(uint32_t sessionId)
{
    mSessionId = sessionId;
}

MSPHnOnDemandStreamerSource::~MSPHnOnDemandStreamerSource()
{
    FNLOG(DL_MSP_ONDEMAND);
    if (mCpeSrcHandle != 0)
    {
        int cpeStatus = cpe_hnsrvmgr_Stop(mCpeSrcHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "HnOnDemand server source stop failed with error code %d", cpeStatus);
        }

        cpeStatus = cpe_hnsrvmgr_Close(mCpeSrcHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "HnOnDemand server source close failed with error code %d", cpeStatus);
        }
        mCpeSrcHandle = 0;
    }

    LOG(DLOGL_REALLY_NOISY, "HnOnDemand Freeing memory for rawpat and rawpmt");

    if (mRawPmtPtr)
    {
        free(mRawPmtPtr);
        mRawPmtPtr = NULL;
    }
    if (mRawPatPtr)
    {
        free(mRawPatPtr);
        mRawPatPtr = NULL;
    }

}

eMspStatus MSPHnOnDemandStreamerSource::load(SourceStateCallback aPlaybackCB, void* aClientContext)
{
    FNLOG(DL_MSP_ONDEMAND);
    mSrcStateCB = aPlaybackCB;
    mClientContext = aClientContext;

    return kMspStatus_Ok;
}

eMspStatus MSPHnOnDemandStreamerSource::open(eResMonPriority pri)
{
    FNLOG(DL_MSP_ONDEMAND);

    (void) pri;  // not used for Server source

    eMspStatus mspStatus = kMspStatus_Ok;
    int cpeStatus = kCpe_NoErr;

    mPgrmHandle = 0;

    cpeStatus = cpe_hnsrvmgr_Open(mCpeSrcHandle, mSessionId, &mPgrmHandle);
    if (cpeStatus != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "HnOnDemand server manager  Open failed 0x%x\n", -cpeStatus);
        cpe_hnsrvmgr_Close(mCpeSrcHandle);
        mspStatus = kMspStatus_Error;
    }

    LOG(DLOGL_REALLY_NOISY, "HnOnDemand server open returning %d <SH = %p PH = %p>\n", mspStatus, mCpeSrcHandle, mPgrmHandle);

    return mspStatus;
}

eMspStatus MSPHnOnDemandStreamerSource::start()
{
    eMspStatus status =  kMspStatus_Ok;
    // Start the source
    if (mCpeSrcHandle != 0)
    {
        int cpeStatus = kCpe_NoErr;

        LOG(DLOGL_REALLY_NOISY, "Calling cpe_hnsrvmgr_Start");
        cpeStatus = cpe_hnsrvmgr_Start(mPgrmHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "HnOnDemand server start failed 0x%x\n", -cpeStatus);

            if (mPgrmHandle)
            {
                LOG(DLOGL_ERROR, "Start returned error.. Calling cpe_hnsrvmgr_Close");
                cpe_hnsrvmgr_Close(mPgrmHandle);
                mPgrmHandle = 0;
            }

            status = kMspStatus_BadSource;
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "Successfully started the MSPHnOnDemandStreamerSource");
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "HnOnDemand server source start returning kMspStatus_StateError <SH = %p PH = %p>\n", mCpeSrcHandle, mPgrmHandle);
        status =  kMspStatus_StateError;
    }

    return status;
}

eMspStatus MSPHnOnDemandStreamerSource::stop()
{
    // Stop the source
    FNLOG(DL_MSP_ONDEMAND);
    int cpeStatus = kCpe_NoErr;
    eMspStatus status = kMspStatus_Ok;

    if (mCpeSrcHandle != 0)
    {
        if (mPgrmHandle != 0)
        {

            cpeStatus = cpe_hnsrvmgr_CancelInjectData(mPgrmHandle, mInjectPat);
            if (cpeStatus != kCpe_NoErr)
            {
                LOG(DLOGL_ERROR, " Call cpe_hnsrvmgr_CancelInjectData(...)  for PAT return error %d, with record handle %d ", cpeStatus, (int) mPgrmHandle);
                status = kMspStatus_Error;
            }

            cpeStatus = cpe_hnsrvmgr_CancelInjectData(mPgrmHandle, mInjectPmt);
            if (cpeStatus != kCpe_NoErr)
            {
                LOG(DLOGL_ERROR, " Call cpe_record_CancelInjectData(...)  for PMT return error %d, with record handle %d ", cpeStatus, (int) mPgrmHandle);
                status = kMspStatus_Error;
            }

            cpeStatus = cpe_hnsrvmgr_Stop(mPgrmHandle);
            if (cpeStatus != kCpe_NoErr)
            {
                LOG(DLOGL_ERROR, "MRDVR HN server stop failed with error code %d", cpeStatus);
                status = kMspStatus_Error;
            }

            cpeStatus = cpe_hnsrvmgr_Close(mPgrmHandle);
            if (cpeStatus != kCpe_NoErr)
            {
                LOG(DLOGL_ERROR, "MRDVR HN server close failed with error code %d", cpeStatus);
                status = kMspStatus_Error;
            }
        }

        mCpeSrcHandle = 0;
        mPgrmHandle = 0;
    }

    return status;
}


tCpeSrcHandle MSPHnOnDemandStreamerSource::getCpeSrcHandle()const
{
    FNLOG(DL_MSP_ONDEMAND);
    return mCpeSrcHandle;
}

tCpePgrmHandle MSPHnOnDemandStreamerSource::getCpeProgHandle()const
{
    FNLOG(DL_MSP_ONDEMAND);
    return mPgrmHandle;
}

int MSPHnOnDemandStreamerSource::getProgramNumber()const
{
    FNLOG(DL_MSP_ONDEMAND);
    return mProgramNumber;
}

int MSPHnOnDemandStreamerSource::getSourceId()const
{
    FNLOG(DL_MSP_ONDEMAND);
    return mSourceId;
}

std::string MSPHnOnDemandStreamerSource::getSourceUrl()const
{
    FNLOG(DL_MSP_ONDEMAND);
    return mSrcUrl;
}

std::string MSPHnOnDemandStreamerSource::getFileName()const
{
    FNLOG(DL_MSP_ONDEMAND);
    return mFileName;
}

bool MSPHnOnDemandStreamerSource::isDvrSource()const
{
    FNLOG(DL_MSP_ONDEMAND);
    return false;
}

bool MSPHnOnDemandStreamerSource::canRecord()const
{
    FNLOG(DL_MSP_ONDEMAND);
    return false;
}

eMspStatus MSPHnOnDemandStreamerSource::setSpeed(tCpePlaySpeed aPlaySpeed, uint32_t aNpt)
{
    FNLOG(DL_MSP_ONDEMAND);
    UNUSED_PARAM(aPlaySpeed)
    UNUSED_PARAM(aNpt)
    return kMspStatus_Error;
}

eMspStatus MSPHnOnDemandStreamerSource::getPosition(float *pNptTime)
{
    FNLOG(DL_MSP_ONDEMAND);
    UNUSED_PARAM(pNptTime)
    return kMspStatus_Error;
}

eMspStatus MSPHnOnDemandStreamerSource::setPosition(float aNptTime)
{
    FNLOG(DL_MSP_ONDEMAND);
    UNUSED_PARAM(aNptTime)
    return kMspStatus_Error;
}

bool MSPHnOnDemandStreamerSource::isPPV(void)
{
    FNLOG(DL_MSP_ONDEMAND);
    return false;
}

bool MSPHnOnDemandStreamerSource::isQamrf(void)
{
    FNLOG(DL_MSP_ONDEMAND);
    return false;
}

// Release the streaming source
eMspStatus MSPHnOnDemandStreamerSource::release()
{
    FNLOG(DL_MSP_ONDEMAND);
    int cpeStatus = kCpe_NoErr;
    eMspStatus status = kMspStatus_Ok;
    if (mPgrmHandle)
    {
        cpeStatus = cpe_hnsrvmgr_Stop(mPgrmHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "MRDVR HN server stop failed with error code %d", cpeStatus);
            status = kMspStatus_Error;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Null Program Handle");
        status = kMspStatus_Error;
    }
    return status;
}

eMspStatus MSPHnOnDemandStreamerSource::setStreamInfo(Psi *ptrPsi)
{
    FNLOG(DL_MSP_ONDEMAND);
    eMspStatus status = kMspStatus_Ok;

    if (ptrPsi)
    {
        int Pidsize = getNumOfPids(ptrPsi);
        LOG(DLOGL_REALLY_NOISY, "Number of Pids returned from psi is %d", Pidsize);
        tCpeSrcPFPidDef* mPidTable = (tCpeSrcPFPidDef*) malloc(sizeof(tCpeSrcPFPidDef) + (sizeof(tCpeSrcPFPidStruct) * Pidsize));
        if (!mPidTable)
        {
            LOG(DLOGL_ERROR, "Unable to allocate memory for mPidTable");
            status = kMspStatus_Error;
        }
        else
        {
            memset(mPidTable, '\0', sizeof(tCpeSrcPFPidDef) + (sizeof(tCpeSrcPFPidStruct) * Pidsize));
            tCpeHnSrvMgrStreamInfo* pStreamInfo = (tCpeHnSrvMgrStreamInfo*) malloc(sizeof(tCpeHnSrvMgrStreamInfo));
            if (pStreamInfo)
            {
                memset(pStreamInfo, 0, sizeof(tCpeHnSrvMgrStreamInfo));
                LOG(DLOGL_REALLY_NOISY, "mCpeSrcHandle:%p", mCpeSrcHandle);
                pStreamInfo->type = eCpeHnSrvMgrStreamType_Tuner;
                mPidTable->numOfPids = 0;

                // Get audio pids
                status = getAudioPid(ptrPsi, mPidTable);
                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "%s:%d Call getAudioPid(...) returns error ", __FUNCTION__, __LINE__);
                    status = kMspStatus_Error;
                }
                else
                {
                    // Get clock pid
                    status = getClockPid(ptrPsi, mPidTable);
                    if (status != kMspStatus_Ok)
                    {
                        LOG(DLOGL_ERROR, "%s:%d Call getClockPid(...) returns error ", __FUNCTION__, __LINE__);
                        status = kMspStatus_Error;
                    }
                    else
                    {
                        // Get music pid
                        uint32_t musicpid = ptrPsi->getMusicPid();
                        if (musicpid)
                        {
                            LOG(DLOGL_REALLY_NOISY, "Adding Music Pid");
                            mPidTable->pids[mPidTable->numOfPids].type = eCpeSrcPFPidType_OtherPid;
                            mPidTable->pids[mPidTable->numOfPids].hasPCR = false;
                            mPidTable->pids[mPidTable->numOfPids].pid = musicpid;
                            mPidTable->numOfPids++;
                        }

                        // Get Video Pids last
                        status = getVideoPid(ptrPsi, mPidTable);
                        if (status != kMspStatus_Ok)
                        {
                            LOG(DLOGL_REALLY_NOISY, "Not Really an error. A stream with only a music pid is expected. RADIO channel.");
                            status = kMspStatus_Ok;
                        }

                        pStreamInfo->pidTable = mPidTable;
                        LOG(DLOGL_REALLY_NOISY, "Before calling set,pidListSize is------------>%d", mPidTable->numOfPids);
                        int length = sizeof(tCpeHnSrvMgrStreamInfo);
                        int cpeStatus = cpe_hnsrvmgr_Set(mPgrmHandle, eCpeHnSrvMgrGetSetNames_StreamInfo, (void*)pStreamInfo, length);
                        if (cpeStatus < 0)
                        {
                            LOG(DLOGL_ERROR, "Set Stream [PAT/PMT] info failed:%d", cpeStatus);
                            status = kMspStatus_Error;
                        }
                        else
                        {
                            LOG(DLOGL_REALLY_NOISY, "Successfully Set Stream [PAT/PMT] info:%d", cpeStatus);

                            status = InjectStreamInfo(ptrPsi);
                            if (status == kMspStatus_Error)
                            {
                                LOG(DLOGL_ERROR, "%s: Serious Error !!!!!!! Failed while injecting into the stream: %d ", __func__, status);
                            }
                            else
                            {
                                LOG(DLOGL_REALLY_NOISY, "%s: Success, Injecting into the stream is successful for In-Memory content %d", __func__, status);
                            }
                        }
                    }
                }

                free(pStreamInfo);
            }
            else
            {
                LOG(DLOGL_ERROR, "Unable to allocate memory for pStreamInfo");
                status = kMspStatus_Error;
            }

            free(mPidTable);
        }
    }
    else
    {
        status = kMspStatus_Error;
        LOG(DLOGL_ERROR, "ptrPsi is NULL");
    }

    return status;
}


/** *********************************************************
/returns
*/
eMspStatus MSPHnOnDemandStreamerSource::getAudioPid(Psi *ptrPsi, tCpeSrcPFPidDef *mPids)
{
    FNLOG(DL_MSP_ONDEMAND);
    eMspStatus status = kMspStatus_Ok;

    if (ptrPsi && mPids)
    {
        //getting audio pid informations
        ptrPsi->lockMutex();

        LanguageSelection langSelect(LANG_SELECT_AUDIO, ptrPsi);
        tPid audioPidStruct = langSelect.pidSelected();

        LOG(DLOGL_NOISE, "audio pid type value is %d", audioPidStruct.streamType);
        Pmt *pmt = ptrPsi->getPmtObj();
        if (pmt)
        {
            std::list<tPid> *audioList = pmt->getAudioPidList();
            std::list<tPid>::iterator iter;
            for (iter = audioList->begin(); iter != audioList->end(); iter++)
            {
                audioPidStruct = (*iter);
                switch (audioPidStruct.streamType)
                {
                case kCpeStreamType_MPEG1_Audio:
                case kCpeStreamType_MPEG2_Audio:
                    mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_AudMpegPid;
                    break;
                case kCpeStreamType_GI_Audio:
                    mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_AudAC3Pid;
                    break;
                case kCpeStreamType_DDPlus_Audio:
                    mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_AudAC3Pid;
                    break;

                case kCpeStreamType_AAC_Audio:
                case kCpeStreamType_AACplus_Audio:          //Not sure about,whether this case means AC3.TODO-verify it
                    mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_AudAACPid;
                    break;
                case kCpeStreamType_LPCM:
                    mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_AudLPCMPid;
                    break;
                default:
                    mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_AudMpegPid;
                    break;
                }
                mPids->pids[mPids->numOfPids].hasPCR = false;
                mPids->pids[mPids->numOfPids].pid = audioPidStruct.pid;
                mPids->numOfPids++;
            }
        }

        ptrPsi->unlockMutex();
    }
    else
    {
        status = kMspStatus_Error;
        LOG(DLOGL_ERROR, "ptrPsi[%p] or/and mPids[%p] may be NULL", ptrPsi, mPids);
    }

    return  status;
}


/** *********************************************************
/returns
*/
eMspStatus MSPHnOnDemandStreamerSource::getVideoPid(Psi *ptrPsi, tCpeSrcPFPidDef *mPids)
{
    FNLOG(DL_MSP_ONDEMAND);

    if (!ptrPsi || !mPids)
    {
        LOG(DLOGL_ERROR, "ptrPsi[%p] or/and mPids[%p] may be NULL", ptrPsi, mPids);
        return kMspStatus_Error;
    }

    eMspStatus status = kMspStatus_Ok;
    tPid videoPidStruct = {0, 0};

    Pmt *pmt = ptrPsi->getPmtObj();
    if (pmt)
    {
        std::list<tPid>* videoList = pmt->getVideoPidList();
        //no for loop to handle the list??.Expecting just one PID?
        int n = videoList->size();
        if (n == 0)
        {
            LOG(DLOGL_SIGNIFICANT_EVENT, "%s:%d No video pid.", __FUNCTION__, __LINE__);
            return  kMspStatus_NotSupported;
        }
        else
        {
            LOG(DLOGL_NOISE, "%s:%d number of video pid in the list is %d ", __FUNCTION__, __LINE__, n);
            std::list<tPid>::iterator iter = videoList->begin();

            //Choose the first one, don't know what to do if we have more than one video pid
            videoPidStruct = (tPid) * iter;

            LOG(DLOGL_REALLY_NOISY, "video pid type value is %d", videoPidStruct.streamType);
            switch (videoPidStruct.streamType)
            {
            case kCpeStreamType_MPEG1_Video:
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_VidMPEG1Pid;
                break;


            case kCpeStreamType_MPEG2_Video:
            case kCpeStreamType_GI_Video:
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_VidMpeg2Pid;
                break;

            case kCpeStreamType_H264_Video:
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_VidH264Pid;
                break;

            case kCpeStreamType_VC1_Video:
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_VidVC1Pid;
                break;

            default:
                //mPids->pids[mPids->numOfPids].type=eCpeSrcPFPidType_OtherPid;
                mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_VidMpeg2Pid; //we are getting PID type value as 128.This seems to be wrong.So used a temporary workaround.
                // TODO needs to be verified and fixed
                break;
            }

            mPids->pids[mPids->numOfPids].hasPCR = false;
            mPids->pids[mPids->numOfPids].pid = videoPidStruct.pid;
            mPids->numOfPids++;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "pmt is NULL");
    }

    return  status;
}


/** *********************************************************
/returns
*/
eMspStatus MSPHnOnDemandStreamerSource::getClockPid(Psi *ptrPsi, tCpeSrcPFPidDef *mPids)
{
    FNLOG(DL_MSP_ONDEMAND);

    if (!ptrPsi || !mPids)
    {
        LOG(DLOGL_ERROR, "ptrPsi[%p] or/and mPids[%p] may be NULL", ptrPsi, mPids);
        return kMspStatus_Error;
    }

    eMspStatus status = kMspStatus_Ok;
    Pmt *pmt = ptrPsi->getPmtObj();
    if (pmt)
    {
        mPids->pids[mPids->numOfPids].hasPCR = true;
        mPids->pids[mPids->numOfPids].type = eCpeSrcPFPidType_OtherPid; //The sample application uses this value for clock PID type.Don't know why.TODO-verify with them
        mPids->pids[mPids->numOfPids].pid = pmt->getPcrpid();
        mPids->numOfPids++;
    }
    else
    {
        LOG(DLOGL_ERROR, "%s:%d Got null pmt object ", __FUNCTION__, __LINE__);
        status = kMspStatus_PsiError;
    }
    return  status;
}

eMspStatus MSPHnOnDemandStreamerSource::InjectStreamInfo(Psi *mPsiptr)
{
    FNLOG(DL_MSP_ONDEMAND);
    eMspStatus status = kMspStatus_Ok;
    // mPid;
    mPsiptr->lockMutex();

    Pmt *pmt = mPsiptr->getPmtObj();
    if (pmt == NULL)
    {
        LOG(DLOGL_ERROR, "PMT structure is NULL,Not able to get raw PAT/PMT data");
        mPsiptr->unlockMutex();
        return kMspStatus_Error;
    }

    status = mPsiptr->getRawPmt(&mRawPmtPtr, &mRawPmtSize, &mPmtPid); //TODO status check
    if (status == kMspStatus_Ok)
    {
        LOG(DLOGL_NOISE, " Fetching raw PMT for recording injection success");
    }
    else
    {
        LOG(DLOGL_ERROR, "Warning: Fetching raw PMT for recording injection failed");
    }

    status = mPsiptr->getRawPat(&mRawPatPtr); //TODO status check
    if (status == kMspStatus_Ok)
    {
        LOG(DLOGL_NOISE, " Fetching SPTS PAT success");
    }
    else
    {
        LOG(DLOGL_ERROR, "Warning: Fetching SPTS PAT failed");
    }

    mPsiptr->unlockMutex();

    LOG(DLOGL_NOISE, "Going to inject the data of size %d and pointer %p\n", kSPTS_PATSizeWithCRC, mRawPatPtr);

    int err = cpe_hnsrvmgr_InjectData(mPgrmHandle, 0, mRawPatPtr, kSPTS_PATSizeWithCRC, eCpeHnSrv_InjPacketPsi, 50, &mInjectPat);
    if (err != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "%s: PAT inject failed: %d\n", __FUNCTION__, err);
        return kMspStatus_Error;
    }
    else
    {
        LOG(DLOGL_NOISE, "Injection of PAT data into the Live stream succeeded!!!!!!!!!!\n");
    }

    status = DumpPmtInfo(mRawPmtPtr, mRawPmtSize);
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "Priniting RAW PMT failed");
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "Success in dumping RAW PMT");
    }

    LOG(DLOGL_NOISE, "Going to inject the data of size %d and pointer %p and PMT Pid %d\n", mRawPmtSize, mRawPmtPtr, mPmtPid);
    err = cpe_hnsrvmgr_InjectData(mPgrmHandle, mPmtPid, mRawPmtPtr, mRawPmtSize, eCpeHnSrv_InjPacketPsi, 50, &mInjectPmt);
    if (err != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "%s: PMT inject failed: %d\n", __FUNCTION__, err);
        return kMspStatus_Error;
    }
    else
    {
        LOG(DLOGL_NOISE, "Injection of PMT data into the Record stream succeeded!!!!!!!!!!\n");
    }

    return kMspStatus_Ok;
}



eMspStatus MSPHnOnDemandStreamerSource::DumpPmtInfo(uint8_t *pPmtPsiPacket, int PMTSize)
{
    uint8_t  esInfoStartIndex = 0;
    uint16_t sectionLength = 0;
    uint16_t program_info_length = 0;

    uint16_t       esInfoLen = 0;
    uint8_t        descTag = 0;
    uint8_t        descLen = 0;
    uint32_t       descCount = 0;

    LOG(DLOGL_REALLY_NOISY, "%s: data ptr = %p data size = %d\n", __FUNCTION__, pPmtPsiPacket, PMTSize);

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
        uint32_t clockPid = (((pPmtPsiPacket[8] & 0x1F) << 8) | (pPmtPsiPacket[9]));
        dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "PCR PID : 0x%x\n", clockPid);

        LOG(DLOGL_REALLY_NOISY, "%s: reserved - %d \n", __FUNCTION__, (pPmtPsiPacket[10] & 0xF0));

        program_info_length = (((((uint16_t)pPmtPsiPacket[10]) & 0x000F) << 8) | ((uint16_t) pPmtPsiPacket[11]));
        LOG(DLOGL_REALLY_NOISY, "%s: prog_info_length - %d \n", __FUNCTION__, program_info_length);

        uint8_t *pPgmInfoStart = pPmtPsiPacket + kAppPmtHeaderLength;
        uint8_t *pPgmInfoEnd   = pPgmInfoStart + program_info_length;

        // Dumping Program Info Descriptors
        if (program_info_length == 0)
        {
            dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "%s:%d pPmtInfo->ppPgmDesc = NULL; \n", __FUNCTION__, __LINE__);
        }
        else
        {
            // Dumping Program Info Descriptors
            while ((pPgmInfoStart < pPgmInfoEnd))
            {
                descTag = *pPgmInfoStart++;
                descLen = *pPgmInfoStart++;

                dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Program Info DescTag:0x%x DescLen:%d", descTag, descLen);

                pPgmInfoStart += descLen;
            }
        }

        esInfoStartIndex = kAppPmtHeaderLength + program_info_length;
        LOG(DLOGL_REALLY_NOISY, "%s: total length to skip - %d \n\n", __FUNCTION__, esInfoStartIndex);

        uint8_t *pEsInfoStart = (pPmtPsiPacket + esInfoStartIndex);
        uint8_t *pEsInfoEnd   = (pPmtPsiPacket + kAppPmtTableLengthMinusSectionLength + sectionLength  - kAppTableCRCSize);

        // Dumping ES Type and PID Number and ES Descriptors
        while ((pEsInfoStart < pEsInfoEnd))
        {
            uint16_t pid = 0;

            uint8_t streamType = *pEsInfoStart++;

            pid = (*pEsInfoStart++ & 0x1F) << 8;
            pid |= *pEsInfoStart++ & 0xFF;

            dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Stream Type : 0x%x -  PID : 0x%x\n", streamType, pid);

            // ES_info_length
            esInfoLen = (*pEsInfoStart++ & 0xF) << 8;
            esInfoLen |= *pEsInfoStart++;

            dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Elementary stream INFO length :%d", esInfoLen);

            // Dumping ES Descriptors
            if (esInfoLen == 0)
            {
                dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Elementary stream INFO length is 0: No ES Descriptors present");
            }
            else
            {
                uint8_t* pDsInfoEnd = NULL;
                pDsInfoEnd = pEsInfoStart + esInfoLen;

                // Dumping ES Descriptors
                while ((pEsInfoStart < pDsInfoEnd) && (descCount < kMaxESDescriptors))
                {
                    descTag = *pEsInfoStart++;
                    descLen = *pEsInfoStart++;

                    dlog(DL_MSP_PSI, DLOGL_REALLY_NOISY, "Elementary stream DescTag:0x%x DescLen:%d", descTag, descLen);

                    pEsInfoStart += descLen;

                    descCount++;
                }
            }
        }

    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid->NULL \n", __FUNCTION__);
    }

    return kMspStatus_Ok;
}

eMspStatus  MSPHnOnDemandStreamerSource::SetPatPMT(Psi *ptrPsi)
{
    FNLOG(DL_MSP_ONDEMAND);
    eMspStatus status = kMspStatus_Ok;
    // mPid;
    uint32_t mpgmNo = 0;
    int err = kCpe_NoErr;
    ptrPsi->lockMutex();
    Pmt *pmt = ptrPsi->getPmtObj();
    if (pmt == NULL)
    {
        LOG(DLOGL_ERROR, "PMT structure is NULL,Not able to get raw PAT/PMT data");
        ptrPsi->unlockMutex();
        return kMspStatus_Error;
    }
    mpgmNo =  ptrPsi->getProgramNo();
    LOG(DLOGL_MINOR_DEBUG, "Program No returned from the psi stucture is %u", mpgmNo);

    err = cpe_ProgramHandle_Set(mPgrmHandle, eCpePgrmHandleNames_ProgramNumber, (void *)&mpgmNo);
    if (err != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "Error set eCpePgrmHandleNames_ProgramNumber rc=%d", err);
        status = kMspStatus_CpeMediaError;
    }
    else
        LOG(DLOGL_REALLY_NOISY, "%s :Successful in Setting Program No %u, handle %p", __func__, mpgmNo, mPgrmHandle);

    status = pmt->getPmtInfo(&mPmtInfo);   // fill pmt info

    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "%s:getPmtInfo error:  %d", __func__ , status);
        status = kMspStatus_PsiError;
    }
    else
    {
        LOG(DLOGL_NOISE, "%s:****PMT Info: vers %d clkpid %x pgmCnt %d escnt %d ppgmdesc %p pesdat %p", __func__,
            mPmtInfo.versionNumber, mPmtInfo.clockPid, mPmtInfo.pgmDescCount, mPmtInfo.esCount, mPmtInfo.ppPgmDesc, mPmtInfo.ppEsData);
        err = cpe_ProgramHandle_Set(mPgrmHandle, eCpePgrmHandleNames_Pmt, (void *)&mPmtInfo);
        if (err != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "Error set eCpePgrmHandleNames_Pmt rc=%d", err);
            status = kMspStatus_CpeMediaError;
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "%s: Successaful in setting eCpePgrmHandleNames_Pmt rc=%d", __func__, err);

        }

    }


    ptrPsi->unlockMutex();
    return status;




}



int MSPHnOnDemandStreamerSource::getNumOfPids(Psi *ptrPsi)
{
    int pidListSize = 0;

    Pmt *pmt = ptrPsi->getPmtObj();
    uint32_t musicpid = ptrPsi->getMusicPid();
    if (musicpid)
    {
        LOG(DLOGL_REALLY_NOISY, "%s::musicpid is found.. adding count of num of pids", __func__);
        pidListSize += 1;

    }

    if (pmt)
    {
        LOG(DLOGL_REALLY_NOISY, "");
        std::list<tPid> *audioList = pmt->getAudioPidList();
        if (audioList)
        {
            pidListSize = pidListSize + audioList->size();
        }
        std::list<tPid> *videoList = pmt->getVideoPidList();
        if (videoList)
        {
            pidListSize = pidListSize + videoList->size();
        }
        // Increment pidListSize for Clock
        ++pidListSize;


    }
    else
    {
        LOG(DLOGL_ERROR, "%s:Erron in while getting the pointer", __func__);

    }

    return pidListSize;

}


/**
 *   @param CCIbyte       - CCI Bits from CAM module for the current stream
 *
 *   @return None
 *   @brief Function      - Function to inject CCI Bits to the Hn Streaming Session
                            cpe_hnsrvmgr_Set() api is called with the enum eCpeHnSrvMgrGetSetNames_CCIInfo to set the CCI Bits to  the streaming session.
 *   When cpe_hnsrvmgr_set is called,Streaming would be enabled with new CCI bits from the current instance of time
 *
 */

eMspStatus MSPHnOnDemandStreamerSource::InjectCCI(uint8_t CCIbyte)
{
    m_CCIbyte = CCIbyte;


    int ret = cpe_hnsrvmgr_Set(mPgrmHandle, eCpeHnSrvMgrGetSetNames_CCIInfo, &m_CCIbyte, sizeof(CCIbyte));
    if (ret != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "Error in injecting CCI to stream");
        return kMspStatus_Error;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "Injected CCI successfully via the source with CCI value in MSPHnOnDemandStreamerSource %u", m_CCIbyte);
        return kMspStatus_Ok;
    }
}

