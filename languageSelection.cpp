#include <iostream>
#include <list>
#include "languageSelection.h"
#include "MspCommon.h"
#include <dlog.h>
#include <string.h>
#include "use_threaded.h"


#if defined(DMALLOC)
#include "dmalloc.h"
#endif
#define DVS_SETTING_VALUE_SIZE 7

#define SET_PRIMARY_AUDIO_TRACK

///constructor
LanguageSelection::LanguageSelection(etLangSelectionType selType, Psi *psiInstance)
{
    mType = selType;
    mpPsiInstance = psiInstance;
    //mListOfLangPid();
//coverity id - 10396
//initializing the following variables to default values.
    mUserAudioPrefSettingCount = 0;
    memset(mUserAudiolangPreference, 0, sizeof(mUserAudiolangPreference));
    mbDVSset = false;
}

///Desctructor
LanguageSelection::~LanguageSelection()
{
    FNLOG(DL_MSP_MPLAYER);
    clearLangPidList();
}

///Clear private PID list
void LanguageSelection::clearLangPidList()
{
    FNLOG(DL_MSP_MPLAYER);

    std::list<PidWLang *>::iterator iter;

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s(%d): mListOfLangPid list size %d\n", __FUNCTION__, __LINE__, mListOfLangPid.size());
    for (iter = mListOfLangPid.begin(); iter != mListOfLangPid.end(); iter++)
    {
        if (*iter)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s(%d): deleting list elem %p", __FUNCTION__, __LINE__, *iter);
            delete *iter;
            *iter = NULL;
        }
        //mListOfLangPid.erase(iter);
    }
    mListOfLangPid.clear();
    if (!mListofDVSLangPid.empty())
    {
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s(%d): mListofDVSLangPid list size %d\n", __FUNCTION__, __LINE__, mListofDVSLangPid.size());
        for (iter = mListofDVSLangPid.begin(); iter != mListofDVSLangPid.end(); iter++)
        {
            if (*iter)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s(%d): deleting list elem %p", __FUNCTION__, __LINE__, *iter);
                delete *iter;
                *iter = NULL;
            }
            //mListOfLangPid.erase(iter);
        }

        mListofDVSLangPid.clear();
    }
}

/// Get User's language preference from united setting
void LanguageSelection::getUserAudioLangPref(void)
{
    FNLOG(DL_MSP_MPLAYER);
    // call unified setting to get it; for now, stub it as not set (All 0s)
    std::memset(mUserAudiolangPreference, 0, sizeof(mUserAudiolangPreference));
    mUserAudioPrefSettingCount = 0;

    eUseSettingsLevel settingLevel;
    uint32_t langListSize = (LANGUAGE_SELECTION_COUNT) * (ISO_LANGUAGE_CODE_LENGTH + SEPARATOR_CHAR_LENGTH);
    char userSetting[langListSize + 1] ;
    Uset_getSettingT(NULL, "ciscoSg/audio/audioLangList", langListSize, userSetting, &settingLevel);

    const char *lang = userSetting;

    for (int i = 0; i < LANGUAGE_SELECTION_COUNT; i++)
    {
        if (langSettingValid(lang))
        {
            if ((std::strncmp(lang, "fra", ISO_LANGUAGE_CODE_LENGTH) == 0))
            {
                std::strncpy(&mUserAudiolangPreference[mUserAudioPrefSettingCount * ISO_LANGUAGE_CODE_LENGTH], "fre", ISO_LANGUAGE_CODE_LENGTH);
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d) Found a french  lang code %c%c%c and substituting equivalent ISO stream code \"fre\" ", __FUNCTION__, __LINE__, lang[0], lang[1], lang [2]);
            }
            else
            {
                std::strncpy(&mUserAudiolangPreference[mUserAudioPrefSettingCount * ISO_LANGUAGE_CODE_LENGTH], lang, ISO_LANGUAGE_CODE_LENGTH);
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d) Found a valid user lang code %c%c%c\n ", __FUNCTION__, __LINE__, lang[0],
                     lang[1], lang [2]);
            }
            mUserAudioPrefSettingCount++;
        }
        lang = lang + ISO_LANGUAGE_CODE_LENGTH + SEPARATOR_CHAR_LENGTH;
    }
    mUserAudiolangPreference[LANGUAGE_SELECTION_COUNT * ISO_LANGUAGE_CODE_LENGTH - 1] = '\0';

    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): total user audio lang pref cnt %d\n", __FUNCTION__, __LINE__, mUserAudioPrefSettingCount);
}

