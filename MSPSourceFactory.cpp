#if PLATFORM_NAME == IP_CLIENT
#include "MSPHTTPSource_ic.h"
#include "MSPPPVSource_ic.h"
#endif
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#include "MSPRFSource.h"
#include "MSPFileSource.h"
#include "MSPHTTPSource.h"
#include "MSPMrdvrStreamerSource.h"
#include "MSPPPVSource.h"
#endif
#include "MSPSourceFactory.h"
#include "string.h"
#include <sail-clm-api.h>
#include "MspCommon.h"


#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"MSPSourceFactory:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

eMSPSourceType MSPSourceFactory :: getMSPSourceType(const char *aSrcUrl)
{
    FNLOG(DL_MEDIAPLAYER);

    std::string srcUrl;
    eMSPSourceType srcType = kMSPInvalidSource;

    srcUrl.assign(aSrcUrl);

    if (srcUrl.find(MRDVR_TSB_STREAMING_URL) == 0 || srcUrl.find(MRDVR_TUNER_STREAMING_URL) == 0)
    {
        LOG(DLOGL_NOISE, "Source type for the given URL is RF <LIVE STREAMING>");
        srcType = kMSPRFSource;
    }
    else if (srcUrl.find(MRDVR_TSB_PPV_STREAMING_URL) == 0)
    {
        LOG(DLOGL_NOISE, "Source type for the given URL is RF <LIVE STREAMING>");
        srcType = kMSPPPVSource;
    }
    else if (srcUrl.find(RF_SOURCE_URI_PREFIX) == 0 || srcUrl.find(QAMRF_SOURCE_URI_PREFIX) == 0)
    {
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
        LOG(DLOGL_NOISE, "Source type for the given URL is RF ");
        srcType = kMSPRFSource;
#endif
#if PLATFORM_NAME == IP_CLIENT
        LOG(DLOGL_NOISE, "Source type for the given URL is HTTP ");
        srcType = kMSPMHTTPSource;
#endif
    }
    else if (srcUrl.find(FILE1_SOURCE_URI_PREFIX) == 0 || srcUrl.find(FILE2_SOURCE_URI_PREFIX) == 0)
    {
        LOG(DLOGL_NOISE, "Source type for the given URL is File");
        srcType = kMSPFileSource;
    }
    else if (srcUrl.find(PPV_SOURCE_URI_PREFIX) == 0)
    {
        LOG(DLOGL_NOISE, "Source type for the given URL is PPV");
        srcType = kMSPPPVSource;
    }
    else if (srcUrl.find(MRDVR_SOURCE_URI_PREFIX) == 0)
    {
        LOG(DLOGL_NOISE, "Source type for the given URL is HTTP");
        srcType = kMSPMHTTPSource;
    }
    else if (srcUrl.find(MRDVR_REC_STREAMING_URL) == 0)
    {
        LOG(DLOGL_NOISE, "Source type for the given URL is REC STREAMER");
        srcType = kMSPMrdvrStreamerSource;
    }
    else if (srcUrl.find(HN_ONDEMAND_STREAMING_URL) == 0)
    {
        LOG(DLOGL_NOISE, "Source type for the given URL is HN ONDEMAND STREAMER");
        srcType = kMSPHnOnDemandStreamerSource;
    }
    else if (srcUrl.find(VOD_SOURCE_URI_PREFIX) == 0)
    {
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
        LOG(DLOGL_NOISE, "Source type for the given URL is RF");
        srcType = kMSPRFSource;
#endif
#if PLATFORM_NAME == IP_CLIENT
        LOG(DLOGL_NOISE, "Source type for the given URL is HTTP ");
        srcType = kMSPMHTTPSource;
#endif
    }
    else
    {
        LOG(DLOGL_ERROR, " Invalid source type %s", aSrcUrl);
    }

    return srcType;
}




