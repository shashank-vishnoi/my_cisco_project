/** @file dsmccProtocol.cpp
 *
 * @brief dsmcc protocol source file.
 *
 * @author Amit Patel
 *
 * @version 1.0
 *
 * @date 12.27.2010
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */
#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include  <sys/types.h>
#include <netinet/in.h>
#include "dlog.h"
#include "dsmccProtocol.h"

#define MSGBODY_BUFFER_LEN  300
//###################################################################################

#define Close(x) do{dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_DEBUG,"%s:%s:%d ZZClose=%d",__FILE__,__FUNCTION__,__LINE__, x); close(x); (x)=-1;}while(0)


ui8* DsmccUtils::GenerateSessionId(ui8 *stbMac, ui32 transId)
{
    ui8 *pBuf = NULL;
    pBuf = new ui8[DSMCC_SESSIONID_LEN];
    if ((NULL != pBuf) && (NULL != stbMac))
    {
        Utils::InsertBuffer(pBuf, stbMac, MAC_ID_SIZE);
        Utils::Put4Byte(pBuf + MAC_ID_SIZE, transId);
    }
    return pBuf;
}
ui8* DsmccUtils::GenerateClientId(ui8 *stbMac)
{
    ui8 *pBuf = NULL;
    pBuf = new ui8[DSMCC_CLIENTID_LEN];
    if ((NULL != pBuf) && (NULL != stbMac))
    {
        memset(pBuf, 0, DSMCC_CLIENTID_LEN);
        Utils::Put1Byte(pBuf, 0x2D);
        Utils::InsertBuffer(pBuf + 13, stbMac, MAC_ID_SIZE);
    }
    return pBuf;
}
ui8* DsmccUtils::GenerateServerId(ui32 serverIPAdd)
{
    ui8 *pBuf = NULL;
    pBuf = new ui8[DSMCC_SERVERID_LEN];
    if (NULL != pBuf)
    {
        memset(pBuf, 0, DSMCC_SERVERID_LEN);
        Utils::Put1Byte(pBuf, 0x2D);
        Utils::Put4Byte(pBuf + 9, serverIPAdd);
    }
    return pBuf;
}

//###################################################################################

VodDsmcc_Base::VodDsmcc_Base()
{
    ProtocolDiscriminator = 0x11;
    DsmccType = 0x02; //for session message;
    MessageId = 0;
    TransactionId = 0;
    Reserved = 0xff;
    AdaptationLength = 0;
    MessageLength = 0;
    memset(AdaptationDataByte, 0x00, 50);
}

VodDsmcc_Base::~VodDsmcc_Base()
{

}

VodDsmcc_Base::VodDsmcc_Base(ui16 msgId, ui32 transId, ui16 msgLen)
{
    ProtocolDiscriminator = 0x11;
    DsmccType = 0x02; //for session message;
    MessageId = msgId;
    Reserved = 0xff;
    TransactionId = transId;
    AdaptationLength = 0;
    MessageLength = msgLen;
    memset(AdaptationDataByte, 0x00, 50);
}

bool VodDsmcc_Base::SendMessage(i32 socket, string ip, ui16 port, ui8 *data,
                                ui32 length)
{
    ui32 count;
    bool status = E_FALSE;

    dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "%s:Dump of Message data", __FUNCTION__);
    for (ui32 i = 0; i < length; i++)
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "%2x", data[i]);

    if (socket <= 0)
    {
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "Invalid socket\n");
    }
    else
    {
        struct sockaddr_in addr;
        //struct sockaddr_in from;
        //int size = 0;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET; // internal protocol
        addr.sin_port = htons(port); // TBI, use provided port
        // use my IP address
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "VodDsmcc_Base::GetSocket() IPAddr: %s:", ip.c_str());
        if (inet_aton(ip.c_str(), &addr.sin_addr) == 0)
        {
            dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
                 "VodDsmcc_Base::GetSocket() inet_aton() failed  \n");
            return status;
        }

        //dlog(DL_MSP_ONDEMAND,DLOGL_DATA_PROCESSING, "Want to send datalen:%d data:%s\n", length ,data );
        //count = sendto(socket, data, length, 0, (struct sockaddr*)&addr, sizeof(addr));
        count = sendto(socket, data, length, 0, (struct sockaddr*) &addr,
                       sizeof(addr));
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "Sent data length: %d ", count);
        if (count != length)
        {
            dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "Socket send error:\n");
        }
        else
        {
            status = E_TRUE;
        }
    }
    return status;
}

bool VodDsmcc_Base::ReadMessageFromSocket(i32 socket, ui8 **pOut, ui32 *lenOut)
{
    //ui8 dsmccHeader[DSMCC_MSG_HEADER_SIZE];
    i32 got = 0;
    struct sockaddr_in from;
    int size = 0;
    bool status = false;

    unsigned char *buffer = new ui8[700];

    got = recvfrom(socket, buffer, 700, 0, (struct sockaddr*) &from, (socklen_t *) &size);

    if (got > 0)
    {
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "got:%d", got);
        for (int i = 0; i < got; i++)
            dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "%2x", buffer[i]);
        // assign data and length to output buffer
        *pOut = buffer;
        *lenOut = got;
        status = true;
    }
    else
    {
        delete [] buffer;
        buffer = NULL;
        *pOut = NULL;
        *lenOut = 0;
    }
    return status;
}

VodDsmcc_Base* VodDsmcc_Base::GetMessageTypeObject(ui8 * data, ui32 length)
{
    ui8 sByte;
    ui16 dByte;
    ui8 *start;

    VodDsmcc_Base* dsmccBase = NULL;

    if (length < DSMCC_MSG_HEADER_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain DSMCC header!\n");
    }
    else
    {
        // remember the start of the message
        start = data;

        // protocol discriminator type
        start = Utils::Get1Byte(start, &sByte);
        // dsm-cc type
        start = Utils::Get1Byte(start, &sByte);
        // message ID
        Utils::Get2Byte(start, &dByte);

        dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS, " Message ID = 0x%04X !\n", dByte);
        switch (dByte)
        {
        case dsmcc_ClientSessionSetUpRequest:
            dsmccBase = new VodDsmcc_ClientSessionSetup();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
                 "Received dsmcc_ClientSessionSetUpRequest message");
            break;
        case dsmcc_ClientSessionSetUpConfirm:
            dsmccBase = new VodDsmcc_ClientSessionConfirm();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
                 "Received dsmcc_ClientSessionSetUpConfirm message");
            break;
        case dsmcc_ClientConnectRequest:
            dsmccBase = new VodDsmcc_ClientConnect();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
                 "Received dsmcc_ClientConnectRequest message");
            break;
        case dsmcc_ClientReleaseRequest:
            dsmccBase = new VodDsmcc_ClientReleaseGeneric();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
                 "Received dsmcc_ClientReleaseRequest message");
            break;
        case dsmcc_ClientReleaseConfirm:
            dsmccBase = new VodDsmcc_ClientReleaseGeneric();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
                 "Received dsmcc_ClientReleaseConfirm message");
            break;
        case dsmcc_ClientReleaseIndication:
            dsmccBase = new VodDsmcc_ClientReleaseGeneric();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
                 "Received dsmcc_ClientReleaseIndication message");
            break;
        case dsmcc_ClientReleaseResponse:
            dsmccBase = new VodDsmcc_ClientReleaseGeneric();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
                 "Received dsmcc_ClientReleaseResponse message");
            break;
        case dsmcc_ClientResetRequest:
            dsmccBase = new VodDsmcc_ClientResetGeneric();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
                 "Received dsmcc_ClientResetRequest message");
            break;
        case dsmcc_ClientResetIndication:
            dsmccBase = new VodDsmcc_ClientResetGeneric();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
                 "Received dsmcc_ClientResetIndication message");
            break;
        case dsmcc_ClientResetResponse:
            dsmccBase = new VodDsmcc_ClientResetGeneric();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
                 "Received dsmcc_ClientResetResponse message");
            break;
        case dsmcc_ClientStatusRequest:
            dsmccBase = new VodDsmcc_ClientStatusRequest();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
                 "Received dsmcc_ClientStatusRequest message");
            break;
        case dsmcc_ClientStatusConfirm:
            dsmccBase = new VodDsmcc_ClientStatusConfirm();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
                 "Received dsmcc_ClientStatusConfirm message");
            break;
        case dsmcc_ClientSessionInProgressRequest:
            dsmccBase = new VodDsmcc_ClientSessionInProgress();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
                 "Received dsmcc_ClientSessionInProgressRequest message");
            break;
        default:
            dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "Unknown message type");
            break;
        }//end switch
    }//end else

    return dsmccBase;
}

