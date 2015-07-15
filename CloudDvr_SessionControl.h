/** @file CloudDvr_SessionControl.h
 *
 * @brief CloudDvr SessionControl header.
 *
 * @author Laliteshwar Prasad Yadav
 *
 * @version 1.0
 *
 * @date 07.30.2013
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#ifndef _CLOUDDVR_SESSIONCONTROL_H
#define _CLOUDDVR_SESSIONCONTROL_H

#include "VOD_SessionControl.h"

#include "ondemand.h"
#include "CiscoCakSessionHandler.h"
#include <netinet/in.h>

#define EID_OFFSET_IN_DSMCC 						6
#define NODE_GROUP_ASSETID_LEN 						23 		//Fixed node Group ID and Asset Id Descriptor length
#define SESSIONID_CLIENTID_SERVERID_USERDATA_LEN 	56
#define CLIENT_RELEASE_MSG_LENGTH    				16
#define CLIENT_CONNECT_REQ_MSG_LENGTH				26
#define MAX_SOCKET_READ_FAIL_COUNT     				15  	//maximum socket read fail count
#define DESC_COUNT 									0x02
#define SVC_GW_COUNT 								0x1
#define NODE_GRP_ID_DESC_LEN 						8  		//tag+len+value
#define ASSET_ID_DESC_LEN  							10   	//tag+len+value
#define ASSET_ID_LEN  								0x08   	//Fixed Length
#define NODEGRP_ID_LEN  							0x06  	//Fixed Length
#define SVC_DESC_COUNT_LEN							0x01
#define SVC_GW_DATA_LEN								20		//Service(16) + ServiceDataLength(4)
#define SERVICE_GW_DESC_LEN 						21		//Service(16)+ServiceDataLength(4)+ descCount(1)
#define PRIVATE_DATA_DESC_LEN 						22 		//sspProtocolId(1)+sspVersion(1)+ ServiceGateway(16) + ServiceGatewayDataLength(4)

class CloudDvr_SessionControl: public VOD_SessionControl
{
public:
    CloudDvr_SessionControl(OnDemand* pOnDemand, const char* serviceUrl, void* pClientMetaDataContext,
                            eOnDemandReqType reqType);
    virtual ~CloudDvr_SessionControl();
    eIMediaPlayerStatus SessionSetup(const std::string& UseUrl);
    eIOnDemandStatus SendKeepAlive();
    eIOnDemandStatus SendConnectRequest();
    /**
    * \return status of SendResetRequest request.
    * \brief This function is used reset the connection between client and SRM.
    */
    eIOnDemandStatus SendResetRequest();
    /**
    * \return status of SendResetResponse request.
    * \brief This function is used to send reset response to SRM.
    */
    eIOnDemandStatus SendResetResponse();
    /**
    * \return status of the SessionTeardown operation.
    * \brief Send a SessionTeardown request message to SRM.
    */
    eIOnDemandStatus SessionTeardown();
    /**
    * \return status of GetConnectionStatus request.
    * \brief This function is used to get the status of SRM connection.
    */
    eIOnDemandStatus GetConnectionStatus();
    /**
    * \return status of the SendReleaseResponse operation.
    * \brief Send a SendReleaseResponse request message to SRM.
    */
    eIOnDemandStatus SendReleaseResponse();
    /**
    */
    void HandleSessionResponse(void *pMsg);
    /**
     * \param ptrMessage received message from the SRM.
    * \return status of the message received from the SRM.
    * \brief Handles all the response received from the SRM for Cloud DVR.
    */
    eIOnDemandStatus HandleInput(void  *ptrMessage);
    /**
    * \return status of SendPendingMessage request.
    * \brief This function is used send pending message to SRM.
    */
    eIOnDemandStatus SendPendingMessage(SendMsgInfo *msfInfo);
    /**
    * \brief This function is used to close the socket connection when CloudDvr session is tear down.
    */
    void CloseControlSocket();
    void DisplayDebugInfo();
    /**
    * \brief This function is used to create a dsmcc object during a session setup.
    */
    void CreateDSMCCObj();

    /**
    * \return RF modulation type in tCpeSrcRFMode format.
    * \brief This function convert RF modulation type from dsmcc to CPE format.
    */
    tCpeSrcRFMode getCpeModFormat(uint8_t mode);

    /**
    * \return mAssetId
    * \brief This function returns the assetId this particular StreamController currently controls.
    */

    uint32_t GetAssetId()
    {
        return mAssetId;
    };

    /**
    * \return StreamerIP address
    * \brief  This functions returns the streamer IP address in the dotted ip form.
    * The caller is responsible for the freeing up any non-NULL values returned.
    */
    uint8_t* GetStreamServerIPAddress()
    {
        uint8_t* IPaddr =  new uint8_t[INET_ADDRSTRLEN];
        if (IPaddr != NULL)
        {
            struct in_addr strIP;
            strIP.s_addr = mStreamerIp;
            inet_ntop(AF_INET, &(strIP.s_addr), (char*)IPaddr, INET_ADDRSTRLEN);
        }
        return IPaddr;
    } ;
    /**
    * \return Stream control server port which is used by stream controller for tcp connection.
    */
    uint16_t GetStreamServerPort()
    {
        return mStreamerPort;
    } ;

    /**
    * \return Stream Handle which is used by stream controller.
    */
    uint32_t GetStreamHandle()
    {
        return mStreamHandle;
    } ;

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
    eIOnDemandStatus parseSource(std::string srcUrl);
    eIOnDemandStatus parseTag(string strURL, const char * tag , string& value, bool isNumberString);
    bool validateDelimiter(const char* delimiter);
    bool parseInt(string srcUrl, string  parseText, uint32_t& parseValue);
    bool parsePort(string srcUrl, string  parseText, uint16_t& parseValue);
    bool parseIP(string srcUrl, string parseText, uint32_t& parseValue);
    /**
    * pointer to OnDemandSession.
    */
    VodDsmcc_Base *mpDsmccBaseObj;
    /**
    * session id.
    */
    VodDsmcc_SessionId mDsmccSessionId;
    /**
    * Client id for dsmcc communication.
    */
    CLIENTID mClientId;
    /**
    * Server id for dsmcc communication.
    */
    SERVERID mServerId;
    uint32_t mAssetId;
    uint32_t mAppId;
    uint16_t mStreamerPort;
    uint32_t mStreamerIp;
    uint32_t mStreamHandle;
    uint32_t mSessionFailCount;
    bool     mIsUrlValid;

    uint8_t *mpCaDescriptor;
    size_t mCaDescriptorLength;

    CiscoCakSessionHandler *mpCakSessionHandler;

};

#endif