/// Get User's DVS setting from united setting
bool LanguageSelection::getUserDVSsetting(void)
{
    // call unified setting to get it; for now, stub it as not set (false)
    FNLOG(DL_MSP_MPLAYER);
    char dvsEnabledStr[DVS_SETTING_VALUE_SIZE];
    eUseSettingsLevel settingLevel;
    Uset_getSettingT(NULL, "ciscoSg/audio/audioDescribed", DVS_SETTING_VALUE_SIZE, dvsEnabledStr, &settingLevel);

    if (strcmp(dvsEnabledStr, "true") == 0)
    {
        mbDVSset = true;
    }
    else
    {
        mbDVSset = false;
    }

    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): user DVS setting %d\n", __FUNCTION__, __LINE__, (int)mbDVSset);
    return mbDVSset;
}

///Decide if user' language preference codes is correct
bool LanguageSelection::langSettingValid(const char* lang)
{
    bool bValid = false;
    if (lang) //&& strlen(lang)== 3 )
    {
        bValid = (validAlphabet(lang[0]) && validAlphabet(lang[1]) && validAlphabet(lang[2]));
    }
    return bValid;
}


/// build the member of language pid list from a tPid list got from PSI
void LanguageSelection::buildListOfLangPid(std::list<tPid> & aList)
{
    FNLOG(DL_MSP_MPLAYER);
    if (aList.size() > 0)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s(%d): audio pid cnt from psi %d\n", __FUNCTION__, __LINE__, aList.size());
        // clear old language pid list
        clearLangPidList();
        std::list<tPid>::iterator iter; // iterator for the PSI's pid list
        //std::list<PidWLang> pidListWlang;
        for (iter = aList.begin(); iter != aList.end(); iter++)
        {
            PidWLang * aPid = new PidWLang((*iter).streamType, (*iter).pid);
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): List: audio pid %d, type = 0x%x\n", __FUNCTION__, __LINE__, (*iter).pid, (*iter).streamType);
#if PLATFORM_NAME == IP_CLIENT
            tMpegDesc langDescr;
#endif
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
            tCpePgrmHandleMpegDesc langDescr;
#endif
            langDescr.tag = ISO_639_LANG_DESCR_TAG;
            langDescr.dataLen = 0;
            langDescr.data = NULL;

            if (mpPsiInstance && mpPsiInstance->getPmtObj())
            {
                // Coverity id - 10047 checking return for getDescriptor
                eMspStatus status = mpPsiInstance->getPmtObj()->getDescriptor(&langDescr, (*iter).pid);
                if (status == kMspStatus_Ok && langDescr.dataLen >= 3)
                {
                    aPid->setHasLangDesc(true);
                    aPid->setLangCode((char *)langDescr.data);
                    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): Pid %d has language code %c%c%c \n", __FUNCTION__, __LINE__, (*iter).pid,
                         langDescr.data[0], langDescr.data[1], langDescr.data[2]);

                }
                else
                {
                    dlog(DL_MEDIAPLAYER, DLOGL_NOISE, "Language Descriptor length %d\n", langDescr.dataLen);
                }
                mpPsiInstance->getPmtObj()->releaseDescriptor(&langDescr);
                char *langCode = aPid->getLangCode();
                for (unsigned int j = 0; j < strlen(langCode); j++)
                {
                    langCode[j] = tolower(langCode[j]);
                }
                if ((strncmp(langCode, DVS_ENGLISH, ISO_LANGUAGE_CODE_LENGTH) == 0) ||
                        (strncmp(langCode, DVS_FRENCH, ISO_LANGUAGE_CODE_LENGTH) == 0) ||
                        (strncmp(langCode, DVS_SPANISH, ISO_LANGUAGE_CODE_LENGTH) == 0))
                {
                    mListofDVSLangPid.push_back(aPid);
                }
                else
                {
                    mListOfLangPid.push_back(aPid);
                }
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d) mListOfLangPid: added list element %p\n", __FUNCTION__, __LINE__, aPid);
            }
            else
            {
                delete aPid;
                aPid = NULL;
            }
        }
    }
    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): language Pid list size %d and dvs list  size %d\n", __FUNCTION__, __LINE__, mListOfLangPid.size(), mListofDVSLangPid.size());
}
/// Select the proper pid from the list of pid With language, mListOfLangPid, with the language preference
tPid LanguageSelection::selectPidBylang(char * langPref, int cnt, bool isDVS)
{
    tPid thePid ;
    tPid defaultPid;
    bool isDefaultPidSelected = false;
    FNLOG(DL_MSP_MPLAYER);
    bool pidFound = false;
    thePid.pid = 0x1FFF;
    thePid.streamType = 0;
    defaultPid.pid = 0x1FFF;
    defaultPid.streamType = 0;
    std::list<PidWLang *> mLangList;
    if (isDVS)
    {
        mLangList = mListofDVSLangPid;
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, " the DVS list is selected having size %d ", mListofDVSLangPid.size());
    }
    else
    {
        mLangList = mListOfLangPid;

        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, " the language list selected having size %d ", mListOfLangPid.size());
    }

    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): mListOfLangPid size %d\n", __FUNCTION__, __LINE__, mListOfLangPid.size());
    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): langPref cnt %d\n", __FUNCTION__, __LINE__, cnt);

    if (mLangList.size() > 0 && cnt > 0)
    {
        std::list<PidWLang *>::iterator iter1;

        for (int i = 0; i < cnt; i++)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): i= %d: langCode: %c%c%c\n", __FUNCTION__, __LINE__, i, langPref[0], langPref[1], langPref[2]);
            for (iter1 = mLangList.begin(); iter1 != mLangList.end(); iter1++)
            {
                if ((*iter1)->hasLangDesc())
                {
                    char *langCode = (*iter1)->getLangCode();
                    for (unsigned int j = 0; j < strlen(langCode); j++)
                    {
                        langCode[j] = tolower(langCode[j]);
                    }

                    if ((strncmp(langCode, &langPref[i * ISO_LANGUAGE_CODE_LENGTH], ISO_LANGUAGE_CODE_LENGTH) == 0) && (pidFound == false))
                    {
                        /// found the pid
                        thePid.pid = (*iter1)->getPid();
                        thePid.streamType = (*iter1)->getStreamType();
                        pidFound = true;
                        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): Found the proper audio pid by lang preference: pid=%d. type= 0x%x\n",
                             __FUNCTION__, __LINE__, thePid.pid, thePid.streamType);
                    }
                    // If audio pid is not already selected, and pid matches the default lang, set the pid.
                    if (!isDefaultPidSelected	&& ((strncmp(langCode, ENGLISH_MAIN, ISO_LANGUAGE_CODE_LENGTH) == 0)
                                                    || (strncmp(langCode, DVS_ENGLISH, ISO_LANGUAGE_CODE_LENGTH) == 0)))
                    {
                        defaultPid.pid = (*iter1)->getPid();
                        defaultPid.streamType = (*iter1)->getStreamType();
                        isDefaultPidSelected = true;
                        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): Selecting the default audio language: pid=%d. type= 0x%x\n",
                             __FUNCTION__, __LINE__, defaultPid.pid, defaultPid.streamType);
                    }
                }
            }
        }
    }
    if (thePid.pid == 0x1FFF && thePid.streamType == 0)
    {

        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, " the default language selected with pid %d ", defaultPid.pid);
        return defaultPid;

    }

    return thePid;
}

