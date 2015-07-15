#include <time.h>
#include "MSPMrdvrStreamerSource.h"
#include <cpe_recmgr.h>
#include <sail-clm-api.h>
#include "cpe_hnservermgr.h"

#define SCOPELOG(section, scopename)  dlogns::ScopeLog __xscopelog(section, scopename, __FILE__, __LINE__, DLOGL_FUNCTION_CALLS)
#define FNLOG(section)  dlogns::ScopeLog __xscopelog(section, __PRETTY_FUNCTION__, __FILE__, __LINE__, DLOGL_FUNCTION_CALLS)
#ifdef LOG
#error  LOG already defined
#endif
#define LOG(level, msg, args...)  dlog(DL_MSP_MRDVR, level,"MSPMrdvrStreamerSource:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#define UNUSED_PARAM(a) (void)a;


MSPMrdvrStreamerSource::MSPMrdvrStreamerSource(std::string aSrcUrl)
{
    FNLOG(DL_MSP_MRDVR);
    mSrcUrl = aSrcUrl;
    mSessionId = 0;
    mProgramNumber = 0;
    mSourceId = 0;
    mCpeSrcHandle = 0;
    mSrcStateCB = NULL;
    mClientContext = NULL;
    mPgrmHandle = 0;
    ptrPlaySession = NULL;
    fileChangeCallbackId = 0;
    m_CCIbyte = DEFAULT_RESTRICTIVE_CCI;
}

void MSPMrdvrStreamerSource::SetCpeStreamingSessionID(uint32_t sessionId)
{
    mSessionId = sessionId;
}

MSPMrdvrStreamerSource::~MSPMrdvrStreamerSource()
{
    FNLOG(DL_MSP_MRDVR);
    if (mCpeSrcHandle != 0)
    {
        int cpeStatus = cpe_src_Stop(mCpeSrcHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, " MRDVR server source stop failed with error code %d", cpeStatus);
        }

        cpeStatus = cpe_src_Close(mCpeSrcHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, " MRDVR server source close failed with error code %d", cpeStatus);
        }
        mCpeSrcHandle = 0;
    }
}

eMspStatus MSPMrdvrStreamerSource::load(SourceStateCallback aPlaybackCB, void* aClientContext)
{
    FNLOG(DL_MSP_MRDVR);
    mSrcStateCB = aPlaybackCB;
    mClientContext = aClientContext;

    return kMspStatus_Ok;
}

