/**
   \file mrdvr.h
   \class mrdvr
*/
#include "zapper.h"



typedef void (*mrdvrCallbackFunction)(eZapperState state, void *clientData);


/**
   \class Mrdvr
   \brief Media Controller component for MediaPlayer used on MRDVR Remote playback
   Implements the IMediaController interface.
   This controller is derived out of Zapper,as it shares more common features with it.
*/
class Mrdvr : public Zapper
{
public:
    eIMediaPlayerStatus SetSpeed(int numerator,
                                 unsigned int denominator);
    eIMediaPlayerStatus GetSpeed(int* pNumerator,
                                 unsigned int* pDenominator);
    eIMediaPlayerStatus SetPosition(float nptTime);
    eIMediaPlayerStatus GetPosition(float *nptTime);
    eIMediaPlayerStatus GetStartPosition(float* pNptTime);
    eIMediaPlayerStatus SetApplicationDataPid(uint32_t aPid);
    eIMediaPlayerStatus GetApplicationData(uint32_t bufferSize,
                                           uint8_t *buffer,
                                           uint32_t *dataSize);
    eCsciMspDiagStatus GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo);
    Mrdvr();
    ~Mrdvr();

protected:
    bool handleEvent(Event *evt);
    eIMediaPlayerStatus loadSource();
    bool isRecordingPlayback()const;
    bool isLiveSourceUsed() const;
private:

    eMspStatus setSpeedMode(int num, unsigned int den, tCpePlaySpeed *pPlaySpeed);

    eIMediaPlayerStatus StartDisplaySession();
    float mNptSetPos;
    int mSpeedNumerator;
    unsigned int mSpeedDenominator;
    float mPosition;

    std::string mFileName;
    void* mPtrCBData;
    CCIcallback_t mCCICBFn;
    boost::signals2::connection mDisplaySessioncallbackConnection;
    static void sourceCB(void *data, eSourceState aState);
    static void psiCallback(ePsiCallBackState state,
                            void *data);
    static void mediaCB(void *clientInst, tCpeMediaCallbackTypes type);

};


