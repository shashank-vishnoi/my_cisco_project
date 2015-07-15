/** @file MRDvrServer.h
 *
 * @author Amit Patel
 * @date 02-21-2011
 *
 * @version 1.0
 *
 * @brief The MRDVR server header file.
 *
 * This module:
 * -- support MRDVR server initialization.
 * -- support to register a callback for new PlayBack/TearDown request from client.
 */

#ifndef _MRDVR_SERVER_H_
#define _MRDVR_SERVER_H_

#include <string>
#include <list>
#include <cpe_error.h>
#include <cpe_source.h>
#include "cpe_hncontentmgr_cds.h"
#include "cpe_hnservermgr.h"
#include "eventQueue.h"
#include "Cam.h"
#include "IMediaStreamer.h"
#include "IMediaPlayerSession.h"
#include "csci-msp-mrdvrsrv-api.h"
#include "MspCommon.h"
#include "CDvrPriorityMediator.h"
#include "csci-dvr-scheduler-api.h"
#define MAX_MACADDR_LEN 128
#define MAX_IPADDR_LEN 128
#define SRCURL_LEN				1024		//As defined in MDA

using namespace std;

// ====== defines ======
/** maximum no of client handle at a time.
 \ingroup MRDvr_Server
 */
// ====== enums ======
/**
 * event types enum.
 */
typedef enum
{
    kMrdvrServeEvent,
    kMrdvrTeardownEvent,
    kMrdvrExitThreadEvent,
    kMrdvrTerminateSessionEvent
} tMrdvrSrvEventType;

/**
 * Status codes used for functions.
 */
typedef enum
{
    kMRDvrServer_Ok,                    /**< Function succeeded.*/
    kMRDvrServer_Error,                 /**< General error. */
    kMRDvrServer_ErrNotSupported,       /**< Not currently supported. */
    kMRDvrServer_ErrNoData,		    /**< Property not found or has no value. */
    kMRDvrServer_ErrInvalidID,          /**< Invalid ID such as device UUID. */
    kMRDvrServer_InvalidParameter,      /**< Invalid parameter. */
    kMRDvrServer_NotSupported,          /**< Not supported. */
    kMRDvrServer_NotInitialized,        /**< Server not initialized. */
    kMRDvrServer_AlreadyInitialized,    /**< Server already initialized. */
    kMRDvrServer_OutofMemory            /**< Not enough memory. */
} eMRDvrServerStatus;

typedef enum
{
    kUnmanagedDevice_Status_Found,
    kUnmanagedDevice_Status_NotFound,
    kUnmanagedDevice_Status_Error
} eUnmanagedDevice_Status;

/**
 * Data Structure to keep track of the last conflicted session for notifying status
 * \ingroup MRDvr_Server
 */
#ifdef __cplusplus
extern "C" {
#endif
    struct ipcsession
    {
        char avfs[SRCURL_LEN];
        char macAddress[MAX_MACADDR_LEN];
        char ipAddress[MAX_IPADDR_LEN];//added for the purpose of sending web sse messages like tuner resource unavailable etc when the session is cancelled
        uint32_t session;
        IMediaPlayerSession *handle;
        bool isOutofSeq;
        bool isCancelled;
        char OutofSeqURL[SRCURL_LEN];
        bool mRetry;
    };// cache/list to keep track of all live/recorded content that is being tuned to the client

#ifdef __cplusplus
}
#endif
/**
 * structure to store session information
\ingroup MRDvr_Server
 */

class ServeSessionInfo
{
public:
    IMediaPlayerSession 	*mPtrStreamingSession;

    ServeSessionInfo()
    {
        mPtrStreamingSession = NULL;
    }

    ~ServeSessionInfo()
    {
        mPtrStreamingSession = NULL;
    }
};

// MRDvrServer class definition.
class MRDvrServer
{
public:

