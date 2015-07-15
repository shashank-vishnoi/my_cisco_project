/** @file csci-ipclient-msp-api.cpp
 *
 * @author Anurag Raghavan <anuragha@cisco.com>
 * @author Karthik Venkateswaran <karthven@cisco.com>
 *
 * @date 01-14-2014
 *
 * @version 1.0
 *
 * @brief The CSCI API implementation.
 *
 */

#include "csci-ipclient-msp-api.h"
#include "dlog.h"
#include "IMediaPlayer.h"

/*
 * API for getting PaidserviceActive status
 *
 * @pre Introduced this method to get status of media services whether
 *      there is any ongoing VoD or PPV
 *
 * @return None
 */

bool Csci_IpClient_MediaService_IsPaidSessActive(void)
{
    bool status = false;
    IMediaPlayer* player = IMediaPlayer::getMediaPlayerInstance();
    if (player)
    {
        status = player->IMediaPlayerSession_IsServiceUrlActive();
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d)Error in creating the Player instance", __FUNCTION__, __LINE__);
    }
    return status;
}
