/** @file PsiUtilInterface.h
 *
 * author Gnanesha Thimmegowda
 *
 * date 31Oct2012
 *
 * version 1.0
 *
 * @brief The PsiUtilInterface.h header file.
 *
 * PsiUtilInterface.h is a abstract base class definition for a PsiUtils derived Class.
 \n Copyright 2012 Cisco Systems, Inc.
 */

#if !defined(PSIUTILSINTERFACE_H)
#define PSIUTILSINTERFACE_H



// standard linux includes
#include <stdint.h>

// MSP includes
#include "MspCommon.h"
#include "pmt.h"
#include "psiUtils.h"
// cpe includes
#include <cpe_sectionfilter.h>



typedef enum
{
    kPSFD_Success,
    kPSFD_Failure,
    kPSFD_MemError
} ePSFDState;

class psiUtilsInterface
{
    static psiUtilsInterface *m_pInstance;
public:
    virtual ePSFDState parseSectionFilterGroupData(uint32_t version, tCpeSFltFilterGroup  *SfGroup) = 0;
    virtual tPat* getPatData(uint8_t* pSecFilterBuf) = 0;
    static psiUtilsInterface *getpsiUtilsIntInstance();
    virtual ~psiUtilsInterface() {};
};


#endif //End of #if !defined(PSIUTILSINTERFACE_H)
