/**
   \file Mrdvr.cpp
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

///////////////////////////////////////////////////////////////////////////
//                    CPE Includes
///////////////////////////////////////////////////////////////////////////
#include <cpe_error.h>
#include <cpe_source.h>
#include <directfb.h>
#include <glib.h>
#include "sys/xattr.h"
//////////////////////////////////////////////////////////////////////////
//                    SAIL Includes
///////////////////////////////////////////////////////////////////////////
#include <sail-clm-api.h>
#include <dlog.h>
#include <Cam.h>


///////////////////////////////////////////////////////////////////////////
//                    Local Includes
///////////////////////////////////////////////////////////////////////////
#include "mrdvr.h"
#include "DisplaySession.h"
#include "psi.h"
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



/** *********************************************************
*/
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
        LOG(DLOGL_NORMAL, "MRDVR thread exit call");
        return true;
        // no break required here


        // no break required here
    case kZapperTimeOutEvent:
        LOG(DLOGL_NORMAL, "PSI Data not ready");
        DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
        break;

    case kZapperEventStop:
        LOG(DLOGL_NORMAL, "MRDVR stop event");
        break;


    case kZapperEventPlay:
        LOG(DLOGL_NORMAL, "Play event");
        if (mSource)
        {
            LOG(DLOGL_NORMAL, "mSource is not NULL");
            // handle load for Mrdvr here
            status = mSource->open(kRMPriority_VideoWithAudioFocus);
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, " %s:%d Load MRDVR Source failed ", __FUNCTION__, __LINE__);
                DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
            }
            else
            {
                if (mNptPendingSeek != 0)
                {
                    LOG(DLOGL_NOISE, "%s:%d  SetPosition : %f ", __FUNCTION__, __LINE__, mNptPendingSeek);
                    status = mSource->setPosition(mNptPendingSeek);
                    if (status != kMspStatus_Ok)
                    {
                        LOG(DLOGL_ERROR, "%s:%d setPosition failed ", __FUNCTION__, __LINE__);
                    }
                }

                queueEvent(kZapperEventPlayBkCallback);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Source Type is Unknown");
        }
        break;


    case kZapperEventPlayBkCallback:
        LOG(DLOGL_NORMAL, "Playback event event");
        if (psi == NULL)
        {
            psi = new Psi();
        }
        if (psi)
        {
            psi->registerPsiCallback(psiCallback, this);
            // get psi started then wait for PSI ready callback
            psi->psiStart(mSource);
        }
        break;

    case kZapperPSIReadyEvent:
        LOG(DLOGL_NORMAL, "PSI ready  event");
        if (mSource)
        {
            status = mSource->start();
            if (status != kMspStatus_Ok)
            {
                LOG(DLOGL_ERROR, " %s:%d Start MRDVR Source failed ", __FUNCTION__, __LINE__);
                DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
            }
            else
            {
                LOG(DLOGL_NORMAL, " %s:%d MRDVR Source Started Successfully ", __FUNCTION__, __LINE__);
            }

            status = StartDisplaySession();
        }
        else
        {
            LOG(DLOGL_ERROR, "Error: PSI ready event mSource = NULL");
        }
        break;


    case kZapperFirstFrameEvent:
        LOG(DLOGL_NORMAL, "kZapperFirstFrameEvent");
        DoCallback(kMediaPlayerSignal_PresentationStarted, kMediaPlayerStatus_Ok);
        state = kZapperStateRendering;
        break;

    case kZapperSrcBOF:
        LOG(DLOGL_NORMAL, "handle kZapperSrcBOF event");
        DoCallback(kMediaPlayerSignal_BeginningOfStream, kMediaPlayerStatus_Ok);
        break;

    case kZapperSrcEOF:
        LOG(DLOGL_NORMAL, "handle kZapperSrcEOF event");
        DoCallback(kMediaPlayerSignal_EndOfStream, kMediaPlayerStatus_Ok);
        break;

    case kZapperSrcProblem:
        LOG(DLOGL_ERROR, "handle kZapperSrcProblem event");
        if (state == kZapperStateRendering)
        {
            LOG(DLOGL_ERROR, "sending server error to MDA");
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ServerError);
        }
        else
        {
            LOG(DLOGL_ERROR, " sending content not found error to MDA");
            DoCallback(kMediaPlayerSignal_Problem, kMediaPlayerStatus_ContentNotFound);
        }
        break;

    case kZapperSrcPendingSetSpeed:
        LOG(DLOGL_NORMAL, "Pending SetSpeed callback.Speed setting can be applied now");
        SetSpeed(mSpeedNumerator, mSpeedDenominator);
        break;

    case kZapperSrcPendingSetPosition:
        LOG(DLOGL_NORMAL, "Pending SetPosition callback. Position setting can be applied now");
        SetPosition(mPosition);
        break;

    case kZapperEventAudioLangChangeCb:
        if (disp_session)
        {
            disp_session->updatePids(psi);
        }
        else
        {
            LOG(DLOGL_ERROR, "NULL Display session.Ignoring audio language changed setting");
        }
        break;

    }
    return false;
}



