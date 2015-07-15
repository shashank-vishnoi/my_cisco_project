/**
   \file shim-video-scaling-cperpv1.cpp
   Implementation file for shim layer for video scaling for G6

*/

#include "avpm.h"
#include "sail_dfb.h"
#include <dlog.h>
#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_AVPM, level,"Avpm:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
void Avpm::setStereoDepth(tCpeVshScaleRects& rects)
{
    (void)rects;
}
void Avpm::DisplayCallback_HDPIP(void                    *ctx,
                                 tCpeVshDisplayToFlags   *flags,
                                 tCpeVshFrameAttributes  *frameAttrib,
                                 IDirectFBSurface        *frameSurface,
                                 IDirectFBSurface        *altSurface,
                                 DFBDimension            screenSize,
                                 tCpeVshScaleRects       *scaleRects)
{
    FNLOG(DL_MSP_AVPM);
    float newAspect = 0.0;
    Avpm *inst = (Avpm*)getAvpmInstance();
    tCpePgrmHandle temp_handle = (tCpePgrmHandle)ctx;
    if (frameAttrib)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d ctx %p", __FUNCTION__, __LINE__, ctx);

        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d Callback flags %x surf %p altsurf %p,scrw %d scrh %d", __FUNCTION__, __LINE__,
             *flags, frameSurface, altSurface, screenSize.w, screenSize.h);

        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d attribe flags %x stream flags %x type %x", __FUNCTION__, __LINE__,
             frameAttrib->flags, frameAttrib->stream.flags, frameAttrib->frameType);

        logScalingRects("Current presentation params at  DisplayCallback_HD", *scaleRects);

        if (temp_handle == inst->mMainScreenPgrHandle)
        {
            inst->threadEventQueue->dispatchEvent(kAvpmApplyHDMainPresentationParams);
        }
        else
        {
            inst->threadEventQueue->dispatchEvent(kAvpmApplyHDPipPresentationParams);
        }

        newAspect = frameAttrib->stream.aspect;
    }
    else
    {
        newAspect = 0.0f;
    }

    if (newAspect != frame_aspPIP)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "Updated AR old %f new %f ", frame_asp, newAspect);
        if (inst->threadEventQueue != NULL)
        {
            inst->threadEventQueue->dispatchEvent(kAvpmStreamAspectChanged);
        }
        frame_aspPIP = newAspect;
    }

}

void Avpm::DisplayCallback_SDPIP(void *ctx,
                                 tCpeVshDisplayToFlags *flags,
                                 tCpeVshFrameAttributes *frameAttrib,
                                 IDirectFBSurface *frameSurface,
                                 IDirectFBSurface *altSurface,
                                 DFBDimension screenSize,
                                 tCpeVshScaleRects *scaleRects)
{
    float newAspect = 0.0f;
    Avpm *inst = (Avpm*)getAvpmInstance();
    tCpePgrmHandle temp_handle = (tCpePgrmHandle)ctx;

    if (frameAttrib)
    {
        FNLOG(DL_MSP_AVPM);
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d ctx %p", __FUNCTION__, __LINE__, ctx);

        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d Callback flags %x surf %p altsurf %p,scrw %d scrh %d", __FUNCTION__, __LINE__,
             *flags, frameSurface, altSurface, screenSize.w, screenSize.h);

        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d attribe flags %x stream flags %x type %x", __FUNCTION__, __LINE__,
             frameAttrib->flags, frameAttrib->stream.flags, frameAttrib->frameType);

        logScalingRects("Current presentation params at DisplayCallback_SD ", *scaleRects);

        if (temp_handle == inst->mMainScreenPgrHandle)
        {
            inst->threadEventQueue->dispatchEvent(kAvpmApplySDMainPresentationParams);
        }
        else
        {
            inst->threadEventQueue->dispatchEvent(kAvpmApplySDPipPresentationParams);
        }

        newAspect = frameAttrib->stream.aspect;
    }
    else
    {
        newAspect = 0.0f;
    }

    if (newAspect != frame_aspPIP)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "Updated AR old %f new %f ", frame_asp, newAspect);
        if (inst->threadEventQueue != NULL)
        {
            inst->threadEventQueue->dispatchEvent(kAvpmStreamAspectChanged);
        }
        frame_aspPIP = newAspect;
    }

}
eMspStatus Avpm::playPIPVideo(tCpePgrmHandle pgrHandle) // //G6:play PIP in GFX Surface (DLID_GFX2)
{

    DFBResult result;
    IVideoStreamHandler *vsh = NULL;
    tCpeMshAssocHandle hdHandle;
    tCpeMshAssocHandle sdHandle;

    FNLOG(DL_MSP_AVPM);

    ProgramHandleSetting* pgrHandleSetting = getProgramHandleSettings(pgrHandle);

    if (!pgrHandleSetting)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error in getting the ProgramHandleSetting", __FUNCTION__, __LINE__);
        return kMspStatus_AvpmError;
    }

    vsh = pgrHandleSetting->vsh;

    if (hdGfx2surface && vsh)
    {
        result = vsh->DisplayToSurface(vsh, tCpeVshDisplayToFlags_OnAspect, hdGfx2surface, DisplayCallback_HDPIP, (void *)pgrHandle, &hdHandle);
        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error calling DisplayToSurface HD Err: %d", __FUNCTION__, __LINE__, result);
            return kMspStatus_AvpmError;
        }

        pgrHandleSetting->hdHandle = hdHandle;
        setWindow(pgrHandleSetting->rect, vsh, eCpeDFBScreenIndex_HD, hdHandle);

    }

    if (sdGfx2surface && vsh)
    {
        result = vsh->DisplayToSurface(vsh, tCpeVshDisplayToFlags_OnAspect, sdGfx2surface, DisplayCallback_SDPIP, (void *)pgrHandle, &sdHandle);
        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s(%d) Error calling DisplayToSurface SD Err: %d", __FUNCTION__, __LINE__, result);
            return kMspStatus_AvpmError;
        }

        pgrHandleSetting->sdHandle = sdHandle;
        setWindow(pgrHandleSetting->rect, vsh, eCpeDFBScreenIndex_SD, sdHandle);
    }

    renderToSurface = true;

    return kMspStatus_Ok;
}

