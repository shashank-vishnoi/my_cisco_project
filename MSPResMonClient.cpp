/*
 * MSPResMonClient.cpp
 *
 *  Derived from:  ResMonClient.cpp
 *  Created on: Nov 24, 2010
 *      Author: robpar
 */

#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <assert.h>
#include "diaglib.h"

#include "MSPResMonClient.h"


#ifdef LOG
#error  LOG already defined
#endif
#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"MSPResMonClient(%p):%s:%d " msg, this, __FUNCTION__, __LINE__, ##args);
#define Close(x) do{dlog(DL_MSP_MPLAYER, DLOGL_MINOR_DEBUG,"%s:%s:%d ZZClose=%d",__FILE__,__FUNCTION__,__LINE__, x); close(x); (x)=-1;}while(0)


static void *threadEntry(void *ctx)
{
    FNLOG(DL_MSP_MPLAYER);

    MSPResMonClient *inst = (MSPResMonClient *)ctx;

    //Making thread cancel enable, so that we can cancel it, if required
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    //Setting canceltype to Asynchonous, so that thread can be canceled immediately.
    //Since dispatch Tread(this thread) is not hoding ant lock or memory
    //It is quite safe to use it.
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    inst->dispatchLoop();

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Exiting Resmon Client Thread");
    pthread_exit(NULL);
}

void MSPResMonClient::doCallback(eResMonCallbackResult reason)
{
    std::list<CallbackInfo>::iterator iter;
    callbackType cb;

    pthread_mutex_lock(&resmon_client_mutex);

    if (callbackList.empty())
    {
        pthread_mutex_unlock(&resmon_client_mutex);
        return;
    }

    for (iter = callbackList.begin(); iter != callbackList.end(); iter++)
    {
        cb = iter->callback;
        LOG(DLOGL_REALLY_NOISY, "do callback cb: %p ctx: %p", cb, iter->ctx);
        if (cb != NULL)
        {
            cb(reason, iter->ctx);
        }
    }

    pthread_mutex_unlock(&resmon_client_mutex);
}


void MSPResMonClient::registerCallback(callbackType cb, void *ctx)
{
    CallbackInfo temp;

    temp.callback = cb;
    temp.ctx = ctx;
    LOG(DLOGL_REALLY_NOISY, "cb: %p ctx: %p", cb, ctx);

    pthread_mutex_lock(&resmon_client_mutex);
    callbackList.push_back(temp);
    pthread_mutex_unlock(&resmon_client_mutex);
}


void MSPResMonClient::unregisterCallback(callbackType cb, void *ctx)
{
    std::list<CallbackInfo>::iterator iter;

    pthread_mutex_lock(&resmon_client_mutex);
    iter = callbackList.begin();

    while (iter != callbackList.end())
    {
        if ((iter->callback == cb) && (iter->ctx == ctx))
        {
            iter = callbackList.erase(iter);
        }
        else
        {
            iter++;
        }
    }
    pthread_mutex_unlock(&resmon_client_mutex);
}

void MSPResMonClient::recoverResmonCrash()
{
    eResMonStatus status = kResMon_Failure;
    int pipeFds[2];

    releaseTunerAccess(); //just to free allocated resources such as uuid and etc
    cancelTunerAccess();  //just to free allocated resources such as uuid and etc
    disconnect(); //Close the current fd which is opened for old resmon


    Close(m_localReadFd); //Close local read fd
    m_localReadFd = -1;
    Close(m_localWriteFd); //close local right fd
    m_localWriteFd = -1;

    while (status != kResMon_Ok)
    {
        LOG(DLOGL_EMERGENCY, "%s:%d Re-connect", __FUNCTION__, __LINE__);
        status = connect(5);
    }

    LOG(DLOGL_EMERGENCY, "%s:%d Hurry.. Reconnect Successful", __FUNCTION__, __LINE__);
    int state = pipe(pipeFds); //Requried because resomn can't work with old fds
    if (state != 0)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_EMERGENCY, "resmon client could not create internal pipe:%d", status);
    }

    m_localReadFd = pipeFds[0];
    m_localWriteFd = pipeFds[1];
    requestTunerAccess(currentPriority); //Request the tuner with currentpriority which is already set my old session



}

