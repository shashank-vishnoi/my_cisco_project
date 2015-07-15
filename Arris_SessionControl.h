/** @file Arris_SessionControl.h
 *
 * @brief Arris-Vod SessionControl header.
 *
 * @author Rosaiah Naidu Mulluri
 *
 * @version 1.0
 *
 * @date 04.10.2012
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#ifndef _ARRIS_SESSIONCONTROL_H
#define _ARRIS_SESSIONCONTROL_H

#include "VOD_SessionControl.h"

#include "vod.h"
#include "ondemand.h"
#include "MspMpEventMgr.h"
#include "CiscoCakSessionHandler.h"

#define EID_OFFSET_IN_DSMCC 6

#define ARRIS_PAD_BYTES 4 //Private data length should be padded to a 4-byte boundary
#define NODE_GROUP_ASSETID_LEN 23 //Fixed node Group ID and Asset Id Descriptor length
#define SERVERID_CLIENTID_LEN 56
class OnDemand;

class Arris_SessionControl: public VOD_SessionControl
{
public:
    Arris_SessionControl(OnDemand* pOnDemand, const char* serviceUrl, void* pClientMetaDataContext,
                         eOnDemandReqType reqType);

    virtual ~Arris_SessionControl();

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

    /**
    * \return RF modulation type in tCpeSrcRFMode format.
    * \brief This function convert RF modulation type from dsmcc to CPE format.
    */
    tCpeSrcRFMode getCpeModFormat(uint8_t mode);


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

    uint32_t mAssetId;
    uint32_t mAppId;
    char mUri[50];
    uint32_t mBitRate;
    char mTitle[50];
    uint32_t mProviderId;
    uint32_t mCatId;
    bool mPreview;

    uint8_t *mpCaDescriptor;
    size_t mCaDescriptorLength;
    int SessionFailCount;

    CiscoCakSessionHandler *mpCakSessionHandler;

};

#endif
