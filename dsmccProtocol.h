/** @file dsmccProtocol.h
 *
 * @brief dsmcc protocol header file.
 *
 * @author Amit Patel
 *
 * @version 1.0
 *
 * @date 12.27.2010
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#ifndef _DSMCC_PROTOCOL_H_
#define _DSMCC_PROTOCOL_H_

#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <cstring>
#include "vodUtils.h"
using namespace std;
#include <sstream>

// The DSM-CC message IDs
#define dsmcc_ClientSessionSetUpRequest	     0x4010
#define dsmcc_ClientSessionSetUpConfirm		 0x4011
#define dsmcc_ClientConnectRequest			 0x4090
#define dsmcc_ClientReleaseRequest			 0x4020
#define dsmcc_ClientReleaseConfirm			 0x4021
#define dsmcc_ClientReleaseIndication		 0x4022
#define dsmcc_ClientReleaseResponse			 0x4023
#define dsmcc_ClientResetRequest			 0x4070
#define dsmcc_ClientResetConfirm			 0x4071
#define dsmcc_ClientResetIndication			 0x4072
#define dsmcc_ClientResetResponse			 0x4073
#define dsmcc_ClientStatusRequest	         0x4060
#define dsmcc_ClientStatusConfirm			 0x4061
#define dsmcc_ClientSessionInProgressRequest 0x40b0

// The DSM-CC reason code
#define dsmcc_RsnOK               0x00
#define dsmcc_RsnNormal           0x01
#define dsmcc_RsnClProcError      0x02
#define dsmcc_RsnSEProcError      0x03
#define dsmcc_RsnClFormatError    0x05
#define dsmcc_RsnNeFormatError    0x06
#define dsmcc_RsnClNoSession	  0x10
#define dsmcc_RsnRetrans          0x13
#define dsmcc_RsnNoTransaction    0x14
#define dsmcc_RsnClSessionRelease 0x19
#define dsmcc_RsnSeSessionRelease 0x1A
#define dsmcc_RsnNeSessionRelease 0x1B

// The DSM-CC Response code

#define dsmcc_RspOK               						0x00
#define dsmcc_RspClNoSession      						0x01
#define dsmcc_RspNeNoCalls        						0x02
#define dsmcc_RspNeInvalidClient  						0x03
#define dsmcc_RspNeInvalidServer  						0x04
#define dsmcc_RspNeNoSession      						0x05
#define dsmcc_RspSeNoCalls        						0x06
#define dsmcc_RspSeInvalidClient 						0x07
#define dsmcc_RspSeNoService      						0x08
#define dsmcc_RspSeNoContinuousFeedSessionForServer     0x09
#define dsmcc_RspSeNoRspFromClient      				0x0a
#define dsmcc_RspSeNoRspFromServer      				0x0b
#define dsmcc_RspSeNoSession      						0x10
#define dsmcc_RspSeAltResourceValue      				0x11
#define dsmcc_RspSeNetworkUnableToAssignResources       0x12
#define dsmcc_RspSeNoErrInResourceCmd      				0x13
#define dsmcc_RspSeResourceHavAltValue      			0x14
#define dsmcc_RspSeNetWaitOnServer      				0x15
#define dsmcc_RspSeUnknwReqIdForClient      			0x16
#define dsmcc_RspSeClientCantUseResources      			0x17
#define dsmcc_RspSeClientRejects      					0x18
#define dsmcc_RspSeNetworkCantAssignResources      		0x19
#define dsmcc_RspSeNoResource     						0x20
#define dsmcc_RspSeProcError      						0x24
#define dsmcc_RspSeFormatError    						0x27



// DSM-CC resource descriptor type
#define DSMCC_RESDESC_MPEGPROG      	0x0003
#define DSMCC_RESDESC_PHYSICALCHAN		0x0004
#define DSMCC_RESDESC_DOWNSTREAMTRANS	0x0006
#define DSMCC_RESDESC_IP				0x0009
#define DSMCC_RESDESC_ATSCMODMODE		0xF001
#define DSMCC_RESDESC_HEADENDID			0xF003
#define DSMCC_RESDESC_SERVERCA			0xF004
#define DSMCC_RESDESC_CLIENTCA			0xF005
#define DSMCC_RESDESC_ETHERNETINT		0xF006
#define DSMCC_RESDESC_SERVICEGROUP		0xF007
#define DSMCC_RESDESC_VIRTUALCHAN		0xF101

// DSM-CC Generic descriptor type
#define GENERIC_ASSETID	    	0x01
#define GENERIC_NODEGROUPID		0x02
#define GENERIC_IPDESC      	0x03
#define GENERIC_STREAMHANDLE	0x04
#define GENERIC_APPREQ		    0x05
#define GENERIC_APPRES		    0x06
#define GENERIC_VENREQ		    0x80

//DSM-CC Application request descriptor type
#define APPREQ_BILLINGID	   	0x03
#define APPREQ_PURCHASETIME		0x04
#define APPREQ_TIMEREMAINING	0x05
#define APPREQ_SSPOPPROT		0x81

//DSM-CC Vendor specific descriptor type
#define VENREQ_FUNCTION	    	0x01
#define VENREQ_SUBFUNCTION		0x02
#define VENREQ_IPDESC   		0x03
#define VENREQ_PURCHASEID  		0x08
#define VENREQ_SMARTCARDID 		0x09
#define VENREQ_KEEPALIVE   		0x0A
#define VENREQ_TYPEINST   		0x0B
#define VENREQ_SIGANALOGCPYST	0x0C
#define VENREQ_SUPERCASID  		0x0E
#define VENREQ_PACKAGEID   		0x0F
#define VENREQ_IPTARGET   		0x10
#define VENREQ_ASSETINFO   		0x11
#define VENREQ_ON2IPTARGET 		0x12
#define VENREQ_IPV6TARGET  		0x15
#define VENREQ_CLIENTREQ   		0x16
#define VENREQ_SSPOPPROT   		0x81
#define VENREQ_STREAMID			0x83
#define VENREQ_PROIRITY   		0x84
#define VENREQ_FORCETSID   		0x85
#define VENREQ_FORCEDNP   		0x86
#define VENREQ_REQLOGENTRY 		0x87
#define VENREQ_PROVIDERID  		0x88
#define VENREQ_PROVASSETID		0x89
#define VENREQ_APPNAME   		0x8A
#define VENREQ_CPE_ID   		0x8B
#define VENREQ_BDT_AESA   		0x8C
#define VENREQ_BP_ID	   		0x8D
#define VENREQ_NPT		   		0x8E
#define VENREQ_EXTERNALSESSID	0x8F
#define VENREQ_SERVERAREANAME	0x90
#define VENREQ_STBID	   		0x91
#define VENREQ_VENDORCOPYPROT	0x92
#define VENREQ_STREAMSOURCEIP   0X93
#define VENREQ_ADSTYPE  		0x94
#define VENREQ_ADSDATA  		0x95
#define VENREQ_PLAYLISTDUR      0x96



//DSMCC message type
#define DSMCC_SESSMSG_TYPE      02
//DSMCC session id length
#define DSMCC_SESSIONID_LEN		10
//DSMCC client id length
#define DSMCC_CLIENTID_LEN 		20
//DSMCC server id length
#define DSMCC_SERVERID_LEN 		20
//DSMCC message header length
#define DSMCC_MSG_HEADER_SIZE   12


#define FUNCTION_DESC_SIZE          1
#define SUBFUNCTION_DESC_SIZE       1
#define DESC_BODY_SIZE              2
#define REQ_DESC_SIZE               3
#define TYPE_OWNER_SIZE             3
#define SIZE_FOUR_BYTE              4
#define NODE_GROUP_SIZE             6
#define CLIENT_STATUS_CONFIRM_SIZE  6
#define MAC_ID_SIZE                 6
#define IP_PORT_SIZE                6
#define CA_DESC_SIZE                6
#define ASSET_ID_SIZE               8
#define PHY_CHANNEL_DESC_SIZE       8
#define CLIENT_RESET_GEN_SIZE      12
#define ATSCMODULATION_DESC_SIZE   12
#define DSTSSTREAM_DESC_SIZE       12
#define RESOURCE_DESC_SIZE         14
#define MPEG_DESC_SIZE             16

//The following sizes are as per the SSP2.3 Private Data specification
//http://wwwin-eng.cisco.com/Eng/SPVTG/NorthAmCableBU/ClientSW/RTN/Software_Engineering/Architecture/Feature/Video-On-Demand/ARRIS_CMM_DSMCC_Client_Interface_016.pdf
#define SERVICE_GW_SIZE	           16
#define SERVICE_SIZE	           16
#define SSP_PROTOCOL_ID_1 		   0x01
#define SSP_PROTOCOL_ID_2 		   0x02
#define SSP_VERSION_0 			   0x00
#define SSP_VERSION_1 			   0x01


// enum for descriptor type
typedef enum
{
    GENERIC,
    APPREQ,
    VENDREQ
} Desc_Category;


//enum for user descriptor values
enum UserDescValueType
{
    E_AssetId,
    E_NodeGroupId,
    E_StreamHandle,
    E_AppRequestData,
    E_AppResponseData,
    E_SeaRequestData,
    E_DSTStreamDesc,
    E_MPEGProgResDesc,
    E_PhysicalChannelResDesc,
    E_ATSCModulationModeDesc,
    E_FunctionDesc,
    E_SubFunctionDesc,
    E_BillingIdDesc,
    E_PurchaseTimeDesc,
    E_TimeRemainingDesc,
    E_ClientCA,
    E_IPType,
    E_KeepAlive,
    E_SEAGeneric,
    E_ArrisAppRequestData
};


// typedef for server id
typedef ui8 SERVERID[DSMCC_SERVERID_LEN];
// typedef for client id
typedef ui8 CLIENTID[DSMCC_CLIENTID_LEN];


// Utility class provide method to get/set buffer values.
class DsmccUtils
{
public:
    static ui8* GenerateSessionId(ui8 *stbMac, ui32 transId);
    static ui8* GenerateClientId(ui8 *stbMac);
    static ui8* GenerateServerId(ui32 serverIPAdd);
};

// DSMCC base class contains members of the DSMCC headers. All other classes will derive from this class.
class VodDsmcc_Base
{
public:
    VodDsmcc_Base();
    virtual ~VodDsmcc_Base();
    VodDsmcc_Base(ui16 msgId, ui32 transId, ui16 msgLen);
    bool SendMessage(i32 socket, string ip, ui16 port, ui8 * data, ui32 length);
    static bool ReadMessageFromSocket(i32 socket, ui8 **pOut, ui32 * lenOut);

    static VodDsmcc_Base* GetMessageTypeObject(ui8 * data, ui32 length);
    status ParseDsmccMessageHdr(ui8 * data, ui32 length,
                                ui8 ** newData, ui32 *newLength);
    virtual status ParseDsmccMessageBody(ui8 * data, ui32 length);
    status PackDsmccMessageHdr(ui8 * msgData, ui32 msgDataLength, ui8 **pOut,
                               ui32 * outLength);
    virtual status PackDsmccMessageBody(ui8 **pOut, ui32 *len);

    static int GetSocket(ui16 port);

    status CloseSocket(int fd);
    static ui32 getTransId();


    //get and set method
    ui8 GetProtocolDiscriminator()
    {
        return ProtocolDiscriminator;
    }
    void SetProtocolDiscriminator(ui8 protocolDiscriminator)
    {
        ProtocolDiscriminator = protocolDiscriminator;
    }

    ui8 GetDsmccType()
    {
        return DsmccType;
    }
    void SetDsmccType(ui8 dsmccType)
    {
        DsmccType = dsmccType;
    }

    ui16 GetMessageId()
    {
        return MessageId;
    }
    void SetMessageId(ui16 MessageId)
    {
        MessageId = MessageId;
    }

    ui32 GetTransactionId()
    {
        return TransactionId;
    }
    void SetTransactionId(ui32 transactionId)
    {
        TransactionId = transactionId;
    }

    ui8 GetAdaptationLength()
    {
        return AdaptationLength;
    }
    void SetAdaptationLength(ui8 adaptationLength)
    {
        AdaptationLength = adaptationLength;
    }
    ui8 GetMessageLength()
    {
        return MessageLength;
    }
    void SetMessageLength(ui8 messageLength)
    {
        MessageLength = messageLength;
    }

private:

    //HDR
    ui8 ProtocolDiscriminator;
    ui8 DsmccType;
    ui16 MessageId;
    ui32 TransactionId;
    ui8 Reserved;
    ui8 AdaptationLength;
    ui16 MessageLength;
    ui8 AdaptationDataByte[50];
    status CreateDsmccMessageHeader(ui8 * msgData, ui32 msgDataLength,
                                    ui8 * adaptHeader, ui32 adaptHeaderLength, ui8 ** dsmccMsg,
                                    ui32 * dsmccMsgLength);
};

/////////////////////////////////////////////////
// base class for user /descriptor value type
class VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_UserDescValueType(UserDescValueType type)
    {
        dataType = type;
    }

    virtual ~VodDsmcc_UserDescValueType()
    {
    }

    UserDescValueType GetUserDescValueType()
    {
        return dataType;
    }

    void SetUserDescValueType(UserDescValueType &dataTypeVal)
    {
        dataType = dataTypeVal;
    }

    virtual ui8 * PackUserDescValueType(ui8 *pBuffer);
    virtual status ParseUserDescValueType(ui8 * data, ui32 length,
                                          ui8 ** newData, ui32 *newLength);

private:
    UserDescValueType dataType;
};
//#################################################################################

class VodDsmcc_SessionId
{
public:
    VodDsmcc_SessionId(ui8 * sessId)
    {
        if (NULL != sessId)
        {
            memcpy(SessId, sessId, DSMCC_SESSIONID_LEN);
        }
        else
        {
            memset(SessId, 0, DSMCC_SESSIONID_LEN);
        }
    }
    VodDsmcc_SessionId()
    {
        memset(SessId, 0, DSMCC_SESSIONID_LEN);
    }

    VodDsmcc_SessionId(const VodDsmcc_SessionId& vodSessionId)
    {
        memcpy(SessId, vodSessionId.SessId, DSMCC_SESSIONID_LEN);
    }

    ~VodDsmcc_SessionId() {}
    ui8 * GetSessionId()
    {
        return SessId;
    }
    void SetSessionId(ui8 * sessid)
    {
        if (sessid != NULL)
            memcpy(SessId, sessid, DSMCC_SESSIONID_LEN);
    }

private:
    ui8 SessId[DSMCC_SESSIONID_LEN];
};

//##################################################################################
class VodDsmcc_AssetId: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_AssetId(ui8 * assetid) :
        VodDsmcc_UserDescValueType(E_AssetId)
    {
        if (NULL != assetid)
        {
            memcpy(AssetId, assetid, ASSET_ID_SIZE);
        }
        else
        {
            memset(AssetId, 0, ASSET_ID_SIZE);
        }
    }

    VodDsmcc_AssetId() :
        VodDsmcc_UserDescValueType(E_AssetId)
    {
        memset(AssetId, 0, ASSET_ID_SIZE);
    }

    ~VodDsmcc_AssetId()
    {
    }

    ui8 * GetAssetId()
    {
        return AssetId;
    }

    void SetAssetId(ui8 * assetid)
    {
        if (AssetId && assetid)
            memcpy(AssetId, assetid, ASSET_ID_SIZE);
    }
    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui8 AssetId[ASSET_ID_SIZE];
};

//##################################################################################
class VodDsmcc_NodeGroupId: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_NodeGroupId(ui8 *nodeGroupId) :
        VodDsmcc_UserDescValueType(E_NodeGroupId)
    {
        if (NULL != nodeGroupId)
        {
            memcpy(NodeGroupId, nodeGroupId, NODE_GROUP_SIZE);
        }
        else
        {
            memset(NodeGroupId, 0, NODE_GROUP_SIZE);
        }
    }

    VodDsmcc_NodeGroupId() :
        VodDsmcc_UserDescValueType(E_NodeGroupId)
    {
        memset(NodeGroupId, 0, NODE_GROUP_SIZE);
    }

    ~VodDsmcc_NodeGroupId()
    {
    }

    ui8 * GetNodeGroupId()
    {
        return NodeGroupId;
    }

    void SetNodeGroupId(ui8 *nodeGroupId)
    {
        if (NodeGroupId && nodeGroupId)
            memcpy(NodeGroupId, nodeGroupId, NODE_GROUP_SIZE);
    }
    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);
private:
    ui8 NodeGroupId[NODE_GROUP_SIZE];

};

//##################################################################
class VodDsmcc_StreamId: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_StreamId(ui32 streamHandle) :
        VodDsmcc_UserDescValueType(E_StreamHandle)
    {
        StreamHandle = streamHandle;
    }

    VodDsmcc_StreamId() :
        VodDsmcc_UserDescValueType(E_SubFunctionDesc)
    {
        StreamHandle = 0;
    }
    ~VodDsmcc_StreamId()
    {
    }

    ui32 GetStreamHandle()
    {
        return StreamHandle;
    }

    void SetStreamHandle(ui32 streamHandle)
    {
        StreamHandle = streamHandle;
    }
    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui32 StreamHandle;
};

//##################################################################################
class VodDsmcc_Descriptor
{

private:
    ui8 Tag;
    ui8 Len;
    VodDsmcc_UserDescValueType *Value;

public:
    VodDsmcc_Descriptor(ui8 tag, ui8 len, VodDsmcc_UserDescValueType * value)
    {
        Tag = tag;
        Len = len;
        Value = value;
    }

    VodDsmcc_Descriptor()
    {
        Tag = 0;
        Len = 0;
        Value = NULL;
    }

    ~VodDsmcc_Descriptor()
    {
    }

    ui8 GetTag()
    {
        return Tag;
    }
    void SetTag(ui8 tag)
    {
        Tag = tag;
    }

    ui8 GetLen()
    {
        return Len;
    }
    void SetLen(ui8 len)
    {
        Len = len;
    }

    VodDsmcc_UserDescValueType * GetValue()
    {
        return Value;
    }
    void SetValue(VodDsmcc_UserDescValueType * value)
    {
        Value = value;
    }
    VodDsmcc_UserDescValueType *getDescriptorObjectFromType(
        Desc_Category category, ui8 type);
    ui8 * PackDescriptor(ui8 *pBuffer);
    status ParseDescriptor(Desc_Category category, ui8 * data, ui32 length,
                           ui8 ** newData, ui32 *newLength);
};

//##################################################################################
class VodDsmcc_SeaReqData: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_SeaReqData(ui8 sSPProtocolID, ui8 sSPVersion, ui8 descCount,
                        vector<VodDsmcc_Descriptor> desc) :
        VodDsmcc_UserDescValueType(E_SeaRequestData)
    {
        SSPProtocolID = sSPProtocolID;
        SSPVersion = sSPVersion;
        DescCount = descCount;
        Desc = desc;
    }

    VodDsmcc_SeaReqData() :
        VodDsmcc_UserDescValueType(E_SeaRequestData)
    {
        SSPProtocolID = 0;
        SSPVersion = 0;
        DescCount = 0;
    }

    ~VodDsmcc_SeaReqData();

    ui8 GetSSPProtocolID()
    {
        return SSPProtocolID;
    }
    void SetSSPProtocolID(ui8 sSPProtocolID)
    {
        SSPProtocolID = sSPProtocolID;
    }

    ui8 GetSSPVersion()
    {
        return SSPVersion;
    }
    void SetSSPVersion(ui8 sSPVersion)
    {
        SSPVersion = sSPVersion;
    }

    ui8 GetDescCount()
    {
        return DescCount;
    }
    void SetDescCount(ui8 descCount)
    {
        DescCount = descCount;
    }

    vector<VodDsmcc_Descriptor> GetDesc()
    {
        return Desc;
    }
    void SetDesc(vector<VodDsmcc_Descriptor> desc)
    {
        Desc = desc;
    }

    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui8 SSPProtocolID;
    ui8 SSPVersion;
    ui8 DescCount;
    vector<VodDsmcc_Descriptor> Desc;
};

//######################################################################
class VodDsmcc_ArrisAppReqData: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_ArrisAppReqData(ui8 *infoData, ui8 infoLen) :
        VodDsmcc_UserDescValueType(E_ArrisAppRequestData)
    {
        InfoData = new ui8[infoLen];
        if (InfoData && infoData)
        {
            memcpy(InfoData, infoData, infoLen);
            InfoLen = infoLen;
        }
    }

    VodDsmcc_ArrisAppReqData(ui8 infoLen) :
        VodDsmcc_UserDescValueType(E_ArrisAppRequestData)
    {
        InfoLen = infoLen;
        InfoData = new ui8[infoLen];
        if (InfoData)
        {
            memset(InfoData, 0, infoLen);
        }
    }

    ~VodDsmcc_ArrisAppReqData()
    {
        if (InfoData)
            delete[] InfoData;
    }

    ui8 * GetArrisAppReqData()
    {
        return InfoData;
    }

    void SetArrisAppReqData(ui8 * infoData, ui8 infoLen)
    {
        if (InfoData == NULL)
        {
            InfoData = new ui8[infoLen];
        }

        if (InfoData && infoData)
        {
            memcpy(InfoData, infoData, infoLen);
            InfoLen = infoLen;
        }
    }
    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui8 *InfoData;
    ui8 InfoLen;
};


//##################################################################################
class VodDsmcc_AppReqData: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_AppReqData(ui8 sSPProtocolID, ui8 sSPVersion, ui8 descCount,
                        vector<VodDsmcc_Descriptor> desc) :
        VodDsmcc_UserDescValueType(E_AppRequestData)
    {
        SSPProtocolID = sSPProtocolID;
        SSPVersion = sSPVersion;
        DescCount = descCount;
        Desc = desc;
    }

    VodDsmcc_AppReqData() :
        VodDsmcc_UserDescValueType(E_AppRequestData)
    {
        SSPProtocolID = 0;
        SSPVersion = 0;
        DescCount = 0;
    }

    ~VodDsmcc_AppReqData();

    ui8 GetSSPProtocolID()
    {
        return SSPProtocolID;
    }
    void SetSSPProtocolID(ui8 sSPProtocolID)
    {
        SSPProtocolID = sSPProtocolID;
    }

    ui8 GetSSPVersion()
    {
        return SSPVersion;
    }
    void SetSSPVersion(ui8 sSPVersion)
    {
        SSPVersion = sSPVersion;
    }

    ui8 GetDescCount()
    {
        return DescCount;
    }
    void SetDescCount(ui8 descCount)
    {
        DescCount = descCount;
    }

    vector<VodDsmcc_Descriptor> GetDesc()
    {
        return Desc;
    }
    void SetDesc(vector<VodDsmcc_Descriptor> desc)
    {
        Desc = desc;
    }
    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui8 SSPProtocolID;
    ui8 SSPVersion;
    ui8 DescCount;
    vector<VodDsmcc_Descriptor> Desc;
};

//##################################################################################
class VodDsmcc_AppResData: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_AppResData(ui8 sSPProtocolID, ui8 sSPVersion, ui8 descCount,
                        vector<VodDsmcc_Descriptor> desc) :
        VodDsmcc_UserDescValueType(E_AppResponseData)
    {
        mSSPProtocolID = sSPProtocolID;
        mSSPVersion = sSPVersion;
        mDescCount = descCount;
        mDesc = desc;
    }

    VodDsmcc_AppResData() :
        VodDsmcc_UserDescValueType(E_AppResponseData)
    {
        mSSPProtocolID = 0;
        mSSPVersion = 0;
        mDescCount = 0;
    }

    ~VodDsmcc_AppResData();

    ui8 GetSSPProtocolID()
    {
        return mSSPProtocolID;
    }
    void SetSSPProtocolID(ui8 sSPProtocolID)
    {
        mSSPProtocolID = sSPProtocolID;
    }

    ui8 GetSSPVersion()
    {
        return mSSPVersion;
    }
    void SetSSPVersion(ui8 sSPVersion)
    {
        mSSPVersion = sSPVersion;
    }

    ui8 GetDescCount()
    {
        return mDescCount;
    }
    void SetDescCount(ui8 descCount)
    {
        mDescCount = descCount;
    }

    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui8 mSSPProtocolID;
    ui8 mSSPVersion;
    ui8 mDescCount;
    vector<VodDsmcc_Descriptor> mDesc;
};

//##################################################################################
class VodDsmcc_DSTStreamData: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_DSTStreamData(ui16 downStreamBWType, ui32 downStreamBWValue,
                           ui16 downstreamTransportIdType, ui32 downstreamTransportIdValue) :
        VodDsmcc_UserDescValueType(E_DSTStreamDesc)
    {
        DownStreamBWType = downStreamBWType;
        DownStreamBWValue = downStreamBWValue;
        DownstreamTransportIdType = downstreamTransportIdType;
        DownstreamTransportIdValue = downstreamTransportIdValue;
    }

    VodDsmcc_DSTStreamData() :
        VodDsmcc_UserDescValueType(E_DSTStreamDesc)
    {
        DownStreamBWType = 0;
        DownStreamBWValue = 0;
        DownstreamTransportIdType = 0;
        DownstreamTransportIdValue = 0;
    }

    ~VodDsmcc_DSTStreamData()
    {
    }

    ui16 GetDownStreamBWType()
    {
        return DownStreamBWType;
    }
    void SetDownStreamBWType(ui16 downStreamBWType)
    {
        DownStreamBWType = downStreamBWType;
    }

    ui32 GetDownStreamBWValue()
    {
        return DownStreamBWValue;
    }
    void SetDownStreamBWValue(ui32 downStreamBWValue)
    {
        DownStreamBWValue = downStreamBWValue;
    }

    ui16 GetDownstreamTransportIdType()
    {
        return DownstreamTransportIdType;
    }
    void SetDownstreamTransportIdType(ui16 downstreamTransportIdType)
    {
        DownstreamTransportIdType = downstreamTransportIdType;
    }

    ui32 GetDownstreamTransportIdValue()
    {
        return DownstreamTransportIdValue;
    }
    void SetDownstreamTransportIdValue(ui32 downstreamTransportIdValue)
    {
        DownstreamTransportIdValue = downstreamTransportIdValue;
    }

    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui16 DownStreamBWType;
    ui32 DownStreamBWValue;
    ui16 DownstreamTransportIdType;
    ui32 DownstreamTransportIdValue;

};

//##################################################################################
class VodDsmcc_MPEGProgResData: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_MPEGProgResData(ui16 mPEGProgramNumberType,
                             ui16 mPEGProgramNumberValue, ui16 pMTPIDType, ui16 pMTPIDValue,
                             ui16 cAPIDValue, ui16 elementaryStreamCountValue, ui16 mPEGPCRType,
                             ui16 mPEGPCRValue) :
        VodDsmcc_UserDescValueType(E_MPEGProgResDesc)
    {
        MPEGProgramNumberType = mPEGProgramNumberType;
        MPEGProgramNumberValue = mPEGProgramNumberValue;
        PMTPIDType = pMTPIDType;
        PMTPIDValue = pMTPIDValue;
        CAPIDValue = cAPIDValue;
        ElementaryStreamCountValue = elementaryStreamCountValue;
        MPEGPCRType = mPEGPCRType;
        MPEGPCRValue = mPEGPCRValue;
    }

    VodDsmcc_MPEGProgResData() :
        VodDsmcc_UserDescValueType(E_MPEGProgResDesc)
    {
        MPEGProgramNumberType = 0;
        MPEGProgramNumberValue = 0;
        PMTPIDType = 0;
        PMTPIDValue = 0;
        CAPIDValue = 0;
        ElementaryStreamCountValue = 0;
        MPEGPCRType = 0;
        MPEGPCRValue = 0;
    }

    ~VodDsmcc_MPEGProgResData()
    {
    }

    ui16 GetMPEGProgramNumberType()
    {
        return MPEGProgramNumberType;
    }
    void SetMPEGProgramNumberType(ui16 mPEGProgramNumberType)
    {
        MPEGProgramNumberType = mPEGProgramNumberType;
    }

    ui16 GetMPEGProgramNumberValue()
    {
        return MPEGProgramNumberValue;
    }
    void SetMPEGProgramNumberValue(ui16 mPEGProgramNumberValue)
    {
        MPEGProgramNumberValue = mPEGProgramNumberValue;
    }

    ui16 GetPMTPIDType()
    {
        return PMTPIDType;
    }
    void SetPMTPIDType(ui16 pMTPIDType)
    {
        PMTPIDType = pMTPIDType;
    }

    ui16 GetPMTPIDValue()
    {
        return PMTPIDValue;
    }
    void SetPMTPIDValue(ui16 pMTPIDValue)
    {
        PMTPIDValue = pMTPIDValue;
    }

    ui16 GetCAPIDValue()
    {
        return CAPIDValue;
    }
    void SetCAPIDValue(ui16 cAPIDValue)
    {
        CAPIDValue = cAPIDValue;
    }

    ui16 GetElementaryStreamCountValue()
    {
        return ElementaryStreamCountValue;
    }
    void SetElementaryStreamCountValue(ui16 elementaryStreamCountValue)
    {
        ElementaryStreamCountValue = elementaryStreamCountValue;
    }

    ui16 GetMPEGPCRType()
    {
        return MPEGPCRType;
    }
    void SetMPEGPCRType(ui16 mPEGPCRType)
    {
        MPEGPCRType = mPEGPCRType;
    }

    ui16 GetMPEGPCRValue()
    {
        return MPEGPCRValue;
    }
    void SetMPEGPCRValue(ui16 mPEGPCRValue)
    {
        MPEGPCRValue = mPEGPCRValue;
    }

    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui16 MPEGProgramNumberType;
    ui16 MPEGProgramNumberValue;
    ui16 PMTPIDType;
    ui16 PMTPIDValue;
    ui16 CAPIDValue;
    ui16 ElementaryStreamCountValue;
    ui16 MPEGPCRType;
    ui16 MPEGPCRValue;
};

//##################################################################################
class VodDsmcc_PhysicalChannelResData: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_PhysicalChannelResData(ui16 channelIdType, ui32 channelIdValue,
                                    ui16 direction) :
        VodDsmcc_UserDescValueType(E_PhysicalChannelResDesc)
    {
        ChannelIdType = channelIdType;
        ChannelIdValue = channelIdValue;
        Direction = direction;
    }

    VodDsmcc_PhysicalChannelResData() :
        VodDsmcc_UserDescValueType(E_PhysicalChannelResDesc)
    {
        ChannelIdType = 0;
        ChannelIdValue = 0;
        Direction = 0;
    }
    ~VodDsmcc_PhysicalChannelResData()
    {
    }

    ui16 GetChannelIdType()
    {
        return ChannelIdType;
    }
    void SetChannelIdType(ui16 transmissionSystem)
    {
        ChannelIdType = transmissionSystem;
    }

    ui32 GetChannelIdValue()
    {
        return ChannelIdValue;
    }
    void SetChannelIdValue(ui32 channelIdValue)
    {
        ChannelIdValue = channelIdValue;
    }

    ui16 GetDirection()
    {
        return Direction;
    }
    void SetDirection(ui16 direction)
    {
        Direction = direction;
    }
    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui16 ChannelIdType;
    ui32 ChannelIdValue;
    ui16 Direction;
};

//##################################################################################
class VodDsmcc_ATSCModulationData: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_ATSCModulationData(ui8 transmissionSystem, ui8 innerCodingMode,
                                ui8 splitBitStreamMode, ui8 modulationFormat, ui32 symbolRate,
                                ui8 interleaveDepth, ui8 modulationMode, ui8 forwardErrorCorrection) :
        VodDsmcc_UserDescValueType(E_ATSCModulationModeDesc)
    {
        TransmissionSystem = transmissionSystem;
        InnerCodingMode = innerCodingMode;
        SplitBitStreamMode = splitBitStreamMode;
        ModulationFormat = modulationFormat;
        SymbolRate = symbolRate;
        InterleaveDepth = interleaveDepth;
        ModulationMode = modulationMode;
        ForwardErrorCorrection = forwardErrorCorrection;
    }

    VodDsmcc_ATSCModulationData() :
        VodDsmcc_UserDescValueType(E_ATSCModulationModeDesc)
    {
        TransmissionSystem = 0;
        InnerCodingMode = 0;
        SplitBitStreamMode = 0;
        ModulationFormat = 0;
        SymbolRate = 0;
        InterleaveDepth = 0;
        ModulationMode = 0;
        ForwardErrorCorrection = 0;
    }

    ~VodDsmcc_ATSCModulationData()
    {
    }

    ui8 GetTransmissionSystem()
    {
        return TransmissionSystem;
    }
    void SetTransmissionSystem(ui8 transmissionSystem)
    {
        TransmissionSystem = transmissionSystem;
    }

    ui8 GetInnerCodingMode()
    {
        return InnerCodingMode;
    }
    void SetInnerCodingMode(ui8 innerCodingMode)
    {
        InnerCodingMode = innerCodingMode;
    }

    ui8 GetSplitBitStreamMode()
    {
        return SplitBitStreamMode;
    }
    void SetSplitBitStreamMode(ui8 splitBitStreamMode)
    {
        SplitBitStreamMode = splitBitStreamMode;
    }

    ui8 GetModulationFormat()
    {
        return ModulationFormat;
    }
    void SetModulationFormat(ui8 modulationFormat)
    {
        ModulationFormat = modulationFormat;
    }

    ui32 GetSymbolRate()
    {
        return SymbolRate;
    }
    void SetSymbolRate(ui32 symbolRate)
    {
        SymbolRate = symbolRate;
    }

    ui8 GetInterleaveDepth()
    {
        return InterleaveDepth;
    }
    void SetInterleaveDepth(ui8 interleaveDepth)
    {
        InterleaveDepth = interleaveDepth;
    }

    ui8 GetModulationMode()
    {
        return ModulationMode;
    }
    void SetModulationMode(ui8 modulationMode)
    {
        ModulationMode = modulationMode;
    }

    ui8 GetForwardErrorCorrection()
    {
        return ForwardErrorCorrection;
    }
    void SetForwardErrorCorrection(ui8 forwardErrorCorrection)
    {
        ForwardErrorCorrection = forwardErrorCorrection;
    }
    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui8 TransmissionSystem;
    ui8 InnerCodingMode;
    ui8 SplitBitStreamMode;
    ui8 ModulationFormat;
    ui32 SymbolRate;
    ui8 Reserved;
    ui8 InterleaveDepth;
    ui8 ModulationMode;
    ui8 ForwardErrorCorrection;
};

//##################################################################
class VodDsmcc_FunctionDesc: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_FunctionDesc(ui8 function) :
        VodDsmcc_UserDescValueType(E_SubFunctionDesc)
    {
        Function = function;
    }

    VodDsmcc_FunctionDesc() :
        VodDsmcc_UserDescValueType(E_SubFunctionDesc)
    {
        Function = 0;
    }

    ~VodDsmcc_FunctionDesc()
    {
    }

    ui8 GetFunctionDesc()
    {
        return Function;
    }

    void SetFunctionDesc(ui8 function)
    {
        Function = function;
    }
    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui8 Function;
};

//##################################################################
class VodDsmcc_SubFunctionDesc: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_SubFunctionDesc(ui8 subFunction) :
        VodDsmcc_UserDescValueType(E_SubFunctionDesc)
    {
        SubFunction = subFunction;
    }

    VodDsmcc_SubFunctionDesc() :
        VodDsmcc_UserDescValueType(E_SubFunctionDesc)
    {
        SubFunction = 0;
    }

    ~VodDsmcc_SubFunctionDesc()
    {
    }

    ui8 GetSubFunctionDesc()
    {
        return SubFunction;
    }

    void SetSubFunctionDesc(ui8 subFunction)
    {
        SubFunction = subFunction;
    }
    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui8 SubFunction;
};

//##################################################################
class VodDsmcc_BillingIdDesc: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_BillingIdDesc(ui32 billingId) :
        VodDsmcc_UserDescValueType(E_BillingIdDesc)
    {
        BillingId = billingId;
    }

    VodDsmcc_BillingIdDesc() :
        VodDsmcc_UserDescValueType(E_BillingIdDesc)
    {
        BillingId = 0;
    }

    ~VodDsmcc_BillingIdDesc()
    {
    }

    ui32 GetBillingIdDesc()
    {
        return BillingId;
    }

    void SetBillingIdDesc(ui32 billingId)
    {
        BillingId = billingId;
    }
    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui32 BillingId;
};

//##################################################################
class VodDsmcc_PurchaseTimeDesc: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_PurchaseTimeDesc(ui32 purchaseTime) :
        VodDsmcc_UserDescValueType(E_PurchaseTimeDesc)
    {
        PurchaseTime = purchaseTime;
    }

    VodDsmcc_PurchaseTimeDesc() :
        VodDsmcc_UserDescValueType(E_PurchaseTimeDesc)
    {
        PurchaseTime = 0;
    }

    ~VodDsmcc_PurchaseTimeDesc()
    {
    }

    ui32 GetPurchaseTimeDesc()
    {
        return PurchaseTime;
    }

    void SetPurchaseTimeDesc(ui32 purchaseTime)
    {
        PurchaseTime = purchaseTime;
    }
    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui32 PurchaseTime;
};

//##################################################################
class VodDsmcc_TimeRemainingDesc: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_TimeRemainingDesc(ui32 timeRemaining) :
        VodDsmcc_UserDescValueType(E_TimeRemainingDesc)
    {
        TimeRemaining = timeRemaining;
    }

    VodDsmcc_TimeRemainingDesc() :
        VodDsmcc_UserDescValueType(E_TimeRemainingDesc)
    {
        TimeRemaining = 0;
    }

    ~VodDsmcc_TimeRemainingDesc()
    {
    }

    ui32 GetTimeRemainingDesc()
    {
        return TimeRemaining;
    }

    void SetTimeRemainingDesc(ui32 timeRemaining)
    {
        TimeRemaining = timeRemaining;
    }
    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui32 TimeRemaining;
};

//##################################################################
class VodDsmcc_IPType: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_IPType(ui16 iPPortNumber, ui32 iPAddress) :
        VodDsmcc_UserDescValueType(E_IPType)
    {
        IPPortNumber = iPPortNumber;
        IPAddress = iPAddress;
    }

    VodDsmcc_IPType() :
        VodDsmcc_UserDescValueType(E_IPType)
    {
        IPPortNumber = 0;
        IPAddress = 0;
    }

    ~VodDsmcc_IPType()
    {
    }

    ui16 GetIPPortNumber()
    {
        return IPPortNumber;
    }

    void SetIPPortNumber(ui16 iPPortNumber)
    {
        IPPortNumber = iPPortNumber;
    }
    ui32 GetIPAddress()
    {
        return IPAddress;
    }

    void SetIPAddress(ui32 iPAddress)
    {
        IPAddress = iPAddress;
    }

    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui16 IPPortNumber;
    ui32 IPAddress;
};


//##################################################################
class VodDsmcc_KeepAlive: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_KeepAlive(ui32 keepAliveTime):
        VodDsmcc_UserDescValueType(E_KeepAlive)
    {
        KeepAliveTime = keepAliveTime;
    }

    VodDsmcc_KeepAlive() :
        VodDsmcc_UserDescValueType(E_KeepAlive)
    {
        KeepAliveTime = 0;
    }

    ~VodDsmcc_KeepAlive()
    {
    }

    ui32 GetKeepAliveTime()
    {
        return KeepAliveTime;
    }

    void SetKeepAliveTime(ui32 keepAliveTime)
    {
        KeepAliveTime = keepAliveTime;
    }

    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui32 KeepAliveTime;
};

//##################################################################
class VodDsmcc_SEAGeneric: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_SEAGeneric(ui8 *infoData, ui8 infoLen) :
        VodDsmcc_UserDescValueType(E_SEAGeneric)
    {
        InfoData = new ui8[infoLen];
        if (InfoData && infoData)
        {
            memcpy(InfoData, infoData, infoLen);
            InfoLen = infoLen;
        }
    }

    VodDsmcc_SEAGeneric(ui8 infoLen) :
        VodDsmcc_UserDescValueType(E_SEAGeneric)
    {
        InfoLen = infoLen;
        InfoData = new ui8[infoLen];
        if (InfoData)
        {
            memset(InfoData, 0, infoLen);
        }
    }

    ~VodDsmcc_SEAGeneric()
    {
        if (InfoData)
            delete[] InfoData;
    }

    ui8 * GetSEAGeneric()
    {
        return InfoData;
    }

    void SetSEAGeneric(ui8 * infoData, ui8 infoLen)
    {
        if (InfoData == NULL)
        {
            InfoData = new ui8[infoLen];
        }
        if (InfoData && infoData)
        {
            memcpy(InfoData, infoData, infoLen);
            InfoLen = infoLen;
        }
    }

    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui8 *InfoData;
    ui8 InfoLen;
};


//##################################################################
class VodDsmcc_ClientCA: public VodDsmcc_UserDescValueType
{
public:
    VodDsmcc_ClientCA(ui32 caSystemId, ui16 caInfoLength, ui8 * caInfoByte) :
        VodDsmcc_UserDescValueType(E_ClientCA)
    {
        CaSystemId = caSystemId;
        if (NULL != caInfoByte)
        {
            CaInfoLength = caInfoLength;
            memcpy(CaInfoByte, caInfoByte, caInfoLength);
        }
        else
        {
            CaInfoLength = 0;
            memset(CaInfoByte, 0, 400);
        }
    }

    VodDsmcc_ClientCA() :
        VodDsmcc_UserDescValueType(E_ClientCA)
    {
        CaSystemId = 0;
        CaInfoLength = 0;
        memset(CaInfoByte, 0, 400);
    }

    ~VodDsmcc_ClientCA()
    {
    }

    ui32 GetCaSystemId()
    {
        return CaSystemId;
    }

    void SetCaSystemId(ui16 caSystemId)
    {
        CaSystemId = caSystemId;
    }

    ui32 GetCaInfoLength()
    {
        return CaInfoLength;
    }

    ui8 * GetCaInfo()
    {
        return CaInfoByte;
    }

    void SetCaInfo(ui32 caInfoLength, ui8 *caInfoByte)
    {
        if (CaInfoByte && caInfoByte)
        {
            CaInfoLength = caInfoLength;
            memcpy(CaInfoByte, caInfoByte, caInfoLength);
        }
    }

    ui8 * PackUserDescValueType(ui8 *pBuffer);
    status ParseUserDescValueType(ui8 * data, ui32 length, ui8 ** newData,
                                  ui32 *newLength);

private:
    ui32 CaSystemId;
    ui16 CaInfoLength;
    ui8 CaInfoByte[400];
};

//##################################################################################


class VodDsmcc_UserPrivateData
{
public:

    VodDsmcc_UserPrivateData(ui8 sSPProtocolID, ui8 sSPVersion, ui8 descCount,
                             vector<VodDsmcc_Descriptor> desc)
    {
        SSPProtocolID = sSPProtocolID;
        SSPVersion = sSPVersion;
        memset(ServiceGateway, '\0', SERVICE_GW_SIZE);
        DescCount = descCount;
        Desc = desc;
        ServiceGatewayDataLength = 0;
        memset(Service, '\0', SERVICE_SIZE);
        ServiceDataLength = 0;
        PadBytes = 0;
    }
    VodDsmcc_UserPrivateData()
    {
        SSPProtocolID = 0;
        SSPVersion = 0;
        memset(ServiceGateway, '\0', SERVICE_GW_SIZE);
        DescCount = 0;
        PadBytes = 0;
        ServiceGatewayDataLength = 0;
        memset(Service, '\0', SERVICE_SIZE);
        ServiceDataLength = 0;
    }
    ~VodDsmcc_UserPrivateData()
    {
    }

    ui8 GetSSPProtocolID()
    {
        return SSPProtocolID;
    }
    ui8 GetSSSPVersion()
    {
        return SSPVersion;
    }
    ui8 GetdescCount()
    {
        return DescCount;
    }
    vector<VodDsmcc_Descriptor> GetDescriptor()
    {
        return Desc;
    }

    void SetSSPProtocolID(ui8 protocolId)
    {
        SSPProtocolID = protocolId;
    }
    void SetSSPVersion(ui8 version)
    {
        SSPVersion = version;
    }
    void SetdescCount(ui8 count)
    {
        DescCount = count;
    }
    void SetServiceGateway(ui8* svcGateway)
    {
        memcpy(ServiceGateway, svcGateway, SERVICE_GW_SIZE);
    }
    ui8* GetServiceGateway()
    {
        return ServiceGateway;
    }
    void SetService(ui8* service)
    {
        memcpy(Service, service, SERVICE_SIZE);
    }
    ui8* GetService()
    {
        return Service;
    }
    void SetServiceGatewayDataLength(uint32_t len)
    {
        ServiceGatewayDataLength = len;
    }
    uint32_t GetServiceGatewayDataLength()
    {
        return ServiceGatewayDataLength;
    }
    void SetServiceDataLength(uint32_t len)
    {
        ServiceDataLength = len;
    }
    uint32_t GetServiceDataLength()
    {
        return ServiceDataLength;
    }
    void SetPadBytes(uint32_t bytesCount)
    {
        PadBytes = bytesCount;
    }

    void SetDescriptor(vector<VodDsmcc_Descriptor> descriptor)
    {
        Desc = descriptor;
    }
    status ParseUserPrivateData(ui8 * data, ui32 length,
                                ui8 ** newData, ui32 *newLength);
    ui8 * PackUserPrivateData(ui8 *pBuffer);
private:
    ui8 SSPProtocolID;
    ui8 SSPVersion;
    ui8 ServiceGateway[SERVICE_GW_SIZE];
    uint32_t ServiceGatewayDataLength;
    ui8 Service[SERVICE_SIZE];
    uint32_t ServiceDataLength;

    ui8 DescCount;
    uint32_t PadBytes;

    vector<VodDsmcc_Descriptor> Desc;
};

//##################################################################################
class VodDsmcc_UserUserData
{
public:
    VodDsmcc_UserUserData(ui8 sSPProtocolID, ui8 sSPVersion, ui8 descCount,
                          vector<VodDsmcc_Descriptor> desc)
    {
        SSPProtocolID = sSPProtocolID;
        SSPVersion = sSPVersion;
        DescCount = descCount;
        Desc = desc;
    }
    VodDsmcc_UserUserData()
    {
        SSPProtocolID = 0;
        SSPVersion = 0;
        DescCount = 0;
    }

    ~VodDsmcc_UserUserData()
    {
    }
    ui8 GetSSPProtocolID()
    {
        return SSPProtocolID;
    }
    ui8 GetSSSPVersion()
    {
        return SSPVersion;
    }
    ui8 GetdescCount()
    {
        return DescCount;
    }
    vector<VodDsmcc_Descriptor> GetDescriptor()
    {
        return Desc;
    }
    void SetSSPProtocolID(ui8 protocolId)
    {
        SSPProtocolID = protocolId;
    }
    void SetSSPVersion(ui8 version)
    {
        SSPVersion = version;
    }
    void SetdescCount(ui8 count)
    {
        DescCount = count;
    }
    void SetDescriptor(vector<VodDsmcc_Descriptor> descriptor)
    {
        Desc = descriptor;
    }
    status ParseUserUserData(ui8 * data, ui32 length,
                             ui8 ** newData, ui32 *newLength);
    ui8 * PackUserUserData(ui8 *pBuffer);

private:
    ui8 SSPProtocolID;
    ui8 SSPVersion;
    ui8 DescCount;
    vector<VodDsmcc_Descriptor> Desc;
};

//##################################################################################
class VodDsmcc_ResourceDescriptor
{
public:
    VodDsmcc_ResourceDescriptor(ui16 resourceRequestId,
                                ui16 resourceDescriptorType, ui16 resourceNum, ui16 associationTag,
                                ui8 resourceFlags, ui8 resourceStatus, ui16 resourceLength,
                                ui16 resourceDataFieldCount, ui8* typeOwnerId, ui8* typeOwnerValue,
                                VodDsmcc_UserDescValueType * descValue)
    {
        ResourceRequestId = resourceRequestId;
        ResourceDescriptorType = resourceDescriptorType;
        ResourceNum = resourceNum;
        AssociationTag = associationTag;
        ResourceFlags = resourceFlags;
        ResourceStatus = resourceStatus;
        ResourceLength = resourceLength;
        ResourceDataFieldCount = resourceDataFieldCount;
        //TypeOwnerId = typeOwnerId;
        if (NULL != typeOwnerId)
            memcpy(TypeOwnerId, typeOwnerId, TYPE_OWNER_SIZE);
        //TypeOwnerValue = typeOwnerValue;
        if (NULL != typeOwnerValue)
            memcpy(TypeOwnerValue, typeOwnerValue, TYPE_OWNER_SIZE);
        DescValue = descValue;
    }

    ~VodDsmcc_ResourceDescriptor()
    {
    }
    VodDsmcc_ResourceDescriptor()
    {
        ResourceRequestId = 0;
        ResourceDescriptorType = 0;
        ResourceNum = 0;
        AssociationTag = 0;
        ResourceFlags = 0;
        ResourceStatus = 0;
        ResourceLength = 0;
        ResourceDataFieldCount = 0;
        memset(TypeOwnerId, 0, TYPE_OWNER_SIZE);
        memset(TypeOwnerValue, 0, TYPE_OWNER_SIZE);
        DescValue = NULL;
    }

    ui16 GetResourceRequestId()
    {
        return ResourceRequestId;
    }
    void SetResourceRequestId(ui16 resourceRequestId)
    {
        ResourceRequestId = resourceRequestId;
    }

    ui16 GetResourceDescriptorType()
    {
        return ResourceDescriptorType;
    }
    void SetResourceDescriptorType(ui16 resourceDescriptorType)
    {
        ResourceDescriptorType = resourceDescriptorType;
    }

    ui16 GetResourceNum()
    {
        return ResourceNum;
    }
    void SetResourceNum(ui16 resourceNum)
    {
        ResourceNum = resourceNum;
    }

    ui16 GetAssociationTag()
    {
        return AssociationTag;
    }
    void SetAssociationTag(ui16 associationTag)
    {
        AssociationTag = associationTag;
    }

    ui8 GetResourceFlags()
    {
        return ResourceFlags;
    }
    void SetResourceFlags(ui8 resourceFlags)
    {
        ResourceFlags = resourceFlags;
    }

    ui8 GetResourceStatus()
    {
        return ResourceStatus;
    }
    void SetResourceStatus(ui8 resourceStatus)
    {
        ResourceStatus = resourceStatus;
    }

    ui16 GetResourceLength()
    {
        return ResourceLength;
    }
    void SetResourceLength(ui16 resourceLength)
    {
        ResourceLength = resourceLength;
    }

    ui16 GetResourceDataFieldCount()
    {
        return ResourceDataFieldCount;
    }
    void SetResourceDataFieldCount(ui16 resourceDataFieldCount)
    {
        ResourceDataFieldCount = resourceDataFieldCount;
    }

    ui8* GetTypeOwnerId()
    {
        return TypeOwnerId;
    }
    void SetTypeOwnerId(ui8* typeOwnerId)
    {
        if (TypeOwnerId && typeOwnerId)
            memcpy(TypeOwnerId, typeOwnerId, TYPE_OWNER_SIZE);
    }

    ui8* GetTypeOwnerValue()
    {
        return TypeOwnerValue;
    }
    void SetTypeOwnerValue(ui8* typeOwnerValue)
    {
        if (TypeOwnerValue && typeOwnerValue)
            memcpy(TypeOwnerValue, typeOwnerValue, TYPE_OWNER_SIZE);
    }

    VodDsmcc_UserDescValueType * GetDescValue()
    {
        return DescValue;
    }
    void SetDescValue(VodDsmcc_UserDescValueType *descValue)
    {
        DescValue = descValue;
    }
    VodDsmcc_UserDescValueType * GetResDescObjectFromType(ui16 resType);
    status ParseResourceDesc(ui8 * data, ui32 length,
                             ui8 ** newData,
                             ui32 *newLength);

private:
    //Hdr
    ui16 ResourceRequestId;
    ui16 ResourceDescriptorType;
    ui16 ResourceNum;
    ui16 AssociationTag;
    ui8 ResourceFlags;
    ui8 ResourceStatus;
    ui16 ResourceLength;
    ui16 ResourceDataFieldCount;
    ui8 TypeOwnerId[TYPE_OWNER_SIZE];
    ui8 TypeOwnerValue[TYPE_OWNER_SIZE];
    //Body
    VodDsmcc_UserDescValueType *DescValue;

};

//##################################################################################
class VodDsmcc_UserData
{
public:
    VodDsmcc_UserData(ui16 uuDataCount, VodDsmcc_UserUserData &uuDataObj,
                      ui16 privateDataCount, VodDsmcc_UserPrivateData &privateDataObj)
    {
        UuDataCount = uuDataCount;
        UuDataObj = uuDataObj;
        PrivateDataCount = privateDataCount;
        PrivateDataObj = privateDataObj;
    }
    VodDsmcc_UserData()
    {
        UuDataCount = 0;
        PrivateDataCount = 0;
    }
    ~VodDsmcc_UserData()
    {
    }

    ui16 GetUuDataCount()
    {
        return UuDataCount;
    }
    void SetUuDataCount(ui16 uuDataCount)
    {
        UuDataCount = uuDataCount;
    }

    VodDsmcc_UserUserData GetUuDataObj()
    {
        return UuDataObj;
    }
    void SetUuDataObj(VodDsmcc_UserUserData uuDataObj)
    {
        UuDataObj = uuDataObj;
    }

    ui16 GetPrivateDataCount()
    {
        return PrivateDataCount;
    }
    void SetPrivateDataCount(ui16 privateDataCount)
    {
        PrivateDataCount = privateDataCount;
    }

    VodDsmcc_UserPrivateData GetPrivateDataObj()
    {
        return PrivateDataObj;
    }
    void SetPrivateDataObj(VodDsmcc_UserPrivateData privateDataObj)
    {
        PrivateDataObj = privateDataObj;
    }

    ui8 * PackUserData(ui8 *pBuffer);
    status ParseUserData(ui8 * data, ui32 length,
                         ui8 ** newData, ui32 *newLength);

private:
    ui16 UuDataCount;
    VodDsmcc_UserUserData UuDataObj;
    ui16 PrivateDataCount;
    VodDsmcc_UserPrivateData PrivateDataObj;
};

//##################################################################################

class VodDsmcc_ClientSessionSetup: public VodDsmcc_Base
{
public:
    status ParseDsmccMessageBody(ui8 * data, ui32 length);
    status PackDsmccMessageBody(ui8 **pOut, ui32 *len);
    /////get and set method for all members
    VodDsmcc_ClientSessionSetup(ui16 msgId, ui32 transId, ui16 msgLen,
                                VodDsmcc_SessionId &sessionId, ui8 *clientId, ui8 *serverId,
                                VodDsmcc_UserData &userData);

    VodDsmcc_ClientSessionSetup()
    {
        Reserved = 0xFFFF;
        memset(ClientId, 0, DSMCC_CLIENTID_LEN);
        memset(ServerId, 0, DSMCC_SERVERID_LEN);
    };
    ~VodDsmcc_ClientSessionSetup();

    VodDsmcc_SessionId GetSessionId()
    {
        return SessionId;
    }
    void SetSessionId(VodDsmcc_SessionId sessionId)
    {
        SessionId = sessionId;
    }

    void GetClientId(CLIENTID clientId)
    {
        if (clientId && ClientId)
            memcpy(clientId, ClientId, DSMCC_CLIENTID_LEN) ;
    }
    void SetClientId(CLIENTID clientId)
    {
        if (clientId && ClientId)
            memcpy(ClientId, clientId, DSMCC_CLIENTID_LEN) ;
    }

    void GetServerId(SERVERID serverId)
    {
        memcpy(serverId, ServerId, DSMCC_SERVERID_LEN) ;
    }
    void SetServerId(SERVERID serverId)
    {
        memcpy(ServerId, serverId, DSMCC_SERVERID_LEN) ;
    }

    VodDsmcc_UserData GetUserData()
    {
        return UserData;
    }
    void SetUserData(VodDsmcc_UserData userData)
    {
        UserData = userData;
    }

private:
    VodDsmcc_SessionId SessionId;
    ui16 Reserved;
    CLIENTID ClientId;
    SERVERID ServerId;
    VodDsmcc_UserData UserData;
};

//##################################################################################
class VodDsmcc_ClientSessionConfirm: public VodDsmcc_Base
{
public:
    status ParseDsmccMessageBody(ui8 * data, ui32 length);
    status PackDsmccMessageBody(ui8 **pOut, ui32 *len);
    /////get snd set method for all members

    VodDsmcc_ClientSessionConfirm()
    {
        Response = 0;
        memset(ServerId, 0, DSMCC_SERVERID_LEN);
        ResourceCount = 0;

        //populate mapping between different response codes and their corresponding strings
        PopulateArrisResponseCodes();
    };
    ~VodDsmcc_ClientSessionConfirm();

    VodDsmcc_SessionId GetSessionId()
    {
        return SessionId;
    }
    void SetSessionId(VodDsmcc_SessionId sessionId)
    {
        SessionId = sessionId;
    }

    ui16 GetResponse()
    {
        return Response;
    }
    void SetResponse(ui16 response)
    {
        Response = response;
    }

    string GetResponseString(ui16 response)
    {
        std::map<int, string>::iterator it;
        std::string error = "Cant find error code in Map with value ";
        std::stringstream ss;

        it = mArrisResponseCodeMap.find(response);

        if (it != mArrisResponseCodeMap.end())
        {
            return it->second;
        }
        else
        {
            ss << error << response;
            return ss.str();
        }
    }

    void GetServerId(SERVERID serverId)
    {
        if (serverId && ServerId)
            memcpy(serverId, ServerId, DSMCC_SERVERID_LEN) ;
    }
    void SetServerId(SERVERID serverId)
    {
        if (ServerId && serverId)
            memcpy(ServerId, serverId, DSMCC_SERVERID_LEN) ;
    }

    ui16 GetResourceCount()
    {
        return ResourceCount;
    }
    void SetResourceCount(ui16 resourceCount)
    {
        ResourceCount = resourceCount;
    }

    vector<VodDsmcc_ResourceDescriptor> GetResources()
    {
        return Resources;
    }
    void SetResources(vector<VodDsmcc_ResourceDescriptor> resources)
    {
        Resources = resources;
    }


    VodDsmcc_UserData GetUserData()
    {
        return UserData;
    }
    void SetUserData(VodDsmcc_UserData userData)
    {
        UserData = userData;
    }

    /**
    * \param
    * \brief This function is simply used to create a mapping between response codes and corresponding strings
    */
    void PopulateArrisResponseCodes();


