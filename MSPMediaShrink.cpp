/*
 * MSPMediaShrink.cpp
 *
 *  Derived from:  ResMonClient.cpp
 *  Created on:    Jan 30, 2014
 *  Author:    	   Steve Olivier, Alap Kumar Sinha
 *
 */
#if (ENABLE_MSPMEDIASHRINK == 1)

#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <assert.h>
#include "diaglib.h"
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "MSPMediaShrink.h"
#include <string>

using namespace std;

static int32_t global_fd = -1;

static pthread_mutex_t mshrink_mutex;

static bool shrinking = false;
//static bool first=true;
static uint32_t mShrinkCurrentState = 0;
static bool gIsSystemReady = false;
static bool gIsInitComplete = false; // This variable is used to determine whether media shrink
// was initialized completely (currently cpe_encodersrc_Init() done.



static map<string, double > lowBitRate;	// Holds the list of recording for which transcoding was skipped
// because the bitrate falls below threshold MIN_TRANSOCODING_BITRATE
// The vale of a key would refer to the bitrate
static map<string, int32_t > aborted;	// Holds the list of recording for which transcoding failed
// The vale of a key would refer to no of failures
static map<string, int32_t > processed; //Holds the list of recordings with transcoding in progress/complete.
// The value of a key  is either TRANSCODE_INPROGRESS or TRANSCODE_COMPLETE
static map<string, int32_t > fragmented;// Holds the list of fragmented recording for ease of ignoring the recording
// for transcoding.

static map<string, stRecData_t> deferred;// Holds the list of recording for which commit was deferred post transcoding
// since the recording was being played back.
typedef map<string, int32_t >::iterator iterMap;

static uint32_t lastWritten = 0;
static char currentFile[LEN] = {0};
static char currentTitle[LEN] = {0};
static char episodeName[MBTV_MAX_EPISODE_NAME] = {0};
static double Mbps = 0.0;
static uint8_t last_percent_complete = 0;
static char cEpgStart[LEN] = {0};
static uint32_t channel = 0;
static int32_t tries = 0;
static time_t startTime = 0;
static time_t stopTime = 0;

tCpeUtilMediaShrinkSessionId lastSessId = 0;
MSPMediaShrink *MSPMediaShrink::m_pInstance = NULL;

int mshrinkCallBack(tCpeUtilMediaShrinkSessionId, tCpeUtilMediaShrinkSessionOp);
void settingChCB(eUse_StatusCode result, UseIpcMsg *pMsg, void *pClientContext);

#ifdef LOG
#error  LOG already defined
#endif
#define LOG(level, msg, args...)  dlog(DL_MSHRINK, level,"MSPMediaShrink:%s:%d " msg,  __FUNCTION__, __LINE__, ##args);
#define LOGE(msg, args...)  dlog(DL_MSHRINK, DLOGL_ERROR,"MSPMediaShrink:%s:%d " msg,  __FUNCTION__, __LINE__, ##args);
#define LOGN(msg, args...)  dlog(DL_MSHRINK, DLOGL_NORMAL,"MSPMediaShrink:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#define LOGX(msg, args...)  dlog(DL_MSHRINK, DLOGL_NOISE,"MSPMediaShrink:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#define LOGF(msg, args...)  dlog(DL_MSHRINK, DLOGL_FUNCTION_CALLS,"MSPMediaShrink:%s:%d " msg,  __FUNCTION__, __LINE__, ##args);
#define Close(x) do{dlog(DL_MSHRINK, DLOGL_MINOR_DEBUG,"%s:%s:%d ZZClose=%d",__FILE__,__FUNCTION__,__LINE__, x); close(x); (x)=-1;}while(0)

void fmtTime(int32_t iTime, char *cTime, int length)
{
    struct tm *tmp = NULL;
    cTime[0] = '\0';

    tmp = localtime((time_t *)&iTime);
    if (tmp != NULL)
    {

        if (strftime(cTime, length, "%D %T", tmp) == 0)
        {
            cTime[0] = '\0';
        }
    }
}

static const char* ms_error_string_simple(uint32_t err)
{
    if (eCpeUtilMediaShrinkSessionState_NotScheduled == err)
        return "NotScheduled";
    if (eCpeUtilMediaShrinkSessionState_Scheduled == err)
        return "Scheduled";
    if (eCpeUtilMediaShrinkSessionState_OpenPending == err)
        return "OpenPending";
    if (eCpeUtilMediaShrinkSessionState_ReencryptPending == err)
        return "ReencryptPending";
    if (eCpeUtilMediaShrinkSessionState_InProgress == err)
        return "InProgress";
    if (eCpeUtilMediaShrinkSessionState_Blocked == err)
        return "Blocked";
    if (eCpeUtilMediaShrinkSessionState_CommitPending == err)
        return "CommitPending";
    if (eCpeUtilMediaShrinkSessionState_Committed == err)
        return "Committed";
    if (eCpeUtilMediaShrinkSessionState_Failed == err)
        return "Failed";

    return "Undefined";
}
static const char* ms_error_string(uint32_t err)
{
    LOGF("Enter");
    if (eCpeUtilMediaShrinkSessionState_NotScheduled == err)
        return "eCpeUtilMediaShrinkSessionState_NotScheduled";
    //< Media Shrink request has been created by caller, but has not yet
    //< been scheduled.  This is the initial state for a user requested
    //< media shrink session.  Valid transitions from this state include
    //< Scheduled.
    if (eCpeUtilMediaShrinkSessionState_Scheduled == err)
        return "eCpeUtilMediaShrinkSessionState_Scheduled";
    //< Media Shrink request has been scheduled and is queued up waiting
    //< for other preceeding requests in the queue to complete before
    //< opening the media shrink session for background transcode.  Valid
    //< transitions from this state include OpenPending.
    if (eCpeUtilMediaShrinkSessionState_OpenPending == err)
        return "eCpeUtilMediaShrinkSessionState_OpenPending";
    //< Waiting for client to confirm Open session operation.  This is the
    //< initial state for a new media shrink session generated by media
    //< shrink's auto-scanning feature that was not scheduled in advance.
    //< Valid transitions from this state include InProgress, ReencryptPending,
    //< and Failed. Media Shrink will not transition session to this state
    //< while the originalFileUrl is still being recorded.
    if (eCpeUtilMediaShrinkSessionState_ReencryptPending == err)
        return "eCpeUtilMediaShrinkSessionState_ReencryptPending";
    //< Waiting for client to confirm Reencrypt session operation.
    //< Only applicable if kCpeUtilMediaShrinkFlags_UseClientSpecificEncryption
    //< flag was specified by client on Open
    if (eCpeUtilMediaShrinkSessionState_InProgress == err)
        return "eCpeUtilMediaShrinkSessionState_InProgress";
    //< Transcoding in progress
    if (eCpeUtilMediaShrinkSessionState_Blocked == err)
        return "eCpeUtilMediaShrinkSessionState_Blocked";
    //< Transcoding session has been paused temporarily due to
    //< resource contention.  It will resume on its own when the
    //< conflict is resolved.  PIP and HN streaming operations
    //< can result in entering this state.  Valid transitions from this
    //< state include InProgress, ReencryptPending, and Failed.
    if (eCpeUtilMediaShrinkSessionState_CommitPending == err)
        return "eCpeUtilMediaShrinkSessionState_CommitPending";
    //< Waiting for client to confirm Commit session operation.
    //< This occurs after InProgress when the transcode operation
    //< is complete and after processing SynchronousCleanup (if applicable) but before
    //< Committed or Failed.
    //< Caller must ensure that originalFileUrl is not being played before confirming
    if (eCpeUtilMediaShrinkSessionState_Committed == err)
        return "eCpeUtilMediaShrinkSessionState_Committed";
    //< Transcoded file has been committed.  This is a final state.
    //< Occurs after CommitPending in response to confirming
    //< eCpeUtilMediaShrinkSessionOp_Commit operation.
    if (eCpeUtilMediaShrinkSessionState_Failed == err)
        return "eCpeUtilMediaShrinkSessionState_Failed";
    //< Transcode operation or commit has failed.  This is a final state.
    //< Media shrink session can transition to this state from any
    //< other state except Not Scheduled and Scheduled.  A Failed session
    //< may be rescheduled via the cpeutil_mediashrink_request_Schedule()
    //< function.


    return "Undefined";
}

