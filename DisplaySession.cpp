/**
   \file DisplaySession.cpp
   \class DisplaySession

Implementation file for DisplaySession class
*/


///////////////////////////////////////////////////////////////////////////
//                          Includes
///////////////////////////////////////////////////////////////////////////

#include "DisplaySession.h"
#include "languageSelection.h"
#include "avpm.h"
#include "IMediaController.h"
#include "MusicAppData.h"
#include "BaseAppData.h"
#include <cpe_error.h>
#include <cpe_cam.h>
#include <dlog.h>
#include <misc_platform.h>

#include "dvr.h"

#if defined(DMALLOC)
#include "dmalloc.h"
#endif

///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

#define UNUSED_PARAM(a) (void)a;


#ifdef LOG
#error  LOG already defined
#endif
#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"DisplaySession:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

#define NULL_PID    0x1fff

static void caEntitlemntCallback(void *pData, camEntitlementStatus entStatus);

static int mediaMediaCallback(tCpeMediaCallbackTypes type, void* userdata, void* pCallbackSpecific)
{
    FNLOG(DL_MSP_MPLAYER);
    UNUSED_PARAM(pCallbackSpecific);
    LOG(DLOGL_MINOR_EVENT, "****CB type %d*****", type);
    if (type == eCpeMediaCallbackType_FirstFrameAlarm)
    {
        LOG(DLOGL_MINOR_EVENT, "****First frame alarm*****");
    }

    DisplaySession* p = (DisplaySession*) userdata;
    if (p)
    {
        p->performCb(type);
    }

    return 0;
}

eMspStatus getCSD(Psi* psi, tCpePgrmHandleMpegDesc * pCcDescr)
{
    eMspStatus status = kMspStatus_Ok;
    tPid videoPidStruct = {0, 0};

    if ((pCcDescr == NULL) || (psi == NULL) || (psi->getPmtObj() == NULL))
    {
        LOG(DLOGL_ERROR, "pCcDescr=%p, psi=%p, psi->getPmtObj()=%p", pCcDescr, psi, (psi) ? (psi->getPmtObj()) : NULL);
        return kMspStatus_Error;
    }

    std::list<tPid>* videoList = psi->getPmtObj()->getVideoPidList();

    int n = videoList->size();

    if (n == 0)
    {
        LOG(DLOGL_ERROR, "No video pid");
        pCcDescr->tag = CAPTION_SERVICE_DESCR_TAG;
        pCcDescr->dataLen = 0;
        pCcDescr->data = NULL;
        status = psi->getPmtObj()->getDescriptor(pCcDescr, kPid);
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): KPid: has CC ccDescr.dataLen %d \n", __FUNCTION__, __LINE__, pCcDescr->dataLen);

    }
    else
    {
        LOG(DLOGL_MINOR_DEBUG, "number of video pid in the list is %d", n);
        std::list<tPid>::iterator iter = videoList->begin();
        //Choose the first one, don't know what to do if we have more than one video pid
        videoPidStruct = (tPid) * iter;

        //tCpePgrmHandleMpegDesc ccDescr;
        pCcDescr->tag = CAPTION_SERVICE_DESCR_TAG;
        pCcDescr->dataLen = 0;
        pCcDescr->data = NULL;
        // unsigned char noOfServices = 0;

        eMspStatus status = psi->getPmtObj()->getDescriptor(pCcDescr, videoPidStruct.pid);

        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): has CC ccDescr.dataLen %d ", __FUNCTION__, __LINE__, pCcDescr->dataLen);
//Fix for CDET CSCur14615 :  Num of bytes (pCcDescr->dataLen - 1) should always be a multiple of 6 bytes as per the spec.
//If it is not a multiple of 6, then the CC stream is not as per the spec and no further processing will be done.
//This will prevent reboots and buffer overruns.
        if ((pCcDescr->dataLen - 1) % 6 != 0)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s(%d): CC ccDescr.dataLen does not have valid number of bytes ", __FUNCTION__, __LINE__);
            status = kMspStatus_Error;
            return status;
        }

        if (status != kMspStatus_Ok || pCcDescr->dataLen <= 3)
        {
            //No descriptor found with Video PID. Try 0x1fff
            psi->getPmtObj()->releaseDescriptor(pCcDescr);
            pCcDescr->dataLen = 0;
            pCcDescr->data = NULL;
            status = psi->getPmtObj()->getDescriptor(pCcDescr, kPid);
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s(%d): KPid: has CC ccDescr.dataLen %d \n", __FUNCTION__, __LINE__, pCcDescr->dataLen);
        }

        if (status == kMspStatus_Ok && pCcDescr->dataLen < 3)
        {
            status = kMspStatus_Error;
        }
    }
    return status;
}

// Controller inform displaysession that PMT change with no AV pid change
// Need to check if CSP changes
eMspStatus DisplaySession::PmtRevUpdated(Psi* psi)
{
    FNLOG(DL_MSP_MPLAYER);
    if (psi == NULL)
    {
        return kMspStatus_Error;
    }

    eMspStatus status = kMspStatus_Ok;

    if (mState == kDisplaySessionIdle
            || mState == kDisplaySessionOpened
            || mState == kDisplaySessionStarted
            || mState ==  kDisplaySessionClosed)
    {
        tCpePgrmHandleMpegDesc ccDescr;

        status = getCSD(psi, &ccDescr);
        if (status == kMspStatus_Ok)
        {
            unsigned int newCrc = psi->crc32(0xFFFFFFFF, (char *)ccDescr.data, ccDescr.dataLen);
            if (newCrc != mCSDcrc)  // CSD changes
            {
                status = formulateCCLanguageList(psi);
            }
        }
        if (psi->getPmtObj())
        {
            psi->getPmtObj()->releaseDescriptor(&ccDescr);
        }
    }
    return status;
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

eMspStatus DisplaySession::formulateCCLanguageList(Psi* psi)
{
    FNLOG(DL_MSP_MPLAYER);

    eMspStatus status = kMspStatus_Ok;
    unsigned char noOfServices = 0;
    tCpePgrmHandleMpegDesc ccDescr;

    status = getCSD(psi, &ccDescr);
    if (status == kMspStatus_Ok)
    {
        mCSDcrc = psi->crc32(0xFFFFFFFF, (char *)ccDescr.data, ccDescr.dataLen);
    }

    Avpm *inst = Avpm::getAvpmInstance();
    inst->mCCLanguageStreamMap.clear();

    if (status == kMspStatus_Ok && ccDescr.dataLen >= 3)
    {
        uint8_t *buff = ccDescr.data;
        caption_service_descriptor *stCsd = (caption_service_descriptor *) buff;
        unsigned char reserved1 = stCsd->reserved;
        int count_setBits_reserved1 = countSetBits(reserved1);
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): reserved1: = %d No of 1's in reserved1 = %d", __FUNCTION__, __LINE__, reserved1, count_setBits_reserved1);
        if (count_setBits_reserved1 != 3) 	//reserved1 has 3 bits. Checking if all reserved1 bits are 1 or not.
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d): All reserved bits in reserved1 are not equal to 1. Error in Stream.  ", __FUNCTION__, __LINE__);
            status = kMspStatus_Error;
            return status;
        }
        noOfServices = stCsd->number_of_services;
        if (((noOfServices * 6) + 1) != (int)ccDescr.dataLen)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d): Number of services in the stream has inappropriate number of bytes in the stream...Error in Stream.. Returning kMspStatus_Error ", __FUNCTION__, __LINE__);
            status = kMspStatus_Error;
            return status;
        }

        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): noOfServices: %d stCsd->number_of_services: %d & buff[0] %02x \n", __FUNCTION__, __LINE__, noOfServices, stCsd->number_of_services, buff[0]);

        for (unsigned int i = 0; i < noOfServices ; i++)
        {
            caption_service_entry *stCse = (caption_service_entry *) &buff[1];
            unsigned char reserved2 = stCse->reserved;
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): reserved2: = %d ", __FUNCTION__, __LINE__, reserved2);
            if (reserved2 != 1) //reserved2 has 1 bit.Checking if reserved2 bit is 1 or not.
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d): Reserved bit in reserved2 is not equal to 1. Error in Stream.  ", __FUNCTION__, __LINE__);
                status = kMspStatus_Error;
                return status;
            }


            if (true == stCse->digital_cc)
            {
                char langCode[CAPTION_LANG_SIZE] = {0};
                snprintf(langCode, CC_LANG_BCK_COMP_ISO_LENGTH + 1, "%s%c%c%c", BACKWARD_COMPATIBLE_STR, stCse->language[0], stCse->language[1], stCse->language[2]);
                string insertString = langCode;
                caption_easy_reader *stCer = (caption_easy_reader *)(buff + CAPTION_SERVICE_OFFSET);
                bitswap16((uint8_t*)stCer);
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, " EZ_R = %d  ", stCer->easy_reader);
                if (stCer->easy_reader)
                {
                    insertString += EZ_READER_SUFFIX;

                }
                uint16_t reserved3 = stCer->reserved1;
                int count_setBits_reserved3 = countSetBits(reserved3);
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): reserved3: = %d No of 1's in reserved3 = %d", __FUNCTION__, __LINE__, reserved3, count_setBits_reserved3);
                if (count_setBits_reserved3 != 14) //reserved3 has 14 bits. Checking if all reserved3 bits are 1 or not.
                {
                    dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d): All reserved bits in reserved3 are not equal to 1. Error in Stream.  ", __FUNCTION__, __LINE__);
                    status = kMspStatus_Error;
                    return status;
                }

                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Lang:%s d_cc %d EZ_R %d  captionServiceNumber %d ", insertString.c_str(), stCse->digital_cc, stCer->easy_reader, stCse->captionServiceNumber);

                inst->mCCLanguageStreamMap.insert(std::make_pair(insertString, stCse->captionServiceNumber));


            }
            buff += CAPTION_SERVICE_SKIP_LENGTH;

        }
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Language Descriptor length %d\n", ccDescr.dataLen);
    }
    if (psi->getPmtObj())
    {
        psi->getPmtObj()->releaseDescriptor(&ccDescr);
    }

    return status;
}

