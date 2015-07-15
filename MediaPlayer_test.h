/**

\file media_player_test.h -- contains the cxxtest test cases for MSP media_player class

We assume that the C-level SAIL API's are tested in separate functional tests so we are concerned only with testing the actual media_player itself, not the SAIL interface

test cases --
Tests are divided into several functional areas
 - checking state -- calling the API when we know the media_player is in the wrong state
 - parameter checking -- validate parameter values for NULL, etc
 - unimplemented feature checking -- media_player implements only a subset of IMediaController these test the unimplemented portions to make sure they return correct values.
 -- Source testing
  At this time, we do not do any testing based on other MSP modules such as PSI, DisplaySession, etc.
*/

#if !defined(MEDIA_PLAYER_TEST_H)
#define MEDIA_PLAYER_TEST_H

#include <cxxtest/TestSuite.h>

#include "IMediaPlayer.h"
#include "MspCommon.h"

#include "cpe_main.h"
#include "cpe_error.h"
#include "cpe_common.h"
#include "cpe_source.h"
#include "cpe_networking_main.h"
#include "cpe_hnservermgr.h"
#include "cpe_hncontentmgr_upnp.h"

#include "mrdvrserver.h"



class media_playerTestSuite : public CxxTest::TestSuite
{
public:

    void setUp()		// TODO: move this to one-time setup
    {

    }

    void test_media_player(void)
    {
        // Initialize the cpe reference platform

        printf(" Amit cpe_platform_Init \n");
        cpe_platform_Init();
        cpe_networking_Init();  // initialize networking module
        cpe_hnupnp_Init();
        cpe_hndmp_Init();
        cpe_hnsrvmgr_Init();

        TS_TRACE("Testing Media Player Instance");
        IMediaPlayer * instance1 = IMediaPlayer::getMediaPlayerInstance();
        IMediaPlayer * instance2 = IMediaPlayer::getMediaPlayerInstance();

        TS_ASSERT(instance1 != NULL);
        TS_ASSERT(instance1 == instance2);


        TS_TRACE("Initialize MRDVR Server start");
        MRDvrServer::getInstance()->Initialize();
        TS_TRACE("Initialize MRDVR Server Done");

        //WeL9polV
        uint8_t url[128] = "http:/WeL9polV";
        tCpeHnSrvMgrMediaServeRequestInfo obj;
        uint8_t ipaddr[4] = {0xd8, 0x00, 0x01, 0x0a};
        uint8_t macaddr[6] = {0x54, 0xd4, 0x6f, 0x9e, 0xd0, 0xc6};
        memcpy(obj.ipAddr, ipaddr, 4);
        memcpy(obj.macAddr, macaddr, 6);
        obj.sessionID = 101;
        obj.pURL = url;
        //MRDvrServer::getInstance()->HandleServeRequest(&obj);

    }

    /**
     * \brief -- test parameter validation
     */

    void test_media_player_session(void)
    {

        eIMediaPlayerStatus status;

        IMediaPlayerSession *pSession1 = NULL;
        //IMediaPlayerSession *pSession2 = NULL;

        TS_TRACE("Running parameter tests");
        TS_TRACE("Testing IMediaPlayerSession_Create");
        printf(" Amit IMediaPlayerSession_Create start \n");
        status = IMediaPlayerSession_Create(&pSession1, mediaPlayerCallback, NULL);
        TS_ASSERT(status == kMediaPlayerStatus_Ok);
        TS_ASSERT(pSession1 != NULL)

        printf(" Amit IMediaPlayerSession_Load start \n");
        //296222723
        status = IMediaPlayerSession_Load(pSession1, "lscp://AssetId=296222739&BillingId=0&PurchaseTime=0&RemainingTime=100", NULL);
        TS_ASSERT(kMediaPlayerStatus_Ok == status);

        printf(" Amit IMediaPlayerSession_Play start \n");
        status = IMediaPlayerSession_Play(pSession1, "decoder://primary", 0, NULL);
        TS_ASSERT(status == kMediaPlayerStatus_Ok);

        /*    sleep(5);
            status = IMediaPlayerSession_Eject(pSession1);
            TS_ASSERT(status == kMediaPlayerStatus_Ok);
        */
        printf(" Amit *** while( 1) *** \n");

        while (1)
        {
            sleep(1);
        }
        printf(" Amit *** EXIT-Die *** \n");

        /*      status = IMediaPlayerSession_Create(&pSession2, NULL, NULL);
        	  TS_ASSERT(status == kMediaPlayerStatus_Ok);
              TS_ASSERT(pSession2 != NULL)
              status = IMediaPlayerSession_Play(pSession1, "decoder://", 0, NULL);
              TS_ASSERT(status == kMediaPlayerStatus_Error_OutOfState);

              status = IMediaPlayerSession_Load(pSession1, "scte://10", NULL);
              TS_ASSERT(kMediaPlayerStatus_Error_InvalidURL==status);

              status = IMediaPlayerSession_Load(pSession1, "sctetv://10", NULL);
              TS_ASSERT(kMediaPlayerStatus_Ok==status);

              status = IMediaPlayerSession_Play(pSession1, "decoder://primary", 0, NULL);
              TS_ASSERT(status == kMediaPlayerStatus_Ok);

              TS_TRACE("Testing IMediaPlayerSession_Destroy");
              status = IMediaPlayerSession_Destroy(NULL);
        	  TS_ASSERT(status == kMediaPlayerStatus_Error_UnknownSession);

              status = IMediaPlayerSession_Destroy(pSession1);
        	  TS_ASSERT(status == kMediaPlayerStatus_Ok);

              status = IMediaPlayerSession_Destroy(pSession2);
        	  TS_ASSERT(status == kMediaPlayerStatus_Ok);

              status = IMediaPlayerSession_Destroy(pSession2);
        	  TS_ASSERT(status == kMediaPlayerStatus_Error_UnknownSession);

        	  status = zap->Load("sctetv://100",NULL);
        	  TS_ASSERT(status == kMediaPlayerStatus_Ok);

        	  status = zap->Play(NULL,1.0,NULL);
        	  TS_ASSERT(status == kMediaPlayerStatus_Error_InvalidParameter);

        	  status = zap->Play("test",1.0,NULL);  // invalid destination url
        	  TS_ASSERT(status == kMediaPlayerStatus_Error_InvalidParameter);

        	  status = zap->Stop(true,true);
        	  TS_ASSERT(status == kMediaPlayerStatus_Ok);
        	  status = zap->Eject();
        	  TS_ASSERT(status == kMediaPlayerStatus_Ok);

              delete zap;
             */
    }

    static void mediaPlayerCallback(IMediaPlayerSession* pIMediaPlayerSession,
                                    tIMediaPlayerCallbackData callbackData,
                                    void* pClientContext,
                                    MultiMediaEvent *pMme)
    {
        (void)pIMediaPlayerSession;
        (void)callbackData;
        (void)pClientContext;
        (void)pMme;
        printf("MediaPlayer call back called");
    }
    /**
     *
     * \brief should all return error not supported on media_player
     */


};


#endif

