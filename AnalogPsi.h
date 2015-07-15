/**
   \file psi.h
   \class psi
*/

#if !defined(ANALOGPSI_H)
#define ANALOGPSI_H



// standard linux includes
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string>
#include<list>

// SAIL includes
#include "sail-mediaplayersession-api.h"
// MSP includes
#include "MspCommon.h"
#include "MSPSource.h"


// cpe includes
#include <cpe_source.h>
#include <cpe_programhandle.h>
#include <cpe_sectionfilter.h>
#include <cpe_mediamgr.h>
#include <cpe_recmgr.h>

class AnalogPsi
{
private:

    /**< To send the state of PSI to the media controller */
    uint16_t 				mClockPid;
    uint16_t				mVideoPid;
    uint16_t				mAudioPid;
    uint16_t				mAmolPid;
    uint16_t                mPgmNo;          /**< program no to look for PMT PID in section filter data */

    tCpeSrcHandle    mSrcHandle;      /**< Source Handle to Open section Filter */

    // tCpeMediaPidTable* sParams;
    unsigned char* mMetaBuf;
    uint32_t mMetaSize;
    tCpeSrcPFPidDef *mPids;

    enum ePlaySource
    {
        kPlayNoSrc,
        kPlayLiveSrc,
        kPlayFileSrc


    } pSource;


public:
    AnalogPsi();
    ~AnalogPsi();
    eMspStatus psiStart(std::string recordUrl);
    eMspStatus psiStart(const MSPSource *aSource);
    eMspStatus psiStop();
    uint16_t getProgramNo(void);
    tCpeSrcPFPidDef * getPids(void)
    {
        return mPids;
    }
    eMspStatus getComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset);
};

#endif