status VodDsmcc_Base::ParseDsmccMessageHdr(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < DSMCC_MSG_HEADER_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain DSMCC header!\n");
    }
    else
    {
        // remember the start of the message
        start = data;

        // protocol discriminator type
        data = Utils::Get1Byte(data, &ProtocolDiscriminator);

        // dsm-cc type
        data = Utils::Get1Byte(data, &DsmccType);

        // message ID
        data = Utils::Get2Byte(data, &MessageId);

        // transaction ID
        data = Utils::Get4Byte(data, &TransactionId);

        // reserved
        data = Utils::Get1Byte(data, &Reserved);

        // adaptation header length
        data = Utils::Get1Byte(data, &AdaptationLength);

        // message length
        data = Utils::Get2Byte(data, &MessageLength);

        dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS, "Message ID \n");

        if ((i32) length != MessageLength + DSMCC_MSG_HEADER_SIZE)
        {
            // message to small!     it is bad
            dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
                 " Bad DSMCC Message length!  Got %d wanted %d\n",
                 MessageLength, length - DSMCC_MSG_HEADER_SIZE);
        }
        else
        {
            // if no adaptation header then use these values

            *newData = data;
            *newLength = length - (data - start);
            result = E_TRUE;
        }

    }//end else
    return result;
}

status VodDsmcc_Base::PackDsmccMessageHdr(ui8 * msgData, ui32 msgDataLength,
        ui8 **pOut, ui32 * outLength)
{

    //fetch all the value here and pass it to below function to pack it.
    status result = CreateDsmccMessageHeader(msgData, msgDataLength, NULL, 0,
                    pOut, outLength);
    return result;
}

status VodDsmcc_Base::CreateDsmccMessageHeader(ui8 * msgData,
        ui32 msgDataLength, ui8 * adaptHeader, ui32 adaptHeaderLength,
        ui8 ** dsmccMsg, ui32 * dsmccMsgLength)
{
    status result = E_FALSE;
    ui32 totalSize = DSMCC_MSG_HEADER_SIZE + adaptHeaderLength + msgDataLength;
    ui8 * buffer;

    buffer = new ui8[totalSize];

    if (!buffer)
    {
        // ooop no memory
        *dsmccMsg = NULL;
        *dsmccMsgLength = 0;
    }
    else
    {
        // we have enough memory to piece together the final dsmcc message
        ui8 * work = buffer;

        // first, we add the standard message header fields

        // Protocol Discriminator Type
        work = Utils::Put1Byte(work, ProtocolDiscriminator);

        // DSM-CC Type
        work = Utils::Put1Byte(work, DsmccType);

        // Message ID
        work = Utils::Put2Byte(work, MessageId);

        // Transaction ID
        work = Utils::Put4Byte(work, TransactionId);

        // Reserved
        work = Utils::Put1Byte(work, 0xff);

        // Adaptation length
        work = Utils::Put1Byte(work, AdaptationLength);

        // Message length (length of all data that follows this field)
        work = Utils::Put2Byte(work, (adaptHeaderLength + msgDataLength));

        // adaptation header
        if (adaptHeader)
            work = Utils::InsertBuffer(work, adaptHeader, adaptHeaderLength);

        // now the message
        if (msgData)
            work = Utils::InsertBuffer(work, msgData, msgDataLength);

        if ((ui32)(work - buffer) != totalSize)
        {
            dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
                 "Size problem creating message header!\n");
        }
        else
        {
            // things look good
            *dsmccMsg = buffer;
            *dsmccMsgLength = totalSize;
            result = E_TRUE;
        }

    }

    return result;
}

status VodDsmcc_Base::ParseDsmccMessageBody(ui8 * data, ui32 length)
{
    status result = E_FALSE;
    data = data;
    length = length;
    return result;
}

status VodDsmcc_Base::PackDsmccMessageBody(ui8 **pOut, ui32 *len)
{
    status result = E_FALSE;
    pOut = pOut;
    len = len;
    return result;
}

status VodDsmcc_Base::CloseSocket(i32 fd)
{
    if (fd != -1)
    {
        Close(fd);
    }
    return E_TRUE;
}

ui32 VodDsmcc_Base::getTransId()
{
    srand(time(0));
    static ui32 transid = rand() % 100 + 1;
    return ++transid;
}

i32 VodDsmcc_Base::GetSocket(ui16 port)
{
    i32 sockFD = -1;

    //assuming recv and send port number the same, for testing.
    sockFD = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_DEBUG, "%s:%s:%d ZZOpen=%d", __FILE__, __FUNCTION__, __LINE__, sockFD);

    if (sockFD != -1)
    {
        int ret, one = 1;
        setsockopt(sockFD, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof(one));
        // now configure the socket
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(struct sockaddr_in));
        addr.sin_family = AF_INET;                      // internal protocol
        addr.sin_port = htons(port);                    // TBI, use provided port
        addr.sin_addr.s_addr = INADDR_ANY;
        // use my IP address
        // and bind!
        ret = bind(sockFD, (struct sockaddr*)&addr, sizeof(addr));
        if (ret >= 0)
        {
            dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS, "VodDsmcc_Base::GetSocket() Successfully created and bound socket:\n");
        }
        else
        {
            // Bind error
            dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "VodDsmcc_Base::GetSocket() Couldn't bind socket retCode:%d \n", ret);
            // Close and invalidate socket
            Close(sockFD);
        }
    }
    else
    {
        // Failure in creating and binding a socket
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             "VodDsmcc_Base::GetSocket() Couldn't create socket:\n");
    }
    return sockFD;
}

//##############################################################################


// VodDsmcc_UserDescValueType::VodDsmcc_UserDescValueType()
ui8 * VodDsmcc_UserDescValueType::PackUserDescValueType(ui8 *pBuffer)
{
    return pBuffer;
}

status VodDsmcc_UserDescValueType::ParseUserDescValueType(ui8 * data,
        ui32 length, ui8 ** newData, ui32 *newLength)
{
    data = data;
    length = length;
    newData = newData;
    newLength = newLength;

    return E_TRUE;
}

//##############################################################################

//   get set and const in .h file class VodDsmcc_AssetId
ui8 * VodDsmcc_AssetId::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::InsertBuffer(pBuffer, AssetId, ASSET_ID_SIZE);
    return pBuffer;
}

status VodDsmcc_AssetId::ParseUserDescValueType(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < ASSET_ID_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // assetId
        data = Utils::GetBuffer(data, AssetId, ASSET_ID_SIZE);

        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;
    }//end else
    return result;
}

//##############################################################################

//   get set and const in .h file  class VodDsmcc_NodeGroupId
ui8 * VodDsmcc_NodeGroupId::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::InsertBuffer(pBuffer, NodeGroupId, NODE_GROUP_SIZE);
    return pBuffer;
}

status VodDsmcc_NodeGroupId::ParseUserDescValueType(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < NODE_GROUP_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // NodeGroupId
        data = Utils::GetBuffer(data, NodeGroupId, NODE_GROUP_SIZE);
        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;

    }//end else
    return result;
}

//##############################################################################

//   get set and const in .h file  class VodDsmcc_StreamHandle
ui8 * VodDsmcc_StreamId::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put4Byte(pBuffer, StreamHandle);
    return pBuffer;
}

status VodDsmcc_StreamId::ParseUserDescValueType(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < SIZE_FOUR_BYTE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // StreamHandle
        data = Utils::Get4Byte(data, &StreamHandle);
        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;

    }//end else
    return result;
}


//##############################################################################

//   get set and const in .h file  class VodDsmcc_StreamHandle
ui8 * VodDsmcc_KeepAlive::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put4Byte(pBuffer, KeepAliveTime);
    return pBuffer;
}

status VodDsmcc_KeepAlive::ParseUserDescValueType(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < SIZE_FOUR_BYTE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // StreamHandle
        data = Utils::Get4Byte(data, &KeepAliveTime);
        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;

    }//end else
    return result;
}


//##############################################################################

//   get set and const in .h file  class VodDsmcc_StreamHandle
ui8 * VodDsmcc_SEAGeneric::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::InsertBuffer(pBuffer, InfoData, InfoLen);
    return pBuffer;
}

status VodDsmcc_SEAGeneric::ParseUserDescValueType(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < InfoLen)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // StreamHandle
        data = Utils::GetBuffer(data, InfoData, InfoLen);
        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;

    }//end else*/
    return result;
}
//##############################################################################

VodDsmcc_SeaReqData::~VodDsmcc_SeaReqData()
{
    vector<VodDsmcc_Descriptor>::const_iterator itr;
    for (itr = Desc.begin(); itr != Desc.end(); itr++)
    {
        VodDsmcc_Descriptor desc = *itr;
        VodDsmcc_UserDescValueType * descValue = desc.GetValue();
        if (descValue)
        {
            delete descValue;
            descValue = NULL;
        }
    }
}

//   get set and const in .h file  class VodDsmcc_SeaRegData
ui8 * VodDsmcc_SeaReqData::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put1Byte(pBuffer, SSPProtocolID);
    pBuffer = Utils::Put1Byte(pBuffer, SSPVersion);
    pBuffer = Utils::Put1Byte(pBuffer, DescCount);
    //pBuffer = Desc.PackDescriptor(pBuffer);
    vector<VodDsmcc_Descriptor>::const_iterator itrDesc;
    for (itrDesc = Desc.begin(); itrDesc != Desc.end(); itrDesc++)
    {
        VodDsmcc_Descriptor desc = *itrDesc;
        pBuffer = desc.PackDescriptor(pBuffer);
    }
    return pBuffer;
}