    struct LastConflictSessionInfo
    {
        char MAC[MAX_MACADDR_LEN];
        char URL[SRCURL_LEN];
        uint32_t sessionID;
        bool isConflict;
        bool isCancelled;
        int OutofSeqCount;
    } conflictSessInfo;

    typedef std::vector <ipcsession*> ClientCache;
    ClientCache m_ipcsession;
    int mTunerFreeFlag;
    int m_sessionID[100];
    int m_sessionIDptr;
    int mCancelledSessionPositon;

    eCsciMspMrdvrConflictStatus mConflictState;
    eCsciMspMrdvrConflictStatus mPrevConflictState;
    /**
     * Destructor function.
     */
    ~MRDvrServer();

    /**
     * Initialize function for mrdvr server.
     */
    eMRDvrServerStatus Initialize();

    /**
     * \return pointer to MRDvrServer Class.
     * \brief This function is used to get MRDvrServer instance.
     */
    static MRDvrServer * getInstance();

    /**
     \brief Finalize function for mrdvr server.
    \ingroup MRDvr_Server
     */
    void MRDvrServer_Finalize();

    eMRDvrServerStatus isAssetInStreaming(
        const char *srvUrl,
        bool *pIsStreaming);

    void cleanupStreamingSession(IMediaPlayerSession *pMPSession);

    /**
     * Helper function to tear down the media streaming session in SAIL, without
     * waiting for the tear down request from CPERP. This will be useful in handling
     * error scenarios to clean up the orphan streaming sessions.
     */
    void handleLocalTeardown(IMediaPlayerSession *pMPSession);


    void HandleTerminateSession();

    /**
     * Media Player Callback Function to pass the asynchronous events from media streaming session
     * to mrdvr server that invoked the media streaming session.
     */
    static void mediaPlayerCallbackFunc(IMediaPlayerSession* pIMediaPlayerSession,
                                        tIMediaPlayerCallbackData callbackData,
                                        void* pClientContext,
                                        MultiMediaEvent *pMme);

    /**
    * @param id [IN] Session ID pertaining to the session that was cleaned up
    * @return None
    * @brief Remove the session ID stored as part of data map, maintained for all active tuner locked client sessions
    */
    void removesess(int id);

    /**
    * @param list [IN] List of all client sessions that are being served
    * @param reqInfo [IN] The Serve request info parameter recieved
    * @param srcurl [IN] SrcUrl that was requested by a client
    * @param mPtrStreamingSession [IN] The session Handle returned by mediaplayer on creating a session for a client request
    * @return None
    * @brief Adds the client serve request details like MAC address, IP address , URL being streamed on to cache maintained
    */
    void addToCache(ClientCache &list, tCpeHnSrvMgrMediaServeRequestInfo *reqInfo, char * srcurl, IMediaPlayerSession *mPtrStreamingSession);

    /**
    * @param list [IN] List of all client sessions that are being served
    * @param handle [IN] The session Handle pointing to the session that was cleaned up
    * @return None
    * @brief Removes the client serve request details from the cache maintained
    */
    void removeFromCache(ClientCache &list, IMediaPlayerSession *handle);

    /**
    * @return None
    * @brief Reference function that prints the details all client sessions being streamed
    */
    void printDetails();

    /**
    * @param srcurl [IN] SrcURL being streamed by the client
    * @param msg [IN] Descriptive Message sent via the SSE
    * @param MAC [IN] MAC address of the client(unique identifier) which is used for notifying the client with SSE messages
    * @param status [IN] Media player status to be sent to the concerned client via SSE for the Media player session specified by the SrcURL
    * @param signal [IN] Media player signal to be sent to the concerned client via SSE for the Media player session specified by the SrcURL
    * @return None
    * @brief Helper function that interfaces with Web-services framework to send Media-based SSE notifications
    */
    void sendSseNotification(char* srcurl, char* msg, char *MAC, eIMediaPlayerStatus status, eIMediaPlayerSignal signal);

