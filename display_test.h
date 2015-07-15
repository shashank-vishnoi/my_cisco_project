/**

\file display_test.h -- contains the cxxtest test cases for MSP display class

We assume that the C-level SAIL API's are tested in separate functional tests so we are concerned only with testing the actual display itself, not the SAIL interface

test cases --
*/

#if !defined(ZAPPER_TEST_H)
#define ZAPPER_TEST_H

#include <cxxtest/TestSuite.h>

#include "DisplaySession.h"

class displayTestSuite : public CxxTest::TestSuite
{
public:

    void setUp()
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

        DisplaySession *display;

        display = new DisplaySession;
        TS_ASSERT(display != NULL);

        delete display;
    }

    void test_states(void)
    {
        eMspStatus status;

        TS_TRACE("Running state test");

        DisplaySession *display;

        display = new DisplaySession;
        TS_ASSERT(display != NULL);

        status = display->open(0, 0);
        TS_ASSERT(status != kMspStatus_Ok);

        tCpeSrcHandle handle = (void*) 1111;
        status = display->open(handle, 0);
        TS_ASSERT(status != kMspStatus_Ok);

        DFBRectangle rect;

        status = display->setVideoWindow(rect, true);
        TS_ASSERT(status != kMspStatus_Ok);

        status =  display->updatePids(NULL);
        TS_ASSERT(status != kMspStatus_Ok);

        Psi* psi = new Psi();
        status =  display->updatePids(psi);
        TS_ASSERT(status != kMspStatus_Ok);
        delete psi;

        status = display->start();
        TS_ASSERT(status != kMspStatus_Ok);

        status = display->stop();
        TS_ASSERT(status == kMspStatus_Ok);

        status = display->close();
        TS_ASSERT(status == kMspStatus_Ok);

        delete display;
    }

};


#endif
