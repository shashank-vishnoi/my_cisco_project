/** @file CiscoCaksessionHandler.cpp
 *
 * @brief CiscoCaksessionHandler source file.
 *
 * @author Laliteshwar Prasad Yadav
 *
 * @version 1.0
 *
 * @date 04.17.2013
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */


#include "CiscoCakSessionHandler.h"

///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"CiscoCaksessionHandler:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

CiscoCakSessionHandler::CiscoCakSessionHandler(const uint8_t emmPowerKey[CISCO_CAK_KEY_SIZE])
{
    FNLOG(DL_MSP_MPLAYER);

    memcpy(mEmm, emmPowerKey, CISCO_CAK_KEY_SIZE);

    memset(mEid, 0, EID_SIZE);

    memset(mSessionId, 0, SESSIONID_SIZE);

    for (uint32_t i = 0; i < CISCO_CAK_KEY_SIZE; i++)
    {
        LOG(DLOGL_NOISE, "%2x", mEmm[i])
    }

}

uint32_t CiscoCakSessionHandler::GetSessionId(void)
{
    FNLOG(DL_MSP_MPLAYER);
    static uint32_t sessionId = 0;
    return ++sessionId;
}

uint32_t CiscoCakSessionHandler::GetEid(void)
{
    FNLOG(DL_MSP_MPLAYER);
    uint32_t eid = 0;
    memcpy(&eid, (mEmm + EID_OFFSET_IN_EMM), EID_SIZE);

    return eid;
}

eMspStatus CiscoCakSessionHandler::CiscoCak_SessionInitialize(void)
{
    FNLOG(DL_MSP_MPLAYER);
    eMspStatus status = kMspStatus_Ok;

    //get unique session id and put into array
    uint32_t SessionId = GetSessionId();
    memcpy(mSessionId, &SessionId, SESSIONID_SIZE);

    //get EID so that it will be passed to CAK API call
    uint32_t eid = GetEid();
    memcpy(mEid, &eid, EID_SIZE);

    LOG(DLOGL_NOISE, " mEid :%x %x %x %x ", *(mEid), *(mEid + 1), *(mEid + 2), *(mEid + 3));
    LOG(DLOGL_NOISE, " mSessionId :%x %x %x %x ", *(mSessionId), *(mSessionId + 1), *(mSessionId + 2), *(mSessionId + 3));

    //Update CA information to CAM module
    eCamStatus camstatus = Csci_CiscoCak_SendDsmccCaInfo(mEmm, CISCO_CAK_KEY_SIZE, mSessionId);
    if (camstatus != kCamStatus_OK)
    {
        LOG(DLOGL_ERROR, "Error: CA Descriptor processing Failed !!!!camstatus :%d", camstatus);
        status = kMspStatus_Error;
    }

    return status;
}

eMspStatus CiscoCakSessionHandler::CiscoCak_SessionFinalize(void)
{
    FNLOG(DL_MSP_MPLAYER);
    eMspStatus status = kMspStatus_Ok;
    LOG(DLOGL_NOISE, " Releasing EID with CAM module :%x %x %x %x ", *(mEid), *(mEid + 1), *(mEid + 2), *(mEid + 3));
    LOG(DLOGL_NOISE, " mSessionId :%x %x %x %x ", *(mSessionId), *(mSessionId + 1), *(mSessionId + 2), *(mSessionId + 3));
    eCamStatus camstatus = Csci_CiscoCak_FinalizeVodSession(mEid, mSessionId);
    if (camstatus != kCamStatus_OK)
    {
        LOG(DLOGL_ERROR, "Error: Call to Csci_CiscoCak_FinalizeVodSession() Failed  !!!camstatus:%d", camstatus);
        status = kMspStatus_Error;
    }
    return status;
}

CiscoCakSessionHandler::~CiscoCakSessionHandler()
{
    FNLOG(DL_MSP_MPLAYER);
}
