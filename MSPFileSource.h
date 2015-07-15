#ifndef MSP_FILE_SOURCE_H
#define MSP_FILE_SOURCE_H

#include <string>
#include <list>
#include "MSPSource.h"
#include "MspCommon.h"


class MSPFileSource: public MSPSource
{
    tCpeSrcHandle mCpeSrcHandle;
    std::string mSrcUrl;
    int mSourceId;
    int mProgramNumber;
    tCpeSrcCallbackID mSrcCbIdEOF;
    tCpeSrcCallbackID mSrcCbIdBOF;
    SourceStateCallback mSrcStateCB;
    void *mClientContext;
    eMspStatus mParseStatus;
    std::list <std::string> mFileNameList;
    uint32_t mCurrentFileIndex;
    std::string mFileName;
    std::string mCurrentSetFileName;
    bool mIsRewindMode;
    bool mStarted;

public:
    MSPFileSource(std::string aSrcUrl);

    ~MSPFileSource();

    eMspStatus load(SourceStateCallback aSrcStateCB, void* aClientContext);

    /* This function open source handle & registers Callback for EOF & BOF */
    eMspStatus open(eResMonPriority pri = kRMPriority_VideoWithAudioFocus);

    /* Start the source */
    eMspStatus start();

    /* Stop source handle & unregisters File Playback Callbacks */
    eMspStatus stop();

    /* release source */
    eMspStatus release();

    /* Getter Methods for Media Controller, Display Session & PSI */
    tCpeSrcHandle getCpeSrcHandle()const;

    int getSourceId()const;

    int getProgramNumber()const;

    /* Required for PIP Swap */
    std::string getSourceUrl()const;


    /* Get File Name */
    std::string getFileName()const;

    bool isDvrSource()const;

    bool canRecord()const;

    bool isPPV(void);

    bool isQamrf(void);

    eMspStatus setSpeed(tCpePlaySpeed aPlaySpeed, uint32_t aNpt);

    eMspStatus getPosition(float *aNptTime);

    bool isAnalogSource()const;

    eMspStatus setPosition(float aNptTime);

    /* Play back callback function EOF */
    static void PlayBkCallbackFunction_EOF(tCpeSrcCallbackTypes aType, void *aUserData, void *pCallbackSpecific);

    /* Play Back callback function BOF */
    static void PlayBkCallbackFunction_BOF(tCpeSrcCallbackTypes aType, void *aUserData, void *pCallbackSpecific);

    tCpePgrmHandle getCpeProgHandle()const;
    void SetCpeStreamingSessionID(uint32_t sessionId);

private:
    void buildFileList();
    eMspStatus setFileByIndex(uint32_t aFileIndex);

};

#endif // #ifndef MSP_FILE_SOURCE_H

