/** @file SeaChange_SessionControl.cpp
 *
 * @brief Session Control implementation for SeaChange.
 *
 * @author Amit Patel
 *
 * @version 1.0
 *
 * @date 12.27.2010
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */
#include <assert.h>
#include <ctime>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <sstream>
#include <iostream>
#include <cpe_networking_un.h>
#include <cpe_networking_main.h>

#include "dlog.h"
#include "vod.h"
#include "SeaChange_SessionControl.h"
#include "ondemand.h"
#include "csci-ciscocak-vod-api.h"

#ifdef LOG
#error  LOG already defined
#endif
#define LOG(level, msg, args...)  dlog(DL_MSP_ONDEMAND, level,"SeaChange_SessCntrl:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#ifdef HEREIAM
#error  HEREIAM already defined
#endif
#define HEREIAM  LOG(DLOGL_REALLY_NOISY, "HEREIAM")

#define FREEMEM {if(nodeGroupId)delete nodeGroupId;if(pAssetId)delete pAssetId; \
				if(pFunc)delete pFunc;if(pSubFunc)delete pSubFunc; \
				if(pBillId)delete pBillId;if(nodeGroupId)delete nodeGroupId; \
				if(pPurchTime)delete pPurchTime;if(seaReqData)delete seaReqData; \
	            if(appReqData)delete appReqData; if(setupObject)delete setupObject;}

using namespace std;

#define Close(x) do{dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_DEBUG,"%s:%s:%d ZZClose=%d",__FILE__,__FUNCTION__,__LINE__, x); close(x); (x)=-1;}while(0)


//---------------------
SeaChange_SessionControl::SeaChange_SessionControl(OnDemand* ptrOnDemand, const char* serviceUrl,
        void* pClientMetaDataContext,
        eOnDemandReqType reqType)
    : VOD_SessionControl(ptrOnDemand, reqType)
{
    //generate client id for dsmcc communication
    uint8_t *ptrId;
    assetId = 0;
    billingId = 0;
    purchaseTime = 0;
    timeRemaining = 0;
    caDescriptor = NULL;
    caDescriptorLength = 0;
    mCakresp = CAKSTAT_FAIL;
    mEncrypted = false;
    SessionFailCount = 0;
    memset(caEID, 0, EID_SIZE);

    parseSource(serviceUrl);

    ptrId = DsmccUtils::GenerateClientId(stbMacAddr);
    if (ptrId)
    {
        memcpy(clientId, ptrId, DSMCC_CLIENTID_LEN);
        delete [] ptrId;
        ptrId = NULL;
    }
    //generate server id for dsmcc communication
    ptrId = DsmccUtils::GenerateServerId(serverIpAddr);
    if (ptrId)
    {
        memcpy(serverId, ptrId, DSMCC_SERVERID_LEN);
        delete [] ptrId;
        ptrId = NULL;
    }

    dsmccBaseObj = new VodDsmcc_Base();

    sessionControlURL = serviceUrl;
    pClientMetaDataContext = pClientMetaDataContext;


}

void SeaChange_SessionControl::performCb(eCsciMspMpEventSessType type)
{
    tCsciMspMpEventSessMsg data;

//    if(mPreview)
//    {
//    	data.cbDataSize = asprintf(&data.cbData, "AssetCollectionId=%d&AssetId=%d&catId=%d&npt=%d&preview=true", appId, assetId, mCatId, (timeRemaining/1000));
//    }
//
//    else
//    {
//    	data.cbDataSize = asprintf(&data.cbData, "AssetCollectionId=%d&AssetId=%d&catId=%d&npt=%d&preview=false", appId, assetId, mCatId, (timeRemaining/1000));
//    }


    //For seachange the data must be in the format
    //AssetId=yyyyy1&AppId=yyyyyy2&npt=yyyyy3
    //Where AppId=0 is preview in Seachange. The npt is in second.
    data.cbDataSize = asprintf(&data.cbData, "AssetId=%d&AppId=%d&npt=%d", assetId, appId, (mNpt / 1000));

    LOG(DLOGL_NORMAL, "The call back Data is message is : %s and size is : %d .[%s][%d]\n ", data.cbData, data.cbDataSize, __FUNCTION__, __LINE__);

    if (data.cbDataSize > 0)
    {
        MspMpEventMgr * eventMgr = MspMpEventMgr::getMspMpEventMgrInstance();
        if (eventMgr)
        {
            data.eventSessVod = type;
            eventMgr->Msp_Mp_PerformCb(kCsciMspMpEventType_SessionExistenceVod, data);
            LOG(DLOGL_NORMAL, "Msp_Mp_PerformCb called");
        }
        else
        {
            LOG(DLOGL_NORMAL, "Warning : Msp_Mp_PerformCb can not called");
        }

        free(data.cbData);
    }

}