char* getInfo(char *srcFile, uint32_t &channel,  uint32_t &epgStart, uint32_t &epgDuration,  uint32_t &recStart, uint32_t &recDuration, uint32_t &assetId, bool &isRecording , char* episodeName)
{
    bool success = false;
    char query[LEN] = {0};
    static	char title[LEN] = {0};
    title[0] = '\0';
    assetId = 0;

    char *pos = NULL;
    pos = strrchr(srcFile, '/');
    if (pos == NULL)
    {
        pos = srcFile;
    }
    else
    {
        pos = &(pos[1]);
    }

    snprintf(query, LEN - 1, "SELECT RecordedAsset_ScheduledAsset_idDvrAsset FROM RecordedAssetFragments WHERE recordedAssetFragmentFilename = '%s'", pos);

    string rawQueryString(query);
    LOGF("MediaShrink query:%s", query);

    CDvrQueryResult *returnQueryResult;
    DB_RESULT dberr = DB_SUCCESS;

    dberr = CDvrDBFacade::getHandle().rawQuery(rawQueryString, returnQueryResult);

    if (dberr == DB_SUCCESS && returnQueryResult != NULL)
    {
        uint32_t recordCount = returnQueryResult->getRecordCount();
        if (recordCount)
        {
            const CDvrDataBaseRecord* record = returnQueryResult->getRecord(recordCount - 1);
            unsigned int fieldCount = record->getFieldCount();
            if (fieldCount)
            {
                assetId = (uint32_t)atol(record->getField(fieldCount - 1));
            }
        }
        if (assetId > 0)
        {
            RecdAssetAttr recdAssetAttr;
            dberr = CDvrDBFacade::getHandle().getAsset(assetId, recdAssetAttr);
            if (dberr == DB_SUCCESS)
            {
                char const *rec;
                if (recdAssetAttr.m_IsRecording) rec = "TRUE";
                else rec = "FALSE";
                LOGN("MediaShrink  assetId: %d title: %s isRecording %s", assetId, recdAssetAttr.m_Title, rec);
                strncpy(title, recdAssetAttr.m_Title, LEN - 1);
                strncpy(episodeName, recdAssetAttr.m_EpisodeName, MBTV_MAX_EPISODE_NAME - 1);
                epgStart = recdAssetAttr.m_EpgStartTime;
                epgDuration = recdAssetAttr.m_EpgDuration;
                recStart = recdAssetAttr.m_RecStartTime;
                recDuration = recdAssetAttr.m_RecDuration;
                channel = recdAssetAttr.m_ChannelNumber;
                isRecording = recdAssetAttr.m_IsRecording;
                success = true;
            }
            else
            {
                LOGE(" MediaShrink FAILURE");
            }
        }
        else
        {
            LOGE(" MediaShrink FAILURE");

        }
    }
    else if (dberr == DB_FAILURE)
    {
        LOGE(" MediaShrink DB_FAILURE");
    }
    else if (dberr == DB_NO_RECORDS)
    {
        LOGE(" MediaShrink DB_NO_RECORDS");
    }
    else if (dberr == DB_NETWORKTIMEOUT)
    {
        LOGE(" MediaShrink DB_NETWORKTIMEOUT");
    }
    else if (dberr == DB_BUSYRESOLVINGCONFLICT)
    {
        LOGE(" MediaShrink DB_BUSYRESOLVINGCONFLICT");
    }
    else
    {
        LOGE(" MediaShrink error %d", dberr);
    }
    if (returnQueryResult)
        delete returnQueryResult;

    if (success)
        return title;
    else
        return NULL;
}

// This is function is not being used since there is no shutdown of Mediashrink
void MSPMediaShrink::turnOffMediaShrink(tCpeUtilMediaShrinkSessionId sess_id, char *filename)
{
    LOGN(" MediaShrink TURNED OFF");

    int err = 0;
    if (mShrinkCurrentState)
    {
        if (sess_id > 0  && filename != NULL)
        {
            err = cpeutil_mediashrink_session_Confirm(sess_id, eCpeUtilMediaShrinkSessionOp_Close, filename, LEN);
            if (kCpe_NoErr != err)
            {
                LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm(Close) FAIL Error: %d %s", err, ms_error_string(err));
            }
            else
            {
                LOGN(" MediaShrink cpeutil_mediashrink_session_Confirm(Close) due to turn OFF ");
            }
        }

        uint32_t Enabled = 0;

        if (kCpe_NoErr != cpeutil_mediashrink_Set(eCpeUtilMediaShrinkNames_Enabled, &Enabled, sizeof(Enabled)))
        {
            LOGE(" MediaShrink disable failed");
        }
        else
        {
            LOGE(" MediaShrink disable success");
            mShrinkCurrentState = 0;
        }
    }
    pthread_mutex_lock(&mshrink_mutex);
    shrinking = false;
    processed.clear();
    pthread_mutex_unlock(&mshrink_mutex);

    return;
}

bool MSPMediaShrink::Initilize_MediaShrink()
{
    bool status 	= false;
    int  err	= 0;
    int  retStat	= kCpe_Err;

    if (kCpe_NoErr != (retStat = cpeutil_mediashrink_Init()))
    {
        // No recovery from this point
        LOGE("MediaShrink Initialize failed. Error Code: %d", retStat);
    }
    else
    {
        LOGN("MediaShrink Initialized");

        status = true;

        if (kCpe_NoErr != cpeutil_mediashrink_RegisterCallback(&mshrinkCallBack))
        {
            LOGE("MediaShrink register failed");
        }
        else
        {
            LOGN("MediaShrink register success");
        }
        // Set the media storage paths to include when scanning for content to transcode.
        // More than one path names are separated by commas.

        char buf[LEN];
        snprintf(buf, (sizeof(buf) - 1), DVR_PATH);
        err = cpeutil_mediashrink_Set(eCpeUtilMediaShrinkNames_IncludedPaths, buf, (strlen(DVR_PATH) + 1));
        if (kCpe_NoErr != err)
        {
            LOGE("MediaShrink Include Paths FAIL (%s) Error: %d %d %d %d %d", buf, err, kCpe_ParameterErr, kCpe_GetSetNameErr, kCpe_NotSupportedErr, kCpe_MemoryFullErr);
        }
        else
        {
            LOGN("MediaShrink enable Include Paths (%s)", buf);
        }

        // Set the threshold for minimum free space (value in percentage 0 - 100%) on target storage that must be
        // available after subtracting estimated size of the transcoded media file before
        // allowing a background transcode to start
        uint32_t Threshold = STOP_THRESHOLD ;
        if (kCpe_NoErr != cpeutil_mediashrink_Set(eCpeUtilMediaShrinkNames_StopThreshold, &Threshold, sizeof(Threshold)))
        {
            LOGE(" eCpeUtilMediaShrinkNames_StopThreshold failed");
        }
        else
        {
            LOGN(" eCpeUtilMediaShrinkNames_StopThreshold success");
        }
        // Set Low watermark threshold value (0-100%) for free space on target storage
        //that marks the point when background transcode must stop before falling
        //below this line.
        Threshold = START_THRESHOLD;
        if (kCpe_NoErr != cpeutil_mediashrink_Set(eCpeUtilMediaShrinkNames_StartThreshold, &Threshold, sizeof(Threshold)))
        {
            LOGE(" eCpeUtilMediaShrinkNames_StartThreshold failed");
        }
        else
        {
            LOGN(" eCpeUtilMediaShrinkNames_StartThreshold success");
        }
    }

    return status;
}

// This function would turn ON /OFF mediashrink but doesn't shutdown

void mediaShrinkChangeState(uint32_t Enable)
{
    uint32_t flag = Enable;
    // Check if the system is ready and also the current state is same as requested
    if (gIsSystemReady && mShrinkCurrentState != flag)
    {
        if (kCpe_NoErr != cpeutil_mediashrink_Set(eCpeUtilMediaShrinkNames_Enabled, &flag, sizeof(flag)))
        {
            LOGE(" MediaShrink State Change to %d failed", flag);
        }
        else
        {
            LOGE(" MediaShrink State Change to %d succeeded", flag);
            mShrinkCurrentState = flag;
        }
    }
}


void MSPMediaShrink::turnOnMediaShrink()
{

    LOGN(" MediaShrink TURN ON");

    uint32_t Enabled = 1;

    // Turn ON only when the no of recordings are less than the threshold.
    if (mNoOfRecSessions <= MAX_REC_SESSION_THRESHOLD)
    {
        if (!mShrinkCurrentState)
        {
            if (kCpe_NoErr != cpeutil_mediashrink_Set(eCpeUtilMediaShrinkNames_Enabled, &Enabled, sizeof(Enabled)))
            {
                LOGE(" MediaShrink enable failed");
            }
            else
            {
                LOGE(" MediaShrink enable success");
                mShrinkCurrentState = 1;
            }
        }
    }
    return;
}

