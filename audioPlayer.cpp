/**
	\file audioPlayer.cpp
	\class AudioPlayer

Implementation of AudioPlayer - an instance of an IMediaController object
*/

#include <sys/stat.h>
#include <assert.h>
#include <pthread.h>

#include "dlog.h"
#include "pthread_named.h"

#include "cpe_error.h"
#include "cpe_mediamgr.h"
#include "eventQueue.h"
#include "audioPlayer.h"


// audio player resides in MSP.  Since it is used only in association with EAS
//   audio processing, use EAS logging macros.
#define LOG_NORMAL( format, args... ) dlog( DL_SIG_EAS, DLOGL_NORMAL, format, ##args );
#define LOG_ERROR( format, args... ) dlog( DL_SIG_EAS, DLOGL_ERROR, format, ##args );
#define LOG_EMERGENCY( format, args... ) dlog( DL_SIG_EAS, DLOGL_EMERGENCY, format, ##args );

// constants and structure definitions used for parsing the header of an .aiff audio file
#define FORM_ID		0x464F524D 	//"FORM"
#define AIFF_TYPE	0x41494646	//"AIFF"
#define COMM_ID		0x434F4D4D	//"COMM"
#define SSND_ID		0x53534E44	//"SSND"
#define ANNO_ID     0x414E4E4F  // "ANNO"

typedef struct
{
    uint32_t ckId;
    uint32_t ckSize;
} ChunkHeader;

typedef struct
{
    int16_t	numChannels;
    int16_t	numSampleFrames1;
    int16_t	numSampleFrames2;
    int16_t	sampleSize;
} CommonChunk;

typedef struct
{
    uint32_t offset;
    uint32_t blockSize;
} SoundDataChunk;

typedef struct
{
    uint16_t exponent;
    uint16_t fraction[4];
} EightyBitExtended;

bool AudioPlayer::mEasAudioActive = false;


/********************************************************************************
 *
 *	Function:	AudioPlayer
 *
 *	Purpose:	Initialize all members, then create an EventQueue and thread
 *				for processing audio player events.
 *
 */

AudioPlayer::AudioPlayer(IMediaPlayerSession *pIMediaPlayerSession) :
    repeatDelay(0),
    mutexStack(0),
    currentState(kState_Idle),
    callbackId(0),
    mediaHandle(0),
    sourceHandle(0),
    audioFp(NULL),
    soundDataSize(0),
    timerId(0),
    mediaPlayerInstance(NULL),
    mIMediaPlayerSession(pIMediaPlayerSession)
{
    fileUrl[0] = '\0';
    srcUrl = "";
    memset(&sourceBuffer, 0, sizeof(tCpeSrcMemBuffer));
    memset(&aiffAudioInfo, 0, sizeof(tCpeAudioInfo));

    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);

    // get a pointer to the MediaPlayer object and error check
    mediaPlayerInstance = IMediaPlayer::getMediaPlayerInstance();
    assert(mediaPlayerInstance);

    // acquire timer resources
    Timer_init();

    // initialize queue and mutex
    apQueue = new MSPEventQueue();
    pthread_mutex_init(&apMutex, NULL);

    // initialize thread machinery
    createThread();
}


/********************************************************************************
 *
 *	Function:	~AudioPlayer
 *
 *	Purpose:	Release all remaining audio player resources.
 *
 */

AudioPlayer::~AudioPlayer(void)
{
    LOG_NORMAL("%s(), enter", __FUNCTION__);

    std::list<CallbackInfo*>::iterator iter;
    for (iter = callbackList.begin(); iter != callbackList.end(); iter++)
    {
        delete(*iter);
        *iter = NULL;
    }
    callbackList.clear();

    delete apQueue;
    pthread_mutex_destroy(&apMutex);

    // if active, cancel timer
    cancelTimer();

    LOG_NORMAL("%s(), exit", __FUNCTION__);
}


/********************************************************************************
 *
 *	Function:	Load
 *
 *	Purpose:	Parse serviceUrl to initialize playback parameters and
 *				send an event to the audio player thread instructing it
 *				to prepare for audio playback.
 *
 */

eIMediaPlayerStatus
AudioPlayer::Load(const char* serviceUrl, const MultiMediaEvent **pMme)
{
    char copyUrl[kMaxUrlSize + 1];
    char *ptr;

    (void) pMme;

    // save serviceUrl and make a copy
    srcUrl = serviceUrl;
    strncpy(copyUrl, serviceUrl, kMaxUrlSize);

    // parse for the delay parameter
    ptr = strstr(copyUrl, "&delay=");
    if (ptr)
    {
        sscanf(ptr, "&delay=%d", &repeatDelay);
        *ptr = '\0';	// chop off delay portion of url in copyUrl
    }

    // parse for file name
    sscanf(copyUrl, "audio://%s", fileUrl);
    LOG_NORMAL("enter %s(), srcUrl: %s    fileUrl: %s    repeatDelay: %d    current state: %d",
               __FUNCTION__, srcUrl.c_str(), fileUrl, repeatDelay, currentState);

    // request that the thread prepare for source playback
    queueEvent(kPrepareSourceEvent);

    return kMediaPlayerStatus_Loading;
}


/********************************************************************************
 *
 *	Function:	Play
 *
 *	Purpose:	Send an event to the audio player thread instructing it
 *				to start audio playback.
 *
 */

eIMediaPlayerStatus
AudioPlayer::Play(const char* outputUrl, float nptStartTime, const MultiMediaEvent **pMme)
{
    (void) outputUrl;
    (void) nptStartTime;
    (void) pMme;

    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);

    // request that the thread start source playback
    queueEvent(kPlaySourceEvent);

    return kMediaPlayerStatus_Ok;
}


