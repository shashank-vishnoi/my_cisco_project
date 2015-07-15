
#if !defined(IPLAYSESSION_H)
#define IPLAYSESSION_H

// Stubs for IPlaySession file.
//typedef void (*CCIcallback_t) (void *pData, int iCCIbyte);

class IPlaySession
{
private:
    //CCIcallback_t mCb;
    void *mData;
public:
    IPlaySession();
//   int registerCCIupdate(void *pData, CCIcallback_t cbFunc);
    int unRegisterCCIupdate(int regId);
    virtual ~IPlaySession()
    {};
};

#endif