private:
    VodDsmcc_SessionId SessionId;
    ui16 Response;
    SERVERID ServerId;
    ui16 ResourceCount;
    vector<VodDsmcc_ResourceDescriptor> Resources;
    VodDsmcc_UserData UserData;


    /**
    * this map is used for mapping of error response codes to corresponding strings
    */
    map<int, string> mArrisResponseCodeMap;
};

class VodDsmcc_ClientConnect: public VodDsmcc_Base
{
public:
    status ParseDsmccMessageBody(ui8 * data, ui32 length);
    status PackDsmccMessageBody(ui8 **pOut, ui32 *len);
    /////get snd set method for all members
    VodDsmcc_SessionId GetSessionId()
    {
        return SessionId;
    }
    void SetSessionId(VodDsmcc_SessionId sessionId)
    {
        SessionId = sessionId;
    }

    VodDsmcc_UserData GetUserData()
    {
        return UserData;
    }
    void SetUserData(VodDsmcc_UserData userData)
    {
        UserData = userData;
    }

    VodDsmcc_ClientConnect() {};
    ~VodDsmcc_ClientConnect();
    VodDsmcc_ClientConnect(ui16 msgId, ui32 transId, ui16 msgLen,
                           VodDsmcc_SessionId &sessionId, VodDsmcc_UserData &userData);

private:
    VodDsmcc_SessionId SessionId;
    VodDsmcc_UserData UserData;
};

