/** @file CloudDvr_SessionControl.cpp
 *
 * @brief Session Control implementation for CloudDvr.
 *
 * @author Laliteshwar Prasad Yadav
 *
 * @version 1.0
 *
 * @date 07.30.2013
 *
 * Copyright: Cisco
 */
#include <ctime>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <sstream>
#include <iostream>
#include "dlog.h"
#include "CloudDvr_SessionControl.h"

using namespace std;

#ifdef LOG
#error  LOG already defined
#endif
#define LOG(level, msg, args...)  dlog(DL_MSP_ONDEMAND, level,"CloudDvr_SessCntrl:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#ifdef HEREIAM
#error  HEREIAM already defined
#endif
#define HEREIAM  LOG(DLOGL_REALLY_NOISY, "HEREIAM")
#define UNUSED_PARAM(a) (void)a;
#define FREEMEM {if(nodeGroupId)delete nodeGroupId;if(pAssetId)delete pAssetId; \
		         if(setupObject)delete setupObject;}
#define Close(x) do{dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_DEBUG,"%s:%s:%d ZZClose=%d",__FILE__,__FUNCTION__,__LINE__, x); close(x); (x)=-1;}while(0)
#define MAX_UINT32_VALUE    0xffffffff
#define MAX_SOCKET_PORT_VALUE 65535
#define MAX_FIELDS_IN_URL 3

#define CLOUD_DVR_EQUAL "="
#define CLOUD_DVR_DELIMITER "&"
#define VBO_IP "VboIpAddress="
#define SRM_PORT "SrmPort"
#define SRM_IP "SrmIpAddress"
#define STREAMER_IP "StreamerIpAddress"
#define STREAMER_PORT "StreamerPort"
#define REC_ID "recordingId="
#define APP_ID "AppId="
#define SERVICE_NAME "NDVR"


//---------------------
CloudDvr_SessionControl::CloudDvr_SessionControl(OnDemand* ptrOnDemand, const char* serviceUrl,
        void* pClientMetaDataContext,
        eOnDemandReqType reqType)
    : VOD_SessionControl(ptrOnDemand, reqType)
{
    uint8_t *ptrId = NULL;
    UNUSED_PARAM(pClientMetaDataContext)
    mStreamHandle = 0;
    mIsUrlValid = false;
    mAssetId = 0;
    mAppId = 0;
    mStreamerPort = 0;
    mStreamerIp = 0;
    memset(mServerId, 0, DSMCC_SERVERID_LEN);
    memset(mClientId, 0, DSMCC_CLIENTID_LEN);
    mpDsmccBaseObj = NULL;
    mEncrypted = false;
    mpCaDescriptor = NULL;
    mCaDescriptorLength = 0;
    mSessionFailCount = 0;
    mpCakSessionHandler = NULL;
    memset(caEID, 0, EID_SIZE);

    eIOnDemandStatus status = parseSource(serviceUrl);
    if (ON_DEMAND_OK == status)
    {
        ptrId = DsmccUtils::GenerateClientId(stbMacAddr);
        if (ptrId)
        {
            memcpy(mClientId, ptrId, DSMCC_CLIENTID_LEN);
            delete [] ptrId;
            ptrId = NULL;
        }
        //generate server id for dsmcc communication
        ptrId = DsmccUtils::GenerateServerId(serverIpAddr);
        if (ptrId)
        {
            memcpy(mServerId, ptrId, DSMCC_SERVERID_LEN);
            delete [] ptrId;
            ptrId = NULL;
        }
        sessionControlURL = serviceUrl;
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: parsing Failed!. Skipping clientId,serverId & socketFd creation");
    }

    LOG(DLOGL_SIGNIFICANT_EVENT, "Created CloudDvr_SessionController");
}

void CloudDvr_SessionControl::CreateDSMCCObj()
{
    FNLOG(DL_MSP_ONDEMAND);
    mpDsmccBaseObj = new VodDsmcc_Base();
}


CloudDvr_SessionControl::~CloudDvr_SessionControl()
{

    LOG(DLOGL_SIGNIFICANT_EVENT, "Destructed CloudDvr_SessionController");

    if (NULL != mpDsmccBaseObj)
    {
        delete mpDsmccBaseObj;
        mpDsmccBaseObj = NULL;
    }

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
}

