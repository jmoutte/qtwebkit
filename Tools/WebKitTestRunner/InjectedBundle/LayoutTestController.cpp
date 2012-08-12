/*
 * Copyright (C) 2010, 2011, 2012 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "LayoutTestController.h"

#include "InjectedBundle.h"
#include "InjectedBundlePage.h"
#include "JSLayoutTestController.h"
#include "PlatformWebView.h"
#include "StringFunctions.h"
#include "TestController.h"
#include <WebCore/PageVisibilityState.h>
#include <WebKit2/WKBundleBackForwardList.h>
#include <WebKit2/WKBundleFrame.h>
#include <WebKit2/WKBundleFramePrivate.h>
#include <WebKit2/WKBundleInspector.h>
#include <WebKit2/WKBundleNodeHandlePrivate.h>
#include <WebKit2/WKBundlePagePrivate.h>
#include <WebKit2/WKBundlePrivate.h>
#include <WebKit2/WKBundleScriptWorld.h>
#include <WebKit2/WKRetainPtr.h>
#include <WebKit2/WKSerializedScriptValue.h>
#include <WebKit2/WebKit2_C.h>
#include <wtf/CurrentTime.h>
#include <wtf/HashMap.h>
#include <wtf/text/StringBuilder.h>

#if ENABLE(WEB_INTENTS)
#include <WebKit2/WKBundleIntentRequest.h>
#include <WebKit2/WKIntentData.h>
#endif

namespace WTR {

// This is lower than DumpRenderTree's timeout, to make it easier to work through the failures
// Eventually it should be changed to match.
const double LayoutTestController::waitToDumpWatchdogTimerInterval = 6;

PassRefPtr<LayoutTestController> LayoutTestController::create()
{
    return adoptRef(new LayoutTestController);
}

LayoutTestController::LayoutTestController()
    : m_whatToDump(RenderTree)
    , m_shouldDumpAllFrameScrollPositions(false)
    , m_shouldDumpBackForwardListsForAllWindows(false)
    , m_shouldAllowEditing(true)
    , m_shouldCloseExtraWindows(false)
    , m_dumpEditingCallbacks(false)
    , m_dumpStatusCallbacks(false)
    , m_dumpTitleChanges(false)
    , m_dumpPixels(true)
    , m_dumpFullScreenCallbacks(false)
    , m_dumpFrameLoadCallbacks(false)
    , m_dumpProgressFinishedCallback(false)
    , m_dumpResourceResponseMIMETypes(false)
    , m_waitToDump(false)
    , m_testRepaint(false)
    , m_testRepaintSweepHorizontally(false)
    , m_willSendRequestReturnsNull(false)
    , m_policyDelegateEnabled(false)
    , m_policyDelegatePermissive(false)
    , m_globalFlag(false)
    , m_customFullScreenBehavior(false)
    , m_userStyleSheetEnabled(false)
    , m_userStyleSheetLocation(adoptWK(WKStringCreateWithUTF8CString("")))
{
    platformInitialize();
}

LayoutTestController::~LayoutTestController()
{
}

JSClassRef LayoutTestController::wrapperClass()
{
    return JSLayoutTestController::layoutTestControllerClass();
}

void LayoutTestController::display()
{
    WKBundlePageRef page = InjectedBundle::shared().page()->page();
    WKBundlePageForceRepaint(page);
    WKBundlePageSetTracksRepaints(page, true);
    WKBundlePageResetTrackedRepaints(page);
}

void LayoutTestController::dumpAsText(bool dumpPixels)
{
    if (m_whatToDump < MainFrameText)
        m_whatToDump = MainFrameText;
    m_dumpPixels = dumpPixels;
}

// FIXME: Needs a full implementation see https://bugs.webkit.org/show_bug.cgi?id=42546
void LayoutTestController::setCustomPolicyDelegate(bool enabled, bool permissive)
{
    m_policyDelegateEnabled = enabled;
    m_policyDelegatePermissive = permissive;
}

void LayoutTestController::waitForPolicyDelegate()
{
    setCustomPolicyDelegate(true);
    waitUntilDone();
}

void LayoutTestController::waitUntilDone()
{
    m_waitToDump = true;
    if (InjectedBundle::shared().useWaitToDumpWatchdogTimer())
        initializeWaitToDumpWatchdogTimerIfNeeded();
}

void LayoutTestController::waitToDumpWatchdogTimerFired()
{
    invalidateWaitToDumpWatchdogTimer();
    const char* message = "FAIL: Timed out waiting for notifyDone to be called\n";
    InjectedBundle::shared().stringBuilder()->append(message);
    InjectedBundle::shared().stringBuilder()->append("\n");
    InjectedBundle::shared().done();
}

void LayoutTestController::notifyDone()
{
    if (!InjectedBundle::shared().isTestRunning())
        return;

    if (m_waitToDump && !InjectedBundle::shared().topLoadingFrame())
        InjectedBundle::shared().page()->dump();

    m_waitToDump = false;
}

unsigned LayoutTestController::numberOfActiveAnimations() const
{
    // FIXME: Is it OK this works only for the main frame?
    // FIXME: If this is needed only for the main frame, then why is the function on WKBundleFrame instead of WKBundlePage?
    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    return WKBundleFrameGetNumberOfActiveAnimations(mainFrame);
}

bool LayoutTestController::pauseAnimationAtTimeOnElementWithId(JSStringRef animationName, double time, JSStringRef elementId)
{
    // FIXME: Is it OK this works only for the main frame?
    // FIXME: If this is needed only for the main frame, then why is the function on WKBundleFrame instead of WKBundlePage?
    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    return WKBundleFramePauseAnimationOnElementWithId(mainFrame, toWK(animationName).get(), toWK(elementId).get(), time);
}

bool LayoutTestController::pauseTransitionAtTimeOnElementWithId(JSStringRef propertyName, double time, JSStringRef elementId)
{
    // FIXME: Is it OK this works only for the main frame?
    // FIXME: If this is needed only for the main frame, then why is the function on WKBundleFrame instead of WKBundlePage?
    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    return WKBundleFramePauseTransitionOnElementWithId(mainFrame, toWK(propertyName).get(), toWK(elementId).get(), time);
}

void LayoutTestController::suspendAnimations()
{
    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    WKBundleFrameSuspendAnimations(mainFrame);
}

JSRetainPtr<JSStringRef> LayoutTestController::layerTreeAsText() const
{
    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    WKRetainPtr<WKStringRef> text(AdoptWK, WKBundleFrameCopyLayerTreeAsText(mainFrame));
    return toJS(text);
}

void LayoutTestController::addUserScript(JSStringRef source, bool runAtStart, bool allFrames)
{
    WKRetainPtr<WKStringRef> sourceWK = toWK(source);
    WKRetainPtr<WKBundleScriptWorldRef> scriptWorld(AdoptWK, WKBundleScriptWorldCreateWorld());

    WKBundleAddUserScript(InjectedBundle::shared().bundle(), InjectedBundle::shared().pageGroup(), scriptWorld.get(), sourceWK.get(), 0, 0, 0,
        (runAtStart ? kWKInjectAtDocumentStart : kWKInjectAtDocumentEnd),
        (allFrames ? kWKInjectInAllFrames : kWKInjectInTopFrameOnly));
}

void LayoutTestController::addUserStyleSheet(JSStringRef source, bool allFrames)
{
    WKRetainPtr<WKStringRef> sourceWK = toWK(source);
    WKRetainPtr<WKBundleScriptWorldRef> scriptWorld(AdoptWK, WKBundleScriptWorldCreateWorld());

    WKBundleAddUserStyleSheet(InjectedBundle::shared().bundle(), InjectedBundle::shared().pageGroup(), scriptWorld.get(), sourceWK.get(), 0, 0, 0,
        (allFrames ? kWKInjectInAllFrames : kWKInjectInTopFrameOnly));
}

void LayoutTestController::keepWebHistory()
{
    WKBundleSetShouldTrackVisitedLinks(InjectedBundle::shared().bundle(), true);
}

JSValueRef LayoutTestController::computedStyleIncludingVisitedInfo(JSValueRef element)
{
    // FIXME: Is it OK this works only for the main frame?
    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    JSContextRef context = WKBundleFrameGetJavaScriptContext(mainFrame);
    if (!JSValueIsObject(context, element))
        return JSValueMakeUndefined(context);
    JSValueRef value = WKBundleFrameGetComputedStyleIncludingVisitedInfo(mainFrame, const_cast<JSObjectRef>(element));
    if (!value)
        return JSValueMakeUndefined(context);
    return value;
}

JSRetainPtr<JSStringRef> LayoutTestController::markerTextForListItem(JSValueRef element)
{
    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    JSContextRef context = WKBundleFrameGetJavaScriptContext(mainFrame);
    if (!element || !JSValueIsObject(context, element))
        return 0;
    WKRetainPtr<WKStringRef> text(AdoptWK, WKBundleFrameCopyMarkerText(mainFrame, const_cast<JSObjectRef>(element)));
    if (WKStringIsEmpty(text.get()))
        return 0;
    return toJS(text);
}

void LayoutTestController::execCommand(JSStringRef name, JSStringRef argument)
{
    WKBundlePageExecuteEditingCommand(InjectedBundle::shared().page()->page(), toWK(name).get(), toWK(argument).get());
}

bool LayoutTestController::findString(JSStringRef target, JSValueRef optionsArrayAsValue)
{
    WKFindOptions options = 0;

    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    JSContextRef context = WKBundleFrameGetJavaScriptContext(mainFrame);
    JSRetainPtr<JSStringRef> lengthPropertyName(Adopt, JSStringCreateWithUTF8CString("length"));
    JSObjectRef optionsArray = JSValueToObject(context, optionsArrayAsValue, 0);
    JSValueRef lengthValue = JSObjectGetProperty(context, optionsArray, lengthPropertyName.get(), 0);
    if (!JSValueIsNumber(context, lengthValue))
        return false;

    size_t length = static_cast<size_t>(JSValueToNumber(context, lengthValue, 0));
    for (size_t i = 0; i < length; ++i) {
        JSValueRef value = JSObjectGetPropertyAtIndex(context, optionsArray, i, 0);
        if (!JSValueIsString(context, value))
            continue;

        JSRetainPtr<JSStringRef> optionName(Adopt, JSValueToStringCopy(context, value, 0));

        if (JSStringIsEqualToUTF8CString(optionName.get(), "CaseInsensitive"))
            options |= kWKFindOptionsCaseInsensitive;
        else if (JSStringIsEqualToUTF8CString(optionName.get(), "AtWordStarts"))
            options |= kWKFindOptionsAtWordStarts;
        else if (JSStringIsEqualToUTF8CString(optionName.get(), "TreatMedialCapitalAsWordStart"))
            options |= kWKFindOptionsTreatMedialCapitalAsWordStart;
        else if (JSStringIsEqualToUTF8CString(optionName.get(), "Backwards"))
            options |= kWKFindOptionsBackwards;
        else if (JSStringIsEqualToUTF8CString(optionName.get(), "WrapAround"))
            options |= kWKFindOptionsWrapAround;
        else if (JSStringIsEqualToUTF8CString(optionName.get(), "StartInSelection")) {
            // FIXME: No kWKFindOptionsStartInSelection.
        }
    }

    return WKBundlePageFindString(InjectedBundle::shared().page()->page(), toWK(target).get(), options);
}

void LayoutTestController::clearAllDatabases()
{
    WKBundleClearAllDatabases(InjectedBundle::shared().bundle());
}

void LayoutTestController::setDatabaseQuota(uint64_t quota)
{
    return WKBundleSetDatabaseQuota(InjectedBundle::shared().bundle(), quota);
}

void LayoutTestController::clearAllApplicationCaches()
{
    WKBundleClearApplicationCache(InjectedBundle::shared().bundle());
}

void LayoutTestController::setAppCacheMaximumSize(uint64_t size)
{
    WKBundleSetAppCacheMaximumSize(InjectedBundle::shared().bundle(), size);
}

bool LayoutTestController::isCommandEnabled(JSStringRef name)
{
    return WKBundlePageIsEditingCommandEnabled(InjectedBundle::shared().page()->page(), toWK(name).get());
}

void LayoutTestController::setCanOpenWindows(bool)
{
    // It's not clear if or why any tests require opening windows be forbidden.
    // For now, just ignore this setting, and if we find later it's needed we can add it.
}

void LayoutTestController::setXSSAuditorEnabled(bool enabled)
{
    WKRetainPtr<WKStringRef> key(AdoptWK, WKStringCreateWithUTF8CString("WebKitXSSAuditorEnabled"));
    WKBundleOverrideBoolPreferenceForTestRunner(InjectedBundle::shared().bundle(), InjectedBundle::shared().pageGroup(), key.get(), enabled);
}

void LayoutTestController::setAllowUniversalAccessFromFileURLs(bool enabled)
{
    WKBundleSetAllowUniversalAccessFromFileURLs(InjectedBundle::shared().bundle(), InjectedBundle::shared().pageGroup(), enabled);
}

void LayoutTestController::setAllowFileAccessFromFileURLs(bool enabled)
{
    WKBundleSetAllowFileAccessFromFileURLs(InjectedBundle::shared().bundle(), InjectedBundle::shared().pageGroup(), enabled);
}

void LayoutTestController::setFrameFlatteningEnabled(bool enabled)
{
    WKBundleSetFrameFlatteningEnabled(InjectedBundle::shared().bundle(), InjectedBundle::shared().pageGroup(), enabled);
}

void LayoutTestController::setPluginsEnabled(bool enabled)
{
    WKBundleSetPluginsEnabled(InjectedBundle::shared().bundle(), InjectedBundle::shared().pageGroup(), enabled);
}

void LayoutTestController::setGeolocationPermission(bool enabled)
{
    WKBundleSetGeolocationPermission(InjectedBundle::shared().bundle(), InjectedBundle::shared().pageGroup(), enabled);
}

void LayoutTestController::setJavaScriptCanAccessClipboard(bool enabled)
{
     WKBundleSetJavaScriptCanAccessClipboard(InjectedBundle::shared().bundle(), InjectedBundle::shared().pageGroup(), enabled);
}

void LayoutTestController::setPrivateBrowsingEnabled(bool enabled)
{
     WKBundleSetPrivateBrowsingEnabled(InjectedBundle::shared().bundle(), InjectedBundle::shared().pageGroup(), enabled);
}

void LayoutTestController::setPopupBlockingEnabled(bool enabled)
{
     WKBundleSetPopupBlockingEnabled(InjectedBundle::shared().bundle(), InjectedBundle::shared().pageGroup(), enabled);
}

void LayoutTestController::setAuthorAndUserStylesEnabled(bool enabled)
{
     WKBundleSetAuthorAndUserStylesEnabled(InjectedBundle::shared().bundle(), InjectedBundle::shared().pageGroup(), enabled);
}

void LayoutTestController::addOriginAccessWhitelistEntry(JSStringRef sourceOrigin, JSStringRef destinationProtocol, JSStringRef destinationHost, bool allowDestinationSubdomains)
{
    WKBundleAddOriginAccessWhitelistEntry(InjectedBundle::shared().bundle(), toWK(sourceOrigin).get(), toWK(destinationProtocol).get(), toWK(destinationHost).get(), allowDestinationSubdomains);
}

void LayoutTestController::removeOriginAccessWhitelistEntry(JSStringRef sourceOrigin, JSStringRef destinationProtocol, JSStringRef destinationHost, bool allowDestinationSubdomains)
{
    WKBundleRemoveOriginAccessWhitelistEntry(InjectedBundle::shared().bundle(), toWK(sourceOrigin).get(), toWK(destinationProtocol).get(), toWK(destinationHost).get(), allowDestinationSubdomains);
}

int LayoutTestController::numberOfPages(double pageWidthInPixels, double pageHeightInPixels)
{
    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    return WKBundleNumberOfPages(InjectedBundle::shared().bundle(), mainFrame, pageWidthInPixels, pageHeightInPixels);
}

JSRetainPtr<JSStringRef> LayoutTestController::pageSizeAndMarginsInPixels(int pageIndex, int width, int height, int marginTop, int marginRight, int marginBottom, int marginLeft)
{
    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    return toJS(WKBundlePageSizeAndMarginsInPixels(InjectedBundle::shared().bundle(), mainFrame, pageIndex, width, height, marginTop, marginRight, marginBottom, marginLeft));
}

bool LayoutTestController::isPageBoxVisible(int pageIndex)
{
    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    return WKBundleIsPageBoxVisible(InjectedBundle::shared().bundle(), mainFrame, pageIndex);
}

void LayoutTestController::setValueForUser(JSContextRef context, JSValueRef element, JSStringRef value)
{
    if (!element || !JSValueIsObject(context, element))
        return;

    WKRetainPtr<WKBundleNodeHandleRef> nodeHandle(AdoptWK, WKBundleNodeHandleCreate(context, const_cast<JSObjectRef>(element)));
    WKBundleNodeHandleSetHTMLInputElementValueForUser(nodeHandle.get(), toWK(value).get());
}

unsigned LayoutTestController::windowCount()
{
    return InjectedBundle::shared().pageCount();
}

void LayoutTestController::clearBackForwardList()
{
    WKBundleBackForwardListClear(WKBundlePageGetBackForwardList(InjectedBundle::shared().page()->page()));
}

// Object Creation

void LayoutTestController::makeWindowObject(JSContextRef context, JSObjectRef windowObject, JSValueRef* exception)
{
    setProperty(context, windowObject, "testRunner", this, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete, exception);
}

void LayoutTestController::showWebInspector()
{
#if ENABLE(INSPECTOR)
    WKBundleInspectorShow(WKBundlePageGetInspector(InjectedBundle::shared().page()->page()));
#endif // ENABLE(INSPECTOR)
}

void LayoutTestController::closeWebInspector()
{
#if ENABLE(INSPECTOR)
    WKBundleInspectorClose(WKBundlePageGetInspector(InjectedBundle::shared().page()->page()));
#endif // ENABLE(INSPECTOR)
}

void LayoutTestController::evaluateInWebInspector(long callID, JSStringRef script)
{
#if ENABLE(INSPECTOR)
    WKRetainPtr<WKStringRef> scriptWK = toWK(script);
    WKBundleInspectorEvaluateScriptForTest(WKBundlePageGetInspector(InjectedBundle::shared().page()->page()), callID, scriptWK.get());
#endif // ENABLE(INSPECTOR)
}

typedef WTF::HashMap<unsigned, WKRetainPtr<WKBundleScriptWorldRef> > WorldMap;
static WorldMap& worldMap()
{
    static WorldMap& map = *new WorldMap;
    return map;
}

unsigned LayoutTestController::worldIDForWorld(WKBundleScriptWorldRef world)
{
    WorldMap::const_iterator end = worldMap().end();
    for (WorldMap::const_iterator it = worldMap().begin(); it != end; ++it) {
        if (it->second == world)
            return it->first;
    }

    return 0;
}

void LayoutTestController::evaluateScriptInIsolatedWorld(JSContextRef context, unsigned worldID, JSStringRef script)
{
    // A worldID of 0 always corresponds to a new world. Any other worldID corresponds to a world
    // that is created once and cached forever.
    WKRetainPtr<WKBundleScriptWorldRef> world;
    if (!worldID)
        world.adopt(WKBundleScriptWorldCreateWorld());
    else {
        WKRetainPtr<WKBundleScriptWorldRef>& worldSlot = worldMap().add(worldID, 0).iterator->second;
        if (!worldSlot)
            worldSlot.adopt(WKBundleScriptWorldCreateWorld());
        world = worldSlot;
    }

    WKBundleFrameRef frame = WKBundleFrameForJavaScriptContext(context);
    if (!frame)
        frame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());

    JSGlobalContextRef jsContext = WKBundleFrameGetJavaScriptContextForWorld(frame, world.get());
    JSEvaluateScript(jsContext, script, 0, 0, 0, 0); 
}

void LayoutTestController::setPOSIXLocale(JSStringRef locale)
{
    char localeBuf[32];
    JSStringGetUTF8CString(locale, localeBuf, sizeof(localeBuf));
    setlocale(LC_ALL, localeBuf);
}

void LayoutTestController::setTextDirection(JSStringRef direction)
{
    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    return WKBundleFrameSetTextDirection(mainFrame, toWK(direction).get());
}
    
void LayoutTestController::setShouldStayOnPageAfterHandlingBeforeUnload(bool shouldStayOnPage)
{
    InjectedBundle::shared().postNewBeforeUnloadReturnValue(!shouldStayOnPage);
}

void LayoutTestController::setDefersLoading(bool shouldDeferLoading)
{
    WKBundlePageSetDefersLoading(InjectedBundle::shared().page()->page(), shouldDeferLoading);
}

void LayoutTestController::setPageVisibility(JSStringRef state)
{
    WebCore::PageVisibilityState visibilityState = WebCore::PageVisibilityStateVisible;

    if (JSStringIsEqualToUTF8CString(state, "hidden"))
        visibilityState = WebCore::PageVisibilityStateHidden;
    else if (JSStringIsEqualToUTF8CString(state, "prerender"))
        visibilityState = WebCore::PageVisibilityStatePrerender;
    else if (JSStringIsEqualToUTF8CString(state, "preview"))
        visibilityState = WebCore::PageVisibilityStatePreview;

    WKBundleSetPageVisibilityState(InjectedBundle::shared().bundle(), InjectedBundle::shared().page()->page(), visibilityState, /* isInitialState */ false);
}

