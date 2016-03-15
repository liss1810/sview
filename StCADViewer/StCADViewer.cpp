/**
 * This source is a part of sView program.
 *
 * Copyright © Kirill Gavrilov, 2011-2016
 */

#include "StCADViewer.h"

#include "StCADViewerGUI.h"
#include "StCADLoader.h"
#include "StCADWindow.h"
#include "StCADFrameBuffer.h"
#include "StCADMsgPrinter.h"

#include <Message_Messenger.hxx>
#include <Message_PrinterOStream.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <V3d_DirectionalLight.hxx>
#include <V3d_AmbientLight.hxx>

#include <StGLCore/StGLCore20.h>
#include <StGLWidgets/StGLMsgStack.h>

#include <StSettings/StSettings.h>

#include "../StOutAnaglyph/StOutAnaglyph.h"
#include "../StOutDual/StOutDual.h"
#include "../StOutIZ3D/StOutIZ3D.h"
#include "../StOutInterlace/StOutInterlace.h"
#include "../StOutPageFlip/StOutPageFlipExt.h"
#include "../StOutDistorted/StOutDistorted.h"

#ifdef __ANDROID__
    #include <EGL/egl.h>
#endif

namespace {
    static const char ST_SETTING_FPSTARGET[] = "fpsTarget";
    static const char ST_SETTING_SHOW_FPS[]  = "toShowFps";
    static const StString ST_PARAM_NORMALS   = "showNormals";
    static const StString ST_PARAM_TRIHEDRON = "showTrihedron";
    static const StString ST_PARAM_PROJMODE  = "projMode";
    static const StString ST_PARAM_FILLMODE  = "fillMode";
}

const StString StCADViewer::ST_DRAWER_PLUGIN_NAME = "StCADViewer";

StCADViewer::StCADViewer(const StHandle<StResourceManager>& theResMgr,
                         const StNativeWin_t                theParentWin,
                         const StHandle<StOpenInfo>&        theOpenInfo)
: StApplication(theResMgr, theParentWin, theOpenInfo),
  myIsLeftHold(false),
  myIsRightHold(false),
  myIsMiddleHold(false),
  myIsCtrlPressed(false) {
    mySettings = new StSettings(myResMgr, ST_DRAWER_PLUGIN_NAME);

    myTitle = "sView - CAD Viewer";
    //
    params.isFullscreen = new StBoolParam(false);
    params.isFullscreen->signals.onChanged.connect(this, &StCADViewer::doFullscreen);
    params.ToShowFps = new StBoolParam(false);
    params.toShowTrihedron = new StBoolParam(true);
    params.projectMode = new StInt32Param(ST_PROJ_STEREO);
    params.projectMode->signals.onChanged.connect(this, &StCADViewer::doChangeProjection);
    params.fillMode = new StInt32Param(ST_FILL_MESH);
    params.TargetFps = 0;

    mySettings->loadInt32 (ST_SETTING_FPSTARGET, params.TargetFps);
    mySettings->loadParam (ST_SETTING_SHOW_FPS,  params.ToShowFps);

#if defined(__ANDROID__)
    addRenderer(new StOutInterlace  (myResMgr, theParentWin));
    addRenderer(new StOutAnaglyph   (myResMgr, theParentWin));
    addRenderer(new StOutDistorted  (myResMgr, theParentWin));
#else
    addRenderer(new StOutAnaglyph   (myResMgr, theParentWin));
    addRenderer(new StOutDual       (myResMgr, theParentWin));
    addRenderer(new StOutIZ3D       (myResMgr, theParentWin));
    addRenderer(new StOutInterlace  (myResMgr, theParentWin));
    addRenderer(new StOutDistorted  (myResMgr, theParentWin));
    addRenderer(new StOutPageFlipExt(myResMgr, theParentWin));
#endif
}