status VodDsmcc_SeaReqData::ParseUserDescValueType(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < REQ_DESC_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             "Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // SSPProtocolID
        data = Utils::Get1Byte(data, &SSPProtocolID);
        // SSPVersion
        data = Utils::Get1Byte(data, &SSPVersion);
        // DescCount
        data = Utils::Get1Byte(data, &DescCount);

        *newData = data;
        *newLength = length - (data - start);
        //Amit update parse function
        for (i8 i = 0; i < DescCount; i++)
        {
            VodDsmcc_Descriptor desc;
            desc.ParseDescriptor(VENDREQ, *newData, *newLength, newData,
                                 newLength);
            Desc.push_back(desc);
        }

        result = E_TRUE;

    }//end else
    return result;
}

//##############################################################################

ui8 * VodDsmcc_ArrisAppReqData::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::InsertBuffer(pBuffer, InfoData, InfoLen);
    return pBuffer;
}

status VodDsmcc_ArrisAppReqData::ParseUserDescValueType(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < InfoLen)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // StreamHandle
        data = Utils::GetBuffer(data, InfoData, InfoLen);
        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;

    }//end else*/
    return result;
}


//##############################################################################
//   get set and const in .h file  class VodDsmcc_AppReqData
VodDsmcc_AppReqData::~VodDsmcc_AppReqData()
{
    vector<VodDsmcc_Descriptor>::const_iterator itr;
    for (itr = Desc.begin(); itr != Desc.end(); itr++)
    {
        VodDsmcc_Descriptor desc = *itr;
        VodDsmcc_UserDescValueType * descValue = desc.GetValue();
        if (descValue)
        {
            delete descValue;
            descValue = NULL;
        }
    }
}

ui8 * VodDsmcc_AppReqData::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put1Byte(pBuffer, SSPProtocolID);
    pBuffer = Utils::Put1Byte(pBuffer, SSPVersion);
    pBuffer = Utils::Put1Byte(pBuffer, DescCount);
    //pBuffer = Desc.PackDescriptor(pBuffer);
    vector<VodDsmcc_Descriptor>::const_iterator itrDesc;
    for (itrDesc = Desc.begin(); itrDesc != Desc.end(); itrDesc++)
    {
        VodDsmcc_Descriptor desc = *itrDesc;
        pBuffer = desc.PackDescriptor(pBuffer);
    }
    return pBuffer;
}

status VodDsmcc_AppReqData::ParseUserDescValueType(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < REQ_DESC_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // SSPProtocolID
        data = Utils::Get1Byte(data, &SSPProtocolID);
        // SSPVersion
        data = Utils::Get1Byte(data, &SSPVersion);
        // DescCount
        data = Utils::Get1Byte(data, &DescCount);

        *newData = data;
        *newLength = length - (data - start);

        for (i8 i = 0; i < DescCount; i++)
        {
            VodDsmcc_Descriptor desc;
            desc.ParseDescriptor(APPREQ, *newData, *newLength,  newData, newLength);
            Desc.push_back(desc);
        }
        result = E_TRUE;

    }//end else
    return result;
}

//##############################################################################
//   get set and const in .h file  class VodDsmcc_AppResData
VodDsmcc_AppResData::~VodDsmcc_AppResData()
{
    vector<VodDsmcc_Descriptor>::const_iterator itr;
    for (itr = mDesc.begin(); itr != mDesc.end(); itr++)
    {
        VodDsmcc_Descriptor desc = *itr;
        VodDsmcc_UserDescValueType * descValue = desc.GetValue();
        if (descValue)
        {
            delete descValue;
            descValue = NULL;
        }
    }
}

ui8 * VodDsmcc_AppResData::PackUserDescValueType(ui8 *pBuffer)
{
    if (pBuffer != NULL)
    {
        pBuffer = Utils::Put1Byte(pBuffer, mSSPProtocolID);
        pBuffer = Utils::Put1Byte(pBuffer, mSSPVersion);
        pBuffer = Utils::Put1Byte(pBuffer, mDescCount);
        vector<VodDsmcc_Descriptor>::const_iterator itrDesc;
        for (itrDesc = mDesc.begin(); itrDesc != mDesc.end(); itrDesc++)
        {
            VodDsmcc_Descriptor desc = *itrDesc;
            pBuffer = desc.PackDescriptor(pBuffer);
        }
    }
    else
    {
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "%s:%d NULL pBuffer passed !\n", __FUNCTION__, __LINE__);
    }
    return pBuffer;
}

status VodDsmcc_AppResData::ParseUserDescValueType(ui8 * pData, ui32 length,
        ui8 ** ppNewData, ui32 *pNewLength)
{
    ui8 *pStart = NULL;
    status result = E_FALSE;
    if ((length < REQ_DESC_SIZE) || (pData == NULL) || (ppNewData == NULL))
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Invalid input arguments length:%d pData:%p ppNewData:%p !\n", length, pData, ppNewData);
    }
    else
    {
        // remember the start of the message
        pStart = pData;
        // SSPProtocolID
        pData = Utils::Get1Byte(pData, &mSSPProtocolID);
        // SSPVersion
        pData = Utils::Get1Byte(pData, &mSSPVersion);
        // DescCount
        pData = Utils::Get1Byte(pData, &mDescCount);

        *ppNewData = pData;
        *pNewLength = length - (pData - pStart);

        for (i8 i = 0; i < mDescCount; i++)
        {
            VodDsmcc_Descriptor desc;
            desc.ParseDescriptor(APPREQ, *ppNewData, *pNewLength,  ppNewData, pNewLength);
            mDesc.push_back(desc);
        }
        result = E_TRUE;

    }//end else
    return result;
}

//##############################################################################
//   get set and const in .h file  class VodDsmcc_DSTStreamData
ui8 * VodDsmcc_DSTStreamData::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put2Byte(pBuffer, DownStreamBWType);
    pBuffer = Utils::Put4Byte(pBuffer, DownStreamBWValue);
    pBuffer = Utils::Put2Byte(pBuffer, DownstreamTransportIdType);
    pBuffer = Utils::Put4Byte(pBuffer, DownstreamTransportIdValue);
    return pBuffer;
}

status VodDsmcc_DSTStreamData::ParseUserDescValueType(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < DSTSSTREAM_DESC_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // DownStreamBWType
        data = Utils::Get2Byte(data, &DownStreamBWType);
        // DownStreamBWValue
        data = Utils::Get4Byte(data, &DownStreamBWValue);
        // DownstreamTransportIdType
        data = Utils::Get2Byte(data, &DownstreamTransportIdType);
        // DownstreamTransportIdValue
        data = Utils::Get4Byte(data, &DownstreamTransportIdValue);

        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;
    }//end else
    return result;
}

//##############################################################################
//   get set and const in .h file  class VodDsmcc_MPEGProgResData
ui8 * VodDsmcc_MPEGProgResData::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put2Byte(pBuffer, MPEGProgramNumberType);
    pBuffer = Utils::Put2Byte(pBuffer, MPEGProgramNumberValue);
    pBuffer = Utils::Put2Byte(pBuffer, PMTPIDType);
    pBuffer = Utils::Put2Byte(pBuffer, PMTPIDValue);
    pBuffer = Utils::Put2Byte(pBuffer, CAPIDValue);
    pBuffer = Utils::Put2Byte(pBuffer, ElementaryStreamCountValue);
    pBuffer = Utils::Put2Byte(pBuffer, MPEGPCRType);
    pBuffer = Utils::Put2Byte(pBuffer, MPEGPCRValue);
    return pBuffer;

}

status VodDsmcc_MPEGProgResData::ParseUserDescValueType(ui8 * data,
        ui32 length, ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < MPEG_DESC_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // MPEGProgramNumberType
        data = Utils::Get2Byte(data, &MPEGProgramNumberType);
        // MPEGProgramNumberValue
        data = Utils::Get2Byte(data, &MPEGProgramNumberValue);
        // PMTPIDType
        data = Utils::Get2Byte(data, &PMTPIDType);
        // PMTPIDValue
        data = Utils::Get2Byte(data, &PMTPIDValue);
        // CAPIDValue
        data = Utils::Get2Byte(data, &CAPIDValue);
        // ElementaryStreamCountValue
        data = Utils::Get2Byte(data, &ElementaryStreamCountValue);
        // MPEGPCRType
        data = Utils::Get2Byte(data, &MPEGPCRType);
        // MPEGPCRValue
        data = Utils::Get2Byte(data, &MPEGPCRValue);

        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;

    }//end else
    return result;
}

//##############################################################################
//   get set and const in .h file  class VodDsmcc_PhysicalChannelResData
ui8 * VodDsmcc_PhysicalChannelResData::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put2Byte(pBuffer, ChannelIdType);
    pBuffer = Utils::Put4Byte(pBuffer, ChannelIdValue);
    pBuffer = Utils::Put2Byte(pBuffer, Direction);
    return pBuffer;
}

status VodDsmcc_PhysicalChannelResData::ParseUserDescValueType(ui8 * data,
        ui32 length, ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < PHY_CHANNEL_DESC_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // ChannelIdType
        data = Utils::Get2Byte(data, &ChannelIdType);
        // ChannelIdValue
        data = Utils::Get4Byte(data, &ChannelIdValue);
        // Direction
        data = Utils::Get2Byte(data, &Direction);

        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;
    }//end else
    return result;
}