int mshrinkCallBack(tCpeUtilMediaShrinkSessionId sess_id, tCpeUtilMediaShrinkSessionOp op)
{
    int err = 0;
    char srcFile[LEN] = {0};
    char destFile[LEN] = {0};
    uint32_t op_data = 0;

    uint32_t epgStart = 0;
    uint32_t epgDuration = 0;
    uint32_t recStart = 0;
    uint32_t recDuration = 0;
    uint32_t assetId = 0;
    char cRecStart[LEN] = {0};

    memset(srcFile, 0, LEN);
    memset(destFile, 0, LEN);


    LOGF(" MediaShrink entering");

    if (sess_id == kCpeUtilMediaShrink_InvalidSessionId)
    {
        LOGE(" MediaShrink bad session_id");
        return kCpe_NoErr;
    }
    if (op > eCpeUtilMediaShrinkSessionOp_Started)
    {
        LOGE(" MediaShrink unknown op");
        return kCpe_NoErr;
    }

    if (eCpeUtilMediaShrinkSessionOp_Open == op)
    {
        LOGN(" MediaShrink eCpeUtilMediaShrinkSessionOp_Open %d", sess_id);
        // triggered prior to starting new background transcode  session to give client opportunity to confirm or reject.
        // The path of the media file that will be transcoded during this session can be obtained via cpeutil_mediashrink_session_Get()
        // specifying eCpeUtilMediaShrinkSessionNames_OriginalFileUrl as the requested parameter name.
        // opSpecificData parameter in corresponding invocation of cpeutil_mediashrink_session_Confirm() is pointer to uint8_t
        // specifying transcode flags encoded as a bit mapped field. Supported flags include:\n
        // - kCpeUtilMediaShrinkFlags_UseNativeCamDvrEncryption - kCpeUtilMediaShrinkFlags_UseClientSpecificEncryption

        err = cpeutil_mediashrink_request_Get((tCpeUtilMediaShrinkRequestId)sess_id, eCpeUtilMediaShrinkRequestNames_SrcFileUrl, srcFile, LEN);
        if (kCpe_NoErr != err)
        {
            LOGE(" MediaShrink cpeutil_mediashrink_request_Get(eCpeUtilMediaShrinkRequestNames_SrcFileUrl) FAIL Error: %d %s", err, ms_error_string(err));
            return kCpe_NoErr;
        }
        else
        {
            LOGN(" MediaShrink eCpeUtilMediaShrinkRequestNames_SrcFileUrl %s", srcFile);
        }

        string s;
        s = srcFile;
        pthread_mutex_lock(&mshrink_mutex);
        // Check if the file has already been processed, which should not happen in general.
        iterMap iter = processed.find(s);
        if (iter != processed.end())
        {
            err = cpeutil_mediashrink_session_Confirm(sess_id, eCpeUtilMediaShrinkSessionOp_Close, srcFile, LEN);
            if (kCpe_NoErr != err)
            {
                LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm FAIL Error: %d %s", err, ms_error_string(err));
            }
            else
            {
                LOGN(" MediaShrink (eCpeUtilMediaShrinkSessionOp_Close) file found in processed map");
            }
            pthread_mutex_unlock(&mshrink_mutex);
            return kCpe_NoErr;
        }
        // Check if the same recording has already failed transcoding twice and if so skip.
        map<string, int32_t >::iterator iterAbort;
        iterAbort = aborted.find(srcFile);
        if (iterAbort != aborted.end())
        {
            if (MAX_RETRY_ON_FAILURE == iterAbort->second)
            {
                err = cpeutil_mediashrink_session_Confirm(sess_id, eCpeUtilMediaShrinkSessionOp_Close, srcFile, LEN);
                if (kCpe_NoErr != err)
                {
                    LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm FAIL Error: %d %s", err, ms_error_string(err));
                }
                else
                {
                    LOGE(" MediaShrink (eCpeUtilMediaShrinkSessionOp_Close) file found in aborted map with two or more failure/timeout");
                }
                pthread_mutex_unlock(&mshrink_mutex);
                return kCpe_NoErr;
            }
        }
        // Check if the bitrate of the stream false below the MIN_TRANSOCODING_BITRATE and if so
        // skip the transcoding
        map<string, double>::iterator iterLowBitRate;
        iterLowBitRate = lowBitRate.find(srcFile);
        if (iterLowBitRate != lowBitRate.end())
        {
            err = cpeutil_mediashrink_session_Confirm(sess_id, eCpeUtilMediaShrinkSessionOp_Close, srcFile, LEN);
            if (kCpe_NoErr != err)
            {
                LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm FAIL Error: %d %s", err, ms_error_string(err));
            }
            else
            {
                LOGE(" MediaShrink (eCpeUtilMediaShrinkSessionOp_Close) file found in lowBitRate map");
            }
            pthread_mutex_unlock(&mshrink_mutex);
            return kCpe_NoErr;
        }
        pthread_mutex_unlock(&mshrink_mutex);

        memset(currentFile, 0 , LEN);
        strncpy(currentFile, srcFile, LEN - 1);
        tries = 0;
        lastWritten = 0;
        lastSessId = sess_id;
        startTime = time(NULL);
        if (startTime < 0) startTime = 0;

        bool isRecording = true;
        char *title;
        currentTitle[0] = '\0';
        bzero(episodeName, MBTV_MAX_EPISODE_NAME);
        Mbps = 0.0;

        uint32_t sourceSize = 0;

        err = cpeutil_mediashrink_session_Get(sess_id, eCpeUtilMediaShrinkSessionNames_OriginalFileSizeInKb, &sourceSize, sizeof(sourceSize));
        if (kCpe_NoErr != err)
        {
            LOGE(" MediaShrink cpeutil_mediashrink_session_Get(eCpeUtilMediaShrinkSessionNames_OriginalFileSizeInKb) FAIL Error: %d %s", err, ms_error_string(err));
        }
        else
        {
            LOGN(" MediaShrink _session_Get(eCpeUtilMediaShrinkSessionNames_OriginalFileSizeInKb) = %u", sourceSize);
        }

        title = getInfo(srcFile,  channel, epgStart, epgDuration, recStart, recDuration, assetId, isRecording , episodeName);
        if (title)
        {
            strncpy(currentTitle, title, LEN - 1);
            fmtTime(epgStart, cEpgStart, LEN);
            fmtTime(recStart, cRecStart, LEN);

            Mbps = ((double) sourceSize / (double)recDuration) * 8.0 / 1024.0;

            LOGN(" MediaShrink title %s ,episode %s Channel: %d, epgStart:%s epgDuration:%d recStart:%s, recDuration:%d isRecording: %d source file size: %d %f Mbps", title, episodeName, channel, ctime((time_t*)&epgStart), epgDuration, ctime((time_t *)&recStart), recDuration, isRecording, sourceSize, Mbps);

        }
        // If it is still recording, it can not be transcoded.

        if (isRecording)
        {
            err = cpeutil_mediashrink_session_Confirm(sess_id, eCpeUtilMediaShrinkSessionOp_Close, srcFile, LEN);
            if (kCpe_NoErr != err)
            {
                LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm FAIL Error: %d %s", err, ms_error_string(err));
            }
            else
            {
                LOGN(" MediaShrink recording in progress skipping file %s", title);
            }
            return kCpe_NoErr;
        }
        // Determine if the recording is fragmented. Currently fragmented recordings are skipped.
        int fragmentCount = 0;
        vector<RecdAssetAttr> fragDetails;

        if (CDvrDBFacade::getHandle().getRecdAssetFragmentCount(assetId, fragmentCount, fragDetails) != DB_SUCCESS)
        {
            LOGE(" MediaShrink title %s, fragment LOOKUP FAILED", title);
        }
        fragmentCount = fragDetails.size();
        LOGN(" MediaShrink title %s, fragmentCount:%d", title, fragmentCount);

        if (fragmentCount > 1)
        {
            for (int frgCount = 0; frgCount < fragmentCount; frgCount++)
            {
                LOGN(" MediaShrink title %s, fragment:%s", title, fragDetails[frgCount].m_RecordingFileName);
            }

            int32_t filesize = 0;
            err = cpeutil_mediashrink_session_Get(sess_id, eCpeUtilMediaShrinkSessionNames_OriginalFileSizeInKb, &filesize, sizeof(filesize));
            if (kCpe_NoErr != err)
            {
                LOGE(" MediaShrink cpeutil_mediashrink_session_Get(eCpeUtilMediaShrinkSessionNames_OriginalFileSizeInKb) FAIL Error: %d %s", err, ms_error_string(err));
            }
            else
            {
                LOGN(" MediaShrink _session_Get(eCpeUtilMediaShrinkSessionNames_OriginalFileSizeInKb) = %u", filesize);
            }

            err = cpeutil_mediashrink_session_Confirm(sess_id, eCpeUtilMediaShrinkSessionOp_Close, srcFile, LEN);
            if (kCpe_NoErr != err)
            {
                LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm FAIL Error: %d %s", err, ms_error_string(err));
            }
            else
            {
                LOGN(" MediaShrink skipping fragmented file %s", title);

                map<string, int32_t >::iterator iterFrag;
                iterFrag = fragmented.find(srcFile);
                if (iterFrag == fragmented.end())
                {
                    fragmented.insert(pair<std::string, int32_t>(srcFile, 1));

                    char timestring[TIME_STRLEN] = {0};
                    time_t	tmVal = time(NULL);
                    fmtTime(tmVal, timestring, TIME_STRLEN);
                    FILE *log = NULL;
                    log = fopen("/rtn/mediashrink.fail", "a+");
                    string titlelog = (title != NULL ? title : "Not Available");
                    if (log)
                    {
                        fprintf(log, "%s |%s | %s | %s | %s | %d | %f | FRAGMENTED N/A\n", timestring, srcFile, titlelog.c_str(), episodeName, cEpgStart, channel, Mbps);

                        fclose(log);
                    }
                    else
                    {
                        LOGE(" Error in opening file /rtn/mediashrink.fail");
                    }


                }

                if (title)
                {
                    fmtTime(epgStart, cEpgStart, LEN);
                    fmtTime(recStart, cRecStart, LEN);
                    LOGN(" MediaShrink title %s, Channel %d, epgStart:%s epgDuration:%d recStart:%s, recDuration:%d", title, channel, cEpgStart, epgDuration, cRecStart, recDuration);
                }
                else
                {
                    title = "unknown";
                }

            }
            return kCpe_NoErr;
        }

        if (Mbps < MIN_TRANSOCODING_BITRATE)
        {
            err = cpeutil_mediashrink_session_Confirm(sess_id, eCpeUtilMediaShrinkSessionOp_Close, srcFile, LEN);
            if (kCpe_NoErr != err)
            {
                LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm FAIL Error: %d %s", err, ms_error_string(err));
            }
            else
            {
                LOGN(" MediaShrink skipping low bit rate file %s, %f", title, Mbps);


                if (title)
                {
                    fmtTime(epgStart, cEpgStart, LEN);
                    fmtTime(recStart, cRecStart, LEN);
                    LOGN(" MediaShrink title %s, Channel %d, epgStart:%s epgDuration:%d recStart:%s, recDuration:%d", title, channel, cEpgStart, epgDuration, cRecStart, recDuration);
                }
                else
                {
                    title = "unknown";
                }

            }
            lowBitRate.insert(pair<std::string, double>(srcFile, Mbps));

            char timestring[TIME_STRLEN] = {0};
            time_t	tmVal = time(NULL);
            fmtTime(tmVal, timestring, TIME_STRLEN);
            FILE *log = NULL;
            log = fopen("/rtn/mediashrink.fail", "a+");
            string titlelog = (title != NULL ? title : "Not Available");
            if (log)
            {
                fprintf(log, "%s |%s | %s | %s | %s | %d | %f |LOW BIT RATE  N/A\n", timestring, srcFile, titlelog.c_str(), episodeName, cEpgStart, channel, Mbps);

                fclose(log);
            }
            else
            {
                LOGE(" Error in opening file /rtn/mediashrink.fail");
            }

            return kCpe_NoErr;
        }

        //		Get/Set. pValue is a pointer to NUL terminated char buffer containing the URL of the media file created for storing the transcoded media.
        //		Default value is same as < eCpeUtilMediaShrinkRequestNames_SrcFileUrl.
        //		Value cannot be modified after user has confirmed the eCpeUtilMediaShrinkSessionOp_Open operation for the session
        err = cpeutil_mediashrink_request_Get((tCpeUtilMediaShrinkRequestId)sess_id, eCpeUtilMediaShrinkRequestNames_DestFileUrl, destFile, LEN);
        if (kCpe_NoErr != err)
        {
            LOGE(" MediaShrink cpeutil_mediashrink_request_Get(eCpeUtilMediaShrinkRequestNames_DestFileUrl) FAIL Error: %d %s", err, ms_error_string(err));
        }
        else
        {
            LOGN(" MediaShrink eCpeUtilMediaShrinkRequestNames_DestFileUrl %s", destFile);
        }

        err = cpeutil_mediashrink_session_Get(sess_id, eCpeUtilMediaShrinkSessionNames_State, &op_data, sizeof(op_data));
        if (kCpe_NoErr != err)
        {
            LOGE(" MediaShrink cpeutil_mediashrink_session_Get(eCpeUtilMediaShrinkSessionNames_State) FAIL Error: %d %s", err, ms_error_string(err));
        }
        else
        {
            LOGN(" MediaShrink _session_Get(eCpeUtilMediaShrinkSessionNames_State) = %u" , op_data);
        }
        // Check if a transcoding is in progress and if yes, discard. This should not happen
        // since RP would do transcoding in sequence.
        if (!shrinking)
        {
            uint8_t value[5];
            last_percent_complete = 0;

            value[0] = kCpeUtilMediaShrinkFlags_UseNativeCamDvrEncryption;
            value[1] = 0;
            value[2] = 0;
            value[3] = 0x1F;
            value[4] = 0xAC;
            err = cpeutil_mediashrink_session_Confirm(sess_id, eCpeUtilMediaShrinkSessionOp_Open, &value, 5);
            if (kCpe_NoErr != err)
            {
                LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm FAIL Error: %d %s", err, ms_error_string(err));
            }
            else
            {
                LOGN(" MediaShrink cpeutil_mediashrink_session_Confirm (eCpeUtilMediaShrinkSessionOp_Open)");
            }

            pthread_mutex_lock(&mshrink_mutex);
            processed.insert(pair<std::string, int32_t>(srcFile, TRANSCODE_INPROGRESS));
            shrinking = true;
            pthread_mutex_unlock(&mshrink_mutex);

            if (global_fd >= 0) write(global_fd, &sess_id, sizeof(uint32_t));

        }
        else
        {
            err = cpeutil_mediashrink_session_Confirm(sess_id, eCpeUtilMediaShrinkSessionOp_Close, srcFile, LEN);
            if (kCpe_NoErr != err)
            {
                LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm FAIL Error: %d %s", err, ms_error_string(err));
            }
            else
            {
                LOGN(" MediaShrink cpeutil_mediashrink_session_Confirm (eCpeUtilMediaShrinkSessionOp_Close)");
            }
        }
    }
    else if (eCpeUtilMediaShrinkSessionOp_Reencrypt == op)
    {
        // triggered after Open has been confirmed by client if kCpeUtilMediaShrinkFlags_UseClientSpecificEncryption flag
        // was specified.  This operation will not be triggered if kCpeUtilMediaShrinkFlags_UseNativeCamDvrEncryption flag was
        // specified on Open.  Client is responsible for setting up decrypt and encrypt session before confirming the session operation.
        // Program handle for stream to decrypt can be obtained via cpeutil_mediashrink_session_Get() specifying
        // eCpeUtilMediaShrinkSessionNames_DecodeProgramHandle as the requested parameter name.
        // Program handle for stream to encrypt can be obtained via cpeutil_mediashrink_session_Get()
        // specifying eCpeUtilMediaShrinkSessionNames_EncodeProgramHandle as the requested parameter name.
        // opSpecificData parameter is unused in corresponding invocation of cpeutil_mediashrink_session_Confirm()
        LOGN(" MediaShrink eCpeUtilMediaShrinkSessionOp_Reencrypt %d", sess_id);
    }
    else if (eCpeUtilMediaShrinkSessionOp_SynchronousCleanup == op)
    {
        // triggered after transcode process has completed or failed but before committing the transcoded file and/or closing the transcode session
        // if kCpeUtilMediaShrinkFlags_UseClientSpecificEncryption flag was specified on Open.  The client must synchronously release
        // its encrypt/decrypt resources for the specified media shrink session before returning from the callback.
        // This operation will not be
        // triggered if kCpeUtilMediaShrinkFlags_UseNativeCamDvrEncryption
        // flag was specified on Open.  No corresponding invocation of
        // cpeutil_mediashrink_session_Confirm() shall be performed by the client.
        // This operation must be synchronous in order to ensure support for
        // synchronous release of all transcode resources used for background
        // transcoding in the following scenarios:
        // - client performs cpeutil_mediashrink_Set() to explicitly Disable
        //   background transcode processing
        // - client performs cpeutil_mediashrink_Set() to notify media shrink
        //   that client is preparing to activate PIP
        // - client performs cpeutil_mediashrink_Session_Confirm() specifying
        //   eCpeUtilMediaShrinkSessionOp_Close to immediately close a session
        // - client performs cpeutil_mediashrink_Shutdown() to shutdown media
        //   shrink module
        // - media shrink cancels active session to avoid conflict with PIP,
        //   HN streaming, or real time transcoding.
        // - media shrink cancels active session due to file deletion
        LOGN(" MediaShrink eCpeUtilMediaShrinkSessionOp_SynchronousCleanup %d", sess_id);

    }
    else if (eCpeUtilMediaShrinkSessionOp_Commit == op)
    {
        //< triggered after transcode process is complete and
        //< encrypt/decrypt sessions have been closed to give client opportunity
        //< to control when the transcoded file should be committed.
        //< On commit, media shrink will rename the OriginalFileUrl to a temp
        //< file name, rename the completed TempXcodedFileUrl to OriginalFileUrl
        //< then delete the temp file name containing the original file.  If
        //< any of these rename operations fail, the commit will fail and the
        //< temp file name containing the original file will be renamed back
        //< to the OriginalFileUrl.
        //< Client must defer committing transcoded file until original file
        //< is no longer being played back.  Client is responsible for updating
        //< profile attribute for corresponding object in UPNP CDS after
        //< successfully committing the transcoded file
        //< opSpecificData parameter is unused in corresponding invocation of
        //< cpeutil_mediashrink_session_Confirm()
        LOGN(" MediaShrink eCpeUtilMediaShrinkSessionOp_Commit %d", sess_id);

        int32_t filesize = 0;
        err = cpeutil_mediashrink_session_Get(sess_id, eCpeUtilMediaShrinkSessionNames_OriginalFileSizeInKb, &filesize, sizeof(filesize));
        if (kCpe_NoErr != err)
        {
            LOGE(" MediaShrink cpeutil_mediashrink_session_Get(eCpeUtilMediaShrinkSessionNames_OriginalFileSizeInKb) FAIL Error: %d %s", err, ms_error_string(err));
        }
        else
        {
            LOGN(" MediaShrink _session_Get(eCpeUtilMediaShrinkSessionNames_OriginalFileSizeInKb) = %u", filesize);
        }


        int32_t newsize = 0;
        err = cpeutil_mediashrink_session_Get(sess_id, eCpeUtilMediaShrinkSessionNames_TempXcodedFileSizeInKb, &newsize, sizeof(newsize));
        if (kCpe_NoErr != err)
        {
            LOGE(" MediaShrink cpeutil_mediashrink_session_Get(eCpeUtilMediaShrinkSessionNames_TempXcodedFileSizeInKb) FAIL Error: %d %s", err, ms_error_string(err));
        }
        else
        {
            LOGN(" MediaShrink _session_Get(eCpeUtilMediaShrinkSessionNames_TempXcodedFileSizeInKb) = %u", newsize);
        }
        // Get the recording details
        err = cpeutil_mediashrink_request_Get((tCpeUtilMediaShrinkRequestId)sess_id, eCpeUtilMediaShrinkRequestNames_SrcFileUrl, srcFile, LEN);
        LOGN(" MediaShrink File: %s original size %u compressed size %u, ratio %f", srcFile, filesize, newsize, ((double)filesize / double(newsize)));

        stopTime = time(NULL);
        if (stopTime < 0) stopTime = startTime;
        int32_t deltaTime = stopTime - startTime;
        int32_t Hrs = deltaTime / 3600;
        int32_t Min = (deltaTime - Hrs * 3600) / 60;
        int32_t Sec = (deltaTime - Hrs * 3600 - Min * 60);

        double ratio = 0.0;
        double percent = 0.0;
        ratio = (double)filesize / (double)newsize;
        percent = ((double)newsize / (double)filesize) * 100.00;

        bool isRecording = false;
        char *title;
        bzero(episodeName, MBTV_MAX_EPISODE_NAME);


        title = getInfo(srcFile, channel, epgStart, epgDuration, recStart, recDuration, assetId, isRecording, episodeName);
        if (title)
        {
            fmtTime(epgStart, cEpgStart, LEN);
            fmtTime(recStart, cRecStart, LEN);

            LOGE(" MediaShrink title %s, episode %s, Channel %d, epgStart:%s epgDuration:%d recStart:%s, recDuration:%d", title, episodeName, channel, cEpgStart, epgDuration, cRecStart, recDuration);
        }
        else
        {
            title = "unknown";
        }
        //change the state of the recording to TRANSCODE_COMPLETE
        iterMap iter = processed.find(srcFile);
        if (iter != processed.end())
        {
            iter->second = TRANSCODE_COMPLETE;
        }
        // Checke if the recording is currently being played back. If yes put into deferred map.
        // When the rec. playback stops, it would get committed unless there is a reboot before that.
        if (isTheRecordingPlaying(assetId))
        {
            //Can't commit since the recording being played back. Defer it.
            stRecData_t recordingData;
            recordingData.isRecording = isRecording;
            recordingData.filesize = filesize;
            recordingData.newsize = newsize;
            recordingData.ratio = ratio;
            recordingData.percent = percent;
            recordingData.Hrs = Hrs;
            recordingData.Min = Min;
            recordingData.Sec = Sec;
            recordingData.epgDuration = epgDuration;
            recordingData.recDuration = recDuration;
            recordingData.channel = channel;
            recordingData.assetID = assetId ;
            recordingData.sess_Id = sess_id;
            recordingData.mbps = Mbps;

            bzero(recordingData.title, MBTV_MAX_EPISODE_NAME);
            bzero(recordingData.episodeName, MBTV_MAX_EPISODE_NAME);
            bzero(recordingData.cEpgStart, LEN);
            bzero(recordingData.cRecStart, LEN);

            snprintf(recordingData.title, (MBTV_MAX_EPISODE_NAME - 1), title);
            snprintf(recordingData.episodeName, (MBTV_MAX_EPISODE_NAME - 1), episodeName);
            snprintf(recordingData.cEpgStart, (LEN - 1), cEpgStart);
            snprintf(recordingData.cRecStart, (LEN - 1), cRecStart);

            pthread_mutex_lock(&mshrink_mutex);

            deferred.insert(pair<std::string, stRecData_t>(srcFile, recordingData));
            shrinking = false;

            pthread_mutex_unlock(&mshrink_mutex);

            LOGE(" MediaShrink Deferred Commit:title %s, episode %s, Channel %d, epgStart:%s epgDuration:%d recStart:%s, recDuration:%d", title, episodeName, channel, cEpgStart, epgDuration, cRecStart, recDuration);

            return kCpe_NoErr;

        }

        FILE *log = NULL;
        log = fopen("/rtn/mediashrink.txt", "a+");
        char timestring[TIME_STRLEN] = {0};
        time_t	tmVal = time(NULL);
        fmtTime(tmVal, timestring, TIME_STRLEN);
        if (log)
        {
            fprintf(log, "%s|%s|%u|%u|%f|%f%%|%02d:%02d:%02d|%s|%s|%s|%u|%s|%u|%d|%f\n", timestring, srcFile, filesize, newsize, ratio, percent, Hrs, Min, Sec, title, episodeName, cEpgStart, epgDuration, cRecStart, recDuration, channel, Mbps);
            fclose(log);
        }
        else
        {
            LOGE(" Error in opening file /rtn/mediashrink.txt");
        }


        err = cpeutil_mediashrink_session_Confirm(sess_id, eCpeUtilMediaShrinkSessionOp_Commit, NULL, 0);
        if (kCpe_NoErr != err)
        {
            LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm FAIL Error: %d %s", err, ms_error_string(err));
        }
        else
        {
            LOGN(" MediaShrink cpeutil_mediashrink_session_Confirm (eCpeUtilMediaShrinkSessionOp_Commit)");
        }

    }
    else if (eCpeUtilMediaShrinkSessionOp_Close == op)
    {
        //< triggered when media shrink session has entered a final state
        //< either due to successful commit or due to a failure encountered
        //< during the transcode/commit process. The corresponding
        //< tCpeUtilMediaShrinkSessionId will remain in scope until the client
        //< performs a corresponding cpeutil_mediashrink_session_Confirm()
        //< call for this operation or returns an error from the callback.
        //< This operation can also be initiated by the client.  The client may
        //< invoke this operation via cpeutil_mediashrink_session_Confirm() on an
        //< open session at any time regardless of the current session state
        //< to trigger immediate cleanup and closure of this session.  This operation
        //< is not valid for sessions in the Not Scheduled or Scheduled states.  If
        //< kCpeUtilMediaShrinkFlags_UseClientSpecificEncryption
        //< was specified on Open and the Reencrypt operation had already been
        //< invoked, then eCpeUtilMediaShrinkSessionOp_SynchronousCleanup
        //< operation will be invoked within the context of the
        //< cpeutil_mediashrink_session_Confirm() call.
        //< opSpecificData parameter is unused in corresponding invocation of
        //< cpeutil_mediashrink_session_Confirm()
        err = cpeutil_mediashrink_request_Get((tCpeUtilMediaShrinkRequestId)sess_id, eCpeUtilMediaShrinkRequestNames_SrcFileUrl, srcFile, LEN);
        LOGN(" MediaShrink eCpeUtilMediaShrinkSessionOp_Close %d %s", sess_id, srcFile);

        err = cpeutil_mediashrink_session_Get(sess_id, eCpeUtilMediaShrinkSessionNames_State, &op_data, sizeof(op_data));
        if (kCpe_NoErr != err)
        {
            LOGE(" MediaShrink cpeutil_mediashrink_session_Get(eCpeUtilMediaShrinkSessionNames_State) FAIL Error: %d %s", err, ms_error_string(err));
        }
        else
        {
            LOGN(" MediaShrink _session_Get(eCpeUtilMediaShrinkSessionNames_State) = %u", op_data);
        }

        // This will be continuously re-requested
        if (op_data == eCpeUtilMediaShrinkSessionState_Failed)
        {

            pthread_mutex_lock(&mshrink_mutex);

            iterMap iter = processed.find(srcFile);
            if (iter != processed.end())
            {
                processed.erase(iter);
            }

            // Check if this file has already failed for two times
            // Now update the aborted map with the failure count
            int32_t failCount = 1;
            map<string, int32_t >::iterator iterAbort;
            iterAbort = aborted.find(srcFile);
            if (iterAbort != aborted.end())
            {
                iterAbort->second++;
            }
            else
            {
                aborted.insert(pair<std::string, int32_t>(srcFile, failCount));
            }
            pthread_mutex_unlock(&mshrink_mutex);
            LOGN(" MediaShrink eCpeUtilMediaShrinkSessionOp_Close SESSION_ID: %d File: %s (%s) is in FAILED STATE %d%% complete", sess_id, srcFile, currentFile, last_percent_complete);

            err = cpeutil_mediashrink_session_Confirm(sess_id, eCpeUtilMediaShrinkSessionOp_Close, srcFile, LEN);
            if (kCpe_NoErr != err)
            {
                LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm FAIL Error: %d %s", err, ms_error_string(err));
            }
            else
            {
                LOGN(" MediaShrink cpeutil_mediashrink_session_Confirm (eCpeUtilMediaShrinkSessionOp_Close)");
            }
            pthread_mutex_lock(&mshrink_mutex);
            shrinking = false;
            pthread_mutex_unlock(&mshrink_mutex);



            char timestring[TIME_STRLEN] = {0};
            time_t	tmVal = time(NULL);
            fmtTime(tmVal, timestring, TIME_STRLEN);
            FILE *log = NULL;
            log = fopen("/rtn/mediashrink.fail", "a+");
            if (log)
            {
                fprintf(log, "%s |%s | %s | %s | %s | %d | %f | FAILED at %d%%\n", timestring, currentFile, currentTitle, episodeName, cEpgStart, channel, Mbps, last_percent_complete);

                fclose(log);
            }
            else
            {
                LOGE(" Error in opening file /rtn/mediashrink.fail");
            }


            return kCpe_NoErr;
        }
        else
        {
            err = cpeutil_mediashrink_session_Get(sess_id, eCpeUtilMediaShrinkSessionNames_OriginalFileSizeInKb, &op_data, sizeof(op_data));
            if (kCpe_NoErr != err)
            {
                LOGE(" MediaShrink cpeutil_mediashrink_session_Get(eCpeUtilMediaShrinkSessionNames_OriginalFileSizeInKb) FAIL Error: %d %s", err, ms_error_string(err));
            }
            else
            {
                LOGN(" MediaShrink _session_Get(eCpeUtilMediaShrinkSessionNames_OriginalFileSizeInKb) = %u", op_data);
            }

            err = cpeutil_mediashrink_session_Get(sess_id, eCpeUtilMediaShrinkSessionNames_RequestId, &op_data, sizeof(op_data));
            if (kCpe_NoErr != err)
            {
                LOGE(" MediaShrink cpeutil_mediashrink_session_Get(eCpeUtilMediaShrinkSessionNames_RequestId) FAIL Error: %d %s", err, ms_error_string(err));
            }
            else
            {
                LOGN(" MediaShrink _session_Get(eCpeUtilMediaShrinkSessionNames_RequestId) = %u", op_data);
            }

            err = cpeutil_mediashrink_session_Confirm(sess_id, eCpeUtilMediaShrinkSessionOp_Close, srcFile, LEN);
            if (kCpe_NoErr != err)
            {
                LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm FAIL Error: %d %s", err, ms_error_string(err));
            }
            else
            {
                LOGN(" MediaShrink cpeutil_mediashrink_session_Confirm (eCpeUtilMediaShrinkSessionOp_Close)");
            }
        }
        pthread_mutex_lock(&mshrink_mutex);
        shrinking = false;
        pthread_mutex_unlock(&mshrink_mutex);
        lastWritten = 0;

    }
    else if (eCpeUtilMediaShrinkSessionOp_Started == op)
    {
        //< This is informative only, requiring no confirmation back.
        //< This signals to the MW that the media shrink transcoding session has begun
        //< and that the meta-data has been propagated/updated for
        //< eCpeUtilMediaShrinkSessionNames_TempXcodedFileUrl accordingly.  This is
        //< also be the indication to the MW that it can now make any required updates
        //< to the meta-data for the transcoded recording using the
        //< cpeutil_mediashrink_UpdateRecordingMetaData() function.
        LOGN(" MediaShrink eCpeUtilMediaShrinkSessionOp_Started %d", sess_id);
        if (shrinking && global_fd >= 0) write(global_fd, &sess_id, sizeof(uint32_t));

    }
    else
    {
        LOGE(" MediaShrink eCpeUtilMediaShrinkSessionOp UNKNOWN %d", sess_id);
    }

    LOGN(" MediaShrink exiting");

    return kCpe_NoErr;
}