SeaChange_SessionControl::~SeaChange_SessionControl()
{
    if (NULL != dsmccBaseObj)
    {
        delete dsmccBaseObj;
        dsmccBaseObj = NULL;
    }

    if (0 != caDescriptorLength)
    {
        if (NULL != mpCakSessionHandler)
        {
            LOG(DLOGL_NOISE, " Releasing EID with CAM module :%x %x %x %x ", *(caEID), *(caEID + 1), *(caEID + 2), *(caEID + 3));
            mpCakSessionHandler->CiscoCak_SessionFinalize();
            delete mpCakSessionHandler;
            mpCakSessionHandler = NULL;
        }
        if (NULL != caDescriptor)
        {
            delete []caDescriptor;
            caDescriptor = NULL;
        }

        caDescriptorLength = 0;
    }

    performCb(kCsciMspMpEventSess_Torndown);
}

eIMediaPlayerStatus SeaChange_SessionControl::SessionSetup(const std::string& useUrl)
{
    FNLOG(DL_MSP_ONDEMAND);
    string Url = useUrl;
    uint8_t *data = NULL;
    uint32_t length = 0;
    VodDsmcc_NodeGroupId *nodeGroupId = NULL;
    VodDsmcc_AssetId *pAssetId = NULL;
    VodDsmcc_FunctionDesc *pFunc = NULL;
    VodDsmcc_BillingIdDesc *pBillId = NULL;
    VodDsmcc_PurchaseTimeDesc *pPurchTime = NULL;
    VodDsmcc_TimeRemainingDesc *pTimeRemaining = NULL;
    VodDsmcc_SubFunctionDesc *pSubFunc = NULL;
    VodDsmcc_SeaReqData *seaReqData = NULL;
    VodDsmcc_AppReqData *appReqData = NULL;
    VodDsmcc_ClientSessionSetup  *setupObject = NULL;

    uint8_t nodegroupid[NODE_GROUP_SIZE] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

    uint32_t sgId = OnDemandSystemClient::getInstance()->getSgId();

    if (!sgId)
    {
        LOG(DLOGL_ERROR, "Error invalid sgId: %d ", sgId);
        return kMediaPlayerStatus_ClientError;  // client
    }

    if (!assetId)
    {
        LOG(DLOGL_ERROR, "Error invalid assetId: %d ", assetId);
        return kMediaPlayerStatus_Error_InvalidURL;
    }

    LOG(DLOGL_NORMAL, "sgId: %d  assetId: %d appId: %d", sgId, assetId, appId);

    Utils::Put4Byte(nodegroupid + 2, sgId);

    nodeGroupId = new VodDsmcc_NodeGroupId(nodegroupid);
    if (!nodeGroupId)
    {
        FREEMEM
        return kMediaPlayerStatus_Error_OutOfMemory;
    }

    uint8_t assId[ASSET_ID_SIZE] = {0x00};// = {0x00,0x00,0x00,0x00,0x0F,0xB0, 0x00,0x2B};
    Utils::Put4Byte(assId + 0, appId);
    Utils::Put4Byte(assId + 4, assetId);
    pAssetId = new VodDsmcc_AssetId(assId);
    if (!pAssetId)
    {
        FREEMEM
        return kMediaPlayerStatus_Error_OutOfMemory;
    }

    pFunc = new VodDsmcc_FunctionDesc(0x02);
    if (!pFunc)
    {
        FREEMEM
        return kMediaPlayerStatus_Error_OutOfMemory;
    }
    pSubFunc = new VodDsmcc_SubFunctionDesc(0x02);
    if (!pSubFunc)
    {
        FREEMEM
        return kMediaPlayerStatus_Error_OutOfMemory;
    }

    vector<VodDsmcc_Descriptor> seaReqDesc;
    seaReqDesc.push_back(VodDsmcc_Descriptor(VENREQ_FUNCTION, 0x01 , pFunc));
    seaReqDesc.push_back(VodDsmcc_Descriptor(VENREQ_SUBFUNCTION, 0x01 , pSubFunc));
    seaReqData = new VodDsmcc_SeaReqData(0x01, 0x01, 0x02, seaReqDesc);
    if (!seaReqData)
    {
        FREEMEM
        return kMediaPlayerStatus_Error_OutOfMemory;
    }

    pBillId = new VodDsmcc_BillingIdDesc(billingId);
    if (!pBillId)
    {
        FREEMEM
        return kMediaPlayerStatus_Error_OutOfMemory;
    }

    pPurchTime = new VodDsmcc_PurchaseTimeDesc(purchaseTime);
    pTimeRemaining = new VodDsmcc_TimeRemainingDesc(timeRemaining);

    vector<VodDsmcc_Descriptor> appReqDesc;
    appReqDesc.push_back(VodDsmcc_Descriptor(APPREQ_BILLINGID, 0x04 , pBillId));
    appReqDesc.push_back(VodDsmcc_Descriptor(APPREQ_PURCHASETIME, 0x04 , pPurchTime));
    appReqDesc.push_back(VodDsmcc_Descriptor(APPREQ_TIMEREMAINING, 0x04 , pTimeRemaining));

    appReqData = new VodDsmcc_AppReqData(0x80, 0x01, 0x03, appReqDesc);
    if (!appReqData)
    {
        FREEMEM
        return kMediaPlayerStatus_Error_OutOfMemory;
    }

    vector<VodDsmcc_Descriptor> userPrivateDataDesc;
    userPrivateDataDesc.push_back(VodDsmcc_Descriptor(GENERIC_ASSETID, 0x08 , pAssetId));
    userPrivateDataDesc.push_back(VodDsmcc_Descriptor(GENERIC_NODEGROUPID, 0x06 , nodeGroupId));
    userPrivateDataDesc.push_back(VodDsmcc_Descriptor(GENERIC_VENREQ, 0x09 , seaReqData));
    userPrivateDataDesc.push_back(VodDsmcc_Descriptor(GENERIC_APPREQ, 0x15 , appReqData));

    VodDsmcc_UserPrivateData userPrivateData(0x01, 0x01, 0x04, userPrivateDataDesc);
    userPrivateData.SetPadBytes(0);
    VodDsmcc_UserData userData;

    userData.SetUuDataCount(0);
    userData.SetPrivateDataCount(55);
    userData.SetPrivateDataObj(userPrivateData);

    uint32_t transId = VodDsmcc_Base::getTransId();
    mTransSessionId = transId;
    uint8_t *sessId = NULL;
    uint8_t resIdAssigner = 0;
    // If session id assigner is one generate session id otherwise set to zero
    OnDemandSystemClient::getInstance()->GetSessionIdAssignor(&resIdAssigner);
    if (resIdAssigner)
    {
        sessId = DsmccUtils::GenerateSessionId(stbMacAddr, mVodSessionId);
        LOG(DLOGL_NORMAL, "Vod transID 0x%x", transId);
    }
    else
    {
        sessId = new ui8[DSMCC_SESSIONID_LEN];
        bzero(sessId, DSMCC_SESSIONID_LEN);
    }
    dsmccSessionId.SetSessionId(sessId);
    if (sessId)
    {
        delete []sessId;
        sessId = NULL;
    }

    setupObject = new VodDsmcc_ClientSessionSetup(dsmcc_ClientSessionSetUpRequest, transId,
            111,
            dsmccSessionId,
            clientId,
            serverId,
            userData);

    if (setupObject)
    {
        setupObject->PackDsmccMessageBody((uint8_t **)&data, &length);

        if (mSocketFd != -1)
        {
            LOG(DLOGL_NOISE, " srmIpAddr:%x port:%d ", srmIpAddr, srmPort);
            setupObject->SendMessage(mSocketFd, (i8 *)IPAddressIntToStr(srmIpAddr), srmPort, data, length);
            //add message to send message queue
            uint8_t retryCount = 0;
            OnDemandSystemClient::getInstance()->GetMessageRetryCount(&retryCount);
            //increment retry count by 1 to send error message in case response is not received from server
            retryCount++;
            SendMsgInfo info = {transId, dsmcc_ClientSessionSetUpRequest, data, length, NULL, NULL, NULL, mVodSessionId, retryCount};
            SendMsgInfo *msgInfo = new SendMsgInfo;
            if (msgInfo)
            {
                *msgInfo = info;
                AddMessageToSendList(msgInfo);
            }
        }
        delete setupObject;
        setupObject = NULL;
    }
    else
    {
        FREEMEM
        return kMediaPlayerStatus_Error_OutOfMemory;
    }

    return kMediaPlayerStatus_Ok;
}


