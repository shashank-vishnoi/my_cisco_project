#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include  <sys/types.h>
#include <netinet/in.h>
#include "dlog.h"
#include <stdio.h>
#include <netdb.h>

#include "lscProtocolclass.h"

#define Close(x) do{dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_DEBUG,"%s:%s:%d ZZClose=%d",__FILE__,__FUNCTION__,__LINE__, x); close(x); (x)=-1;}while(0)

//###################################################################################
ui32 VodLscp_Base::getTransId()
{
    srand(time(0));
    static ui32 transid = rand() % 100 + 1;
    return ++transid;
}

VodLscp_Base::VodLscp_Base()
{
    socketFD = -1;
    version = 0;
    transactionid = 0;
    opcode = 0;
    statuscode = 0;
    streamhandle = 0;

}

VodLscp_Base::~VodLscp_Base()
{
}

VodLscp_Base::VodLscp_Base(ui8 v) :
    version(v)
{
    socketFD = -1;
    transactionid = 0;
    opcode = 0;
    statuscode = 0;
    streamhandle = 0;
}

VodLscp_Base::VodLscp_Base(ui8 v, ui8 op) :
    version(v), opcode(op)
{
    socketFD = -1;
    transactionid = 0;
    statuscode = 0;
    streamhandle = 0;
}

VodLscp_Base::VodLscp_Base(ui8 v, ui8 tr, ui8 op, ui8 stat, ui32 strm) :
    version(v), transactionid(tr), opcode(op), statuscode(stat), streamhandle(
        strm)
{
    socketFD = -1;
}

VodLscp_pause::VodLscp_pause()
{
    stopNPT = NPT_CURRENT;
}

VodLscp_pause::~VodLscp_pause()
{
}

VodLscp_pause::VodLscp_pause(i32 stp, ui8 v, ui8 tr, ui8 op, ui8 stat,
                             ui32 strm) : VodLscp_Base(v, tr, op, stat, strm), stopNPT(stp)
{
}

VodLscp_resume::VodLscp_resume()
{
    startNPT = NPT_CURRENT;
    scaleNum = 1;
    scaleDenom = 1;
}

VodLscp_resume::~VodLscp_resume()
{
}

VodLscp_resume::VodLscp_resume(i32 str, i16 num, ui16 den, ui8 v, ui8 tr,
                               ui8 op, ui8 stat, ui32 strm) :
    VodLscp_Base(v, tr, op, stat, strm), startNPT(str), scaleNum(num), scaleDenom(den)
{

}

VodLscp_status::VodLscp_status()
{
}

VodLscp_status::~VodLscp_status()
{
}

VodLscp_status::VodLscp_status(ui8 v, ui8 tr, ui8 op, ui8 stat, ui32 strm) :
    VodLscp_Base(v, tr, op, stat, strm)
{
}

VodLscp_reset::VodLscp_reset()
{
}

VodLscp_reset::~VodLscp_reset()
{
}
VodLscp_reset::VodLscp_reset(ui8 v, ui8 tr, ui8 op, ui8 stat, ui32 strm) :
    VodLscp_Base(v, tr, op, stat, strm)
{
}

VodLscp_jump::VodLscp_jump()
{
    startNPT = NPT_START;
    stopNPT = NPT_END;
    scaleNum = 1;
    scaleDenom = 1;
}

VodLscp_jump::~VodLscp_jump()
{
}

VodLscp_jump::VodLscp_jump(i32 str, i32 stp, i16 num, ui16 den, ui8 v, ui8 tr,
                           ui8 op, ui8 stat, ui32 strm) :
    VodLscp_Base(v, tr, op, stat, strm), startNPT(str), stopNPT(stp), scaleNum(num), scaleDenom(den)
{
}

VodLscp_play::VodLscp_play()
{
    startNPT = NPT_START;
    stopNPT = NPT_END;
    scaleNum = 1;
    scaleDenom = 1;
}

VodLscp_play::~VodLscp_play()
{
}

VodLscp_play::VodLscp_play(i32 str, i32 stp, i16 num, ui16 den, ui8 v, ui8 tr,
                           ui8 op, ui8 stat, ui32 strm) :
    VodLscp_Base(v, tr, op, stat, strm), startNPT(str), stopNPT(stp), scaleNum(num), scaleDenom(den)
{
}

