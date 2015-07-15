///////////////////////////////////////////////////////////////////////////////////////////////////////
//
//
//
// MediaControllerClassFactory.h -- Header file for class that is responsible for created different controller types
//
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////////////////
#if !defined(MEDIA_CONTROLLER_SOURCE_FACTORY_H)
#define MEDIA_CONTROLLER_SOURCE_FACTORY_H

#include <string>

class IMediaController;
class IMediaPlayerSession;

class MediaControllerClassFactory
{

public:
    typedef enum
    {
        eControllerTypeUnknown,
        eControllerTypeZapper,
        eControllerTypeDvr,
        eControllerTypeVod,
        eControllerTypeMrdvr,
        eControllerTypeAudio,
        eControllerTypeRecStreamer,
        eControllerTypeTsbStreamer,
        eControllerTypeHnOnDemandStreamer
    } eControllerType;

    static IMediaController* CreateController(const std::string srcUrl, IMediaPlayerSession *pIMediaPlayerSession);
    static eControllerType GetControllerType(const std::string srcUrl);
};

#endif