/********************************************************************************
 *
 *	Function:	Stop
 *
 *	Purpose:	Stop audio playback and restore normal audio.  Release
 *				any previously acquired audio resources.
 *
 */

eIMediaPlayerStatus
AudioPlayer::Stop(bool stopPlay, bool stopPersistentRecord)
{
    eIMediaPlayerStatus result;

    (void) stopPlay;
    (void) stopPersistentRecord;

    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);

    // if audio playback is active
    if (currentState == kState_PlaySource)
    {
        // transition into the stopping state
        enterStoppingState();
        result = kMediaPlayerStatus_Loading;
    }
    else
    {
        // no audio teardown operations necessary.  Release
        //   remaining resources
        completeStopSequence();
        result = kMediaPlayerStatus_Ok;
    }

    return result;
}


/********************************************************************************
 *
 *	Function:	completeStopSequence
 *
 *	Purpose:	Stopping the audio player is an asynchronous completion sequence.
 *				The state machine executes this routine after the reference
 *				platform has reported that audio playback is no longer active.
 *
 */

void
AudioPlayer::completeStopSequence(void)
{
    // update state
    currentState = kState_Stopped;

    // release memory resources
    if (sourceBuffer.pBuffer != NULL)
    {
        free(sourceBuffer.pBuffer);
        memset(&sourceBuffer, 0, sizeof(tCpeSrcMemBuffer));
    }

    // release file resources
    if (audioFp)
    {
        fclose(audioFp);
        audioFp = NULL;
    }
}


/********************************************************************************
 *
 *	Function:	Eject
 *
 *	Purpose:	Terminate the audio player thread.
 *
 */

void
AudioPlayer::Eject(void)
{
    LOG_NORMAL("enter %s()", __FUNCTION__);

    if (apThread)
    {
        LOG_NORMAL("%s(), queueing exit event", __FUNCTION__);
        queueEvent(kExitThreadEvent);  			// tell thread to exit
        unLockMutex();
        pthread_join(apThread, NULL);       	// wait for event thread to exit
        lockMutex();
        apThread = 0;
    }
    else
    {
        LOG_NORMAL("%s(), exit previously requested, no action taken", __FUNCTION__);
    }
}


/********************************************************************************
 *
 *	Function:	enterPrepareSourceState
 *
 *	Purpose:	Prepare a memory buffer for audio playback.  Steps:
 *				- parse header of audio file to retrieve playback parameters
 *				  and error check.
 *				- allocate memory for audio samples and error check
 *				- read audio samples into memory and error check
 *				- If all Ok, exec status Ok callback.  Else, error status
 *				  callback.
 *				- update state appropriately
 *
 */

void
AudioPlayer::enterPrepareSourceState(void)
{
    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);

    // assume that the prepare operations will fail
    currentState = kState_Error;

    // extract parameters and error check
    if (readAudioInfo())
    {
        // allocate buffer memory and error check
        sourceBuffer.pBuffer = (uint8_t*) malloc(sizeof(uint8_t) * soundDataSize + 1);
        if (sourceBuffer.pBuffer != NULL)
        {
            // read file contents into buffer and error check
            if (readSoundData(&(sourceBuffer.pBuffer), &(sourceBuffer.length)))
            {
                // success, notify client
                currentState = kState_PrepareSource;
                LOG_NORMAL("    %s(), success, buffer length: %d", __FUNCTION__, sourceBuffer.length);
                doCallback(kMediaPlayerSignal_ServiceLoaded, kMediaPlayerStatus_Ok);
            }
        }
    }

    // if a failure
    if (currentState != kState_PrepareSource)
    {
        // notify client of failure
        doCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_Error_Unknown);
    }
}


/********************************************************************************
 *
 *	Function:	enterPlaySourceState
 *
 *	Purpose:	Start the playback of EAS audio. Steps:
 *				- stop primary audio
 *				- using audio data found in a previously prepared buffer,
 *				  create a memory source and prepare the audio path to
 *				  play back from this source
 *				- start playback and error check.  If Ok, exec status
 *				  Ok calback.  Else, exec error status playback.
 *				- update state appropriately
 *
 */

void
AudioPlayer::enterPlaySourceState(void)
{
    // stop the primary audio source and start Eas Audio Playback
    stopPrimaryAudioAndStartEasAudio();
}

// Responsible for starting the EAS audio playback
// This API is applicable for the controllers associated with EAS audio playback session
// For the controllers associated with non EAS audio playback sessions simply returns
//
// Since AudioPlayer is the controller associated with EAS audio playback session,
// this API is starting the playback of the EAS audio clip
void
AudioPlayer::startEasAudio(void)
{
    int result;

    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);

    // assume that the play source sequence will fail
    currentState = kState_Error;

    // set up the memory source and the media destination
    result = prepareMemorySource();

    // if OK
    if (result == kCpe_NoErr)
    {
        // start playback and error check
        result = cpe_src_Set(sourceHandle, eCpeSrcNames_MemBuffer, &sourceBuffer);
        if (result == kCpe_NoErr)
        {
            // everything OK, update state, notify client
            currentState = kState_PlaySource;
            doCallback(kMediaPlayerSignal_PresentationStarted, kMediaPlayerStatus_Ok);
        }
    }

    // if playback did not start correctly
    if (currentState != kState_PlaySource)
    {
        restartPrimaryAudio();

        // notify client of error
        doCallback(kMediaPlayerSignal_PresentationTerminated, kMediaPlayerStatus_Error_Unknown);
    }
}

/********************************************************************************
 *
 *	Function:	enterStoppingState
 *
 *	Purpose:	Stopping the audio player is an asynchronous completion sequence.
 *				This routine performs the first steps of that sequence.
 *
 */