/** *********************************************************
 */
eMspStatus Mrdvr::setSpeedMode(int num, unsigned int den, tCpePlaySpeed *pPlaySpeed)
{

    FNLOG(DL_MSP_MRDVR);
    if (pPlaySpeed)
    {
        if (num > 0)
        {
            pPlaySpeed->mode = eCpePlaySpeedMode_Forward;
            LOG(DLOGL_NORMAL, " SetSpeedMode:: eCpePlaySpeedMode_Forward ");
        }
        else if (num < 0)
        {
            pPlaySpeed->mode = eCpePlaySpeedMode_Rewind;
            LOG(DLOGL_NORMAL, " SetSpeedMode:: eCpePlaySpeedMode_Rewind ");
        }
        else if (num == 0)
        {
            pPlaySpeed->mode = eCpePlaySpeedMode_Pause;
            LOG(DLOGL_NORMAL, " SetSpeedMode:: eCpePlaySpeedMode_Pause ");
        }

        if (num <= 0 || num >= 100)
        {
            if ((num == 0) || ((num % 100) != 0)) //avoid dividing zero by 100,incase of pause and supporting Sailtest speed parameters.
            {
                pPlaySpeed->scale.numerator = abs(num);
                mSpeedNumerator = num;
            }
            else
            {
                pPlaySpeed->scale.numerator = abs(num) / 100; //CPERP doesn't works with Num/Den in 100/100 or 200/100 ... formats.
                mSpeedNumerator = num;
            }

            if ((den % 100) != 0) //divide by 100,only if speed parameters are multiples of 100
            {
                pPlaySpeed->scale.denominator = abs((long int)den);
                mSpeedDenominator = den;
            }
            else
            {
                pPlaySpeed->scale.denominator = abs((long int)den) / 100;
                mSpeedDenominator = den;
            }
        }
        else
        {
            den = den / num;
            num = 1;
            pPlaySpeed->scale.numerator = abs(num);
            pPlaySpeed->scale.denominator = abs((long int)den);

        }




        LOG(DLOGL_NORMAL, "  ant_num = %d  ant_den = %d", num, den);
        LOG(DLOGL_NORMAL, " cpe_num = %d  cpe_den = %d", pPlaySpeed->scale.numerator, pPlaySpeed->scale.denominator);
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: Null pPlaySpeed");
        return kMspStatus_Error;
    }
    return kMspStatus_Ok;

}



/** *********************************************************
  \returns always returns kMediaPlayerStatus_Error_NotSupported
 */
eIMediaPlayerStatus Mrdvr::SetSpeed(int numerator, unsigned int denominator)
{
    int status;
    tCpePlaySpeed playSpeed;
    FNLOG(DL_MSP_MRDVR);

    LOG(DLOGL_NORMAL, " Inside Set Speed ant_num = %d  ant_den = %d", numerator, denominator);

    if (disp_session == NULL)
    {
        LOG(DLOGL_ERROR, "ERROR: No display SetSpeed");
        return kMediaPlayerStatus_Error_Unknown;
    }

    if ((setSpeedMode(numerator, denominator, &playSpeed)) == kMspStatus_Ok)
    {
        disp_session->controlMediaAudio(playSpeed);
        LOG(DLOGL_NORMAL, " After disp_session->ControlAudio ");

        status = mSource->setSpeed(playSpeed, 0);
        if (status == kMspStatus_Loading)
        {
            LOG(DLOGL_NORMAL, "HTTP source is not ready for speed settings.Delaying it");
            return kMediaPlayerStatus_Ok;
        }
        else if (status != kMspStatus_Ok)
        {
            return kMediaPlayerStatus_Error_Unknown;
        }
        status = disp_session->setMediaSpeed(playSpeed);
        if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, " cpe_media_set failed for disp_session->setSpeed, Error = %d", status);
            return kMediaPlayerStatus_Error_CodecNotSupported;//TODO:check proper return value
        }

    }
    else
    {
        LOG(DLOGL_ERROR, "SetSpeed:: Not Set--- Wrong Parameters ");
        return kMediaPlayerStatus_Error_InvalidParameter;
    }


    return  kMediaPlayerStatus_Ok;
}



