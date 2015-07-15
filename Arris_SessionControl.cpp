/** @file Arris_SessionControl.cpp
 *
 * @brief Session Control implementation for Arris-Vod.
 *
 * @author Rosaiah Naidu Mulluri
 *
 * @version 1.0
 *
 * @date 04.10.2012
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */
#include <stdio.h>
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
#include "Arris_SessionControl.h"
#include "ondemand.h"
#include "csci-ciscocak-vod-api.h"

#ifdef LOG
#error  LOG already defined
#endif
#define LOG(level, msg, args...)  dlog(DL_MSP_ONDEMAND, level,"Arris_SessCntrl:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#ifdef HEREIAM
#error  HEREIAM already defined
#endif
#define HEREIAM  LOG(DLOGL_REALLY_NOISY, "HEREIAM")

#define FREEMEM {if(nodeGroupId)delete nodeGroupId;if(pAssetId)delete pAssetId; \
		         if(appReqData)delete appReqData; if(setupObject)delete setupObject;}

using namespace std;

#define Close(x) do{dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_DEBUG,"%s:%s:%d ZZClose=%d",__FILE__,__FUNCTION__,__LINE__, x); close(x); (x)=-1;}while(0)


//---------------------
Arris_SessionControl::Arris_SessionControl(OnDemand* ptrOnDemand, const char* serviceUrl,
        void* pClientMetaDataContext,
        eOnDemandReqType reqType)
    : VOD_SessionControl(ptrOnDemand, reqType)
{
    //generate client id for dsmcc communication
    uint8_t *ptrId;
    mAssetId = 0;
    mNpt = 0;
    mBitRate = 0;
    mProviderId = 0;
    mCatId = 0;
    mPreview = false;
    memset(mUri, 0, 50);
    memset(mTitle, 0, 50);
    mpCaDescriptor = NULL;
    mCaDescriptorLength = 0;
    mCakresp = CAKSTAT_FAIL;
    mEncrypted = false;
    SessionFailCount = 0;
    memset(caEID, 0, EID_SIZE);
    mpCakSessionHandler = NULL;

    parseSource(serviceUrl);

    ptrId = DsmccUtils::GenerateClientId(stbMacAddr);
    if (ptrId)
    {
        memcpy(clientId, ptrId, DSMCC_CLIENTID_LEN);
        delete [] ptrId;
        ptrId = NULL;
    }
    //generate server id for dsmcc communication
    uint32_t serverIpAddr1 = htonl(serverIpAddr);
    serverIpAddr = 0;
    serverIpAddr = serverIpAddr1;

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

Arris_SessionControl::~Arris_SessionControl()
{
    if (NULL != dsmccBaseObj)
    {
        delete dsmccBaseObj;
        dsmccBaseObj = NULL;
    }
    if (0 != mCaDescriptorLength)
    {
        if (NULL != mpCakSessionHandler)
        {
            LOG(DLOGL_NOISE, " Releasing EID with CAM module :%x %x %x %x ", *(caEID), *(caEID + 1), *(caEID + 2), *(caEID + 3));
            mpCakSessionHandler->CiscoCak_SessionFinalize();
            delete mpCakSessionHandler;
            mpCakSessionHandler = NULL;
        }
        if (NULL != mpCaDescriptor)
        {
            delete []mpCaDescriptor;
            mpCaDescriptor = NULL;
        }

        mCaDescriptorLength = 0;
    }

    performCb(kCsciMspMpEventSess_Torndown);
}

eIMediaPlayerStatus Arris_SessionControl::SessionSetup(const std::string& useUrl)
{
    FNLOG(DL_MSP_ONDEMAND);
    string Url = useUrl;
    uint8_t *data = NULL;
    uint32_t length = 0;
    uint32_t padBytesCount = 0;
    uint32_t privateDatalength = 0;
    VodDsmcc_NodeGroupId *nodeGroupId = NULL;
    VodDsmcc_AssetId *pAssetId = NULL;
    VodDsmcc_ArrisAppReqData *appReqData = NULL;
    char appReqDataBuf[300] = {0};
    uint32_t appReqDataLength = 0;
    VodDsmcc_ClientSessionSetup  *setupObject = NULL;

    uint8_t nodegroupid[NODE_GROUP_SIZE] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

    uint32_t sgId = 0;
    sgId = OnDemandSystemClient::getInstance()->getSgId();

    if (!sgId)
    {
        LOG(DLOGL_ERROR, "Error invalid sgId: %d ", sgId);
        return kMediaPlayerStatus_ClientError;  // client
    }

    if (!mAssetId)
    {
        LOG(DLOGL_ERROR, "Error invalid mAssetId: %d ", mAssetId);
        return kMediaPlayerStatus_Error_InvalidURL;
    }

    LOG(DLOGL_NORMAL, "ServiceGroupId: %d  AssetId: %d AssetCollectionID: %d", sgId, mAssetId, mAppId);

    Utils::Put4Byte(nodegroupid + 2, sgId);

    nodeGroupId = new VodDsmcc_NodeGroupId(nodegroupid);
    if (!nodeGroupId)
    {
        FREEMEM
        return kMediaPlayerStatus_Error_OutOfMemory;
    }

    uint8_t assId[ASSET_ID_SIZE] = {0x00};


    Utils::Put4Byte(assId + 0, mAppId);
    Utils::Put4Byte(assId + 4, mAssetId);

    pAssetId = new VodDsmcc_AssetId(assId);
    if (!pAssetId)
    {
        FREEMEM
        return kMediaPlayerStatus_Error_OutOfMemory;
    }

    //TODO: Please wait for arris reply to add required app descriptors properly and do clean-up
    if (mPreview)
    {
        appReqDataLength = sprintf((char *)appReqDataBuf, "oda=noda;serviceArea=%d;npt=%d;catId=%d;preview=true", sgId, mNpt,  mCatId);
    }
    else
    {
        appReqDataLength = sprintf((char *)appReqDataBuf, "oda=noda;serviceArea=%d;npt=%d;catId=%d", sgId,  mNpt,  mCatId);
    }

    LOG(DLOGL_NORMAL, "length = %d \n AppReqString: %s \n", appReqDataLength, appReqDataBuf);

    appReqData = new VodDsmcc_ArrisAppReqData((uint8_t *)appReqDataBuf, appReqDataLength);
    if (!appReqData)
    {
        FREEMEM
        return kMediaPlayerStatus_Error_OutOfMemory;
    }

    vector<VodDsmcc_Descriptor> userPrivateDataDesc;
    userPrivateDataDesc.push_back(VodDsmcc_Descriptor(GENERIC_NODEGROUPID, 0x06 , nodeGroupId));
    userPrivateDataDesc.push_back(VodDsmcc_Descriptor(GENERIC_APPREQ, appReqDataLength , appReqData));
    userPrivateDataDesc.push_back(VodDsmcc_Descriptor(GENERIC_ASSETID, 0x08 , pAssetId));

    VodDsmcc_UserPrivateData userPrivateData(0x01, 0x00, 0x03, userPrivateDataDesc);

    privateDatalength = appReqDataLength + NODE_GROUP_ASSETID_LEN;
    padBytesCount = privateDatalength % ARRIS_PAD_BYTES;
    padBytesCount = ARRIS_PAD_BYTES - padBytesCount;
    privateDatalength = privateDatalength + padBytesCount;
    userPrivateData.SetPadBytes(padBytesCount);

    VodDsmcc_UserData userData;
    userData.SetUuDataCount(0);
    userData.SetPrivateDataCount(privateDatalength);
    userData.SetPrivateDataObj(userPrivateData);

    uint32_t transId1 = VodDsmcc_Base::getTransId();
    //Arris Vod Requrires Endiness Conversion
    uint32_t transId = htonl(transId1);
    mTransSessionId = transId;
    uint8_t *sessId = NULL;
    uint8_t resIdAssigner = 0;
    // If session id assigner is one generate session id otherwise set to zero
    OnDemandSystemClient::getInstance()->GetSessionIdAssignor(&resIdAssigner);
    if (resIdAssigner)
    {

        sessId = DsmccUtils::GenerateSessionId(stbMacAddr, mVodSessionId);
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
            privateDatalength + SERVERID_CLIENTID_LEN,
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


eIOnDemandStatus Arris_SessionControl::SendKeepAlive()
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

eIOnDemandStatus Arris_SessionControl::SendConnectRequest()
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


eIOnDemandStatus Arris_SessionControl::SendResetRequest()
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
eIOnDemandStatus Arris_SessionControl::SessionTeardown()
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

eIOnDemandStatus Arris_SessionControl::GetConnectionStatus()
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
eIOnDemandStatus Arris_SessionControl::SendReleaseResponse()
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


eIOnDemandStatus Arris_SessionControl::SendResetResponse()
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

void Arris_SessionControl::HandleSessionResponse(void *pMsg)
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

eIOnDemandStatus Arris_SessionControl::SendPendingMessage(SendMsgInfo *msgInfo)
{
    VOD_SessionControl *objPtr = msgInfo->objPtr;
    if (objPtr)
    {
        dsmccBaseObj->SendMessage(objPtr->GetFD(), (char *)IPAddressIntToStr(srmIpAddr), srmPort, msgInfo->msgData, msgInfo->msgLen);
    }
    return ON_DEMAND_OK;
}

void Arris_SessionControl::DisplayDebugInfo()
{
    LOG(DLOGL_REALLY_NOISY, "Arris_SessionControl::Purchase/View/Preview/Service request type %d", GetOnDemandReqType());
    LOG(DLOGL_REALLY_NOISY, "Arris_SessionControl::streamControlURL %s", streamControlURL.c_str());
    LOG(DLOGL_REALLY_NOISY, "Arris_SessionControl::keepAliveTimeOut %i", keepAliveTimeOut);
}

void Arris_SessionControl::CloseControlSocket()
{
    //Not Handled
}


eIOnDemandStatus Arris_SessionControl::HandleInput(void  *ptrMessage)
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
        HandleSessionConfirmResp(message);       //  error or set state
        RemoveMessageFromSendList(&msgInfo);
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
        LOG(DLOGL_REALLY_NOISY, "deleting Message with address: %p", message);
        delete message;
        message = NULL;
    }

    return status;
}

void Arris_SessionControl::HandleSessionConfirmResp(VodDsmcc_Base *message)
{
    FNLOG(DL_MSP_MPLAYER);

    VodDsmcc_ClientSessionConfirm *sessConfirm = (VodDsmcc_ClientSessionConfirm *) message;


    //update tunning param info from response message
    if (NULL != sessConfirm)
    {
        if (sessConfirm->GetResponse() == dsmcc_RspOK)
        {
            LOG(DLOGL_REALLY_NOISY, "Received dsmcc_RspOK");
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
                        mMode = getCpeModFormat(descValue->GetModulationFormat());
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
                        mCaDescriptorLength = descValue->GetCaInfoLength();

                        uint8_t *caData = descValue->GetCaInfo();
                        if (caData)
                        {
                            mpCaDescriptor = new uint8_t[mCaDescriptorLength];
                            memcpy(mpCaDescriptor, caData, mCaDescriptorLength);
                        }
                        mEncrypted = true;
                        LOG(DLOGL_MINOR_DEBUG, "caDescriptorLength: %x ", mCaDescriptorLength);
                        LOG(DLOGL_MINOR_DEBUG, " caDescriptor: ");
                        for (ui32 i = 0; i < mCaDescriptorLength; i++)
                            LOG(DLOGL_MINOR_DEBUG,  "%2x ", mpCaDescriptor[i]);

                        // Store the eid from ca descriptor which will be used when releasing the session.
                        Utils::GetBuffer(mpCaDescriptor + EID_OFFSET_IN_DSMCC, caEID, EID_SIZE) ;
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
                        mpCakSessionHandler = new CiscoCakSessionHandler(mpCaDescriptor);
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

            performCb(kCsciMspMpEventSess_Created);

            //change state to server ready
            ptrOnDemand->queueEvent(kOnDemandSetupRespEvent);
        }
        else
        {

            LOG(DLOGL_SIGNIFICANT_EVENT, "WARNING - SERVER NOT READY - Invalid DSMCC response received : %x : %s",
                sessConfirm->GetResponse(), sessConfirm->GetResponseString(sessConfirm->GetResponse()).c_str());
            ptrOnDemand->queueEvent(kOnDemandSessionErrorEvent);
        }
    }
}

void Arris_SessionControl::performCb(eCsciMspMpEventSessType type)
{
    tCsciMspMpEventSessMsg data;

    if (mPreview)
    {
        data.cbDataSize = asprintf(&data.cbData, "AssetCollectionId=%d&AssetId=%d&catId=%d&npt=%d&preview=true", mAppId, mAssetId, mCatId, (mNpt / 1000));
    }

    else
    {
        data.cbDataSize = asprintf(&data.cbData, "AssetCollectionId=%d&AssetId=%d&catId=%d&npt=%d&preview=false", mAppId, mAssetId, mCatId, (mNpt / 1000));
    }

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

void Arris_SessionControl::HandleReleaseConfirmResp(VodDsmcc_Base *message)
{
    VodDsmcc_ClientReleaseGeneric *releaseConfirm =
        (VodDsmcc_ClientReleaseGeneric *) message;
    if (releaseConfirm)
    {
        LOG(DLOGL_MINOR_EVENT, "OnDemand::HandleReleaseConfirmResp release reason : %x: %s",
            releaseConfirm->GetReason(), releaseConfirm->GetReasonString(releaseConfirm->GetReason()).c_str());
        //TODO: release resource
        ptrOnDemand->queueEvent(kOnDemandReleaseRespEvent);
    }
}

void Arris_SessionControl::HandleReleaseIndicationResp(VodDsmcc_Base *message)
{
    VodDsmcc_ClientReleaseGeneric *releaseIndication =
        (VodDsmcc_ClientReleaseGeneric *) message;

    if (releaseIndication)
    {
        LOG(DLOGL_MINOR_EVENT, " OnDemand::HandleReleaseIndicationResp release reason : %x: %s",
            releaseIndication->GetReason(), releaseIndication->GetReasonString(releaseIndication->GetReason()).c_str());
        SendReleaseResponse();
    }
}

void Arris_SessionControl::HandleStatusConfirmResp(VodDsmcc_Base *message)
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

void Arris_SessionControl::HandleResetConfirmResp(VodDsmcc_Base *message)
{
    VodDsmcc_ClientResetGeneric *resetConfirm =
        (VodDsmcc_ClientResetGeneric *) message;
    if (resetConfirm)
    {
        LOG(DLOGL_MINOR_EVENT, " OnDemand::HandleResetConfirmResp reset reason : %x",
            resetConfirm->GetReason());
    }
}

void Arris_SessionControl::HandleResetIndicationResp(VodDsmcc_Base *message)
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

tCpeSrcRFMode Arris_SessionControl::getCpeModFormat(uint8_t mode)
{
    tCpeSrcRFMode cpeMode = eCpeSrcRFMode_QAM16;

    switch (mode)
    {
    case Mode_QAM16:
        cpeMode = eCpeSrcRFMode_QAM16;
        break;
    case Mode_QAM32:
        cpeMode = eCpeSrcRFMode_QAM32;
        break;
    case Mode_QAM64:
        cpeMode = eCpeSrcRFMode_QAM64;
        break;
    case Mode_QAM128:
        cpeMode = eCpeSrcRFMode_QAM128;
        break;
    case Mode_QAM256:
        cpeMode = eCpeSrcRFMode_QAM256;
        break;
    default:
        cpeMode = eCpeSrcRFMode_BaseBandInput;
        break;
    }
    return cpeMode;
}


eIOnDemandStatus Arris_SessionControl::parseSource(std::string src)
{
    size_t pos, asPos, apPos, nptPos, uriPos, brPos, tPos, piPos, ciPos, endPos, strPos, cMgrPos, previewPos;


    eIOnDemandStatus status = ON_DEMAND_OK;

    //src = "lscp://AssetCollectionId=11802&AssetId=10458&npt=0&asset-string=GAME_OF219413_HOSD0596a9536.mpi&bitRate=3750000&title=New Destinations&providerId=10136&catId=10386";

    LOG(DLOGL_NORMAL, "parseSource: source %s", src.c_str());

    pos = src.find("lscp://");
    if (pos == 0)
    {
        asPos = src.find("AssetCollectionId=");
        if (asPos != string::npos)
        {
            strPos = src.find("=", asPos);
            endPos = src.find("&", asPos);
            mAppId = strtoul(src.substr(strPos + 1, endPos - strPos - 1).c_str(), 0, 0);
        }

        apPos = src.find("AssetId=");
        if (apPos != string::npos)
        {
            strPos = src.find("=", apPos);
            endPos = src.find("&", apPos);
            mAssetId = strtoul(src.substr(strPos + 1, endPos - strPos - 1).c_str(), 0, 0);
        }

        nptPos = src.find("npt=");
        if (nptPos != string::npos)
        {
            strPos = src.find("=", nptPos);
            endPos = src.find("&", nptPos);
            mNpt = strtoul(src.substr((strPos + 1), (endPos - strPos - 1)).c_str(), 0, 0);
        }

        uriPos = src.find("uri=");
        if (uriPos != string::npos)
        {
            strPos = src.find("=", uriPos);
            endPos = src.find("&", uriPos);
            strcpy(mUri, src.substr(strPos + 1, endPos - strPos - 1).c_str());
        }

        brPos = src.find("bitRate=");
        if (brPos != string::npos)
        {
            strPos = src.find("=", brPos);
            endPos = src.find("&", brPos);
            mBitRate = strtoul(src.substr(strPos + 1, endPos - strPos - 1).c_str(), 0, 0);
        }

        tPos = src.find("title=");
        if (tPos != string::npos)
        {
            strPos = src.find("=", tPos);
            endPos = src.find("&", tPos);
            strcpy(mTitle, src.substr(strPos + 1, endPos - strPos - 1).c_str());
        }

        piPos = src.find("providerId=");
        if (piPos != string::npos)
        {
            strPos = src.find("=", piPos);
            endPos = src.find("&", piPos);
            mProviderId = strtoul(src.substr(strPos + 1, endPos - strPos - 1).c_str(), 0, 0);
        }

        ciPos = src.find("catId=");
        if (ciPos != string::npos)
        {
            strPos = src.find("=", ciPos);
            endPos = src.find("&", ciPos);
            mCatId = strtoul(src.substr(strPos + 1, endPos - strPos - 1).c_str(), 0, 0);
        }

        cMgrPos = src.find("connectMgrIp=");
        if (cMgrPos != string::npos)
        {
            strPos = src.find("=", cMgrPos);
            endPos = src.find("&", cMgrPos);
            serverIpAddr = strtoul(src.substr(strPos + 1, endPos - strPos - 1).c_str(), 0, 0);
        }

        previewPos = src.find("preview=");
        if (previewPos != string::npos)
        {
            mPreview = true;
        }


    }

    LOG(DLOGL_NORMAL, "AssetCollectionId = %d\n  AssetId = %d\n npt = %d\n  uri = %s\n bitRate = %d\n title = %s\n mProviderId = %d\n catId = %d\n  connectMgrIp= %x \n  Preview = %d \n", mAppId, mAssetId, mNpt, mUri, mBitRate, mTitle, mProviderId, mCatId, serverIpAddr, mPreview);

    return status;
}