void
AudioPlayer::enterStoppingState(void)
{
    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);

    // update state and start the audio teardown sequence
    currentState = kState_Stopping;
    startAudioTeardown();
}


/********************************************************************************
 *
 *	Function:	enterDelayState
 *
 *	Purpose:	Transition into the delay state.  Steps:
 *				- restore primary audio
 *				- update state
 *				- start delay timer
 *
 */

void
AudioPlayer::enterDelayState(void)
{
    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);

    // update state and start timer
    currentState = kState_Delay;
    startTimer(repeatDelay);
}


/********************************************************************************
 *
 *	Function:	stopPrimaryAudio
 *
 *	Purpose:	Stop the audio associated with currently active video.  Audio
 *				player state machine calls this method just before starting
 *				the EAS audio playback.
 *
 */

void
AudioPlayer::stopPrimaryAudioAndStartEasAudio(void)
{
    mediaPlayerInstance->StopInFocusAudioAndStartEasAudio(mIMediaPlayerSession);
}


/********************************************************************************
 *
 *	Function:	restartPrimaryAudio
 *
 *	Purpose:	Restart the audio associated with currently active video.  Audio
 *				player state machine calls this method after the EAS audio has
 *				completed its playback.
 *
 */

void
AudioPlayer::restartPrimaryAudio(void)
{
    mediaPlayerInstance->RestartInFocusAudio();
}


/********************************************************************************
 *
 *	Function:	startAudioTeardown
 *
 *	Purpose:	Tearding down audio playback is an asynchronous completion sequence.
 *				This routine performs the first steps of that sequence.
 *
 */

void
AudioPlayer::startAudioTeardown(void)
{
    int result;
    tCpeSrcMemStreamType streamType = eCpeSrcMemStreamType_PCMAud;

    LOG_NORMAL("%s(), enter.", __FUNCTION__);

    // if valid source handle
    if (sourceHandle)
    {
        result = cpe_src_Set(sourceHandle, eCpeSrcNames_FlushBuffers, &streamType);
        if (result != kCpe_NoErr)
        {
            LOG_ERROR("    %s(), source flush buffers error: %d", __FUNCTION__, result);
        }
    }
    else
    {
        LOG_ERROR("    %s(), NULL source handle", __FUNCTION__);
    }
}


/********************************************************************************
 *
 *	Function:	completeAudioTeardown
 *
 *	Purpose:	Tearding down audio playback is an asynchronous completion
 *				sequence.  The state machine executes this routine after the
 *				reference platform has reported that audio playback is no
 *				longer active.
 *
 */

void
AudioPlayer::completeAudioTeardown(void)
{
    int result;

    LOG_NORMAL("%s(), enter.", __FUNCTION__);

    // if valid source and media handles
    if (sourceHandle && mediaHandle)
    {
        result = cpe_src_Stop(sourceHandle);
        if (result != kCpe_NoErr)
        {
            LOG_ERROR("    %s(), source stop error: %d", __FUNCTION__, result);
        }

        result = cpe_media_Stop(mediaHandle, kCpeMedia_AudioDecoder);
        if (result != kCpe_NoErr)
        {
            LOG_ERROR("    %s(), media stop error: %d", __FUNCTION__, result);
        }

        result = cpe_media_Close(mediaHandle);
        mediaHandle = 0;
        if (result != kCpe_NoErr)
        {
            LOG_ERROR("    %s(), media close error: %d", __FUNCTION__, result);
        }

        result = cpe_src_UnregisterCallback(sourceHandle, callbackId);
        if (result != kCpe_NoErr)
        {
            LOG_ERROR("    %s(), source unregister callback error: %d", __FUNCTION__, result);
        }

        result = cpe_src_Close(sourceHandle);
        sourceHandle = 0;
        if (result != kCpe_NoErr)
        {
            LOG_ERROR("    %s(), source close error: %d", __FUNCTION__, result);
        }
    }
    else
    {
        LOG_ERROR("    %s(), NULL source or media handle", __FUNCTION__);
    }

    // restart audio associated with current video
    restartPrimaryAudio();
}


/********************************************************************************
 *
 *	Function:	prepareMemorySource
 *
 *	Purpose:	Take the steps to prepare a reference platform memory source
 *				as a source for audio playback.
 *
 *	Parameters:	Routine assumes that the static structure aiffAudioInfo
 *				contains the correct parameters for playback.
 *
 *	Returns:	If success, return kCpe_Err.  Else, error code.
 *
 */

int
AudioPlayer::prepareMemorySource(void)
{
    int	result;
    tCpeSrcMemStreamType streamType = eCpeSrcMemStreamType_PCMAud;

    // initialize
    result = kCpe_Err;

    result = cpe_src_Open(eCpeSrcType_MemorySource, &sourceHandle);
    if (result == kCpe_NoErr)
    {
        result = cpe_src_RegisterCallback(eCpeSrcCallbackTypes_BufferCmplt, this,
                                          audioCallback, &callbackId, sourceHandle, NULL);
        if (result == kCpe_NoErr)
        {
            result = cpe_src_Set(sourceHandle, eCpeSrcNames_DataType, &streamType);
            if (result == kCpe_NoErr)
            {
                result = cpe_src_Set(sourceHandle, eCpeSrcNames_PCMAudioInfo, &aiffAudioInfo);
                if (result == kCpe_NoErr)
                {
                    result = cpe_media_Open(sourceHandle, eCpeMediaStreamTypes_ElementaryStream,
                                            &mediaHandle);
                    if (result == kCpe_NoErr)
                    {
                        result = cpe_src_Start(sourceHandle);
                        if (result == kCpe_NoErr)
                        {
                            result = cpe_media_Start(mediaHandle, kCpeMedia_AudioDecoder);
                        }
                    }
                }
            }
        }
    }

    LOG_NORMAL("%s(), result: %d.  Source handle: 0x%p    Media Handle: 0x%p",
               __FUNCTION__, result, sourceHandle, mediaHandle);

    return result;
}