/** *********************************************************
 */
eIMediaPlayerStatus Mrdvr::GetSpeed(int* pNumerator, unsigned int* pDenominator)
{
    FNLOG(DL_MSP_MRDVR);


    if (state != kZapperStateRendering)
    {
        LOG(DLOGL_ERROR, "GetSpeed during wrong state. state: %d", state);
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


/** *********************************************************
*/
eIMediaPlayerStatus Mrdvr::SetPosition(float nptTime)
{
    FNLOG(DL_MSP_MRDVR);

    LOG(DLOGL_NORMAL, "\n  SetPosition pNptTime = %f  ", nptTime);

    if (nptTime == MEDIA_PLAYER_NPT_START)
    {
        return kMediaPlayerStatus_Ok;  //ignore this.   //temporary workaround to avoid trickplay problem,due to ANT calling wrong.TODO-Remove this,once ANT resolves issue
    }
    else if (nptTime == MEDIA_PLAYER_NPT_NOW)
    {
        return kMediaPlayerStatus_Ok;  //ignore this.
    }
    else if (nptTime == MEDIA_PLAYER_NPT_END)          //TODO- need to know,what ANT will call for Live TV press on recorded video keypress...ARUN
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
        //currently not having condition checking for nptTime > max possible.,its not possible with recorded video.
        //I assume,that condition might be handled by CPERP itself.. ARUN
    }

    if (mSource != NULL)
    {
        mPosition = nptTime;
        eMspStatus status = mSource->setPosition(nptTime);
        if (status == kMspStatus_Loading)
        {
            LOG(DLOGL_NORMAL, "HTTP player is not ready for position setting.Delaying the position setting");
            return kMediaPlayerStatus_Ok;
        }
        else if (status != kMspStatus_Ok)
        {
            LOG(DLOGL_ERROR, "cpe_src_Set failed to set eCpeSrcNames_CurrentNPT, Error = %d", status);
            return kMediaPlayerStatus_Error_Unknown;
        }
        else
        {
            //need to cleanup platform media buffer's outdated frames.else it causes sync issues,when new frames from new position get pumped in
            if (state == kZapperStateRendering)
            {
                LOG(DLOGL_NORMAL, "media start and stop");
                status = disp_session->stopMedia();
                status = disp_session->startMedia();
            }

            LOG(DLOGL_REALLY_NOISY, " cpe_src_Set(...) success ");
        }
    }
    else
    {
        return kMediaPlayerStatus_Error_Unknown;   //NPT can't be negative.other NPT_END
    }
    return  kMediaPlayerStatus_Ok;
}




/** *********************************************************
 */
eIMediaPlayerStatus Mrdvr::GetPosition(float* pNptTime)
{

    FNLOG(DL_MSP_MRDVR);

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
    }

    return  kMediaPlayerStatus_Error_OutOfState;
}





/** *********************************************************
 */
