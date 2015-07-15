/**
   \file Mrdvr_ic.cpp
   \class mrdvr

    Implementation file for MRDVR media controller class
*/


///////////////////////////////////////////////////////////////////////////
//                    Standard Includes
///////////////////////////////////////////////////////////////////////////
#include <list>
#include <assert.h>
#if defined(DMALLOC)
#include "dmalloc.h"
#endif

//////////////////////////////////////////////////////////////////////////
//                    SAIL Includes
///////////////////////////////////////////////////////////////////////////
#include <sail-clm-api.h>
#include <dlog.h>
#include <Cam.h>


///////////////////////////////////////////////////////////////////////////
//                    Local Includes
///////////////////////////////////////////////////////////////////////////
#include "mrdvr_ic.h"
#include "eventQueue.h"

#include "IMediaPlayer.h"
#include "MSPSourceFactory.h"


#include "pthread_named.h"


#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_MRDVR, level,"MrDvr:%s:%d " msg, __FUNCTION__, __LINE__, ##args);



///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

#define UNUSED_PARAM(a) (void)a;
//static volatile bool tunerLocked = false;       // DEBUG ONLY

///////////////////////////////////////////////////////////////////////////
//                      Member implementation
///////////////////////////////////////////////////////////////////////////

bool Mrdvr::handleEvent(Event *aEvt)
{
    int status;
    FNLOG(DL_MSP_MRDVR);

    if (aEvt == NULL)
    {
        LOG(DLOGL_ERROR, "Error: MRDVR handleEvent is NULL");
        return false;
    }

    switch (aEvt->eventType)
    {
        // exit
    case kZapperEventExit:
        LOG(DLOGL_REALLY_NOISY, "MRDVR thread exit call");
        return true;
        // no break required here

    case kZapperTimeOutEvent:
        LOG(DLOGL_REALLY_NOISY, "PSI Data not ready");
        DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
        break;

    case kZapperEventStop:
        LOG(DLOGL_REALLY_NOISY, "MRDVR stop event");
        break;

    case kZapperEventPlay:
        LOG(DLOGL_REALLY_NOISY, "Play event");
        if (mSource)
        {
            // handle load for Mrdvr here
            status = mSource->open(kRMPriority_VideoWithAudioFocus);
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, " %s:%d Load MRDVR Source failed ", __FUNCTION__, __LINE__);
                DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
            }
            else
            {
                queueEvent(kZapperEventPlayBkCallback);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Source Type is Unknown");
        }
        break;


    case kZapperEventPlayBkCallback:
        LOG(DLOGL_REALLY_NOISY, "Playback callback event");
        if (mSource)
        {
            /* handle start for Mrdvr here */
            status = mSource->start();
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, " %s:%d Start MRDVR Source failed ", __FUNCTION__, __LINE__);
                DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Source Type is Unknown");
        }
        break;

    case kZapperPMTReadyEvent:
        dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "kZapperPMTReadyEvent received successfully");
        break;

    case kZapperAnalogPSIReadyEvent:
    case kZapperPSIReadyEvent:
        dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "kZapperPSIReadyEvent received successfully");

        if (mSource)
        {
            LOG(DLOGL_REALLY_NOISY, "Create psi");
            mPsi = new Psi();

            if (mPsi != NULL)
            {
                eMspStatus status = kMspStatus_Ok;

                LOG(DLOGL_REALLY_NOISY, "Start psi");
                status = mPsi->psiStart(mSource);
                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "Error: Start psi failed");
                    DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
                }
                else
                {
                    LOG(DLOGL_REALLY_NOISY, "Select Audio Video Components with EAS being %s", (mEasAudioActive == true) ? "TRUE" : "FALSE");
                    status = mSource->SelectAudioVideoComponents(mPsi, mEasAudioActive);
                    if (status != kMspStatus_Ok)
                    {
                        LOG(DLOGL_ERROR, "Error: selecting AUDIO and VIDEO components failed");
                        DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
                    }
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "Error: Memory allocation failed");
                DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ServiceNotAvailable);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Error: Invalid source");
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ServiceNotAvailable);
        }
        break;

    case kZapperFirstFrameEvent:

        if (state != kZapperStateRendering)
        {
            if (mSource)
            {
                /* Playback started... */
                /* Lets register for the audio language change notifications */
                mSource->SetAudioLangCB(this, audioLanguageChangedCB);

                eMspStatus status = Avpm::getAvpmInstance()->connectOutput();
                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "connectOuput error %d", status);
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "NULL source. Ignoring audio language setting");
            }
        }

        dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "Video Presentation started successfully");
        DoCallback(kMediaPlayerSignal_PresentationStarted, kMediaPlayerStatus_Ok);
        state = kZapperStateRendering;

        break;

    case kZapperSrcBOF:
        LOG(DLOGL_REALLY_NOISY, "handle kZapperSrcBOF event");
        DoCallback(kMediaPlayerSignal_BeginningOfStream, kMediaPlayerStatus_Ok);
        break;

    case kZapperSrcEOF:
        LOG(DLOGL_REALLY_NOISY, "handle kZapperSrcEOF event");
        DoCallback(kMediaPlayerSignal_EndOfStream, kMediaPlayerStatus_Ok);
        break;

    case kZapperSrcProblem:
        LOG(DLOGL_REALLY_NOISY, "handle kZapperSrcProblem event");
        if (state == kZapperStateRendering)
        {
            LOG(DLOGL_REALLY_NOISY, "sending server error to MDA");
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ServerError);
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, " sending content not found error to MDA");
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
        }
        break;

    case kZapperSrcPendingSetSpeed:
        LOG(DLOGL_REALLY_NOISY, "Pending SetSpeed callback.Speed setting can be applied now");
        SetSpeed(mSpeedNumerator, mSpeedDenominator);
        break;

    case kZapperSrcPendingSetPosition:
        LOG(DLOGL_REALLY_NOISY, "Pending SetPosition callback. Position setting can be applied now");
        SetPosition(mPosition);
        break;

    case kZapperEventAudioLangChangeCb:

        if (mSource)
        {
            mSource->UpdateAudioLanguage(mPsi);
        }
        else
        {
            LOG(DLOGL_ERROR, "NULL display source. Ignoring audio language changed setting");
        }
        break;
    }

    return false;
}