static void *MediaShrinkthreadEntry(void *ctx)
{
    FNLOG(DL_MSHRINK);

    MSPMediaShrink *inst = (MSPMediaShrink *)ctx;

    return inst->dispatchLoop();
}

void *MSPMediaShrink::dispatchLoop()
{
    FNLOG(DL_MSHRINK);
    bool 	exitThisThread 	= false;
    int  	status		= 0;
    struct 	pollfd pollfds[2];
    int 	timeout = 15 * ONE_SECOND;
    int 	err		= 0;
    struct 	stat filestatus;


    // Wait till system is ready before enbaling mediashrink
    while (!gIsSystemReady)
    {
        LOGE("Waiting for  \"/tmp/system_ready\"");

        if (0 == stat("/tmp/system_ready", &filestatus))
        {
            gIsSystemReady = true;
        }
        else
        {
            status = poll(NULL, 0, 30 * ONE_SECOND);
        }
    }
    gIsSystemReady = true;


    // Create a log file if it doesn't exist and add headers. This would capture successful trancodings

    struct stat logBuf;
    err = stat("/rtn/mediashrink.txt", &logBuf) ;
    if (err < 0)
    {
        FILE *log = NULL;
        log = fopen("/rtn/mediashrink.txt", "a+");
        if (log)
        {
            fprintf(log, "TimeOfTranscode|Source|Original Size|Compressed Size|Ratio|Percent|Elapsed Time|Title|EpisodeName|EPG Date|Epg Duration|Recording Start|Recording Duration|Channel|BitRate\n");
            fclose(log);
        }
        else
        {
            LOGE("Error in opening file /rtn/mediashrink.txt");
        }
    }
    // Create a log file if it doesn't exist and add headers. This would capture three different
    // scenarios (1) Skipped Transcoding due to fragmented Recording (2) TimeOut (3) Failure
    // (2) & (3) are error scenarios but (1) is as designed
    err = stat("/rtn/mediashrink.fail", &logBuf) ;
    if (err < 0)
    {
        FILE *log = NULL;
        log = fopen("/rtn/mediashrink.fail", "a+");
        if (log)
        {
            fprintf(log, "TimeOfFailure/Skip|Source|Title|Episode|EPG Date|Channel|Bitrate|Reason\n");
            fclose(log);
        }
        else
        {
            LOGE("Error in opening file /rtn/mediashrink.fail");
        }

    }

    // Initialize the encoder-sub modules
    err = cpe_encodersrc_Init();
    if (err)
    {
        LOGE(" MediaShrink Encoder Source Init Failed %d (%d)",  err, errno);
    }
    else
    {
        LOGE(" MediaShrink Encoder Source Init Success %d (%d)", err, errno);
    }

    status = poll(NULL, 0, 60 * ONE_SECOND); // wait 1 minute for autostart

    turnOnMediaShrink();
    gIsInitComplete = true;


    if (m_localReadFd == -1)
    {
        LOGE("Error m_localReadFd == -1");
        return NULL;             // local socket not set up -- this is an error
    }

    LOGE("MediaShrink Poll Loop fd:%d fd:%d", m_localReadFd, m_localWriteFd);

    timeout = 15 * ONE_SECOND;              // default is no timeout
    while (!exitThisThread)
    {
        pollfds[0].fd = m_localReadFd;
        pollfds[0].events = POLLIN;
        pollfds[0].revents = 0;

        pollfds[1].fd = m_localWriteFd;
        pollfds[1].events = POLLIN;
        pollfds[1].revents = 0;

        int32_t sess_id = -1;

        status = poll(pollfds, 2, timeout);
        if (status == 0)
        {
            LOG(DLOGL_NOISE, "  WARNING: MediaShrink poll returned timeout with timeout being configured value as %d shrinking: %d", timeout, shrinking);
            timeout = 60 * ONE_SECOND; // reset timeout after this one
        }
        else if (status < 0)
        {
            LOG(DLOGL_ERROR, "  MediaShrink poll returned error: %d", errno);
        }
        else
        {
            if (pollfds[0].revents & POLLIN)
            {
                status = read(m_localReadFd, &sess_id, sizeof(sess_id));
                if (status < 0)
                {
                    LOG(DLOGL_ERROR, " MediaShrink reading from MediaShrink (fd) socket: wanted: %d, err:%d", sizeof(sess_id), errno);
                }
                else
                {
                    //If there is an active shrinking get the progress after 30 secs.
                    if (shrinking)
                    {
                        if (status < (int)sizeof(sess_id))
                        {
                            LOGE("  MediaShrink reading from MediaShrink (fd) socket: wanted %d, read %d ", sizeof(sess_id), status);
                        }
                        else
                        {
                            LOGX(" MediaShrink read from MediaShrink (fd) socket: wanted: %d, read: %d sess_id=%d", sizeof(sess_id), status, sess_id);
                        }

                        poll(NULL, 0, 30 * ONE_SECOND);

                        uint8_t percent_comp = 0;
                        uint32_t written = 0;
                        // Query the trancode progress
                        err = cpeutil_mediashrink_session_Get((tCpeUtilMediaShrinkRequestId)sess_id, eCpeUtilMediaShrinkSessionNames_PercentComplete, &percent_comp, sizeof(percent_comp));
                        if (kCpe_NoErr != err)
                        {
                            LOG(DLOGL_ERROR, " MediaShrink eCpeUtilMediaShrinkSessionNames_PercentComplete FAIL Error: %d %s", err, ms_error_string(err));
                        }
                        else
                        {
                            last_percent_complete = percent_comp;
                            err = cpeutil_mediashrink_session_Get((tCpeUtilMediaShrinkRequestId)sess_id, eCpeUtilMediaShrinkSessionNames_TempXcodedFileSizeInKb, &written, sizeof(written));
                            if (kCpe_NoErr == err)
                            {
                                tCpeUtilMediaShrinkSessionState state;
                                err = cpeutil_mediashrink_session_Get((tCpeUtilMediaShrinkRequestId)sess_id, eCpeUtilMediaShrinkSessionNames_State, &state, sizeof(state));
                                if (state == eCpeUtilMediaShrinkSessionState_Blocked)
                                {
                                    LOG(DLOGL_ERROR, " MediaShrink eCpeUtilMediaShrinkRequestNames_PercentComplete %s %u %d%% BLOCKED", currentTitle, written, percent_comp);
                                }
                                else
                                {
                                    LOGN(" MediaShrink of \"%s\" session: %d in progress %u::>%u %s %d%%", currentTitle, sess_id, lastWritten, written, ms_error_string(state), percent_comp);
                                }
                                // Check if the progress is stalled for last 30 secs and if this continues for
                                // MAX_TIMEOOUT_ITERATION times
                                // timeout and close the file if it is not in blocked state
                                if (written <= lastWritten && (eCpeUtilMediaShrinkSessionState_Blocked != state))
                                {
                                    tries++;
                                    if (tries > MAX_TIMEOOUT_ITERATION)
                                    {
                                        LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm(Close) due to inactivity for : %s", currentFile);

                                        err = cpeutil_mediashrink_session_Confirm(sess_id, eCpeUtilMediaShrinkSessionOp_Close, currentFile, LEN);
                                        if (kCpe_NoErr != err)
                                        {
                                            LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm(Close) FAIL Error: %d %s", err, ms_error_string(err));
                                        }
                                        else
                                        {
                                            LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm(Close) due to in activity (eCpeUtilMediaShrinkSessionOp_Close)");
                                        }
                                        pthread_mutex_lock(&mshrink_mutex);
                                        iterMap iter = processed.find(currentFile);
                                        if (iter != processed.end())
                                        {
                                            processed.erase(iter);
                                        }
                                        // Now update the aborted map with the failure count
                                        shrinking = false;
                                        int32_t failCount = 1;
                                        map<string, int32_t >::iterator iterAbort;
                                        iterAbort = aborted.find(currentFile);
                                        if (iterAbort != aborted.end())
                                        {
                                            iterAbort->second++;
                                        }
                                        else
                                        {
                                            aborted.insert(pair<std::string, int32_t>(currentFile, failCount));
                                        }
                                        pthread_mutex_unlock(&mshrink_mutex);

                                        timeout = 10 * ONE_SECOND;
                                        char timestring[TIME_STRLEN] = {0};
                                        time_t	tmVal = time(NULL);
                                        fmtTime(tmVal, timestring, TIME_STRLEN);
                                        FILE *log = NULL;
                                        log = fopen("/rtn/mediashrink.fail", "a+");
                                        if (log)
                                        {
                                            fprintf(log, "%s |%s | %s | %s | %s | %d | %f | TimeOut(%s) at %d%%\n", timestring, currentFile, currentTitle, episodeName, cEpgStart, channel, Mbps, ms_error_string_simple(state), last_percent_complete);
                                            fclose(log);
                                        }
                                        else
                                        {
                                            LOGE("Error in opening file /rtn/mediashrink.fail");
                                        }

                                        tries = 0;
                                        continue;
                                    }
                                }
                                else
                                {
                                    tries = 0;
                                }

                                lastWritten = written;

                                if (shrinking && global_fd >= 0) write(global_fd, &sess_id, sizeof(uint32_t));
                            }
                            else
                            {
                                LOGE(" MediaShrink eCpeUtilMediaShrinkSessionNames_TempXcodedFileSizeInKb FAIL Error: %d %s", err, ms_error_string(err));
                            }
                        }
                    }
                }
            }

            if (pollfds[1].revents & POLLHUP)
            {
                dlog(DL_MSHRINK, DLOGL_EMERGENCY, " ERROR: MediaShrink Went down. Should be rare.. No recovery for MShrink.pollfds[1].revents = %d", pollfds[1].revents);
            }

            if (pollfds[1].revents & POLLIN)
            {
                LOGE(" MediaShrink unexpected read on (fd) write socket: wanted: %d, err:%d", sizeof(sess_id), errno);

            }

        }
    }
    return NULL;
}
MSPMediaShrink::MSPMediaShrink()
{
    FNLOG(DL_MSHRINK);
    int thread_retvalue = 1; //Setting to nonzero value to indicate error
    pthread_attr_t attr;
    int status  = -1;
    int pipeFds[2];

    gIsSystemReady = false;
    mNoOfRecSessions = 0;
    mPipEnabled = 0;
    dispatchThread = -1;
    m_localReadFd = -1;
    m_localWriteFd = -1;

    pthread_mutex_init(&mshrink_mutex, NULL);
    // setup local fd for communication with dispatch thread

    status = pipe(pipeFds);
    if (status != 0)
    {
        LOGE("MediaShrink could not create internal pipe:%d", status);
        return;
    }

    m_localReadFd 	= pipeFds[0];
    m_localWriteFd 	= pipeFds[1];
    global_fd 		= m_localWriteFd;

    LOGN("call pipe: m_localReadFd ZZOpen=%d", m_localReadFd);
    LOGN("call pipe: m_localWriteFd ZZOpen=%d", m_localWriteFd);

    if (!Initilize_MediaShrink())
    {
        LOG(DLOGL_ERROR, "ERROR  In Mediashrink init ");
        return;
    }


    // create and start our thread
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024); // this should be way more than we need
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    thread_retvalue = pthread_create(&dispatchThread, &attr, MediaShrinkthreadEntry, (void *)this);

    if (thread_retvalue)
    {
        LOGE("ERROR: In creating Mediashrink thread: %d", thread_retvalue);
        return;
    }
    else
    {
        LOGX("Launched Mediashrink Thread\n");
    }

    thread_retvalue = pthread_setname_np(dispatchThread, "Mediashrink Thread");
    if (0 != thread_retvalue)
    {
        LOGE("Mediashrink thread naming failed:%d", thread_retvalue);
    }


}