eMspStatus MSPMrdvrStreamerSource::open(eResMonPriority pri)
{
    FNLOG(DL_MSP_MRDVR);

    (void) pri;  // not used for Server source
    eMspStatus mspStatus = kMspStatus_Ok;
    int cpeStatus = kCpe_NoErr;
    unsigned pos = 0;

    tCpeRecDataBase *dvrBlob = NULL;
    tCpeRecDataBaseType *dvrBlobDbType = NULL;
    tCpeRecDataBaseType *dvrCADbType = NULL;
    eBlobType blob_type;
    uint8_t *CADescBlob = NULL;
    string filepath;

    mPgrmHandle = 0;
    mCpeSrcHandle = 0;

    // Converting SAIL Source URL to CPERP recording URL
    if ((pos = mSrcUrl.find("svfs")) == 0)
    {
        mSrcUrl.replace(mSrcUrl.begin(), mSrcUrl.begin() + 4, "avfs");
    }

    if (mspStatus == kMspStatus_Ok && cpeStatus == kCpe_NoErr)
    {
        cpeStatus = cpe_src_Open(eCpeSrcType_ServerFileSource, &mCpeSrcHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "MRDVR server source Open failed %d\n", -cpeStatus);
        }
    }

    if (mspStatus == kMspStatus_Ok && cpeStatus == kCpe_NoErr)
    {
        //Check whether live streaming or recording streaming
        if ((pos = mSrcUrl.find("avfs://item=live/")) == 0)
        {
            // LIVE STREAMING REQUEST
            // apply the URL directly .
            cpeStatus = cpe_src_Set(mCpeSrcHandle, eCpeSrcNames_URL, (void *) mSrcUrl.c_str());
            if (cpeStatus != kCpe_NoErr)
            {
                LOG(DLOGL_ERROR, "Src set url failed 0x%x\n", -cpeStatus);
                cpe_src_Close(mCpeSrcHandle);
            }
        }
        else
        {
            // RECORDING STREAMING REQUEST
            // check for the URL. If the URL indicates ,its a normal recording,apply the URL directly .
            // or append "avfs:/" first fragment file name path.
            if (!strstr(mSrcUrl.c_str(), "avfs://segmented"))
            {
                cpeStatus = cpe_src_Set(mCpeSrcHandle, eCpeSrcNames_URL, (void *) mSrcUrl.c_str());
            }
            else
            {
                string strUrl = "avfs:/";
                strUrl.append(mSrcUrl.c_str() + 16);
                cpeStatus = cpe_src_Set(mCpeSrcHandle, eCpeSrcNames_URL, (char *)strUrl.c_str());
            }

            if (cpeStatus != kCpe_NoErr)
            {
                LOG(DLOGL_ERROR, "Src set url failed 0x%x\n", -cpeStatus);
                cpe_src_Close(mCpeSrcHandle);
            }
        }
    }

    if (mspStatus == kMspStatus_Ok && cpeStatus == kCpe_NoErr)
    {
        cpeStatus = cpe_hnsrvmgr_Open(mCpeSrcHandle, mSessionId, &mPgrmHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "MRDVR HN server manager  Open failed %d\n", -cpeStatus);
            cpe_src_Close(mCpeSrcHandle);
        }
    }

    if (mspStatus == kMspStatus_Ok && cpeStatus == kCpe_NoErr)
    {
        char *filename = NULL;
        if ((pos = mSrcUrl.find("avfs://item=live/")) == 0)
        {
            /* LIVE STREAMING REQUEST */
            filepath = "";
            filename = (char *)((char *) mSrcUrl.c_str() + 6);
            filepath.append(filename);
            cpeStatus = kCpe_NoErr;		//setting cpeStatus as 0 because it is not a segemented recording.
        }
        else
        {
            /* RECORDING STREAMING REQUEST */
            if (!strstr((char *)mSrcUrl.c_str(), "avfs://segmented"))
            {
                filepath = "";
                filename = (char *)((char *) mSrcUrl.c_str() + 6);
                filepath.append(filename);
                cpeStatus = kCpe_NoErr;		//setting cpeStatus as 0 because it is not a segemented recording.
                LOG(DLOGL_REALLY_NOISY, "Not a Segmented recording \n");
            }
            else
            {
                filepath = "";
                filename = (char *)((char *) mSrcUrl.c_str() + 16);
                filepath.append(filename);
                fileChangeCallbackId = 0;

                cpeStatus = cpe_hnsrvmgr_RegisterSessionCallback(eCpeHnSrvMgrCallbackTypes_FileChange, (void*)this, FileChangeCallback,
                            &fileChangeCallbackId, getCpeProgHandle(), NULL);

                if (kCpe_NoErr != cpeStatus)
                {
                    LOG(DLOGL_ERROR, "File Change Callback Reg Failed :%d \n", cpeStatus);
                }
                LOG(DLOGL_REALLY_NOISY, "Its a Segmented recording , so registered for FileChangeCallback\n");
            }
        }

        mspStatus = ReadMrdvrMetaData((char *)filepath.c_str(), &dvrBlob);
        if (mspStatus != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "HN Srvmgr Open Rec DB Failed 0x%x\n", mspStatus);
        }

    }

    if (mspStatus == kMspStatus_Ok && cpeStatus == kCpe_NoErr)
    {
        dvrBlobDbType = GetDecryptCABlob(dvrBlob , &blob_type);
        if ((dvrBlobDbType == NULL) || (blob_type == kInvalid_Blob))
        {
            LOG(DLOGL_ERROR, "CAM blob fetching from recording failed");
            delete[] dvrBlob;
            dvrBlob = NULL;
            if (dvrBlobDbType)
            {
                delete dvrBlobDbType;
                dvrBlobDbType = NULL;
            }
            return kMspStatus_Error;
        }

        if (blob_type == kRTN_Blob)
        {
            LOG(DLOGL_NOISE, "It's an RTN CA Blob.Hence querying for CA Descriptor from metadata");
            dvrCADbType = GetDecryptCADesc(dvrBlob);
            if (dvrCADbType == NULL)
            {
                LOG(DLOGL_ERROR, "CA Descriptor blob fetching from recording failed");

                delete dvrBlobDbType;
                dvrBlobDbType = NULL;

                delete[] dvrBlob;
                dvrBlob = NULL;

                return kMspStatus_Error;
            }
        }
        else if (blob_type == kSARA_Blob)
        {
            LOG(DLOGL_NOISE, "Its a SARA CA Blob.Hence creating CA descriptor of own");
            CADescBlob = (uint8_t *)calloc(1, CA_DESCRIPTOR_LENGTH);
            dvrCADbType =  new tCpeRecDataBaseType ;
            if ((!dvrCADbType) || (!CADescBlob))
            {
                LOG(DLOGL_ERROR, "Out of memory for CA descriptor creation");

                delete dvrBlobDbType;
                dvrBlobDbType = NULL;

                delete[] dvrBlob;
                dvrBlob = NULL;

                if (CADescBlob)
                {
                    free(CADescBlob);
                    CADescBlob = NULL;
                }

                delete dvrCADbType;
                dvrCADbType = NULL;

                return kMspStatus_Error;
            }

            CADescBlob[0] = CA_DESCRIPTOR_DEFAULT;
            dvrCADbType->dataBuf = CADescBlob;
            dvrCADbType->size = CA_DESCRIPTOR_LENGTH;
        }

        uint8_t scramblingMode = GetScramblingMode(dvrBlob);

        LOG(DLOGL_REALLY_NOISY, "calling CamDecryptionStart with %p size %d, Inject Default CCI now", dvrBlobDbType->dataBuf, dvrBlobDbType->size);
        InjectCCI(DEFAULT_RESTRICTIVE_CCI);  // Inject the default resitrict one, will be overwritten by the real one if any

        mspStatus = CamDecryptionStart(kCpeCam_Dvr_PowerKEYDRM, getCpeProgHandle(), &ptrPlaySession, dvrBlobDbType, dvrCADbType, scramblingMode);
        if (mspStatus != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "Cam Decryption for MRDVR serving for the URL %s failed with error code %d", (char *) mSrcUrl.c_str(), mspStatus);
            delete[] dvrBlob;
            dvrBlob = NULL;
        }
        else             //just free the buffer holding DVR metadata
        {
            LOG(DLOGL_REALLY_NOISY, "Cam Decryption for MRDVR serving started successfully for the URL %s", (char *) mSrcUrl.c_str());
            delete[] dvrBlob;
            dvrBlob = NULL;
        }

        if (dvrBlobDbType)
        {
            if (dvrBlobDbType->dataBuf)
            {
                free(dvrBlobDbType->dataBuf);
                dvrBlobDbType->dataBuf = NULL;
            }

            if (dvrBlobDbType)
            {
                delete dvrBlobDbType;
                dvrBlobDbType = NULL;
            }
        }

        if (dvrCADbType)
        {
            if (dvrCADbType->dataBuf)
            {
                free(dvrCADbType->dataBuf);
                dvrCADbType->dataBuf = NULL;
            }

            if (dvrCADbType)
            {
                delete dvrCADbType;
                dvrCADbType = NULL;
            }
        }
    }

    if (mspStatus != kMspStatus_Ok || cpeStatus != kCpe_NoErr)
    {
        if (cpeStatus == kCpe_DeviceTimeoutErr || cpeStatus == kCpe_InvalidHandleErr)
        {
            LOG(DLOGL_ERROR, "MSPMrdvrStreamerSource::open failed with device time out error: %d ", cpeStatus);
            mspStatus = kMspStatus_TimeOut;
        }
        else
        {
            LOG(DLOGL_ERROR, "MSPMrdvrStreamerSource::open failed with a generic error: %d ", cpeStatus);
            mspStatus = kMspStatus_Error;
        }
    }

    LOG(DLOGL_REALLY_NOISY, "MSPMrdvrStreamerSource::open returning %d <SH = %p PH = %p>\n", mspStatus, mCpeSrcHandle, mPgrmHandle);
    if (dvrBlob)
    {
        delete[] dvrBlob;
        dvrBlob = NULL;
    }

    return mspStatus;
}