/********************************************************************************
 *
 *	Function:	readSoundData
 *
 *	Purpose:	Read the data portion of the audio file into the indicated
 *				buffer.  Method assumes that audioFp and soundDataSize are
 *				valid on entry.  Regardless success/fail, method will
 *				close audioFp.
 *
 *
 *	Parameters:	ppBuff - pointer to buffer to store contents of audio file
 *				bufSize - how many bytes read into buffer
 *
 *	Returns:	Boolean.  If success, true.
 *
 */

bool
AudioPlayer::readSoundData(uint8_t** ppBuff, uint32_t* bufSize)
{
    bool success = false;
    size_t bytesRead;

    if (audioFp && soundDataSize)
    {
        bytesRead = fread(*ppBuff, sizeof(uint8_t), soundDataSize, audioFp);
        LOG_NORMAL("%s, %d, Dump EAS audio sound data, sizeRead %d, dataSize %d", __FUNCTION__, __LINE__, bytesRead, soundDataSize);

        if (bytesRead == soundDataSize)
        {
            *bufSize = soundDataSize;
            success = true;
        }

        // always close audio file
        fclose(audioFp);
        audioFp = NULL;
    }

    LOG_NORMAL("%s(), exiting, success: %d", __FUNCTION__, success);

    return success;
}


/********************************************************************************
 *
 *	Function:	readAudioInfo
 *
 *	Purpose:	Parse the header of the .aiff audio file.  See
 *				http://preserve.harvard.edu/standards/audioiffspecification1-3.pdf
 *				for a complete discussion .aiff file format.  The method performs
 *				several actions:
 *				- initializes audioFp and places the file position at the beginning
 *				  of the file data - the audio samples.
 *				- initializes soundDataSize.
 *				- loads the structure aiffAudioInfo with the values extracted
 *				  from parsing the aiff header.
 *
 *	Returns:	Boolean.  If success, true.
 *
 */

bool AudioPlayer::readAudioInfo(void)
{
    bool success = false;
    uint8_t tempBuffer[kAudioHeaderSize + 1];
    uint32_t fileSize;

    memset(&aiffAudioInfo, 0, sizeof(tCpeAudioInfo));
    LOG_NORMAL("%s, %d, Opening EAS audio file URL %s", __FUNCTION__, __LINE__, fileUrl);
    audioFp = fopen(fileUrl, "rb");
    if (audioFp)
    {
        // obtain file size
        fseek(audioFp, 0, SEEK_END);
        fileSize = ftell(audioFp);
        LOG_NORMAL("%s, %d, Open EAS audio file URL %s, file size %d", __FUNCTION__, __LINE__, fileUrl, fileSize);
        if (fileSize >= kAudioHeaderSize)
        {
            rewind(audioFp);
            size_t bytesRead;

            // copy the file header into the buffer
            bytesRead = fread(tempBuffer, sizeof(uint8_t),
                              kAudioHeaderSize, audioFp);
            LOG_NORMAL("%s, %d, Dump EAS audio file header, sizeRead %d", __FUNCTION__, __LINE__, bytesRead);

            if (bytesRead == kAudioHeaderSize)
            {
                uint8_t* ptr = tempBuffer;
                bool parseContinue = true;
                bool bGetSSND = false;
                bool bGetCOMM = false;
                bool bGetFORM = false;
                // int sizeParsed = ptr - tempBuffer;

                while ((ptr - tempBuffer) < (signed int)bytesRead && parseContinue)   //|| (!bGetSSND && !bGetCOMM && !bGetFORM )
                {
                    ChunkHeader chunk;

                    memcpy(&chunk, ptr, sizeof(ChunkHeader));

                    //for G8 EAS porting
                    chunk.ckId = ntohl(chunk.ckId);
                    chunk.ckSize = ntohl(chunk.ckSize);

                    ptr += sizeof(ChunkHeader);
                    LOG_NORMAL("%s(), chId: 0x%4X, ckSize: %d",  __FUNCTION__, chunk.ckId, chunk.ckSize);

                    switch (chunk.ckId)
                    {
                    case FORM_ID:
                    {
                        bGetFORM = true;
                        uint32_t formType;
                        LOG_NORMAL("%s, %d, Got FORM", __FUNCTION__, __LINE__);

                        memcpy(&formType, ptr, sizeof(uint32_t));

                        formType = ntohl(formType);
                        ptr += sizeof(uint32_t);
                        if (formType != AIFF_TYPE)
                        {
                            parseContinue = false;
                            LOG_ERROR("%s, %d, format Type 0x%x is not AIFF, stop parsing", __FUNCTION__, __LINE__,  formType);
                        }
                        break;
                    }

                    case COMM_ID:
                    {
                        bGetCOMM = true;
                        LOG_NORMAL("%s, %d, Got COMM", __FUNCTION__, __LINE__);
                        CommonChunk commonChunk;
                        int	size = sizeof(CommonChunk);

                        // extract the CommonChunk
                        memcpy(&commonChunk, ptr, size);

                        //for G8 EAS porting
                        commonChunk.numChannels = ntohs(commonChunk.numChannels);
                        commonChunk.numSampleFrames1 = ntohs(commonChunk.numSampleFrames1);
                        commonChunk.numSampleFrames2 = ntohs(commonChunk.numSampleFrames2);
                        commonChunk.sampleSize = ntohs(commonChunk.sampleSize);


                        ptr += size;

                        // extract the floating point sample rate and convert it to an integer.  Curiously,
                        //   the reference platform expects the sample rate to be left shifted 16 bits.
                        int32_t integerResult;
                        EightyBitExtended extended;
                        size = sizeof(EightyBitExtended);
                        memcpy(&extended, ptr, size);

                        //for G8 EAS porting
                        extended.exponent = ntohs(extended.exponent);

                        extended.fraction[0] = ntohs(extended.fraction[0]);
                        extended.fraction[1] = ntohs(extended.fraction[1]);
                        extended.fraction[2] = ntohs(extended.fraction[2]);
                        extended.fraction[3] = ntohs(extended.fraction[3]);

                        ptr += size;
                        integerResult = floatToInteger(extended.exponent, extended.fraction[0]);
                        // LOG_NORMAL("%s, %d, Comment off left shift 16", __FUNCTION__, __LINE__);
                        integerResult = (integerResult << 16);

                        // assign output structure values
                        aiffAudioInfo.channels = (int)commonChunk.numChannels;
                        aiffAudioInfo.channelsize = (int)commonChunk.sampleSize;
                        aiffAudioInfo.samplerate = (tCpeSrcMemSamplingRate)integerResult;    // samplingRate;
                        break;
                    }

                    case SSND_ID:
                    {
                        bGetSSND = true;
                        SoundDataChunk ssndChunk;
                        int	size = sizeof(SoundDataChunk);
                        LOG_NORMAL("%s, %d, Got SSND", __FUNCTION__, __LINE__);

                        memcpy(&ssndChunk, ptr, size);

                        ssndChunk.offset = ntohl(ssndChunk.offset);
                        ssndChunk.blockSize = ntohl(ssndChunk.blockSize);
                        ptr += size;
                        soundDataSize = chunk.ckSize - 8;
                        parseContinue = false;
                        success = true;

                        int parsedSize = ptr - tempBuffer;
                        if (parsedSize != kAudioHeaderSize)
                        {
                            int seekSize = (parsedSize - kAudioHeaderSize);
                            fseek(audioFp, (parsedSize - kAudioHeaderSize), SEEK_CUR);
                            LOG_NORMAL("%s, %d, SEEKed %d, parsed %d, data size %d", __FUNCTION__, __LINE__, seekSize, parsedSize, soundDataSize);
                        }
                        break;
                    }
                    case ANNO_ID:
                    {
                        char * pAnno = (char *)malloc(chunk.ckSize + 1);
                        memcpy(pAnno, ptr, chunk.ckSize);
                        pAnno[chunk.ckSize] = 0;
                        LOG_NORMAL("Got an ANNO chunk, Ignore: %s", pAnno);
                        ptr += chunk.ckSize;
                        break;
                    }



                    default:
                        // parseContinue = false;
                        LOG_ERROR("Got an unknown chunk 0x%x, size 0x%x, Ignore it", chunk.ckId, chunk.ckSize);
                        ptr += chunk.ckSize;
                        break;
                    }
                }
            }
        }
    }

    LOG_NORMAL("%s(), channels: %d    channelSize: %d    sampleRate: 0x%08X",
               __FUNCTION__, aiffAudioInfo.channels, aiffAudioInfo.channelsize, aiffAudioInfo.samplerate);
    LOG_NORMAL("%s(), exiting.  Success: %d    size: %d",
               __FUNCTION__, success, soundDataSize);
    return success;
}


