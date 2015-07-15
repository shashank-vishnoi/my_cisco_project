///////////////////////////////////////////////////////////////////////////
//                    SAIL Includes
///////////////////////////////////////////////////////////////////////////

#include <dlog.h>

//////////////////////////////////////////////////////////////////////////
//                         Local Includes
///////////////////////////////////////////////////////////////////////////
#if PLATFORM_NAME == G8
#include "avpm.h"
#endif
#if PLATFORM_NAME == IP_CLIENT
#include "avpm_ic.h"
#endif
#include "IMediaController.h"
///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////


#define LOG(level, msg, args...)  dlog(DL_MSP_AVPM, level,"1394:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#define UNUSED_PARAM(a) (void)a;



/**
 *   \brief This function  is a stub equivalent of G6 version of the same for initializing 1394 port
 *
 *   \param None
 *
 *   \return kMspStatus_NotSupported .
 */

eMspStatus Avpm::avpm1394PortInit(void)
{
    FNLOG(DL_MSP_MPLAYER);
    LOG(DLOGL_NOISE, "G8 stub version");
    return kMspStatus_NotSupported;
}


#if PLATFORM_NAME == G8
/**
 *   \brief This function is a stub equivalent of G6 version of the same function to enable/disable the 1394 streaming
 *
 *   \param mediaHandle - the program handle that would be passed by Media Player
 *   \param enable      - The flag that indicates whether to enable or disable the port
 *
 *   \return kMspStatus_NotSupported.
 */
eMspStatus Avpm::avpm1394PortStreaming(tCpePgrmHandle mediaHandle, bool enable)
{
    UNUSED_PARAM(mediaHandle);
    UNUSED_PARAM(enable);
    FNLOG(DL_MSP_MPLAYER);
    LOG(DLOGL_NOISE, "G8 stub version");
    return kMspStatus_NotSupported;
}

#endif
#if PLATFORM_NAME == IP_CLIENT

/**
 *   \brief This function is a stub equivalent of G6 version of the same function to enable/disable the 1394 streaming
 *
 *   \param mediaHandle - the program handle that would be passed by Media Player
 *   \param enable      - The flag that indicates whether to enable or disable the port
 *
 *   \return kMspStatus_NotSupported.
 */
eMspStatus Avpm::avpm1394PortStreaming(bool enable)
{
    UNUSED_PARAM(enable);
    FNLOG(DL_MSP_MPLAYER);
    LOG(DLOGL_NOISE, "IPClient stub version");
    return kMspStatus_NotSupported;
}
#endif

/**
 *   \brief This function is a stub equivalent of G6 version of the same function, which does switching between analog and digiital mode.
 *
 *   \param enable      - TRUE - sets the port to Analog mode
 *                                      - FALSE - sets the port to Digital mode
 *
 *   \return kMspStatus_NotSupported
 */
eMspStatus Avpm::avpmSwitch1394PortMode(bool enable)
{
    UNUSED_PARAM(enable);
    FNLOG(DL_MSP_MPLAYER);
    LOG(DLOGL_NOISE, "G8 stub version");
    return kMspStatus_NotSupported;

}