eMspStatus DisplaySession::formulateAudioPidTable(Psi* psi)
{
    FNLOG(DL_MSP_MPLAYER);
    eMspStatus status = kMspStatus_Ok;

    LanguageSelection langSelect(LANG_SELECT_AUDIO, psi);
    tPid audioPidStruct = langSelect.pidSelected();

    mTsParams.pidTable.audioPid = audioPidStruct.pid;
    mTsParams.pidTable.audioStreamType = audioPidStruct.streamType;
    LOG(DLOGL_MINOR_DEBUG, "audio pid 0x%x, streamType %d", mTsParams.pidTable.audioPid, mTsParams.pidTable.audioStreamType);
    if (mPtrPlaySession)
    {
        LOG(DLOGL_REALLY_NOISY, "add stream. pid %d", audioPidStruct.pid);
        mPtrPlaySession->addStream(audioPidStruct.pid);
    }

    return status;
}

eMspStatus DisplaySession::formulateVideoPidTable(Psi *psi, Pmt* pmt)
{
    FNLOG(DL_MSP_MPLAYER);

    eMspStatus status = kMspStatus_Ok;
    tCpePgrmHandleMpegDesc cakDescriptor;
    tCpePgrmHandleMpegDesc cakSystemDescriptor;
    tPid videoPidStruct = {0, 0};
    uint8_t *caDescriptorAddress = NULL;
    size_t caDescriptorLength;
    uint32_t musicpid = psi->getMusicPid(); // obtain DMX private data pid

    std::list<tPid>* videoList = pmt->getVideoPidList();

    //get pcr pid first
    mTsParams.pidTable.clockPid = pmt->getPcrpid();
    LOG(DLOGL_MINOR_DEBUG, "clockPid or PCR pid 0x%x", mTsParams.pidTable.clockPid);

    // Adding CA descriptor for encrypted DMX music channel is done
    // here because this is the place where the code checksl for no video
    // pid and the argument Pmt *pmt is available as opposed to
    // formulateAudioPidTable() where there is no Pmt *pmt and
    // there is no knowledge whether video pid exists
    int n = videoList->size();
    if (n == 0)
    {
        LOG(DLOGL_ERROR, "No video pid");
        if (musicpid) // DMX music channel - need to add CA descriptor
        {
            videoPidStruct.pid = musicpid;
        }
        else
        {
            mTsParams.pidTable.videoPid = NULL_PID;
            mTsParams.pidTable.videoStreamType = 0;
        }

    }
    else
    {
        LOG(DLOGL_MINOR_DEBUG, "number of video pid in the list is %d", n);
        std::list<tPid>::iterator iter = videoList->begin();
        //Choose the first one, don't know what to do if we have more than one video pid
        videoPidStruct = (tPid) * iter;
        mTsParams.pidTable.videoPid = videoPidStruct.pid;
        mTsParams.pidTable.videoStreamType = videoPidStruct.streamType;

        LOG(DLOGL_MINOR_DEBUG, "video pid %d, streamType %d, pcr pid 0x%x",
            mTsParams.pidTable.videoPid, mTsParams.pidTable.videoStreamType, mTsParams.pidTable.clockPid);
    }

    // now tell CAM play session about PID's etc
    if (mPtrPlaySession)
    {
        // find CA descriptor tag
        // get the CA descriptor from the PMT
        cakDescriptor.tag = 0x9;
        cakDescriptor.dataLen = 0;
        cakDescriptor.data = NULL;

        caDescriptorAddress = NULL;
        caDescriptorLength = 0;
        status = psi->getPmtObj()->getDescriptor(&cakDescriptor, videoPidStruct.pid);
        if (status != kMspStatus_Ok)
        {
            // no CA descriptor found in PMT, see if there is one from metadata (i.e. clear to encrypted transcription)
            LOG(DLOGL_NOISE, "CA descriptor not found in PMT: status: %d ", status);
            caDescriptorAddress = psi->getCADescriptorPtr();
            caDescriptorLength = psi->getCADescriptorLength();
            LOG(DLOGL_REALLY_NOISY, "CA descriptor from CA metadata:%p %d ", caDescriptorAddress, caDescriptorLength);
            status = kMspStatus_Ok;         // fixup status for return since this is not really an error
            mScramblingMode = 0;
        }
        else
        {
            mCaSystem = (cakDescriptor.data[0] << 8) + cakDescriptor.data[1];
            mCaPid = ((cakDescriptor.data[2] << 8) + cakDescriptor.data[3]) & 0x1fff;
            caDescriptorAddress = cakDescriptor.data;
            caDescriptorLength = static_cast<size_t>(cakDescriptor.dataLen);
            LOG(DLOGL_REALLY_NOISY, "CA Descriptor: ecmpid: 0x%x sys: 0x%x len: %d: ",
                mCaPid, mCaSystem, cakDescriptor.dataLen);

            // now get the CA system descriptor
            cakSystemDescriptor.tag = 0x65;
            cakSystemDescriptor.dataLen = 0;
            cakSystemDescriptor.data = NULL;

            status = psi->getPmtObj()->getDescriptor(&cakSystemDescriptor, videoPidStruct.pid);
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_NOISE, "Get CA System descriptor error: %d ", status);
                status = kMspStatus_Ok;         // fixup status for return since this is not really an error
                mScramblingMode = 0;
            }
            else
            {
                mScramblingMode = cakSystemDescriptor.data[0];
                psi->getPmtObj()->releaseDescriptor(&cakSystemDescriptor);
            }
        }
        if (NULL != caDescriptorAddress)
        {

            // the source is encrypted
            LOG(DLOGL_REALLY_NOISY, "CA System addPsiData: %p len %d scrammode: 0x%x descdata %x %x %x %x",
                caDescriptorAddress, caDescriptorLength, mScramblingMode, caDescriptorAddress[0],
                caDescriptorAddress[1], caDescriptorAddress[2], caDescriptorAddress[3]);
            Cam::getInstance()->addPsiData(*mPtrPlaySession, caDescriptorAddress, caDescriptorLength, mScramblingMode);

            LOG(DLOGL_REALLY_NOISY, "CA System addDVRMetaData: %p len %d", psi->getCAMetaDataPtr(), psi->getCAMetaDataLength());
            mPtrPlaySession->addDVRmetadata(psi->getCAMetaDataPtr(), psi->getCAMetaDataLength());
            LOG(DLOGL_REALLY_NOISY, "CA System addStream %d", videoPidStruct.pid);
            mPtrPlaySession->addStream(videoPidStruct.pid);
            psi->getPmtObj()->releaseDescriptor(&cakDescriptor);
        }
        else
        {
            // the source is in the clear

            LOG(DLOGL_NOISE, "CA Descriptor address is NULL");
            LOG(DLOGL_REALLY_NOISY, "CA System addPsiData: %p len %d scrammode: 0x%x",
                caDescriptorAddress, caDescriptorLength, mScramblingMode);
            if (musicpid == 0)
            {
                // only need this if this is NOT a DMX music channel
                // otherwise clear music data pid disappears into the clear CAK
                // and cannot be retrieved
                Cam::getInstance()->addPsiData(*mPtrPlaySession, NULL, 0, mScramblingMode);
                mPtrPlaySession->addStream(videoPidStruct.pid);
            }
        }

    }
    mTsParams.progNumber = pmt->getProgramNumber();
    mTsParams.transportID = pmt->getTransportID();
    // TODO: What is the amolPid - is setting to 0 correct?
    mTsParams.pidTable.amolPid = 0;
    dlog(DL_MSP_MPLAYER, DLOGL_MINOR_DEBUG, "DisplaySession::%s:%d programNumber %d, transportID %d", __FUNCTION__, __LINE__, mTsParams.progNumber, mTsParams.transportID);

    return status;
}
eMspStatus DisplaySession::SapChangedCb()
{
    FNLOG(DL_MSP_MPLAYER);
    if (mSourcetype == kMspSrcTypeAnanlog)
    {
        eUseSettingsLevel settingLevel1, settingLevel2;
        char sap[20] = {0};
        char sapSupport[20] = {0};
        eUse_StatusCode eUseStatus = USE_RESULT_OK;

        eUseStatus =  Uset_getSettingT(NULL, "ciscoSg/audio/sap", sizeof(sap), sap, &settingLevel1);
        LOG(DLOGL_NORMAL, "Usetting return %d, value %s, for ciscoSg/audio/sap", eUseStatus, sap);

        eUseStatus = Uset_getSettingT(NULL, "ciscoSg/audio/sapSuppt", sizeof(sapSupport), sapSupport, &settingLevel2);
        LOG(DLOGL_NORMAL, "Usetting return %d, value %s, for ciscoSg/audio/sapSuppt", eUseStatus, sapSupport);

        uint32_t sapaudio = 0;
        if ((strcasecmp(sap, "true") == 0 || strcasecmp(sap, "Enabled") == 0))   //&& strcasecmp(sapSupport, "true") == 0 )
        {
            sapaudio = 1;
        }
        int   err = cpe_media_Set(mMediaHandle, eCpeMediaGetSetNames_EnableAnalogSAPAudio, &sapaudio);
        LOG(DLOGL_NORMAL, "err = %d calling cpe_media_Set with sapaudio %d", err, sapaudio);
        if (!err && sapaudio)
        {
            Avpm::getAvpmInstance()->setAvpmSapEnabled(true);
        }
        else
        {
            Avpm::getAvpmInstance()->setAvpmSapEnabled(false);
        }
    }
    return kMspStatus_Ok;
}
///////////////////////////////////////////////////////////////////////////
//                      Member implementation
///////////////////////////////////////////////////////////////////////////

/** *********************************************************
 */