eIMediaPlayerStatus Mrdvr::GetStartPosition(float* pNptTime)
{

    FNLOG(DL_MSP_MRDVR);
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
Mrdvr::~Mrdvr()
{
    FNLOG(DL_MSP_MRDVR);


    LOG(DLOGL_REALLY_NOISY, " Mrdvr::Eject");
}

/** *********************************************************
 */
Mrdvr::Mrdvr()
{
    FNLOG(DL_MSP_MRDVR);
    mFileName = "";
    mSource = NULL;

    // create event queue for scan thread
    enaPicMode = true;
    enaAudio = true;
    mPtrCBData = NULL;
    mCCICBFn = NULL;

    mSpeedNumerator = 100;
    mSpeedDenominator = 100;
    mPosition = 0;
    mNptSetPos = 0;
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

    disp_session = new DisplaySession();
    if (disp_session)
    {
        mDisplaySessioncallbackConnection = disp_session->setCallback(boost::bind(&Mrdvr::displaySessionCallbackFunction, this, _1, _2));

        disp_session->registerDisplayMediaCallback(this, mediaCB);

        MSPSource *source = NULL;
        source = mSource;

        disp_session->SetCCICallback(mPtrCBData, mCCICBFn);
        if (enaAudio) // Check if its Pip Session
        {
            disp_session->SetAudioLangCB(this, audioLanguageChangedCB);
        }
        disp_session->setVideoWindow(screenRect, enaAudio);
        disp_session->updatePids(psi);
        disp_session->open(source);
        disp_session->start(mEasAudioActive);
    }
    else
    {
        LOG(DLOGL_EMERGENCY, "Error: Null disp_session");
        return kMediaPlayerStatus_Error_Unknown;
    }

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

    LOG(DLOGL_NORMAL, "source callback cames up with the state as %d", aSrcState);

    if (inst != NULL)
    {
        switch (aSrcState)
        {

        case kSrcBOF:
            LOG(DLOGL_NORMAL, " handle kSrcBOF event");
            inst->queueEvent(kZapperSrcBOF);
            break;

        case kSrcEOF:
            LOG(DLOGL_NORMAL, " handle kSrcEOF event");
            inst->queueEvent(kZapperSrcEOF);
            break;

        case kSrcProblem:
            LOG(DLOGL_ERROR, " handle kSrcProblem event");
            inst->queueEvent(kZapperSrcProblem);
            break;

        case kSrcPendingSetSpeed:
            LOG(DLOGL_NORMAL, " handle kSrcPendingSetSpeed event");
            inst->queueEvent(kZapperSrcPendingSetSpeed);
            break;

        case kSrcPendingSetPosition:
            LOG(DLOGL_NORMAL, " handle kSrcPendingSetPosition event");
            inst->queueEvent(kZapperSrcPendingSetPosition);
            break;

        default:
            LOG(DLOGL_NORMAL, " Not a valid callback event");
            break;

        }
    }
}





/** *********************************************************
 *
 */
void Mrdvr::mediaCB(void *clientInst, tCpeMediaCallbackTypes type)
{
    FNLOG(DL_MSP_MRDVR);
    if (!clientInst)
    {
        LOG(DLOGL_ERROR, "Error: null clientInst for type: %d", type);
        return;
    }

    Mrdvr *inst = (Mrdvr *)clientInst;
    if (inst)
    {
        switch (type)
        {
        case eCpeMediaCallbackType_FirstFrameAlarm:
            inst->queueEvent(kZapperFirstFrameEvent);
            break;

        default:
            LOG(DLOGL_REALLY_NOISY, "media type %d not handled", type);
            break;
        }
    }
}




/** *********************************************************
 *
 */
void Mrdvr::psiCallback(ePsiCallBackState state, void *data)
{
    Mrdvr *inst = (Mrdvr *)data;
    FNLOG(DL_MSP_MRDVR);

    if (inst)
    {
        switch (state)
        {
        case kPSIReady:
            LOG(DLOGL_MINOR_EVENT, "kPSIReady");
            inst->queueEvent(kZapperPSIReadyEvent);
            break;

        case kPSIUpdate:
            LOG(DLOGL_MINOR_EVENT, "kPSIUpdate");
            inst->queueEvent(kZapperPSIUpdateEvent);
            break;

        case kPSITimeOut:
            LOG(DLOGL_MINOR_EVENT, "kPSITimeOut");
            inst->queueEvent(kZapperTimeOutEvent);
            break;

        case kPSIError:
            LOG(DLOGL_MINOR_EVENT, "Warning: kPSIError - handle as timeout");
            inst->queueEvent(kZapperTimeOutEvent);
            break;

        default:
            LOG(DLOGL_EMERGENCY, "Warning: Unhandled event: %d", state);
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

