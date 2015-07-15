#include "mrdvrserver.h"

void MRDvrServer::MRDvrServer_Finalize()
{
    FNLOG(DL_MSP_MRDVR);
    isMrDvrAuthorized = false;

    if (isInitialized)
    {
        if (suCallbackId)
        {
            cpe_hnsrvmgr_UnregisterCallback(suCallbackId);
            suCallbackId = 0;
        }
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "cpe_hnsrvmgr_UnregisterCallback(suCallbackId)");

        if (tdCallbackId)
        {
            cpe_hnsrvmgr_UnregisterCallback(tdCallbackId);
            tdCallbackId = 0;
        }
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "cpe_hnsrvmgr_UnregisterCallback(tdCallbackId)");

        // Stop thread
        threadEventQueue->dispatchEvent(kMrdvrExitThreadEvent, NULL);
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "dispatched Event waiting for join");
        pthread_join(eventHandlerThread, NULL);       // wait for event thread to exit
        dlog(DL_MSP_MRDVR, DLOGL_NOISE, "return from join now exit");
        eventHandlerThread = 0;

        // Delete MRDvr Server Object
        if (instance)
        {
            dlog(DL_MSP_MRDVR, DLOGL_NOISE, "delete instance");
            delete instance;
            instance = NULL;
        }
    }
    dlog(DL_MSP_MRDVR, DLOGL_NOISE, "exit finalize");
}