/********************************************************************************
 *
 *	Function:	floatToInteger
 *
 *	Purpose:	Convert a floating point representation of a number to integer
 *				format.
 *
 *	Parameters:	exponent - exponent portion of an 80 bit IEEE 754 floating
 *					point number
 *				fraction - most significant 16 bits of the fraction portion
 *					of an 80 bit IEEE 754 floating point number
 *
 *	Returns:	Integer representation of the floating point number.
 *
 *	Notes:		See http://www.cs.trinity.edu/About/The_Courses/cs2322/ieee-fp.html
 *				for a concise explanation of the 80 bit format and conversion
 *				algorithm.  Because it is sufficient, this routine uses only
 *				the most significant 16 bits of the fraction field.
 *
 */

int32_t
AudioPlayer::floatToInteger(uint16_t exponent, uint16_t fraction)
{
    float multiplier;
    float numerator;
    float denominator;
    float base;
    float result;
    uint16_t adjustedExponent;
    bool explicitOnesBit;
    bool signBit;
    int32_t integerResult;

    // process the special bits
    explicitOnesBit = false;
    signBit = false;
    if (exponent & 0x8000)
    {
        signBit = true;
    }
    exponent &= 0x7FFF;
    if (fraction & 0x8000)
    {
        explicitOnesBit = true;
    }
    fraction &= 0x7FFF;

    // construct base portion
    numerator = (float) fraction;
    denominator = 0x8000;
    base = numerator / denominator;
    if (explicitOnesBit)
    {
        base += 1;
    }

    // construct multiplier
    adjustedExponent = exponent - 16383;
    multiplier = (float)(1 << adjustedExponent);

    // construct result
    result = base * multiplier;
    if (signBit)
    {
        result *= -1.0;
    }
    integerResult = (int32_t) result;

    LOG_NORMAL("%s(), result: %d", __FUNCTION__, integerResult);

    return integerResult;
}


/********************************************************************************
 *
 *	Function:	processTimerEvent
 *
 *	Purpose:	Process timer event.  Cancel timer.  If in delay state,
 *				transition to play source state.
 *
 */

void
AudioPlayer::processTimerEvent(void)
{
    // log entry
    LOG_NORMAL("enter %s(), current state: %d", __FUNCTION__, currentState);

    // always cancel the timer
    cancelTimer();

    if (currentState == kState_Delay)
    {
        // repeat source playback
        enterPlaySourceState();
    }
    else
    {
        LOG_ERROR("%s, unexpected timer event", __FUNCTION__);
    }
}


