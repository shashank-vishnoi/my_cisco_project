/** @file VODFactory.cpp
 *
 * @brief Factory method implementation to differentiate Vod types.
 *
 * @author Rosaiah Naidu Mulluri
 *
 * @version 1.0
 *
 * @date 05.10.2012
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */
#include "Arris_SessionControl.h"
#include "CloudDvr_SessionControl.h"
#include "CloudDvr_StreamControl.h"
#include "Arris_StreamControl.h"
#include "SeaChange_SessionControl.h"
#include "SeaChange_StreamControl.h"
#include "VODFactory.h"
#include "string.h"


#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"VODFactory:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

VOD_SessionControl*  VODFactory :: getVODSessionControlInstance(OnDemand* ptrOnDemand, const char *aSrcUrl)
{
    VOD_SessionControl *src = NULL;
    char srmMfg[20] = {0};
    size_t Pos = -1 , strPos = -1, endPos = -1;

    FNLOG(DL_MEDIAPLAYER);

    std::string srcUrl;
    srcUrl.assign(aSrcUrl);

    LOG(DLOGL_NORMAL, "parseSource: source %s", srcUrl.c_str());

    //First check & initialize the Cloud-DVR Session Controller
    Pos = srcUrl.find("rtsp://");
    if (Pos == 0)
    {
        LOG(DLOGL_NORMAL, "VOD Session type for the given URL is Cloud DVR");
        CloudDvr_SessionControl* cloudDvr = new CloudDvr_SessionControl(ptrOnDemand, aSrcUrl, NULL, ePreparingToViewType);
        //Added for fixing the coverity defect
        cloudDvr->CreateDSMCCObj();
        src = cloudDvr;
    }
    else
    {
        Pos = srcUrl.find("srmManufacturer=");
        if (Pos != string::npos)
        {
            strPos = srcUrl.find("=", Pos);
            endPos = srcUrl.find("&", Pos);
            strcpy(srmMfg, srcUrl.substr(strPos + 1, endPos - strPos - 1).c_str());
        }

        LOG(DLOGL_NORMAL, "srmManufacturer= %s", srmMfg);


        if (strcmp(srmMfg, "Arris") == 0)
        {
            LOG(DLOGL_NORMAL, "VOD Session type for the given URL is ARRIS ");
            src = new Arris_SessionControl(ptrOnDemand, aSrcUrl, NULL, ePreparingToViewType);
        }
        else if (strcmp(srmMfg, "Seachange") == 0)
        {
            LOG(DLOGL_NORMAL, "VOD Session type for the given URL is SEA-CHANGE");
            src = new SeaChange_SessionControl(ptrOnDemand, aSrcUrl, NULL, ePreparingToViewType);
        }
        else
        {
            LOG(DLOGL_ERROR, " Invalid VOD Session Control type");
        }
    }
    return src;
}

VOD_StreamControl*  VODFactory :: getVODStreamControlInstance(OnDemand* ptrOnDemand, const char *aSrcUrl)
{
    VOD_StreamControl *src = NULL;
    char strMfg[20] = {0};
    size_t Pos = -1 , strPos = -1, endPos = -1;

    FNLOG(DL_MEDIAPLAYER);

    std::string srcUrl;
    srcUrl.assign(aSrcUrl);

    LOG(DLOGL_NORMAL, "parseSource: source %s", srcUrl.c_str());

    //First check & initialize the Cloud-DVR Session Controller
    Pos = srcUrl.find("rtsp://");
    if (Pos == 0)
    {
        LOG(DLOGL_NORMAL, "VOD Stream type for the given URL is Cloud DVR");
        src = new CloudDvr_StreamControl(ptrOnDemand, NULL, ePreparingToViewType);
    }
    else
    {
        Pos = srcUrl.find("streamerManufacturer=");
        if (Pos != string::npos)
        {
            strPos = srcUrl.find("=", Pos);
            endPos = srcUrl.find("&", Pos);
            strcpy(strMfg, srcUrl.substr(strPos + 1, endPos - strPos - 1).c_str());
        }

        LOG(DLOGL_NORMAL, "streamerManufacturer= %s", strMfg);


        if (strcmp(strMfg, "Arris") == 0)
        {
            LOG(DLOGL_NORMAL, "VOD Stream type for the given URL is ARRIS ");
            src = new Arris_StreamControl(ptrOnDemand, NULL, ePreparingToViewType);
        }
        else if (strcmp(strMfg, "Seachange") == 0)
        {
            LOG(DLOGL_NORMAL, "VOD Stream type for the given URL is SEA-CHANGE");
            src = new SeaChange_StreamControl(ptrOnDemand, NULL, ePreparingToViewType);
        }
        else
        {
            LOG(DLOGL_ERROR, " Invalid VOD Session Control type");
        }
    }

    return src;
}