//##############################################################################
//   get set and const in .h file  class VodDsmcc_ATSCModulationData
ui8 * VodDsmcc_ATSCModulationData::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put1Byte(pBuffer, TransmissionSystem);
    pBuffer = Utils::Put1Byte(pBuffer, InnerCodingMode);
    pBuffer = Utils::Put1Byte(pBuffer, SplitBitStreamMode);
    pBuffer = Utils::Put1Byte(pBuffer, ModulationFormat);
    pBuffer = Utils::Put4Byte(pBuffer, SymbolRate);
    pBuffer = Utils::Put1Byte(pBuffer, Reserved);
    pBuffer = Utils::Put1Byte(pBuffer, InterleaveDepth);
    pBuffer = Utils::Put1Byte(pBuffer, ModulationMode);
    pBuffer = Utils::Put1Byte(pBuffer, ForwardErrorCorrection);
    return pBuffer;
}
status VodDsmcc_ATSCModulationData::ParseUserDescValueType(ui8 * data,
        ui32 length, ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < ATSCMODULATION_DESC_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // TransmissionSyatem
        data = Utils::Get1Byte(data, &TransmissionSystem);
        // InnerCodingMode
        data = Utils::Get1Byte(data, &InnerCodingMode);
        // SplitBitStreamMode
        data = Utils::Get1Byte(data, &SplitBitStreamMode);
        // ModulationFormat
        data = Utils::Get1Byte(data, &ModulationFormat);
        // SymbolRate
        data = Utils::Get4Byte(data, &SymbolRate);
        // Reserved
        data = Utils::Get1Byte(data, &Reserved);
        // InterleaveDepth
        data = Utils::Get1Byte(data, &InterleaveDepth);
        // ModulationMode
        data = Utils::Get1Byte(data, &ModulationMode);
        // ForwardErrorCorrection
        data = Utils::Get1Byte(data, &ForwardErrorCorrection);

        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;
    }//end else
    return result;
}

//###############################################################################
ui8 * VodDsmcc_FunctionDesc::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put1Byte(pBuffer, Function);
    return pBuffer;
}

status VodDsmcc_FunctionDesc::ParseUserDescValueType(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < FUNCTION_DESC_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        data = Utils::Get1Byte(data, &Function);
        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;
    }//end else
    return result;
}

//###############################################################################
ui8 * VodDsmcc_SubFunctionDesc::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put1Byte(pBuffer, SubFunction);
    return pBuffer;
}

status VodDsmcc_SubFunctionDesc::ParseUserDescValueType(ui8 * data,
        ui32 length, ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < SUBFUNCTION_DESC_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        data = Utils::Get1Byte(data, &SubFunction);
        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;
    }//end else
    return result;
}

//###############################################################################
ui8 * VodDsmcc_BillingIdDesc::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put4Byte(pBuffer, BillingId);
    return pBuffer;
}

status VodDsmcc_BillingIdDesc::ParseUserDescValueType(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < SIZE_FOUR_BYTE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             " Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        data = Utils::Get4Byte(data, &BillingId);
        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;
    }//end else
    return result;
}

//###############################################################################
ui8 * VodDsmcc_PurchaseTimeDesc::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put4Byte(pBuffer, PurchaseTime);
    return pBuffer;
}

status VodDsmcc_PurchaseTimeDesc::ParseUserDescValueType(ui8 * data,
        ui32 length, ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < SIZE_FOUR_BYTE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             ": Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        data = Utils::Get4Byte(data, &PurchaseTime);
        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;
    }//end else
    return result;
}

//###############################################################################
ui8 * VodDsmcc_TimeRemainingDesc::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put4Byte(pBuffer, TimeRemaining);
    return pBuffer;
}

status VodDsmcc_TimeRemainingDesc::ParseUserDescValueType(ui8 * data,
        ui32 length, ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < SIZE_FOUR_BYTE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             ": Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        data = Utils::Get4Byte(data, &TimeRemaining);

        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;
    }//end else
    return result;
}

//###############################################################################
ui8 * VodDsmcc_IPType::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put2Byte(pBuffer, IPPortNumber);
    pBuffer = Utils::Put4Byte(pBuffer, IPAddress);
    return pBuffer;
}

status VodDsmcc_IPType::ParseUserDescValueType(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < IP_PORT_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             ": Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        data = Utils::Get2Byte(data, &IPPortNumber);
        data = Utils::Get4Byte(data, &IPAddress);

        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;
    }//end else
    return result;
}

//###############################################################################
ui8 * VodDsmcc_ClientCA::PackUserDescValueType(ui8 *pBuffer)
{
    pBuffer = Utils::Put4Byte(pBuffer, CaSystemId);
    pBuffer = Utils::Put2Byte(pBuffer, CaInfoLength);
    pBuffer = Utils::InsertBuffer(pBuffer, CaInfoByte, CaInfoLength);
    return pBuffer;
}

status VodDsmcc_ClientCA::ParseUserDescValueType(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < CA_DESC_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             ": Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        data = Utils::Get4Byte(data, &CaSystemId);
        data = Utils::Get2Byte(data, &CaInfoLength);
        data = Utils::GetBuffer(data, CaInfoByte, CaInfoLength);

        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;
    }//end else
    return result;
}

//##############################################################################

VodDsmcc_UserDescValueType * VodDsmcc_Descriptor::getDescriptorObjectFromType(
    Desc_Category category, ui8 type)
{
    VodDsmcc_UserDescValueType * descType = NULL;
    if (category == GENERIC)
    {
        switch (type)
        {
        case GENERIC_ASSETID:
            descType = new VodDsmcc_AssetId();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object of VodDsmcc_AssetId");
            break;
        case GENERIC_NODEGROUPID:
            descType = new VodDsmcc_NodeGroupId();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object of VodDsmcc_NodeGroupId");
            break;
        case GENERIC_IPDESC:
            descType = new VodDsmcc_IPType();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object of VodDsmcc_IPType");
            break;
        case GENERIC_STREAMHANDLE:
            descType = new VodDsmcc_StreamId();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object of VodDsmcc_StreamHandle");
            break;
        case GENERIC_APPREQ:
            descType = new VodDsmcc_AppReqData();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object of VodDsmcc_AppReqData");
            break;
        case GENERIC_APPRES:
            descType = new VodDsmcc_AppResData();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object of VodDsmcc_AppResData");
            break;
        case GENERIC_VENREQ:
            descType = new VodDsmcc_SeaReqData();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object of VodDsmcc_SeaReqData");
            break;
        default:
            descType = NULL;
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " No match found");

            break;
        }
    }
    if (category == APPREQ)
    {
        switch (type)
        {
        case APPREQ_BILLINGID:
            descType = new VodDsmcc_BillingIdDesc();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object of VodDsmcc_BillingIdDesc");
            break;
        case APPREQ_PURCHASETIME:
            descType = new VodDsmcc_PurchaseTimeDesc();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object VodDsmcc_PurchaseTimeDesc");
            break;
        case APPREQ_TIMEREMAINING:
            descType = new VodDsmcc_TimeRemainingDesc();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
                 " Object VodDsmcc_TimeRemainingDesc");
            break;
        case APPREQ_SSPOPPROT:
            descType = NULL;
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object APPREQ_SSPOPPROT");
            break;
        default:
            descType = NULL;
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " No match found");
            break;
        }
    }
    if (category == VENDREQ)
    {
        switch (type)
        {
        case VENREQ_FUNCTION:
            descType = new VodDsmcc_FunctionDesc();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object of VodDsmcc_FunctionDesc");
            break;
        case VENREQ_SUBFUNCTION:
            descType = new VodDsmcc_SubFunctionDesc();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object VodDsmcc_SubFunctionDesc");
            break;
        case VENREQ_IPDESC:
            descType = new VodDsmcc_IPType();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object VodDsmcc_IPType");
            break;
        case VENREQ_STREAMID:
            descType = new VodDsmcc_StreamId();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object VodDsmcc_StreamHandle");
            break;
        case VENREQ_KEEPALIVE:
            descType = new VodDsmcc_KeepAlive();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object VodDsmcc_KeepAlive");
            break;
        case VENREQ_PURCHASEID:
        case VENREQ_SMARTCARDID:
        case VENREQ_TYPEINST:
        case VENREQ_SIGANALOGCPYST:
        case VENREQ_SUPERCASID:
        case VENREQ_PACKAGEID:
        case VENREQ_IPTARGET:
        case VENREQ_ASSETINFO:
        case VENREQ_ON2IPTARGET:
        case VENREQ_IPV6TARGET:
        case VENREQ_CLIENTREQ:
        case VENREQ_SSPOPPROT:
        case VENREQ_PROIRITY:
        case VENREQ_FORCETSID:
        case VENREQ_FORCEDNP:
        case VENREQ_REQLOGENTRY:
        case VENREQ_PROVIDERID:
        case VENREQ_PROVASSETID:
        case VENREQ_APPNAME:
        case VENREQ_CPE_ID:
        case VENREQ_BDT_AESA:
        case VENREQ_BP_ID:
        case VENREQ_NPT:
        case VENREQ_EXTERNALSESSID:
        case VENREQ_SERVERAREANAME:
        case VENREQ_STBID:
        case VENREQ_VENDORCOPYPROT:
        case VENREQ_STREAMSOURCEIP:
        case VENREQ_ADSTYPE:
        case VENREQ_ADSDATA:
        case VENREQ_PLAYLISTDUR:
            descType = new VodDsmcc_SEAGeneric(Len);
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object VodDsmcc_SEAGeneric");
            break;

        default:
            descType = NULL;
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " No match found");
            break;
        }
    }
    return descType;
}
ui8 * VodDsmcc_Descriptor::PackDescriptor(ui8 *pBuffer)
{
    pBuffer = Utils::Put1Byte(pBuffer, Tag);
    pBuffer = Utils::Put1Byte(pBuffer, Len);
    pBuffer = Value->PackUserDescValueType(pBuffer);
    return pBuffer;
}