    /**
    * @param handle [IN] The MediaPlayerSession Handle for which a MAC has to be retrieved from the cache
    * @return None
    * @brief Helper function that retrieves MAC address stored as part of the cache
    */
    char* getMacFromHandle(IMediaPlayerSession* handle);

    /**
    * @return value held by the tuner free flag (0 - not available, 1 - available)
    * @brief Mutex locked Helper function that retrieves Status as whether a tuner is available or not
    */
    int getTunerFreeFlag();

    /**
    * @param i [IN] Value to be set for the tuner free flag
    * @return None
    * @brief Mutex locked Helper function that sets/unsets the conditional flag.
    */
    void setTunerFreeFlag(int i);

    /**
    * @param i [IN] Index position
    * @return uint32_t
    * @brief Helper function that returns the sessionID stored on the index queried and is sent as part of the MME
    */
    uint32_t GetSessionID(uint32_t i);

    /**
    * @param sessionID [IN] sessionID queried for the friendly name
    * @return char*
    * @brief Return the MAC for the sessionID
    */
    const char* GetClientSessionMac(uint32_t sessionID);

    /*
    * @return uint32_t
    * @brief returns the position of the session that was cleaned up - Ensures the right entry is removed from TunerUsage list
    */
    uint32_t GetRemovedAssetID();

    /*
    * @param Srvurl [IN] The URL which was cancelled from conflict barker
    * @param id [IN] Session ID corresponding to the session cancelled
    * @return bool
    * @brief Cleans up an active client session if it was cancelled and sends SSE notifications to the client if tuner was denied/granted/revoked based on the user action
    */
    void ApplyResolution();

    /**
    * @param reqInfo [IN] The Serve request info parameter recieved
    * @param srcurl [IN] SrcUrl that was requested by a client
    * @param MAC [IN] MAC address of the client(unique identifier) which is used for notifying the client with TUNER_CONFLICT SSE message
    * @param pMMEvent [IN] MME data returned
    * @return None
    * @brief Sends an async notification to the UI that a conflict was triggered and SSE to the requested client of a conflict scenario
    */
    void HandleTunerConflict(tCpeHnSrvMgrMediaServeRequestInfo *reqInfo, char* MAC, const MultiMediaEvent *pMMEvent, bool isOutofSeq);

    /**
    * @param reqInfo [IN] The Serve request info parameter recieved
    * @return bool
    * @brief Checks if the serve request is valid enough to be handled and processed futher
    */
    bool IsSrvReqValid(tCpeHnSrvMgrMediaServeRequestInfo *reqInfo);

    /**
    * @param reqInfo [IN] The Serve request info parameter recieved
    * @return bool
    * @brief Checks if the serve request is a retry req/pending request or a new request
    */
    bool IsSrvReqPending(tCpeHnSrvMgrMediaServeRequestInfo *reqInfo);

    /**
    * @param reqInfo [IN] The Serve request info parameter recieved
    * @return bool
    * @brief Checks if the serve request has come out of sequence or not
    */
    bool CheckForOutOfSequenceEvents(tCpeHnSrvMgrMediaServeRequestInfo *reqInfo);

    /**
    * @param reqInfo [IN] The Serve request info parameter recieved
    * @brief Handles the pending serve request
    */
    void HandlePendingRequest(tCpeHnSrvMgrMediaServeRequestInfo *reqInfo);

    /**
    * @param MAC [IN] Mac address
    * @return IMediaPlayerSession*
    * @brief returns the session handle associated with a mac address
    */
    IMediaPlayerSession* getHandleFromMac(char* MAC);

    /**
    * @param oldId,newId [IN] session ID to be updated
    * @brief updated the older session ID with the new ID given
    */
    void UpdateSessionID(int oldID, int newID);

    /**
    * @brief Sends a retry notification SSE to client to set up a new connection for streaming
    */
    void Retry();

    void sendSseNotification_SDV(char* srcurl, int connectionId, char *MAC);