eMspStatus MSPMrdvrStreamerSource::start()
{
    // Start the source
    if (mCpeSrcHandle != 0)
    {
        int cpeStatus = kCpe_NoErr;

        cpeStatus = cpe_src_Start(mCpeSrcHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "MRDVR server source start failed 0x%x\n", -cpeStatus);

            if (mPgrmHandle)
            {
                cpe_hnsrvmgr_Close(mPgrmHandle);
                mPgrmHandle = 0;
            }

            if (mCpeSrcHandle)
            {
                cpe_src_Close(mCpeSrcHandle);
                mCpeSrcHandle = 0;
            }
            return kMspStatus_BadSource;
        }

        if (cpeStatus == kCpe_NoErr)
        {
            cpeStatus = cpe_hnsrvmgr_Start(mPgrmHandle);
            if (cpeStatus != kCpe_NoErr)
            {
                LOG(DLOGL_ERROR, "MRDVR HN server start failed 0x%x\n", -cpeStatus);

                if (mPgrmHandle)
                {
                    cpe_hnsrvmgr_Close(mPgrmHandle);
                    mPgrmHandle = 0;
                }

                if (mCpeSrcHandle)
                {
                    cpeStatus = cpe_src_Stop(mCpeSrcHandle);
                    if (cpeStatus != kCpe_NoErr)
                    {
                        LOG(DLOGL_ERROR, "MRDVR HN server cpe_src_Stop failed 0x%x\n", -cpeStatus);
                    }
                    cpe_src_Close(mCpeSrcHandle);
                    mCpeSrcHandle = 0;
                }
                return kMspStatus_BadSource;
            }
        }
        return kMspStatus_Ok;
    }
    else
    {
        LOG(DLOGL_ERROR, "MRDVR server source start returning kMspStatus_StateError <SH = %p PH = %p>\n", mCpeSrcHandle, mPgrmHandle);
        return kMspStatus_StateError;
    }
}