eIOnDemandStatus SeaChange_SessionControl::SendKeepAlive()
{
    LOG(DLOGL_NOISE, "enter");
    eIOnDemandStatus status = ON_DEMAND_OK;
    uint8_t *data;
    uint32_t length = 0;
    vector <VodDsmcc_SessionId> sessIdList;
    sessIdList.push_back(dsmccSessionId);

    VodDsmcc_ClientSessionInProgress  *sessInProgObject = new VodDsmcc_ClientSessionInProgress(dsmcc_ClientSessionInProgressRequest,
            VodDsmcc_Base::getTransId(), 12, 1, sessIdList);

    if (sessInProgObject)
    {
        sessInProgObject->PackDsmccMessageBody((uint8_t **)&data, &length);

        if (mSocketFd != -1)
        {
            sessInProgObject->SendMessage(mSocketFd, (i8 *)IPAddressIntToStr(srmIpAddr), srmPort, data, length);
        }
        delete sessInProgObject;
        sessInProgObject = NULL;
    }

    return status;
}

eIOnDemandStatus SeaChange_SessionControl::SendConnectRequest()
{
    LOG(DLOGL_FUNCTION_CALLS, "enter");

    eIOnDemandStatus status = ON_DEMAND_ERROR;

    VodDsmcc_UserData userData;

    userData.SetUuDataCount(0);
    userData.SetPrivateDataCount(0);

    VodDsmcc_ClientConnect  *connectObject = new VodDsmcc_ClientConnect(dsmcc_ClientConnectRequest,
            VodDsmcc_Base::getTransId(),
            26,
            dsmccSessionId,
            userData);

    if (connectObject)
    {
        uint8_t *data;
        uint32_t length = 0;

        connectObject->PackDsmccMessageBody((uint8_t **)&data, &length);

        if (GetFD() != -1)
        {
            connectObject->SendMessage(GetFD(), (i8 *)IPAddressIntToStr(srmIpAddr), srmPort, data, length);
            status = ON_DEMAND_OK;
        }
        delete connectObject;
        connectObject = NULL;
    }

    return status;
}


