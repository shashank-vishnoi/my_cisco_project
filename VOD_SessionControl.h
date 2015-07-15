/**
\addtogroup subsystem_top
@{
 \file VOD_SessionControl.h

 \brief VOD SessionControl abstract class header.

 \n Original author: Amit Patel

 \n Copyright 2010 Cisco Systems, Inc.
 \defgroup OnDemandSession VOD_SessionControl
@}*/
/**
\addtogroup OnDemandSession
@{
   This section describe interface for VOD session controller.
@}
*/
#ifndef _VOD_SESSIONCONTROL_H
#define _VOD_SESSIONCONTROL_H

// *INDENT-ON*
#include <time.h>
#include <string>
#include <list>
#include "ondemand.h"
#include "vod.h"
#include "vodUtils.h"
#include "dsmccProtocol.h"

using namespace std;

#define EID_SIZE 4 //Entitlement ID size
#define TRANSID_SIZE 4

/**
   This class used to store information related to send message.
 */
class SendMsgInfo
{
public:
    uint32_t       transId;
    uint32_t       msgType;
    uint8_t        *msgData;
    uint32_t       msgLen;
    EventTimer *evtTimer;
    VOD_SessionControl *objPtr;
    OnDemand       *ptrOnDemand;
    int            sessionId;
    uint8_t        msgRetryCount;
};

typedef std::map<unsigned int, OnDemand *>   ActiveVodSessionMap;

/**********************************************************************
*
* Forward declarations
*
**********************************************************************/
/**
   This class provide the interface for VOD session control. This interface can be implemented
   for different platform.
 */
class VOD_SessionControl
{
public:
    /**
    * \param vodSession Pointer to onDemandSession.
    * \param reqType  enum to OnDemandReqType.
    * \brief constructor function for VOD_SessionControl.
    */
    VOD_SessionControl(OnDemand *pOnDemand, eOnDemandReqType reqType);

    /**
    * \brief destructor function for VOD_SessionControl.
    */
    virtual ~VOD_SessionControl();

    /**
    * \param UseUrl URL to set-up VOD session.
    * \return status of the SessionSetup operation.
    * \brief First set-up a connection with network and send SessionSetup request message to network.
    */
    virtual eIMediaPlayerStatus SessionSetup(const std::string& UseUrl) = 0;

    /**
    * \return status of the SessionTeardown operation.
    * \brief Send a SessionTeardown request message to network.
    */
    virtual eIOnDemandStatus SessionTeardown() = 0;

    /**
    * \return status of SendResetRequest request.
    * \brief This function is used reset the connection between client and network.
    */
    virtual eIOnDemandStatus SendResetRequest() = 0;
    /**
    * \return status of GetConnectionStatus request.
    * \brief This function is used to get status of active session for respective client.
    */
    virtual eIOnDemandStatus GetConnectionStatus() = 0;

    /**
    * \return status of SendKeepAlive request.
    * \brief This function is used to send SendKeepAlive messages to the network.
    */
    virtual eIOnDemandStatus SendKeepAlive() = 0;

    /**
    * \brief This function is used to display debug information for VOD_SessionControl.
    */
    virtual void DisplayDebugInfo() = 0;

    /**
    * \brief This function is used to close the socket connection when VOD session is tear down.
    */
    virtual void CloseControlSocket() = 0;

    /**
    */
    virtual void HandleSessionResponse(void *pMsg) = 0;

    /**
     * \param ptrMessage received message from the network.
     * \return status of the message received from the network.
     * \brief Handles all the response received from the network for VOD.
     */
    virtual eIOnDemandStatus HandleInput(void  *ptrMessage) = 0;

    /**
    * \return status of SendPendingMessage request.
    * \brief This function is used send pending message to network.
    */
    virtual eIOnDemandStatus SendPendingMessage(SendMsgInfo *msfInfo) = 0;

    /**
    * \return value of OnDemandReqType.
    * \brief This function is used to retrieve value of OnDemandReqType.
    */
    eOnDemandReqType GetOnDemandReqType()
    {
        return reqType;
    }

    /**
    * \return URL for stream control.
    * \brief This function is used to get url for Stream Control.
    */
    string GetStreamControlUrl();

    /**
    * \return time interval for KeepAlive message.
    * \brief This function is used to retrive value of OnDemandReqType.
    */
    uint32_t GetKeepAliveTimeOut();

    /**
    * \return file descriptor for socket connection
    * \brief This function return file descriptor for session socket connection.
    */
    static int32_t GetFD();

    static unsigned int  getSessionNumber(void  *pMsg);


    /**
    * \return status of AddMessageToSendList request.
    * \param msfInfo  object to SendMegInfo structure which needs to be added.
    * \brief This function add send message to send message queue.
    */
    eIOnDemandStatus AddMessageToSendList(SendMsgInfo *msfInfo);