eMspStatus MSPMrdvrStreamerSource::stop()
{
    // Stop the source
    FNLOG(DL_MSP_MRDVR);
    int cpeStatus = kCpe_NoErr;
    eMspStatus status = kMspStatus_Ok;

    if (fileChangeCallbackId)
    {
        cpeStatus = cpe_hnsrvmgr_UnregisterSessionCallback(getCpeProgHandle(), fileChangeCallbackId);
        if (kCpe_NoErr != cpeStatus)
        {
            LOG(DLOGL_ERROR, "File Change Callback un-reg failed :%d ", cpeStatus);
        }
        fileChangeCallbackId = 0;
    }

    status = CamDecryptionStop(ptrPlaySession);
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "HN Srv CAM Stop failed 0x%x\n", status);
    }

    if (mCpeSrcHandle != 0)
    {
        cpeStatus = cpe_src_Stop(mCpeSrcHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "MRDVR server source stop failed with error code %d", cpeStatus);
        }

        if (mPgrmHandle != 0)
        {
            cpeStatus = cpe_hnsrvmgr_Stop(mPgrmHandle);
            if (cpeStatus != kCpe_NoErr)
            {
                LOG(DLOGL_ERROR, "MRDVR HN server stop failed with error code %d", cpeStatus);
            }

            cpeStatus = cpe_hnsrvmgr_Close(mPgrmHandle);
            if (cpeStatus != kCpe_NoErr)
            {
                LOG(DLOGL_ERROR, "MRDVR HN server close failed with error code %d", cpeStatus);
            }
        }

        cpeStatus = cpe_src_Close(mCpeSrcHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "MRDVR source close failed with error code %d", cpeStatus);
        }

        mCpeSrcHandle = 0;
        mPgrmHandle = 0;
    }

    if (cpeStatus != kCpe_NoErr)
    {
        status = kMspStatus_Error;
    }

    return status;
}


tCpeSrcHandle MSPMrdvrStreamerSource::getCpeSrcHandle()const
{
    FNLOG(DL_MSP_MRDVR);
    return mCpeSrcHandle;
}

tCpePgrmHandle MSPMrdvrStreamerSource::getCpeProgHandle()const
{
    FNLOG(DL_MSP_MRDVR);
    return mPgrmHandle;
}

IPlaySession* MSPMrdvrStreamerSource::getCAMPlaySession()
{
    FNLOG(DL_MSP_MRDVR);
    return ptrPlaySession;
}

void MSPMrdvrStreamerSource::setCAMPlaySession(IPlaySession* pPlaysession)
{
    ptrPlaySession = pPlaysession;
}

int MSPMrdvrStreamerSource::getProgramNumber()const
{
    FNLOG(DL_MSP_MRDVR);
    return mProgramNumber;
}

int MSPMrdvrStreamerSource::getSourceId()const
{
    FNLOG(DL_MSP_MRDVR);
    return mSourceId;
}

std::string MSPMrdvrStreamerSource::getSourceUrl()const
{
    FNLOG(DL_MSP_MRDVR);
    return mSrcUrl;
}

std::string MSPMrdvrStreamerSource::getFileName()const
{
    FNLOG(DL_MSP_MRDVR);
    return mFileName;
}

bool MSPMrdvrStreamerSource::isDvrSource()const
{
    FNLOG(DL_MSP_MRDVR);
    return false;
}