eIMediaPlayerStatus CloudDvr_SessionControl::SessionSetup(const std::string& useUrl)
{
    FNLOG(DL_MSP_ONDEMAND);

    LOG(DLOGL_SIGNIFICANT_EVENT, "SessionSetup by CloudDvr_SessionController for Cloud Recorded Content Playback");

    eIMediaPlayerStatus status = kMediaPlayerStatus_Ok;
    string Url = useUrl;
    uint8_t *data = NULL;
    uint32_t length = 0;
    uint8_t nodegroupid[NODE_GROUP_SIZE] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint32_t sgId = 0;
    VodDsmcc_NodeGroupId *nodeGroupId = NULL;
    VodDsmcc_AssetId *pAssetId = NULL;
    VodDsmcc_ClientSessionSetup  *setupObject = NULL;
    uint32_t privateDatalength = 0;

    if (false == mIsUrlValid)
    {
        return kMediaPlayerStatus_Error_InvalidURL;
    }
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
    LOG(DLOGL_NORMAL, "ServiceGroupId: %d  AssetId: %d AppID: %d", sgId, mAssetId, mAppId);
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
    //User Private Data
    i8 service_name[SERVICE_SIZE] = { '\0' };
    snprintf(service_name, SERVICE_SIZE, "%s", SERVICE_NAME);
    ui8 service_gw[SERVICE_GW_SIZE] = { '\0' };

    uint32_t serviceDataLength = ASSET_ID_DESC_LEN + NODE_GRP_ID_DESC_LEN + SVC_DESC_COUNT_LEN;
    uint32_t serviceGatewayDataLength = serviceDataLength + SVC_GW_DATA_LEN;

    vector<VodDsmcc_Descriptor> userPrivateDataDesc;
    userPrivateDataDesc.push_back(VodDsmcc_Descriptor(GENERIC_ASSETID, ASSET_ID_LEN, pAssetId));
    userPrivateDataDesc.push_back(VodDsmcc_Descriptor(GENERIC_NODEGROUPID, NODEGRP_ID_LEN, nodeGroupId));
    VodDsmcc_UserPrivateData userPrivateData(SSP_PROTOCOL_ID_2, SSP_VERSION_1, DESC_COUNT, userPrivateDataDesc);
    privateDatalength = serviceGatewayDataLength + PRIVATE_DATA_DESC_LEN;

    userPrivateData.SetServiceGateway(service_gw);
    userPrivateData.SetServiceGatewayDataLength(serviceGatewayDataLength);
    userPrivateData.SetService((ui8*)service_name);
    userPrivateData.SetServiceDataLength(serviceDataLength);

    userPrivateData.SetPadBytes(0);
    VodDsmcc_UserData userData;
    userData.SetUuDataCount(0);
    userData.SetPrivateDataCount(privateDatalength);
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
    mDsmccSessionId.SetSessionId(sessId);
    if (sessId)
    {
        delete []sessId;
        sessId = NULL;
    }
    ui16 sessionSetupMsgLen = privateDatalength + SESSIONID_CLIENTID_SERVERID_USERDATA_LEN;
    setupObject = new VodDsmcc_ClientSessionSetup(dsmcc_ClientSessionSetUpRequest, transId,
            sessionSetupMsgLen,
            mDsmccSessionId,
            mClientId,
            mServerId,
            userData);

    if (setupObject)
    {
        setupObject->PackDsmccMessageBody((uint8_t **)&data, &length);
        if (mSocketFd != -1)
        {
            LOG(DLOGL_NOISE, " srmIpAddr:%x port:%d ", srmIpAddr, srmPort);
            bool msgRetval = setupObject->SendMessage(mSocketFd, (i8 *)IPAddressIntToStr(srmIpAddr), srmPort, data, length);
            LOG(DLOGL_SIGNIFICANT_EVENT, "Sent a dsmcc_ClientSessionSetUpRequest message");
            if (msgRetval == false)
            {
                LOG(DLOGL_ERROR, "SendMessage Failed");
            }
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
        else
        {
            LOG(DLOGL_ERROR, "Error: Socket is not open OR Socket open Failed ");
            status = kMediaPlayerStatus_Error_Unknown;
        }
        delete setupObject;
        setupObject = NULL;
    }
    else
    {
        FREEMEM
        status = kMediaPlayerStatus_Error_OutOfMemory;
    }

    return status;
}
//We will make this always as synchronous function call.
eIOnDemandStatus CloudDvr_SessionControl::SessionTeardown()
{
    FNLOG(DL_MSP_ONDEMAND);
    eIOnDemandStatus status = ON_DEMAND_ERROR;

    VodDsmcc_UserData userData;
    userData.SetUuDataCount(0);
    userData.SetPrivateDataCount(0);
    VodDsmcc_ClientReleaseGeneric  *releaseObject = new VodDsmcc_ClientReleaseGeneric(dsmcc_ClientReleaseRequest,
            VodDsmcc_Base::getTransId(),
            CLIENT_RELEASE_MSG_LENGTH,
            mDsmccSessionId,
            dsmcc_RsnClSessionRelease,
            userData);
    if (releaseObject)
    {
        uint8_t *data = NULL;
        uint32_t length = 0;

        releaseObject->PackDsmccMessageBody((uint8_t **)&data, &length);
        if (mSocketFd != -1)
        {
            bool retVal = releaseObject->SendMessage(mSocketFd, (i8 *)IPAddressIntToStr(srmIpAddr), srmPort, data, length);
            if (retVal)
            {
                LOG(DLOGL_SIGNIFICANT_EVENT, "Sent a dsmcc_ClientReleaseRequest message");
                status = ON_DEMAND_OK;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Error: Socket is not open OR Socket open Failed ");
        }
        delete releaseObject;
        releaseObject = NULL;
    }
    return status;
}

void CloudDvr_SessionControl::HandleSessionResponse(void *pMsg)
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

eIOnDemandStatus CloudDvr_SessionControl::HandleInput(void  *ptrMessage)
{
    VodDsmcc_Base * message = (VodDsmcc_Base *)ptrMessage;
    if (NULL == message)
    {
        return ON_DEMAND_ERROR;
    }

    eIOnDemandStatus status = ON_DEMAND_OK;
    SendMsgInfo msgInfo;
    msgInfo.transId = message->GetTransactionId();
    switch (message->GetMessageId())
    {

    case dsmcc_ClientSessionSetUpConfirm:
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "Received a dsmcc_ClientSessionSetUpConfirm message");
        HandleSessionConfirmResp(message);       //  error or set state
        RemoveMessageFromSendList(&msgInfo);
    }
    break;

    case dsmcc_ClientReleaseConfirm:
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "Received a dsmcc_ClientReleaseConfirm message");
        HandleReleaseConfirmResp(message);          // change state
        RemoveMessageFromSendList(&msgInfo);
    }
    break;

    case dsmcc_ClientReleaseIndication:
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "Received a dsmcc_ClientReleaseIndication message");
        HandleReleaseIndicationResp(message);          // void
    }
    break;

    case dsmcc_ClientStatusConfirm:
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "Received a dsmcc_ClientStatusConfirm message");
        HandleStatusConfirmResp(message);
        RemoveMessageFromSendList(&msgInfo);
    }
    break;

    case dsmcc_ClientResetConfirm:
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "Received a dsmcc_ClientResetConfirm message");
        HandleResetConfirmResp(message);
        RemoveMessageFromSendList(&msgInfo);
    }
    break;

    case dsmcc_ClientResetIndication:
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "Received a dsmcc_ClientResetIndication message");
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

