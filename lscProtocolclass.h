#ifndef _LSCP_PROTOCOL_H_
#define _LSCP_PROTOCOL_H_

#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <vector>
//#include <list>
#include <cstring>
#include "vodUtils.h"
using namespace std;


//LSC Protocol Verison
#define LSC_PROTOCOL_VERSION       0x1
#define LSC_STATUS_CODE            0x0


typedef enum
{
    O_MODE,
    P_MODE,
    ST_MODE,
    T_MODE,
    TP_MODE,
    STP_MODE,
    PST_MODE,
    MODE_EOS
} Server_modes;

typedef enum
{
    LSC_OK = 0x0,
    LSC_BAD_REQUEST = 0x10,
    LSC_BAD_STREAM,
    LSC_WRONG_STATE,
    LSC_UNKNOWN,
    LSC_NO_PERMISSION,
    LSC_BAD_PARAM,
    LSC_NO_IMPLEMENT,
    LSC_NO_MEMORY,
    LSC_IMP_LIMIT,
    LSC_TRANSIENT,
    LSC_NO_RESOURCES = 0x26,
    LSC_SERVER_ERROR = 0x20,
    LSC_SERVER_FAILURE,
    LSC_BAD_SCALE = 0x30,
    LSC_BAD_START,
    LSC_BAD_STOP,
    LSC_MPEG_DELIVERY = 0x40

} LSC_Response_status_code;




// The message IDs

#define LSC_PAUSE					0x01
#define LSC_RESUME					0x02
#define LSC_STATUS					0x03
#define LSC_RESET					0x04
#define LSC_JUMP					0x05
#define LSC_PLAY					0x06
#define LSC_DONE					0x40
#define LSC_PAUSE_REPLY				0x81
#define LSC_RESUME_REPLY			0x82
#define LSC_STATUS_REPLY			0x83
#define LSC_RESET_REPLY				0x84
#define LSC_JUMP_REPLY				0x85
#define LSC_PLAY_REPLY				0x86

//NPT VALUES

#define NPT_CURRENT               0x80000000
#define NPT_START                 0x00000000
#define NPT_END              	  0x7FFFFFFF

//LSC Header size
#define LSC_HEADER_SIZE	8
#define LSCP_MSG_BODY_SIZE 12
#define LSC_MSG_SIZE      20

//#define LSCP_SESSMSG_TYPE		0x?
//#define LSCP_SESSIONID_LEN		0x?
//#define LSCP_CLIENTID_LEN 		0x?
//#define LSCP_SERVERID_LEN 		0x?


#define SIZE_3   3
#define SIZE_4   4
#define SIZE_6   6
#define SIZE_8   8
#define SIZE_10 10

#define  ui32 unsigned int
#define  i32  int
#define  ui16 unsigned short
#define  i16  short
#define  ui8  unsigned char
#define  i8   char

class VodLscp_Base
{

public:
//////Add constructor destructor code here
    VodLscp_Base();
    virtual ~VodLscp_Base();
    VodLscp_Base(ui8 v);
    VodLscp_Base(ui8 v, ui8 op);
    VodLscp_Base(ui8 v, ui8 tr, ui8 op, ui8 stat, ui32 strm);
    bool SendMessage(i32 socket, ui8 * data, ui32 length);
    bool ReadMessageFromSocket(i32 socket, ui8 **pOut, ui32 * lenOut);

    VodLscp_Base* GetMessageTypeObject(ui8 * data, ui32 length);
    status ParseLscpMessageHdr(ui8 * data, ui32 length, ui8 ** newData,
                               ui32 *newLength);
    virtual status ParseLscpMessageBody(ui8 * data, ui32 length);
    status PackLscpMessageHdr(ui8 * msgData, ui32 msgDataLength, ui8 **pOut,
                              ui32 * outLength);
    virtual status PackLscpMessageBody(ui8 **pOut, ui32 *len);

    int GetSocket(string ip, ui16 port);

//Setter functions

    ui8 GetMessageId()
    {
        return opcode;
    }
    static ui32 getTransId();

    ui8 GetReturnTransId()
    {
        return transactionid;
    }

    virtual i32 GetNPT()
    {
        return 0;
    }
    virtual i16 GetNum()
    {
        return 0;
    };
    virtual ui16 GetDen()
    {
        return 0;
    }
    ;
    virtual ui8 GetMode()
    {
        return 0;
    }
    ;
    ui8 GetReturnStatusCode()
    {
        return statuscode;
    }
    ;
private:

    int socketFD;

//HDR
    ui8 version;
    ui8 transactionid;
    ui8 opcode;
    ui8 statuscode;
    ui32 streamhandle;