MSPMediaShrink * MSPMediaShrink::getMediaShrinkInstance(bool globalEnable)
{
    FNLOG(DL_MSHRINK);

    // Create the object only when "globalEnable" is true which happens
    // when this method is called from Msp_Mediashrink_Init()
    if ((m_pInstance == NULL) && globalEnable)
    {
        m_pInstance = new MSPMediaShrink();
    }

    return m_pInstance;
}


bool Msp_Mediashrink_Init()
{
    char enableMShrink[MAX_SETTING_SIZE] = {0};
    int  settingVal = 0;
    bool retStat = false;

    eUseSettingsLevel settingLevel = eUseSettingsLevel_BuiltInDef;
    // Read the unified the settings to determine if mediashrink to be enabled or not.
    if (USE_RESULT_OK == Uset_getSettingT(NULL, MEDIASHRINK_SETTING , MAX_SETTING_SIZE, enableMShrink, &settingLevel))
    {
        settingVal = atoi(enableMShrink);
        LOGE("(Not an Error:Value of ciscoSg/rec/enableMediashrink : %d string value: %s", settingVal, enableMShrink);
    }
    else
    {
        LOGE("Failed to get the setting ciscoSg/rec/enableMediashrink. Mediashrink will not be intialized");
    }
    // If the setting value is 1 then initialize mediashrink
    if (MEDIASHRINK_ENABLED == settingVal)
    {
        if (MSPMediaShrink::getMediaShrinkInstance(true))
        {
            retStat = true;
        }
    }
    return retStat;
}

