/** @file VOD_StreamControl.cpp
 *
 * @brief VOD StreamControl abstract class implementation.
 *
 * @author Manu Mishra
 *
 * @version 1.0
 *
 * @date 01.10.2011
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#include <time.h>
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <cstdio>
#include <stdlib.h>
using namespace std;

#include <assert.h>

//#include "conflictapi.h"
#include "VOD_StreamControl.h"
#include "vod.h"
#include "dlog.h"
#define UNUSED_PARAM(a) (void)a;
//#include "../../csplib/include/conflict_defs.h"
#define Close(x) do{dlog(DL_MSP_ONDEMAND, DLOGL_MINOR_DEBUG,"%s:%s:%d ZZClose=%d",__FILE__,__FUNCTION__,__LINE__, x); close(x); (x)=-1;}while(0)


VOD_StreamControl::VOD_StreamControl(OnDemand* pOnDemand, eOnDemandReqType reqType)
{
    this->reqType = reqType;
    this->ptrOnDemand = pOnDemand;

    // numerator, denominator, nptPosition are use to report back speed and position
    // and therefore can be cleared at init time

    numerator   = 1;   // default to normal play
    denominator = 1;   //       "
    nptPosition = 0;   // default to beginning
    socketFd = -1;
    rettransid = 0;
    retstatus = 0;
    server_mode = -1;
    streamControlPort = 0;
    isStreamParametersSet = false;
    streamStarted = false;
}

VOD_StreamControl::~VOD_StreamControl()
{
    dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS, "%s:%i  ~VOD_StreamControl.\n", __FUNCTION__, __LINE__);

}

int32_t VOD_StreamControl::GetFD()
{
    return socketFd;

}

int16_t VOD_StreamControl::GetNumerator()
{
    return numerator;

}

uint16_t VOD_StreamControl::GetDenominator()
{
    return denominator;

}

float VOD_StreamControl::GetPos()
{
    return nptPosition;

}
uint8_t VOD_StreamControl::GetReturnStatus()
{
    return retstatus;

}

uint8_t VOD_StreamControl::GetReturnTransId()
{
    return rettransid;

}

uint8_t VOD_StreamControl::GetServerMode()
{
    return server_mode;

}

void VOD_StreamControl::CloseControlSocket()
{
    dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS, "%s:%i  CloseControlSocket() Unsupported.\n", __FUNCTION__, __LINE__);
}

eIOnDemandStatus VOD_StreamControl::ResetControlConnection()
{
    dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS, "%s:%i  ResetControlConnection() Unsupported.\n", __FUNCTION__, __LINE__);
    return ON_DEMAND_ERROR; //connectManager->ResetControlConnection();
}

eIOnDemandStatus VOD_StreamControl::ConnectConfirmation()
{
    dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS, "%s:%i  ConnectConfirmation() Unsupported.\n", __FUNCTION__, __LINE__);
    return ON_DEMAND_ERROR; //connectManager->ConnectConfirmation();
}


eIOnDemandStatus VOD_StreamControl::StreamTearDown()
{
    eIOnDemandStatus status = ON_DEMAND_OK;
    //Note for stream control session, you "do not" send the TEAR DOWN msg to the server!!!!!!!!
    dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "%s:%i Closing the socket[%d]\n", __FUNCTION__, __LINE__, socketFd);

    Close(socketFd);
    socketFd = -1;

    return status;
}

eIOnDemandStatus VOD_StreamControl::StreamOpen(const char* destUrl, const MultiMediaEvent **mme)
{
    UNUSED_PARAM(destUrl)
    UNUSED_PARAM(mme)
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS, "%s:%i  StreamOpen() Unsupported.\n", __FUNCTION__, __LINE__);
    return status;
}

eIOnDemandStatus VOD_StreamControl::StreamClose()
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS, "%s:%i  StreamClose() Unsupported.\n", __FUNCTION__, __LINE__);
    return status;

}


eConnectLinkState VOD_StreamControl::GetConnectionState()
{
    dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS, "%s:%i  GetConnectionState() Unsupported.\n", __FUNCTION__, __LINE__);
    return eConnectLinkDown ; //connectManager->GetConnectionState();
}

eIOnDemandStatus VOD_StreamControl::SendPendingMessage()
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS, "%s:%i  SendPendingMessage() Unsupported.\n", __FUNCTION__, __LINE__);
    return status;
}
eIOnDemandStatus VOD_StreamControl::StreamStop()
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS, "%s:%i  StreamStop() Unsupported.\n", __FUNCTION__, __LINE__);
    return status;
}



eIOnDemandStatus VOD_StreamControl::ProcessTimeOutResponseRTSP(unsigned int sequenceNum)
{
    UNUSED_PARAM(sequenceNum)
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    dlog(DL_MSP_ONDEMAND, DLOGL_FUNCTION_CALLS, "%s:%i  StreamStop() ProcessTimeOutResponseRTSP.\n", __FUNCTION__, __LINE__);
    return status;
}