status VodDsmcc_Descriptor::ParseDescriptor(Desc_Category category, ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < DESC_BODY_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             ": Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // Tag
        data = Utils::Get1Byte(data, &Tag);
        // Len
        data = Utils::Get1Byte(data, &Len);

        *newData = data;
        *newLength = length - (data - start);
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, " %s:%d Tag %d Len:%d\n", __FUNCTION__, __LINE__, Tag, Len);

        VodDsmcc_UserDescValueType *userDesc = getDescriptorObjectFromType(
                category, Tag);
        if (userDesc)
        {
            userDesc->ParseUserDescValueType(*newData, *newLength, newData,
                                             newLength);
            Value = userDesc;
            result = E_TRUE;
        }
        else
        {
            dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, " %s:%d userDesc is NULL", __FUNCTION__, __LINE__);
        }
    }//end else
    return result;
}

//##############################################################################

ui8 * VodDsmcc_UserPrivateData::PackUserPrivateData(ui8 *pBuffer)
{
    if (SSPProtocolID == SSP_PROTOCOL_ID_1)
    {
        pBuffer = Utils::Put1Byte(pBuffer, SSPProtocolID);
        pBuffer = Utils::Put1Byte(pBuffer, SSPVersion);
        pBuffer = Utils::Put1Byte(pBuffer, DescCount);
        //pBuffer = Desc.PackDescriptor(pBuffer);
        vector<VodDsmcc_Descriptor>::const_iterator itrDesc;
        for (itrDesc = Desc.begin(); itrDesc != Desc.end(); itrDesc++)
        {
            VodDsmcc_Descriptor desc = *itrDesc;
            pBuffer = desc.PackDescriptor(pBuffer);
        }

        while (PadBytes)
        {
            pBuffer = Utils::Put1Byte(pBuffer, 0x44);
            PadBytes--;
        }
    }
    else if (SSPProtocolID == SSP_PROTOCOL_ID_2)
    {
        pBuffer = Utils::Put1Byte(pBuffer, SSPProtocolID);
        pBuffer = Utils::Put1Byte(pBuffer, SSPVersion);
        pBuffer = Utils::InsertBuffer(pBuffer, ServiceGateway, SERVICE_GW_SIZE);
        pBuffer = Utils::Put4Byte(pBuffer, ServiceGatewayDataLength);
        pBuffer = Utils::InsertBuffer(pBuffer, (ui8*)Service, SERVICE_SIZE);
        pBuffer = Utils::Put4Byte(pBuffer, ServiceDataLength);
        pBuffer = Utils::Put1Byte(pBuffer, DescCount);

        vector<VodDsmcc_Descriptor>::const_iterator itrDesc;
        for (itrDesc = Desc.begin(); itrDesc != Desc.end(); itrDesc++)
        {
            VodDsmcc_Descriptor desc = *itrDesc;
            pBuffer = desc.PackDescriptor(pBuffer);
        }

        while (PadBytes)
        {
            pBuffer = Utils::Put1Byte(pBuffer, 0x44);
            PadBytes--;
        }

    }
    else
    {
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             ":Unsupported SSPProtocolID:%d!\n", SSPProtocolID);
    }

    return pBuffer;
}

status VodDsmcc_UserPrivateData::ParseUserPrivateData(ui8 * data, ui32 length, ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < REQ_DESC_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             ": Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // SSPProtocolID
        data = Utils::Get1Byte(data, &SSPProtocolID);
        // SSPVersion
        data = Utils::Get1Byte(data, &SSPVersion);

        if (SSPProtocolID == SSP_PROTOCOL_ID_1)
        {
            // DescCount
            data = Utils::Get1Byte(data, &DescCount);

            *newData = data;
            *newLength = length - (data - start);

            for (i8 i = 0; i < DescCount; i++)
            {
                VodDsmcc_Descriptor desc;
                //desc = desc->getDescriptorObjectFromType(GENERIC,*data);
                desc.ParseDescriptor(GENERIC, *newData, *newLength, newData,
                                     newLength);
                Desc.push_back(desc);
            }
            result = E_TRUE;
        }
        else if (SSPProtocolID == SSP_PROTOCOL_ID_2)
        {
            if (SSPVersion == SSP_VERSION_1)
            {
                //ServiceGateway
                data = Utils::GetBuffer(data, ServiceGateway, SERVICE_GW_SIZE);
                //ServiceGatewayDataLength
                data = Utils::Get4Byte(data, &ServiceGatewayDataLength);

                if (ServiceGatewayDataLength != 0)
                {
                    //Service
                    data = Utils::GetBuffer(data, Service, SERVICE_SIZE);
                    //ServiceDataLength
                    data = Utils::Get4Byte(data, &ServiceDataLength);

                    if (ServiceDataLength != 0)
                    {
                        // DescCount
                        data = Utils::Get1Byte(data, &DescCount);
                        *newData = data;
                        *newLength = length - (data - start);

                        for (i8 i = 0; i < DescCount; i++)
                        {
                            VodDsmcc_Descriptor desc;
                            desc.ParseDescriptor(GENERIC, *newData, *newLength, newData,
                                                 newLength);
                            Desc.push_back(desc);
                        }

                        result = E_TRUE;
                    }
                    else
                    {
                        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
                             "ServiceDataLength is %d ", ServiceDataLength);
                    }
                }
                else
                {
                    dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
                         "ServiceGatewayDataLength is %d ", ServiceGatewayDataLength);
                }
            }
            else
            {
                dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "Error Unsupported version:%d ", SSPVersion);
            }
        }
        else
        {
            dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
                 ":Unsupported SSPProtocolID:%d", SSPProtocolID);
        }

    }//end else
    return result;
}

//##############################################################################
ui8 * VodDsmcc_UserUserData::PackUserUserData(ui8 *pBuffer)
{
    pBuffer = Utils::Put1Byte(pBuffer, SSPProtocolID);
    pBuffer = Utils::Put1Byte(pBuffer, SSPVersion);
    pBuffer = Utils::Put1Byte(pBuffer, DescCount);
    //pBuffer = Desc.PackDescriptor(pBuffer);
    vector<VodDsmcc_Descriptor>::const_iterator itrDesc;
    for (itrDesc = Desc.begin(); itrDesc != Desc.end(); itrDesc++)
    {
        VodDsmcc_Descriptor desc = *itrDesc;
        pBuffer = desc.PackDescriptor(pBuffer);
    }
    return pBuffer;
}

status VodDsmcc_UserUserData::ParseUserUserData(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < REQ_DESC_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             ": Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // SSPProtocolID
        data = Utils::Get1Byte(data, &SSPProtocolID);
        // SSPVersion
        data = Utils::Get1Byte(data, &SSPVersion);
        // DescCount
        data = Utils::Get1Byte(data, &DescCount);

        *newData = data;
        *newLength = length - (data - start);

        for (i32 i = 0; i < DescCount; i++)
        {
            VodDsmcc_Descriptor descriptor;
            descriptor.ParseDescriptor(GENERIC, *newData, *newLength,  newData,
                                       newLength);
        }
        result = E_TRUE;
    }//end else
    return result;
}

//##############################################################################
//class VodDsmcc_ResourceDescriptor

