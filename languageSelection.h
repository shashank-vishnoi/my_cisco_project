
#if !defined(LANGUAGE_SELECTION_H)
#define LANGUAGE_SELECTION_H

// standard linux includes
#include <stdint.h>
#include <stdbool.h>
#include <cstring>
#if PLATFORM_NAME == IP_CLIENT
#include "psi_ic.h"
#endif
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#include "psi.h"
#endif
#include <endian.h>

#define ISO_LANGUAGE_CODE_LENGTH 3
#define LANGUAGE_SELECTION_COUNT 9
#define SEPARATOR_CHAR_LENGTH 1

#define ISO_639_LANG_DESCR_TAG 10
#define CAPTION_SERVICE_DESCR_TAG 0x86
#define LANG_CODE_SIZE 3
#define ENGLISH_MAIN "eng" //for main audio selection
#define FRENCH_MAIN "fra" //for main audio selection
#define SPANISH_MAIN "spa" //for main audio selection
#define ENGLISH "d1_eng"  //for CC audio selection
#define ENGLISH_EZ "d1_eng_ez"
#define DVS_ENGLISH "enm"
#define FRENCH "fra"
#define FRENCH_OTHER "d1_fre" //for CC audio selection
#define FRENCH_OTHER_EZ "d1_fre_ez"
#define SPANISH "d1_spa" //for CC audio selection
#define SPANISH_EZ "d1_spa_ez"
#define DVS_FRENCH "frm"
#define DVS_SPANISH "spm"
#define	BACKWARD_COMPATIBLE_STR	"d1_"
#define	CC_LANG_BCK_COMP_ISO_LENGTH	6 	// 3 bytes for "d1_" and 3 bytes for ISO lang code

#define	CAPTION_LANG_SIZE	    	15
#define	CAPTION_SERVICE_SKIP_LENGTH 	6
#define	CAPTION_SERVICE_OFFSET	    	5
#define	EZ_READER_SUFFIX	    	"_ez"
#define	EZ_R_LEN		    	6
#define PRIMARY_DIGITAL_SERVICE_NO  	1
#define SECONDARY_DIGITAL_SERVICE_NO  	2

#if PLATFORM_NAME == G6

#define ENDIANIZE_BITS2(x1,x2) x1 x2
#define ENDIANIZE_BITS3(x1,x2,x3) x1 x2 x3
static inline void bitswap16(uint8_t *buf)
{
    (void) buf;
}
#else

#define ENDIANIZE_BITS2(x1,x2) x2 x1
#define ENDIANIZE_BITS3(x1,x2,x3) x3 x2 x1

static inline void bitswap16(uint8_t * buf)
{
    *((uint16_t*)buf) = bswap_16((*(uint16_t*)buf));
}
#endif
typedef struct __attribute__((__packed__))
{
    uint8_t tag;
    uint8_t len;
} ccdescriptor;

typedef struct __attribute__((__packed__))
{
    //ccdescriptor desc;

    ENDIANIZE_BITS2(
        uint8_t reserved              : 3; ,
        uint8_t number_of_services    : 5;);
} caption_service_descriptor ;

typedef struct __attribute__((__packed__))
{
    uint8_t language[3];
    ENDIANIZE_BITS3(
        uint8_t digital_cc          	: 1; ,
        uint8_t reserved        	  	: 1; ,
        uint8_t captionServiceNumber      : 6;);
} caption_service_entry;

typedef struct __attribute__((__packed__))
{
    ENDIANIZE_BITS3(
        uint16_t easy_reader            : 1; ,
        uint16_t wide_aspect_ratio      : 1; ,
        uint16_t reserved1              : 14;);
} caption_easy_reader;

typedef enum
{
    LANG_SELECT_AUDIO,
    LANG_SELECT_VIDEO,
    LANG_SELECT_CAPTION
} etLangSelectionType;

class PidWLang
{
private:
    uint16_t mStreamType;
    uint16_t mPidNum;
    bool mbHasLangDesc;
    char mLangCode[LANG_CODE_SIZE + 1]; //Lang Code Size + Null term.
public:
    PidWLang(uint16_t type, uint16_t pidVal)
    {
        mStreamType = type;
        mPidNum = pidVal;
        mbHasLangDesc = false;
        std::memset(mLangCode, '\0' , LANG_CODE_SIZE + 1);
    }
    void setHasLangDesc(bool hasLang)
    {
        mbHasLangDesc = hasLang;
    }
    void setLangCode(char * langCode)
    {
        if (langCode)
        {
            std::strncpy(mLangCode, langCode, LANG_CODE_SIZE);
            mLangCode[LANG_CODE_SIZE] = '\0';
        }
    }
    char * getLangCode()
    {
        return mLangCode;
    }
    bool hasLangDesc()
    {
        return mbHasLangDesc;
    }
    uint16_t getPid()
    {
        return mPidNum;
    }
    uint16_t getStreamType()
    {
        return mStreamType;
    }
    ~PidWLang() {}

};

//typedef char* languagePref ;

class LanguageSelection
{
private:
    etLangSelectionType mType;
    char mUserAudiolangPreference[LANGUAGE_SELECTION_COUNT*ISO_LANGUAGE_CODE_LENGTH]; /// get from unified setting
    int mUserAudioPrefSettingCount;
    bool mbDVSset;                   /// get from unified setting, only Applied to audio
    void getUserAudioLangPref(void); /// Get User's language preference from united setting
    bool getUserDVSsetting(void);     /// Get User's DVS setting from united setting
    Psi *mpPsiInstance;
    std::list<PidWLang *> mListOfLangPid;
    std::list<PidWLang *> mListofDVSLangPid;
    void buildListOfLangPid(std::list<tPid> & aList);
    tPid selectPidBylang(char * langPref, int cnt, bool isDVS);
    bool langSettingValid(const char* lang);
    void clearLangPidList();
    tPid getDefautpid(bool isDVS);
    bool validAlphabet(char c)
    {
        return (c >= 'a' && c <= 'z');
    }

public:
    LanguageSelection(etLangSelectionType selType, Psi *psiInstance);

    tPid pidSelected();
    tPid videoPidSelected(); /// Returns first video PID from the video PID list
    ~LanguageSelection(void);
};



#endif