void CloudDvr_SessionControl::HandleSessionConfirmResp(VodDsmcc_Base *message)
{
    FNLOG(DL_MSP_ONDEMAND);
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
                        LOG(DLOGL_REALLY_NOISY, "mMode: %x mSymbolRate: %x ", mMode, mSymbolRate);
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
                            if (mpCaDescriptor)
                            {
                                LOG(DLOGL_ERROR, "Preventing memory leak!!! The mpCaDescriptor should be NULL");
                                delete []mpCaDescriptor;
                                mpCaDescriptor = NULL;
                            }

                            mpCaDescriptor = new uint8_t[mCaDescriptorLength];
                            if (!mpCaDescriptor)
                            {
                                LOG(DLOGL_ERROR, "The mpCaDescriptor is NULL");
                                break;
                            }

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

                        if (mpCakSessionHandler)
                        {
                            LOG(DLOGL_ERROR, "Preventing memory leak!!! The mpCakSessionHandler should be NULL");
                            delete mpCakSessionHandler;
                            mpCakSessionHandler = NULL;
                        }

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
                        mStreamerIp = descType->GetIPAddress();
                        mStreamerPort = descType->GetIPPortNumber();

                        LOG(DLOGL_REALLY_NOISY, "mStreamerIp: %d mStreamerPort: %d ", mStreamerIp, mStreamerPort);
                    }
                }
                break;
                case GENERIC_STREAMHANDLE:
                {
                    VodDsmcc_StreamId *descType =
                        dynamic_cast<VodDsmcc_StreamId *>(desc.GetValue());
                    if (NULL != descType)
                    {
                        mStreamHandle = descType->GetStreamHandle();
                        LOG(DLOGL_REALLY_NOISY, "mStreamHandle: %x ", mStreamHandle);
                    }
                }
                break;
                default:
                    break;
                }

            }

            if (ptrOnDemand == NULL)
            {
                LOG(DLOGL_ERROR, "Ondemand instance is :%p ", ptrOnDemand);
            }
            else
            {
                ptrOnDemand->SetupCloudDVRStream();

                //change state to server ready
                ptrOnDemand->queueEvent(kOnDemandSetupRespEvent);
            }
        }
        else
        {
            LOG(DLOGL_SIGNIFICANT_EVENT, "WARNING - SERVER NOT READY - Invalid DSMCC response received : %x : %s",
                sessConfirm->GetResponse(), sessConfirm->GetResponseString(sessConfirm->GetResponse()).c_str());
            ptrOnDemand->queueEvent(kOnDemandSessionErrorEvent);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: NULL Session confirmation Response");
    }
}