eMspStatus DisplaySession::updatePids(Psi* psi)
{
    FNLOG(DL_MSP_MPLAYER);

    if (!psi)
    {
        LOG(DLOGL_ERROR, "Error: null psi");
        return kMspStatus_BadParameters;
    }

    psi->lockMutex();

    LOG(DLOGL_REALLY_NOISY, "mState: %d", mState);

    eMspStatus status = kMspStatus_Ok;

    if (mState == kDisplaySessionIdle
            || mState == kDisplaySessionOpened
            || mState == kDisplaySessionStarted
            || mState ==  kDisplaySessionClosed)
    {
        Pmt *pmt = psi->getPmtObj();
        mPgmNo = psi->getProgramNo();

        if (!mPtrPlaySession)
        {
            // TODO: verify OK for this to be null for all the above states
            status = initializePlaySession();
        }

        if (status == kMspStatus_Ok && pmt)
        {
            // TODO: Investigate a separate PMT object that knows how to display itself
            //       If we already have pmt, why does it need mPmtInfo??
            //       This makes a copy of info
            status = pmt->getPmtInfo(&mPmtInfo);   // fill pmt info

            LOG(DLOGL_NOISE, "****PMT Info: vers %d clkpid %x pgmCnt %d escnt %d ppgmdesc %p pesdat %p",
                mPmtInfo.versionNumber, mPmtInfo.clockPid, mPmtInfo.pgmDescCount, mPmtInfo.esCount, mPmtInfo.ppPgmDesc, mPmtInfo.ppEsData);

            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "getPmtInfo error: %d", status);
                status = kMspStatus_PsiError;
            }
            else
            {
                status = formulateVideoPidTable(psi, pmt);
                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "formulateVideoPidTable error %d", status);
                }

                status = formulateCCLanguageList(psi);
                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_REALLY_NOISY, "formulateCCLanguageList. May be the CSD is not available. Error: %d", status);
                }


                // for 2nd stream ,need to zero AMOL pid to avoid resource conflict
                if (!mAudioFocus)
                {
                    /* PIP Session so we should not set Audio PID */
                    LOG(DLOGL_NOISE, "PIP Audio");
                    mTsParams.fullRes = false;  // make sure
                    mTsParams.pidTable.amolPid = 0;
                    mTsParams.pidTable.audioPid = 0;  // CPERP expect 0, not NULL_PID
                }
                else
                {
                    LOG(DLOGL_NOISE, "Main Audio");
                    status = formulateAudioPidTable(psi);
                    if (status != kMspStatus_Ok)
                    {
                        LOG(DLOGL_ERROR, "formulateAudioPidTable error %d", status);
                    }
                }

                if (mState == kDisplaySessionStarted)
                {
                    tCpeMediaUpdatePids updatedPids;
                    updatedPids.pidTable = mTsParams.pidTable;
                    updatedPids.flag = mDecoderFlag;
                    int err = cpe_media_Set(mMediaHandle, eCpeMediaGetSetNames_UpdatePids, (void *)&updatedPids);
                    if (err != kCpe_NoErr)
                    {
                        LOG(DLOGL_ERROR, "Error set eCpeMediaGetSetNames_UpdatePids rc=%d", err);
                        status = kMspStatus_CpeMediaError;
                    }

                    err = cpe_ProgramHandle_Set(mMediaHandle, eCpePgrmHandleNames_Pmt, (void *)&mPmtInfo);
                    if (err != kCpe_NoErr)
                    {
                        LOG(DLOGL_ERROR, "Error set eCpePgrmHandleNames_Pmt rc=%d", err);
                        status = kMspStatus_CpeMediaError;
                    }

                    uint32_t progNum = mPgmNo;

                    err = cpe_ProgramHandle_Set(mMediaHandle, eCpePgrmHandleNames_ProgramNumber, (void *)&progNum);
                    if (err != kCpe_NoErr)
                    {
                        LOG(DLOGL_ERROR, "Error set eCpePgrmHandleNames_ProgramNumber rc=%d", err);
                        status = kMspStatus_CpeMediaError;
                    }
                    else
                        LOG(DLOGL_NORMAL, "Set Program No %d, handle %p", progNum, mMediaHandle);
                }
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Error: null pmt object");
            status = kMspStatus_PsiError;
        }
    }
    else
    {
        status = kMspStatus_StateError;
        LOG(DLOGL_ERROR, "Error: invalid state");
    }

    psi->unlockMutex();

    return  status;
}

/** *********************************************************
 */
eMspStatus DisplaySession::setVideoWindow(const DFBRectangle rect, bool audioFocus)
{
    FNLOG(DL_MSP_MPLAYER);

    eMspStatus status = kMspStatus_Ok;

    mRect = rect;
    mAudioFocus = audioFocus;
    mWindowSetPending = true;

    LOG(DLOGL_REALLY_NOISY, "rect %d %d %d %d audio:%d", rect.x, rect.y, rect.w, rect.h, audioFocus);

    if (mAudioFocus)
    {
        //Main TV
        LOG(DLOGL_MINOR_DEBUG, "Foreground session");
        mTsParams.fullRes = true;
    }
    else
    {
        //Pip TV
        LOG(DLOGL_MINOR_DEBUG, "Background session");
        mTsParams.fullRes = false;
        mTsParams.pidTable.amolPid = 0;
        mTsParams.pidTable.audioPid = 0;  // CPERP expect 0, not NULL_PID
    }

    if (mState == kDisplaySessionStarted)
    {
        status = Avpm::getAvpmInstance()->setPresentationParams(mMediaHandle, rect, audioFocus);
        if (status == kMspStatus_Ok)
        {
            mWindowSetPending = false;
        }
        else
        {
            LOG(DLOGL_ERROR, "setPresentationParams error %d", status);
        }
    }
    else
    {
        LOG(DLOGL_NOISE, "not setting params yet, current state %d, ", mState);
        status = kMspStatus_StateError;
    }

    return  status;
}



void DisplaySession::LogMediaTsParams(tCpeMediaTransportStreamParam *tsParams)
{
    if (tsParams)
    {
        LOG(DLOGL_NOISE, " -progNumber:      %d", tsParams->progNumber);
        LOG(DLOGL_NOISE, " -transportID:     %d", tsParams->transportID);
        LOG(DLOGL_NOISE, " -fullRes:         %d", tsParams->fullRes);
        LOG(DLOGL_NOISE, " -progNumber:      %d", tsParams->progNumber);
        LOG(DLOGL_NOISE, " -clockPid:      0x%x", tsParams->pidTable.clockPid);
        LOG(DLOGL_NOISE, " -videoPid:      0x%x", tsParams->pidTable.videoPid);
        LOG(DLOGL_NOISE, " -videoStreamType: %d", tsParams->pidTable.videoStreamType);
        LOG(DLOGL_NOISE, " -audioPid:      0x%x", tsParams->pidTable.audioPid);
        LOG(DLOGL_NOISE, " -audioStreamType: %d", tsParams->pidTable.audioStreamType);
        LOG(DLOGL_NOISE, " -amolPid:       0x%x", tsParams->pidTable.amolPid);
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: Null tsParams");
    }
}


/** *********************************************************
  /returns
 */