bool MSPMrdvrStreamerSource::canRecord()const
{
    FNLOG(DL_MSP_MRDVR);
    return false;
}

eMspStatus MSPMrdvrStreamerSource::setSpeed(tCpePlaySpeed aPlaySpeed, uint32_t aNpt)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(aPlaySpeed)
    UNUSED_PARAM(aNpt)
    return kMspStatus_Error;
}

eMspStatus MSPMrdvrStreamerSource::getPosition(float *pNptTime)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(pNptTime)
    return kMspStatus_Error;
}

eMspStatus MSPMrdvrStreamerSource::setPosition(float aNptTime)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(aNptTime)
    return kMspStatus_Error;
}

bool MSPMrdvrStreamerSource::isPPV(void)
{
    FNLOG(DL_MSP_MRDVR);
    return false;
}

bool MSPMrdvrStreamerSource::isQamrf(void)
{
    FNLOG(DL_MSP_MRDVR);
    return false;
}

// Release the streaming source
eMspStatus MSPMrdvrStreamerSource::release()
{
    FNLOG(DL_MSP_MRDVR);
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

eMspStatus MSPMrdvrStreamerSource::CamDecryptionStart(uint32_t drmSystemID, tCpePgrmHandle pgrmPlayHandle,
        IPlaySession **dPtrPlaySession,
        tCpeRecDataBaseType *pDvrBlob, tCpeRecDataBaseType *pDvrCaDesc, uint8_t  scramblingMode)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(drmSystemID)
    IPlaySession *ptrPlaySession = NULL;

    // Create PlaySession object here.
    int err = Cam::getInstance()->createPlaySession(&ptrPlaySession);      // TODO: need to check return here
    if (err != 0 || !ptrPlaySession)    // not error
    {
        LOG(DLOGL_ERROR, " Error: %d initDecryptSession on mPtrPlaySession: %p", err, ptrPlaySession);
        return kMspStatus_Error;
    }

    // add dvr ca descriptor to CAM playsession
    err = Cam::getInstance()->addPsiData(*ptrPlaySession, pDvrCaDesc->dataBuf, pDvrCaDesc->size, scramblingMode);
    if (err == 0)
    {
        // add dvr meta-data to CAM playsession
        err =  ptrPlaySession->addDVRmetadata(pDvrBlob->dataBuf, pDvrBlob->size);
    }
    else
    {
        LOG(DLOGL_ERROR, " Error: %d addDVRmetadata ", err);
        return kMspStatus_Error;
    }

    if (err == 0)
    {
        // add prog handle
        err = ptrPlaySession->addProgramHandle(pgrmPlayHandle);
    }
    else
    {
        LOG(DLOGL_ERROR, " Error: %d addProgramHandle ", err);
        return kMspStatus_Error;
    }

    *dPtrPlaySession = ptrPlaySession;

    return kMspStatus_Ok;
}

eMspStatus MSPMrdvrStreamerSource::CamDecryptionStop(IPlaySession *ptrPlaySession)
{
    if (NULL != ptrPlaySession)
    {
        LOG(DLOGL_REALLY_NOISY, "CamDecryptionStop ptrPlaySession->shutdown \n");
        ptrPlaySession->shutdown();
        ptrPlaySession = NULL;
    }
    return kMspStatus_Ok;
}