MSPMediaShrink::~MSPMediaShrink()
{
    FNLOG(DL_MSHRINK);


    if (dispatchThread != (pthread_t) - 1)
    {
        pthread_join(dispatchThread, NULL);
    }
    else
    {
        LOGE("Mediashrink not created.so.skipping waiting for that thread to exit");
    }



    pthread_mutex_destroy(&mshrink_mutex);

}


bool MSPMediaShrink::deleteRecording(const char* fileToDelete)
{
    map<string, int32_t >::iterator iterMap;
    string fDelete = AVFS_FILE_PREFIX;
    fDelete.append(fileToDelete);


    if (0 == cpeutil_mediashrink_wrap_Delete(fileToDelete))
    {
        LOGE("Deleted Recording %s using Mediashrink Utility", fileToDelete);

        pthread_mutex_lock(&mshrink_mutex);
        iterMap = processed.find(fDelete.c_str());
        if (iterMap != processed.end())
        {
            // Check if transcoding is in progress then set the state
            // to false
            if (TRANSCODE_INPROGRESS == iterMap->second)
            {
                LOGE("Deleted Recording %s is TRANSCODE_INPROGRESS (%d) ", fileToDelete, iterMap->second);
                shrinking = false;
            }
            processed.erase(iterMap);
        }
        pthread_mutex_unlock(&mshrink_mutex);

        return true;
    }
    else
    {
        LOGE("Failed to delete Recording %s using Mediashrink Utility", fileToDelete);
        return false;
    }

}