eMspStatus DisplaySession::open(const MSPSource *aSource)
{
    FNLOG(DL_MSP_MPLAYER);

    if (!aSource)
    {
        LOG(DLOGL_ERROR, "error null aSource");
        return kMspStatus_BadParameters;
    }

    mSrcHandle = aSource->getCpeSrcHandle();

    if (!mSrcHandle)
    {
        LOG(DLOGL_ERROR, "error null mSrcHandle");
        return kMspStatus_BadParameters;
    }


    if (mState != kDisplaySessionIdle && mState != kDisplaySessionClosed)
    {
        LOG(DLOGL_ERROR, "error bad state: %d", mState);
        return kMspStatus_StateError;
    }

    eMspStatus status = kMspStatus_Ok;
    mSourcetype = kMspSrcTypeDigital;

    int err = cpe_media_Open(mSrcHandle, eCpeMediaStreamTypes_TransportStream, &mMediaHandle);
    if (err != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "cpe_media_Open  error %d, with source handle %d", err, (int) mSrcHandle);
        status = kMspStatus_CpeMediaError;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "cpe_media_Open success!");
        LOG(DLOGL_REALLY_NOISY, "Change state to %d, previous state: %d", kDisplaySessionOpened, mState);
        mState = kDisplaySessionOpened;
    }

    LOG(DLOGL_NOISE, "setting following tsParams");
    LogMediaTsParams(&mTsParams);

    err = cpe_media_Set(mMediaHandle, eCpeMediaGetSetNames_TransportStreamParam, (void *)&mTsParams);
    if (err != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "cpe_media_Set [TS Params] error: %d", err);
        status = kMspStatus_CpeMediaError;
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "cpe_media_Set good, program No %d from TS params, handle %p", mTsParams.progNumber, mMediaHandle);
        cpe_media_RegisterCallback(eCpeMediaCallbackType_FirstFrameAlarm, this, mediaMediaCallback,
                                   &mFirstFrameCbId, mMediaHandle, NULL);
        cpe_media_RegisterCallback(eCpeMediaCallbackType_AbsoluteAlarm, this, mediaMediaCallback,
                                   &mAbsoluteFrameCbId, mMediaHandle, NULL);
    }

    err = cpe_ProgramHandle_Set(mMediaHandle, eCpePgrmHandleNames_Pmt, (void *)&mPmtInfo);
    if (err != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "cpe_ProgramHandle_Set[Pmt] error: %d ", err);
        status = kMspStatus_CpeMediaError;
    }
    uint32_t progNum = mPgmNo;
    err = cpe_ProgramHandle_Set(mMediaHandle, eCpePgrmHandleNames_ProgramNumber, (void *)&progNum);
    if (err != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "cpe_ProgramHandle_Set[ProgramNumber] error: %d", err);
        status = kMspStatus_CpeMediaError;
    }
    LOG(DLOGL_REALLY_NOISY, "cpe_ProgramHandle_Set for PMT and program No %d, handle %p", progNum, mMediaHandle);
    if (mPtrPlaySession)
    {
        LOG(DLOGL_REALLY_NOISY, "Call attachService");
        err = mPtrPlaySession->attachService(aSource->getSourceId());     // note -- may not be needed later
        if (err)
        {
            LOG(DLOGL_ERROR, "attachService error: %d", err);
        }

        LOG(DLOGL_REALLY_NOISY, "Call addProgramHandle %p", mMediaHandle);
        err = mPtrPlaySession->addProgramHandle(mMediaHandle);
        if (err)
        {
            LOG(DLOGL_ERROR, "addProgramHandle error: %d", err);
        }

        LOG(DLOGL_REALLY_NOISY, "addProgramHandle complete");

        if (mCaPid && !UseCableCardRpCAK())
        {
            startCaFilter(mCaPid);
        }
    }
    Avpm::getAvpmInstance()->setAvpmAudioSrc(tAvpmAudioSrcDigital);
    return  status;
}
// For analog channel
eMspStatus DisplaySession::open(const MSPSource *aSource, int ChannelType)
{
    eMspStatus status = kMspStatus_Ok;
    int err;
    tCpeMediaAnalogStreamParam   mAsParams;
    FNLOG(DL_MSP_MPLAYER);
    ChannelType = 1;
// Init Analog channel
    mTsParams.transportID = 0;
    mTsParams.pidTable.clockPid = NULL_PID;
    mTsParams.pidTable.videoStreamType = 0;
    mTsParams.pidTable.videoPid = 0;
    mTsParams.pidTable.videoStreamType = 0;
    mTsParams.pidTable.audioPid = 0;
    mTsParams.pidTable.audioStreamType = 0;
    mTsParams.pidTable.amolPid = 0;

    mSrcHandle = aSource->getCpeSrcHandle();
    mSourcetype = kMspSrcTypeAnanlog;

    if (mSrcHandle)
    {
        switch (mState)
        {
        case kDisplaySessionIdle:
        case kDisplaySessionClosed:
        {
            err = cpe_media_Open(mSrcHandle, eCpeMediaStreamTypes_AnalogProgram, &mMediaHandle);
            if (err != kCpe_NoErr)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "DisplaySession::%s:%d Call cpe_media_Open(...) return error %d, with source handle %d", __FUNCTION__, __LINE__, err, (int) mSrcHandle);
                status = kMspStatus_CpeMediaError;
                break;
            }
            else
            {
                dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "DisplaySession::%s:%d Call cpe_media_Open(...) success!!!", __FUNCTION__, __LINE__);
                mState = kDisplaySessionOpened;
                void *ptr = malloc(sizeof(tCpeSrcPFPidDef) + (sizeof(tCpeSrcPFPidStruct) * 3));
                if (ptr)
                {
                    cpe_src_Get(mSrcHandle, eCpeSrcNames_PIDNumbers, ptr, (sizeof(tCpeSrcPFPidDef) + (sizeof(tCpeSrcPFPidStruct) * 3)));

                    memset(&mAsParams, 0, sizeof(tCpeMediaAnalogStreamParam));
                    if (!mAudioFocus)
                    {
                        /* PIP Session so we should not set Audio PID */
                        LOG(DLOGL_NOISE, "Analog PIP or POP Audio");
                        mTsParams.fullRes = false;  // make sure
                        mAsParams.pidTable.audioPid = 0 ;
                    }
                    else
                    {
                        mAsParams.fullRes = true;
                        mAsParams.pidTable.audioPid = ((tCpeSrcPFPidDef*)ptr)->pids[0].pid ;
                    }

                    mAsParams.pidTable.clockPid = ((tCpeSrcPFPidDef*)ptr)->pids[2].pid;                      ///< Clock PID
                    mAsParams.pidTable.videoPid = ((tCpeSrcPFPidDef*)ptr)->pids[1].pid;                      ///< Video PID - 0 = No Video PID
                    mAsParams.pidTable.videoStreamType = 0;               ///< Video Stream Type
                    ///< Audio PID - 0 = No Audio PID
                    mAsParams.pidTable.audioStreamType = 0;               ///< Audio Stream Type
                    mAsParams.pidTable.amolPid = 0;

                    free(ptr);

                    err = cpe_media_Set(mMediaHandle, eCpeMediaGetSetNames_AnalogStreamParam, (void *)&mAsParams);
                    if (err != kCpe_NoErr)
                    {
                        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "DisplaySession::%s:%d cpe_media_Set[eCpeMediaGetSetNames_TransportStreamParam] Failed rc=%d", __FUNCTION__, __LINE__, err);
                        status = kMspStatus_CpeMediaError;
                    }
                    else
                    {
                        cpe_media_RegisterCallback(eCpeMediaCallbackType_FirstFrameAlarm, this, mediaMediaCallback, &mFirstFrameCbId, mMediaHandle, NULL);
                        cpe_media_RegisterCallback(eCpeMediaCallbackType_AbsoluteAlarm, this, mediaMediaCallback, &mAbsoluteFrameCbId, mMediaHandle, NULL);
                    }
                }
            }
        }
        break;
        default:
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "DisplaySession::%s:%d Wrong state, current state %d", __FUNCTION__, __LINE__, mState);
            status = kMspStatus_StateError;
            break;
        }
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "DisplaySession::%s:%d Null Handle", __FUNCTION__, __LINE__);
        status = kMspStatus_BadParameters;
    }
    Avpm::getAvpmInstance()->setAvpmAudioSrc(tAvpmAudioSrcAnalog);
    Avpm::getAvpmInstance()->setAvpmSapEnabled(false);
    return  status;
}



eMspStatus DisplaySession::setUpDfbThroughAvpm(void)
{
    eMspStatus status = kMspStatus_Ok;

    FNLOG(DL_MSP_MPLAYER);

    Avpm::getAvpmInstance()->setIsVod(mIsVod);

    //Call AVPM to set up the display
    //err = dfb_StartVideo(pMedia->pgrmHandleMedia, &pMedia->msh, &pMedia->vsh, &pMedia->hdHandle, &pMedia->sdHandle);

    Avpm::getAvpmInstance()->SetVideoPresent(mTsParams.pidTable.videoPid != NULL_PID);

    if (mWindowSetPending)
    {
        status = Avpm::getAvpmInstance()->setPresentationParams(mMediaHandle, mRect, mAudioFocus);
        if (status == kMspStatus_Ok)
        {
            mWindowSetPending = false;
        }
        else
        {
            LOG(DLOGL_ERROR, "setPresentationParams error %d", status);
        }
    }
    status = Avpm::getAvpmInstance()->setAudioParams(mMediaHandle);
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "setAudioParams error %d", status);
    }
    bool ccEnabled;
    Avpm *inst = Avpm::getAvpmInstance();
    if ((kMspStatus_Ok == inst->getCc(&ccEnabled)) && ccEnabled)
    {
        inst->SetDigitalCCLang(mMediaHandle);
    }

    LOG(DLOGL_REALLY_NOISY, "vod1080pCheck: mIsVod : %d. For VOD, start output only after receiving first frame event.", mIsVod);
    if (false == mIsVod)
    {
        status = Avpm::getAvpmInstance()->connectOutput(mMediaHandle);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "connectOuput error %d", status);
        }
    }

    return status;
}


eMspStatus DisplaySession::setUpDemuxDecoder(bool isEasAudioActive)
{
    eMspStatus status = kMspStatus_Ok;
    int err;

    FNLOG(DL_MSP_MPLAYER);

    mDecoderFlag = 0;

    if (mTsParams.pidTable.clockPid != NULL_PID)
    {
        mDecoderFlag |= kCpeMedia_ClockDecoder;
    }

    if (mTsParams.pidTable.audioPid != NULL_PID && mAudioFocus != 0)
    {
        mDecoderFlag |= kCpeMedia_AudioDecoder;
    }

    if (mTsParams.pidTable.videoPid != NULL_PID)
    {
        mDecoderFlag |= kCpeMedia_VideoDecoder;
    }


    if (!mDecoderFlag)
    {
        LOG(DLOGL_ERROR, "Error: No valid pids");
        // status = kMspStatus_PsiError;
    }
    else
    {
        // adjust the decoder flag based on EAS audio playback state
        uint32_t adjustedDecoderFlag = getEasAdjustedDecoderFlag(isEasAudioActive);

        // execute media start
        LOG(DLOGL_REALLY_NOISY, "go to cpe_media_Start adjustedDecoderFlag: 0x%x", adjustedDecoderFlag);
        err = cpe_media_Start(mMediaHandle, adjustedDecoderFlag);
        if (err != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "cpe_media_Start error: %d", err);
            status = kMspStatus_CpeMediaError;
        }
    }

    return status;
}


/** *********************************************************
 */
eMspStatus DisplaySession::start(bool isEasAudioActive)
{
    FNLOG(DL_MSP_MPLAYER);

    if (mState != kDisplaySessionOpened)
    {
        LOG(DLOGL_ERROR, "error: start called in state: %d", mState);
        return kMspStatus_StateError;
    }

    LOG(DLOGL_REALLY_NOISY, "Change state to %d, previous state: %d", kDisplaySessionStartedIncomplete, mState);
    mState = kDisplaySessionStartedIncomplete;

    eMspStatus status = setUpDemuxDecoder(isEasAudioActive);
    // Check the need to enable SAP on ananlog channel
    if (mSourcetype == kMspSrcTypeAnanlog)
    {

        eUseSettingsLevel settingLevel1, settingLevel2;
        char sap[20] = {0};
        char sapSupport[20] = {0};
        eUse_StatusCode eUseStatus = USE_RESULT_OK;

        eUseStatus =  Uset_getSettingT(NULL, "ciscoSg/audio/sap", sizeof(sap), sap, &settingLevel1);
        LOG(DLOGL_NORMAL, "Usetting return %d, value %s, for ciscoSg/audio/sap", eUseStatus, sap);

        eUseStatus = Uset_getSettingT(NULL, "ciscoSg/audio/sapSuppt", sizeof(sapSupport), sapSupport, &settingLevel2);
        LOG(DLOGL_NORMAL, "Usetting return %d, value %s, for ciscoSg/audio/sapSuppt", eUseStatus, sapSupport);

        uint32_t sapaudio = 0;

#if 0
        int err = cpe_media_Get(mMediaHandle, eCpeMediaGetSetNames_EnableAnalogSAPAudio, &sapaudio, sizeof(uint32_t));
        if (err == kCpe_NoErr)
        {
            LOG(DLOGL_NORMAL, "SAP enabled %d", sapaudio);
        }
        else
            LOG(DLOGL_ERROR, "err = %d calling cpe_media_get", err);
#endif
        if ((strcasecmp(sap, "true") == 0 || strcasecmp(sap, "Enabled") == 0))   //&& strcasecmp(sapSupport, "true") == 0 )
        {
            sapaudio = 1;
        }
        int err = cpe_media_Set(mMediaHandle, eCpeMediaGetSetNames_EnableAnalogSAPAudio, &sapaudio);
        LOG(DLOGL_NORMAL, "err = %d calling cpe_media_Set with sapaudio %d", err, sapaudio);
        if (!err && sapaudio)
        {
            Avpm::getAvpmInstance()->setAvpmSapEnabled(true);
        }
        else
            Avpm::getAvpmInstance()->setAvpmSapEnabled(false);
    }

    if (status == kMspStatus_Ok)
    {
        status = setUpDfbThroughAvpm();
        if (status != kMspStatus_Ok)
        {
            // if DMX content we expect an error
            if (((mDecoderFlag & kCpeMedia_VideoDecoder) == 0) &&
                    (status == kMspStatus_AvpmError))
            {
                // it is necessary to advance to started state in order for
                //   the StopAudio() and RestartAudio() methods to work correctly.
                //   See the tail of this source file.
                mState = kDisplaySessionStarted;
            }
            else
            {
                LOG(DLOGL_ERROR, "setUpDfbThroughAvpm error: %d", status);
            }
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "Change state to %d, previous state: %d", kDisplaySessionStarted, mState);
            mState = kDisplaySessionStarted;
        }

        if (mAudioFocus == 0)
        {
            Avpm::getAvpmInstance()->clearVideoSurface();
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "setUpDemuxDecoder error: %d", status);
    }