eIOnDemandStatus SeaChange_SessionControl::SendResetRequest()
{
    // TODO:   WHEN IS THIS CALLED??

    eIOnDemandStatus status = ON_DEMAND_ERROR;
    uint8_t *data;
    uint32_t length = 0;

    VodDsmcc_ClientResetGeneric  *resetObject = new VodDsmcc_ClientResetGeneric(dsmcc_ClientResetRequest,
            VodDsmcc_Base::getTransId(),
            22,
            clientId,
            (uint16_t)dsmcc_RsnClSessionRelease);
    if (resetObject)
    {
        resetObject->PackDsmccMessageBody((uint8_t **)&data, &length);

        if (mSocketFd != -1)
        {
            resetObject->SendMessage(mSocketFd, (i8 *)IPAddressIntToStr(srmIpAddr), srmPort, data, length);
        }
        delete resetObject;
        resetObject = NULL;
    }

    return status;
}


//We will make this always as synchronous function call.
eIOnDemandStatus SeaChange_SessionControl::SessionTeardown()
{
    LOG(DLOGL_FUNCTION_CALLS, "enter");
    eIOnDemandStatus status = ON_DEMAND_ERROR;

    VodDsmcc_UserData userData;
    userData.SetUuDataCount(0);
    userData.SetPrivateDataCount(0);

    VodDsmcc_ClientReleaseGeneric  *releaseObject = new VodDsmcc_ClientReleaseGeneric(dsmcc_ClientReleaseRequest,
            VodDsmcc_Base::getTransId(),
            16,
            dsmccSessionId,
            dsmcc_RsnClSessionRelease,
            userData);
    if (releaseObject)
    {
        uint8_t *data;
        uint32_t length = 0;

        releaseObject->PackDsmccMessageBody((uint8_t **)&data, &length);
        if (mSocketFd != -1)
        {
            releaseObject->SendMessage(mSocketFd, (i8 *)IPAddressIntToStr(srmIpAddr), srmPort, data, length);

            LOG(DLOGL_NOISE, "VOD Session Release Msg sent");
            status = ON_DEMAND_OK;
        }
        delete releaseObject;
        releaseObject = NULL;
    }

    return status;
}

eIOnDemandStatus SeaChange_SessionControl::GetConnectionStatus()
{
    // TODO:   WHEN IS THIS CALLED??

    eIOnDemandStatus status = ON_DEMAND_ERROR;
    uint8_t *data;
    uint32_t length = 0;

    VodDsmcc_ClientStatusRequest  *statusObject = new VodDsmcc_ClientStatusRequest(dsmcc_ClientStatusRequest,
            VodDsmcc_Base::getTransId(),
            26,
            dsmcc_RsnOK, clientId,
            0x01,
            0x00,
            NULL);
    if (statusObject)
    {
        statusObject->PackDsmccMessageBody((uint8_t **)&data, &length);

        if (mSocketFd != -1)
        {
            statusObject->SendMessage(mSocketFd, (i8 *)IPAddressIntToStr(srmIpAddr), srmPort, data, length);
            status = ON_DEMAND_OK;
        }
        delete statusObject;
        statusObject = NULL;
    }

    return status;
}


