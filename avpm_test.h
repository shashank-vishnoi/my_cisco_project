#include <cxxtest/TestSuite.h>

#include "avpm.h"
#include "IMediaPlayer.h"
#include "MspCommon.h"

class avpmTestSuite : public CxxTest::TestSuite
{
public:

    void test_singleton(void)
    {
        TS_TRACE("avpm test");

        //Avpm *avpm1;
        // Avpm *avpm2;


        /*avpm_inst1 = avpm1->GetInstance();
        TS_ASSERT(avpm1 != NULL);

        avpm_inst2 = avpm2->GetInstance();
        TS_ASSERT(avpm_inst1 == avpm_inst2);*/


    }



    /**
     * \brief -- test parameter validation
     */

    void test_params(void)
    {

        eMspStatus status;
        Avpm *avpm;

        TS_TRACE("Running parameter tests");

        tCpePgrmHandle pgrHandle = (void*) 1111;
        DFBRectangle rect;

        TS_ASSERT(avpm != NULL);

        rect.x = 100;
        rect.y = 100;
        rect.w = 100;
        rect.h = 100;
        status = avpm->setPresentationParams(pgrHandle, rect, false);
        TS_ASSERT(status == kMspStatus_Ok);

        status = avpm->setPresentationParams(pgrHandle, rect, true);
        TS_ASSERT(status == kMspStatus_Ok);

        status = avpm->setAudioParams(pgrHandle);
        TS_ASSERT(status == kMspStatus_Ok);

        status = avpm->setAudioParams(pgrHandle);
        TS_ASSERT(status == kMspStatus_Ok);

        status = avpm->connectOutput(pgrHandle);
        TS_ASSERT(status == kMspStatus_Ok);

        status = avpm->stopOutput(pgrHandle);
        TS_ASSERT(status == kMspStatus_Ok);

        status = avpm->disconnectOutput(pgrHandle);
        TS_ASSERT(status == kMspStatus_Ok);


    }


};