void Avpm::setupHDPIPPOPLayer(void)
{
    DFBResult result;
    DirectResult dr;
    DFBDisplayLayerConfig  dlc;

    FNLOG(DL_MSP_AVPM);

    if (hdGfx2surface)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d release old hd surface:%p", __FUNCTION__, __LINE__, hdGfx2surface);
        dr = hdGfx2surface->Release(hdGfx2surface);
        if (dr != DR_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s:%d Could not release surface: Code %d", __FUNCTION__, __LINE__, dr);
        }
        hdGfx2surface = NULL;
    }

    if (hdGfx2Layer)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d release old hd layer:%p", __FUNCTION__, __LINE__, hdGfx2Layer);
        dr = hdGfx2Layer->Release(hdGfx2Layer);
        if (dr != DR_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s:%d Could not release Layer: Code %d", __FUNCTION__, __LINE__, dr);
        }
        hdGfx2Layer = NULL;
    }
    if (dfb)
    {
        // setup Hd gfx2 for PIP/POP use
        result = dfb->GetDisplayLayer(dfb, VANTAGE_HD_PIP, &hdGfx2Layer);
        if ((result != DFB_OK) || (hdGfx2Layer == NULL))
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "Get HD GFX2 Display Layer failed (0x%x)", result);
            return;
        }
    }
    // set cooperative level to DLSCL_ADMINISTRATIVE to change level configuration
    result = hdGfx2Layer->SetCooperativeLevel(hdGfx2Layer, DLSCL_ADMINISTRATIVE);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "SetCooperativeLevel failed code: (0x%x)", result);
        hdGfx2Layer->Release(hdGfx2Layer);
    }

    memset(&dlc, 0, sizeof(dlc));
    GetScreenSize(eCpeDFBScreenIndex_HD, &dlc.width, &dlc.height);
    dlc.flags = (DFBDisplayLayerConfigFlags)(DLCONF_PIXELFORMAT | DLCONF_WIDTH | DLCONF_HEIGHT | DLCONF_BUFFERMODE | DLCONF_SOURCE | DLCONF_SURFACE_CAPS);
    dlc.buffermode = DLBM_FRONTONLY;
    dlc.pixelformat =  DSPF_ARGB;
    dlc.source = 0;  // unique buffer not mirror of HD
    // dlc.source = 1;  // mirror of HD don't set this for this plane
    dlc.surface_caps = DSCAPS_VIDEOONLY;

    result = hdGfx2Layer->SetConfiguration(hdGfx2Layer, &dlc);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s:%d SetConfiguration fails %dx%dx%d (0x%x flags=0x%x)", __FUNCTION__, __LINE__, dlc.width, dlc.height, dlc.pixelformat, result, dlc.flags);
    }

    result = hdGfx2Layer->GetSurface(hdGfx2Layer, &hdGfx2surface);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s:%d GetSurface fails code: (0x%x)", __FUNCTION__, __LINE__, result);
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d GetSurface returned: %p", __FUNCTION__, __LINE__, hdGfx2surface);
    }

    if (hdGfx2surface)
    {
        result = hdGfx2surface->Clear(hdGfx2surface, 0, 0, 0, 0);
        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s:%d Could not clear HDGFX2 code: (0x%x)", __FUNCTION__, __LINE__, result);
        }
    }

    GetScreenSize(eCpeDFBScreenIndex_HD, &mMaxOutputScreenWidthHD, &mMaxOutputScreenHeightHD);
}