bool StCADViewer::resetDevice() {
    if(myGUI.isNull()
    || myCADLoader.isNull()) {
        return init();
    }

    // be sure Render plugin process quit correctly
    myWindow->beforeClose();

    releaseDevice();
    myWindow->close();
    myWindow.nullify();
    return open();
}

void StCADViewer::saveGuiParams() {
    if(myGUI.isNull()) {
        return;
    }

    mySettings->saveParam(ST_PARAM_TRIHEDRON,   params.toShowTrihedron);
    mySettings->saveParam(ST_PARAM_PROJMODE,    params.projectMode);
    mySettings->saveParam(ST_PARAM_FILLMODE,    params.fillMode);
    mySettings->saveInt32(ST_SETTING_FPSTARGET, params.TargetFps);
    mySettings->saveParam(ST_SETTING_SHOW_FPS,  params.ToShowFps);
}

void StCADViewer::saveAllParams() {
    saveGuiParams();
    mySettings->flush();
}

void StCADViewer::releaseDevice() {
    saveAllParams();

    // release GUI data and GL resources before closing the window
    myGUI.nullify();
    myContext.nullify();
    myAisContext.Nullify();
    myView.Nullify();
    myViewer.Nullify();
}

StCADViewer::~StCADViewer() {
    releaseDevice();
    // wait working threads to quit and release resources
    myCADLoader.nullify();
}

bool StCADViewer::initOcctViewer() {
    Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(StCADMsgPrinter));
    Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(Message_PrinterOStream));
    Handle(StCADMsgPrinter) aPrinter = new StCADMsgPrinter(myMsgQueue);
    Message::DefaultMessenger()->AddPrinter(aPrinter);

#ifdef __ANDROID__
    int aWidth = 2, aHeight = 2;
    EGLint aCfgId = 0;
    EGLDisplay anEglDisplay = eglGetCurrentDisplay();
    EGLContext anEglContext = eglGetCurrentContext();
    EGLSurface anEglSurf    = eglGetCurrentSurface(EGL_DRAW);
    if(anEglDisplay == EGL_NO_DISPLAY
    || anEglContext == EGL_NO_CONTEXT
    || anEglSurf    == EGL_NO_SURFACE) {
        myMsgQueue->pushError(stCString("Critical error:\nNo active EGL context!"));
        return false;
    }

    eglQuerySurface(anEglDisplay, anEglSurf, EGL_WIDTH,     &aWidth);
    eglQuerySurface(anEglDisplay, anEglSurf, EGL_HEIGHT,    &aHeight);
    eglQuerySurface(anEglDisplay, anEglSurf, EGL_CONFIG_ID, &aCfgId);

    const EGLint aConfigAttribs[] = { EGL_CONFIG_ID, aCfgId, EGL_NONE };
    EGLint       aNbConfigs = 0;
    void*        anEglConfig = NULL;

    if(eglChooseConfig(anEglDisplay, aConfigAttribs, &anEglConfig, 1, &aNbConfigs) != EGL_TRUE) {
        myMsgQueue->pushError(stCString("Critical error:\nEGL does not provide compatible configurations!"));
        return false;
    }

    if(!myViewer.IsNull()) {
        Handle(OpenGl_GraphicDriver) aDriver = Handle(OpenGl_GraphicDriver)::DownCast(myViewer->Driver());
        Handle(StCADWindow)          aWindow = Handle(StCADWindow)::DownCast(myView->Window());
        if(!aDriver->InitEglContext(anEglDisplay, anEglContext, anEglConfig)) {
            myMsgQueue->pushError(stCString("Critical error:\nOpenGl_GraphicDriver can not be initialized!"));
            return false;
        }

        aWindow->SetSize(aWidth, aHeight);
        myView->SetWindow(aWindow, (Aspect_RenderingContext )anEglContext);
        return true;
    }

