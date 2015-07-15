
#ifndef MSP_SOURCE_FACTORY_H
#define MSP_SOURCE_FACTORY_H

#include "MSPSource.h"

typedef enum
{
    kMSPRFSource,
    kMSPPPVSource,
    kMSPFileSource,
    kMSPMHTTPSource,
    kMSPMrdvrStreamerSource,
    kMSPHnOnDemandStreamerSource,
    kMSPInvalidSource
} eMSPSourceType;


class MSPSourceFactory
{
public:
    static MSPSource* getMSPSourceInstance(eMSPSourceType srcType, const char *aSrcUrl, IMediaPlayerSession *pImediaPlayerSession);
    static eMSPSourceType getMSPSourceType(const char* aSrcUrl);
};

#endif // #ifndef MSP_SOURCE_FACTORY_H