void LayoutTestController::resetPageVisibility()
{
    WKBundleSetPageVisibilityState(InjectedBundle::shared().bundle(), InjectedBundle::shared().page()->page(), WebCore::PageVisibilityStateVisible, /* isInitialState */ true);
}

typedef WTF::HashMap<unsigned, JSValueRef> CallbackMap;
static CallbackMap& callbackMap()
{
    static CallbackMap& map = *new CallbackMap;
    return map;
}

enum {
    AddChromeInputFieldCallbackID = 1,
    RemoveChromeInputFieldCallbackID,
    FocusWebViewCallbackID,
    SetBackingScaleFactorCallbackID
};

static void cacheLayoutTestControllerCallback(unsigned index, JSValueRef callback)
{
    if (!callback)
        return;

    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    JSContextRef context = WKBundleFrameGetJavaScriptContext(mainFrame);
    JSValueProtect(context, callback);
    callbackMap().add(index, callback);
}

static void callLayoutTestControllerCallback(unsigned index)
{
    if (!callbackMap().contains(index))
        return;
    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    JSContextRef context = WKBundleFrameGetJavaScriptContext(mainFrame);
    JSObjectRef callback = JSValueToObject(context, callbackMap().take(index), 0);
    JSObjectCallAsFunction(context, callback, JSContextGetGlobalObject(context), 0, 0, 0);
    JSValueUnprotect(context, callback);
}