int MSPMrdvrStreamerSource::FileChangeCallback(tCpeHnSrvMgrCallbackTypes type, void *userdata, void *pCallbackSpecific)
{
    FNLOG(DL_MSP_MRDVR);

    tCpeRecDataBaseType *dvrBlobDbType = NULL;
    tCpeRecDataBaseType *dvrCADbType = NULL;
    MSPMrdvrStreamerSource *session = NULL;
    eMspStatus status = kMspStatus_Ok;
    eBlobType blob_type;
    tCpeRecDataBase *dvrBlob = NULL;
    uint8_t *CADescBlob = NULL;
    int ret = -1;
    if ((type == eCpeHnSrvMgrCallbackTypes_FileChange))
    {
        char *nextFileName = (char *)pCallbackSpecific;
        session = (MSPMrdvrStreamerSource *)userdata;
        LOG(DLOGL_REALLY_NOISY, ":%s nextFileName: %s  session: %p \n", __FUNCTION__, nextFileName, session);
        if (session && nextFileName)
        {
            IPlaySession* playSession = session->getCAMPlaySession();
            status = session->CamDecryptionStop(playSession);
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "HN Srv CAM Stop failed 0x%x\n", status);
            }

            status = ReadMrdvrMetaData(nextFileName + 6, &dvrBlob);
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "HN Srvmgr Open Rec DB Failed 0x%x\n", status);
            }
            else
            {
                dvrBlobDbType = GetDecryptCABlob(dvrBlob , &blob_type);
                if ((dvrBlobDbType == NULL) || (blob_type == kInvalid_Blob))
                {
                    LOG(DLOGL_ERROR, "CAM blob fetching from recording failed");
                }

                if (blob_type == kRTN_Blob)
                {
                    LOG(DLOGL_REALLY_NOISY, "It's an RTN CA Blob.Hence querying for CA Descriptor from metadata");
                    dvrCADbType = GetDecryptCADesc(dvrBlob);
                    if (dvrCADbType == NULL)
                    {
                        LOG(DLOGL_ERROR, "CA Descriptor blob fetching from recording failed");
                    }
                }
                else if (blob_type == kSARA_Blob)
                {
                    LOG(DLOGL_REALLY_NOISY, "Its a SARA CA Blob.Hence creating CA descriptor of own");
                    CADescBlob = (uint8_t *)calloc(1, CA_DESCRIPTOR_LENGTH);
                    dvrCADbType =  new tCpeRecDataBaseType ;
                    if ((!dvrCADbType) || (!CADescBlob))
                    {
                        LOG(DLOGL_EMERGENCY, "Out of memory for CA descriptor creation");
                    }
                    else
                    {
                        CADescBlob[0] = CA_DESCRIPTOR_DEFAULT;
                        dvrCADbType->dataBuf = CADescBlob;
                        dvrCADbType->size = CA_DESCRIPTOR_LENGTH;
                    }
                }

                uint8_t scramblingMode = GetScramblingMode(dvrBlob);

                if (dvrBlobDbType != NULL)
                {
                    LOG(DLOGL_REALLY_NOISY, "calling CamDecryptionStart with %p size %d", dvrBlobDbType->dataBuf, dvrBlobDbType->size);
                }

                IPlaySession* pPlaySession = NULL;

                status = session->CamDecryptionStart(kCpeCam_Dvr_PowerKEYDRM, session->getCpeProgHandle(), &pPlaySession, dvrBlobDbType, dvrCADbType, scramblingMode);
                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "Cam Decryption for MRDVR serving for the filename %s failed with error code %d", nextFileName, status);
                    delete[] dvrBlob;
                    dvrBlob = NULL;
                }
                else             //just free the buffer holding DVR metadata
                {
                    session->setCAMPlaySession(pPlaySession);

                    LOG(DLOGL_REALLY_NOISY, "Cam Decryption for MRDVR serving started successfully for filename: %s", nextFileName);
                    delete[] dvrBlob;
                    dvrBlob = NULL;
                }

                if (dvrBlobDbType)
                {
                    if (dvrBlobDbType->dataBuf)
                    {
                        free(dvrBlobDbType->dataBuf);
                        dvrBlobDbType->dataBuf = NULL;
                    }
                    delete dvrBlobDbType;
                    dvrBlobDbType = NULL;
                }
                if (dvrCADbType)
                {
                    if (dvrCADbType->dataBuf)
                    {
                        free(dvrCADbType->dataBuf);
                        dvrCADbType->dataBuf = NULL;
                    }
                    delete dvrCADbType;
                    dvrCADbType = NULL;
                }
            }
            ret = 0;
        }
        else
        {
            LOG(DLOGL_ERROR , "Session pointer is invalid ");
        }
    }
    LOG(DLOGL_NOISE , "%s return status: %d", __FUNCTION__, ret);

    return ret;
}


eMspStatus MSPMrdvrStreamerSource::InjectCCI(uint8_t CCIbyte)
{
    m_CCIbyte = CCIbyte;


#if PLATFORM_NAME == G8 || PLATFORM_NAME == IP_CLIENT
    int ret = cpe_hnsrvmgr_Set(mPgrmHandle, eCpeHnSrvMgrGetSetNames_CCIInfo, &m_CCIbyte, sizeof(CCIbyte));
    if (ret != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "Error in injecting CCI to stream");
        return kMspStatus_Error;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "Injected CCI successfully via the source with CCI value in MSPMrdvrStreamerSource %u", m_CCIbyte);
        return kMspStatus_Ok;
    }
#endif
#if PLATFORM_NAME == G6
    LOG(DLOGL_ERROR, "cpe_hnsrvmgr_Set is not supported for G6 as of now so returning error");
    return kMspStatus_Error;
#endif
}