/********************************************************************************
 *
 *	Function:	processAudioEvent
 *
 *	Purpose:	Process an audio event.
 *
 *	Parameters:	eventData - payload of an audio player event
 *
 */

void
AudioPlayer::processAudioEvent(void* eventData)
{
    AudioPlayerEventData* data = (AudioPlayerEventData*) eventData;
    tCpeSrcCallbackTypes type = (tCpeSrcCallbackTypes) data->payload;

    LOG_NORMAL("%s, currentState: %d   type: %d", __FUNCTION__, currentState, type);

    if (type == eCpeSrcCallbackTypes_BufferCmplt)
    {
        if (currentState == kState_PlaySource)
        {
            // complete audio teardown sequence
            completeAudioTeardown();

            // audio restored, start delay
            enterDelayState();
        }
        else if (currentState == kState_Stopping)
        {
            // complete audio teardown sequence
            completeAudioTeardown();

            // complete the stop sequence
            completeStopSequence();

            // notify client
            doCallback(kMediaPlayerSignal_PresentationTerminated, kMediaPlayerStatus_Ok);
        }
    }
}


/********************************************************************************
 *
 *	Function:	processEvent
 *
 *	Purpose:	Process an audio player event.  In most cases, the event is
 *				delegated to a type specific handler.
 *
 *	Parameters:	event - an audio player event
 *
 *	Returns:	Boolean.  If false, tells caller( threadFunction() ) to continue
 *				processing events.  If true, tells caller to stop processing
 *				events and terminate the thread.
 *
 */

bool
AudioPlayer::processEvent(Event* event)
{
    bool finished;

    // assume that we will continue processing events
    finished = false;

    // process event
    switch (event->eventType)
    {
    case kTimerEvent:
        processTimerEvent();
        break;
    case kAudioEvent:
        processAudioEvent(event->eventData);
        break;
    case kPrepareSourceEvent:
        enterPrepareSourceState();
        break;
    case kPlaySourceEvent:
        enterPlaySourceState();
        break;
    case kExitThreadEvent:
        LOG_NORMAL("%s(), exit event received.", __FUNCTION__);
        finished = true;
        break;
    default:
        LOG_ERROR("%s(), unexpected event type: %d", __FUNCTION__, event->eventType);
        break;
    }

    return finished;
}


/********************************************************************************
 *
 *	Function:	createThread
 *
 *	Purpose:	Create the audio player thread.  Steps:
 *				- set stack size
 *				- spawn the thread
 *				- set thread name
 *
 *				The newly created thread will begin execution
 *				at threadFunction().
 */

void
AudioPlayer::createThread(void)
{
    int error;
    pthread_attr_t attr;
    const char* threadName = "MSP AudioPlayer";

    // set attributes to default and set stack size
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kStackSize);

    // create thread and error check
    error = pthread_create(&apThread, &attr, threadFunction, this);
    if (error)
    {
        LOG_ERROR("pthread create error: %d", error);
    }

    // set thread name and error check
    error = pthread_setname_np(apThread, threadName);
    if (error)
    {
        LOG_ERROR("pthread set name error: %d", error);
    }
}


/********************************************************************************
 *
 *	Function:	threadFunction
 *
 *	Purpose:	Root function of the audio player thread.  In an infinite loop,
 *				perform a blocking read on the apQueue.  Each time an event
 *				is placed on this queue, process it, delete associated event
 *				data, and repeat.
 *
 *	Parameters:	data - not used
 *
 */

void*
AudioPlayer::threadFunction(void* data)
{
    AudioPlayer* instance = (AudioPlayer*) data;
    MSPEventQueue* eventQueue = instance->apQueue;
    assert(eventQueue);

    bool finished = false;;
    while (!finished)
    {
        Event* event = eventQueue->popEventQueue();
        instance->lockMutex();
        finished = instance->processEvent(event);
        if (event->eventData != NULL)
        {
            AudioPlayerEventData* data = (AudioPlayerEventData*) event->eventData;
            delete data;
        }
        eventQueue->freeEvent(event);
        instance->unLockMutex();
    }
    LOG_NORMAL("%s(), exiting audio player thread.", __FUNCTION__);
    pthread_exit(NULL);
    return NULL;
}


/********************************************************************************
 *
 *	Function:	queueEvent
 *
 *	Purpose:	Place an event on the audio player's internal queue.  Refer to
 *				threadFunction() to see where the event is processed.
 *
 *				This routine is for events which do not carry a payload.
 *
 *	Parameters:	type - type of event
 *
 */

void
AudioPlayer::queueEvent(eAudioPlayerEvent type)
{
    assert(apQueue);
    apQueue->dispatchEvent(type, NULL);
}


/********************************************************************************
 *
 *	Function:	queueEventPlusPayload
 *
 *	Purpose:	Place an event on the audio player's internal queue.  Refer to
 *				threadFunction() to see where the event is processed.
 *
 *				This routine is for events which carry a payload.  Notice that
 *				it it necessary to instantiate an AudioPlayerEventData structure
 *				to transport the payload.  Also notice that the
 *				instantiated structure is deleted in threadFunction() AFTER the
 *				event has been processed
 *
 *	Parameters:	type - type of event
 *				data - the event's payload
 *
 */

void
AudioPlayer::queueEventPlusPayload(eAudioPlayerEvent type, uint32_t data)
{
    assert(apQueue);
    AudioPlayerEventData* eventData = new AudioPlayerEventData;
    eventData->payload = data;
    apQueue->dispatchEvent(type, eventData);
}