void LayoutTestController::addChromeInputField(JSValueRef callback)
{
    cacheLayoutTestControllerCallback(AddChromeInputFieldCallbackID, callback);
    InjectedBundle::shared().postAddChromeInputField();
}

void LayoutTestController::removeChromeInputField(JSValueRef callback)
{
    cacheLayoutTestControllerCallback(RemoveChromeInputFieldCallbackID, callback);
    InjectedBundle::shared().postRemoveChromeInputField();
}

void LayoutTestController::focusWebView(JSValueRef callback)
{
    cacheLayoutTestControllerCallback(FocusWebViewCallbackID, callback);
    InjectedBundle::shared().postFocusWebView();
}

void LayoutTestController::setBackingScaleFactor(double backingScaleFactor, JSValueRef callback)
{
    cacheLayoutTestControllerCallback(SetBackingScaleFactorCallbackID, callback);
    InjectedBundle::shared().postSetBackingScaleFactor(backingScaleFactor);
}

void LayoutTestController::setWindowIsKey(bool isKey)
{
    InjectedBundle::shared().postSetWindowIsKey(isKey);
}

void LayoutTestController::callAddChromeInputFieldCallback()
{
    callLayoutTestControllerCallback(AddChromeInputFieldCallbackID);
}

void LayoutTestController::callRemoveChromeInputFieldCallback()
{
    callLayoutTestControllerCallback(RemoveChromeInputFieldCallbackID);
}

