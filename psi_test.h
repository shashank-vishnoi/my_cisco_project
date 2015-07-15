/**

\file psi_test.h -- contains the cxxtest test cases for MSP psi class

We assume that the C-level SAIL API's are tested in separate functional tests so we are concerned only with testing the actual PSI itself, not the SAIL interface

test cases --
Tests are divided into several functional areas
 - checking state -- calling the API when we know the PSI is in the wrong state
 - parameter checking -- validate parameter values for NULL, etc
 -- Source testing
  At this time, we do not do any testing based on other MSP modules such as Zapper, DisplaySession, etc.
*/

#if !defined(PSI_TEST_H)
#define PSI_TEST_H

#include <cxxtest/TestSuite.h>

#include "psi.h"

#define UNUSED_PARAM(a) (void)a;

class psiTestSuite : public CxxTest::TestSuite
{
public:

    void setUp()		// TODO: move this to one-time setup
    {
        eMspStatus status;
        static bool initDone = false;

        if (!initDone)
        {
            status = MspCommon::getInstance()->initSoft();
            TS_ASSERT(status == kMspStatus_Ok);

            initDone = true;
        }

    }

    void test_new_delete(void)
    {

        TS_TRACE("Running new/delete test");

        Psi *psi;

        psi = new Psi;
        TS_ASSERT(psi != NULL);

        delete psi;
    }


    /**
      *
      * \brief should all return what is the current state of PSI
      */

    void test_states(void)
    {


        Psi *psi;
        Pmt *mPmt;
        eMspStatus status;
        uint16_t pgmNo;
        tCpeSrcHandle srcHandle;
        tCpePgrmHandle mPgrmHandleSf;

        UNUSED_PARAM(mPmt)
        UNUSED_PARAM(mPgrmHandleSf)

        psi = new Psi;
        TS_ASSERT(psi != NULL);

        srcHandle = (void*) 1111;
        pgmNo = 16;

        TS_TRACE("Running state test");

        status = psi->psiStart(pgmNo, srcHandle);
        TS_ASSERT(status != kMspStatus_Ok);

        status = psi->psiStop();
        TS_ASSERT(status != kMspStatus_Ok);

        /* status = psi->registerPsiCallback();
         TS_ASSERT(status != kMspStatus_Ok);

         status = psi->unRegisterPsiCallback();
         TS_ASSERT(status != kMspStatus_Ok); */

        mPmt = psi->getPmtObj();
        TS_ASSERT(mPmt == NULL);

        /*  status = psi->setSectionFilter();
          TS_ASSERT(status != kMspStatus_Ok);

          status =  psi->start();
          TS_ASSERT(status != kMspStatus_Ok);

          status = psi->stop();
          TS_ASSERT(status != kMspStatus_Ok);

          status = psi->close();
          TS_ASSERT(status == kMspStatus_Ok); */

        delete psi;


    }

};


#endif