bool MSPResMonClient::dispatchLocalCmd(localCmd cmd)
{

    bool ret_value;

    if (cmd.action == kCmdActionExit)
    {
        return true;
    }
    else if (cmd.action == kCmdActionRelease)
    {
        // Currently there is no way to release a resource solely based on its uuid,
        // so we provide some defaults here for the other args
        if (m_uuid != 0)
        {
            if (m_fd == -1)
            {
                LOG(DLOGL_ERROR, "warning: calling ResMon_sendCommand with m_fd == -1");
            }
            else
            {
                ret_value = ResMon_sendCommand(m_fd, kResMon_Release, kResMon_Tuner, &m_uuid, currentPriority, 1, (void *)this, NULL);
                LOG(DLOGL_NOISE, "ResMon_sendCommand release tuner uuid %x(%d)", m_uuid, m_uuid);
                granted = false;
                m_uuid = 0;
            }
        }
        return false;
    }
    else if (cmd.action == kCmdActionCancel)
    {
        if (m_uuid != 0)
        {
            ret_value = ResMon_sendCommand(m_fd, kResMon_Cancel, kResMon_Tuner, &m_uuid, currentPriority, 1, (void *)this, NULL);
            LOG(DLOGL_NOISE, "ResMon_sendCommand cancel tuner  uuid %x(%d)", m_uuid, m_uuid);
            m_uuid = 0;
            granted = false;
        }
        return false;
    }
    else if (cmd.action == kCmdActionPriority)
    {
        currentPriority = (eResMonPriority)cmd.data;

        ret_value = ResMon_sendCommand(m_fd, kResMon_UpdatePriority, kResMon_Tuner, &m_uuid, currentPriority, 1, (void *)this, NULL);
        LOG(DLOGL_NOISE, "ResMon_sendCommand kResMon_UpdatePriority uuid %x(%d)", m_uuid, m_uuid);
        return ret_value;
    }
    else if (cmd.action == kCmdActionAcquire)
    {
        currentPriority = (eResMonPriority)cmd.data;
        ret_value = ResMon_sendCommand(m_fd, kResMon_Request, kResMon_Tuner, &m_uuid, currentPriority, 1, (void *)this, "MSP");
        LOG(DLOGL_NOISE, "ResMon_sendCommand acquire tuner uuid %x(%d)", m_uuid, m_uuid);
        return ret_value;
    }

    return false;
}



void *MSPResMonClient::dispatchLoop()
{
    bool exitThisThread = false;
    int status;
    struct pollfd pollfds[2];
    localCmd buf;
    eResMonCallbackResult result;
    eResMonStatus rmStatus;
    eResMonCommandType type;
    tAllocation allocationData;
    int timeout;

    FNLOG(DL_MSP_MPLAYER);

    if (m_localReadFd == -1)
    {
        LOG(DLOGL_ERROR, "Error m_localReadFd == -1");
        return NULL;             // local socket not set up -- this is an error
    }


    timeout = -1;                // default is no timeout
    while (!exitThisThread)
    {
        pollfds[0].fd = m_localReadFd;
        pollfds[0].events = POLLIN;
        pollfds[0].revents = 0;

        pollfds[1].fd = m_fd;
        pollfds[1].events = POLLIN;
        pollfds[1].revents = 0;
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "resmon client poll fd:%d fd:%d", m_localReadFd, m_fd);

        status = poll(pollfds, 2, timeout);

        if (status == 0)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: WARNING: poll returned timeout with timeout being configured value as %d", __FUNCTION__, timeout);
            timeout = -1;  // reset timeout after this one
        }
        else if (status < 0)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: poll returned error: %d", __FUNCTION__, errno);
        }
        else
        {
            if (pollfds[0].revents & POLLIN)
            {
                status = read(m_localReadFd, &buf, sizeof(buf));
                if (status != sizeof(buf))
                {
                    dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "ERROR: reading from resmon client socket: wanted %d, read %d err:%d", sizeof(buf), status, errno);
                }
                dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s: read %d from pipe", __FUNCTION__, buf.action);
                exitThisThread = dispatchLocalCmd(buf);
                timeout = buf.timeout;
            }

            // When resmon goes down, ideally poll shoudl return erro, but observed that it still returns success. But the revent is set 17 (0x11) which
            // basically is POLLIN + POLLHUP. So following logic checks if this error occured.
            // Currently there is no clean mechanism to set the states right even after reconnecting. So forcing a reboot on this event.

            if (pollfds[1].revents & POLLHUP)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_EMERGENCY, "%s:%d: EMERGENCY: Resmon Went down. MSP trying to recover. Please Wait......", __FUNCTION__, __LINE__);
                recoverResmonCrash();
                //Once recovered assign all the new fds here only
                pollfds[0].fd = m_localReadFd;
                pollfds[0].events = POLLIN;
                pollfds[0].revents = 0;

                pollfds[1].fd = m_fd;
                pollfds[1].events = POLLIN;
                pollfds[1].revents = 0;
            }

            if (pollfds[1].revents & POLLIN)
            {
                rmStatus = getResponse(&result, &type, &allocationData);
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s: read response from resmon status %d result %d type %d grant %d", __FUNCTION__, rmStatus, result, type, allocationData.AllocationGranted);
                if (allocationData.AllocationGranted)
                {
                    granted = true;
                }
                if (rmStatus == kResMon_Ok)
                {
                    doCallback(result);
                }
                timeout = -1;
            }
        }
    }

    return NULL;
}


