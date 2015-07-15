/*
 * MSPMediaShrinkClient.h

 *  Derived from ResMonClient.h
 *  Created on: Nov 22, 2010
 *      Author: robpar
 */
#if (ENABLE_MSPMEDIASHRINK == 1)


#ifndef MSPMEDIASHRINK_H_
#define MSPMEDIASHRINK_H_

#include <pthread.h>
#include <pthread_named.h>
#include <list>
#include "cpeutil_mediashrink.h"
#include "dlog.h"
#include "sail-settingsuser-api.h"
#include "use_threaded.h"
#include "sail-dvr-scheduler-api.h"
#include "CDvrQueryResult.h"
#include "CDvrDBFacade.h"
#include "MSPMediaShrinkInterface.h"
#include "cpe_dvr_common.h"
#include "cpe_source.h"
#include "cpe_recmgr.h"
#include <map>
#define MAX_SETTING_SIZE 256
#define	MAX_REC_SESSION_THRESHOLD 2
#define LEN 512
#define TIME_STRLEN 200
#define MAX_RETRY_ON_FAILURE 2
#define MAX_RETRY_CNT_SETTING 3
#define	START_THRESHOLD	5

#define	STOP_THRESHOLD	2

#define	TRANSCODE_INPROGRESS	1
#define	TRANSCODE_COMPLETE	2

#define	ONE_SECOND	1000

#define	USEEM_RECON_TIME				60 //60secs
#define	RECONNECT_TIMER				"USEEM_RECONNECT"

#define	MAX_ALLOWED_TIMEOUT	3
#define LOCAL_UUID_STRING "00000000-0000-0000-0000-000000000000"
#define	DVR_PATH   "/mnt/dvr0,/mnt/dvr1"
#define AVFS_FILE_PREFIX "avfs:/" //Prefix required to add to file to be deleted to
// match the srcFile name given by MDK

#define MEDIASHRINK_SETTING	"ciscoSg/rec/enableMediashrink"
#define MEDIASHRINK_ENABLED	1
#define MAX_TIMEOOUT_ITERATION	10 	// 5 minutes

#define MIN_TRANSOCODING_BITRATE	0 	// Mbps: Currently set to 0 so that all
// SD recordings get transcoded

uint32_t isTheRecordingPlaying(uint32_t assetID);

void commitDeferred();

struct stRecData_t
{
    bool isRecording;
    int32_t filesize;
    int32_t newsize;
    double ratio;
    double percent;
    int32_t Hrs;
    int32_t Min;
    int32_t Sec;
    char title[MBTV_MAX_EPISODE_NAME];
    char episodeName[MBTV_MAX_EPISODE_NAME];
    char cEpgStart[LEN];
    uint32_t epgDuration;
    char cRecStart[LEN];
    uint32_t recDuration;
    uint32_t channel;
    uint32_t assetID;
    tCpeUtilMediaShrinkSessionId sess_Id;
    double mbps;

};

class MSPMediaShrink
{

private:

    static MSPMediaShrink* m_pInstance;
    MSPMediaShrink();


public:
    virtual ~MSPMediaShrink();
    // Shall be passed  with "globalEnable = false" except from Msp_Mediashrink_Init()
    static MSPMediaShrink* getMediaShrinkInstance(bool globalEnable = false);

    void 	*dispatchLoop(void);
    bool 	Initilize_MediaShrink();
    void  	PipNotification(uint32_t Enabled);
    uint32_t  	mPipEnabled;
    uint32_t 	mNoOfRecSessions;
    bool 	deleteRecording(const char* fileToDelete);
    void 	RecStartNotification();
    void 	RecStopNotification();
    void	RecPlayBackStartNotification();
    void 	RecPlayBackStopNotification();
    void 	handleRecNotification(bool recState);
    void	turnOnMediaShrink();
    void	turnOffMediaShrink(tCpeUtilMediaShrinkSessionId sess_id, char *filename);


protected:

    int  m_localReadFd;
    int  m_localWriteFd;

    pthread_t dispatchThread;


};

#endif /* MSPMEDIASHRINK_H_ */

#endif //#if (ENABLE_MSPMEDIASHRINK == 1)