//We will make this always as synchronous function call.
eIOnDemandStatus SeaChange_SessionControl::SendReleaseResponse()
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    uint8_t *data;
    uint32_t length = 0;

    eOnDemandState state = ptrOnDemand->GetOnDemandState();
    LOG(DLOGL_FUNCTION_CALLS, "%s:%i state[%d].", __FUNCTION__, __LINE__, state);

    if (state >= kOnDemandSessionServerReady)
    {
        VodDsmcc_UserData userData;
        userData.SetUuDataCount(0);
        userData.SetPrivateDataCount(0);

        VodDsmcc_ClientReleaseGeneric  *releaseObject = new VodDsmcc_ClientReleaseGeneric(dsmcc_ClientReleaseResponse,
                VodDsmcc_Base::getTransId(),
                16,
                dsmccSessionId,
                dsmcc_RsnSeSessionRelease,
                userData);
        if (releaseObject)
        {
            releaseObject->PackDsmccMessageBody((uint8_t **)&data, &length);

            if (mSocketFd != -1)
            {
                releaseObject->SendMessage(mSocketFd, (i8 *)IPAddressIntToStr(srmIpAddr), srmPort, data, length);
                status = ON_DEMAND_OK;
            }
            delete releaseObject;
            releaseObject = NULL;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "%s:%i Error wrong state[].", __FUNCTION__, __LINE__);
    }

    return status;
}


eIOnDemandStatus SeaChange_SessionControl::SendResetResponse()
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    uint8_t *data;
    uint32_t length = 0;

    VodDsmcc_ClientResetGeneric  *resetObject = new VodDsmcc_ClientResetGeneric(dsmcc_ClientResetResponse,
            VodDsmcc_Base::getTransId(),
            22,
            clientId,
            (uint16_t)dsmcc_RsnSeSessionRelease);
    if (resetObject)
    {
        resetObject->PackDsmccMessageBody((uint8_t **)&data, &length);

        if (mSocketFd != -1)
        {
            resetObject->SendMessage(mSocketFd, (char  *)IPAddressIntToStr(srmIpAddr), srmPort, data, length);
        }
        delete resetObject;
        resetObject = NULL;
    }

    return status;
}

void SeaChange_SessionControl::HandleSessionResponse(void *pMsg)
{
    VodDsmcc_Base *dsmccObj = (VodDsmcc_Base *)pMsg;

    LOG(DLOGL_FUNCTION_CALLS, "enter");

    if (dsmccObj)
    {
        LOG(DLOGL_NORMAL, " dsmccObj :%p", dsmccObj);
        //Handle dsmcc response type
        this->HandleInput((void *) dsmccObj);
    }
    else
    {
        LOG(DLOGL_ERROR, "NULL dsmccObj ");
    }
}

eIOnDemandStatus SeaChange_SessionControl::SendPendingMessage(SendMsgInfo *msgInfo)
{
    VOD_SessionControl *objPtr = msgInfo->objPtr;
    if (objPtr)
    {
        dsmccBaseObj->SendMessage(objPtr->GetFD(), (char *)IPAddressIntToStr(srmIpAddr), srmPort, msgInfo->msgData, msgInfo->msgLen);
    }
    return ON_DEMAND_OK;
}

void SeaChange_SessionControl::DisplayDebugInfo()
{
    LOG(DLOGL_REALLY_NOISY, "SeaChange_SessionControl::Purchase/View/Preview/Service request type %d", GetOnDemandReqType());
    LOG(DLOGL_REALLY_NOISY, "SeaChange_SessionControl::streamControlURL %s", streamControlURL.c_str());
    LOG(DLOGL_REALLY_NOISY, "SeaChange_SessionControl::keepAliveTimeOut %i", keepAliveTimeOut);
}

void SeaChange_SessionControl::CloseControlSocket()
{
    //Not Handle
}


eIOnDemandStatus SeaChange_SessionControl::HandleInput(void  *ptrMessage)
{
    eIOnDemandStatus status = ON_DEMAND_OK;
    VodDsmcc_Base * message = (VodDsmcc_Base *)ptrMessage;
    if (NULL == message)
    {
        return ON_DEMAND_ERROR;
    }
    SendMsgInfo msgInfo;

    msgInfo.transId = message->GetTransactionId();

    switch (message->GetMessageId())
    {
    case dsmcc_ClientSessionSetUpConfirm:
    {
        LOG(DLOGL_MINOR_EVENT, "dsmcc_ClientSessionSetUpConfirm");
        RemoveMessageFromSendList(&msgInfo);
        HandleSessionConfirmResp(message);       //  error or set state
    }
    break;

    case dsmcc_ClientReleaseConfirm:
    {
        LOG(DLOGL_MINOR_EVENT, "dsmcc_ClientReleaseConfirm");
        HandleReleaseConfirmResp(message);          // change state
        RemoveMessageFromSendList(&msgInfo);
    }
    break;

    case dsmcc_ClientReleaseIndication:
    {
        LOG(DLOGL_MINOR_EVENT, "dsmcc_ClientReleaseIndication");
        HandleReleaseIndicationResp(message);          // void
    }
    break;

    case dsmcc_ClientStatusConfirm:
    {
        LOG(DLOGL_MINOR_EVENT, "dsmcc_ClientStatusConfirm");
        HandleStatusConfirmResp(message);
        RemoveMessageFromSendList(&msgInfo);
    }
    break;

    case dsmcc_ClientResetConfirm:
    {
        LOG(DLOGL_MINOR_EVENT, "dsmcc_ClientResetConfirm");
        HandleResetConfirmResp(message);
        RemoveMessageFromSendList(&msgInfo);
    }
    break;

    case dsmcc_ClientResetIndication:
    {
        LOG(DLOGL_MINOR_EVENT, "dsmcc_ClientResetIndication");
        HandleResetIndicationResp(message);
    }
    break;

    default:
        break;
    }
    // free memory
    if (message)
    {
        delete message;
        message = NULL;
    }

    return status;
}