tCpeSrcRFMode CloudDvr_SessionControl::getCpeModFormat(uint8_t mode)
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

eIOnDemandStatus CloudDvr_SessionControl::parseSource(std::string srcUrl)
{

    size_t firstPos, secondPos;

    eIOnDemandStatus status = ON_DEMAND_ERROR;
    int8_t numFields = 0;

    LOG(DLOGL_NORMAL, " sourceUri :%s", srcUrl.c_str());

    //If rtsp:// is repeatedly used, it is an Invalid URL
    firstPos = srcUrl.find(RTSP_SOURCE_URI_PREFIX);
    secondPos = srcUrl.rfind(RTSP_SOURCE_URI_PREFIX);
    if (firstPos != secondPos)
    {
        LOG(DLOGL_ERROR, " Invalid URL");
    }
    else
    {
        do
        {
            if (firstPos == 0)
            {
                if (parseInt(srcUrl, REC_ID, mAssetId) == true)
                {
                    numFields++;
                }
                else
                {
                    LOG(DLOGL_ERROR, "RecId not present in Service URL");
                    break;
                }

                if (parseInt(srcUrl, APP_ID, mAppId) == true)
                {
                    numFields++;
                }
                else
                {
                    LOG(DLOGL_ERROR, "App Id not present in Service URL");
                    break;
                }

                if (parsePort(srcUrl, SRM_PORT, srmPort) == true)
                {
                    LOG(DLOGL_NORMAL, "srm port  present in Service URL ");
                }
                else
                {
                    LOG(DLOGL_NORMAL, "srm port not present in Service URL, so will be taken from nps.ini ");
                }

                if (parseIP(srcUrl, SRM_IP, srmIpAddr) == true)
                {
                    LOG(DLOGL_NORMAL, "srm IP present in service URL");
                }
                else
                {
                    LOG(DLOGL_NORMAL, "srm IP not present in Service URL, so will be taken from nps.ini ");
                }

                if (parseIP(srcUrl, VBO_IP, serverIpAddr) == true)
                {
                    numFields++;
                }
                else
                {
                    LOG(DLOGL_ERROR, "VboIp not present in Service URL");
                    break;
                }

                if (parseIP(srcUrl, STREAMER_IP, mStreamerIp) == true)
                {
                    LOG(DLOGL_NORMAL, "Streamer IP present in Service URL ");
                }
                else
                {
                    LOG(DLOGL_NORMAL, "Streamer IP not present in Service URL, so will be taken from DSMCC MSG received on session setup response");
                }
                if (parsePort(srcUrl, STREAMER_PORT, mStreamerPort) == true)
                {
                    LOG(DLOGL_NORMAL, "Streamer port present in Service URL ");
                }
                else
                {
                    LOG(DLOGL_NORMAL, "Streamer Port  not present in Service URL, so will be taken from DSMCC MSG received on session setup response");
                }

            }

            else
            {
                LOG(DLOGL_ERROR, "URL is invalid");
            }

        }
        while (0);   //Even if url is not containing "rtsp://"
        //Execution should come out of do-while loop.
    }
    if (numFields == MAX_FIELDS_IN_URL)
    {
        status = ON_DEMAND_OK;
        mIsUrlValid = true;
    }
    else
    {
        LOG(DLOGL_ERROR,
            "RTSP URL parse error : All required tags not present in URL ");
        mIsUrlValid = false;
    }

    LOG(DLOGL_NORMAL,
        "RecordingId=%d\n VBOIpAddress=%x\n AppId=%d\n SrmIpAddress=%x\n SrmPort=%d StreamerIp=%x StreamerPort=%d \n",
        mAssetId, serverIpAddr, mAppId, srmIpAddr, srmPort, mStreamerIp, mStreamerPort);

    return status;
}