//##################################################################################
class VodDsmcc_ClientReleaseGeneric: public VodDsmcc_Base
{
public:
    status ParseDsmccMessageBody(ui8 * data, ui32 length);
    status PackDsmccMessageBody(ui8 **pOut, ui32 *len);
    /////get snd set method for all members
    VodDsmcc_SessionId GetSessionId()
    {
        return SessionId;
    }
    void SetSessionId(VodDsmcc_SessionId sessionId)
    {
        SessionId = sessionId;
    }
    ui16 GetReason()
    {
        return Reason;
    }

    string GetReasonString(ui16 reason)
    {
        return mArrisReasonCodeMap.find(reason)->second;
    }

    void SetReason(ui16 reason)
    {
        Reason = reason;
    }

    VodDsmcc_UserData GetUserData()
    {
        return UserData;
    }
    void SetUserData(VodDsmcc_UserData userData)
    {
        UserData = userData;
    }

    /**
    * \param
    * \brief This function is simply used to create a mapping between response codes and corresponding strings
    */
    void PopulateArrisReasonCodes();

    VodDsmcc_ClientReleaseGeneric()
    {
        Reason = 0;
        PopulateArrisReasonCodes();
    };
    ~VodDsmcc_ClientReleaseGeneric();
    VodDsmcc_ClientReleaseGeneric(ui16 msgId, ui32 transId, ui16 msgLen,
                                  VodDsmcc_SessionId &sessionId, ui16 reason, VodDsmcc_UserData &userData);

private:
    VodDsmcc_SessionId SessionId;
    ui16 Reason;
    VodDsmcc_UserData UserData;