void SeaChange_SessionControl::HandleSessionConfirmResp(VodDsmcc_Base *message)
{
    FNLOG(DL_MSP_MPLAYER);

    VodDsmcc_ClientSessionConfirm *sessConfirm = (VodDsmcc_ClientSessionConfirm *) message;


    //update tunning param info from response message
    if (NULL != sessConfirm)
    {
        if (sessConfirm->GetResponse() == dsmcc_RspOK)
        {
            vector<VodDsmcc_ResourceDescriptor> resDesc = sessConfirm->GetResources();
            vector<VodDsmcc_ResourceDescriptor>::const_iterator itrResDesc;
            for (itrResDesc = resDesc.begin(); itrResDesc != resDesc.end(); itrResDesc++)
            {
                VodDsmcc_ResourceDescriptor desc = *itrResDesc;
                switch (desc.GetResourceDescriptorType())
                {
                case DSMCC_RESDESC_MPEGPROG:
                {
                    VodDsmcc_MPEGProgResData
                    *descValue = dynamic_cast<VodDsmcc_MPEGProgResData *>(desc.GetDescValue());
                    if (descValue)
                    {
                        mProgramNumber = descValue->GetMPEGProgramNumberValue();
                        LOG(DLOGL_REALLY_NOISY, "mProgramNumber: %x ", mProgramNumber);
                    }
                }
                break;
                case DSMCC_RESDESC_PHYSICALCHAN:
                {
                    VodDsmcc_PhysicalChannelResData
                    *descValue =
                        dynamic_cast<VodDsmcc_PhysicalChannelResData *>(desc.GetDescValue());
                    if (descValue)
                    {
                        mFrequency = descValue->GetChannelIdValue();
                        LOG(DLOGL_REALLY_NOISY, "frequency: %x ", mFrequency);
                    }
                }
                break;
                case DSMCC_RESDESC_DOWNSTREAMTRANS:
                {

                }
                break;
                case DSMCC_RESDESC_IP:
                {

                }
                break;
                case DSMCC_RESDESC_ATSCMODMODE:
                {

                    VodDsmcc_ATSCModulationData
                    *descValue =
                        dynamic_cast<VodDsmcc_ATSCModulationData *>(desc.GetDescValue());
                    if (descValue)
                    {
                        MspCommon::getCpeModeFormat(descValue->GetModulationFormat(), mMode);
                        mSymbolRate = descValue->GetSymbolRate();
                        LOG(DLOGL_REALLY_NOISY, "mMode: %x ", mMode);
                        LOG(DLOGL_REALLY_NOISY, "mSymbolRate: %x ", mSymbolRate);
                    }
                }
                break;
                case DSMCC_RESDESC_HEADENDID:
                {

                }
                break;
                case DSMCC_RESDESC_SERVERCA:
                {

                }
                break;
                case DSMCC_RESDESC_CLIENTCA: // for encrypted VOD asset only, N/A to clear VOD
                {
                    VodDsmcc_ClientCA
                    *descValue =
                        dynamic_cast<VodDsmcc_ClientCA *>(desc.GetDescValue());
                    if (descValue)
                    {
                        CakMsgRcvResult data;
                        caDescriptorLength = descValue->GetCaInfoLength();

                        uint8_t *caData = descValue->GetCaInfo();
                        if (caData)
                        {
                            caDescriptor = new uint8_t[caDescriptorLength];
                            memcpy(caDescriptor, caData, caDescriptorLength);
                        }
                        mEncrypted = true;
                        LOG(DLOGL_MINOR_DEBUG, "caDescriptorLength: %x ", caDescriptorLength);
                        LOG(DLOGL_MINOR_DEBUG, " caDescriptor: ");
                        for (ui32 i = 0; i < caDescriptorLength; i++)
                            LOG(DLOGL_MINOR_DEBUG,  "%2x ", caDescriptor[i]);

                        // Store the eid from ca descriptor which will be used when releasing the session.
                        Utils::GetBuffer(caDescriptor + EID_OFFSET_IN_DSMCC, caEID, EID_SIZE) ;
                        LOG(DLOGL_NOISE, " caEID :%x %x %x %x ", *(caEID), *(caEID + 1), *(caEID + 2), *(caEID + 3));

                        // get current EMM count
                        emmCount = 0;
                        memset(&data, 0, sizeof(CakMsgRcvResult));

                        data.caMsgType = eEMM;
                        if (!cam_getCakMsgRcvResult(0, &data))
                        {
                            emmCount = (uint32_t)data.seCureAcceptNum;
                        }

                        eMspStatus retval = kMspStatus_Ok;
                        mpCakSessionHandler = new CiscoCakSessionHandler(caDescriptor);
                        if (mpCakSessionHandler)
                        {
                            retval = mpCakSessionHandler->CiscoCak_SessionInitialize();
                            if (kMspStatus_Ok != retval)
                            {
                                LOG(DLOGL_ERROR, "Error: CiscoCak_SessionInitialize  FAILED !!!");
                            }
                        }
                        else
                        {
                            LOG(DLOGL_ERROR, "Error: CiscoCakSessionHandler class allocation  FAILED !!!");
                        }

                    }
                }
                break;
                case DSMCC_RESDESC_ETHERNETINT:
                {

                }
                break;
                case DSMCC_RESDESC_SERVICEGROUP:
                {

                }
                break;
                case DSMCC_RESDESC_VIRTUALCHAN:
                {

                }
                break;
                default:
                    break;
                }
            }

            //update streaming info from response message
            VodDsmcc_UserData userData = sessConfirm->GetUserData();
            //there is no user user data in this case
            VodDsmcc_UserPrivateData privateData = userData.GetPrivateDataObj();

            vector<VodDsmcc_Descriptor> Desc = privateData.GetDescriptor();
            vector<VodDsmcc_Descriptor>::const_iterator itrDesc;
            for (itrDesc = Desc.begin(); itrDesc != Desc.end(); itrDesc++)
            {
                VodDsmcc_Descriptor desc = *itrDesc;
                switch (desc.GetTag())
                {
                case GENERIC_IPDESC:
                {
                    VodDsmcc_IPType *descType =
                        dynamic_cast<VodDsmcc_IPType *>(desc.GetValue());
                    if (NULL != descType)
                    {
                        StreamServerIPAdd = IPAddressIntToStr(
                                                descType->GetIPAddress());
                        StreamServerPort = descType->GetIPPortNumber();
                        LOG(DLOGL_REALLY_NOISY, "StreamServerIPAdd: %s ", StreamServerIPAdd);
                        LOG(DLOGL_REALLY_NOISY, "StreamServerPort: %x ", StreamServerPort);
                    }
                }
                break;
                case GENERIC_STREAMHANDLE:
                {
                    VodDsmcc_StreamId *descType =
                        dynamic_cast<VodDsmcc_StreamId *>(desc.GetValue());
                    if (NULL != descType)
                    {
                        StreamHandle = descType->GetStreamHandle();
                        LOG(DLOGL_REALLY_NOISY, "StreamHandle: %x ", StreamHandle);
                    }
                }
                break;
                default:
                    break;
                }

            }

            //send connect request
            SendConnectRequest();

            performCb(kCsciMspMpEventSess_Created);

            //change state to server ready
            ptrOnDemand->queueEvent(kOnDemandSetupRespEvent);
        }
        else
        {
            LOG(DLOGL_ERROR, "WARNING - SERVER NOT READY - Invalid DSMCC response received : %x !!",
                sessConfirm->GetResponse());
            ptrOnDemand->queueEvent(kOnDemandSessionErrorEvent);
        }
    }
}