VodLscp_done_event::VodLscp_done_event()
{
    NPT = 0;
    scaleNum = 1;
    scaleDenom = 1;
    mode = 0;
}

VodLscp_done_event::~VodLscp_done_event()
{
}

VodLscp_done_event::VodLscp_done_event(i32 npt, i16 num, ui16 den, ui8 md,
                                       ui8 v, ui8 tr, ui8 op, ui8 stat, ui32 strm) :
    VodLscp_Base(v, tr, op, stat, strm), NPT(npt), scaleNum(num), scaleDenom(den), mode(md)
{
}

VodLscp_pause_response::VodLscp_pause_response()
{
    NPT = 0;
    scaleNum = 1;
    scaleDenom = 1;
    mode = 0;
}

VodLscp_pause_response::~VodLscp_pause_response()
{
}

VodLscp_pause_response::VodLscp_pause_response(i32 npt, i16 num, ui16 den,
        ui8 md, ui8 v, ui8 tr, ui8 op, ui8 stat, ui32 strm) :
    VodLscp_Base(v, tr, op, stat, strm), NPT(npt), scaleNum(num), scaleDenom(den), mode(md)
{
}

VodLscp_resume_response::VodLscp_resume_response()
{
    NPT = 0;
    scaleNum = 1;
    scaleDenom = 1;
    mode = 0;
}

VodLscp_resume_response::~VodLscp_resume_response()
{
}

VodLscp_resume_response::VodLscp_resume_response(i32 npt, i16 num, ui16 den,
        ui8 md, ui8 v, ui8 tr, ui8 op, ui8 stat, ui32 strm) :
    VodLscp_Base(v, tr, op, stat, strm), NPT(npt), scaleNum(num), scaleDenom(den), mode(md)
{
}

VodLscp_status_response::VodLscp_status_response()
{
    NPT = 0;
    scaleNum = 1;
    scaleDenom = 1;
    mode = 0;
}

VodLscp_status_response::~VodLscp_status_response()
{
}

VodLscp_status_response::VodLscp_status_response(i32 npt, i16 num, ui16 den,
        ui8 md, ui8 v, ui8 tr, ui8 op, ui8 stat, ui32 strm) :
    VodLscp_Base(v, tr, op, stat, strm), NPT(npt), scaleNum(num), scaleDenom(den), mode(md)
{
}

VodLscp_reset_response::VodLscp_reset_response()
{
    NPT = 0;
    scaleNum = 1;
    scaleDenom = 1;
    mode = 0;
}

VodLscp_reset_response::~VodLscp_reset_response()
{
}

VodLscp_reset_response::VodLscp_reset_response(i32 npt, i16 num, ui16 den,
        ui8 md, ui8 v, ui8 tr, ui8 op, ui8 stat, ui32 strm) :
    VodLscp_Base(v, tr, op, stat, strm), NPT(npt), scaleNum(num), scaleDenom(den), mode(md)
{
}

VodLscp_jump_response::VodLscp_jump_response()
{
    NPT = 0;
    scaleNum = 1;
    scaleDenom = 1;
    mode = 0;
}

VodLscp_jump_response::~VodLscp_jump_response()
{
}

VodLscp_jump_response::VodLscp_jump_response(i32 npt, i16 num, ui16 den,
        ui8 md, ui8 v, ui8 tr, ui8 op, ui8 stat, ui32 strm) :
    VodLscp_Base(v, tr, op, stat, strm), NPT(npt), scaleNum(num), scaleDenom(den), mode(md)
{
}

VodLscp_play_response::VodLscp_play_response()
{
    NPT = 0;
    scaleNum = 1;
    scaleDenom = 1;
    mode = 0;
}

VodLscp_play_response::~VodLscp_play_response()
{
}

VodLscp_play_response::VodLscp_play_response(i32 npt, i16 num, ui16 den,
        ui8 md, ui8 v, ui8 tr, ui8 op, ui8 stat, ui32 strm) :
    VodLscp_Base(v, tr, op, stat, strm), NPT(npt), scaleNum(num), scaleDenom(den), mode(md)
{
}

//###################################################################################