    /**
    * this map is used for mapping of error reason codes to corresponding strings
    */
    map<int, string> mArrisReasonCodeMap;
};

//##################################################################################
class VodDsmcc_ClientResetGeneric: public VodDsmcc_Base
{
public:
    status ParseDsmccMessageBody(ui8 * data, ui32 length);
    status PackDsmccMessageBody(ui8 **pOut, ui32 *len);

    void GetClientId(CLIENTID clientId)
    {
        if (clientId && ClientId)
            memcpy(clientId, ClientId, DSMCC_CLIENTID_LEN) ;
    }
    void SetClientId(CLIENTID clientId)
    {
        if (clientId && ClientId)
            memcpy(ClientId, clientId, DSMCC_CLIENTID_LEN) ;
    }
    ui16 GetReason()
    {
        return Reason;
    }
    void SetReason(ui16 reason)
    {
        Reason = reason;
    }

    VodDsmcc_ClientResetGeneric()
    {
        Reason = 0;
    };
    ~VodDsmcc_ClientResetGeneric() {};
    VodDsmcc_ClientResetGeneric(ui16 msgId, ui32 transId, ui16 msgLen,
                                ui8 *clientId, ui16 reason);

private:
    CLIENTID ClientId;
    ui16 Reason;
};

//##################################################################################
class VodDsmcc_ClientSessionInProgress: public VodDsmcc_Base
{

public:
    status ParseDsmccMessageBody(ui8 * data, ui32 length);
    status PackDsmccMessageBody(ui8 **pOut, ui32 *len);
    /////get snd set method for all members
    VodDsmcc_ClientSessionInProgress()
    {
        SessionCount = 0;
    };
    ~VodDsmcc_ClientSessionInProgress() {};
    VodDsmcc_ClientSessionInProgress(ui16 msgId, ui32 transId, ui16 msgLen,
                                     ui16 sessCount, vector <VodDsmcc_SessionId> sessionIdList);

private:
    ui16 SessionCount;
    vector <VodDsmcc_SessionId> SessionIdList;
};