/********************************************************************************
 *
 *	Function:	lockMutex
 *
 *	Purpose:	Lock the audio player mutex.
 *
 */

void
AudioPlayer::lockMutex(void)
{
    pthread_mutex_lock(&apMutex);
    mutexStack++;
    LOG_NORMAL("%s(), stack: %d", __FUNCTION__, mutexStack);
}


/********************************************************************************
 *
 *	Function:	unLockMutex
 *
 *	Purpose:	Unlock the audio player mutex.
 *
 */

void
AudioPlayer::unLockMutex(void)
{
    mutexStack--;
    LOG_NORMAL("%s(), stack: %d", __FUNCTION__, mutexStack);
    pthread_mutex_unlock(&apMutex);
}


/********************************************************************************
 *
 *	Function:	audioCallback
 *
 *	Purpose:	The audio playback logic calls this routine to communicate
 *				current status of a previous audio playback request.  Extract
 *				the status and queue the appropriate event.
 *
 *	Parameters:	type - type of audio playback event
 *				userdata- pointer to self
 *				pCallbackSpecific - not used
 *
 */

int
AudioPlayer::audioCallback(tCpeSrcCallbackTypes type, void* userdata,
                           void* pCallbackSpecific)
{
    (void) pCallbackSpecific;

    // get a pointer to self and then queue event
    AudioPlayer* instance = (AudioPlayer*) userdata;

    LOG_NORMAL("%s, instance: %p", __FUNCTION__, instance);

    instance->queueEventPlusPayload(kAudioEvent, type);
    return 0;
}


/********************************************************************************
 *
 *	Function:	timerCallback
 *
 *	Purpose:	The timer logic will call this routine when the audio player
 *				timer expires.  Simply queue a timer event.
 *
 *	Parameters:	data - pointer to an EventTimer structure.
 *				fd and what are not used
 *
 */

void
AudioPlayer::timerCallback(evutil_socket_t fd, short what, void* data)
{
    (void) fd;
    (void) what;

    EventTimer* timer = reinterpret_cast <EventTimer*>(data);
    AudioPlayer* instance = reinterpret_cast <AudioPlayer*>(timer->getUserData()) ;

    LOG_NORMAL("%s, instance: %p", __FUNCTION__, instance);

    instance->queueEvent(kTimerEvent);
}


/********************************************************************************
 *
 *	Function:	startTimer
 *
 *	Purpose:	Start the audio timer using the indicated timer period.
 *
 *	Parameters:	seconds - timer will expire in this many seconds
 *
 */

void
AudioPlayer::startTimer(int seconds)
{
    eTimer_StatusCode result;

    result = Timer_addTimerDuration(seconds, timerCallback,
                                    this, &timerId);
    LOG_NORMAL("%s, currentState: %d   result: %d    this: %p",
               __FUNCTION__, currentState, result, this);
}


/********************************************************************************
 *
 *	Function:	cancelTimer
 *
 *	Purpose:	If the audio player timer is running, cancel it.
 *
 *
 */

void
AudioPlayer::cancelTimer(void)
{
    // if a timer is active, cancel it
    if (timerId)
    {
        Timer_deleteTimer(timerId);
        timerId = 0;
    }
}


eIMediaPlayerStatus
AudioPlayer::doCallback(eIMediaPlayerSignal sig, eIMediaPlayerStatus stat)
{
    FNLOG(DL_MSP_ZAPPER);
    tIMediaPlayerCallbackData cbData;

    cbData.status = stat;
    cbData.signalType = sig;
    cbData.data[0] = '\0';

    std::list<CallbackInfo *>::iterator iter;

    for (iter = callbackList.begin(); iter != callbackList.end(); iter++)
    {
        if ((*iter)->mCallback != NULL)
        {
            (*iter)->mCallback((*iter)->mpSession, cbData, (*iter)->mClientContext, NULL);
        }
    }
    return  kMediaPlayerStatus_Ok;
}



eIMediaPlayerStatus
AudioPlayer::SetCallback(IMediaPlayerSession *pIMediaPlayerSession,
                         IMediaPlayerStatusCallback cb,
                         void* pClientContext)
{
    CallbackInfo *cbInfo = new CallbackInfo();
    if (cbInfo)
    {
        cbInfo->mpSession = pIMediaPlayerSession;
        cbInfo->mCallback = cb;
        cbInfo->mClientContext = pClientContext;
        callbackList.push_back(cbInfo);
    }
    else
    {
        LOG_EMERGENCY("%s(), Error: Unable to alloc mem", __FUNCTION__);
        assert(cbInfo);
    }

    return  kMediaPlayerStatus_Ok;
}


/*************************************************************************
 *
 *	DetachCallback
 *
 *
 */

eIMediaPlayerStatus
AudioPlayer::DetachCallback(IMediaPlayerStatusCallback cb)
{
    std::list<CallbackInfo*>::iterator iter;
    eIMediaPlayerStatus status = kMediaPlayerStatus_Error_InvalidParameter;
    for (iter = callbackList.begin(); iter != callbackList.end(); iter++)
    {
        if ((*iter)->mCallback == cb)
        {
            callbackList.remove(*iter);
            delete(*iter);
            status = kMediaPlayerStatus_Ok;
            break;
        }
    }
    return status;
}

/***********************************************************************

	An AudioPlayer object is a IMediaController which plays back
	PCM audio originating from a file.  Many of the methods specified
	by the base class don't apply to this type of audio playback.
	Below are 'dummy' implementations of these methods.

***********************************************************************/