#elif defined(_WIN32)
    HWND  aWinHandle = (HWND  )myWindow->getNativeOglWin();
    HDC   aWindowDC  = (HDC   )myWindow->getNativeOglDC();
    HGLRC aRendCtx   = (HGLRC )myWindow->getNativeOglRC();
    if(aWinHandle == NULL
    || aWindowDC  == NULL
    || aRendCtx   == NULL) {
        myMsgQueue->pushError(stCString("Critical error:\nNo active WGL context!"));
        return false;
    }

    if(!myViewer.IsNull()) {
        Handle(StCADWindow) aWindow = new StCADWindow(aWinHandle);
        myView->SetWindow(aWindow, (Aspect_RenderingContext )aRendCtx);
        return true;
    }
#endif

    Handle(OpenGl_GraphicDriver) aDriver = new OpenGl_GraphicDriver(NULL, Standard_False);
    aDriver->ChangeOptions().ffpEnable     = Standard_False;
    aDriver->ChangeOptions().buffersNoSwap = Standard_True;
#ifdef ST_DEBUG_GL
    aDriver->ChangeOptions().contextDebug  = Standard_True;
#else
    aDriver->ChangeOptions().contextDebug  = Standard_False;
#endif
#ifdef ST_DEBUG_SHADERS
    aDriver->ChangeOptions().glslWarnings  = Standard_True;
#else
    aDriver->ChangeOptions().glslWarnings  = Standard_False;
#endif

#ifdef __ANDROID__
    if(!aDriver->InitEglContext(anEglDisplay, anEglContext, anEglConfig)) {
        myMsgQueue->pushError(stCString("Critical error:\nOpenGl_GraphicDriver can not be initialized!!"));
        return false;
    }
#endif

    myViewer = new V3d_Viewer(aDriver, TCollection_ExtendedString("Viewer").ToExtString(), "", 1000.0,
                              V3d_XposYnegZpos, Quantity_NOC_BLACK, V3d_ZBUFFER, V3d_GOURAUD, V3d_WAIT,
                              Standard_True, Standard_False);
    Handle(V3d_DirectionalLight) aLightDir = new V3d_DirectionalLight(myViewer, V3d_Zneg, Quantity_NOC_WHITE, Standard_True);
    Handle(V3d_AmbientLight)     aLightAmb = new V3d_AmbientLight(myViewer);
    aLightDir->SetDirection ( 1.0, -2.0, -10.0);
    myViewer->SetLightOn (aLightDir);
    myViewer->SetLightOn (aLightAmb);

    myAisContext = new AIS_InteractiveContext(myViewer);
    myAisContext->SetDisplayMode(AIS_Shaded);
    myAisContext->SetAutoActivateSelection(Standard_False);
    myAisContext->SetHilightColor(Quantity_NOC_CYAN1);
    myAisContext->SelectionColor (Quantity_NOC_WHITE);
    const Handle(Prs3d_Drawer)& aDrawer = myAisContext->DefaultDrawer();
    aDrawer->SetAutoTriangulation (Standard_False);
#ifdef __ANDROID__
    Handle(StCADWindow) aWindow = new StCADWindow();
    aWindow->SetSize(aWidth, aHeight);
#elif defined(_WIN32)
    Handle(StCADWindow) aWindow = new StCADWindow(aWinHandle);
#endif

    myView = myViewer->CreateView();
    myView->Camera()->SetProjectionType(myProjection.isPerspective()
                                      ? Graphic3d_Camera::Projection_Perspective
                                      : Graphic3d_Camera::Projection_Orthographic);
    myView->SetImmediateUpdate(Standard_False);

#ifdef __ANDROID__
    myView->SetWindow(aWindow, (Aspect_RenderingContext )anEglContext);
#else
    myView->SetWindow(aWindow, (Aspect_RenderingContext )aRendCtx);
#endif

    myView->TriedronDisplay(Aspect_TOTP_RIGHT_LOWER, Quantity_NOC_WHITE, 0.08, V3d_ZBUFFER);
    myView->SetSurfaceDetail(V3d_TEX_ALL);
    return true;
}