//##################################################################################
class VodDsmcc_ClientStatusRequest: public VodDsmcc_Base
{

public:
    status ParseDsmccMessageBody(ui8 * data, ui32 length);
    status PackDsmccMessageBody(ui8 **pOut, ui32 *len);
    /////get snd set method for all members
    ui16 GetReason()
    {
        return Reason;
    }
    void SetReason(ui16 reason)
    {
        Reason = reason;
    }
    void GetClientId(CLIENTID clientId)
    {
        if (clientId && ClientId)
            memcpy(clientId, ClientId, DSMCC_CLIENTID_LEN) ;
    }
    void SetClientId(CLIENTID clientId)
    {
        if (clientId && ClientId)
            memcpy(ClientId, clientId, DSMCC_CLIENTID_LEN) ;
    }
    ui16 GetStatusType()
    {
        return StatusType;
    }
    void SetStatusType(ui16 statusType)
    {
        StatusType = statusType;
    }
    ui16 GetStatusCount()
    {
        return StatusCount;
    }
    void SetStatusCount(ui16 statusCount)
    {
        StatusCount = statusCount;
    }
    ui8* GetStatusByte()
    {
        return StatusByte;
    }
    void SetStatusByte(ui8* statusByte)
    {
        StatusByte = new ui8[StatusCount];
        if (StatusByte && statusByte)
        {
            memcpy(StatusByte, statusByte, StatusCount);
        }
    }