VodDsmcc_UserDescValueType * VodDsmcc_ResourceDescriptor::GetResDescObjectFromType(
    ui16 resType)
{
    VodDsmcc_UserDescValueType * descType = NULL;
    switch (resType)
    {
    case DSMCC_RESDESC_MPEGPROG:
        descType = new VodDsmcc_MPEGProgResData();
        dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " Object of VodDsmcc_MPEGProgResData");
        break;
    case DSMCC_RESDESC_PHYSICALCHAN:
        descType = new VodDsmcc_PhysicalChannelResData();
        dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT,
             " Object VodDsmcc_PhysicalChannelResData");
        break;
    case DSMCC_RESDESC_DOWNSTREAMTRANS:
        descType = new VodDsmcc_DSTStreamData();
        dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Object of VodDsmcc_DSTStreamData");
        break;
    case DSMCC_RESDESC_IP:
        descType = NULL;
        dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Object of dsmcc_ResDesc_IP");
        break;
    case DSMCC_RESDESC_ATSCMODMODE:
        descType = new VodDsmcc_ATSCModulationData();
        dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Object of VodDsmcc_ATSCModulationData");
        break;
    case DSMCC_RESDESC_HEADENDID:
        descType = NULL;
        dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Object of dsmcc_ResDesc_HeadEndId");
        break;
    case DSMCC_RESDESC_SERVERCA:
        descType = NULL;
        dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Object of dsmcc_ResDesc_ServerCA");
        break;
    case DSMCC_RESDESC_CLIENTCA:
        descType = new VodDsmcc_ClientCA();
        dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Object of VodDsmcc_ClientCA");
        break;
    case DSMCC_RESDESC_ETHERNETINT:
        descType = NULL;
        dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Parse dsmcc_ResDesc_EthernetInt");
        break;
    case DSMCC_RESDESC_SERVICEGROUP:
        descType = NULL;
        dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Object of dsmcc_ResDesc_ServiceGroup");
        break;
    case DSMCC_RESDESC_VIRTUALCHAN:
        descType = NULL;
        dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Object of dsmcc_ResDesc_VitrualChan");
        break;
    default:
        descType = NULL;
        dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, " No match found");
        break;
    }
    return descType;
}

status VodDsmcc_ResourceDescriptor::ParseResourceDesc(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < RESOURCE_DESC_SIZE) //update size
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             ": Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // ResourceRequestId
        data = Utils::Get2Byte(data, &ResourceRequestId);
        // ResourceDescriptorType
        data = Utils::Get2Byte(data, &ResourceDescriptorType);
        // ResourceNum
        data = Utils::Get2Byte(data, &ResourceNum);
        // AssociationTag
        data = Utils::Get2Byte(data, &AssociationTag);
        // ResourceFlags
        data = Utils::Get1Byte(data, &ResourceFlags);
        // ResourceStatus
        data = Utils::Get1Byte(data, &ResourceStatus);
        // ResourceLength
        data = Utils::Get2Byte(data, &ResourceLength);
        // ResourceDataFieldCount
        data = Utils::Get2Byte(data, &ResourceDataFieldCount);

        *newData = data;
        *newLength = length - (data - start);

        DescValue = GetResDescObjectFromType(ResourceDescriptorType);
        if (DescValue)
        {
            DescValue->ParseUserDescValueType(*newData, *newLength, newData,
                                              newLength);
        }

        result = E_TRUE;
    }//end else
    return result;
}
//##############################################################################

ui8 * VodDsmcc_UserData::PackUserData(ui8 *pBuffer)
{

    pBuffer = Utils::Put2Byte(pBuffer, UuDataCount);
    if (UuDataCount)
    {
        pBuffer = UuDataObj.PackUserUserData(pBuffer);
    }
    pBuffer = Utils::Put2Byte(pBuffer, PrivateDataCount);
    if (PrivateDataCount)
    {
        pBuffer = PrivateDataObj.PackUserPrivateData(pBuffer);
    }
    return pBuffer;
}

status VodDsmcc_UserData::ParseUserData(ui8 * data, ui32 length,
                                        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < SIZE_FOUR_BYTE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR,
             ": Message not long enough to contain !\n");
    }
    else
    {
        // remember the start of the message
        start = data;
        // ResourceRequestId
        data = Utils::Get2Byte(data, &UuDataCount);

        *newData = data;
        *newLength = length - (data - start);

        if (UuDataCount != 0)
        {
            UuDataObj.ParseUserUserData(*newData, *newLength,
                                        newData, newLength);

        }
        // private data
        start = *newData;
        *newData = Utils::Get2Byte(*newData, &PrivateDataCount);
        *newLength = *newLength - (*newData - start);

        // AssociationTag
        if (PrivateDataCount != 0)
        {
            PrivateDataObj.ParseUserPrivateData(*newData, *newLength,
                                                newData, newLength);
        }

        result = E_TRUE;
    }//end else
    return result;

}
//##############################################################################

VodDsmcc_ClientSessionSetup::VodDsmcc_ClientSessionSetup(ui16 msgId,
        ui32 transId, ui16 msgLen, VodDsmcc_SessionId &sessionId, ui8 *clientId,
        ui8 *serverId, VodDsmcc_UserData &userData) :
    VodDsmcc_Base(msgId, transId, msgLen)
{
    SessionId = sessionId;
    Reserved = 0xFFFF;
    memcpy(ClientId, clientId, DSMCC_CLIENTID_LEN);
    memcpy(ServerId, serverId, DSMCC_SERVERID_LEN);
    UserData = userData;
}

//##############################################################################
VodDsmcc_ClientSessionSetup::~VodDsmcc_ClientSessionSetup()
{
    // free userdata
    VodDsmcc_UserPrivateData privData = UserData.GetPrivateDataObj();
    vector<VodDsmcc_Descriptor> privDesc = privData.GetDescriptor();
    vector<VodDsmcc_Descriptor>::const_iterator itr;
    for (itr = privDesc.begin(); itr != privDesc.end(); itr++)
    {
        VodDsmcc_Descriptor desc = *itr;
        VodDsmcc_UserDescValueType * descValue = desc.GetValue();
        if (descValue)
        {
            delete descValue;
            descValue = NULL;
        }
    }
    VodDsmcc_UserUserData usrData = UserData.GetUuDataObj();
    vector<VodDsmcc_Descriptor> userDesc = usrData.GetDescriptor();
    for (itr = userDesc.begin(); itr != userDesc.end(); itr++)
    {
        VodDsmcc_Descriptor desc = *itr;
        VodDsmcc_UserDescValueType * descValue = desc.GetValue();
        if (descValue)
        {
            delete descValue;
            descValue = NULL;
        }
    }

}

//##############################################################################
status VodDsmcc_ClientSessionSetup::ParseDsmccMessageBody(ui8 * data,
        ui32 length)
{
    status result = E_FALSE;
    data = data;
    length = length;
    return result;
}

status VodDsmcc_ClientSessionSetup::PackDsmccMessageBody(ui8 **pOut, ui32 *len)
{
    status result = E_FALSE;
    ui8 pBuf[MSGBODY_BUFFER_LEN] = {0};
    ui8 *pTemp = NULL;

    //dlog(DL_MSP_ONDEMAND,DLOGL_ERROR, "SdbSess[%s]: bufLen is %u\n", __FUNCTION__, bufLen);
    pTemp = pBuf;
    pTemp = Utils::InsertBuffer(pTemp, SessionId.GetSessionId(), DSMCC_SESSIONID_LEN);
    pTemp = Utils::Put2Byte(pTemp, Reserved);
    pTemp = Utils::InsertBuffer(pTemp, ClientId, DSMCC_CLIENTID_LEN);
    pTemp = Utils::InsertBuffer(pTemp, ServerId, DSMCC_SERVERID_LEN);
    pTemp = UserData.PackUserData(pTemp);

    result = PackDsmccMessageHdr(pBuf, pTemp - pBuf, pOut, len);

    return result;
}

//##############################################################################

VodDsmcc_ClientSessionConfirm::~VodDsmcc_ClientSessionConfirm()
{
    //free resource desc
    vector<VodDsmcc_ResourceDescriptor>::const_iterator resItr;
    for (resItr = Resources.begin(); resItr != Resources.end(); resItr++)
    {
        VodDsmcc_ResourceDescriptor resDesc = *resItr;
        VodDsmcc_UserDescValueType * descValue = resDesc.GetDescValue();
        if (descValue)
        {
            delete descValue;
            descValue = NULL;
        }
    }
    //free user data
    VodDsmcc_UserPrivateData privData = UserData.GetPrivateDataObj();
    vector<VodDsmcc_Descriptor> privDesc = privData.GetDescriptor();
    vector<VodDsmcc_Descriptor>::const_iterator itr;
    for (itr = privDesc.begin(); itr != privDesc.end(); itr++)
    {
        VodDsmcc_Descriptor desc = *itr;
        VodDsmcc_UserDescValueType * descValue = desc.GetValue();
        if (descValue)
        {
            delete descValue;
            descValue = NULL;
        }
    }
    VodDsmcc_UserUserData usrData = UserData.GetUuDataObj();
    vector<VodDsmcc_Descriptor> userDesc = usrData.GetDescriptor();
    for (itr = userDesc.begin(); itr != userDesc.end(); itr++)
    {
        VodDsmcc_Descriptor desc = *itr;
        VodDsmcc_UserDescValueType * descValue = desc.GetValue();
        if (descValue)
        {
            delete descValue;
            descValue = NULL;
        }
    }
}