// for audio only channels or analog channel, no first frame alarm happens, so we fake one here to keep RTN happy
    if ((mTsParams.pidTable.videoPid == NULL_PID && mTsParams.pidTable.audioPid != NULL_PID) ||
            (mSourcetype == kMspSrcTypeAnanlog))
    {
        LOG(DLOGL_NORMAL, "Fake a FirstFrameAlarm CB");
        performCb(eCpeMediaCallbackType_FirstFrameAlarm);
    }
    return status;
}


/** *********************************************************
 */
eMspStatus DisplaySession::controlMediaAudio(tCpePlaySpeed pPlaySpd)
{
    int err;
    tCpeMediaDecoderControl ctrl;
    eMspStatus status = kMspStatus_Ok;
    static bool isCcEnabledBeforeSlowPlay = false;
    FNLOG(DL_MSP_MPLAYER);

    Avpm *inst = Avpm::getAvpmInstance();

    if (pPlaySpd.mode == eCpePlaySpeedMode_Forward &&
            pPlaySpd.scale.numerator / pPlaySpd.scale.denominator == 1)
    {
        LOG(DLOGL_NOISE, "Enable Media Audio");
        ctrl = eCpeMediaDecoderControl_Start;
        if (isCcEnabledBeforeSlowPlay)
        {
            LOG(DLOGL_NOISE, "CC enabled");
            status = inst->setCc(true);

            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "error in enabling closed captions after slow play: %d", status);
            }
        }
    }
    else
    {
        LOG(DLOGL_NOISE, "Disabling Media Audio");
        ctrl = eCpeMediaDecoderControl_Stop;

        status = inst->getCc(&isCcEnabledBeforeSlowPlay);

        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "error in while getting closed captions status for slow play: %d", status);
        }
        else
        {
            if (isCcEnabledBeforeSlowPlay)
            {
                LOG(DLOGL_NOISE, "Disabling Closed caption for slow play");
                status = inst->setCc(false);

                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "error in disabling closed captions for slow play: %d", status);
                }
            }
        }
    }

    err = cpe_media_Control(mMediaHandle, eCpeMediaControlNames_AudioDecoder, &ctrl);
    if (err == kCpe_NoErr)
    {
        LOG(DLOGL_NOISE, "cpe_media_Control Success");
        status = kMspStatus_Ok;
    }
    else
    {
        LOG(DLOGL_ERROR, "cpe_media_Control error: %d", err);
        status =  kMspStatus_CpeMediaError;
    }

    return status;
}


/** *********************************************************
 */
eMspStatus DisplaySession::setMediaSpeed(tCpePlaySpeed pPlaySpd)
{
    FNLOG(DL_MSP_MPLAYER);

    LOG(DLOGL_NOISE, "set mode: %d  numerator: %d  denominator: %d",
        pPlaySpd.mode, pPlaySpd.scale.numerator, pPlaySpd.scale.denominator);

    eMspStatus status = kMspStatus_Ok;

    int err = cpe_media_Set(mMediaHandle, eCpeMediaGetSetNames_PlaySpeed, (void *)&pPlaySpd);

    if (err != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "cpe_media_Set error: %d", err);
        status = kMspStatus_CpeMediaError;
    }

    return status;
}


eMspStatus DisplaySession::startMedia()
{
    FNLOG(DL_MSP_MPLAYER);
    eMspStatus status = kMspStatus_Ok;
    int err;

    err = cpe_media_Start(mMediaHandle, mDecoderFlag);
    if (err != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "cpe_media_Start error: %d", err);
        status = kMspStatus_Error;
    }

    return status;
}

eMspStatus DisplaySession::stopMedia()
{
    FNLOG(DL_MSP_MPLAYER);
    eMspStatus status = kMspStatus_Ok;
    int err;

    err = cpe_media_Stop(mMediaHandle , mDecoderFlag);
    if (err != kCpe_NoErr)
    {
        LOG(DLOGL_ERROR, "cpe_media_Start error: %d", err);
        status = kMspStatus_Error;
    }

    return status;
}


/** *********************************************************
 */
eMspStatus DisplaySession::freeze(void)
{
    eMspStatus status = kMspStatus_Ok;

    FNLOG(DL_MSP_MPLAYER);

    switch (mState)
    {
    case kDisplaySessionStartedIncomplete:
    case kDisplaySessionStarted:
    {
        //Need to call AVPM to do the stop
        status = Avpm::getAvpmInstance()->pauseVideo(mMediaHandle);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "pauseVideo error %d", status);
        }

        LOG(DLOGL_REALLY_NOISY, "Change state to %d, previous state: %d", kDisplaySessionFreezed, mState);
        mState = kDisplaySessionFreezed;
    }
    break;
    default:
        LOG(DLOGL_ERROR, "Warning Wrong state %d", mState);
        //We will return  kMspStatus_Ok since it is being stopped but complain with the log
        break;
    }

    return  status;
}

/** *********************************************************
 */
eMspStatus DisplaySession::stopOutput(void)
{
    eMspStatus status = kMspStatus_Ok;

    FNLOG(DL_MSP_MPLAYER);

    status = Avpm::getAvpmInstance()->stopOutput(mMediaHandle);
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "stopOutput error %d", status);
    }

    if (mAudioFocus == 0)
    {
        Avpm::getAvpmInstance()->clearVideoSurface();
    }

    return  status;
}

/** *********************************************************
 */
eMspStatus DisplaySession::startOutput(void)
{
    eMspStatus status = kMspStatus_Ok;

    FNLOG(DL_MSP_MPLAYER);

    status = Avpm::getAvpmInstance()->startOutput(mMediaHandle);
    if (status != kMspStatus_Ok)
    {
        LOG(DLOGL_ERROR, "startOutput error %d", status);
    }

    return  status;
}




/** *********************************************************
 */
eMspStatus DisplaySession::stop(void)
{
    eMspStatus status = kMspStatus_Ok;
    int err;

    FNLOG(DL_MSP_MPLAYER);

    switch (mState)
    {
    case kDisplaySessionStartedIncomplete:
    case kDisplaySessionStarted:
    case kDisplaySessionFreezed:
    {

        //Need to call AVPM to do the stop
        status = Avpm::getAvpmInstance()->stopOutput(mMediaHandle);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "stopOutput error %d", status);
        }


        err = cpe_media_Stop(mMediaHandle , mDecoderFlag);
        if (err != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "cpe_media_Stop error %d", err);
            //We will still return OK since we are stop and then close later. Just log it.
        }

        LOG(DLOGL_REALLY_NOISY, "Change state to %d, previous state: %d", kDisplaySessionStopped, mState);
        mState = kDisplaySessionStopped;

    }
    break;
    default:
        LOG(DLOGL_ERROR, "warning Wrong state %d", mState);
        //We will return  kMspStatus_Ok since it is being stopped but complain with the log
        break;
    }

    return  status;
}

/** *********************************************************
  \returns
 */
eMspStatus DisplaySession::close(bool isEasAudioActive)
{
    eMspStatus status = kMspStatus_Ok;
    int err;

    FNLOG(DL_MSP_MPLAYER);

    switch (mState)
    {
    case kDisplaySessionStopped:
    case kDisplaySessionOpened:
    {
        //Need to call AVPM to do the disconnect
        if (mSfHandle != 0)
        {
            stopCaFilter();
            mSfHandle = 0;
        }
        stopAppDataFilter();
        status = Avpm::getAvpmInstance()->disconnectOutput(mMediaHandle);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "disconnectOutput error %d", status);
        }

        if (mPtrPlaySession != NULL)
        {
            mPtrPlaySession->unRegisterCCIupdate(mCCIRegId);
            if (UseCableCardRpCAK())
                mPtrPlaySession->unRegisterEntitlementUpdate(mEntRegId);
            LOG(DLOGL_REALLY_NOISY, "playSession shutdown");
            mPtrPlaySession->shutdown();

            mPtrPlaySession = NULL;
        }
        closeAppDataFilter();

        unregisterDisplayMediaCallback();

        if (isEasAudioActive)
        {
            // adjust the decoder flag based on EAS audio playback state
            uint32_t adjustedDecoderFlag = getEasAdjustedDecoderFlag(isEasAudioActive);
            // execute media stop
            err = cpe_media_Stop(mMediaHandle , adjustedDecoderFlag);
            if (err != kCpe_NoErr)
            {
                LOG(DLOGL_ERROR, "cpe_media_Stop error %d", err);
                //We will still return OK since we are stop and then close later. Just log it.
            }
        }
        err = cpe_media_Close(mMediaHandle);
        if (err != kCpe_NoErr)
        {
            LOG(DLOGL_ERROR, "cpe_media_Close error %d", err);
            //We will still return OK since we closing. Just log it.
        }

        LOG(DLOGL_REALLY_NOISY, "Change state to %d, previous state: %d", kDisplaySessionClosed, mState);
        mState = kDisplaySessionClosed;
    }
    break;
    default:
        LOG(DLOGL_ERROR, "warning wrong state %d", mState);
        //We will return  kMspStatus_Ok since it is being stopped but complain with the log
        break;
    }
    return  status;
}

/** *********************************************************
  \returns
 */
eMspStatus DisplaySession::registerDisplayMediaCallback(void *client, DisplayMediaCallbackFunction callBc)
{
    eMspStatus status = kMspStatus_Ok;

    FNLOG(DL_MSP_MPLAYER);

    if (callBc)
    {
        mCb          = callBc;
        mClientForCb = client;
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "DisplaySession::%s:%d Null callBc", __FUNCTION__, __LINE__);
        status = kMspStatus_BadParameters;
    }

    return  status;
}

/** *********************************************************
  \returns
 */
