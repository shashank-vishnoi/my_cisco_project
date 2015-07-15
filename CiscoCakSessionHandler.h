/**
  \file CiscoCakSessionHandler.h
  \class CiscoCakSessionHandler
*/

#if !defined(CISCOCAKSESSIONHANDLER_H)
#define CISCOCAKSESSIONHANDLER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "MspCommon.h"
#include "csci-cam-common-api.h"
#include "csci-ciscocak-vod-api.h"

#define EID_SIZE           	4
#define SESSIONID_SIZE      	4
#define CISCO_CAK_KEY_SIZE  301   //Fixed length
#define EID_OFFSET_IN_EMM   	6

/////////////////////////////////////////////////////////////////
//// This is a common class which will be used by Arris/Seachange
//// VOD and URI based tuning channels e.g AVN.
/////////////////////////////////////////////////////////////////

class  CiscoCakSessionHandler
{
private:

    uint8_t  mEmm[CISCO_CAK_KEY_SIZE];          //EMM key as passed from source URI
    uint8_t mEid[EID_SIZE];                //eid value extracted from EMM key
    uint8_t mSessionId[SESSIONID_SIZE];    //SessionId

public:

    CiscoCakSessionHandler(const uint8_t emmPowerKey[CISCO_CAK_KEY_SIZE]);
    ~CiscoCakSessionHandler();
    eMspStatus CiscoCak_SessionInitialize(void) ;
    eMspStatus CiscoCak_SessionFinalize(void);
    uint32_t GetSessionId(void);  //Generate unique SessionId. It starts with 1
    //and get increamented by 1 whenever new session is created.

    uint32_t GetEid(void);        //parse emm string passed in URI and get eid
};


#endif
