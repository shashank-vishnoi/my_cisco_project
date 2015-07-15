#ifndef MEDIA_RTT_IC_H
#define MEDIA_RTT_IC_H
#include <MspCommon.h>
#include <itimer.h>

typedef void (*MediaRTTRetryCallback)(void *aClientContext);

class MediaRTT
{
private:
    static MediaRTT *mInstance;
    intptr_t timerId;

    MediaRTT();         // private for singleton
    static void timerCallback(evutil_socket_t fd, short what, void* data);
    void startRTTTimeoutTimer(void);
    time_t CalculateTimerTime();
    void CleanUpTimer(void);
    void ReScheduleRTTTimeout(void);


public:
    ~MediaRTT();
    static MediaRTT * getMediaRTTInstance();
    void MediaRTTRegisterCallback(MediaRTTRetryCallback aRTTRetryCB, void* aClientContext);
    void MediaRTTUnRegisterCallback();

protected:
    MediaRTTRetryCallback mRTTRetryCB;
    void *mClientContext;

};

#endif // #ifndef MEDIA_RTT_IC_H

