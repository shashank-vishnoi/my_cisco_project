#include <time.h>
#include <sys/time.h>
#include "MediaRTT_ic.h"

#define LOG(level, msg, args...)  dlog(DL_MSP_MRDVR,level,"MediaRTT:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#define UNUSED_PARAM(arg) (void )arg;

MediaRTT *MediaRTT::mInstance = NULL;

/**
    \brief Constructor
 */
MediaRTT::MediaRTT()
{
    FNLOG(DL_MSP_MRDVR);

    timerId = 0;
    mRTTRetryCB = NULL;
    mClientContext = NULL;
}

/**
    \brief Get the MediaRTT Singleton instance
 */
MediaRTT * MediaRTT::getMediaRTTInstance()
{
    FNLOG(DL_MSP_MRDVR);
    //Only single thread is expected to call this since only one channel tune at a time in client side.
    //So not protecting with mutex.
    if (NULL == mInstance)
    {
        mInstance = new MediaRTT();

        if (NULL != mInstance)
        {
            mInstance->startRTTTimeoutTimer();
        }
    }
    return mInstance;
}

/**
    \brief Destructor
 */
MediaRTT::~MediaRTT()
{
    FNLOG(DL_MSP_MRDVR);

    CleanUpTimer();
    mRTTRetryCB = NULL;
    mClientContext = NULL;
    mInstance = NULL;
    LOG(DLOGL_REALLY_NOISY, "RTT Timer class is destroyed");
}

/**
    \brief timerCallback is triggered from ITimer wrapper in timer thread whan the scheduled time runs out at 3AM.
    \param fd, what - Ignored eventloop variables
    \param data - this provides the singleton instance of MediaRTT to access its objects
    \return void
 */
void MediaRTT::timerCallback(evutil_socket_t fd, short what, void* data)
{
    FNLOG(DL_MSP_MRDVR);

    UNUSED_PARAM(fd);
    UNUSED_PARAM(what);
    EventTimer* timer = reinterpret_cast <EventTimer*>(data);
    MediaRTT* instance = reinterpret_cast <MediaRTT*>(timer->getUserData());

    if (NULL != instance)
    {
        if ((NULL != instance->mClientContext) && (NULL != instance->mRTTRetryCB))
        {
            if (DLOGL_REALLY_NOISY == dlog_get_level(DL_MSP_MRDVR))
            {
                time_t currTime = 0;
                struct tm * currTimeStruct = NULL;
                time(&currTime);
                currTimeStruct = localtime(&currTime);

                LOG(DLOGL_REALLY_NOISY, "RTT timer retry triggered at time day - %d, month - %d , time %d:%d:%d",
                    (currTimeStruct->tm_mday += 1), (currTimeStruct->tm_mon += 1),
                    (currTimeStruct->tm_hour), (currTimeStruct->tm_min), (currTimeStruct->tm_sec));
            }

            //Even if this is called after the mClientContext becomes NULL from zapper_stop, we check the instance and the state in zapper
            //This is again synchronized in the event Q of zapper. So mutex is not needed here.
            instance->mRTTRetryCB(instance->mClientContext);
        }
        else
        {
            //RTT retry should be done only when the channel is not a recorded content or a VOD content. These wont have the zapper instance.
            LOG(DLOGL_ERROR, "RTT Timer triggered, but not doing retry. Rescheduling the timer");
        }
        instance->ReScheduleRTTTimeout();
    }
    else
    {
        //Shouldn't get here unless someone exclusively deleted the singleton object and has not reinitialized.
        LOG(DLOGL_ERROR, "RTT instance is NULL so timer scheduling failed");
    }
}

/**
    \brief Calculate the time structure for the next day 3AM
    \return time structure
 */
time_t MediaRTT::CalculateTimerTime()
{
    FNLOG(DL_MSP_MRDVR);

    time_t currTime = 0;
    struct tm * currTimeStruct = NULL;

    time(&currTime);
    currTimeStruct = localtime(&currTime);
    currTimeStruct->tm_sec = 0;
    currTimeStruct->tm_min = 0;
    currTimeStruct->tm_hour = 3;
    //C++ time module will automatically take into account the correct days of the month, year and roll over correctly.
    currTimeStruct->tm_mday++;
    LOG(DLOGL_REALLY_NOISY, "calculated time to 3AM - %f", difftime(mktime(currTimeStruct), currTime));
    return mktime(currTimeStruct);
}

/**
    \brief start the RTT Timer for the first time. Called only once to avoid multiple Timer threads running
 */
void MediaRTT::startRTTTimeoutTimer(void)
{
    FNLOG(DL_MSP_MRDVR);

    eTimer_StatusCode ret = ETIMER_ERROR;

    LOG(DLOGL_REALLY_NOISY, "Trigger Timer the first time");
    ret = Timer_addTimerAbsolute(CalculateTimerTime() , MediaRTT::timerCallback, (void *) mInstance, &timerId);
    if (ETIMER_SUCCESS != ret)
    {
        LOG(DLOGL_ERROR, "RTT Timer creation failed. Can result in frozen screen");
    }
}

/**
    \brief CleanUp the ITimer Thread and the event loop associated
 */
void MediaRTT::CleanUpTimer(void)
{
    FNLOG(DL_MSP_MRDVR);

    if (timerId)
    {
        //Ignoring the return code as the timerid is valid
        Timer_deleteTimer(timerId);
        timerId = 0;
        LOG(DLOGL_REALLY_NOISY, "Timer deleted");
    }
}

/**
    \brief Update the RTT Timer to be triggered at next day's 3AM. Can be called multiple times.
            Does not spawn new instance of Itimer thread
 */
void MediaRTT::ReScheduleRTTTimeout(void)
{
    FNLOG(DL_MSP_MRDVR);

    time_t currTime = 0;
    eTimer_StatusCode ret = ETIMER_ERROR;

    time(&currTime);
    ret = Timer_updateTimer((difftime(CalculateTimerTime(), currTime)), &timerId);
    if (ETIMER_SUCCESS != ret)
    {
        LOG(DLOGL_ERROR, "RTT Timer creation failed. Can result in frozen screen");
    }
}

/**
    \brief Register the new instance of zapper context with RTT.
    \param aRTTRetryCB RTT defined callback function for zapper
    \param aClientContext zapper context
 */
void MediaRTT::MediaRTTRegisterCallback(MediaRTTRetryCallback aRTTRetryCB, void* aClientContext)
{
    FNLOG(DL_MSP_MRDVR);

    mRTTRetryCB = aRTTRetryCB;
    mClientContext = aClientContext;
}

/**
    \brief UnRegister the zapper instance as it is being stopped and not valid for retry
 */
void MediaRTT::MediaRTTUnRegisterCallback()
{
    FNLOG(DL_MSP_MRDVR);

    mRTTRetryCB = NULL;
    mClientContext = NULL;
}