bool Msp_Mediashrink_Delete_Recording(const char* fileToDelete)
{
    MSPMediaShrink* mshkObj = NULL;
    bool retStat = false;

    if ((mshkObj = MSPMediaShrink::getMediaShrinkInstance()))
    {
        retStat = mshkObj->deleteRecording(fileToDelete);
    }
    else
    {
        // Mediashrink is not active use remove() directly using system calls
        if (remove(fileToDelete))
        {
            LOGE("Failed to delete DVR file %s using remove()", fileToDelete);
        }
        else
        {
            LOGE("Successfully Deleted file %s using remove()", fileToDelete);
            retStat = true;

        }
    }

    return retStat;

}
void MSPMediaShrink::PipNotification(uint32_t Enabled)
{
    FNLOG(DL_MSHRINK);
    if (mPipEnabled != Enabled)
    {
        if (kCpe_NoErr != cpeutil_mediashrink_Set(eCpeUtilMediaShrinkNames_PipActive, &Enabled, sizeof(Enabled)))
        {
            LOG(DLOGL_ERROR, " MediaShrink PIP set failed for Enabled: %d", Enabled);
        }
        else
        {
            LOG(DLOGL_ERROR, " MediaShrink PIP set succeeded for Enabled: %d", Enabled);
            mPipEnabled = Enabled;
        }
    }

}