//
// send a command via the local channel to the dispatch thread
//
eResMonStatus MSPResMonClient::sendLocalCmd(localCmd cmd)
{
    int numBytesWritten = write(m_localWriteFd, &cmd, sizeof(cmd));

    if (numBytesWritten != sizeof(cmd))
    {
        LOG(DLOGL_ERROR, "ERROR: write resmon client thread: wanted %d, wrote %d err:%d", sizeof(cmd), numBytesWritten, errno);
        return kResMon_Failure;
    }

    return kResMon_Ok;
}


MSPResMonClient::MSPResMonClient()
{
    int thread_retvalue;
    pthread_attr_t attr;
    int status;
    int pipeFds[2];

    FNLOG(DL_MSP_MPLAYER);

    m_bConnected = false;
    m_fd = -1;
    m_localReadFd = -1;
    m_localWriteFd = -1;
    m_uuid = 0;
    dispatchThread = -1;
    currentPriority = kRMPriority_VideoWithAudioFocus;      // default for MSP
    granted = false;
    pthread_mutex_init(&resmon_client_mutex, NULL);

    callbackList.clear();

    // setup local fd for communication with dispatch thread

    status = pipe(pipeFds);
    if (status != 0)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "resmon client could not create internal pipe:%d", status);
        return;
    }

    m_localReadFd = pipeFds[0];
    m_localWriteFd = pipeFds[1];

    dlog(DL_MSP_MPLAYER, DLOGL_MINOR_DEBUG, "%s:%s:%d call pipe: m_localReadFd ZZOpen=%d", __FILE__, __FUNCTION__, __LINE__, m_localReadFd);
    dlog(DL_MSP_MPLAYER, DLOGL_MINOR_DEBUG, "%s:%s:%d call pipe: m_localWriteFd ZZOpen=%d", __FILE__, __FUNCTION__, __LINE__, m_localWriteFd);

    connect(5);  // connect to resmon with retries -- need to do this before we create the thread

    // create and start our thread
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024); // this should be way more than we need

    thread_retvalue = pthread_create(&dispatchThread, &attr, threadEntry, (void *)this);

    if (thread_retvalue)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "ERROR: In creating resmon client thread: %d", thread_retvalue);
        return;
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Launch resmon clientThread\n");
    }

    thread_retvalue = pthread_setname_np(dispatchThread, "RF tuner resmon client");
    if (0 != thread_retvalue)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "resmon client thread naming failed:%d", thread_retvalue);
    }

}

MSPResMonClient::~MSPResMonClient()
{
    FNLOG(DL_MSP_MPLAYER);
    localCmd cmd;


    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s: Send exit to resmon client", __FUNCTION__);

    cmd.action = kCmdActionExit;
    cmd.data = 0;
    cmd.timeout = -1;
    eResMonStatus status = sendLocalCmd(cmd);


    if (dispatchThread != (pthread_t) - 1)
    {
        if (status == kResMon_Ok)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s: sent kCmdActionExit command successfully. (Not an error)", __FUNCTION__);
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s: wait for resmon client to exit. (Not an error)", __FUNCTION__);
            pthread_join(dispatchThread, NULL);
        }
        else   //sendLocalCmd Failed
        {
            //This can happen only when local Read/Write FDs are dead(-1), OR Resmon client is stuck somewhere
            //It is of no use waiting here, specially in case of channel change.
            //If Resmon is DOWN and RECOVERY is taking time, and we are changing the channel,
            //It can lead to a dead lock.
            //So let this thread to cancel and move ahead.
            //Once flow is moving from here, everything will be cleaned by below other APIs.
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: Canceling resmon client,as sending command to resmon thread failed", __FUNCTION__);
            pthread_cancel(dispatchThread);
        }
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: resmon client not created.so.skipping waiting for that thread to exit", __FUNCTION__);
    }

    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s: Calling disconnect m_fd=%d", __FUNCTION__, m_fd);

    // This disconnect must be after the thread exit
    disconnect();



    Close(m_localReadFd);
    m_localReadFd = -1;
    Close(m_localWriteFd);
    m_localWriteFd = -1;

    pthread_mutex_destroy(&resmon_client_mutex);

}

