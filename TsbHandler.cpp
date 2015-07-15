
#include "TsbHandler.h"

#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"IMediaPlayer:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

TsbHandler *TsbHandler::mInstance = NULL;

TsbHandler::TsbHandler()
{
    FNLOG(DL_MSP_MPLAYER);

    // Initializing the tsb availablility based on number of tuners from Unified settings.

    char tunerSettingbuffer[10] = {0};
    tSettingsAttributes attr;
    Settings_Get(NULL, "ciscoSg/media/numTuners", tunerSettingbuffer, (size_t) 3, &attr);
    dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s[%d] TUNER COUNT FROM Unified Settings=%s", __FUNCTION__, __LINE__, tunerSettingbuffer);
    sscanf(tunerSettingbuffer, "%d", &mTsbCount);

    dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s[%d] TSB COUNT=%d", __FUNCTION__, __LINE__, mTsbCount);

    mTsbInfo = new tsb_info((int)mTsbCount);

    for (int i = 0; i < mTsbInfo->tsb_count; i++)
    {
        mTsbInfo->tsb_availability[i] = true;
    }

    // Initialising the mutex that protects Mediaplayer shared resources
    pthread_mutexattr_t mta;
    pthread_mutexattr_init(&mta);
    pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m_TsbHandlerMutex, &mta);
}

TsbHandler::~TsbHandler()
{
    FNLOG(DL_MSP_MPLAYER);
}


void TsbHandler::locktsbhandlermutex()
{
    pthread_mutex_lock(&m_TsbHandlerMutex);
}

void TsbHandler::unlocktsbhandlermutex()
{
    pthread_mutex_unlock(&m_TsbHandlerMutex);
}


eMspStatus  TsbHandler::get_tsb(unsigned int *tsb_number)
{
    eMspStatus status = kMspStatus_Ok;
    int i;

    //mutex is included here to prevent synchronize issue ,since get_tsb is shared my IMediaStreamer as well

    locktsbhandlermutex();

    //going through the available TSB's
    for (i = 0; i < mTsbCount; i++)
    {
        if (mTsbInfo->tsb_availability[i] == true)
        {
            mTsbInfo->tsb_availability[i] = false;
            break;
        }
    }
    if (i == mTsbCount)
    {
        dlog(DL_MEDIAPLAYER, DLOGL_ERROR, "%d,%s All TSB's are in use \n", __LINE__, __FUNCTION__);

        //No tsb availability TODO-why can't we introduce kMspStatus_Resource_Not_available in error list?
        status =  kMspStatus_Error;
    }
    else
    {
        *tsb_number = i;
    }

    unlocktsbhandlermutex();

    return status;
}

eMspStatus TsbHandler::release_tsb(unsigned int *tsb_number)
{
    eMspStatus status = kMspStatus_Ok;

    locktsbhandlermutex();

    if (*tsb_number >= (unsigned int)mTsbCount)   //not a valid TSB number , so setting status to kMspStatus_BadParameters
    {
        dlog(DL_MEDIAPLAYER, DLOGL_ERROR, "%d,%s Not a valid TSB \n", __LINE__, __FUNCTION__);

        status = kMspStatus_BadParameters;
    }
    else
    {
        mTsbInfo->tsb_availability[*tsb_number] = true;
    }

    unlocktsbhandlermutex();

    return status;

}

TsbHandler * TsbHandler::getTsbHandlerInstance()
{
    if (mInstance == NULL)
    {
        mInstance = new TsbHandler();
    }
    return mInstance;
}

