///////////////////////////////////////////////////////////////////////////////////////////////////////
//
//
//
// MediaControllerClassFactory.cpp -- Implementation file for class that is responsible for created different controller types
//
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////////////////

#include "MediaControllerClassFactory.h"
#if PLATFORM_NAME == IP_CLIENT
#include "zapper_ic.h"
#include "mrdvr_ic.h"
#include "sail-clm-api.h"
#include "audioPlayer_ic.h"
#endif
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#include "dvr.h"
#include "zapper.h"
#include "mrdvr.h"
#include "ondemand.h"
#include "audioPlayer.h"
#include "MrdvrTsbStreamer.h"
#include "MrdvrRecStreamer.h"
#include "HnOnDemandStreamer.h"
#endif

#include <dlog.h>


#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"MediaContollerClassFactory:%s:%d " msg, __FUNCTION__, __LINE__, ##args)


// Where is the header file for this???
//To know whether the box has support for DVR or Not before creating media controller.
extern bool IsDvrSupported();

IMediaController* MediaControllerClassFactory::CreateController(const std::string srcUrl, IMediaPlayerSession *pIMediaPlayerSession)
{
    IMediaController*  mediaController = NULL;
    eControllerType controllerType = eControllerTypeUnknown;

    controllerType = GetControllerType(srcUrl);
    LOG(DLOGL_REALLY_NOISY, "controllerType: %d", controllerType);

    switch (controllerType)
    {
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    case eControllerTypeDvr:
        mediaController = new Dvr(pIMediaPlayerSession);
        break;  // PJB testing only for zapper test

    case eControllerTypeVod:
        mediaController = new OnDemand();
        break;

    case eControllerTypeRecStreamer:
        mediaController = new MrdvrRecStreamer(pIMediaPlayerSession);
        break;
#endif //endif for PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#if PLATFORM_NAME == G8
    case eControllerTypeTsbStreamer:
        mediaController = new MrdvrTsbStreamer(pIMediaPlayerSession);
        break;

    case eControllerTypeHnOnDemandStreamer:
        mediaController = new HnOnDemandStreamer();
        break;
#endif  //endif for PLATFORM_NAME == G8

    case eControllerTypeAudio:
        mediaController = new AudioPlayer(pIMediaPlayerSession);
        break;

    case eControllerTypeZapper:
        LOG(DLOGL_REALLY_NOISY, "Creating controller of type Zapper");
        mediaController = new Zapper(false, pIMediaPlayerSession);
        break;

    case eControllerTypeMrdvr:
        mediaController = new Mrdvr();
        break;

    case eControllerTypeUnknown:
    default:
        LOG(DLOGL_ERROR, "Error: Unknown/Illegal mediaController(%d) for source: %s", controllerType, srcUrl.c_str());
        break;

    }

    if (!mediaController)
    {
        LOG(DLOGL_EMERGENCY, "Error: Creating mediaController");
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "mediaController created: %p type %d", mediaController, controllerType);
    }

    return mediaController;
}

MediaControllerClassFactory::eControllerType MediaControllerClassFactory::GetControllerType(const std::string serviceUrl)
{

    if ((serviceUrl.find(RF_SOURCE_URI_PREFIX) == 0)  || (serviceUrl.find(FILE2_SOURCE_URI_PREFIX) == 0) || (serviceUrl.find(PPV_SOURCE_URI_PREFIX) == 0))
    {
        bool isDvrSupported = IsDvrSupported();

        if (isDvrSupported)
        {
            if (0 == serviceUrl.find(RF_SOURCE_URI_PREFIX))
            {
                // check for music channel type and use zapper if true
                std::string channelStr = serviceUrl.substr(strlen(RF_SOURCE_URI_PREFIX));
                if (channelStr.length())
                {
                    Channel ClmChannel = atoi(channelStr.c_str());
                    ChannelList* pChannelList = CLM_GetChannelList(RF_SOURCE_URI_PREFIX);
                    if (pChannelList)
                    {
                        int32_t type;
                        if (kChannel_OK != Channel_GetInt(pChannelList, ClmChannel, 0, kChannelType, &type))
                        {
                            LOG(DLOGL_ERROR, "Failed to get Channel Type for channel:%d", ClmChannel);
                        }
                        else
                        {
                            LOG(DLOGL_NOISE, "ChannelType:%d for Channel:%d.",  type, ClmChannel);
                            if (kChannelType_Music == type)
                            {
                                // Music channel uses Zapper
                                CLM_FinalizeChannelList(&pChannelList);
                                return eControllerTypeZapper;
                            }
                        }
                    }
                }
            }// end of if (0 == serviceUrl.find(RF_SOURCE_URI_PREFIX) )

            return eControllerTypeDvr;
        }
        else  // create zapper if not DVR  TODO: Add other controllers here
        {
            return eControllerTypeZapper;
        }

    }
    else if ((serviceUrl.find(VOD_SOURCE_URI_PREFIX) == 0) || serviceUrl.find(RTSP_SOURCE_URI_PREFIX) == 0)
    {
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
        LOG(DLOGL_REALLY_NOISY, "returning eControllerTypeVod");
        return eControllerTypeVod;
#endif
#if PLATFORM_NAME == IP_CLIENT
        LOG(DLOGL_REALLY_NOISY, "returning eControllerTypeZapper");
        return eControllerTypeZapper;
#endif

    }
    else if (serviceUrl.find(MRDVR_SOURCE_URI_PREFIX) == 0)
    {
        return eControllerTypeMrdvr;
    }
    else if (serviceUrl.find(AUDIO_SOURCE_URI_PREFIX) == 0)
    {
        return eControllerTypeAudio;
    }
    else if (serviceUrl.find(QAMRF_SOURCE_URI_PREFIX) == 0)
    {
        return eControllerTypeZapper;
    }
    else if ((serviceUrl.find(MRDVR_TSB_STREAMING_URL) == 0) || (serviceUrl.find(MRDVR_TSB_PPV_STREAMING_URL) == 0) || (serviceUrl.find(MRDVR_TUNER_STREAMING_URL) == 0) || (serviceUrl.find(MRDVR_TUNER_PPV_STREAMING_URL) == 0))
    {
        return eControllerTypeTsbStreamer;
    }
    else if (serviceUrl.find(MRDVR_REC_STREAMING_URL) == 0)
    {
        return eControllerTypeRecStreamer;
    }
    else if (serviceUrl.find(HN_ONDEMAND_STREAMING_URL) == 0)
    {
        return eControllerTypeHnOnDemandStreamer;
    }
    else
    {
        LOG(DLOGL_ERROR, "Unknown Controller type url:%s", serviceUrl.c_str());
    }

    return eControllerTypeUnknown;
}