void VodDsmcc_ClientSessionConfirm::PopulateArrisResponseCodes()
{
    mArrisResponseCodeMap[dsmcc_RspOK] = "Request completed without errors";
    mArrisResponseCodeMap[dsmcc_RspClNoSession] = "Client Rejected, invalid session id";
    mArrisResponseCodeMap[dsmcc_RspNeNoCalls] = "SRM unable to accept new calls";
    mArrisResponseCodeMap[dsmcc_RspNeInvalidClient] = "SRM Rejected invalid clientId";
    mArrisResponseCodeMap[dsmcc_RspNeInvalidServer] = "SRM Rejected invalid server Id";
    mArrisResponseCodeMap[dsmcc_RspNeNoSession] = "SRM Rejected invalid session id";
    mArrisResponseCodeMap[dsmcc_RspSeNoCalls] = "Server unable to accept new calls or NAG check failed";
    mArrisResponseCodeMap[dsmcc_RspSeInvalidClient] = "Server Rejected invalid clientId";
    mArrisResponseCodeMap[dsmcc_RspSeNoService] = "Server rejected Service Not available";
    mArrisResponseCodeMap[dsmcc_RspSeNoContinuousFeedSessionForServer] = "No Continuous Feed Session (CFS) for Server";
    mArrisResponseCodeMap[dsmcc_RspSeNoRspFromClient] = "Network timed Out - no response from Client";
    mArrisResponseCodeMap[dsmcc_RspSeNoRspFromServer] = "Network timed Out - no response from Server";
    mArrisResponseCodeMap[dsmcc_RspSeNoSession] = "Server timed out, invalid sessionId";
    mArrisResponseCodeMap[dsmcc_RspSeAltResourceValue] = "Network has assigned alternate resource value";
    mArrisResponseCodeMap[dsmcc_RspSeNetworkUnableToAssignResources] = "Network unable to assign resources";
    mArrisResponseCodeMap[dsmcc_RspSeNoErrInResourceCmd] = "No errors in resource command";
    mArrisResponseCodeMap[dsmcc_RspSeResourceHavAltValue] = "Resource(s) have alternate value";
    mArrisResponseCodeMap[dsmcc_RspSeNetWaitOnServer] = "Network is waiting on Server";
    mArrisResponseCodeMap[dsmcc_RspSeUnknwReqIdForClient] = "Unknown request ID for Client";
    mArrisResponseCodeMap[dsmcc_RspSeClientCantUseResources] = "Client can't use resources";
    mArrisResponseCodeMap[dsmcc_RspSeClientRejects] = "Client rejects - no calls now";
    mArrisResponseCodeMap[dsmcc_RspSeNetworkCantAssignResources] = "Network can't assign resource(s)";
    mArrisResponseCodeMap[dsmcc_RspSeNoResource] = "Server unable to complete session setup owing to missing resourc";
    mArrisResponseCodeMap[dsmcc_RspSeProcError] = "Server detected error";
    mArrisResponseCodeMap[dsmcc_RspSeFormatError] = "Server detected a format error";
}

status VodDsmcc_ClientSessionConfirm::ParseDsmccMessageBody(ui8 * data,
        ui32 length)
{
    status result = E_FALSE;
    ui8 * newPointer, *start;
    ui32 newLen;

    result = ParseDsmccMessageHdr(data, length, &newPointer, &newLen);

    if (result == E_TRUE && newLen >= 34)
    {
        start = newPointer;
        //parse sessionid
        // adaptation header length
        ui8 sessId[DSMCC_SESSIONID_LEN];
        newPointer = Utils::GetBuffer(newPointer, sessId, DSMCC_SESSIONID_LEN);
        SessionId.SetSessionId(sessId);
        // adaptation header length
        newPointer = Utils::Get2Byte(newPointer, &Response);

        // adaptation header length
        newPointer = Utils::GetBuffer(newPointer, ServerId, DSMCC_SERVERID_LEN);

        // adaptation header length
        newPointer = Utils::Get2Byte(newPointer, &ResourceCount);

        newLen = newLen - (newPointer - start);

        for (int i = 0; i < ResourceCount; i++)
        {
            VodDsmcc_ResourceDescriptor resDesc;
            resDesc.ParseResourceDesc(newPointer, newLen,
                                      &newPointer, &newLen); //Amit check first 2 param
            Resources.push_back(resDesc);
        }
        if (newLen)
        {

            UserData.ParseUserData(newPointer, newLen, &newPointer,
                                   &newLen); //Amit check first 2 param
        }
        if (newLen != 0)
            result = E_FALSE;
    }

    return result;
}

status VodDsmcc_ClientSessionConfirm::PackDsmccMessageBody(ui8 **pOut,
        ui32 *len)
{
    status result = E_FALSE;
    pOut = pOut;
    len = len;
    return result;
}

//##############################################################################

VodDsmcc_ClientConnect::~VodDsmcc_ClientConnect()
{
    VodDsmcc_UserPrivateData privData = UserData.GetPrivateDataObj();
    vector<VodDsmcc_Descriptor> privDesc = privData.GetDescriptor();
    vector<VodDsmcc_Descriptor>::const_iterator itr;
    for (itr = privDesc.begin(); itr != privDesc.end(); itr++)
    {
        VodDsmcc_Descriptor desc = *itr;
        VodDsmcc_UserDescValueType * descValue = desc.GetValue();
        if (descValue)
        {
            delete descValue;
            descValue = NULL;
        }
    }
    VodDsmcc_UserUserData usrData = UserData.GetUuDataObj();
    vector<VodDsmcc_Descriptor> userDesc = usrData.GetDescriptor();
    for (itr = userDesc.begin(); itr != userDesc.end(); itr++)
    {
        VodDsmcc_Descriptor desc = *itr;
        VodDsmcc_UserDescValueType * descValue = desc.GetValue();
        if (descValue)
        {
            delete descValue;
            descValue = NULL;
        }
    }
}

VodDsmcc_ClientConnect::VodDsmcc_ClientConnect(ui16 msgId, ui32 transId,
        ui16 msgLen, VodDsmcc_SessionId &sessionId, VodDsmcc_UserData &userData) :
    VodDsmcc_Base(msgId, transId, msgLen)
{
    SessionId = sessionId;
    UserData = userData;
}

status VodDsmcc_ClientConnect::ParseDsmccMessageBody(ui8 * data, ui32 length)
{
    status result = E_FALSE;
    data = data;
    length = length;

    return result;
}

status VodDsmcc_ClientConnect::PackDsmccMessageBody(ui8 **pOut, ui32 *len)
{
    status result = E_FALSE;
    ui8 pBuf[MSGBODY_BUFFER_LEN] = {0};
    ui8 *pTemp = NULL;
    //dlog(DL_MSP_ONDEMAND,DLOGL_ERROR, "SdbSess[%s]: bufLen is %u\n", __FUNCTION__, bufLen);
    pTemp = pBuf;
    pTemp = Utils::InsertBuffer(pTemp, SessionId.GetSessionId(), DSMCC_SESSIONID_LEN);
    pTemp = UserData.PackUserData(pTemp);

    PackDsmccMessageHdr(pBuf, pTemp - pBuf, pOut, len);

    return result;
}

//##############################################################################

VodDsmcc_ClientReleaseGeneric::~VodDsmcc_ClientReleaseGeneric()
{
    VodDsmcc_UserPrivateData privData = UserData.GetPrivateDataObj();
    vector<VodDsmcc_Descriptor> privDesc = privData.GetDescriptor();
    vector<VodDsmcc_Descriptor>::const_iterator itr;
    for (itr = privDesc.begin(); itr != privDesc.end(); itr++)
    {
        VodDsmcc_Descriptor desc = *itr;
        VodDsmcc_UserDescValueType * descValue = desc.GetValue();
        if (descValue)
        {
            delete descValue;
            descValue = NULL;
        }
    }
    VodDsmcc_UserUserData usrData = UserData.GetUuDataObj();
    vector<VodDsmcc_Descriptor> userDesc = usrData.GetDescriptor();
    for (itr = userDesc.begin(); itr != userDesc.end(); itr++)
    {
        VodDsmcc_Descriptor desc = *itr;
        VodDsmcc_UserDescValueType * descValue = desc.GetValue();
        if (descValue)
        {
            delete descValue;
            descValue = NULL;
        }
    }
}

VodDsmcc_ClientReleaseGeneric::VodDsmcc_ClientReleaseGeneric(ui16 msgId,
        ui32 transId, ui16 msgLen, VodDsmcc_SessionId &sessionId, ui16 reason,
        VodDsmcc_UserData &userData) :
    VodDsmcc_Base(msgId, transId, msgLen)
{
    SessionId.SetSessionId(sessionId.GetSessionId());
    Reason = reason;
    UserData = userData;
}

void VodDsmcc_ClientReleaseGeneric::PopulateArrisReasonCodes()
{
    mArrisReasonCodeMap[dsmcc_RsnOK] = "Proceeding Normally";
    mArrisReasonCodeMap[dsmcc_RsnNormal] = "Normal conditions for Releasing";
    mArrisReasonCodeMap[dsmcc_RsnClProcError] = "Client detected procedure error";
    mArrisReasonCodeMap[dsmcc_RsnSEProcError] = "Server detected procedure error";
    mArrisReasonCodeMap[dsmcc_RsnClFormatError] = "Client detected invalid format";
    mArrisReasonCodeMap[dsmcc_RsnNeFormatError] = "SRM detected invalid format";
    mArrisReasonCodeMap[dsmcc_RsnClNoSession] = "Client indicates that the sessionId indicated in a message is not active";
    mArrisReasonCodeMap[dsmcc_RsnRetrans] = "Retransmitted message";
    mArrisReasonCodeMap[dsmcc_RsnNoTransaction] = "Message received without transactionId";
    mArrisReasonCodeMap[dsmcc_RsnClSessionRelease] = "Client Initiated session release";
    mArrisReasonCodeMap[dsmcc_RsnSeSessionRelease] = "Server Initiated session release";
    mArrisReasonCodeMap[dsmcc_RsnNeSessionRelease] = "SRM Initiated session release";
}

