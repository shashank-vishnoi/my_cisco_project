///////////////////////////////////////////////////////////////////////////
//                    SAIL Includes
///////////////////////////////////////////////////////////////////////////

#include <dlog.h>

//////////////////////////////////////////////////////////////////////////
//                         Local Includes
///////////////////////////////////////////////////////////////////////////

#include "avpm.h"
#include <time.h>
#include "IMediaController.h"
///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

#define LOG(level, msg, args...)  dlog(DL_MSP_AVPM, level,"1394:%s:%d " msg, __FUNCTION__, __LINE__, ##args);



/**
 *   \brief This function shall get the number of instances of 1394 ports and store it in a member of Avpm class.
 *   Note that though the number of instances are retreived, the CPE-RP API's currently might perform a specified
 *   action on all available 1394 ports. There is no mechanism to act on specific instances of 1394 ports available.
 *
 *   \param None
 *
 *   \return kMspStatus_Ok or kMspStatus_Error.
 */

eMspStatus Avpm::avpm1394PortInit(void)
{
    tCpeMedia1394List p1394List;
    eMspStatus retStatus;
    //p1394List = (tCpeMedia1394List*)(malloc(sizeof(tCpeMedia1394List)));
    FNLOG(DL_MSP_MPLAYER);
    int status = cpe_media_SystemGet(eCpeMediaSystemGetSetNames_1394Capabilities, (tCpeMedia1394List*)&p1394List, sizeof(tCpeMedia1394List));
    if (status == 0)
    {
        /* Get the instances from the mem filled by the CPE-RP call above */
        mAvailable1394Instances = p1394List.numInstances;
        LOG(DLOGL_NORMAL, "Number of 1394 Instances %d", mAvailable1394Instances);
        retStatus = kMspStatus_Ok;
    }
    else
    {
        mAvailable1394Instances = -1;
        LOG(DLOGL_ERROR, "Error in getting 1394 capabilities %d", status);
        retStatus = kMspStatus_Error;
    }
    return retStatus;
}



/**
 *   \brief This function shall Enable/Disable the available 1394 ports. The ports shall be enabled in Digital mode by default.
 *
 *   \param mediaHandle - the program handle that would be passed by Media Player
 *   \param enable      - The flag that indicates whether to enable or disable the port
 *
 *   \return kMspStatus_Ok or kMspStatus_Error.
 */
eMspStatus Avpm::avpm1394PortStreaming(tCpePgrmHandle mediaHandle, bool enable)
{
    eMspStatus retStatus;
    int status;

    FNLOG(DL_MSP_MPLAYER);

    LOG(DLOGL_NOISE, "%s 1394 port \n", (enable ? "Enabling" : "Disabling"));

    status = cpe_media_Set(mediaHandle, eCpeMediaGetSetNames_Enable1394, (void*)&enable);
    if (status == 0)
    {
        LOG(DLOGL_NOISE, "%s the 1394 port suceeded", (enable ? "Enabled" : "Disabled"));
        retStatus = kMspStatus_Ok;
    }
    else
    {
        LOG(DLOGL_ERROR, "Set request to 1394 Failed: Status = %d", status);
        retStatus = kMspStatus_Error;
    }

    return retStatus;
}



/**
 *   \brief This function shall switch the port to Analog or Digital mode based upon the parameter.
 *                      It shall use the current media program handle, to invoke the CPE-RP api and switch the jack mode.
 *
 *   \param enable      - TRUE - sets the port to Analog mode
 *                                      - FALSE - sets the port to Digital mode
 *
 *   \return kMspStatus_Ok or kMspStatus_Error.
 */
eMspStatus Avpm::avpmSwitch1394PortMode(bool enable)
{
    eMspStatus retStatus = kMspStatus_Ok;
    int status = -1;
    tCpeMedia1394Command controlCommand;

    FNLOG(DL_MSP_MPLAYER);

    controlCommand.command = eCpeMedia1394CommandTypes_SelectJack;

    if (enable)
    {
        controlCommand.info.jack = eCpeMedia1394Jack_JackAnalog;
        LOG(DLOGL_NORMAL, "Switching 1394 to Analog mode");
    }
    else
    {
        controlCommand.info.jack = eCpeMedia1394Jack_JackDigital;
        LOG(DLOGL_NORMAL, "Switching 1394 to Digital mode");
    }
    status = cpe_media_Control(mMainScreenPgrHandle, eCpeMediaControlNames_1394Command, (void*)&controlCommand);
    if (status == 0)
    {
        LOG(DLOGL_NORMAL, "Switched 1394 to %s mode!\n", (enable ? "Analog" : "Digital"));
    }
    else
    {
        retStatus = kMspStatus_Error;
        LOG(DLOGL_ERROR, "Switching the 1394 port mode failed with return status %d", status);
    }
    return retStatus;

}