    VodDsmcc_ClientStatusRequest()
    {
        Reason = 0;
        memset(ClientId, 0, DSMCC_CLIENTID_LEN);
        StatusType = 0;
        StatusCount = 0;
        StatusByte = NULL;
    };
    ~VodDsmcc_ClientStatusRequest();
    VodDsmcc_ClientStatusRequest(ui16 msgId, ui32 transId, ui16 msgLen,
                                 ui16 reason, ui8 *clientId, ui16 statusType, ui16 statusCount,
                                 ui8* statusByte);

private:
    ui16 Reason;
    CLIENTID ClientId;
    ui16 StatusType;
    ui16 StatusCount;
    ui8* StatusByte;
};

//##################################################################################
class VodDsmcc_ClientStatusConfirm: public VodDsmcc_Base
{

public:
    status ParseDsmccMessageBody(ui8 * data, ui32 length);
    status PackDsmccMessageBody(ui8 **pOut, ui32 *len);
    /////get snd set method for all members
    ui16 GetReason()
    {
        return Reason;
    }
    void SetReason(ui16 reason)
    {
        Reason = reason;
    }
    ui16 GetStatusType()
    {
        return StatusType;
    }
    void SetStatusType(ui16 statusType)
    {
        StatusType = statusType;
    }
    ui16 GetStatusCount()
    {
        return StatusCount;
    }
    void SetStatusCount(ui16 statusCount)
    {
        StatusCount = statusCount;
    }
    ui8* GetStatusByte()
    {
        return StatusByte;
    }
    void SetStatusByte(ui8* statusByte)
    {
        StatusByte = new ui8[StatusCount];
        if (StatusByte && statusByte)
        {
            memcpy(StatusByte, statusByte, StatusCount);
        }
    }
    VodDsmcc_ClientStatusConfirm()
    {
        Reason = 0;
        StatusType = 0;
        StatusCount = 0;
        StatusByte = NULL;
    };
    ~VodDsmcc_ClientStatusConfirm();

private:
    ui16 Reason;
    ui16 StatusType;
    ui16 StatusCount;
    ui8* StatusByte;
};
//##################################################################################
#endif

