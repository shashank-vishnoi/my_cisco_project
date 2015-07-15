/**

\file LANGUAGE_SELECTION_test.h -- contains the cxxtest test cases for MSP LANGUAGE_SELECTION class

We assume that the C-level SAIL API's are tested in separate functional tests so we are concerned only with testing the actual LANGUAGE_SELECTION itself, not the SAIL interface

test cases --
Tests are divided into several functional areas
 - checking state -- calling the API when we know the LANGUAGE_SELECTION is in the wrong state
 - parameter checking -- validate parameter values for NULL, etc
 - unimplemented feature checking -- LANGUAGE_SELECTION implements only a subset of IMediaController these test the unimplemented portions to make sure they return correct values.
 -- Source testing
  At this time, we do not do any testing based on other MSP modules such as PSI, DisplaySession, etc.
*/

#if !defined(LANGUAGE_SELECTION_TEST_H)
#define LANGUAGE_SELECTION_TEST_H

#include <cxxtest/TestSuite.h>

//#include <media-player-session.h>
//#include "IMediaPlayer.h"
#include "MspCommon.h"
#include "languageSelection.h"

class LANGUAGE_SELECTIONTestSuite : public CxxTest::TestSuite
{
public:

    void setUp()		// TODO: move this to one-time setup
    {
    }

    void test_LANGUAGE_SELECTION(void)
    {
        TS_TRACE("Testing language Selection ");
        tPid pid;

        Psi * psi = new Psi();
        TS_ASSERT(psi != NULL);

        LanguageSelection *pLangSel = new LanguageSelection(LANG_SELECT_AUDIO, psi);

        TS_ASSERT(pLangSel != NULL);

        pid = pLangSel->pidSelected();

        delete pLangSel;
        TS_TRACE("Done language Selection test");
        //  TS_ASSERT(pid.pid==0x1fff);
        //  TS_ASSERT(pid.streamType==0);

    }

};

/**
 * \brief -- test parameter validation
 */




#endif