bool CloudDvr_SessionControl::validateDelimiter(const char* delimiter)
{
    bool retVal = false;
    string value;
    if (delimiter == NULL)
    {
        return retVal;
    }
    value.assign(delimiter - 1, delimiter);
    if (strcmp(value.c_str(), "&") == 0)
    {
        retVal = true;
    }
    else
    {
        value.assign(delimiter - strlen(RTSP_SOURCE_URI_PREFIX), delimiter);
        if (strcasecmp(value.c_str(), RTSP_SOURCE_URI_PREFIX) == 0)
        {
            retVal = true;
        }
    }
    return retVal;
}

eIOnDemandStatus CloudDvr_SessionControl:: parseTag(string strURL, const char * tag, string& value,
        bool isNumberString)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    const char *tagPosition, *startPosition, *endPosition = NULL;
    if (tag == NULL)
    {
        return status;
    }
    tagPosition = strcasestr(strURL.c_str(), tag);
    if (tagPosition != NULL)
    {
        bool retVal = validateDelimiter(tagPosition);
        if (false == retVal)
        {
            LOG(DLOGL_ERROR, " Invalid URL ");
            return status;
        }
        startPosition = strcasestr(tagPosition, CLOUD_DVR_EQUAL) + 1;
        endPosition = strcasestr(tagPosition, CLOUD_DVR_DELIMITER);
        value.assign(startPosition,
                     ((endPosition) ?
                      (endPosition - startPosition) : strlen(startPosition)));
        status = ON_DEMAND_OK;

        if (isNumberString)
        {
            if (value.size() == 0)
                status = ON_DEMAND_ERROR;
            for (uint32_t i = 0; i < value.size(); i++)
            {
                if (!isdigit(value[i]))
                {
                    LOG(DLOGL_ERROR, " input string is not Numeric string:%s ", value.c_str());
                    status = ON_DEMAND_ERROR;
                    break;
                }
            }
        }
    }
    else
    {
        LOG(DLOGL_ERROR, " parsing failed for tag:%s in url:%s",
            tag, strURL.c_str());
    }
    return status;
}

bool CloudDvr_SessionControl::parseInt(string srcUrl, string parseText, uint32_t& parseValue)
{
    string value;
    bool status = false;

    if (parseTag(srcUrl, parseText.c_str(), value, true) == ON_DEMAND_OK)
    {
        unsigned long long returnValue = strtoull(value.c_str(), 0, 0);
        if (returnValue > MAX_UINT32_VALUE)
        {
            LOG(DLOGL_ERROR, "parsed value  is INVALID:%lld", returnValue);
        }
        else
        {
            parseValue = returnValue;
            status = true;
        }
    }
    return status;
}

bool CloudDvr_SessionControl::parsePort(string srcUrl, string parseText, uint16_t& parseValue)
{
    string value;
    bool status = false;

    if (parseTag(srcUrl, parseText.c_str(), value, true) == ON_DEMAND_OK)
    {
        unsigned long long returnValue = strtoull(value.c_str(), 0, 0);
        if (returnValue > MAX_SOCKET_PORT_VALUE)
        {
            LOG(DLOGL_ERROR, "parsed value  is INVALID:%lld", returnValue);
        }
        else
        {
            parseValue = returnValue;
            status = true;
        }
    }
    return status;
}


bool CloudDvr_SessionControl::parseIP(string srcUrl, string parseText, uint32_t& parseValue)
{
    const char *strPos = NULL;
    const char *endPos = NULL;
    const char *ipPos = NULL;
    string value;
    bool status = false;
    if (srcUrl.size() == 0)
    {
        return status;
    }

    ipPos = strcasestr(srcUrl.c_str(), parseText.c_str());
    if (ipPos != NULL)
    {
        bool retVal = validateDelimiter(ipPos);
        if (false == retVal)
        {
            LOG(DLOGL_ERROR, " Invalid URL ");
            return status;
        }
        strPos = strcasestr(ipPos, CLOUD_DVR_EQUAL) + 1;
        endPos = strcasestr(ipPos, CLOUD_DVR_DELIMITER);
        value.assign(strPos, ((endPos) ? (endPos - strPos) : strlen(strPos)));

        unsigned long long servIp = strtoull(value.c_str(), 0, 16);
        if (servIp > MAX_UINT32_VALUE)
        {
            LOG(DLOGL_ERROR, " Invalid IP Address:%lld", servIp);
        }
        else
        {
            parseValue = servIp;
            status = true;
        }
    }
    return status;
}

