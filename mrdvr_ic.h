/**
   \file mrdvr_ic.h
   \class mrdvr
*/
#include "zapper_ic.h"

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
    eIMediaPlayerStatus Play(const char* outputUrl,
                             float nptStartTime,
                             const MultiMediaEvent **pMme);

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
    eIMediaPlayerStatus GetEndPosition(float* pNptTime);
    eCsciMspDiagStatus GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo);
    eCsciMspDiagStatus GetMspStreamingInfo(DiagMspStreamingInfo *msgInfo);
    /* Returns the CCI byte value */
    uint8_t GetCciBits(void);

    Mrdvr();
    ~Mrdvr();

protected:
    bool handleEvent(Event *evt);
    eIMediaPlayerStatus loadSource();
    bool isRecordingPlayback()const;
    bool isLiveSourceUsed() const;
private:

    eIMediaPlayerStatus StartDisplaySession();
    float mNptSetPos;
    int mSpeedNumerator;
    unsigned int mSpeedDenominator;
    float mPosition;

    static void sourceCB(void *data, eSourceState aState);
};