VodLscp_Base* VodLscp_Base::GetMessageTypeObject(ui8 * data, ui32 length)
{
    ui8 byte1, byte2, byte3;
    ui8 *start;
    VodLscp_Base* lscpBase = NULL;

    if (length < LSC_HEADER_SIZE)
    {
        // message to too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE,
             "%s: Message not long enough to contain LSCP header!",
             __FUNCTION__);
    }
    else
    {
        // remember the start of the message
        start = data;

        // version
        start = Utils::Get1Byte(start, &byte1);

        // transaction-id
        start = Utils::Get1Byte(start, &byte2);

        // message ID or opcode
        start = Utils::Get1Byte(start, &byte3);

        dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS, "%s: Message ID = 0x%04X: opcode = %p !",
             __FUNCTION__, byte2, start);

        switch (byte3)
        {
        case LSC_PAUSE:
            lscpBase = new VodLscp_pause();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Received LSC_PAUSE message");
            break;
        case LSC_RESUME:
            lscpBase = new VodLscp_resume();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Received LSC_RESUME message");
            break;
        case LSC_STATUS:
            lscpBase = new VodLscp_status();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Received LSC_STATUS message");
            break;
        case LSC_RESET:
            lscpBase = new VodLscp_reset();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Received LSC_RESET message");
            break;
        case LSC_JUMP:
            lscpBase = new VodLscp_jump();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Received LSC_JUMP message");
            break;
        case LSC_PLAY:
            lscpBase = new VodLscp_play();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Received LSC_PLAY message");
            break;
        case LSC_DONE:
            lscpBase = new VodLscp_done_event();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Received LSC_DONE message");
            break;
        case LSC_PAUSE_REPLY:
            lscpBase = new VodLscp_pause_response();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Received LSC_PAUSE_REPLY message");
            break;
        case LSC_RESUME_REPLY:
            lscpBase = new VodLscp_resume_response();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Received LSC_RESUME_REPLY message");
            break;
        case LSC_STATUS_REPLY:
            lscpBase = new VodLscp_status_response();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Received LSC_STATUS_REPLY message");
            break;
        case LSC_RESET_REPLY:
            lscpBase = new VodLscp_reset_response();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Received LSC_RESET_REPLY message");
            break;
        case LSC_JUMP_REPLY:
            lscpBase = new VodLscp_jump_response();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Received LSC_JUMP_REPLY message");
            break;
        case LSC_PLAY_REPLY:
            lscpBase = new VodLscp_play_response();
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Received LSC_PLAY_REPLY message");
            break;
        default:
            dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_EVENT, "Unknown message type");
            break;
        }//end switch
    }//end else

    return lscpBase;
}

//###################################################################################

status VodLscp_Base::ParseLscpMessageHdr(ui8 * data, ui32 length,
        ui8 ** newData, ui32 *newLength)
{
    ui8 *start;
    status result = E_FALSE;
    if (length < LSC_HEADER_SIZE)
    {
        // message too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE,
             "%s: Message not long enough to contain LSCP header!",
             __FUNCTION__);
    }
    else
    {
        // remember the start of the message
        start = data;

        // protocol version
        data = Utils::Get1Byte(data, &version);

        // transaction-id
        data = Utils::Get1Byte(data, &transactionid);

        // op-code / message ID
        data = Utils::Get1Byte(data, &opcode);

        // status code
        data = Utils::Get1Byte(data, &statuscode);

        // Stream - handle
        data = Utils::Get4Byte(data, &streamhandle);

        //
        *newData = data;
        *newLength = length - (data - start);
        result = E_TRUE;

    }
    return result;
}

status VodLscp_Base::PackLscpMessageHdr(ui8 * msgData, ui32 msgDataLength,
                                        ui8 **pOut, ui32 * outLength)
{

    //fetch all the value here and pass it to below function for packing.
    status result = CreateLscpMessageHeader(msgData, msgDataLength, pOut,
                                            outLength);
    return result;
}

