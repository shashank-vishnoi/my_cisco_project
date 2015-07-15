#if !defined(IMEDIASTREAMER_H)
#define IMEDIASTREAMER_H

#include <list>
#include <stdint.h>
#include <pthread.h>
#include <sail-mediaplayersession-api.h>   // public SAIL header file
#include "IMediaController.h"       // internal definition of medial controller object
#include "eventQueue.h"
#include "MspCommon.h"
#include "MSPEventCallback.h"
#include "MSPDiagPages.h"
#include "IMediaPlayer.h"


typedef enum
{
    kMediaStreamerEventNotDefined = -1,
    kMediaStreamerEventCCIUpdated,
    kMediaStreamerEventThreadExit
} eMediaStreamerEvent;

typedef struct MediaStreamerSessionCciData
{
    void* pData;
    uint8_t CCIbyte;
} tMediaStreamerSessionCciData;

class IMediaPlayerSession;
class IMediaController;

class IMediaStreamer
{
private:
    static IMediaStreamer *mInstance;

    ///Constructor
    IMediaStreamer(); // private for singleton

    static std::list<IMediaPlayerSession*> mstreamingSessionList;

    std::list<IMediaPlayerSession *>::iterator getIterator(IMediaPlayerSession * session);

    bool IsSessionRegistered(IMediaPlayerSession* pIMediaPlayerSession);

    pthread_mutex_t m_StreamerMutex;

    // Event queue monitored by media streamer thread
    MSPEventQueue* threadEventQueue;
    // Media streamer thread to monitor the asynchronous events posted onto the
    // media streamer thread event queue
    pthread_t mEventHandlerThread;

public:

    ///Destructor
    ~IMediaStreamer();

    ///This method creates and returns an IMediaStreamer instance.
    static IMediaStreamer * getMediaStreamerInstance();

    ///This method creates an IMediaPlayerSession instance.
    eIMediaPlayerStatus IMediaStreamerSession_Create(IMediaPlayerSession ** ppIMediaPlayerSession,
            IMediaPlayerStatusCallback eventStatusCB,
            void* pClientContext);

    ///Loads a service into the media player session.
    eIMediaPlayerStatus IMediaStreamerSession_Load(IMediaPlayerSession *pIMediaPlayerSession,
            const char * serviceUrl,
            const MultiMediaEvent ** pMme);


    ///   This method begins streaming the media service. Physical resources get assigned during this operation.
    eIMediaPlayerStatus IMediaStreamerSession_Play(IMediaPlayerSession *pIMediaPlayerSession,
            const char *outputUrl,
            float nptStartTime,
            const MultiMediaEvent **pMme);

    /// This method stops the media service streaming.
    eIMediaPlayerStatus IMediaStreamerSession_Stop(IMediaPlayerSession *pIMediaPlayerSession,
            bool stopPlay,
            bool stopPersistentRecord);


    ///Ejects service from the Media Player Session.
    eIMediaPlayerStatus IMediaStreamerSession_Eject(IMediaPlayerSession *pIMediaPlayerSession);


    ///This method destroys an IMediaPlayerSession.
    eIMediaPlayerStatus IMediaStreamerSession_Destroy(IMediaPlayerSession *pIMediaPlayerSession);

    ///Sets the playback speed of the IMediaPlayerSession.
    eIMediaPlayerStatus IMediaStreamerSession_SetSpeed(IMediaPlayerSession* pIMediaPlayerSession,
            int numerator,
            unsigned int denominator);

    //Gets the current playback speed on an IMediaPlayerSession.
    eIMediaPlayerStatus IMediaStreamerSession_GetSpeed(IMediaPlayerSession* pIMediaPlayerSession,
            int* pNumerator,
            unsigned int* pDenominator);

    //Sets an approximated current NPT (Normal Play Time) position.
    eIMediaPlayerStatus IMediaStreamerSession_SetPosition(IMediaPlayerSession* pIMediaPlayerSession, float nptTime);

    //Gets an approximated current NPT (Normal Play Time) position.
    eIMediaPlayerStatus IMediaStreamerSession_GetPosition(IMediaPlayerSession* pIMediaPlayerSession, float* pNptTime);

    //extenssion apis are used to get mini carosuel data

    eIMediaPlayerStatus IMediaStreamerSession_SetApplicationDataPidExt(IMediaPlayerSession *pIMediaPlayerSession,
            IMediaPlayerStatusCallback eventStatusCB, void *pEventStatusCBClientContext, uint32_t pid, const tIMediaPlayerSfltFilterGroup *sfltGroup,
            IMediaPlayerClientSession ** ppClientSession);


    eIMediaPlayerStatus IMediaStreamerSession_GetApplicationDataExt(IMediaPlayerClientSession * pClientSession,
            IMediaPlayerSession *pIMediaPlayerSession,
            uint32_t bufferSize,
            uint8_t *buffer,
            uint32_t *dataSize);

    //Mutex introduced to share the common resource(servesession)
    void lockmutex();

    void unlockmutex();

    //Gets an Copy protection info for the streaming sessions for diag page
    static void GetMspCopyProtectionInfo(DiagCCIData *pStreamingCopyInfo , int *psesscount);

    //CCI callback for the streaming sessions
    static void StreamerCCIUpdated(void *pData, uint8_t CCIbyte);

    //Stop the streaming source associated with the session's Controller
    eIMediaPlayerStatus IMediaStreamerSession_StopStreaming(IMediaPlayerSession *pIMediaPlayerSession);

    // Media player thread entry function
    static void* streamerEventThreadFunc(void *data);
    // Queues an event onto the queue monitored by Media player thread
    eIMediaPlayerStatus queueEvent(eMediaStreamerEvent evtyp, void* pData)	;
    // Process the CCI Updated event
    void ProcessStreamerCCIUpdated(void *pData, uint8_t CCIbyte);
};

#endif
