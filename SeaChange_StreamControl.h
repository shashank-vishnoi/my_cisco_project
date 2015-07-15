/** @file SeaChange_StreamControl.h
 *
 * @brief SeaChange StreamControl header.
 *
 * @author Manu Mishra
 *
 * @version 1.0
 *
 * @date 01.15.2011
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#ifndef _SeaChange_STREAMCONTROL_H
#define _SeaChange_STREAMCONTROL_H

#include "VOD_StreamControl.h"

#include "vod.h"
#include "ondemand.h"
#include "lscProtocolclass.h"
#define HUNDRED_MS 100000
#define LSC_TIMEOUT 3
#define PRECISION_UPDATE 180
#define ASSET_UP_NPT 1500

class OnDemand;

class SeaChange_StreamControl : public VOD_StreamControl
{
protected:
    /*
         * Pointer to LSCP base
         */
    VodLscp_Base *lscpBaseObj;
    time_t start, end;
public:

    eIOnDemandStatus StreamGetParameter();

    eIOnDemandStatus StreamPlay(bool usingStartNPT);

    eIOnDemandStatus SendKeepAlive();

    void HandleInput(void *ptrMessage);

    std::string GetNPTFromRange();


    eIOnDemandStatus StreamSetup(std::string url, std::string sessionId);

    void DisplayDebugInfo();
//Refactored code
    eIOnDemandStatus StreamGetPosition(float *npt);
    void StreamSetSpeed(int16_t num, uint16_t den);
    void StreamSetNPT(float npt);
    eIOnDemandStatus StreamGetSpeed(int16_t *num, uint16_t *den);
    eIOnDemandStatus StreamPause();

    void ReadStreamSocketData();
    void HandleStreamResp();

    VodLscp_Base  * GetLscObj()
    {
        return lscpBaseObj;
    };

    SeaChange_StreamControl(OnDemand* pOnDemand, void* pClientMetaDataContext, eOnDemandReqType reqType);
    ~SeaChange_StreamControl();

private:

    bool isPrecisoNset;
    bool isAssetUpdatedNPTset;
    int StreamFailCount;
};
#endif