void MSPMediaShrink::RecPlayBackStartNotification()
{
    uint32_t noOfSessions = 0;
    uint32_t Enabled = 0;

    FNLOG(DL_MSHRINK);

    noOfSessions = MSPMediaShrink::getMediaShrinkInstance()->mNoOfRecSessions;
    LOGN("noOfSessions = %d ", noOfSessions);
    if (noOfSessions >= MAX_REC_SESSION_THRESHOLD - 1)
    {
        LOGN("inside noOfSessions check ");
        // If initilization not complete no action
        if (gIsInitComplete)
        {
            LOGN(" MediaShrink Disable on one recording playback and one recording progress ");
            mediaShrinkChangeState(Enabled);
        }
    }

}


void MSPMediaShrink::RecStartNotification()
{
    FNLOG(DL_MSHRINK);
    handleRecNotification(true);
}

void MSPMediaShrink::RecStopNotification()
{
    FNLOG(DL_MSHRINK);
    handleRecNotification(false);

}

void MSPMediaShrink::RecPlayBackStopNotification()
{
    uint32_t noOfSessions = 0;
    uint32_t Enabled = 0;
    FNLOG(DL_MSHRINK);
    // Playback of a recording stopped. Now check if any of transcode commit
    // was deferred and if there, complete the commit
    pthread_mutex_lock(&mshrink_mutex);

    commitDeferred();

    pthread_mutex_unlock(&mshrink_mutex);

    noOfSessions = MSPMediaShrink::getMediaShrinkInstance()->mNoOfRecSessions;
    LOGN(" noOfSessions = %d ", noOfSessions);
    if (noOfSessions <= MAX_REC_SESSION_THRESHOLD)
    {
        LOGN("inside noOfSessions check ");
        // If initilization not complete no action
        if (gIsInitComplete)
        {
            LOGN(" MediaShrink Enable on recording playback stop and one/two recordings in progress");
            mediaShrinkChangeState(!Enabled);
        }
    }

}

void Msp_Mediashrink_Rec_Play_Stop_Notify()
{
    FNLOG(DL_MSHRINK);

    if (MSPMediaShrink::getMediaShrinkInstance())
    {
        pthread_mutex_lock(&mshrink_mutex);

        commitDeferred();

        pthread_mutex_unlock(&mshrink_mutex);
    }

}

void MSPMediaShrink::handleRecNotification(bool recState)
{
    FNLOG(DL_MSHRINK);
    uint32_t noOfSessions = 0;
    uint32_t Enabled = 0;

    if (recState)
    {
        LOGN(" MediaShrink Recording START Event ");
        noOfSessions = ++(MSPMediaShrink::getMediaShrinkInstance()->mNoOfRecSessions);

        if (noOfSessions > MAX_REC_SESSION_THRESHOLD)
        {
            // If initilization not complete no action
            if (gIsInitComplete)
            {
                LOGN(" MediaShrink Disable on recording start ");
                mediaShrinkChangeState(Enabled);
            }
        }

    }
    else
    {
        LOGN(" MediaShrink Recording STOP Event ");
        noOfSessions = MSPMediaShrink::getMediaShrinkInstance()->mNoOfRecSessions;

        if (noOfSessions)
        {
            noOfSessions = --(MSPMediaShrink::getMediaShrinkInstance()->mNoOfRecSessions);
            if (noOfSessions <= MAX_REC_SESSION_THRESHOLD)
            {
                // If initilization not complete no action
                if (gIsInitComplete)
                {
                    LOGN(" MediaShrink Enable on recording stop ");
                    mediaShrinkChangeState(!Enabled);
                }
            }
        }

    }
    LOGX(" MediaShrink No of Recording Sessions %d", noOfSessions);


}
uint32_t isTheRecordingPlaying(uint32_t assetID)
{
    DvrRecordedProgram* pRecordedProg = NULL;
    eDvrRecordedProgram_Status status = kDvrRecordedProgram_Failure;
    uint32_t isPlaying = false;
    DvrAssetId assetRecIdStr = {'\0'};

    memset(assetRecIdStr, 0, sizeof(DvrAssetId));
    snprintf((char *)assetRecIdStr, Dvr_UniqueId_Size, "%.36s%08u", LOCAL_UUID_STR, assetID);

    /*Get associated Recorded program pointer from the asset id available with us*/
    eDvr_Status result = DvrRecordedProgram_Lookup(assetRecIdStr, &pRecordedProg);

    if (kDvr_Failure == result)
    {
        LOGE(" MediaShrink AssetID: %s not found, should NEVER be here!!!", assetRecIdStr);
    }
    else if (kDvr_Success == result)
    {
        status = DvrRecordedProgram_GetBoolean(pRecordedProg, kDvrApiIsPlayback, &isPlaying);
        if (kDvrRecordedProgram_Success == status)
        {
            LOGN(" MediaShrink isPlaying Status %d for assetID: %s !!!", isPlaying, assetRecIdStr);
        }
        else
        {
            LOGE(" MediaShrink FAILED to get isPlaying Status assetID: %s !!!", assetRecIdStr);
        }

    }

    if (pRecordedProg)
    {
        DvrRecordedProgram_Finalize(&pRecordedProg);
    }

    return isPlaying;
}

void commitDeferred()
{
    int err = 0;
    for (std::map<std::string, stRecData_t>::iterator itDef = deferred.begin(); itDef != deferred.end();)
    {

        // Check if the recording is playing like streaming on home network
        if (!isTheRecordingPlaying(itDef->second.assetID))
        {

            LOGN(" MediaShrink Deferred Commit:title %s, episode %s, Channel %d, epgStart:%s epgDuration:%d recStart:%s, recDuration:%d", itDef->second.title, itDef->second.episodeName, itDef->second.channel, itDef->second.cEpgStart, itDef->second.epgDuration, itDef->second.cRecStart, itDef->second.recDuration);


            err = cpeutil_mediashrink_session_Confirm(itDef->second.sess_Id, eCpeUtilMediaShrinkSessionOp_Commit, NULL, 0);
            if (kCpe_NoErr != err)
            {
                LOGE(" MediaShrink cpeutil_mediashrink_session_Confirm FAIL Error: %d %s", err, ms_error_string(err));
            }
            else
            {
                LOGN(" MediaShrink cpeutil_mediashrink_session_Confirm (eCpeUtilMediaShrinkSessionOp_Commit)");
                FILE *log = NULL;
                log = fopen("/rtn/mediashrink.txt", "a+");
                char timestring[TIME_STRLEN] = {0};
                time_t	tmVal = time(NULL);
                fmtTime(tmVal, timestring, TIME_STRLEN);
                if (log)
                {
                    fprintf(log, "%s|%s|%u|%u|%f|%f%%|%02d:%02d:%02d|%s|%s|%s|%u|%s|%u|%d|%f\n", timestring, itDef->first.c_str(), itDef->second.filesize, itDef->second.newsize, itDef->second.ratio, itDef->second.percent, itDef->second.Hrs, itDef->second.Min, itDef->second.Sec, itDef->second.title, itDef->second.episodeName, itDef->second.cEpgStart, itDef->second.epgDuration, itDef->second.cRecStart, itDef->second.recDuration, itDef->second.channel, itDef->second.mbps);
                    fclose(log);
                }
                else
                {
                    LOGE("Error in opening file /rtn/mediashrink.txt");
                }

                deferred.erase(itDef++);
                continue;
            }
        }
        else
        {
            LOGN(" Recording is still playing. Can't commit now");
        }
        ++itDef;
    }

}

#endif //#if (ENABLE_MSPMEDIASHRINK == 1)
