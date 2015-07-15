/** @file SeaChange_SessionControl.h
 *
 * @brief SeaChange SessionControl header.
 *
 * @author Amit Patel
 *
 * @version 1.0
 *
 * @date 12.27.2010
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#ifndef _SEACHANGE_SESSIONCONTROL_H
#define _SEACHANGE_SESSIONCONTROL_H

#include "VOD_SessionControl.h"

#include "vod.h"
#include "ondemand.h"
#include "csci-msp-mediaplayer-api.h"
#include "MspMpEventMgr.h"
#include "CiscoCakSessionHandler.h"

#define EID_OFFSET_IN_DSMCC 6

class OnDemand;

class SeaChange_SessionControl: public VOD_SessionControl
{
public:
    SeaChange_SessionControl(OnDemand* pOnDemand, const char* serviceUrl, void* pClientMetaDataContext,
                             eOnDemandReqType reqType);

    virtual ~SeaChange_SessionControl();

    eIMediaPlayerStatus SessionSetup(const std::string& UseUrl);

    eIOnDemandStatus SendKeepAlive();

    eIOnDemandStatus SendConnectRequest();

    /**
    * \return status of SendResetRequest request.
    * \brief This function is used reset the connection between client and network.
    */
    eIOnDemandStatus SendResetRequest();

    /**
    * \return status of SendResetResponse request.
    * \brief This function is used to send reset response to SRM.
    */
    eIOnDemandStatus SendResetResponse();

    /**
    * \return status of the SessionTeardown operation.
    * \brief Send a SessionTeardown request message to network.
    */
    eIOnDemandStatus SessionTeardown();

    /**
    * \return status of GetConnectionStatus request.
    * \brief This function is used to get the status of network connection.
    */
    eIOnDemandStatus GetConnectionStatus();

    /**
    * \return status of the SendReleaseResponse operation.
    * \brief Send a SendReleaseResponse request message to network.
    */
    eIOnDemandStatus SendReleaseResponse();


    /**
    */
    void HandleSessionResponse(void *pMsg);

    /**
     * \param ptrMessage received message from the network.
    * \return status of the message received from the network.
    * \brief Handles all the response received from the network for VOD.
    */
    eIOnDemandStatus HandleInput(void  *ptrMessage);

    /**
    * \return status of SendPendingMessage request.
    * \brief This function is used send pending message to network.
    */
    eIOnDemandStatus SendPendingMessage(SendMsgInfo *msfInfo);


    /**
    * \brief This function is used to close the socket connection when VOD session is tear down.
    */
    void CloseControlSocket();

    void DisplayDebugInfo();

private:

    /**
    * \param pointer to VodDsmcc_ClientSessionConfirm  message type
    * \brief handle function for dsmcc_ClientSessionSetUpConfirm message type
    */
    void HandleSessionConfirmResp(VodDsmcc_Base *message);
    /**
    * \param pointer to VodDsmcc_ClientReleaseGeneric message type
    * \brief handle function for dsmcc_ClientReleaseConfirm message type
    */
    void HandleReleaseConfirmResp(VodDsmcc_Base *message);
    /**
    * \param pointer to VodDsmcc_ClientReleaseGeneric message type
    * \brief handle function for dsmcc_ClientReleaseIndication message type
    */
    void HandleReleaseIndicationResp(VodDsmcc_Base *message);
    /**
    * \param pointer to VodDsmcc_ClientStatusConfirm message type
    * \brief handle function for dsmcc_ClientStatusConfirm message type
    */
    void HandleStatusConfirmResp(VodDsmcc_Base *message);
    /**
    * \param pointer to VodDsmcc_ClientResetGeneric message type
    * \brief handle function for dsmcc_ClientResetConfirm message type
    */
    void HandleResetConfirmResp(VodDsmcc_Base *message);
    /**
    * \param pointer to VodDsmcc_ClientResetGeneric message type
    * \brief handle function for dsmcc_ClientResetIndication message type
    */
    void HandleResetIndicationResp(VodDsmcc_Base *message);

    eIOnDemandStatus parseSource(std::string src);

    void performCb(eCsciMspMpEventSessType type);

    /**
    * pointer to OnDemandSession.
    */
    VodDsmcc_Base *dsmccBaseObj;
    /**
    * session id.
    */
    VodDsmcc_SessionId dsmccSessionId;

    /**
    * Client id for dsmcc communication.
    */
    CLIENTID clientId;

    /**
    * Server id for dsmcc communication.
    */
    SERVERID serverId;

    uint32_t assetId;
    uint32_t appId;
    uint32_t billingId;
    uint32_t purchaseTime;
    uint32_t timeRemaining;
    uint32_t mCatId;
    bool mPreview;

    uint8_t *caDescriptor;
    size_t caDescriptorLength;
    int SessionFailCount;

    CiscoCakSessionHandler *mpCakSessionHandler;
};

#endif