void SeaChange_SessionControl::HandleReleaseConfirmResp(VodDsmcc_Base *message)
{
    VodDsmcc_ClientReleaseGeneric *releaseConfirm =
        (VodDsmcc_ClientReleaseGeneric *) message;
    if (releaseConfirm)
    {
        LOG(DLOGL_MINOR_EVENT, "OnDemand::HandleReleaseConfirmResp release reason : %x",
            releaseConfirm->GetReason());
        //TODO: release resource
        ptrOnDemand->queueEvent(kOnDemandReleaseRespEvent);
    }
}

void SeaChange_SessionControl::HandleReleaseIndicationResp(VodDsmcc_Base *message)
{
    VodDsmcc_ClientReleaseGeneric *releaseIndication =
        (VodDsmcc_ClientReleaseGeneric *) message;

    if (releaseIndication)
    {
        LOG(DLOGL_MINOR_EVENT, " OnDemand::HandleReleaseIndicationResp release reason : %x",
            releaseIndication->GetReason());
        SendReleaseResponse();
    }
}

void SeaChange_SessionControl::HandleStatusConfirmResp(VodDsmcc_Base *message)
{
    VodDsmcc_ClientStatusConfirm *statusConfirm =
        (VodDsmcc_ClientStatusConfirm *) message;
    if (statusConfirm)
    {
        LOG(DLOGL_MINOR_EVENT, " OnDemand::HandleStatusConfirmResp status confirm reason : %x",
            statusConfirm->GetReason());
        LOG(DLOGL_MINOR_EVENT, " OnDemand::HandleStatusConfirmResp status type : %x",
            statusConfirm->GetStatusType());
        LOG(DLOGL_MINOR_EVENT, " OnDemand::HandleStatusConfirmResp status count : %x",
            statusConfirm->GetStatusCount());
        uint8_t *status = statusConfirm->GetStatusByte();
        LOG(DLOGL_MINOR_EVENT, " OnDemand::HandleStatusConfirmResp status byte : ");
        for (ui32 i = 0; i < statusConfirm->GetStatusCount(); i++)
            LOG(DLOGL_MINOR_DEBUG,  "%2x ", status[i]);
    }
}

