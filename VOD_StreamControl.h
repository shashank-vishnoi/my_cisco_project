/**
 \addtogroup subsystem_top
 @{
 \file VOD_StreamControl.h

 \brief VOD StreamControl abstract class header.

 \n Original author: Manu Mishra

 \n Copyright 2010 Cisco Systems, Inc.
 \defgroup OnDemandSession VOD_SessionControl
 @}*/
/**
 \addtogroup OnDemandSession
 @{
 This section describe interface for VOD stream controller.
 @}
 */

#ifndef _VOD_STREAMCONTROL_H
#define _VOD_STREAMCONTROL_H

#include <time.h>
#include <string>
#include "ondemand.h"

#include "vod.h"

using namespace std;

/**********************************************************************
 *
 * Forward declarations
 *
 **********************************************************************/





class VOD_StreamControl
{

protected:
    //TODO For Testing purpose. Can be removed after a while
    virtual void HandleStreamResp() = 0;

    string streamControlSessionId;
    //TODO Changed OD i/f from RTSP to LSC
    float nptPosition;
    //TODO Added OD i/f from RTSP to LSC
    int16_t numerator;
    uint16_t denominator;
    uint8_t rettransid;
    uint8_t retstatus;
    uint8_t server_mode;

    //For approximating the NPT
    bool streamStarted;

    bool Pause_Mode;


    /*
     * Pointer to OnDemand
     */
    OnDemand *ptrOnDemand; //OnDemand Controller Object


    /**
     * URL for stream control connection.
     */
    std::string streamControlURL;

    bool isStreamParametersSet;

    /**
     * Port number for stream control connection.
     */
    uint16_t streamControlPort;

    //TODO Deleted connectManager & streamPlayer required by RTSP

public:

    virtual eIOnDemandStatus StreamPlay(bool usingStartNPT) = 0;

    virtual eIOnDemandStatus StreamGetParameter() = 0;

    virtual eIOnDemandStatus SendKeepAlive() = 0;
    //TODO Changed OD i/f for RTSP
    virtual void HandleInput(void *ptrMessage) = 0;

    virtual eIOnDemandStatus
    StreamSetup(std::string url, std::string sessionId) = 0;

    virtual eIOnDemandStatus StreamOpen(const char* destUrl, const MultiMediaEvent **mme);

    virtual eIOnDemandStatus StreamPause() = 0;

    virtual eIOnDemandStatus StreamStop();

    virtual eIOnDemandStatus StreamClose();

    virtual eIOnDemandStatus StreamGetPosition(float* npt) = 0;
    /**
        * \return status of the Stream Teardown operation.
        * \brief Send a StreamTeardown request message to network.
        */

    virtual eIOnDemandStatus StreamTearDown();

    /**
       * \return status of the Stream Teardown operation.
       * \brief Send a StreamTeardown request message to network.
       */


    virtual void DisplayDebugInfo() = 0;

    int32_t GetFD();

    int16_t GetNumerator();
    uint16_t GetDenominator();
    float GetPos();
    uint8_t GetReturnStatus();
    uint8_t GetReturnTransId();
    uint8_t GetServerMode();

    void CloseControlSocket();

    eIOnDemandStatus ResetControlConnection();
    eIOnDemandStatus ConnectConfirmation();

    //TODO Changed OD i/f for RTSP
    virtual void StreamSetSpeed(int16_t num, uint16_t den) = 0;

    //TODO Changed OD i/f for RTSP
    virtual eIOnDemandStatus StreamGetSpeed(int16_t *num, uint16_t *den) = 0;

    virtual void StreamSetNPT(float npt) = 0;

    eConnectLinkState GetConnectionState();

    eIOnDemandStatus ProcessTimeOutResponseRTSP(unsigned int sequenceNum);

    eIOnDemandStatus SendPendingMessage();


    //Added Manu
    //TODO Added OD i/f for LSC
    virtual void ReadStreamSocketData() = 0;

    int32_t socketFd;
    eOnDemandReqType reqType;

public:
    //TODO Changed OD i/f from RTSP to LSC
    VOD_StreamControl(OnDemand* pOnDemand, eOnDemandReqType reqType);
    virtual ~VOD_StreamControl();

};
#endif
