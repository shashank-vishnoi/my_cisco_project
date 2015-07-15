/** @file vod.h
 *
 * @brief VOD internal header.
 *
 * @author
 *
 * @version 1.0
 *
 * @date
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#ifndef _VOD_H
#define _VOD_H

#include <string>
using namespace std;

#ifdef __cplusplus
extern "C"
{
#endif

#define RTSP_KEEP_ALIVE_TIME_OUT_DEFAULT  180

    typedef enum
    {
        ON_DEMAND_TICKET_EXPIRED,
        ///< Indicates the server session has expired.  \n
        ON_DEMAND_BEGINNING_OF_STREAM,
        ///< Indicates the BOS(Beginning Of Stream) location reached.  \n
        ON_DEMAND_END_OF_STREAM,
        ///< Indicates the EOS(End Of Stream) location has been reached.  \n
        ON_DEMAND_PURCHASE_ASSET,
        ///< Asynchronous notification of the purchase asset request.  \n
        ON_DEMAND_PREPARE_TO_VIEW_ASSET,
        ///< Asynchronous notification of the prepare to view request.  \n
        ON_DEMAND_PREPARE_TO_PREVIEW_ASSET,
        ///< Asynchronous notification of the prepare to preview request.  \n
        ON_DEMAND_PURCHASE_SERVICE,
        ///< Asynchrounous notification of the purchase service request.  \n
        ON_DEMAND_START,
        ///< Asychrounous notification of the start request.  \n
        ON_DEMAND_STOP,
        ///< Asynchronous notification of the sop request.  \n
        ON_DEMAND_SPEED,
        ///< Asynchronous notification set speed request.  \n
        ON_DEMAND_SERVER_ERROR,
        ///< Announcement of an unexpected / unrecoverable system error.  \n
        ON_DEMAND_MEDIA_PLAYER,
        ///< Announcement of an unexpected / unrecoverable client error.  \n
        ON_DEMAND_INSUFFICIENT_BANDWIDTH_ERROR
        ///<  \n
    }
    eIOnDemandSignal;


    typedef enum
    {
        ON_DEMAND_OK,///< \n
        ON_DEMAND_ERROR,///< \n
        ON_DEMAND_INVALID_INPUT_ERROR,///< \n
        ON_DEMAND_NOTSUPPORTED_ERROR,///< \n
        ON_DEMAND_NOTAUTHORIZED_ERROR,///< \n
    } eIOnDemandStatus;


    typedef enum
    {
        eConnectLinkDown,      //0
        eConnectLinkUp,        //1
        eConnectLinkUpPending        //2
    } eConnectLinkState;

    typedef enum
    {
        ePurchasingAsssetType,
        ePreparingToViewType,
        ePreparingToPreviewType,
        ePurchaseServiceType
    } eOnDemandReqType;

    typedef enum
    {
        eStreamControl = 1,
        eSessionControl
    } eServerControlType;


#ifdef __cplusplus
}
#endif
#endif