bool StCADViewer::createGui() {
    if(!myGUI.isNull()) {
        saveGuiParams();
        myGUI.nullify();
        myKeyActions.clear();
    }

    // create the GUI with default values
    //params.ScaleHiDPI->setValue(myWindow->getScaleFactor());
    myGUI = new StCADViewerGUI(this);
    myGUI->setContext(myContext);

    // load settings
    myWindow->setTargetFps(double(params.TargetFps));
    mySettings->loadParam(ST_PARAM_TRIHEDRON, params.toShowTrihedron);
    mySettings->loadParam(ST_PARAM_PROJMODE,  params.projectMode);
    mySettings->loadParam(ST_PARAM_FILLMODE,  params.fillMode);

    myGUI->stglInit();
    myGUI->stglResize(myWindow->stglViewport(ST_WIN_MASTER));

    registerHotKeys();
    return true;
}

bool StCADViewer::init() {
    const bool isReset = !myCADLoader.isNull();
    if(!myContext.isNull()
    && !myGUI.isNull()) {
        return true;
    }

    // initialize GL context
    myContext = myWindow->getContext();
    myContext->setMessagesQueue(myMsgQueue);
    if(!myContext->isGlGreaterEqual(2, 0)) {
        myMsgQueue->pushError(stCString("OpenGL 2.0 is required by CAD Viewer!"));
        myMsgQueue->popAll();
        return false;
    }

    myWindow->setTargetFps(double(params.TargetFps));
    myWindow->setStereoOutput(params.projectMode->getValue() == ST_PROJ_STEREO);

    if(!initOcctViewer()) {
        //
    }

    // load hot-keys
    if(!isReset) {
        for(std::map< int, StHandle<StAction> >::iterator anIter = myActions.begin();
            anIter != myActions.end(); ++anIter) {
            mySettings->loadHotKey(anIter->second);
        }
    }

    // create the GUI with default values
    if(!createGui()) {
        myMsgQueue->pushError(stCString("CAD Viewer - GUI initialization failed!"));
        myMsgQueue->popAll();
        myGUI.nullify();
        return false;
    }

    myGUI->stglResize(myWindow->stglViewport(ST_WIN_MASTER));

    // create working threads
    if(!isReset) {
        myCADLoader = new StCADLoader(StHandle<StLangMap>::downcast(myGUI->myLangMap));
        myCADLoader->signals.onError = stSlot(myMsgQueue.access(), &StMsgQueue::doPushError);
    }
    return true;
}

bool StCADViewer::open() {
    const bool isReset = !mySwitchTo.isNull();
    if(!StApplication::open()
    || !init()) {
        myMsgQueue->popAll();
        return false;
    }

    if(isReset) {
        myCADLoader->doLoadNext();
        return true;
    }

    //parseArguments(myOpenFileInfo.getArgumentsMap());
    const StMIME anOpenMIME = myOpenFileInfo->getMIME();
    if(myOpenFileInfo->getPath().isEmpty()) {
        // open drawer without files
        return true;
    }

    // clear playlist first
    myCADLoader->getPlayList().clear();

    if(!anOpenMIME.isEmpty()) {
        // create just one-file playlist
        myCADLoader->getPlayList().addOneFile(myOpenFileInfo->getPath(), anOpenMIME);
    } else {
        // create playlist from file's folder
        myCADLoader->getPlayList().open(myOpenFileInfo->getPath());
    }

    if(!myCADLoader->getPlayList().isEmpty()) {
        doUpdateStateLoading();
        myCADLoader->doLoadNext();
    }

    return true;
}

void StCADViewer::doPause(const StPauseEvent& theEvent) {
    StApplication::doPause(theEvent);
    saveAllParams();
}

void StCADViewer::doResize(const StSizeEvent& ) {
    if(myGUI.isNull()) {
        return;
    }

    const StGLBoxPx aWinRect = myWindow->stglViewport(ST_WIN_MASTER);
    myGUI->stglResize(aWinRect);
    myProjection.resize(*myContext, aWinRect.width(), aWinRect.height());
}

