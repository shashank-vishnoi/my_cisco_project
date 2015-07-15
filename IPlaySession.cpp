#include "IPlaySession.h"

// Implementing required stub functions for IPlaySession.
IPlaySession::IPlaySession(): mData(0)
{
    //mCb = 0;
}

int IPlaySession::unRegisterCCIupdate(int regId)
{
    regId = regId;
    return 0;
}