    status CreateLscpMessageHeader(ui8 * msgData, ui32 msgDataLength,
                                   ui8 ** lscpMsg, ui32 * lscpMsgLength);

};

/***********************************************************************************/
class VodLscp_pause: public VodLscp_Base
{

public:

//status ParseLscpMessageBody(ui8 * data, ui32 length);
    status PackLscpMessageBody(ui8 **pOut, ui32 *len);
////get snd set method for all members
    VodLscp_pause();
    ~VodLscp_pause();
//VodLscp_pause(i32 stp);
    VodLscp_pause(int, unsigned char, unsigned char, unsigned char, unsigned char,
                  unsigned int);
//////Add constructor destructor code here
private:

    i32 stopNPT;
};
/***********************************************************************************/
class VodLscp_resume: public VodLscp_Base
{

public:
//status ParseLscpMessageBody(ui8 * data, ui32 length);
    status PackLscpMessageBody(ui8 **pOut, ui32 *len);
/////get snd set method for all members
    VodLscp_resume();
    ~VodLscp_resume();
    VodLscp_resume(i32 str, i16 num, ui16 den, ui8 v, ui8 tr, ui8 op, ui8 stat,
                   ui32 strm);
private:

    i32 startNPT;
    i16 scaleNum;
    ui16 scaleDenom;
};
/***********************************************************************************/
class VodLscp_status: public VodLscp_Base
{

public:
//status ParseLscpMessageBody(ui8 * data, ui32 length);
    status PackLscpMessageBody(ui8 **pOut, ui32 *len);
/////get snd set method for all members
    VodLscp_status();
    ~VodLscp_status();
    VodLscp_status(ui8 v, ui8 tr, ui8 op, ui8 stat, ui32 strm);

};
/***********************************************************************************/
class VodLscp_reset: public VodLscp_Base
{

public:
//status ParseLscpMessageBody(ui8 * data, ui32 length);
    status PackLscpMessageBody(ui8 **pOut, ui32 *len);
/////get snd set method for all members

    VodLscp_reset();
    ~VodLscp_reset();
    VodLscp_reset(ui8 v, ui8 tr, ui8 op, ui8 stat, ui32 strm);
};
/***********************************************************************************/
class VodLscp_jump: public VodLscp_Base
{

public:
//status ParseLscpMessageBody(ui8 * data, ui32 length);
    status PackLscpMessageBody(ui8 **pOut, ui32 *len);

    VodLscp_jump();
    ~VodLscp_jump();
    VodLscp_jump(i32 str, i32 stp, i16 num, ui16 den, ui8 v, ui8 tr, ui8 op,
                 ui8 stat, ui32 strm);
private:
    i32 startNPT;
    i32 stopNPT;
    i16 scaleNum;
    ui16 scaleDenom;
};
/***********************************************************************************/

class VodLscp_play: public VodLscp_Base
{

public:
//status ParseLscpMessageBody(ui8 * data, ui32 length);
    status PackLscpMessageBody(ui8 **pOut, ui32 *len);
/////get snd set method for all members
    VodLscp_play();
    ~VodLscp_play();
    VodLscp_play(i32 str, i32 stp, i16 num, ui16 den, ui8 v, ui8 tr, ui8 op,
                 ui8 stat, ui32 strm);
private:
    i32 startNPT;
    i32 stopNPT;
    i16 scaleNum;
    ui16 scaleDenom;
};
/***********************************************************************************/
//  Response Message
class VodLscp_pause_response: public VodLscp_Base
{

public:
    status ParseLscpMessageBody(ui8 * data, ui32 length);
//status PackLscpMessageBody(ui8 **pOut, ui32 *len);

    VodLscp_pause_response();
    ~VodLscp_pause_response();
    VodLscp_pause_response(i32 npt, i16 num, ui16 den, ui8 md, ui8 v, ui8 tr,
                           ui8 op, ui8 stat, ui32 strm);
    i32 GetNPT()
    {
        return NPT;
    }
    ;
    i16 GetNum()
    {
        return scaleNum;
    }
    ;
    ui16 GetDen()
    {
        return scaleDenom;
    }
    ;
private:
    i32 NPT;
    i16 scaleNum;
    ui16 scaleDenom;
    ui8 mode;
};
/***********************************************************************************/
class VodLscp_resume_response: public VodLscp_Base
{

public:
    status ParseLscpMessageBody(ui8 * data, ui32 length);
//status PackLscpMessageBody(ui8 **pOut, ui32 *len);