status VodLscp_Base::CreateLscpMessageHeader(ui8 * msgData, ui32 msgDataLength,
        ui8 ** lscpMsg, ui32 * lscpMsgLength)
{
    status result = E_FALSE;
    ui32 totalSize = LSC_HEADER_SIZE + msgDataLength;
    ui8 * buffer;

    buffer = (ui8*) malloc(totalSize);
    if (!buffer)
    {
        // ooop no memory
        *lscpMsg = NULL;
        *lscpMsgLength = 0;
    }
    else
    {
        // we have enough memory to piece together the final lscp message
        ui8 * work = buffer;

        // We add the standard message header fields

        // Version
        work = Utils::Put1Byte(work, version);

        // Transaction -id
        work = Utils::Put1Byte(work, transactionid);

        // Opcode / Message ID
        work = Utils::Put1Byte(work, opcode);

        // Statuscode
        work = Utils::Put1Byte(work, statuscode);

        // Stream Handle
        work = Utils::Put4Byte(work, streamhandle);

        // Message length (length of all data that follows this field)
        // work = Utils::Put2Byte(work, msgDataLength);


        // now the message
        if (msgData)
            work = Utils::InsertBuffer(work, msgData, msgDataLength);


        /*if ((ui32)(work-buffer) != totalSize) {
         dlog(DL_MSP_ONDEMAND,DLOGL_FUNCTION_CALLS, "Size problem creating message header!\n");
         }
         else */

        // things look good
        *lscpMsg = buffer;
        *lscpMsgLength = totalSize;
        result = E_TRUE;

        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "work pointer is %p in %s", work, __FUNCTION__);

    }

    // we always free the msgData
    if (msgData)
    {
        free(msgData);
        msgData = NULL;
    }

    return result;
}

status VodLscp_Base::ParseLscpMessageBody(ui8 * data, ui32 length)
{
    status result = E_FALSE;
    (void)data;
    (void)length;
    return result;
}

status VodLscp_Base::PackLscpMessageBody(ui8 **pOut, ui32 *len)
{
    status result = E_FALSE;
    (void)pOut;
    (void)len;
    return result;
}
//###################################################################################

//###################################################################################

bool VodLscp_Base::SendMessage(i32 socket, ui8 *data, ui32 length)
{
    ui32 count;
    bool status = true;

    dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "%s Dump of the message:", __FUNCTION__);
    for (ui32 i = 0; i < length; i++)
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "%2x ", data[i]);

    if (socket <= 0)
    {
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "Invalid socket\n");
        status = false;
    }
    else
    {
        dlog(DL_MSP_ONDEMAND, DLOGL_DATA_PROCESSING,
             "Want to send datalen:%d data:%s\n", length, data);
        //count = sendto(socket, data, length, 0, (struct sockaddr*)&addr, sizeof(addr));
        count = write(socket, data, length);

        if (count != length)
        {
            dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "Socket send error:");
            status = false;
        }
        free(data);
        data = NULL;
    }
    return status;
}

bool VodLscp_Base::ReadMessageFromSocket(i32 socket, ui8 **pOut, ui32 *lenOut)
{
    ui8 lscpData[20] = {0};
    i32 got = 0;
    bool status = false;
    //Reading from socket
    got = read(socket, (char *) lscpData, 20);

    if (got > 0)
    {

        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "Received  lsc:  ");
        for (i32 i = 0; i < got; i++)
            dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "%2x ", lscpData[i]);
        // Got valid data
        ui8 *p = NULL;
        p = (ui8*) malloc((got + 1));

        if (p)
        {
            memcpy(p, lscpData, got);
            // everything ok
            *pOut = p;
            *lenOut = got;

            dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "VodLscp_Base::ReadMessageFromSocket p: %p got: %d ", p, got);
            status = true;
        }
    }
    else
    {
        *pOut = NULL;
        *lenOut = 0;

    }
    return status;
}