void LayoutTestController::callFocusWebViewCallback()
{
    callLayoutTestControllerCallback(FocusWebViewCallbackID);
}

void LayoutTestController::callSetBackingScaleFactorCallback()
{
    callLayoutTestControllerCallback(SetBackingScaleFactorCallbackID);
}

void LayoutTestController::overridePreference(JSStringRef preference, bool value)
{
    WKBundleOverrideBoolPreferenceForTestRunner(InjectedBundle::shared().bundle(), InjectedBundle::shared().pageGroup(), toWK(preference).get(), value);
}

void LayoutTestController::sendWebIntentResponse(JSStringRef reply)
{
#if ENABLE(WEB_INTENTS)
    WKRetainPtr<WKBundleIntentRequestRef> currentRequest = InjectedBundle::shared().page()->currentIntentRequest();
    if (!currentRequest)
        return;

    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    JSContextRef context = WKBundleFrameGetJavaScriptContext(mainFrame);

    if (reply) {
        WKRetainPtr<WKSerializedScriptValueRef> serializedData(AdoptWK, WKSerializedScriptValueCreate(context, JSValueMakeString(context, reply), 0));
        WKBundleIntentRequestPostResult(currentRequest.get(), serializedData.get());
    } else {
        JSRetainPtr<JSStringRef> errorReply(JSStringCreateWithUTF8CString("ERROR"));
        WKRetainPtr<WKSerializedScriptValueRef> serializedData(AdoptWK, WKSerializedScriptValueCreate(context, JSValueMakeString(context, errorReply.get()), 0));
        WKBundleIntentRequestPostFailure(currentRequest.get(), serializedData.get());
    }
#endif
}