eMspStatus DisplaySession::unregisterDisplayMediaCallback(void)
{
    FNLOG(DL_MSP_MPLAYER);

    cpe_media_UnregisterCallback(mMediaHandle, mFirstFrameCbId);
    return  kMspStatus_Ok;
}

/** *********************************************************
  \returns
 */
void DisplaySession::performCb(tCpeMediaCallbackTypes type)
{
    FNLOG(DL_MSP_MPLAYER);
    LOG(DLOGL_NOISE, "type: %d", type);

    Avpm::getAvpmInstance()->clearVideoSurface();

    if (mCb)
    {
        mCb(mClientForCb, type);
    }
    else
    {
        LOG(DLOGL_NOISE, "Warning: no registered callback");
    }

    LOG(DLOGL_NOISE, "mIsVod: %d. If callback is for VOD, start output after receiving first frame event.", mIsVod);
    if (mIsVod && (type == eCpeMediaCallbackType_FirstFrameAlarm))
    {
        eMspStatus status = Avpm::getAvpmInstance()->connectOutput(mMediaHandle);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "connectOuput error %d", status);
        }
    }
}

/** *********************************************************
 */
DisplaySession::~DisplaySession()
{
    FNLOG(DL_MSP_MPLAYER);
    pthread_mutex_destroy(&mAppDataMutex);
}

DisplaySession::DisplaySession(bool isVod)
{
    FNLOG(DL_MSP_MPLAYER);

    mState = kDisplaySessionIdle;
    mCb = NULL;
    mClientForCb = NULL;
    mWindowSetPending = false;
    mAudioFocus = true;
    mMediaHandle = 0;
    mDecoderFlag = 0;
    mFirstFrameCbId = 0;
    mAbsoluteFrameCbId = 0;
    mPtrPlaySession = NULL;
    mSfHandle = NULL;
    mCaPid = 0;
    memset(&mTsParams, 0, sizeof(mTsParams));
    mTsParams.fullRes = true; //Make it a mainTV
    mTsParams.progNumber = 0;
    mTsParams.transportID = 0;
    mTsParams.pidTable.clockPid = NULL_PID;
    mTsParams.pidTable.videoPid = NULL_PID;
    mTsParams.pidTable.videoStreamType = 0;
    mTsParams.pidTable.audioPid = NULL_PID;
    mTsParams.pidTable.audioStreamType = 0;
    mTsParams.pidTable.amolPid = 0;
    memset(&mPmtInfo, 0, sizeof(mPmtInfo));
    mPgmNo = -1;
    mPtrCBData = NULL;
    mCCICBFn = NULL;
    mCCIRegId = 0;
    mEntRegId = 0;
    mAppSfHandle = NULL;
    mbCamSfHandleAdded = false;
    mAppSfCbIdError = 0;
    mAppSfCbIdSectionData = 0;
    mAppSfCbIdTimeOut = 0;
    mDataReadyCB = NULL;
    mDataReadyContext = NULL;
    mAppData = NULL;
    mOldCaStatus = 0;
//coverity id - 10397
//initializing the uninitialized variables
    mSrcHandle = 0;
    mFCbIdSectionData = 0;
    mSfCbIdError = 0;
    mSfCbIdSectionData = 0;
//coverity id - 10397
    mScramblingMode = 0;
    mCaSystem = 0;
    mRect.x = 0;
    mRect.y = 0;
    mRect.w = 0;
    mRect.h = 0;
    mSourcetype = kMSpSrcTypeUnknown;
    mCSDcrc = 0;
    pthread_mutex_init(&mAppDataMutex, NULL);
    mIsVod = isVod;
}

static void caEntitlemntCallback(void *pData, camEntitlementStatus entStatus)
{
    DisplaySession *pSession = (DisplaySession *)pData;
    dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "%s, %d, session %p, Informing SL new CA Entitlement status %d", __FUNCTION__, __LINE__, pSession, entStatus);

    if (pSession)
    {
        if (entStatus == CAM_ENTITLED)
        {
            pSession->callback(kMediaPlayerSignal_ServiceAuthorized, kMediaPlayerStatus_Ok);
        }
        else
        {
            pSession->callback(kMediaPlayerSignal_ServiceDeauthorized, kMediaPlayerStatus_NotAuthorized);
        }
    }

    return;
}
/** *********************************************************
 */
void *DisplaySession::sfCallback(tCpeSFltCallbackTypes type, void* pCallbackSpecific)
{
    UNUSED_PARAM(pCallbackSpecific);
    ICaFilter *filterFunc;
    int status;

    if (type == eCpeSFltCallbackTypes_SectionData)
    {
        tCpeSFltBuffer *pSfltBuff = (tCpeSFltBuffer *)pCallbackSpecific;
        if (pSfltBuff != NULL)
        {
            if (mPtrPlaySession != NULL)
            {
                filterFunc = mPtrPlaySession->getCaFilter();
                if (filterFunc != NULL)
                {
                    status = filterFunc->doFilter((char *)pSfltBuff->pBuffer, pSfltBuff->length);
                    // check result and send auth/notauth callback to SL on every transition
                    if (status != mOldCaStatus)
                    {
                        if (status == 0)
                        {
                            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Content re-authorized. code %d , calling SL", __FUNCTION__, status);
                            callback(kMediaPlayerSignal_ServiceAuthorized, kMediaPlayerStatus_Ok);
                        }
                        else  // CAM returns -2 for all error conditions - no way to distinguish not-auth from everything else
                        {
                            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Content Not authorized. code %d , calling SL", __FUNCTION__, status);
                            callback(kMediaPlayerSignal_ServiceDeauthorized, kMediaPlayerStatus_NotAuthorized);
                        }
                        mOldCaStatus = status;
                    }
                }
            }
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s NULL section filter callback data ", __FUNCTION__);
        }
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: BAD CA section filter callback. Type: %d", __FUNCTION__, type);
    }


    return NULL;
}


/** *********************************************************
 */
void *DisplaySession::secFltCallbackFunction(tCpeSFltCallbackTypes type, void* userdata, void* pCallbackSpecific)
{
    DisplaySession *ds = (DisplaySession *)userdata;

    if (ds)
    {
        return ds->sfCallback(type, pCallbackSpecific);
    }
    else
    {
        return NULL;
    }
}

void *DisplaySession::appSecFltCallbackFunction(tCpeSFltCallbackTypes type, void* userdata, void* pCallbackSpecific)
{
    DisplaySession *inst = (DisplaySession *)userdata;
    tCpeSFltBuffer *secFiltData = (tCpeSFltBuffer *)pCallbackSpecific;

    FNLOG(DL_MSP_MPLAYER);
    LOG(DLOGL_NOISE, "CB type %d", type);

    if (inst)
    {
        //Lock the mutex now.
        pthread_mutex_lock(&inst->mAppDataMutex);
        if (inst->mAppData == NULL)
        {
            pthread_mutex_unlock(&inst->mAppDataMutex);
            return NULL;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "####### Error: Displaysession Instance is NULL ########");
        return NULL;
    }

    switch (type)
    {
    case eCpeSFltCallbackTypes_SectionData: ///< Callback returns the section data to the caller
    {
        if (secFiltData && secFiltData->length != 0 && inst)
        {
            int databytes = 0;
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s:%d secfilt data %d %p", __FUNCTION__, __LINE__, secFiltData->length, secFiltData);

            if (inst->mAppData)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s:%d ApplicationData Pointer received = %p", __FUNCTION__, __LINE__, inst->mAppData);
                databytes = inst->mAppData->addData(secFiltData->pBuffer, secFiltData->length);
                //Copy callback data to a local copy so that it can be called outside the lock
                DataReadyCallback localCallback = inst->mDataReadyCB;
                //Unlock the mutex now
                pthread_mutex_unlock(&inst->mAppDataMutex);

                if ((databytes) && (localCallback))
                {
                    localCallback(inst->mDataReadyContext);
                }
            }
            else
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s:%d: Application Data Instance is Null.", __FUNCTION__, __LINE__);
            }
        }
    }
    break;

    case eCpeSFltCallbackTypes_Timeout:   ///< Read operation timed out.
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s:%d Section Filter Timeout\n", __FUNCTION__, __LINE__);
    }
    break;

    case eCpeSFltCallbackTypes_Error:           ///< Error occured during read operation.
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s:%d Section Filter Error \n", __FUNCTION__, __LINE__);
        break;

    default:
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:%d Unknown event %d from source callback \n", __FUNCTION__, __LINE__, type);
        break;

    } //end of switch

    pthread_mutex_unlock(&inst->mAppDataMutex);
    return NULL;
}

/** *********************************************************
*/
eMspStatus DisplaySession::startCaFilter(uint16_t pid)
{
    int status;
    FNLOG(DL_MSP_MPLAYER);

    status = cpe_sflt_Open(mSrcHandle, kCpeSFlt_LowBandwidth, &mSfHandle);
    if (status != kCpe_NoErr)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to open CA section filter. Err: %d", __FUNCTION__, status);
        return kMspStatus_Error;
    }

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "DisplaySession::%s: Open CA section filter. Handle %p", __FUNCTION__, mSfHandle);
    status = cpe_sflt_Set(mSfHandle, eCpeSFltNames_PID, (void *)&pid);
    if (status != kCpe_NoErr)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to set CA section filter. Err: %d", __FUNCTION__, status);
        return kMspStatus_Error;
    }

    // Register for Callbacks
    cpe_sflt_RegisterCallback(eCpeSFltCallbackTypes_Error, (void *)this, (tCpeSFltCallbackFunction)secFltCallbackFunction, &mSfCbIdError, mSfHandle);
    cpe_sflt_RegisterCallback(eCpeSFltCallbackTypes_SectionData, (void *)this, (tCpeSFltCallbackFunction)secFltCallbackFunction, &mFCbIdSectionData, mSfHandle);
    cpe_sflt_RegisterCallback(eCpeSFltCallbackTypes_Timeout, (void *)this, (tCpeSFltCallbackFunction)secFltCallbackFunction, &mSfCbIdSectionData, mSfHandle);

    status = cpe_sflt_Start(mSfHandle, 0);
    if (status != kCpe_NoErr)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to start CA section filter. Err: %d", __FUNCTION__, status);
        return kMspStatus_Error;
    }

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s: start CA section filter. handle %p", __FUNCTION__, mSfHandle);
    return kMspStatus_Ok;
}