eResMonStatus MSPResMonClient::connect(int iRetries)
{
    FNLOG(DL_MSP_MPLAYER);

    // Make sure we're not already connected to the ResMon server
    if (m_bConnected)
    {
        LOG(DLOGL_ERROR, "Error: Already connected to ResMon server, must disconnect before reconnecting");
        return kResMon_Failure;
    }

    eResMonStatus status = ResMon_init(&m_fd);
    dlog(DL_MSP_MPLAYER, DLOGL_MINOR_DEBUG, "%s:%s:%d ZZ  call ResMon_init return fd %d", __FILE__, __FUNCTION__, __LINE__, m_fd);
    for (int i = 0; i < iRetries && status != kResMon_Ok; i++)
    {
        LOG(DLOGL_ERROR, "Could not connect to ResMon server, retrying...");

        sleep(1);
        status = ResMon_init(&m_fd);
    }

    if (status == kResMon_Ok)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_MINOR_DEBUG, "%s:%s:%d ZZOpen call ResMon_init return fd %d", __FILE__, __FUNCTION__, __LINE__, m_fd);

        m_bConnected = true;
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "resmon client did not connect to resmon status:%d", status);
    }

    return status;
}

eResMonStatus MSPResMonClient::disconnect()
{
    FNLOG(DL_MSP_MPLAYER);

    // Make sure we're connected to the ResMon server
    if (!m_bConnected)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Not connected to ResMon server");
        return kResMon_Failure;
    }


    dlog(DL_MSP_MPLAYER, DLOGL_MINOR_DEBUG, "%s:%s:%d ZZClose call ResMon_finalize Pre fd %d", __FILE__, __FUNCTION__, __LINE__, m_fd);

    eResMonStatus status = ResMon_finalize(m_fd);
    if (status == kResMon_Ok)
    {
        m_bConnected = false;
        m_fd = -1;
    }

    dlog(DL_MSP_MPLAYER, DLOGL_MINOR_DEBUG, "%s:%s:%d ZZClose call ResMon_finalize Post fd %d, expect fd = -1", __FILE__, __FUNCTION__, __LINE__, m_fd);

    return status;
}


eResMonStatus MSPResMonClient::getResponse(eResMonCallbackResult *result, eResMonCommandType *type, tAllocation *allocationData)
{
    FNLOG(DL_MSP_MPLAYER);

    // Make sure we're connected to the ResMon server
    if (!m_bConnected)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Not connected to ResMon server");
        return kResMon_Failure;
    }

    return ResMon_getResourceEvent(m_fd, result, type, allocationData);
}

eResMonStatus MSPResMonClient::releaseResource(void)
{
    FNLOG(DL_MSP_MPLAYER);

    localCmd cmd;

    cmd.action = kCmdActionRelease;
    cmd.data = 0;
    cmd.timeout = -1;

    return sendLocalCmd(cmd);
}

eResMonStatus MSPResMonClient::requestTunerAccess(eResMonPriority priority)
{
    FNLOG(DL_MSP_MPLAYER);

    LOG(DLOGL_NOISE, "priority: %d  uuid %x(%d)p", priority, m_uuid, m_uuid);

    localCmd cmd;
    cmd.action = kCmdActionAcquire;
    cmd.data = priority;
    cmd.timeout = 5000;     // 5 second timeout (fixed) for now

    eResMonStatus errStatus = sendLocalCmd(cmd);

    if (errStatus)
    {
        LOG(DLOGL_ERROR, "Error requesting tuner");
    }

    return errStatus;
}


eResMonStatus MSPResMonClient::releaseTunerAccess(void)
{
    FNLOG(DL_MSP_MPLAYER);

    localCmd cmd;

    cmd.action = kCmdActionRelease;
    cmd.data = 0;
    cmd.timeout = -1;

    eResMonStatus errStatus  = sendLocalCmd(cmd);

    if (errStatus)
    {
        LOG(DLOGL_ERROR, "Error releasing tuner");
    }

    return errStatus;
}

eResMonStatus MSPResMonClient::cancelTunerAccess(void)
{
    FNLOG(DL_MSP_MPLAYER);

    localCmd cmd;

    cmd.action = kCmdActionCancel;
    cmd.data = 0;
    cmd.timeout = -1;

    eResMonStatus errStatus  = sendLocalCmd(cmd);

    if (errStatus)
    {
        LOG(DLOGL_ERROR, "Error cancelling tuner");
    }

    return errStatus;
}

eResMonStatus MSPResMonClient::setPriority(eResMonPriority pri)
{
    localCmd cmd;
    FNLOG(DL_MSP_MPLAYER);

    if (!m_uuid)
    {
        LOG(DLOGL_ERROR, "Error: m_uuid not set (tuner not allocated)");
        return kResMon_Failure;
    }

    cmd.action = kCmdActionPriority;
    cmd.data = pri;
    cmd.timeout = -1;

    return sendLocalCmd(cmd);
}