void StCADViewer::doMouseDown(const StClickEvent& theEvent) {
    if(myGUI.isNull()) {
        return;
    }

    myGUI->tryClick(theEvent);

    if(theEvent.Button == ST_MOUSE_LEFT) {
        myIsLeftHold = true; ///
        myPrevMouse.x() = theEvent.PointX;
        myPrevMouse.y() = theEvent.PointY;
    } else if(theEvent.Button == ST_MOUSE_RIGHT) {
        myIsRightHold = true; ///
        myPrevMouse.x() = theEvent.PointX;
        myPrevMouse.y() = theEvent.PointY;
        if(myIsCtrlPressed && !myView.IsNull()) {
            StRectI_t aWinRect = myWindow->getPlacement();
            myView->StartRotation(int(double(aWinRect.width())  * theEvent.PointX),
                                  int(double(aWinRect.height()) * theEvent.PointY));
        }
    } else if(theEvent.Button == ST_MOUSE_MIDDLE) {
        myIsMiddleHold = true; ///
        myPrevMouse.x() = theEvent.PointX;
        myPrevMouse.y() = theEvent.PointY;
    }
}

void StCADViewer::doMouseUp(const StClickEvent& theEvent) {
    if(myGUI.isNull()) {
        return;
    }

    switch(theEvent.Button) {
        case ST_MOUSE_LEFT: {
            myIsLeftHold = false;
            break;
        }
        case ST_MOUSE_RIGHT: {
            myIsRightHold = false;
            break;
        }
        case ST_MOUSE_MIDDLE: {
            if(!myIsCtrlPressed) {
                params.isFullscreen->reverse();
            }
            myIsMiddleHold = false;
            break;
        }
        default: break;
    }
    myGUI->tryUnClick(theEvent);
}

void StCADViewer::doScroll(const StScrollEvent& theEvent) {
    if(myGUI.isNull()) {
        return;
    }

    if(theEvent.StepsY >= 1) {
        if(myIsCtrlPressed) {
            if(!myView.IsNull()) {
                Standard_Real aFocus = myView->Camera()->ZFocus() + 0.05;
                if(aFocus > 0.2
                && aFocus < 2.0) {
                  myView->Camera()->SetZFocus(myView->Camera()->ZFocusType(), aFocus);
                }
            }
            myProjection.setZScreen(myProjection.getZScreen() + 1.1f);
        } else {
            if(!myView.IsNull()) {
                myView->Zoom(0, 0, 10, 10);
            }
            myProjection.setZoom(myProjection.getZoom() * 0.9f);
        }
    } else if(theEvent.StepsY <= -1) {
        if(myIsCtrlPressed) {
            if(!myView.IsNull()) {
                Standard_Real aFocus = myView->Camera()->ZFocus() - 0.05;
                if(aFocus > 0.2
                && aFocus < 2.0) {
                  myView->Camera()->SetZFocus(myView->Camera()->ZFocusType(), aFocus);
                }
            }
            myProjection.setZScreen(myProjection.getZScreen() - 1.1f);
        } else {
            if(!myView.IsNull()) {
                myView->Zoom(0, 0, -10, -10);
            }
            myProjection.setZoom(myProjection.getZoom() * 1.1f);
        }
    }

    myGUI->doScroll(theEvent);
}

