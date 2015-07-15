#include "mrdvrserver.h"

#define UNUSED_PARAM(a) (void)a;

void MRDvrServer::MRDvrServer_Finalize()
{
    FNLOG(DL_MSP_MRDVR);
    isMrDvrAuthorized = false;
}

