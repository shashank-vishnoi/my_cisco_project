/**

\file zapper_test.h -- contains the cxxtest test cases for MSP zapper class

We assume that the C-level SAIL API's are tested in separate functional tests so we are concerned only with testing the actual zapper itself, not the SAIL interface

test cases --
Tests are divided into several functional areas
 - checking state -- calling the API when we know the zapper is in the wrong state
 - parameter checking -- validate parameter values for NULL, etc
 - unimplemented feature checking -- zapper implements only a subset of IMediaController these test the unimplemented portions to make sure they return correct values.
 -- Source testing
  At this time, we do not do any testing based on other MSP modules such as PSI, DisplaySession, etc.
*/

#if !defined(ZAPPER_TEST_H)
#define ZAPPER_TEST_H

#include <cxxtest/TestSuite.h>

#include "zapper.h"

class zapperTestSuite : public CxxTest::TestSuite
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

        Zapper *zap;

        zap = new Zapper;
        TS_ASSERT(zap != NULL);

        delete zap;
    }

    /**
     * \brief -- test parameter validation
     */

    void test_params(void)
    {

        eIMediaPlayerStatus status;
        Zapper *zap;

        TS_TRACE("Running parameter tests");

        zap = new Zapper;
        TS_ASSERT(zap != NULL);

        status = zap->Load(NULL, NULL);
        TS_ASSERT(status == kMediaPlayerStatus_Error_InvalidParameter);

// do a real load now so we can test play params
        status = zap->Load("sctetv://100", NULL);
        TS_ASSERT(status == kMediaPlayerStatus_Ok);

        status = zap->Play(NULL, 1.0, NULL);
        TS_ASSERT(status == kMediaPlayerStatus_Error_InvalidParameter);

        status = zap->Play("test", 1.0, NULL); // invalid destination url
        TS_ASSERT(status == kMediaPlayerStatus_Error_InvalidParameter);

        status = zap->Play("decoder://primary", 1.0, NULL); // valid destination url
        TS_ASSERT(status == kMediaPlayerStatus_Ok);

        status = zap->Stop(true, true);
        TS_ASSERT(status == kMediaPlayerStatus_Ok);

        status = zap->Eject();
        TS_ASSERT(status == kMediaPlayerStatus_Ok);

        delete zap;
    }

    /**
     *
     * \brief should all return error not supported on zapper
     */
    void test_not_supported(void)
    {
        eIMediaPlayerStatus status;
        Zapper *zap;
        float pos;

        TS_TRACE("Running not supported tests");

        zap = new Zapper;
        TS_ASSERT(zap != NULL);

        status = zap->PersistentRecord("test", 0.0, 1.0, NULL);
        TS_ASSERT(status == kMediaPlayerStatus_Error_NotSupported);

        status = zap->SetSpeed(1, 1);
        TS_ASSERT(status == kMediaPlayerStatus_Error_NotSupported);

        status = zap->SetPosition(0.0f);
        TS_ASSERT(status == kMediaPlayerStatus_Error_NotSupported);

        status = zap->GetPosition(&pos);
        TS_ASSERT(status == kMediaPlayerStatus_Error_NotSupported);

        status = zap->GetStartPosition(&pos);
        TS_ASSERT(status == kMediaPlayerStatus_Error_NotSupported);

        status = zap->GetEndPosition(&pos);
        TS_ASSERT(status == kMediaPlayerStatus_Error_NotSupported);

        delete zap;

    }

    void test_states(void)
    {
        TS_TRACE("Running state tests");

        Zapper *zap;
        eIMediaPlayerStatus status;
        int num;
        unsigned int denom;

        zap = new Zapper;
        TS_ASSERT(zap != NULL);

        //  state is idle here, should only take Load or SetCallback
        status = zap->SetCallback(NULL, NULL, NULL);
        TS_ASSERT(status == kMediaPlayerStatus_Ok);

        status = zap->Eject();  // Eject is legal any time
        TS_ASSERT(status == kMediaPlayerStatus_Ok);

        status = zap->Stop(false, false);		// Stop no longer enforces state requirements since it is asynchronous
        TS_ASSERT(status == kMediaPlayerStatus_Ok);

        // these are invalid

        status = zap->Play("test", 1.0, NULL);
        TS_ASSERT(status == kMediaPlayerStatus_Error_OutOfState);


        status = zap->GetSpeed(&num, &denom);
        TS_ASSERT(status == kMediaPlayerStatus_Error_OutOfState);

        // now do a load then test states again
        status = zap->Load("sctetv://100", NULL);
        TS_ASSERT(status == kMediaPlayerStatus_Ok);

        status = zap->GetSpeed(&num, &denom);
        TS_ASSERT(status == kMediaPlayerStatus_Error_OutOfState);

        status = zap->Play("decoder://primary", 0.0f, NULL);
        TS_ASSERT(status == kMediaPlayerStatus_Ok);
        sleep(30);  // time for tuner/psi callback test

        status = zap->Eject();
        TS_ASSERT(status == kMediaPlayerStatus_Ok);

        delete zap;
    }

};


#endif