void Avpm::setupSDPIPPOPLayer(void)
{
    DFBResult result;
    DirectResult dr;
    DFBDisplayLayerConfig  dlc;

    FNLOG(DL_MSP_AVPM);

#ifndef SD_PIP_MIRRORING
    if (sdGfx2surface)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d release old sd surface:%p", __FUNCTION__, __LINE__, sdGfx2surface);
        dr = sdGfx2surface->Release(sdGfx2surface);
        if (dr != DR_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s:%d Could not release surface: Code %d", __FUNCTION__, __LINE__, dr);
        }
        sdGfx2surface = NULL;
    }
#endif


    if (sdGfx2Layer)
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d release old sd layer:%p", __FUNCTION__, __LINE__, sdGfx2Layer);
        dr = sdGfx2Layer->Release(sdGfx2Layer);
        if (dr != DR_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s:%d Could not release Layer: Code %d", __FUNCTION__, __LINE__, dr);
        }
        sdGfx2Layer = NULL;
    }
    if (dfb)
    {
        // setup sd gfx2 for PIP/POP use
        result = dfb->GetDisplayLayer(dfb, VANTAGE_SD_PIP, &sdGfx2Layer);
        if ((result != DFB_OK) || (sdGfx2Layer == NULL))
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "Get SD GFX2 Display Layer failed (0x%x)", result);
            return;
        }
    }
    // set cooperative level to DLSCL_ADMINISTRATIVE to change level configuration
    result = sdGfx2Layer->SetCooperativeLevel(sdGfx2Layer, DLSCL_ADMINISTRATIVE);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "SetCooperativeLevel failed code: (0x%x)", result);
        sdGfx2Layer->Release(sdGfx2Layer);
    }

    memset(&dlc, 0, sizeof(dlc));
    GetScreenSize(eCpeDFBScreenIndex_SD, &dlc.width, &dlc.height);
    dlc.flags = (DFBDisplayLayerConfigFlags)(DLCONF_PIXELFORMAT | DLCONF_WIDTH | DLCONF_HEIGHT | DLCONF_BUFFERMODE | DLCONF_SOURCE | DLCONF_SURFACE_CAPS);
    dlc.buffermode = DLBM_FRONTONLY;
    dlc.pixelformat =  DSPF_ARGB;
#ifdef SD_PIP_MIRRORING
    dlc.source = 1;  // mirroring HD.
#else
    dlc.source = 0; //unique buffer.not mirror of HD
#endif
    dlc.surface_caps = DSCAPS_VIDEOONLY;

    result = sdGfx2Layer->SetConfiguration(sdGfx2Layer, &dlc);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s:%d SetConfiguration fails %dx%dx%d (0x%x flags=0x%x)", __FUNCTION__, __LINE__, dlc.width, dlc.height, dlc.pixelformat, result, dlc.flags);
    }


#ifndef SD_PIP_MIRRORING
    result = sdGfx2Layer->GetSurface(sdGfx2Layer, &sdGfx2surface);
    if (result != DFB_OK)
    {
        dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s:%d GetSurface fails code: (0x%x)", __FUNCTION__, __LINE__, result);
    }
    else
    {
        dlog(DL_MSP_AVPM, DLOGL_REALLY_NOISY, "%s:%d GetSurface returned: %p", __FUNCTION__, __LINE__, sdGfx2surface);
    }

    if (sdGfx2surface)
    {
        result = sdGfx2surface->Clear(sdGfx2surface, 0, 0, 0, 0);
        if (result != DFB_OK)
        {
            dlog(DL_MSP_AVPM, DLOGL_ERROR, "%s:%d Could not clear SDGFX2 code: (0x%x)", __FUNCTION__, __LINE__, result);
        }
    }
#endif

    GetScreenSize(eCpeDFBScreenIndex_SD, &mMaxOutputScreenWidthSD, &mMaxOutputScreenHeightSD);
}