i32 VodLscp_Base::GetSocket(string ip, ui16 portno)
{
    i32 sockfd = -1;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    //assuming recv and send port number the same, for testing.
    if (portno == 0)
    {
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE,
             "VodLscp_Base::GetSocket() port number is 0");
    }
    else
    {
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE,
             "VodLscp_Base::GetSocket() port number is :%d", portno);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS,
             "VodLscp_Base::GetSocket() Error in socket creation");
        return -1;
    }

    dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_DEBUG, "%s:%s:%d ZZOpen fd %d", __FILE__, __FUNCTION__, __LINE__, sockfd);

    server = gethostbyname(ip.c_str());
    if (server == NULL)
    {
        dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS,
             "VodLscp_Base::GetSocket() No such server host known");
        Close(sockfd);
        return -1;
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *) server->h_addr, (char *) &serv_addr.sin_addr.s_addr,
          server->h_length);
    serv_addr.sin_port = htons(portno);

    if (connect(sockfd, (const sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
    {
        dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS,
             "VodLscp_Base::GetSocket() Error connecting to the server");
        Close(sockfd);
        return -1;
    }

    return sockfd;
}

//###################################################################################

status VodLscp_pause::PackLscpMessageBody(ui8 **pOut, ui32 *len)
{
    status result = E_FALSE;
    ui8 *pBuf = NULL;
    ui8 *pTemp = NULL;
    ui32 bufLen;

    bufLen = 4;
    //dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "SdbSess[%s]: bufLen is %u\n", __FUNCTION__,
    //	bufLen);
    if (bufLen)
    {
        pBuf = (ui8 *) calloc(bufLen, 1);
        if (pBuf)
        {
            pTemp = pBuf;
            pTemp = Utils::Put4Byte(pTemp, stopNPT);

        }
        else
        {
            dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "%s: OUT OF MEMORY !", __FUNCTION__);
        }
    }
    ui32 msgLen = pTemp - pBuf;
    ui8 *msg = pBuf;
    PackLscpMessageHdr(msg, msgLen, pOut, len);

    return result;
}

//###################################################################################

status VodLscp_resume::PackLscpMessageBody(ui8 **pOut, ui32 *len)
{
    status result = E_FALSE;
    ui8 *pBuf = NULL;
    ui8 *pTemp = NULL;
    ui32 bufLen;

    bufLen = 8;
    //dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "SdbSess[%s]: bufLen is %u\n", __FUNCTION__,
    //		bufLen);
    if (bufLen)
    {
        pBuf = (ui8 *) calloc(bufLen, 1);
        if (pBuf)
        {
            pTemp = pBuf;
            pTemp = Utils::Put4Byte(pTemp, startNPT);
            pTemp = Utils::Put2Byte(pTemp, scaleNum);
            pTemp = Utils::Put2Byte(pTemp, scaleDenom);
        }
        else
        {
            dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "%s: OUT OF MEMORY !", __FUNCTION__);
        }
    }
    ui32 msgLen = pTemp - pBuf;
    ui8 *msg = pBuf;

    PackLscpMessageHdr(msg, msgLen, pOut, len);

    return result;
}

//###################################################################################

status VodLscp_status::PackLscpMessageBody(ui8 **pOut, ui32 *len)
{
    status result = E_FALSE;
    PackLscpMessageHdr(NULL, 0, pOut, len);

    return result;
}

//###################################################################################

status VodLscp_reset::PackLscpMessageBody(ui8 **pOut, ui32 *len)
{
    status result = E_FALSE;
    PackLscpMessageHdr(NULL, 0, pOut, len);

    return result;
}

//###################################################################################

status VodLscp_jump::PackLscpMessageBody(ui8 **pOut, ui32 *len)
{
    status result = E_FALSE;
    ui8 *pBuf = NULL;
    ui8 *pTemp = NULL;
    ui32 bufLen;

    bufLen = 12;
    //dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "SdbSess[%s]: bufLen is %u\n", __FUNCTION__,
    //	bufLen);
    if (bufLen)
    {
        pBuf = (ui8 *) calloc(bufLen, 1);
        if (pBuf)
        {
            pTemp = pBuf;
            pTemp = Utils::Put4Byte(pTemp, startNPT);
            pTemp = Utils::Put4Byte(pTemp, stopNPT);
            pTemp = Utils::Put2Byte(pTemp, scaleNum);
            pTemp = Utils::Put2Byte(pTemp, scaleDenom);
        }
        else
        {
            dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "%s: OUT OF MEMORY !", __FUNCTION__);
        }
    }
    ui32 msgLen = pTemp - pBuf;
    ui8 *msg = pBuf;
    PackLscpMessageHdr(msg, msgLen, pOut, len);

    return result;
}

//###################################################################################