void StCADViewer::doKeyDown(const StKeyEvent& theEvent) {
    if(myGUI.isNull()) {
        return;
    }

    if(myGUI->getFocus() != NULL) {
        myGUI->doKeyDown(theEvent);
        return;
    }

    switch(theEvent.VKey) {
        case ST_VK_ESCAPE:
            StApplication::exit(0);
            return;
        case ST_VK_RETURN:
            params.isFullscreen->reverse();
            return;

        // switch projection matrix
        case ST_VK_M:
            params.projectMode->setValue(ST_PROJ_PERSP);
            return;
        case ST_VK_S:
            params.projectMode->setValue(ST_PROJ_STEREO);
            return;
        case ST_VK_O:
            params.projectMode->setValue(ST_PROJ_ORTHO);
            return;

        // separation
        case ST_VK_MULTIPLY: {
            if(theEvent.Flags == ST_VF_NONE) {
                myProjection.setIOD(myProjection.getIOD() + 0.1f);
                ST_DEBUG_LOG("Sep. inc, " + myProjection.toString());
            }
            return;
        }
        case ST_VK_DIVIDE:
            if(theEvent.Flags == ST_VF_NONE) {
                myProjection.setIOD(myProjection.getIOD() - 0.1f);
                ST_DEBUG_LOG("Sep. dec, " + myProjection.toString());
            }
            return;

        case ST_VK_LEFT:
            ///myCam.rotateX(-1.0f);
            return;
        case ST_VK_RIGHT:
            ///myCam.rotateX(1.0f);
            return;
        case ST_VK_UP:
            ///myCam.rotateY(-1.0f);
            return;
        case ST_VK_DOWN:
            ///myCam.rotateY(1.0f);
            return;
        case ST_VK_Q:
            ///myCam.rotateZ(-1.0f);
            return;
        case ST_VK_W:
            ///myCam.rotateZ(1.0f);
            return;

        // call fit all
        case ST_VK_F:
            doFitALL();
            return;

        // playlist navigation
        case ST_VK_PRIOR:
            doListPrev();
            return;
        case ST_VK_NEXT:
            doListNext();
            return;
        case ST_VK_HOME:
            doListFirst();
            return;
        case ST_VK_END:
            doListLast();
            return;

        // shading mode
        case ST_VK_1:
            params.fillMode->setValue(ST_FILL_MESH);
            return;
        case ST_VK_2:
            params.fillMode->setValue(ST_FILL_SHADING);
            return;
        case ST_VK_3:
            params.fillMode->setValue(ST_FILL_SHADED_MESH);
            return;

        default:
            break;
    }
}

void StCADViewer::doKeyHold(const StKeyEvent& theEvent) {
    if(!myGUI.isNull()
    && myGUI->getFocus() != NULL) {
        myGUI->doKeyHold(theEvent);
    }
}

void StCADViewer::doKeyUp(const StKeyEvent& theEvent) {
    if(!myGUI.isNull()
    && myGUI->getFocus() != NULL) {
        myGUI->doKeyUp(theEvent);
    }
}

void StCADViewer::doFileDrop(const StDNDropEvent& theEvent) {
    if(theEvent.NbFiles == 0) {
        return;
    }

    const StString aFilePath = theEvent.Files[0];
    if(myCADLoader->getPlayList().checkExtension(aFilePath)) {
        myCADLoader->getPlayList().open(aFilePath);
        doUpdateStateLoading();
        myCADLoader->doLoadNext();
    }
}

void StCADViewer::doNavigate(const StNavigEvent& theEvent) {
    switch(theEvent.Target) {
        case stNavigate_Backward: doListPrev(); break;
        case stNavigate_Forward:  doListNext(); break;
        default: break;
    }
}