tPid LanguageSelection::getDefautpid(bool inDVS)
{
    tPid thePid;
    thePid.pid = 0x1FFF;
    thePid.streamType = 0;
    FNLOG(DL_MSP_MPLAYER);
    std::list<PidWLang *>::iterator iter1;
    if (inDVS)
    {
        if (mListofDVSLangPid.size() > 0)
        {
            iter1 = mListofDVSLangPid.begin();
            thePid.pid = (*iter1)->getPid();
            thePid.streamType = (*iter1)->getStreamType();
        }

    }
    else
    {
        if (mListOfLangPid.size() > 0)
        {
            iter1 = mListOfLangPid.begin();
            thePid.pid = (*iter1)->getPid();
            thePid.streamType = (*iter1)->getStreamType();
        }
    }
    return thePid;
}

///Public API: select the proper pid by user's language preference
tPid LanguageSelection::pidSelected()
{
    tPid pid;

    FNLOG(DL_MSP_MPLAYER);

    //Initialize the pid value
    pid.pid = 0x1fff;
    pid.streamType = 0;
    bool inDVS = false;
    // Only support audio pid selection for now
    if (mType == LANG_SELECT_AUDIO)
    {
        getUserDVSsetting();
        getUserAudioLangPref();

        if (mpPsiInstance && mpPsiInstance->getPmtObj())
        {
            const std::list<tPid> * aList = mpPsiInstance->getPmtObj()->getAudioPidList();

            if (aList && aList->size() > 0)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): audio pid cnt from psi %d\n", __FUNCTION__, __LINE__, aList->size());
                std::list<tPid>  bList(*aList);
                // rebuild the list of pid with language from the pSI's pid list
                buildListOfLangPid(bList);
                char langSelWithDvs[LANGUAGE_SELECTION_COUNT * ISO_LANGUAGE_CODE_LENGTH];
                std::memset(langSelWithDvs, 0, sizeof(langSelWithDvs));

                if (mbDVSset)
                {
                    int i = 0, cnt = 0;
                    /// find the english and french code, translate them to the corresponding video description code
                    for (i = 0; i < mUserAudioPrefSettingCount; i++)
                    {
                        if (std::strncmp(&mUserAudiolangPreference[i * ISO_LANGUAGE_CODE_LENGTH], ENGLISH_MAIN, 3) == 0)
                        {
                            std::strncpy(&langSelWithDvs[cnt++*ISO_LANGUAGE_CODE_LENGTH], DVS_ENGLISH, 3);
                        }
                        else if (std::strncmp(&mUserAudiolangPreference[i * ISO_LANGUAGE_CODE_LENGTH], FRENCH_MAIN, 3) == 0 ||
                                 std::strncmp(&mUserAudiolangPreference[i * ISO_LANGUAGE_CODE_LENGTH], FRENCH_OTHER, 3) == 0)
                        {
                            std::strncpy(&langSelWithDvs[cnt++*ISO_LANGUAGE_CODE_LENGTH], DVS_FRENCH, 3);
                        }
                        else if (std::strncmp(&mUserAudiolangPreference[i * ISO_LANGUAGE_CODE_LENGTH], SPANISH_MAIN, 3) == 0)
                        {
                            std::strncpy(&langSelWithDvs[cnt++*ISO_LANGUAGE_CODE_LENGTH], DVS_SPANISH, 3);
                        }
                    }
                    if (cnt)
                    {
                        // FOR DVS we need to send 2 languages as prefered language.
                        pid = selectPidBylang(langSelWithDvs, cnt, (!inDVS));
                        if (pid.pid == 0x1FFF && pid.streamType == 0)
                        {
                            pid = getDefautpid(!inDVS);
                        }
                    }
                    else
                    {
                        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): DVS set, but no eng or fra codes in user's preference\n", __FUNCTION__, __LINE__);
                    }
                }
                // if not find the pid by DVS set language code, try the code with not DVS
                if (pid.pid == 0x1FFF && pid.streamType == 0 && mUserAudioPrefSettingCount > 0)
                {
                    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Language Code not found for DVS so searching with normal preference\n");
#ifdef SET_PRIMARY_AUDIO_TRACK
                    // This is to consider only first preference set by user.
                    pid = selectPidBylang(mUserAudiolangPreference, 1, inDVS);
#else
                    pid = selectPidBylang(mUserAudiolangPreference, mUserAudioPrefSettingCount, inDVS);
#endif
                }
                // if can not find a pid with the lanugage preference, select the first pid in the list
                if (pid.pid == 0x1FFF && pid.streamType == 0)
                {
                    dlog(DL_MEDIAPLAYER, DLOGL_NOISE, "%s(%d): Can not find an audio pid from language pref, select the first\n", __FUNCTION__, __LINE__);
                    // Assuming primary audio track will be first Pid in list
                    pid = getDefautpid(inDVS);

                    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Pid number is %d\n", pid.pid);
                }
            }
            else
            {
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): Empty audio pid list from psi\n", __FUNCTION__, __LINE__);
            }
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d): NULL psi %p or PMT instance\n", __FUNCTION__, __LINE__, mpPsiInstance);
        }
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d): The language selection type %d is not supported now\n", __FUNCTION__, __LINE__, mType);
    }

    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): PID num %d selectd, type= %d\n", __FUNCTION__, __LINE__, pid.pid, pid.streamType);
    return pid;
}