status VodLscp_play::PackLscpMessageBody(ui8 **pOut, ui32 *len)
{
    status result = E_FALSE;
    ui8 *pBuf = NULL;
    ui8 *pTemp = NULL;
    ui32 bufLen;

    bufLen = 12;
    //		dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "SdbSess[%s]: bufLen is %u\n", __FUNCTION__,
    //				bufLen);
    if (bufLen)
    {
        pBuf = (ui8 *) calloc(bufLen, 1);
        if (pBuf)
        {
            pTemp = pBuf;
            pTemp = Utils::Put4Byte(pTemp, startNPT);
            pTemp = Utils::Put4Byte(pTemp, stopNPT);
            pTemp = Utils::Put2Byte(pTemp, scaleNum);
            pTemp = Utils::Put2Byte(pTemp, scaleDenom);
        }
        else
        {
            dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "%s: OUT OF MEMORY !", __FUNCTION__);
        }
    }
    ui32 msgLen = pTemp - pBuf;
    ui8 *msg = pBuf;
    PackLscpMessageHdr(msg, msgLen, pOut, len);

    return result;
}
//###################################################################################
//###################################################################################

status VodLscp_pause_response::ParseLscpMessageBody(ui8 * data, ui32 length)
{

    status result = E_FALSE;
    ui8 *newPointer;
    ui32 newLen;
    if (length < LSC_HEADER_SIZE)
    {
        // message too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS,
             "%s: Message not long enough to contain LSCP header!",
             __FUNCTION__);
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "play parse body bad ");

    }
    else
    {


        result = ParseLscpMessageHdr(data, length, &newPointer, &newLen);

        // NPT
        newPointer = Utils::Get4Byte(newPointer, (ui32*) &NPT);

        // transaction-id
        newPointer = Utils::Get2Byte(newPointer, (ui16*) &scaleNum);

        // op-code / message ID
        newPointer = Utils::Get2Byte(newPointer, (ui16*) &scaleDenom);

        // status code
        newPointer = Utils::Get1Byte(newPointer, (ui8*) &mode);

        result = E_TRUE;

        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "%s newPointer is %p", __FUNCTION__, newPointer);
    }

    return result;
}