eIMediaPlayerStatus
AudioPlayer::PersistentRecord(const char* recordUrl, float nptRecordStartTime, float nptRecordStopTime, const MultiMediaEvent **pMme)
{
    (void) recordUrl;
    (void) nptRecordStartTime;
    (void) nptRecordStopTime;
    (void) pMme;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::SetSpeed(int numerator, unsigned int denominator)
{
    (void) numerator;
    (void) denominator;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::GetSpeed(int* pNumerator, unsigned int* pDenominator)
{
    (void) pNumerator;
    (void) pDenominator;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::SetPosition(float nptTime)
{
    (void) nptTime;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::GetPosition(float* pNptTime)
{
    (void) pNptTime;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::IpBwGauge(const char *sTryServiceUrl, unsigned int *pMaxBwProvision, unsigned int *pTryServiceBw, unsigned int *pTotalBwConsumption)
{
    (void) sTryServiceUrl;
    (void) pMaxBwProvision;
    (void) pTryServiceBw;
    (void) pTotalBwConsumption;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::SetPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus)
{
    (void) vidScreenRect;
    (void) enablePictureModeSetting;
    (void) enableAudioFocus;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::GetStartPosition(float* pNptTime)
{
    (void) pNptTime;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::GetEndPosition(float* pNptTime)
{
    (void) pNptTime;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::RegisterCCICallback(void* data, CCIcallback_t cb)
{
    (void) data;
    (void) cb;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::UnRegisterCCICallback(void)
{
    return kMediaPlayerStatus_Error_NotSupported;
}

std::string
AudioPlayer::GetSourceURL(bool liveSrcOnly) const
{
    (void) liveSrcOnly;
    return srcUrl;
}

std::string
AudioPlayer::GetDestURL(void) const
{
    return NULL;
}

bool
AudioPlayer::isRecordingPlayback(void) const
{
    return false;
}

bool
AudioPlayer::isLiveRecording(void) const
{
    return false;
}

eIMediaPlayerStatus
AudioPlayer::SetApplicationDataPid(uint32_t aPid)
{
    (void) aPid;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::SetApplicationDataPidExt(IMediaPlayerClientSession *ApplnClient)
{
    (void) ApplnClient;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset)
{
    (void) info;
    (void) infoSize;
    (void) count;
    (void) offset;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::GetApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    (void) bufferSize;
    (void) buffer;
    (void) dataSize;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::GetApplicationDataExt(IMediaPlayerClientSession *ApplnClient, uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    (void) ApplnClient;
    (void) bufferSize;
    (void) buffer;
    (void) dataSize;
    return kMediaPlayerStatus_Error_NotSupported;
}

uint32_t AudioPlayer::GetSDVClentContext(IMediaPlayerClientSession *ApplnClient)
{
    (void) ApplnClient;
    return kMediaPlayerStatus_Error_NotSupported;
}
eIMediaPlayerStatus
AudioPlayer::SetAudioPid(uint32_t pid)
{
    (void) pid;
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::CloseDisplaySession(void)
{
    return kMediaPlayerStatus_Error_NotSupported;
}

eIMediaPlayerStatus
AudioPlayer::StartDisplaySession(void)
{
    return kMediaPlayerStatus_Error_NotSupported;
}

bool
AudioPlayer::isBackground(void)
{
    return false;
}

eCsciMspDiagStatus
AudioPlayer::GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo)
{
    (void) msgInfo;
    return kCsciMspDiagStat_NoData;
}

void
AudioPlayer::StopAudio(void)
{
    // the IMediaController base class requires an implementation
    //   of StopAudio.  Unlike the other instances of IMediaController,
    //   AudioPlayer does NOT have a DisplaySession member.  Because
    //   of this, AudioPlayer's implementation does nothing.
}


void
AudioPlayer::RestartAudio(void)
{
    // the IMediaController base class requires an implementation
    //   of RestartAudio.  Unlike the other instances of IMediaController,
    //   AudioPlayer does NOT have a DisplaySession member.  Because
    //   of this, AudioPlayer's implementation does nothing.
}



void
AudioPlayer::addClientSession(IMediaPlayerClientSession *pClientSession)
{
    (void) pClientSession;
    // the IMediaController base class requires an implementation
    //   of RestartAudio.  Unlike the other instances of IMediaController,
    //   AudioPlayer does NOT have a DisplaySession member.  Because
    //   of this, AudioPlayer's implementation does nothing.
}

void
AudioPlayer::deleteClientSession(IMediaPlayerClientSession *pClientSession)
{
    (void) pClientSession;
    // the IMediaController base class requires an implementation
    //   of RestartAudio.  Unlike the other instances of IMediaController,
    //   AudioPlayer does NOT have a DisplaySession member.  Because
    //   of this, AudioPlayer's implementation does nothing.
}

void
AudioPlayer::deleteAllClientSession()
{
    // the IMediaController base class requires an implementation
    //   of RestartAudio.  Unlike the other instances of IMediaController,
    //   AudioPlayer does NOT have a DisplaySession member.  Because
    //   of this, AudioPlayer's implementation does nothing.
}

tCpePgrmHandle AudioPlayer::getCpeProgHandle()
{
    return 0;
}

void AudioPlayer::SetCpeStreamingSessionID(uint32_t sessionId)
{
    (void) sessionId;
    return;
}

void AudioPlayer::InjectCCI(uint8_t CCIbyte)
{
    LOG_NORMAL("InjectCCI is not supported in audio player");
    (void)CCIbyte;

}

eIMediaPlayerStatus AudioPlayer::StopStreaming()
{
    // By default the IMediaController base class requires an implementation
    // of StopStreaming since its an abstract class.
    return kMediaPlayerStatus_Error_NotSupported;
}

// Sets the EAS audio playback active status
// True  - EAS audi playback is in progress
// False - EAS audio playback is not in progress
void AudioPlayer::SetEasAudioActive(bool active)
{
    mEasAudioActive = active;
}