///Public API: select the proper video pid - always the first in the video PID list
tPid LanguageSelection::videoPidSelected()
{
    tPid pid;

    FNLOG(DL_MSP_MPLAYER);

    //Initialize the pid value
    pid.pid = 0x1fff;
    pid.streamType = 0;

    if (mType == LANG_SELECT_VIDEO)
    {
        if (mpPsiInstance && mpPsiInstance->getPmtObj())
        {
            const std::list<tPid> * vList = mpPsiInstance->getPmtObj()->getVideoPidList();

            if (vList && vList->size() > 0)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): video pid cnt from psi %d\n", __FUNCTION__, __LINE__, vList->size());

                // Assuming primary video track will be first Pid in list
                pid = vList->front();
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Pid number is %d Pid stream type is %d\n", pid.pid, pid.streamType);
            }
            else
            {
                dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): Empty video pid list from psi\n", __FUNCTION__, __LINE__);
            }
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d): NULL psi %p or PMT instance\n", __FUNCTION__, __LINE__, mpPsiInstance);
        }
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d): The selection type %d is not supported now\n", __FUNCTION__, __LINE__, mType);
    }

    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s(%d): PID num %d selectd, type= %d\n", __FUNCTION__, __LINE__, pid.pid, pid.streamType);

    return pid;
}