    VodLscp_resume_response();
    ~VodLscp_resume_response();
    VodLscp_resume_response(i32 npt, i16 num, ui16 den, ui8 md, ui8 v, ui8 tr,
                            ui8 op, ui8 stat, ui32 strm);
    i32 GetNPT()
    {
        return NPT;
    }
    ;
    i16 GetNum()
    {
        return scaleNum;
    }
    ;
    ui16 GetDen()
    {
        return scaleDenom;
    }
    ;
private:
    i32 NPT;
    i16 scaleNum;
    ui16 scaleDenom;
    ui8 mode;
};
/***********************************************************************************/
class VodLscp_status_response: public VodLscp_Base
{

public:
    status ParseLscpMessageBody(ui8 * data, ui32 length);
//status PackLscpMessageBody(ui8 **pOut, ui32 *len);

    VodLscp_status_response();
    ~VodLscp_status_response();
    VodLscp_status_response(i32 npt, i16 num, ui16 den, ui8 md, ui8 v, ui8 tr,
                            ui8 op, ui8 stat, ui32 strm);
    i32 GetNPT()
    {
        return NPT;
    }
    ;
    i16 GetNum()
    {
        return scaleNum;
    }
    ;
    ui16 GetDen()
    {
        return scaleDenom;
    }
    ;
private:
    i32 NPT;
    i16 scaleNum;
    ui16 scaleDenom;
    ui8 mode;
};
/***********************************************************************************/
class VodLscp_reset_response: public VodLscp_Base
{

public:
    status ParseLscpMessageBody(ui8 * data, ui32 length);
//status PackLscpMessageBody(ui8 **pOut, ui32 *len);

    VodLscp_reset_response();
    ~VodLscp_reset_response();
    VodLscp_reset_response(i32 npt, i16 num, ui16 den, ui8 md, ui8 v, ui8 tr,
                           ui8 op, ui8 stat, ui32 strm);
    i32 GetNPT()
    {
        return NPT;
    }
    ;
    i16 GetNum()
    {
        return scaleNum;
    }
    ;
    ui16 GetDen()
    {
        return scaleDenom;
    }
    ;
private:
    i32 NPT;
    i16 scaleNum;
    ui16 scaleDenom;
    ui8 mode;
};
/***********************************************************************************/
class VodLscp_jump_response: public VodLscp_Base
{

public:
    status ParseLscpMessageBody(ui8 * data, ui32 length);
//status PackLscpMessageBody(ui8 **pOut, ui32 *len);

    VodLscp_jump_response();
    ~VodLscp_jump_response();
    VodLscp_jump_response(i32 npt, i16 num, ui16 den, ui8 md, ui8 v, ui8 tr,
                          ui8 op, ui8 stat, ui32 strm);
    i32 GetNPT()
    {
        return NPT;
    }
    ;
    i16 GetNum()
    {
        return scaleNum;
    }
    ;
    ui16 GetDen()
    {
        return scaleDenom;
    }
    ;
private:
    i32 NPT;
    i16 scaleNum;
    ui16 scaleDenom;
    ui8 mode;
};
/***********************************************************************************/
class VodLscp_play_response: public VodLscp_Base
{

public:
    status ParseLscpMessageBody(ui8 * data, ui32 length);
//status PackLscpMessageBody(ui8 **pOut, ui32 *len);
    VodLscp_play_response();
    ~VodLscp_play_response();
    VodLscp_play_response(i32 npt, i16 num, ui16 den, ui8 md, ui8 v, ui8 tr,
                          ui8 op, ui8 stat, ui32 strm);
    i32 GetNPT()
    {
        return NPT;
    }
    ;
    i16 GetNum()
    {
        return scaleNum;
    }
    ;
    ui16 GetDen()
    {
        return scaleDenom;
    }
    ;
private:
    i32 NPT;
    i16 scaleNum;
    ui16 scaleDenom;
    ui8 mode;
};
/***********************************************************************************/
// Event data
class VodLscp_done_event: public VodLscp_Base
{

public:
    status ParseLscpMessageBody(ui8 * data, ui32 length);
//status PackLscpMessageBody(ui8 **pOut, ui32 *len);

    VodLscp_done_event();
    ~VodLscp_done_event();
    VodLscp_done_event(i32 npt, i16 num, ui16 den, ui8 md, ui8 v, ui8 tr, ui8 op,
                       ui8 stat, ui32 strm);
    i32 GetNPT()
    {
        return NPT;
    }
    ;
    i16 GetNum()
    {
        return scaleNum;
    }
    ;
    ui16 GetDen()
    {
        return scaleDenom;
    }
    ;
    ui8 GetMode()
    {
        return mode;
    }
    ;
private:
    i32 NPT;
    i16 scaleNum;
    ui16 scaleDenom;
    ui8 mode;
};
/***********************************************************************************/

#endif