void CloudDvr_SessionControl::CloseControlSocket()
{
    //Not Handled
}
void CloudDvr_SessionControl::DisplayDebugInfo()
{
    FNLOG(DL_MSP_ONDEMAND);
}
eIOnDemandStatus CloudDvr_SessionControl::SendPendingMessage(SendMsgInfo *msgInfo)
{
    FNLOG(DL_MSP_ONDEMAND);
    UNUSED_PARAM(msgInfo);
    eIOnDemandStatus status = ON_DEMAND_ERROR;

    VOD_SessionControl *objPtr = msgInfo->objPtr;
    if (objPtr)
    {
        if (mpDsmccBaseObj->SendMessage(objPtr->GetFD(), (char *)IPAddressIntToStr(srmIpAddr), srmPort, msgInfo->msgData, msgInfo->msgLen))
        {
            LOG(DLOGL_ERROR, " Successfully sent the message to the SRM");
            status = ON_DEMAND_OK;
        }
        else
        {
            LOG(DLOGL_ERROR, " Problem sending message to the SRM IPaddr:%s port:%d", (char *)IPAddressIntToStr(srmIpAddr), srmPort);
        }
    }

    return status;
}

eIOnDemandStatus CloudDvr_SessionControl::SendKeepAlive()
{
    FNLOG(DL_MSP_ONDEMAND);
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    return status;
}

eIOnDemandStatus CloudDvr_SessionControl::SendConnectRequest()
{
    FNLOG(DL_MSP_ONDEMAND);
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    VodDsmcc_UserData userData;

    userData.SetUuDataCount(0);
    userData.SetPrivateDataCount(0);

    VodDsmcc_ClientConnect  *connectObject = new VodDsmcc_ClientConnect(dsmcc_ClientConnectRequest,
            VodDsmcc_Base::getTransId(),
            CLIENT_CONNECT_REQ_MSG_LENGTH,
            mDsmccSessionId,
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

        LOG(DLOGL_SIGNIFICANT_EVENT, "Sent a dsmcc_ClientConnectRequest message");

        delete connectObject;
        connectObject = NULL;
    }
    return status;
}

eIOnDemandStatus CloudDvr_SessionControl::SendResetRequest()
{
    FNLOG(DL_MSP_ONDEMAND);
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    return status;
}

eIOnDemandStatus CloudDvr_SessionControl::GetConnectionStatus()
{
    FNLOG(DL_MSP_ONDEMAND);
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    return status;
}

eIOnDemandStatus CloudDvr_SessionControl::SendReleaseResponse()
{
    FNLOG(DL_MSP_ONDEMAND);
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
                CLIENT_RELEASE_MSG_LENGTH,
                mDsmccSessionId,
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

            LOG(DLOGL_SIGNIFICANT_EVENT, "Sent a dsmcc_ClientReleaseResponse message");

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

eIOnDemandStatus CloudDvr_SessionControl::SendResetResponse()
{
    FNLOG(DL_MSP_ONDEMAND);
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    return status;
}

void CloudDvr_SessionControl::HandleReleaseConfirmResp(VodDsmcc_Base *message)
{
    VodDsmcc_ClientReleaseGeneric *releaseConfirm =
        (VodDsmcc_ClientReleaseGeneric *) message;
    if (releaseConfirm)
    {
        LOG(DLOGL_MINOR_EVENT, "OnDemand::HandleReleaseConfirmResp release reason : %x: %s",
            releaseConfirm->GetReason(), releaseConfirm->GetReasonString(releaseConfirm->GetReason()).c_str());
        ptrOnDemand->queueEvent(kOnDemandReleaseRespEvent);
    }
}

void CloudDvr_SessionControl::HandleReleaseIndicationResp(VodDsmcc_Base *message)
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

void CloudDvr_SessionControl::HandleStatusConfirmResp(VodDsmcc_Base *message)
{
    UNUSED_PARAM(message)
}

void CloudDvr_SessionControl::HandleResetConfirmResp(VodDsmcc_Base *message)
{
    UNUSED_PARAM(message)
}

void CloudDvr_SessionControl::HandleResetIndicationResp(VodDsmcc_Base *message)
{
    UNUSED_PARAM(message)
}




