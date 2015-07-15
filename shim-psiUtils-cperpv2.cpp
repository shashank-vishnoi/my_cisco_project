/**
   \file G8-psiUtils.cpp

Implementation file for PAT/PMT parsing Logic for PSI for LSB
*/


///////////////////////////////////////////////////////////////////////////
//                          Includes
///////////////////////////////////////////////////////////////////////////
#include <dlog.h>
#include "psiUtils.h"
#define LOG(level, msg, args...)  dlog(DL_MSP_PSI, level,"Psi:%s:%d " msg, __FUNCTION__, __LINE__, ##args);


psiUtils *psiUtils::m_pInstance = NULL;
///////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////

psiUtils* psiUtils::getpsiUtilsInstance()
{
    if (m_pInstance == NULL)
    {
        m_pInstance = new psiUtils();

    }
    return m_pInstance;
}


tPat* psiUtils::getPatData(uint8_t* pSecFilterBuf)
{
    if (NULL != pSecFilterBuf)
    {
        return (tPat*)(pSecFilterBuf + sizeof(tTableHeader));
    }
    else
    {
        LOG(DLOGL_ERROR, "%s Error: NULL pSecFilterBuf data", __FILE__);
        return NULL;
    }
}


ePSFDState psiUtils::parseSectionFilterGroupData(uint32_t version, tCpeSFltFilterGroup  *SfGroup)
{
    ePSFDState State = kPSFD_MemError;

    if (NULL != SfGroup)
    {
        SfGroup->reject = 0;
        SfGroup->numFilts = 1;
        SfGroup->filter[0].value      = (uint64_t)version << 17;
        SfGroup->filter[0].mask       = (uint64_t)0x1f << 17;
        SfGroup->filter[0].offset     = 0;
        SfGroup->filter[0].comparitor = eCpeSFltComp_CompNE;
        State = kPSFD_Success;
    }

    return State;
}