/** *********************************************************
*/
eMspStatus DisplaySession::stopCaFilter(void)
{
    int status;
    FNLOG(DL_MSP_MPLAYER);

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s: stop CA section filter. handle %p", __FUNCTION__, mSfHandle);

    if (mSfHandle == NULL)
    {
        return kMspStatus_StateError;
    }

    status = cpe_sflt_Stop(mSfHandle);
    if (status != kCpe_NoErr)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to stop CA section filter. Err: %d", __FUNCTION__, status);
    }

    cpe_sflt_UnregisterCallback(mSfHandle, mSfCbIdError);
    cpe_sflt_UnregisterCallback(mSfHandle, mFCbIdSectionData);
    cpe_sflt_UnregisterCallback(mSfHandle, mSfCbIdSectionData);

    status = cpe_sflt_Close(mSfHandle);
    if (status != kCpe_NoErr)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to close CA section filter. Err: %d", __FUNCTION__, status);
        return kMspStatus_Error;
    }

    mSfHandle = NULL;
    return kMspStatus_Ok;
}

void DisplaySession::SetCCICallback(void *data, CCIcallback_t cb)
{
    if (mPtrPlaySession != NULL)
    {
        if (mCCIRegId != 0)
        {
            mPtrPlaySession->unRegisterCCIupdate(mCCIRegId);
        }
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, " DisplaySession::SetCCICallback is called and playsession is calledback to regsiter cci callback with playsession in cam");
        mCCIRegId = mPtrPlaySession->registerCCIupdate(data, cb);
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_MINOR_DEBUG, " mPtrPlaySession->RegisterCCIupdate is not called because mPtrPlaySession is NULL");
    }
    mPtrCBData = data;
    mCCICBFn = cb;
}


void DisplaySession::UnSetCCICallback()
{
    if (mPtrPlaySession != NULL)
    {
        mPtrPlaySession->unRegisterCCIupdate(mCCIRegId);
    }
    mCCIRegId = 0;
}

eMspStatus DisplaySession::filterAppDataPid(uint32_t aPid, DataReadyCallback aDataReadyCB, void *aClientContext, bool isMusic)
{
    FNLOG(DL_MSP_MPLAYER);
    uint16_t pid = (uint16_t)aPid;

    if (aPid == INVALID_PID_VALUE)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Invalid Pid -- stop filtering on earlier PID\n");
        eMspStatus status = stopAppDataFilter();
        //delete Application Data Pointer inside the lock
        pthread_mutex_lock(&mAppDataMutex);
        if (mAppData != NULL)
        {
            LOG(DLOGL_NOISE, "Deleting ApplicationData Instance %p", mAppData);
            delete mAppData;
            mAppData = NULL;
        }
        mDataReadyCB = NULL;
        mDataReadyContext = NULL;

        pthread_mutex_unlock(&mAppDataMutex);
        return status;
    }
    else
    {
        if (mAppSfHandle == NULL)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Start Section Filter for App Data Pid %d", aPid);
            mDataReadyCB = aDataReadyCB;
            mDataReadyContext = aClientContext;

            //delete Application Data Pointer inside the lock
            pthread_mutex_lock(&mAppDataMutex);
            if (mAppData != NULL)       // shouldn't happen, but clear old data if it does
            {
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Deleted ApplicationData Instance %p", mAppData);
                delete mAppData;
                mAppData = NULL;
            }
            if (true == isMusic)
            {
                mAppData = new MusicAppData(maxApplicationDataSize);
            }
            else
            {
                mAppData = new ApplicationData(maxApplicationDataSize);
            }

            pthread_mutex_unlock(&mAppDataMutex);

            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Created ApplicationData Instance Pointer = %p", mAppData);

            int status = cpe_sflt_Open(mSrcHandle, kCpeSFlt_LowBandwidth, &mAppSfHandle);
            if (status != kCpe_NoErr)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to open App Data section filter. Err: %d", __FUNCTION__, status);
                return kMspStatus_Error;
            }

            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "DisplaySession::%s: Open App Data section filter. Handle %p", __FUNCTION__, mAppSfHandle);
            status = cpe_sflt_Set(mAppSfHandle, eCpeSFltNames_PID, (void *)&pid);
            if (status != kCpe_NoErr)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to set App Data section filter. Err: %d", __FUNCTION__, status);
                return kMspStatus_Error;
            }

            // Register for Callbacks
            cpe_sflt_RegisterCallback(eCpeSFltCallbackTypes_Error, (void *)this, (tCpeSFltCallbackFunction)appSecFltCallbackFunction, &mAppSfCbIdError, mAppSfHandle);
            cpe_sflt_RegisterCallback(eCpeSFltCallbackTypes_SectionData, (void *)this, (tCpeSFltCallbackFunction)appSecFltCallbackFunction, &mAppSfCbIdSectionData, mAppSfHandle);
            cpe_sflt_RegisterCallback(eCpeSFltCallbackTypes_Timeout, (void *)this, (tCpeSFltCallbackFunction)appSecFltCallbackFunction, &mAppSfCbIdTimeOut, mAppSfHandle);

            status = cpe_sflt_Start(mAppSfHandle, 0);
            if (status != kCpe_NoErr)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to start App Datasection filter. Err: %d", __FUNCTION__, status);
                return kMspStatus_Error;
            }

            if ((true == isMusic) && (mCaPid != 0)) // encrypted DMX music channel
            {
                if (mPtrPlaySession)
                {
                    status = mPtrPlaySession->addSectionFilterHandle(mAppSfHandle);
                    if (status)
                    {
                        LOG(DLOGL_ERROR, "DisplaySession::%s addSectionFilterHandle error: %d",
                            __FUNCTION__, status);
                        return kMspStatus_Error;
                    }
                    mbCamSfHandleAdded = true;
                }

                LOG(DLOGL_NOISE, "DisplaySession::%s successful call addSectionFilterHandle %p", __FUNCTION__,
                    mAppSfHandle);
            }

            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s: start App Data section filter. handle %p", __FUNCTION__, mAppSfHandle);
            return kMspStatus_Ok;
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "App data filter is already Set\n");
            return kMspStatus_Error;
        }
    }
}


eMspStatus DisplaySession::stopAppDataFilter()
{
    FNLOG(DL_MSP_MPLAYER);

    if (mAppSfHandle != 0)
    {
        LOG(DLOGL_NOISE, "Stopping sflt handle %p", mAppSfHandle);
        int cpeStatus = cpe_sflt_Stop(mAppSfHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Failed to stop App filter returned error code %d\n", cpeStatus);
        }

        LOG(DLOGL_NOISE, "Unregister sflt handle %p", mAppSfHandle);

        cpe_sflt_UnregisterCallback(mAppSfHandle, mAppSfCbIdError);

        cpe_sflt_UnregisterCallback(mAppSfHandle, mAppSfCbIdSectionData);

        cpe_sflt_UnregisterCallback(mAppSfHandle, mAppSfCbIdTimeOut);

        cpeStatus  = cpe_sflt_Close(mAppSfHandle);
        if (cpeStatus != kCpe_NoErr)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to close APP section filter. Err: %d", __FUNCTION__, cpeStatus);
            return kMspStatus_Error;
        }
        mAppSfHandle = 0;
    }
    if (mbCamSfHandleAdded)
    {
        if (mPtrPlaySession)
        {
            int ret = mPtrPlaySession->removeCamSfhandle();
            LOG(DLOGL_NORMAL, "Remove CAM SF data handle, %d", ret);
            mbCamSfHandleAdded = false;
        }
        else
        {
            LOG(DLOGL_ERROR, "Inconsistency, mbCamSfHandleAdded, but NULL mPtrPlaySession");
        }
    }
    return kMspStatus_Ok;
}

eMspStatus DisplaySession::closeAppDataFilter()
{
    int cpeStatus = 0;
    FNLOG(DL_MSP_MPLAYER);

    if (mAppSfHandle != 0)
    {
        LOG(DLOGL_NORMAL, "Closing sflt handle %p", mAppSfHandle);
        cpeStatus  = (eMspStatus)cpe_sflt_Close(mAppSfHandle);
        mAppSfHandle = 0;
        if (cpeStatus != kMspStatus_Ok)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Failed to close APP section filter. Err: %d", __FUNCTION__, cpeStatus);
            return kMspStatus_Error;
        }
    }
    else
    {
        LOG(DLOGL_NOISE, "Null mAppSfHandle");
    }

    return kMspStatus_Ok;
}

eMspStatus DisplaySession::updateAudioPid(Psi *aPsi, uint32_t aPid)
{
    FNLOG(DL_MSP_MPLAYER);
    if (mState == kDisplaySessionOpened || mState == kDisplaySessionStarted || mState == kDisplaySessionIdle)
    {
        if (aPsi &&  aPsi->getPmtObj())
        {
            Pmt *pmt = aPsi->getPmtObj();
            std::list<tPid> *audioList = pmt->getAudioPidList();
            std::list<tPid>::iterator iter;
            tPid audioPidStruct;
            audioPidStruct.pid = NULL_PID;
            for (iter = audioList->begin() ; iter != audioList->end(); iter++)
            {
                if ((*iter).pid == aPid)
                {
                    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "PID found in Audio PID list %d %d", (*iter).pid, aPid);
                    audioPidStruct.pid = (*iter).pid;
                    audioPidStruct.streamType = (*iter).streamType;
                    break;
                }
            }
            if (audioPidStruct.pid != NULL_PID)
            {
                eMspStatus status = initializePlaySession();
                if (status != kMspStatus_Ok)
                    return status;
                dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:%d add stream. pid %d", __FUNCTION__, __LINE__, audioPidStruct.pid);
                if (mPtrPlaySession)
                {
                    mPtrPlaySession->addStream(audioPidStruct.pid);
                }
                mTsParams.pidTable.audioPid = audioPidStruct.pid;
                mTsParams.pidTable.audioStreamType = audioPidStruct.streamType;
                dlog(DL_MSP_MPLAYER, DLOGL_MINOR_DEBUG, "DisplaySession::%s:%d audio pid 0x%x, streamType %d", __FUNCTION__, __LINE__, mTsParams.pidTable.audioPid, mTsParams.pidTable.audioStreamType);
                if (mState == kDisplaySessionStarted)
                {
                    tCpeMediaUpdatePids updatedPids;
                    updatedPids.pidTable = mTsParams.pidTable;
                    updatedPids.flag = mDecoderFlag;
                    int err = cpe_media_Set(mMediaHandle, eCpeMediaGetSetNames_UpdatePids, (void *)&updatedPids);
                    if (err != kCpe_NoErr)
                    {
                        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "DisplaySession::%s:%d cpe_media_Set[eCpeMediaGetSetNames_UpdatePids] Failed rc=%d", __FUNCTION__, __LINE__, err);
                        return kMspStatus_CpeMediaError;
                    }
                    if (mPtrPlaySession)

                    {
                        err = mPtrPlaySession->performCpeCamUpdate(mMediaHandle);
                        if (err != kCpe_NoErr)
                        {
                            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "DisplaySession::%s:%d cpe_cam_update Failed Failed rc=%d", __FUNCTION__, __LINE__, err);
                            return kMspStatus_CpeMediaError;

                        }

                    }
                }
                dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Audio Pid Set Success\n");
                return kMspStatus_Ok;
            } // Found Audio Pid Struct
            else
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "PID is not part of Audio PID list\n");
                return kMspStatus_Error;
            }
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "PSI or PMT is NULL\n");
            return kMspStatus_Error;
        }
    }
    else // mState is not valid
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Display Session in wrong State %d", mState);
        return kMspStatus_Error;
    }
}