Mrdvr::Mrdvr()
{
    FNLOG(DL_MSP_MRDVR);
    mSource = NULL;

    // create event queue for scan thread
    enaPicMode = true;
    enaAudio = true;

    mSpeedNumerator = 100;
    mSpeedDenominator = 100;
    mPosition = 0;
    mNptSetPos = 0;
}

Mrdvr::~Mrdvr()
{
    FNLOG(DL_MSP_MRDVR);
    LOG(DLOGL_REALLY_NOISY, " Mrdvr::~Mrdvr");
}

/******************************************************************************
 * Sets the requested speed for the recording playback
 * Returns kMediaPlayerStatus_Ok on Success
 *****************************************************************************/
eIMediaPlayerStatus Mrdvr::SetSpeed(int numerator, unsigned int denominator)
{
    eMspStatus status = kMspStatus_Ok;
    FNLOG(DL_MSP_MRDVR);

    LOG(DLOGL_REALLY_NOISY, " Inside Set Speed ant_num = %d  ant_den = %d", numerator, denominator);

    if (state != kZapperStateRendering)
    {
        LOG(DLOGL_REALLY_NOISY, "SetSpeed during wrong state. state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }
    else if (denominator == 0)
    {
        LOG(DLOGL_ERROR, "Error invalid params denom:");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    if (mSource != NULL)
    {
        mSpeedNumerator = numerator;
        mSpeedDenominator = denominator;

        status = mSource->setSpeed(numerator, denominator, 0);
        if (status == kMspStatus_Loading)
        {
            LOG(DLOGL_REALLY_NOISY, "HTTP source is not ready for speed settings.Delaying it");
            return kMediaPlayerStatus_Ok;
        }
        else if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "setSpeed failed to set %d", status);
            return kMediaPlayerStatus_ContentNotFound;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Source is NULL");
        return kMediaPlayerStatus_Error_Unknown;
    }

    return  kMediaPlayerStatus_Ok;
}

/******************************************************************************
 * Gets the current speed of the recording playback
 * Returns kMediaPlayerStatus_Ok on Success
 *****************************************************************************/
eIMediaPlayerStatus Mrdvr::GetSpeed(int* pNumerator, unsigned int* pDenominator)
{
    FNLOG(DL_MSP_MRDVR);


    if (state != kZapperStateRendering)
    {
        LOG(DLOGL_REALLY_NOISY, "GetSpeed during wrong state. state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if ((pNumerator == NULL) || (pDenominator == NULL))
    {
        LOG(DLOGL_ERROR, "NULL *numerator or *denominator in GetSpeed");
        return  kMediaPlayerStatus_Error_InvalidParameter;
    }

    *pNumerator = mSpeedNumerator;
    *pDenominator = mSpeedDenominator;

    return  kMediaPlayerStatus_Ok;
}

/******************************************************************************
 * Sets the position requested and starts the recording playback from the new
 * position requested.
 * Returns kMediaPlayerStatus_Ok on Success
 *****************************************************************************/
eIMediaPlayerStatus Mrdvr::SetPosition(float nptTime)
{
    FNLOG(DL_MSP_MRDVR);

    LOG(DLOGL_REALLY_NOISY, "\n  SetPosition pNptTime = %f  ", nptTime);

    if (state != kZapperStateRendering)
    {
        LOG(DLOGL_REALLY_NOISY, "SetPosition during wrong state. state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (nptTime == MEDIA_PLAYER_NPT_START)
    {
        return kMediaPlayerStatus_Ok;  //ignore this.
    }
    else if (nptTime == MEDIA_PLAYER_NPT_NOW)
    {
        return kMediaPlayerStatus_Ok;  //ignore this.
    }
    else if (nptTime == MEDIA_PLAYER_NPT_END)
    {
        LOG(DLOGL_ERROR, " Invalid NPT option of NPT_END for a recorded video");
        return kMediaPlayerStatus_Error_Unknown;
    }
    else
    {
        if (nptTime < 0)
        {
            LOG(DLOGL_ERROR, "NPT can't be negative.Invalid NPT");
            return kMediaPlayerStatus_Error_Unknown;   //NPT can't be negative.other NPT_END
        }
    }

    if (mSource != NULL)
    {
        mPosition = nptTime;
        eMspStatus status = mSource->setPosition(nptTime);
        if (status == kMspStatus_Loading)
        {
            LOG(DLOGL_REALLY_NOISY, "HTTP player is not ready for position setting.Delaying the position setting");
            return kMediaPlayerStatus_Ok;
        }
        else if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "setPosition failed to set %d", status);
            return kMediaPlayerStatus_ContentNotFound;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Source is NULL");
        return kMediaPlayerStatus_Error_Unknown;
    }

    return  kMediaPlayerStatus_Ok;
}

/******************************************************************************
 * Gets the current position of the recording playback
 * Returns kMediaPlayerStatus_Ok on Success
 *****************************************************************************/
eIMediaPlayerStatus Mrdvr::GetPosition(float* pNptTime)
{
    FNLOG(DL_MSP_MRDVR);

    if (state != kZapperStateRendering)
    {
        LOG(DLOGL_REALLY_NOISY, "GetPosition during wrong state. state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (pNptTime)
    {
        if (mSource != NULL)
        {
            eMspStatus status = mSource->getPosition(pNptTime);
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "Get Position Failed");
                return kMediaPlayerStatus_ContentNotFound;
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "GetPosition = %f", *pNptTime);
                return  kMediaPlayerStatus_Ok;
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Source is NULL");
            return kMediaPlayerStatus_Error_Unknown;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "NULL input parameter");
        return  kMediaPlayerStatus_Error_InvalidParameter;
    }
}

/******************************************************************************
 * Gets the start position of the recording playback
 * Returns kMediaPlayerStatus_Ok on Success
 *****************************************************************************/
eIMediaPlayerStatus Mrdvr::GetStartPosition(float* pNptTime)
{
    FNLOG(DL_MSP_MRDVR);

    if (state != kZapperStateRendering)
    {
        LOG(DLOGL_REALLY_NOISY, "GetPosition during wrong state. state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (pNptTime == NULL)
    {
        LOG(DLOGL_NORMAL, "GetStartPosition:NPT parameter is NULL");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }
    else
    {
        *pNptTime = 0.0;
        return kMediaPlayerStatus_Ok;
    }
}

/** *********************************************************
 */
eIMediaPlayerStatus Mrdvr::SetApplicationDataPid(uint32_t aPid)
{
    FNLOG(DL_MSP_MRDVR);

    UNUSED_PARAM(aPid)
    return  kMediaPlayerStatus_Error_NotSupported;
}

/** *********************************************************
 */
eIMediaPlayerStatus Mrdvr::GetApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    FNLOG(DL_MSP_MRDVR);

    UNUSED_PARAM(bufferSize)
    UNUSED_PARAM(buffer)
    UNUSED_PARAM(dataSize)

    return kMediaPlayerStatus_Error_NotSupported;

}


/** *********************************************************
 */
eIMediaPlayerStatus Mrdvr::StartDisplaySession()
{
    FNLOG(DL_MSP_MRDVR);

    return kMediaPlayerStatus_Ok;
}


/** *********************************************************
 */
eIMediaPlayerStatus Mrdvr::loadSource()
{
    FNLOG(DL_MSP_MRDVR);

    if (state != kZapperStateIdle)
    {
        LOG(DLOGL_ERROR, "error: bad state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (!mSource)
    {
        LOG(DLOGL_SIGNIFICANT_EVENT, "error: null msource");
        return kMediaPlayerStatus_Error_Unknown;
    }

    eIMediaPlayerStatus mediaPlayerStatus;

    eMspStatus status = mSource->load(sourceCB, this);
    if (status == kMspStatus_Ok)
    {
        int err = createEventThread();
        if (!err)
        {
            // TODO: Create display and psi objects here ??
            state = kZapperStateStop;
            LOG(DLOGL_REALLY_NOISY, "state = kZapperStateStop");
            mediaPlayerStatus = kMediaPlayerStatus_Ok;
        }
        else
        {
            LOG(DLOGL_EMERGENCY, "Error: unable to start thread");
            mediaPlayerStatus = kMediaPlayerStatus_Error_Unknown;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "load failed");
        mediaPlayerStatus = kMediaPlayerStatus_Error_InvalidURL;
    }

    return mediaPlayerStatus;
}

/** *********************************************************
 *
 */
void Mrdvr::sourceCB(void *aData, eSourceState aSrcState)
{
    Mrdvr *inst = (Mrdvr *)aData;
    FNLOG(DL_MSP_MRDVR);

    LOG(DLOGL_REALLY_NOISY, "source callback cames up with the state as %d", aSrcState);

    if (inst != NULL)
    {
        switch (aSrcState)
        {
        case kSrcBOF:
            LOG(DLOGL_REALLY_NOISY, " handle kSrcBOF event");
            inst->queueEvent(kZapperSrcBOF);
            break;

        case kSrcEOF:
            LOG(DLOGL_REALLY_NOISY, " handle kSrcEOF event");
            inst->queueEvent(kZapperSrcEOF);
            break;

        case kSrcProblem:
            LOG(DLOGL_REALLY_NOISY, " handle kSrcProblem event");
            inst->queueEvent(kZapperSrcProblem);
            break;

        case kSrcPendingSetSpeed:
            LOG(DLOGL_REALLY_NOISY, " handle kSrcPendingSetSpeed event");
            inst->queueEvent(kZapperSrcPendingSetSpeed);
            break;

        case kSrcPendingSetPosition:
            LOG(DLOGL_REALLY_NOISY, " handle kSrcPendingSetPosition event");
            inst->queueEvent(kZapperSrcPendingSetPosition);
            break;

        case kSrcPMTReadyEvent:
            LOG(DLOGL_REALLY_NOISY, "%s: handle kSrcPMTReadyEvent event", __FUNCTION__);
            inst->queueEvent(kZapperPMTReadyEvent);
            break;

        case kSrcPSIReadyEvent:
            LOG(DLOGL_REALLY_NOISY, "%s: handle kSrcPSIReadyEvent event", __FUNCTION__);
            inst->queueEvent(kZapperPSIReadyEvent);
            break;

        case kSrcFirstFrameEvent:
            LOG(DLOGL_REALLY_NOISY, "%s: handle kSrcFirstFrameEvent event", __FUNCTION__);
            inst->queueEvent(kZapperFirstFrameEvent);
            break;

        default:
            LOG(DLOGL_NORMAL, " Not a valid callback event");
            break;
        }
    }
}


bool Mrdvr::isRecordingPlayback()const
{
    return true;
}

bool Mrdvr::isLiveSourceUsed() const
{
    return false;
}

eCsciMspDiagStatus Mrdvr::GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo)
{
    FNLOG(DL_MSP_MRDVR);
    UNUSED_PARAM(msgInfo);

    return kCsciMspDiagStat_NoData;

}

eCsciMspDiagStatus Mrdvr::GetMspStreamingInfo(DiagMspStreamingInfo *msgInfo)
{
    FNLOG(DL_MSP_MRDVR);
    if (mSource)
    {
        return mSource->GetMspStreamingInfo(msgInfo);
    }
    else
    {
        return kCsciMspDiagStat_NoData;
    }
}

/******************************************************************************
 * Starts the recording playback
 * Returns kMediaPlayerStatus_Ok on Success
 *****************************************************************************/
eIMediaPlayerStatus Mrdvr::Play(const char* outputUrl,
                                float nptStartTime,
                                const MultiMediaEvent **pMme)
{
    FNLOG(DL_MSP_ZAPPER);

    UNUSED_PARAM(pMme)

    eIMediaPlayerStatus retStatus = kMediaPlayerStatus_Ok;

    LOG(DLOGL_MINOR_EVENT, "outputUrl: %s  state: %d startTime %f", outputUrl, state, nptStartTime);

    if ((state != kZapperStateStop) && (state != kZapperWaitSourceReady))
    {
        LOG(DLOGL_ERROR, "Error state: %d", state);
        return kMediaPlayerStatus_Error_OutOfState;
    }

    if (!outputUrl)
    {
        LOG(DLOGL_ERROR, "Error null URL");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    dest_url = outputUrl;

    if ((dest_url.find("decoder://primary") != 0) && (dest_url.find("decoder://secondary") != 0))
    {
        LOG(DLOGL_ERROR, "Error destUrl: %s", dest_url.c_str());
        return kMediaPlayerStatus_Error_InvalidParameter;
    }

    if (mSource)
    {
        eMspStatus status = kMspStatus_Ok;

        /* handle load for Mrdvr here */
        status = mSource->open(kRMPriority_VideoWithAudioFocus);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, " %s:%d Load MRDVR Source failed ", __FUNCTION__, __LINE__);
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
            retStatus = kMediaPlayerStatus_Error_Unknown;
        }
        else
        {
            /* set the playback start position requested */
            if (nptStartTime != 0)
            {
                LOG(DLOGL_REALLY_NOISY, "%s:%d  SetPosition : %f ", __FUNCTION__, __LINE__, nptStartTime);
                status = mSource->setPosition(nptStartTime);
                if (status != kMspStatus_Ok)
                {
                    LOG(DLOGL_ERROR, "%s:%d setPosition failed ", __FUNCTION__, __LINE__);
                }
            }

            /* start the source */
            status = mSource->start();
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, "%s: Starting source failed unfortunately\n", __FUNCTION__);
                LOG(DLOGL_ERROR, " %s:%d Start MRDVR Source failed ", __FUNCTION__, __LINE__);
                DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
                retStatus = kMediaPlayerStatus_Error_Unknown;
            }
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Source Type is Unknown");
        LOG(DLOGL_ERROR, "Error state: %d", state);
        retStatus = kMediaPlayerStatus_Error_OutOfState;
    }

    return  retStatus;
}

/******************************************************************************
 * Gets the end position of the recording playback
 * Returns always kMediaPlayerStatus_Error_NotSupported
 *****************************************************************************/
eIMediaPlayerStatus Mrdvr::GetEndPosition(float* pNptTime)
{
    FNLOG(DL_MSP_ZAPPER);
    UNUSED_PARAM(pNptTime)

    return  kMediaPlayerStatus_Error_NotSupported;
}

/******************************************************************************
 * Returns the CCI byte value
 *****************************************************************************/
uint8_t Mrdvr::GetCciBits(void)
{
    FNLOG(DL_MSP_MRDVR);

    uint8_t cciData = 0;

    if (mSource)
    {
        cciData =  mSource->GetCciBits();
        LOG(DLOGL_REALLY_NOISY, "%s %s ::: %d", __FUNCTION__, "CCI BYTE = ", cciData);
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid Source", __FUNCTION__);
    }

    return cciData;
}
