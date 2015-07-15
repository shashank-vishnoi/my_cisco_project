/*
 * MSPResMonClient.h

 *  Derived from ResMonClient.h
 *  Created on: Nov 22, 2010
 *      Author: robpar
 */

#ifndef MSPRESMONCLIENT_H_
#define MSPRESMONCLIENT_H_

#include <pthread.h>
#include <pthread_named.h>
#include <list>

#include "csci-resmon-api.h"
#include "csci-resmon-common-api.h"
#include "dlog.h"

class MSPResMonClient
{
    typedef void (*callbackType)(eResMonCallbackResult, void *);
    struct CallbackInfo
    {
        void *ctx;
        callbackType callback;
    };

private:

    enum localCmdAction {kCmdActionExit, kCmdActionConnect, kCmdActionAcquire, kCmdActionPriority, kCmdActionRelease, kCmdActionCancel};

    struct localCmd
    {
        localCmdAction action;
        int data;
        int timeout;
    };
    pthread_mutex_t resmon_client_mutex;


public:
    MSPResMonClient();
    virtual ~MSPResMonClient();

    // Attempt to connect with the Resource Monitor server, method will block and retry with a 1 second sleep between retries
    eResMonStatus connect(int iRetries = 0);

    // register a static function to be called whenever there is data available on the socket
    // eResMonStatus registerPollCallback()

    // Disconnect from the server
    eResMonStatus disconnect();

    int getFd()
    {
        return m_fd;
    }

    bool isConnected()
    {
        return m_bConnected;
    }

    // Shortcut methods for acquiring specific resources
    eResMonStatus requestTunerAccess(eResMonPriority priority);
    eResMonStatus releaseTunerAccess(void);
    eResMonStatus cancelTunerAccess(void);

    void *dispatchLoop(void);
    void registerCallback(callbackType cb, void *ctx);
    void unregisterCallback(callbackType cb, void *ctx);

    eResMonStatus setPriority(eResMonPriority);

protected:

    eResMonStatus sendLocalCmd(localCmd cmd);

    bool m_bConnected;
    int  m_fd;
    int  m_localReadFd;
    int  m_localWriteFd;
    pthread_t dispatchThread;
    eResMonPriority currentPriority;
    uint32_t     m_uuid;
    bool granted;

    // Request resource allocation from the server, by default, method will block and poll the server for a response
    eResMonStatus getResponse(eResMonCallbackResult *result, eResMonCommandType *type, tAllocation *allocationData);
    eResMonStatus releaseResource();
    bool dispatchLocalCmd(localCmd cmd);
    void doCallback(eResMonCallbackResult);
    std::list<CallbackInfo> callbackList;
    void recoverResmonCrash();

};

#endif /* MSPRESMONCLIENT_H_ */