status VodDsmcc_ClientReleaseGeneric::ParseDsmccMessageBody(ui8 * data,
        ui32 length)
{
    status result = E_FALSE;
    ui8 * newPointer, *start;
    ui32 newLen;

    result = ParseDsmccMessageHdr(data, length, &newPointer, &newLen);

    // 16 = sessionid(10)+ reason(2)+ min length of userdata(4)
    if (result == E_TRUE && newLen >= 16)
    {
        start = newPointer;
        //parse sessionid
        ui8 sessId[DSMCC_SESSIONID_LEN];
        newPointer = Utils::GetBuffer(newPointer, sessId,
                                      DSMCC_SESSIONID_LEN);
        SessionId.SetSessionId(sessId);

        // adaptation header length
        newPointer = Utils::Get2Byte(newPointer, &Reason);

        newLen = newLen - (newPointer - start);

        if (newLen)
        {

            UserData.ParseUserData(newPointer, newLen, &newPointer,
                                   &newLen);
        }
        if (newLen != 0)
        {
            result = E_FALSE;
        }
    }
    return result;
}

status VodDsmcc_ClientReleaseGeneric::PackDsmccMessageBody(ui8 **pOut,
        ui32 *len)
{
    status result = E_FALSE;
    ui8 pBuf[MSGBODY_BUFFER_LEN] = {0};
    ui8 *pTemp = NULL;

    pTemp = pBuf;
    pTemp = Utils::InsertBuffer(pTemp, SessionId.GetSessionId(), DSMCC_SESSIONID_LEN);
    pTemp = Utils::Put2Byte(pTemp, Reason);
    pTemp = UserData.PackUserData(pTemp);

    PackDsmccMessageHdr(pBuf, pTemp - pBuf, pOut, len);

    return result;
}

//##############################################################################
VodDsmcc_ClientResetGeneric::VodDsmcc_ClientResetGeneric(ui16 msgId,
        ui32 transId, ui16 msgLen, ui8 *clientId, ui16 reason) :
    VodDsmcc_Base(msgId, transId, msgLen)
{
    Reason = reason;
    memcpy(ClientId, clientId, DSMCC_CLIENTID_LEN);

}


status VodDsmcc_ClientResetGeneric::ParseDsmccMessageBody(ui8 * data,
        ui32 length)
{
    status result = E_FALSE;
    ui8 * newPointer, *start;
    ui32 newLen;

    result = ParseDsmccMessageHdr(data, length, &newPointer, &newLen);

    if (result == E_TRUE && newLen >= CLIENT_RESET_GEN_SIZE)
    {
        start = newPointer;

        // adaptation header length
        newPointer = Utils::GetBuffer(newPointer, ClientId, DSMCC_CLIENTID_LEN);

        // Reason
        newPointer = Utils::Get2Byte(newPointer, &Reason);

        newLen = newLen - (newPointer - start);
        if (newLen != 0)
        {
            result = E_FALSE;
        }
    }
    return result;
}

status VodDsmcc_ClientResetGeneric::PackDsmccMessageBody(ui8 **pOut, ui32 *len)
{
    status result = E_FALSE;
    ui8 pBuf[MSGBODY_BUFFER_LEN] = {0};
    ui8 *pTemp = NULL;
    //dlog(DL_MSP_ONDEMAND,DLOGL_ERROR, "SdbSess[%s]: bufLen is %u\n", __FUNCTION__, bufLen);
    pTemp = pBuf;
    pTemp = Utils::InsertBuffer(pTemp, ClientId, DSMCC_CLIENTID_LEN);
    pTemp = Utils::Put2Byte(pTemp, Reason);

    PackDsmccMessageHdr(pBuf, pTemp - pBuf, pOut, len);

    return result;
}

//##############################################################################

VodDsmcc_ClientSessionInProgress::VodDsmcc_ClientSessionInProgress(ui16 msgId,
        ui32 transId, ui16 msgLen, ui16 sessCount, vector <VodDsmcc_SessionId> sessionIdList) :
    VodDsmcc_Base(msgId, transId, msgLen)
{
    SessionCount = sessCount;
    SessionIdList = sessionIdList;
}


status VodDsmcc_ClientSessionInProgress::ParseDsmccMessageBody(ui8 * data,
        ui32 length)
{
    status result = E_FALSE;
    data = data;
    length = length;

    return result;
}

status VodDsmcc_ClientSessionInProgress::PackDsmccMessageBody(ui8 **pOut,
        ui32 *len)
{
    status result = E_FALSE;
    ui8 pBuf[MSGBODY_BUFFER_LEN] = {0};
    ui8 *pTemp = NULL;
    //dlog(DL_MSP_ONDEMAND,DLOGL_ERROR, "SdbSess[%s]: bufLen is %u\n", __FUNCTION__, bufLen);
    pTemp = pBuf;
    pTemp = Utils::Put2Byte(pTemp, SessionCount);
    vector<VodDsmcc_SessionId>::const_iterator itrSessList;
    for (itrSessList = SessionIdList.begin(); itrSessList != SessionIdList.end(); itrSessList++)
    {
        VodDsmcc_SessionId sessId = *itrSessList;
        pTemp = Utils::InsertBuffer(pTemp, sessId.GetSessionId(), DSMCC_SESSIONID_LEN);
    }

    PackDsmccMessageHdr(pBuf, pTemp - pBuf, pOut, len);

    return result;
}

//##############################################################################

VodDsmcc_ClientStatusRequest::~VodDsmcc_ClientStatusRequest()
{
    if (StatusByte)
    {
        delete StatusByte;
        StatusByte = NULL;
    }
}

VodDsmcc_ClientStatusRequest::VodDsmcc_ClientStatusRequest(ui16 msgId,
        ui32 transId, ui16 msgLen, ui16 reason, ui8 *clientId, ui16 statusType,
        ui16 statusCount, ui8* statusByte) :
    VodDsmcc_Base(msgId, transId, msgLen)
{
    Reason = reason;
    memcpy(ClientId, clientId, DSMCC_CLIENTID_LEN);
    StatusType = statusType;
    StatusCount = statusCount;
    StatusByte = new ui8[statusCount];
    if (StatusByte && statusByte)
    {
        memcpy(StatusByte, statusByte, statusCount);
    }
}


status VodDsmcc_ClientStatusRequest::ParseDsmccMessageBody(ui8 * data,
        ui32 length)
{
    status result = E_FALSE;
    data = data;
    length = length;

    return result;
}

status VodDsmcc_ClientStatusRequest::PackDsmccMessageBody(ui8 **pOut, ui32 *len)
{
    status result = E_FALSE;
    ui8 pBuf[MSGBODY_BUFFER_LEN] = {0};
    ui8 *pTemp = NULL;
    //dlog(DL_MSP_ONDEMAND,DLOGL_ERROR, "SdbSess[%s]: bufLen is %u\n", __FUNCTION__, bufLen);
    pTemp = pBuf;
    pTemp = Utils::Put2Byte(pTemp, Reason);
    pTemp = Utils::InsertBuffer(pTemp, ClientId, DSMCC_CLIENTID_LEN);
    pTemp = Utils::Put2Byte(pTemp, StatusType);
    pTemp = Utils::Put2Byte(pTemp, StatusCount);

    PackDsmccMessageHdr(pBuf, pTemp - pBuf, pOut, len);

    return result;
}

//##############################################################################
VodDsmcc_ClientStatusConfirm::~VodDsmcc_ClientStatusConfirm()
{
    if (StatusByte)
    {
        delete StatusByte;
        StatusByte = NULL;
    }
}

status VodDsmcc_ClientStatusConfirm::ParseDsmccMessageBody(ui8 * data,
        ui32 length)
{
    status result = E_FALSE;
    ui8 * newPointer, *start;
    ui32 newLen;

    result = ParseDsmccMessageHdr(data, length, &newPointer, &newLen);

    if (result == E_TRUE && newLen >= CLIENT_STATUS_CONFIRM_SIZE)
    {
        start = newPointer;

        // Reason
        newPointer = Utils::Get2Byte(newPointer, &Reason);

        // StatusType
        newPointer = Utils::Get2Byte(newPointer, &StatusType);

        // StatusType
        newPointer = Utils::Get2Byte(newPointer, &StatusCount);

        if (StatusCount)
        {
            StatusByte = new ui8[StatusCount];
            if (StatusByte)
            {
                newPointer = Utils::GetBuffer(newPointer, StatusByte , StatusCount);
            }
        }

        newLen = newLen - (newPointer - start);

        if (newLen != 0)
        {
            result = E_FALSE;
        }
    }
    return result;
}

status VodDsmcc_ClientStatusConfirm::PackDsmccMessageBody(ui8 **pOut, ui32 *len)
{
    status result = E_FALSE;
    pOut = pOut;
    len = len;

    return result;
}