void LayoutTestController::deliverWebIntent(JSStringRef action, JSStringRef type, JSStringRef data)
{
#if ENABLE(WEB_INTENTS)
    WKBundleFrameRef mainFrame = WKBundlePageGetMainFrame(InjectedBundle::shared().page()->page());
    JSContextRef context = WKBundleFrameGetJavaScriptContext(mainFrame);

    WKRetainPtr<WKStringRef> actionWK = toWK(action);
    WKRetainPtr<WKStringRef> typeWK = toWK(type);
    WKRetainPtr<WKSerializedScriptValueRef> dataWK(AdoptWK, WKSerializedScriptValueCreate(context, JSValueMakeString(context, data), 0));

    WKRetainPtr<WKMutableDictionaryRef> intentInitDict(AdoptWK, WKMutableDictionaryCreate());
    WKDictionaryAddItem(intentInitDict.get(), WKStringCreateWithUTF8CString("action"), actionWK.get());
    WKDictionaryAddItem(intentInitDict.get(), WKStringCreateWithUTF8CString("type"), typeWK.get());
    WKDictionaryAddItem(intentInitDict.get(), WKStringCreateWithUTF8CString("data"), dataWK.get());

    WKRetainPtr<WKIntentDataRef> wkIntentData(AdoptWK, WKIntentDataCreate(intentInitDict.get()));
    WKBundlePageDeliverIntentToFrame(InjectedBundle::shared().page()->page(), mainFrame, wkIntentData.get());
#endif
}

void LayoutTestController::setAlwaysAcceptCookies(bool accept)
{
    WKBundleSetAlwaysAcceptCookies(InjectedBundle::shared().bundle(), accept);
}

double LayoutTestController::preciseTime()
{
    return currentTime();
}

void LayoutTestController::setUserStyleSheetEnabled(bool enabled)
{
    m_userStyleSheetEnabled = enabled;

    WKRetainPtr<WKStringRef> emptyUrl = adoptWK(WKStringCreateWithUTF8CString(""));
    WKStringRef location = enabled ? m_userStyleSheetLocation.get() : emptyUrl.get();
    WKBundleSetUserStyleSheetLocation(InjectedBundle::shared().bundle(), InjectedBundle::shared().pageGroup(), location);
}

void LayoutTestController::setUserStyleSheetLocation(JSStringRef location)
{
    m_userStyleSheetLocation = adoptWK(WKStringCreateWithJSString(location));

    if (m_userStyleSheetEnabled)
        setUserStyleSheetEnabled(true);
}

} // namespace WTR