MSPSource*  MSPSourceFactory :: getMSPSourceInstance(eMSPSourceType srcType , const char *aSrcUrl, IMediaPlayerSession *pIMediaPlayerSession)
{
    MSPSource *src = NULL;
    std::string srcUrl;
    srcUrl.assign(aSrcUrl);
#if PLATFORM_NAME == IP_CLIENT
    UNUSED(pIMediaPlayerSession);
#endif

    switch (srcType)
    {
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    case kMSPRFSource:
        if (srcUrl.find(RF_SOURCE_URI_PREFIX) == 0)
        {
            std::string channelStr = srcUrl.substr(strlen(RF_SOURCE_URI_PREFIX));
            if (channelStr.length())
            {
                Channel clmChannel = atoi(channelStr.c_str());
                ChannelList *channelList = CLM_GetChannelList(NETWORK_URI_PREFIX);
                if (channelList)
                {
                    time_t now;
                    time(&now);

                    int channelType;
                    eChannel_Status chanStatus = Channel_GetInt(channelList, clmChannel, now, kChannelType, &channelType);
                    if (chanStatus == kChannel_OK)
                    {
                        switch (channelType)
                        {
                        case kChannelType_Video:
                        case kChannelType_Music:
                        case kChannelType_SDV:     // RF source and SDV source are combined now
                        case kChannelType_Mosaic:
                            src = new MSPRFSource(srcUrl, clmChannel, channelList, pIMediaPlayerSession);
                            break;

                        default:
                            dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "UnKnown Channel Type");
                            break;
                        }
                    }
                }
            }
        }
        else if (srcUrl.find(QAMRF_SOURCE_URI_PREFIX) == 0)
        {
            src = new MSPRFSource(srcUrl, 0, NULL, pIMediaPlayerSession);
        }
        else if (srcUrl.find(MRDVR_TSB_STREAMING_URL) == 0)
        {
            std::string channelStr = srcUrl.substr(strlen(MRDVR_TSB_STREAMING_URL));
            if (channelStr.length())
            {
                Channel clmChannel = atoi(channelStr.c_str());
                ChannelList *channelList = CLM_GetChannelList(NETWORK_URI_PREFIX);
                if (channelList)
                {
                    time_t now;
                    time(&now);
                    int channelType;
                    eChannel_Status chanStatus = Channel_GetInt(channelList, clmChannel, now, kChannelType, &channelType);
                    if (chanStatus == kChannel_OK)
                    {
                        switch (channelType)
                        {
                        case kChannelType_Video:
                        case kChannelType_Music:
                        case kChannelType_SDV:     // RF source and SDV source are combined now
                        case kChannelType_Mosaic:
                            src = new MSPRFSource(srcUrl, clmChannel, channelList, pIMediaPlayerSession);
                            break;
                        default:
                            dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "UnKnown Channel Type");
                            break;
                        }
                    }
                }
            }
        }
        else if (srcUrl.find(MRDVR_TUNER_STREAMING_URL) == 0)
        {
            std::string channelStr = srcUrl.substr(strlen(MRDVR_TUNER_STREAMING_URL));
            if (channelStr.length())
            {
                Channel clmChannel = atoi(channelStr.c_str());
                ChannelList *channelList = CLM_GetChannelList(NETWORK_URI_PREFIX);
                if (channelList)
                {
                    time_t now;
                    time(&now);
                    int channelType;
                    eChannel_Status chanStatus = Channel_GetInt(channelList, clmChannel, now, kChannelType, &channelType);
                    if (chanStatus == kChannel_OK)
                    {
                        switch (channelType)
                        {
                        case kChannelType_Video:
                        case kChannelType_Music:
                        case kChannelType_SDV:     // RF source and SDV source are combined now
                        case kChannelType_Mosaic:
                            src = new MSPRFSource(srcUrl, clmChannel, channelList, pIMediaPlayerSession);
                            break;
                        default:
                            dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "UnKnown Channel Type");
                            break;
                        }
                    }
                }
            }
        }
        break;

    case kMSPFileSource:
        src = new MSPFileSource(srcUrl);
        break;
#endif

    case kMSPPPVSource:
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Going to create MSPPVSource.");
        src = new MSPPPVSource(srcUrl, pIMediaPlayerSession);
        break;

    case kMSPMHTTPSource:
        src = new MSPHTTPSource(srcUrl);
        break;
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    case kMSPMrdvrStreamerSource:
        src = new MSPMrdvrStreamerSource(srcUrl);
        break;
#endif
    default:
        LOG(DLOGL_ERROR, "Invalid source type");
        break;
    }
    return src;
}