void SeaChange_SessionControl::HandleResetConfirmResp(VodDsmcc_Base *message)
{
    VodDsmcc_ClientResetGeneric *resetConfirm =
        (VodDsmcc_ClientResetGeneric *) message;
    if (resetConfirm)
    {
        LOG(DLOGL_MINOR_EVENT, " OnDemand::HandleResetConfirmResp reset reason : %x",
            resetConfirm->GetReason());
    }
}

void SeaChange_SessionControl::HandleResetIndicationResp(VodDsmcc_Base *message)
{
    VodDsmcc_ClientResetGeneric *resetIndication =
        (VodDsmcc_ClientResetGeneric *) message;
    if (resetIndication)
    {
        LOG(DLOGL_MINOR_EVENT, " OnDemand::HandleResetIndicationResp reason : %x",
            resetIndication->GetReason());
        SendResetResponse();
    }
}

eIOnDemandStatus SeaChange_SessionControl::parseSource(std::string src)
{
    size_t pos, asPos, biPos, trPos, ptPos, endPos, strPos, epPos, apPos, cMgrPos;


    eIOnDemandStatus status = ON_DEMAND_OK;

    //lscp://AssetId=12345&BillingId=1234&PurchaseTime=123&RemainingTime=12
    LOG(DLOGL_FUNCTION_CALLS, "parseSource: source %s", src.c_str());

    pos = src.find("lscp://");
    if (pos == 0)
    {
        asPos = src.find("AssetId=");
        if (asPos != string::npos)
        {
            strPos = src.find("=", asPos);
            endPos = src.find("&", asPos);
            assetId = strtoul(src.substr(strPos + 1, endPos - strPos - 1).c_str(), 0, 0);
        }
        apPos = src.find("AppId=");
        if (apPos != string::npos)
        {
            strPos = src.find("=", apPos);
            endPos = src.find("&", apPos);
            appId = strtoul(src.substr(strPos + 1, endPos - strPos - 1).c_str(), 0, 0);
        }


        biPos = src.find("BillingId=");
        if (biPos != string::npos)
        {
            strPos = src.find("=", biPos);
            endPos = src.find("&", biPos);
            billingId = strtoul(src.substr((strPos + 1), (endPos - strPos - 1)).c_str(), 0, 0);
        }
        ptPos = src.find("PurchaseTime=");
        if (ptPos != string::npos)
        {
            strPos = src.find("=", ptPos);
            endPos = src.find("&", ptPos);
            purchaseTime = strtoul(src.substr(strPos + 1, endPos - strPos - 1).c_str(), 0, 0);
        }
        trPos = src.find("RemainingTime=");
        if (trPos != string::npos)
        {
            strPos = src.find("=", trPos);
            endPos = src.find("&", trPos);
            timeRemaining = strtoul(src.substr(strPos + 1, endPos - strPos - 1).c_str(), 0, 0);
        }
        epPos = src.find("EndPos=");
        if (epPos != string::npos)
        {
            strPos = src.find("=", epPos);
            endPos = src.find("&", epPos);
            endPosition = strtoul(src.substr(strPos + 1, endPos - strPos - 1).c_str(), 0, 0);
        }
        cMgrPos = src.find("connectMgrIp=");
        if (cMgrPos != string::npos)
        {
            strPos = src.find("=", cMgrPos);
            endPos = src.find("&", cMgrPos);
            serverIpAddr = strtoul(src.substr(strPos + 1, endPos - strPos - 1).c_str(), 0, 0);
        }


    }
    return status;
}