boost::signals2::connection DisplaySession::setCallback(callbackfunctiontype cbfunc)
{
    return callback.connect(cbfunc);
}

void DisplaySession::clearCallback(boost::signals2::connection conn)
{
    conn.disconnect();

}
void DisplaySession::SetAudioLangCB(void *aCbData, AudioLangChangedCB aLangChangedCB)
{
    Avpm *inst = NULL;
    inst = Avpm::getAvpmInstance();
    if (inst)
    {
        inst->SetAudioLangCB(aCbData, aLangChangedCB);
    }
}

void DisplaySession::SetSapChangedCB(void *aCbData, SapChangedCB cb)
{
    Avpm *inst = NULL;
    inst = Avpm::getAvpmInstance();
    if (inst)
    {
        inst->SetSapChangedCB(aCbData, cb);
    }
}

eMspStatus DisplaySession::initializePlaySession()
{
    // TODO: Should this change the state?
    FNLOG(DL_MSP_MPLAYER);

    if (mPtrPlaySession)
    {
        LOG(DLOGL_MINOR_DEBUG, "Warning: called for non-null mPtrPlaySession");
        return kMspStatus_Ok;
    }

    eMspStatus status = kMspStatus_Ok;

    // Create PlaySession object here.
    int err = Cam::getInstance()->createPlaySession(&mPtrPlaySession);      // TODO: need to check return here
    if (!err && mPtrPlaySession)    // not error
    {
        LOG(DLOGL_MINOR_EVENT, "created mPtrPlaySession: %p", mPtrPlaySession);

        if (mCCICBFn)
        {

            mCCIRegId = mPtrPlaySession->registerCCIupdate(mPtrCBData, mCCICBFn);
        }

        err = mPtrPlaySession->initDecryptSession();
        if (err)
        {
            LOG(DLOGL_ERROR, "Error: %d initDecryptSession on mPtrPlaySession: %p", err, mPtrPlaySession);
            status =  kMspStatus_Error;
        }

        if (UseCableCardRpCAK())
        {
            mEntRegId = mPtrPlaySession->registerEntitlementUpdate((void *)this, (EntitlementCallback_t)caEntitlemntCallback);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Error from Cam::createPlaySession: %d  mPtrPlaySession: %p", err, mPtrPlaySession);
        status =  kMspStatus_Error;
    }

    LOG(DLOGL_FUNCTION_CALLS, "return %d", status);

    return status;
}

eMspStatus DisplaySession::getApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    eMspStatus status = kMspStatus_Ok;

    FNLOG(DL_MSP_MPLAYER);

    //This is a critical portion, using lock here
    pthread_mutex_lock(&mAppDataMutex);
    if (NULL == mAppData)
    {
        LOG(DLOGL_ERROR, "ApplicationData pointer is NULL, (May not be an error)");
        pthread_mutex_unlock(&mAppDataMutex);
        return kMspStatus_Error;
    }

    if (dataSize == NULL)
    {
        LOG(DLOGL_ERROR, "dataSize pointer is NULL");
        status = kMspStatus_BadParameters;
    }
    else if ((bufferSize == 0) || (buffer == NULL)) // just requesting size with this call
    {
        *dataSize = mAppData->getTotalSize();
    }
    else
    {
        *dataSize = mAppData->getData(buffer, bufferSize);
    }
    pthread_mutex_unlock(&mAppDataMutex);

    return status;
}


/********************************************************************************
 *
 *	Function:	getEasAdjustedDecoderFlag
 *
 *	Purpose:	Currently, there is only one audio decoder in the STB.  As a
 *				result, the code has to be careful how it uses the decoder when
 *				EAS audio is active.
 *
 *				In mediaservices, refer to audioPlayer.cpp.  This is the component
 *				which controls EAS audio playback.  It is important to note that
 *				EAS audio playback is completely independent of DisplaySession
 *				operations.  In AudioPlayer, note how its code uses
 *				SetEasAudioActive() to communicate if EAS audio is active/inactive.
 *
 *				From the perspective of DisplaySession, it must refrain from
 *				using the audio decoder if EAS audio is active.  This issue comes
 *				into play when activating/suspending a DisplaySession.  This
 *				routine encapsulates the logic necessary to properly share
 *				the audio decoder between normal audio/video streams and
 *				EAS audio.
 *
 *	Returns:	uint32_t - decoder flag value, properly adjusted for EAS audio,
 *					to use when activating/suspending a display session
 *
 */

uint32_t
DisplaySession::getEasAdjustedDecoderFlag(bool easAudioActive)
{
    uint32_t adjustedFlag = mDecoderFlag;

    // if audio decoder originally requested
    if (mDecoderFlag & kCpeMedia_AudioDecoder)
    {
        // if EAS audio is currently playing back
        if (easAudioActive)
        {
            // remove request to manipulate the audio decoder
            adjustedFlag &= ~kCpeMedia_AudioDecoder;
        }
    }

    dlog(DL_SIG_EAS, DLOGL_NORMAL,
         "  DisplaySession::%s(), easAudioActive: %d    mDecoderFlag: 0x%x    adjustedFlag: 0x%x",
         __FUNCTION__, easAudioActive, mDecoderFlag, adjustedFlag);

    return adjustedFlag;
}


/********************************************************************************
 *
 *	Function:	StopAudio
 *
 *	Purpose:	In mediaservices, refer to audioPlayer.cpp.  This component
 *				manages EAS audio playback.  It is a requriement that Audio
 *				Player properly share audio resources with the rest of the
 *				STB.
 *
 *				Each time AudioPlayer initiates the EAS audio playback
 *				sequence, it executes this routine to properly stop the
 *				currently in focus audio stream.
 *
 */

void
DisplaySession::StopAudio(void)
{
    int error = 0;

    dlog(DL_SIG_EAS, DLOGL_NORMAL, "  DisplaySession::%s() enter, state: %d",
         __FUNCTION__, mState);

    if (mAudioFocus && (mState == kDisplaySessionStarted))
    {
        dlog(DL_SIG_EAS, DLOGL_NORMAL, "  DisplaySession::%s(), mediaHandle: %p, ptrPlaySession: %p",
             __FUNCTION__, mMediaHandle, mPtrPlaySession);

        // stop the audio decoder and error check
        error = cpe_media_Stop(mMediaHandle, kCpeMedia_AudioDecoder);
        if (error != kCpe_NoErr)
        {
            dlog(DL_SIG_EAS, DLOGL_ERROR, "    %s(), error: %d when stopping audio decoder", __FUNCTION__, error);
        }

        // conditionally update cam and error check.  The play session pointer will be NULL if
        //   tuned to an analog channel.
        if (mPtrPlaySession)
        {
            error = mPtrPlaySession->performCpeCamUpdate(mMediaHandle);
            if (error != kCpe_NoErr)
            {
                dlog(DL_SIG_EAS, DLOGL_ERROR, "    %s(), error: %d when updating cam", __FUNCTION__, error);
            }
        }
    }
}


/********************************************************************************
 *
 *	Function:	RestartAudio
 *
 *	Purpose:	In mediaservices, refer to audioPlayer.cpp.  This component
 *				manages EAS audio playback.  It is a requriement that Audio
 *				Player properly share audio resources with the rest of the
 *				STB.
 *
 *				Each time AudioPlayer completes the EAS audio playback
 *				sequence, it executes this routine to properly restart the
 *				currently in focus audio stream.
 *
 */

void
DisplaySession::RestartAudio(void)
{
    int error = 0;

    dlog(DL_SIG_EAS, DLOGL_NORMAL, "  DisplaySession::%s() enter, state: %d",
         __FUNCTION__, mState);

    if (mAudioFocus && (mState == kDisplaySessionStarted))
    {
        dlog(DL_SIG_EAS, DLOGL_NORMAL, "  DisplaySession::%s(), mediaHandle: %p, ptrPlaySession: %p",
             __FUNCTION__, mMediaHandle, mPtrPlaySession);

        // start the audio decoder and error check
        error = cpe_media_Start(mMediaHandle, kCpeMedia_AudioDecoder);
        if (error)
        {
            dlog(DL_SIG_EAS, DLOGL_ERROR, "    %s(), error: %d when starting audio decoder", __FUNCTION__, error);
        }

        // conditionally update cam and error check.  The play session pointer will be NULL if
        //   tuned to an analog channel.
        if (mPtrPlaySession)
        {
            error = mPtrPlaySession->performCpeCamUpdate(mMediaHandle);
            if (error)
            {
                dlog(DL_SIG_EAS, DLOGL_ERROR, "    %s(), error: %d when updating cam", __FUNCTION__, error);
            }
        }
        else
        {
            // we are on an analog channel.  It is necessary to set the output mode to
            //   restore analog audio.
            error  = Avpm::getAvpmInstance()->setAnalogOutputMode(mMediaHandle);
            if (error != kMspStatus_Ok)
            {
                dlog(DL_SIG_EAS, DLOGL_ERROR, "    %s(), error: %d when setting analog output mode", __FUNCTION__, error);
            }
        }
    }
}