status VodLscp_resume_response::ParseLscpMessageBody(ui8 * data, ui32 length)
{
    status result = E_FALSE;
    ui8 *newPointer;
    ui32 newLen;
    if (length < LSC_HEADER_SIZE)
    {
        // message too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS,
             "%s: Message not long enough to contain LSCP header!",
             __FUNCTION__);
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "play parse body bad");

    }
    else
    {


        result = ParseLscpMessageHdr(data, length, &newPointer, &newLen);


        // NPT
        newPointer = Utils::Get4Byte(newPointer, (ui32*) &NPT);

        // transaction-id
        newPointer = Utils::Get2Byte(newPointer, (ui16*) &scaleNum);

        // op-code / message ID
        newPointer = Utils::Get2Byte(newPointer, (ui16*) &scaleDenom);

        // status code
        newPointer = Utils::Get1Byte(newPointer, (ui8*) &mode);

        result = E_TRUE;

        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "%s newPointer is %p", __FUNCTION__, newPointer);
    }

    return result;

}
status VodLscp_status_response::ParseLscpMessageBody(ui8 * data, ui32 length)
{
    status result = E_FALSE;
    ui8 *newPointer;
    ui32 newLen;
    if (length < LSC_HEADER_SIZE)
    {
        // message too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS,
             "%s: Message not long enough to contain LSCP header!",
             __FUNCTION__);
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "play parse body bad ");

    }
    else
    {

        result = ParseLscpMessageHdr(data, length, &newPointer, &newLen);

        // NPT
        newPointer = Utils::Get4Byte(newPointer, (ui32*) &NPT);

        // transaction-id
        newPointer = Utils::Get2Byte(newPointer, (ui16*) &scaleNum);

        // op-code / message ID
        newPointer = Utils::Get2Byte(newPointer, (ui16*) &scaleDenom);

        // status code
        newPointer = Utils::Get1Byte(newPointer, (ui8*) &mode);

        result = E_TRUE;

        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "%s newPointer is %p", __FUNCTION__, newPointer);
    }

    return result;

}
status VodLscp_reset_response::ParseLscpMessageBody(ui8 * data, ui32 length)
{
    status result = E_FALSE;
    ui8 *newPointer;
    ui32 newLen;
    if (length < LSC_HEADER_SIZE)
    {
        // message too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE,
             "%s: Message not long enough to contain LSCP header!",
             __FUNCTION__);
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "play parse body bad ");

    }
    else
    {

        result = ParseLscpMessageHdr(data, length, &newPointer, &newLen);

        // NPT
        newPointer = Utils::Get4Byte(newPointer, (ui32*) &NPT);

        // transaction-id
        newPointer = Utils::Get2Byte(newPointer, (ui16*) &scaleNum);

        // op-code / message ID
        newPointer = Utils::Get2Byte(newPointer, (ui16*) &scaleDenom);

        // status code
        newPointer = Utils::Get1Byte(newPointer, (ui8*) &mode);

        result = E_TRUE;

        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "%s newPointer is %p", __FUNCTION__, newPointer);
    }

    return result;
}
status VodLscp_jump_response::ParseLscpMessageBody(ui8 * data, ui32 length)
{
    status result = E_FALSE;
    ui8 *newPointer;
    ui32 newLen;
    if (length < LSC_MSG_SIZE)
    {
        // message too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS,
             "%s: Message not long enough to contain LSCP header!",
             __FUNCTION__);
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "play parse body bad ");

    }
    else
    {
        result = ParseLscpMessageHdr(data, length, &newPointer, &newLen);

        // NPT
        newPointer = Utils::Get4Byte(newPointer, (ui32*) &NPT);

        // transaction-id
        newPointer = Utils::Get2Byte(newPointer, (ui16*) &scaleNum);

        // op-code / message ID
        newPointer = Utils::Get2Byte(newPointer, (ui16*) &scaleDenom);

        // status code
        newPointer = Utils::Get1Byte(newPointer, (ui8*) &mode);

        result = E_TRUE;

        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "%s newPointer is %p", __FUNCTION__, newPointer);

    }

    return result;
}
status VodLscp_play_response::ParseLscpMessageBody(ui8 * data, ui32 length)
{

    status result = E_FALSE;
    ui8 *newPointer;
    ui32 newLen;
    if (length < LSC_HEADER_SIZE)
    {
        // message too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS,
             "%s: Message not long enough to contain LSCP header!",
             __FUNCTION__);
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "play parse body bad ");

    }
    else
    {
        result = ParseLscpMessageHdr(data, length, &newPointer, &newLen);

        // NPT
        newPointer = Utils::Get4Byte(newPointer, (ui32*) &NPT);

        // transaction-id
        newPointer = Utils::Get2Byte(newPointer, (ui16*) &scaleNum);

        // op-code / message ID
        newPointer = Utils::Get2Byte(newPointer, (ui16*) &scaleDenom);

        // status code
        newPointer = Utils::Get1Byte(newPointer, (ui8*) &mode);

        result = E_TRUE;

        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "%s newPointer is %p", __FUNCTION__, newPointer);
    }

    return result;

}

status VodLscp_done_event::ParseLscpMessageBody(ui8 * data, ui32 length)
{
    status result = E_FALSE;
    ui8 *newPointer;
    ui32 newLen;
    if (length < LSC_HEADER_SIZE)
    {
        // message too small!  It must be bad!
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE,
             "%s: Message not long enough to contain LSCP header!",
             __FUNCTION__);
        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "play parse body bad ");

    }
    else
    {
        result = ParseLscpMessageHdr(data, length, &newPointer, &newLen);

        // NPT
        newPointer = Utils::Get4Byte(newPointer, (ui32*) &NPT);

        // transaction-id
        newPointer = Utils::Get2Byte(newPointer, (ui16*) &scaleNum);

        // op-code / message ID
        newPointer = Utils::Get2Byte(newPointer, (ui16*) &scaleDenom);

        // status code
        newPointer = Utils::Get1Byte(newPointer, (ui8*) &mode);

        result = E_TRUE;

        dlog(DL_MSP_ONDEMAND, DLOGL_NOISE, "%s newPointer is %p", __FUNCTION__, newPointer);
    }

    return result;

}
//###################################################################################