    /**
    * \param msfInfo  object to SendMegInfo structure which needs to be removed.
    * \brief This function remove send message to send message queue.
    */
    void RemoveMessageFromSendList(SendMsgInfo *msgInfo);

    /**
    * \return status of ProcessTimeoutCallBack request.
    * \brief This function process time-out for response message.
    */
    static void ProcessTimeoutCallBack(int32_t fd, short event, void *arg);

    static void  ReadSessionSocketCallback(int32_t fd, int16_t event, void *arg);

    /**
    * \return Stream control server ipaddress which is used by stream controller for tcp connection.
    */
    virtual uint8_t * GetStreamServerIPAddress()
    {
        return StreamServerIPAdd;
    } ;
    /**
    * \return Stream control server port which is used by stream controller for tcp connection.
    */
    virtual uint16_t GetStreamServerPort()
    {
        return StreamServerPort;
    } ;
    /**
    * \return Stream Handle which is used by stream controller.
    */
    virtual uint32_t GetStreamHandle()
    {
        return StreamHandle;
    } ;
    /**
    * \return frequency for vod stream.
    */
    uint32_t GetFrequency()
    {
        return mFrequency;
    } ;
    /**
    * \return modulation type for vod stream.
    */
    tCpeSrcRFMode GetModulationType()
    {
        return mMode;
    } ;
    /**
    * \return symbol rate for vod stream.
    */
    uint32_t GetSymbolRate()
    {
        return mSymbolRate;
    } ;
    /**
    * \return mpeg programm number for vod stream.
    */
    uint32_t GetProgramNumber()
    {
        return mProgramNumber;
    } ;

    uint32_t getEndPosition()
    {
        return endPosition;
    }

    void setNpt(float nptPosition)
    {
        mNpt = (uint32_t) nptPosition;

    }

    uint32_t GetSessionId()
    {
        return mTransSessionId;
    }

    uint8_t* GetEntitlementId()
    {
        return caEID;
    } ;

    uint32_t GetCakResp()
    {
        return mCakresp;
    } ;

    void SetCakResp(uint32_t CakStat)
    {
        mCakresp = CakStat;
    } ;

    uint32_t GetEmmCount()
    {
        return emmCount;
    } ;

    bool GetEncryptionflag()
    {
        return mEncrypted;
    } ;

    /**
    * \param pointer to VodDsmcc_ClientSessionConfirm message type
    * \brief handle function for dsmcc_ClientSessionSetUpConfirm message type
    */
    tCpeSrcRFTune GetTuningParameters()
    {
        tCpeSrcRFTune tuningParams;

        tuningParams.frequencyHz = mFrequency;
        tuningParams.mode = mMode;

        tuningParams.symbolRate =  mSymbolRate / 1000;   // mSymbolRate is Hz, need to return kHz

        return tuningParams;
    }

protected:
    /**
    * \return pointer to IP address string.
    * \param ipadd  IP address in hex format.
    * \brief This function converts IP address from hex to string format.
    */
    uint8_t * IPAddressIntToStr(uint32_t ipadd);

    /**
    * pointer to OnDemandSession.
    */
    OnDemand *ptrOnDemand;
    /**
    * URL for session control.
    */
    std::string sessionControlURL;

    /**
    * URL for stream control.
    */
    std::string streamControlURL;

    /**
    * kepp alive time for session management.
    */
    uint32_t keepAliveTimeOut;

    /**
    * MAC id of STB.
    */
    uint8_t stbMacAddr[MAC_ID_SIZE];

    /**
    * SRM ip address.
    */
    uint32_t srmIpAddr;

    /**
    * vod server ip address.
    */
    uint32_t serverIpAddr;

    /**
    * SRM port id.
    */
    uint16_t srmPort;

    //for stream handling
    uint8_t *StreamServerIPAdd;
    uint16_t StreamServerPort;
    uint32_t StreamHandle;
    uint32_t mFrequency;
    tCpeSrcRFMode mMode;
    uint32_t mSymbolRate;
    uint32_t mSourceId;
    uint32_t mProgramNumber;
    uint32_t endPosition;

    uint32_t mNpt;
    int mVodSessionId;
    uint32_t mTransSessionId;
    uint8_t caEID[EID_SIZE]; //Entitlement ID
    uint8_t mTransId[TRANSID_SIZE]; // transID used as session ID for cable card VOD call
    bool mEncrypted;  // VOD asset encryption flag
    uint32_t emmCount;  // current EMM count from CAK
    uint32_t mCakresp; // response from Cak (STAT field in diag page)

    static int32_t mSocketFd;

private:
    /**
    * OnDemand Request type.
    */
    eOnDemandReqType reqType;

    list<SendMsgInfo *> sendMsgInfoList;

    static ActiveVodSessionMap     mActiveVodSessionMap;
    static pthread_mutex_t  mVodSessionListMutex;
    static bool             mVodSessionListInitialized;
    static int              mPrevVodSessionId;

};

#endif