void StCADViewer::beforeDraw() {
    if(myGUI.isNull()) {
        return;
    }

    myIsCtrlPressed = myWindow->getKeysState().isKeyDown(ST_VK_CONTROL);
    Handle(Graphic3d_Camera) aCam = !myView.IsNull()
                                  ?  myView->Camera()
                                  : Handle(Graphic3d_Camera)();
    if(myIsMiddleHold && myIsCtrlPressed && !aCam.IsNull()) {
        // move
        StPointD_t aPt = myWindow->getMousePos();
        gp_Vec2d aFlatMove( 2.0 * (aPt.x() - myPrevMouse.x()),
                           -2.0 * (aPt.y() - myPrevMouse.y()));

        const gp_Dir aSide    = aCam->Direction().Crossed(aCam->Up());
        const gp_Pnt aViewDim = aCam->ViewDimensions();
        const gp_Vec aMoveSide = gp_Vec(aSide)      * 0.5 * aFlatMove.X() * aViewDim.X();
        const gp_Vec aMoveUp   = gp_Vec(aCam->Up()) * 0.5 * aFlatMove.Y() * aViewDim.Y();

        gp_Pnt aCenter = aCam->Center();
        gp_Pnt anEye   = aCam->Eye();
        aCenter.Translate(-aMoveSide);
        anEye  .Translate(-aMoveSide);
        aCenter.Translate(-aMoveUp);
        anEye  .Translate(-aMoveUp);

        aCam->SetCenter(aCenter);
        aCam->SetEye(anEye);

        myPrevMouse = aPt;
    }
    if(myIsRightHold && myIsCtrlPressed) {
        const StPointD_t aPt = myWindow->getMousePos();
        StRectI_t aWinRect = myWindow->getPlacement();
        myView->Rotation(int(double(aWinRect.width())  * aPt.x()),
                         int(double(aWinRect.height()) * aPt.y()));
    }

    if(!myAisContext.IsNull()) {
        NCollection_Sequence<Handle(AIS_InteractiveObject)> aNewPrsList;
        if(myCADLoader->getNextShape(aNewPrsList)) {
            myAisContext->RemoveAll(Standard_False);
            for(NCollection_Sequence<Handle(AIS_InteractiveObject)>::Iterator aPrsIter(aNewPrsList); aPrsIter.More(); aPrsIter.Next()) {
                myAisContext->Display(aPrsIter.Value(), 1, 0, Standard_False);
            }

            doFitALL();
            doUpdateStateLoaded(!aNewPrsList.IsEmpty());
        }
    }

    myGUI->setVisibility(myWindow->getMousePos(), true);
    myGUI->stglUpdate(myWindow->getMousePos());
}