    /**
    * @param None
    * @return bool	- true if notifed any client succesfully
    				- false is not success
    * @brief Notify connected clients of warning that a tuner may be reclaimed .
    */
    bool NotifyWarning();

    /**
    * @param handle[IN] Session Handle associated with a session
    * @brief Checks a particular session's teardown has arrived out of sequence
    * @return bool - if out of seq, returns true
    */
    bool isRequestOutofSeq(IMediaPlayerSession* handle);

    /**
    * @param handle[IN] Session Handle associated with a session
    * @brief return the URL for which retry has to be triggered
    * @return the URL that was tagged with a session for which teardown dint come.
    */
    char* getOutOfSeqURL(IMediaPlayerSession* handle);

    /**
    * @param pCancelledConflictItem[IN] MME item selected by the user
    * @param isLoser[IN] indicates if the asset passed is marked for cancelling or not
    * @brief Map the asset with local streaming items and update
    * @return bool - true if the asset is updated successfully.
    */
    bool updateAsset(const MultiMediaEvent *pCancelledConflictItem, bool isLoser);

    /**
    * @param status[IN] Conflict status passed[Resolved/Detected/None]
    * @brief set the conflict status passed
    * @return None
    */
    void setConflictStatus(eCsciMspMrdvrConflictStatus status);

    /**
    * @param None
    * @brief return the current conflict status the gateway is in.
    * @return eCsciMspMrdvrConflictStatus - Current conflict status
    */
    eCsciMspMrdvrConflictStatus getConflictStatus();

    MSPEventQueue* threadEventQueue;

    /**
    * @param handle[IN] Session Handle associated with a session
    * @brief Enable retry functionality for this session.
    * @return None
    */
    void EnableRetry(IMediaPlayerSession* handle);

    void lockMutex(void);
    void unlockMutex(void);

#if PLATFORM_NAME == G8 || PLATFORM_NAME == IP_CLIENT
    eUnmanagedDevice_Status isUnmanagedDevice(const char *MAC);
#endif

    /**
    * @param MAC[IN] Mac address of the client associated with the selected Asset URL
    * @brief isLoser[IN] indicates if the asset passed is marked for cancelling or not
    * @return Current Active Streaming URL of the client
    */
    const char* FindAndUpdateActiveURL(const char *MAC, bool isLoser);

    /**
    * @param None
    * @brief Helper API to check if live streaming is in progress currently
    * @return bool - true if any client is actively locked to a tuner
    */
    bool IsLiveStreamingActive();

    /**
    * @param None
    * @brief return singleton object instance
    * @return return singleton object instance
    * 				 Null if no instance was created/finalised.
    */
    static MRDvrServer *getHandle();

private:

    /**
     * Constructor function.
     */
    MRDvrServer();
    static void* eventthreadFunc(void *data);
    bool handleEvent(Event *evt);
    int createThread();
    void stopThread();
    void HandleServeRequest(tCpeHnSrvMgrMediaServeRequestInfo *reqInfo);
    void HandleTeardownRequest(tCpePgrmHandle* pPgrmHandle);

    static int ServerManagerCallback(tCpeHnSrvMgrCallbackTypes type, void *userdata, void *pCallbackSpecific);

    /**
     * static Pointer to MRDvrServer class.
     */
    static MRDvrServer *instance;

    /**
     * to check if server is already initialized.
     */
    bool isInitialized;

    /**
     * callback id for server request.
     */
    tCpeHnMsm_CallbackID suCallbackId;
    /**
     * callback id for teardown request.
     */
    tCpeHnMsm_CallbackID tdCallbackId;

    /**
     * pointer to thread event queue.
     */
    pthread_t eventHandlerThread;
    pthread_mutex_t  mMutex;
    pthread_mutex_t mTunerMutex;
    pthread_mutex_t mSessionMutex;
    /**
     * list of active serve session information.
     */
    list<ServeSessionInfo *> ServeSessList;
    bool isMrDvrAuthorized;
};

#endif