void StCADViewer::stglDraw(unsigned int theView) {
    if(myGUI.isNull()) {
        return;
    }

    if(!myView.IsNull()) {
        Graphic3d_Camera::Projection aProj = Graphic3d_Camera::Projection_Orthographic;
        if(myProjection.isPerspective()) {
            switch(theView) {
                case ST_DRAW_LEFT:  aProj = Graphic3d_Camera::Projection_MonoLeftEye;  break;
                case ST_DRAW_RIGHT: aProj = Graphic3d_Camera::Projection_MonoRightEye; break;
                case ST_DRAW_MONO:  aProj = Graphic3d_Camera::Projection_Perspective;  break;
            }
        }

        // Do the magic:
        // - define default FBO for OCCT from StGLContext
        // - resize virtual window without OCCT viewer redraw
        // - copy viewport restore it back
        // What does not handled:
        // - Dual Output, OCCT makes OpenGL context always on master window
        // - scissor test is likely incorrectly applied
        // - MSAA blitting might use incorrect viewport
        Handle(OpenGl_GraphicDriver) aDriver = Handle(OpenGl_GraphicDriver)::DownCast(myViewer->Driver());
        Handle(OpenGl_Context) aCtx = aDriver->GetSharedContext();
        Handle(StCADFrameBuffer) anFboWrapper = Handle(StCADFrameBuffer)::DownCast(aCtx->DefaultFrameBuffer());
        if(anFboWrapper.IsNull()) {
            anFboWrapper = new StCADFrameBuffer();
        }
        anFboWrapper->wrapFbo(*myContext);
        aCtx->SetDefaultFrameBuffer(anFboWrapper);
        myContext->stglBindFramebuffer(0);

        Handle(StCADWindow) aWindow = Handle(StCADWindow)::DownCast(myView->Window());
        if(anFboWrapper->GetVPSizeX() > 0
        && anFboWrapper->GetVPSizeY() > 0
        && aWindow->SetSize(anFboWrapper->GetVPSizeX(), anFboWrapper->GetVPSizeY())) {
            Standard_Real aRatio = double(anFboWrapper->GetVPSizeX()) / double(anFboWrapper->GetVPSizeY());
            myView->Camera()->SetAspect(aRatio);
            myView->View()->Resized();
        }

        StGLBoxPx aVPort = myContext->stglViewport();
        StGLBoxPx aScissRect;
        bool toSetScissorRect = myContext->stglScissorRect(aScissRect);

        myView->Camera()->SetProjectionType(aProj);
        myView->Redraw();

        myContext->stglResizeViewport(aVPort);
        if(toSetScissorRect) {
            myContext->stglSetScissorRect(aScissRect, false);
        }
    } else if(!myContext.isNull()
            && myContext->core20fwd != NULL) {
        // clear the screen and the depth buffer
        myContext->core11fwd->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    myGUI->getCamera()->setView(theView);
    myProjection.setView(theView);

    // draw GUI
    myContext->core11fwd->glDisable(GL_DEPTH_TEST);
    myGUI->stglDraw(theView);
}

void StCADViewer::doUpdateStateLoading() {
    const StString aFileToLoad = myCADLoader->getPlayList().getCurrentTitle();
    if(aFileToLoad.isEmpty()) {
        myWindow->setTitle("sView - CAD Viewer");
    } else {
        myWindow->setTitle(aFileToLoad + " Loading... - sView");
    }
}

void StCADViewer::doUpdateStateLoaded(bool isSuccess) {
    const StString aFileLoaded = myCADLoader->getPlayList().getCurrentTitle();
    if(aFileLoaded.isEmpty()) {
        myWindow->setTitle("sView - CAD Viewer");
    } else {
        myWindow->setTitle(aFileLoaded + (isSuccess ? StString() : StString(" FAIL to open")) + " - sView");
    }
}

void StCADViewer::doFullscreen(const bool theIsFullscreen) {
    if(!myWindow.isNull()) {
        myWindow->setFullScreen(theIsFullscreen);
    }
}

void StCADViewer::doListFirst(const size_t ) {
    if(myCADLoader->getPlayList().walkToFirst()) {
        myCADLoader->doLoadNext();
        doUpdateStateLoading();
    }
}

void StCADViewer::doListPrev(const size_t ) {
    if(myCADLoader->getPlayList().walkToPrev()) {
        myCADLoader->doLoadNext();
        doUpdateStateLoading();
    }
}

void StCADViewer::doListNext(const size_t ) {
    if(myCADLoader->getPlayList().walkToNext()) {
        myCADLoader->doLoadNext();
        doUpdateStateLoading();
    }
}

void StCADViewer::doListLast(const size_t ) {
    if(myCADLoader->getPlayList().walkToLast()) {
        myCADLoader->doLoadNext();
        doUpdateStateLoading();
    }
}

void StCADViewer::doFitALL(const size_t ) {
    if(!myView.IsNull()) {
        myView->FitAll(0.01, Standard_False);
    }
}

void StCADViewer::doChangeProjection(const int32_t theProj) {
    if(myWindow.isNull()) {
        return;
    }

    switch(theProj) {
        case ST_PROJ_ORTHO: {
            myWindow->setStereoOutput(false);
            myProjection.setPerspective(false);
            if(!myView.IsNull()) {
                myView->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
            }
            break;
        }
        case ST_PROJ_PERSP: {
            myWindow->setStereoOutput(false);
            myProjection.setPerspective(true);
            if(!myView.IsNull()) {
                myView->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Perspective);
            }
            break;
        }
        case ST_PROJ_STEREO: {
            myWindow->setStereoOutput(true);
            myProjection.setPerspective(true);
            if(!myView.IsNull()) {
                myView->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Perspective);
            }
            break;
        }
    }
}
