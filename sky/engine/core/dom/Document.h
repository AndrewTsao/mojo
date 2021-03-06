/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 *           (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2012 Apple Inc. All rights reserved.
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved. (http://www.torchmobile.com/)
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef SKY_ENGINE_CORE_DOM_DOCUMENT_H_
#define SKY_ENGINE_CORE_DOM_DOCUMENT_H_

#include "sky/engine/bindings/core/v8/ExceptionStatePlaceholder.h"
#include "sky/engine/bindings/core/v8/ScriptValue.h"
#include "sky/engine/core/animation/AnimationClock.h"
#include "sky/engine/core/animation/CompositorPendingAnimations.h"
#include "sky/engine/core/dom/ContainerNode.h"
#include "sky/engine/core/dom/DocumentInit.h"
#include "sky/engine/core/dom/DocumentLifecycle.h"
#include "sky/engine/core/dom/DocumentSupplementable.h"
#include "sky/engine/core/dom/ExecutionContext.h"
#include "sky/engine/core/dom/MutationObserver.h"
#include "sky/engine/core/dom/TextLinkColors.h"
#include "sky/engine/core/dom/TreeScope.h"
#include "sky/engine/core/dom/UserActionElementSet.h"
#include "sky/engine/core/dom/custom/CustomElement.h"
#include "sky/engine/core/fetch/ResourceClient.h"
#include "sky/engine/core/loader/DocumentLoadTiming.h"
#include "sky/engine/core/page/FocusType.h"
#include "sky/engine/core/page/PageVisibilityState.h"
#include "sky/engine/platform/Length.h"
#include "sky/engine/platform/Timer.h"
#include "sky/engine/platform/heap/Handle.h"
#include "sky/engine/platform/weborigin/KURL.h"
#include "sky/engine/platform/weborigin/ReferrerPolicy.h"
#include "sky/engine/wtf/HashSet.h"
#include "sky/engine/wtf/OwnPtr.h"
#include "sky/engine/wtf/PassOwnPtr.h"
#include "sky/engine/wtf/PassRefPtr.h"
#include "sky/engine/wtf/WeakPtr.h"
#include "sky/engine/wtf/text/TextEncoding.h"

namespace blink {

class AbstractModule;
class AnimationTimeline;
class Attr;
class CSSStyleDeclaration;
class CSSStyleSheet;
class CanvasRenderingContext2D;
class Comment;
class ConsoleMessage;
class CustomElementMicrotaskRunQueue;
class CustomElementRegistrationContext;
class DocumentFragment;
class DocumentLifecycleNotifier;
class DocumentLoadTiming;
class DocumentMarkerController;
class DocumentParser;
class Element;
class ElementDataCache;
class Event;
class EventFactoryBase;
class EventListener;
template <typename EventType>
class EventWithHitTestResults;
class ExceptionState;
class FloatQuad;
class FloatRect;
class Frame;
class FrameHost;
class FrameView;
class HTMLCanvasElement;
class HTMLDocumentParser;
class HTMLElement;
class HTMLImport;
class HTMLImportLoader;
class HTMLImportsController;
class HTMLScriptElement;
class HitTestRequest;
class LayoutPoint;
class LocalDOMWindow;
class LocalFrame;
class Location;
class MediaQueryListListener;
class MediaQueryMatcher;
class Page;
class PlatformMouseEvent;
class QualifiedName;
class Range;
class RenderView;
class RequestAnimationFrameCallback;
class ResourceFetcher;
class ScriptRunner;
class ScriptedAnimationController;
class SegmentedString;
class SelectorQueryCache;
class SerializedScriptValue;
class Settings;
class StyleEngine;
class StyleResolver;
class StyleSheet;
class StyleSheetList;
class Text;
class Touch;
class TouchList;
class WebGLRenderingContext;

struct AnnotatedRegionValue;

typedef EventWithHitTestResults<PlatformMouseEvent> MouseEventWithHitTestResults;
typedef int ExceptionCode;

enum StyleResolverUpdateMode {
    // Discards the StyleResolver and rebuilds it.
    FullStyleUpdate,
    // Attempts to use StyleInvalidationAnalysis to avoid discarding the entire StyleResolver.
    AnalyzedStyleUpdate
};

enum DocumentClass {
    DefaultDocumentClass = 0,
    HTMLDocumentClass = 1,
    MediaDocumentClass = 1 << 4,
};

typedef unsigned char DocumentClassFlags;

class Document;

class Document : public ContainerNode, public TreeScope, public ExecutionContext, public ExecutionContextClient
    , public DocumentSupplementable, public LifecycleContext<Document>, public ResourceClient {
    DEFINE_WRAPPERTYPEINFO();
public:
    static PassRefPtr<Document> create(const DocumentInit& initializer = DocumentInit())
    {
        return adoptRef(new Document(initializer));
    }
    virtual ~Document();

    // Called by JS.
    static PassRefPtr<Document> create(Document&);

    MediaQueryMatcher& mediaQueryMatcher();

    void mediaQueryAffectingValueChanged();

#if !ENABLE(OILPAN)
    using ContainerNode::ref;
    using ContainerNode::deref;
#endif
    using ExecutionContextClient::addConsoleMessage;
    using TreeScope::getElementById;

    virtual bool canContainRangeEndPoint() const override { return true; }

    SelectorQueryCache& selectorQueryCache();

    // Focus Management.
    Element* activeElement() const;
    bool hasFocus() const;

    AbstractModule* module() const { return m_module; }
    void setModule(AbstractModule* module) { m_module = module; }

    // DOM methods & attributes for Document

    Length viewportDefaultMinWidth() const { return m_viewportDefaultMinWidth; }

    ReferrerPolicy referrerPolicy() const { return m_referrerPolicy; }

    String outgoingReferrer();

    Element* documentElement() const
    {
        return m_documentElement.get();
    }

    Location* location() const;

    PassRefPtr<Element> createElement(const AtomicString& name, ExceptionState&);
    PassRefPtr<DocumentFragment> createDocumentFragment();
    PassRefPtr<Text> createTextNode(const String& data);
    PassRefPtr<Node> importNode(Node* importedNode, bool deep, ExceptionState&);
    PassRefPtr<Element> createElement(const QualifiedName&, bool createdByParser);

    Element* elementFromPoint(int x, int y) const;
    PassRefPtr<Range> caretRangeFromPoint(int x, int y);

    String readyState() const;

    AtomicString inputEncoding() const { return Document::encodingName(); }
    AtomicString charset() const { return Document::encodingName(); }
    AtomicString characterSet() const { return Document::encodingName(); }

    AtomicString encodingName() const;

    AtomicString contentType() const; // DOM 4 document.contentType

    const AtomicString& contentLanguage() const { return m_contentLanguage; }
    void setContentLanguage(const AtomicString&);

    KURL baseURI() const;

    String visibilityState() const;
    bool hidden() const;
    void didChangeVisibilityState();

    PassRefPtr<Node> adoptNode(PassRefPtr<Node> source, ExceptionState&);

    bool isHTMLDocument() const { return m_documentClasses & HTMLDocumentClass; }

    struct TransitionElementData {
        String scope;
        String selector;
        String markup;
    };
    void getTransitionElementData(Vector<TransitionElementData>&);

    StyleResolver* styleResolver() const;
    StyleResolver& ensureStyleResolver() const;

    bool isRenderingReady() const { return haveImportsLoaded(); }
    bool isScriptExecutionReady() const { return isRenderingReady(); }

    // This is a DOM function.
    StyleSheetList* styleSheets();

    StyleEngine* styleEngine() { return m_styleEngine.get(); }

    // Called when one or more stylesheets in the document may have been added, removed, or changed.
    void styleResolverChanged(StyleResolverUpdateMode = FullStyleUpdate);
    void styleResolverMayHaveChanged();

    // FIXME: Switch all callers of styleResolverChanged to these or better ones and then make them
    // do something smarter.
    void removedStyleSheet(StyleSheet*, StyleResolverUpdateMode = FullStyleUpdate);
    void addedStyleSheet(StyleSheet*) { styleResolverChanged(); }
    void modifiedStyleSheet(StyleSheet*, StyleResolverUpdateMode = FullStyleUpdate);
    void changedSelectorWatch() { styleResolverChanged(); }

    void evaluateMediaQueryList();

    void setStateForNewFormElements(const Vector<String>&);

    FrameView* view() const; // can be null
    LocalFrame* frame() const { return m_frame; } // can be null
    FrameHost* frameHost() const; // can be null
    Page* page() const; // can be null
    Settings* settings() const; // can be null

    float devicePixelRatio() const;

    PassRefPtr<Range> createRange();

    // Special support for editing
    PassRefPtr<Text> createEditingTextNode(const String&);

    void setupFontBuilder(RenderStyle* documentStyle);

    bool needsRenderTreeUpdate() const;
    void updateRenderTreeIfNeeded() { updateRenderTree(NoChange); }
    void updateRenderTreeForNodeIfNeeded(Node*);
    void updateLayout();
    enum RunPostLayoutTasks {
        RunPostLayoutTasksAsyhnchronously,
        RunPostLayoutTasksSynchronously,
    };
    void updateLayoutIgnorePendingStylesheets(RunPostLayoutTasks = RunPostLayoutTasksAsyhnchronously);
    PassRefPtr<RenderStyle> styleForElementIgnoringPendingStylesheets(Element*);

    void updateDistributionForNodeIfNeeded(Node*);

    ResourceFetcher* fetcher() { return m_fetcher.get(); }

    virtual void attach(const AttachContext& = AttachContext()) override;
    virtual void detach(const AttachContext& = AttachContext()) override;
    void prepareForDestruction();

    // If you have a Document, use renderView() instead which is faster.
    void renderer() const = delete;

    RenderView* renderView() const { return m_renderView; }

    DocumentLoadTiming* timing() const;

    DocumentParser* startParsing();
    void cancelParsing();

    // close() is the DOM API document.close()
    void close(ExceptionState& = ASSERT_NO_EXCEPTION);
    // In some situations (see the code), we ignore document.close().
    // explicitClose() bypass these checks and actually tries to close the
    // input stream.
    void explicitClose();
    // implicitClose() actually does the work of closing the input stream.
    void implicitClose();
    void checkCompleted();

    void dispatchUnloadEvents();

    enum PageDismissalType {
        NoDismissal = 0,
        PageHideDismissal = 2,
        UnloadDismissal = 3
    };
    PageDismissalType pageDismissalEventBeingDispatched() const;

    const KURL& url() const { return m_url; }
    void setURL(const KURL&);

    // To understand how these concepts relate to one another, please see the
    // comments surrounding their declaration.
    const KURL& baseURL() const { return m_baseURL; }

    KURL completeURL(const String&) const;

    CSSStyleSheet& elementSheet();

    TextPosition parserPosition() const;

    enum ReadyState {
        Loading,
        Interactive,
        Complete
    };
    void setReadyState(ReadyState);
    bool isLoadCompleted();

    void setParsing(bool);
    bool parsing() const { return m_isParsing; }

    bool shouldScheduleLayout() const;
    int elapsedTime() const;

    TextLinkColors& textLinkColors() { return m_textLinkColors; }

    MouseEventWithHitTestResults prepareMouseEvent(const HitTestRequest&, const LayoutPoint&, const PlatformMouseEvent&);

    bool setFocusedElement(PassRefPtr<Element>, FocusType = FocusTypeNone);
    Element* focusedElement() const { return m_focusedElement.get(); }
    UserActionElementSet& userActionElements()  { return m_userActionElements; }
    const UserActionElementSet& userActionElements() const { return m_userActionElements; }
    void setNeedsFocusedElementCheck();
    void setAutofocusElement(Element*);
    Element* autofocusElement() const { return m_autofocusElement.get(); }

    void setActiveHoverElement(PassRefPtr<Element>);
    Element* activeHoverElement() const { return m_activeHoverElement.get(); }

    void removeFocusedElementOfSubtree(Node*, bool amongChildrenOnly = false);
    void hoveredNodeDetached(Node*);
    void activeChainNodeDetached(Node*);

    void updateHoverActiveState(const HitTestRequest&, Element*, const PlatformMouseEvent* = 0);

    void scheduleRenderTreeUpdateIfNeeded();
    bool hasPendingForcedStyleRecalc() const;

    void attachRange(Range*);
    void detachRange(Range*);

    void updateRangesAfterChildrenChanged(ContainerNode*);
    void updateRangesAfterNodeMovedToAnotherDocument(const Node&);
    // nodeChildrenWillBeRemoved is used when removing all node children at once.
    void nodeChildrenWillBeRemoved(ContainerNode&);
    // nodeWillBeRemoved is only safe when removing one node at a time.
    void nodeWillBeRemoved(Node&);

    void didInsertText(Node*, unsigned offset, unsigned length);
    void didRemoveText(Node*, unsigned offset, unsigned length);
    void didMergeTextNodes(Text& oldNode, unsigned offset);
    void didSplitTextNode(Text& oldNode);

    void clearDOMWindow() { m_domWindow = nullptr; }
    LocalDOMWindow* domWindow() const { return m_domWindow; }

    static void registerEventFactory(PassOwnPtr<EventFactoryBase>);
    static PassRefPtr<Event> createEvent(const String& eventType, ExceptionState&);

    // keep track of what types of event listeners are registered, so we don't
    // dispatch events unnecessarily
    enum ListenerType {
        DOMSUBTREEMODIFIED_LISTENER          = 1,
        DOMNODEINSERTED_LISTENER             = 1 << 1,
        DOMNODEREMOVED_LISTENER              = 1 << 2,
        DOMNODEREMOVEDFROMDOCUMENT_LISTENER  = 1 << 3,
        DOMNODEINSERTEDINTODOCUMENT_LISTENER = 1 << 4,
        DOMCHARACTERDATAMODIFIED_LISTENER    = 1 << 5,
        ANIMATIONEND_LISTENER                = 1 << 6,
        ANIMATIONSTART_LISTENER              = 1 << 7,
        ANIMATIONITERATION_LISTENER          = 1 << 8,
        TRANSITIONEND_LISTENER               = 1 << 9,
        SCROLL_LISTENER                      = 1 << 10,
    };

    bool hasListenerType(ListenerType listenerType) const { return (m_listenerTypes & listenerType); }
    void addListenerTypeIfNeeded(const AtomicString& eventType);

    bool hasMutationObserversOfType(MutationObserver::MutationType type) const
    {
        return m_mutationObserverTypes & type;
    }
    bool hasMutationObservers() const { return m_mutationObserverTypes; }
    void addMutationObserverTypes(MutationObserverOptions types) { m_mutationObserverTypes |= types; }

    String title() const { return m_title; }
    void setTitle(const String&);

    Element* titleElement() const { return m_titleElement.get(); }
    void setTitleElement(Element*);
    void removeTitle(Element* titleElement);

    const AtomicString& dir();
    void setDir(const AtomicString&);

    const AtomicString& referrer() const;

    String domain() const;
    void setDomain(const String& newDomain, ExceptionState&);

    // The following implements the rule from HTML 4 for what valid names are.
    // To get this right for all the XML cases, we probably have to improve this or move it
    // and make it sensitive to the type of document.
    static bool isValidName(const String&);

    // The following breaks a qualified name into a prefix and a local name.
    // It also does a validity check, and returns false if the qualified name
    // is invalid.  It also sets ExceptionCode when name is invalid.
    static bool parseQualifiedName(const AtomicString& qualifiedName, AtomicString& prefix, AtomicString& localName, ExceptionState&);

    // Decide which element is to define the viewport's overflow policy. If |rootStyle| is set, use
    // that as the style for the root element, rather than obtaining it on our own. The reason for
    // this is that style may not have been associated with the elements yet - in which case it may
    // have been calculated on the fly (without associating it with the actual element) somewhere.
    Element* viewportDefiningElement(RenderStyle* rootStyle = 0) const;

    DocumentMarkerController& markers() const { return *m_markers; }

    bool directionSetOnDocumentElement() const { return m_directionSetOnDocumentElement; }
    void setDirectionSetOnDocumentElement(bool b) { m_directionSetOnDocumentElement = b; }

    KURL openSearchDescriptionURL();

    Document& topDocument() const;
    WeakPtr<Document> contextDocument();

    HTMLScriptElement* currentScript() const { return !m_currentScriptStack.isEmpty() ? m_currentScriptStack.last().get() : 0; }
    void pushCurrentScript(PassRefPtr<HTMLScriptElement>);
    void popCurrentScript();

    enum PendingSheetLayout { NoLayoutWithPendingSheets, DidLayoutWithPendingSheets, IgnoreLayoutWithPendingSheets };

    bool didLayoutWithPendingStylesheets() const { return m_pendingSheetLayout == DidLayoutWithPendingSheets; }
    bool ignoreLayoutWithPendingStylesheets() const { return m_pendingSheetLayout == IgnoreLayoutWithPendingSheets; }

    bool hasNodesWithPlaceholderStyle() const { return m_hasNodesWithPlaceholderStyle; }
    void setHasNodesWithPlaceholderStyle() { m_hasNodesWithPlaceholderStyle = true; }

    // Extension for manipulating canvas drawing contexts for use in CSS
    void getCSSCanvasContext(const String& type, const String& name, int width, int height, RefPtr<CanvasRenderingContext2D>&, RefPtr<WebGLRenderingContext>&);
    HTMLCanvasElement& getCSSCanvasElement(const String& name);

    void finishedParsing();

    const WTF::TextEncoding& encoding() const { return WTF::UTF8Encoding(); }

    virtual void removeAllEventListeners() override final;

    bool allowExecutingScripts(Node*);

    void statePopped(PassRefPtr<SerializedScriptValue>);

    enum LoadEventProgress {
        LoadEventNotRun,
        LoadEventTried,
        LoadEventInProgress,
        LoadEventCompleted,
        PageHideInProgress,
        UnloadEventInProgress,
        UnloadEventHandled
    };
    bool loadEventStillNeeded() const { return m_loadEventProgress == LoadEventNotRun; }
    bool processingLoadEvent() const { return m_loadEventProgress == LoadEventInProgress; }
    bool loadEventFinished() const { return m_loadEventProgress >= LoadEventCompleted; }
    bool unloadStarted() const { return m_loadEventProgress >= PageHideInProgress; }

    virtual bool isContextThread() const override final;

    bool containsValidityStyleRules() const { return m_containsValidityStyleRules; }
    void setContainsValidityStyleRules() { m_containsValidityStyleRules = true; }

    void enqueueResizeEvent();
    void enqueueScrollEventForNode(Node*);
    void enqueueAnimationFrameEvent(PassRefPtr<Event>);
    // Only one event for a target/event type combination will be dispatched per frame.
    void enqueueUniqueAnimationFrameEvent(PassRefPtr<Event>);
    void enqueueMediaQueryChangeListeners(Vector<RefPtr<MediaQueryListListener> >&);

    // Used to allow element that loads data without going through a FrameLoader to delay the 'load' event.
    void incrementLoadEventDelayCount() { ++m_loadEventDelayCount; }
    void decrementLoadEventDelayCount();
    void checkLoadEventSoon();
    bool isDelayingLoadEvent();

    PassRefPtr<Touch> createTouch(LocalDOMWindow*, EventTarget*, int identifier, double pageX, double pageY, double screenX, double screenY, double radiusX, double radiusY, float rotationAngle, float force) const;
    PassRefPtr<TouchList> createTouchList(Vector<RefPtr<Touch> >&) const;

    int requestAnimationFrame(PassOwnPtr<RequestAnimationFrameCallback>);
    void cancelAnimationFrame(int id);
    void serviceScriptedAnimations(double monotonicAnimationStartTime);

    virtual EventTarget* errorEventTarget() override final;
    virtual void logExceptionToConsole(const String& errorMessage, int scriptId, const String& sourceURL, int lineNumber, int columnNumber, PassRefPtr<ScriptCallStack>) override final;

    IntSize initialViewportSize() const;

    PassRefPtr<Element> createElement(const AtomicString& localName, const AtomicString& typeExtension, ExceptionState&);
    ScriptValue registerElement(ScriptState*, const AtomicString& name, ExceptionState&);
    ScriptValue registerElement(ScriptState*, const AtomicString& name, const Dictionary& options, ExceptionState&, CustomElement::NameSet validNames = CustomElement::StandardNames);
    CustomElementRegistrationContext* registrationContext() { return m_registrationContext.get(); }
    CustomElementMicrotaskRunQueue* customElementMicrotaskRunQueue();

    void setImportsController(HTMLImportsController*);
    HTMLImportsController* importsController() const { return m_importsController; }
    HTMLImportsController& ensureImportsController();
    HTMLImportLoader* importLoader() const;
    HTMLImport* import() const;

    bool haveImportsLoaded() const;
    void didLoadAllImports();

    void adjustFloatQuadsForScroll(Vector<FloatQuad>&);
    void adjustFloatRectForScroll(FloatRect&);

    unsigned activeParserCount() { return m_activeParserCount; }
    void incrementActiveParserCount() { ++m_activeParserCount; }
    void decrementActiveParserCount();

    ElementDataCache* elementDataCache() { return m_elementDataCache.get(); }

    void didLoadAllScriptBlockingResources();
    void didRemoveAllPendingStylesheet();
    void clearStyleResolver();

    bool inStyleRecalc() const { return m_lifecycle.state() == DocumentLifecycle::InStyleRecalc; }

    AnimationClock& animationClock() { return m_animationClock; }
    AnimationTimeline& timeline() const { return *m_timeline; }
    CompositorPendingAnimations& compositorPendingAnimations() { return m_compositorPendingAnimations; }

    // A non-null m_templateDocumentHost implies that |this| was created by ensureTemplateDocument().
    bool isTemplateDocument() const { return !!m_templateDocumentHost; }
    Document& ensureTemplateDocument();
    Document* templateDocumentHost() { return m_templateDocumentHost; }

    virtual void addMessage(PassRefPtr<ConsoleMessage>) override final;

    virtual LocalDOMWindow* executingWindow() override final;
    LocalFrame* executingFrame();

    DocumentLifecycleNotifier& lifecycleNotifier();
    DocumentLifecycle& lifecycle() { return m_lifecycle; }
    bool isActive() const { return m_lifecycle.isActive(); }
    bool isStopped() const { return m_lifecycle.state() == DocumentLifecycle::Stopped; }
    bool isDisposed() const { return m_lifecycle.state() == DocumentLifecycle::Disposed; }

    enum HttpRefreshType {
        HttpRefreshFromHeader,
        HttpRefreshFromMetaTag
    };
    void maybeHandleHttpRefresh(const String&, HttpRefreshType);

    PassOwnPtr<LifecycleNotifier<Document> > createLifecycleNotifier();

    void setHasViewportUnits() { m_hasViewportUnits = true; }
    bool hasViewportUnits() const { return m_hasViewportUnits; }
    void notifyResizeForViewportUnits();

    void didRecalculateStyleForElement() { ++m_styleRecalcElementCounter; }

    virtual v8::Handle<v8::Object> wrap(v8::Handle<v8::Object> creationContext, v8::Isolate*) override;

protected:
    Document(const DocumentInit&, DocumentClassFlags = DefaultDocumentClass);

#if !ENABLE(OILPAN)
    virtual void dispose() override;
#endif

    PassRefPtr<Document> cloneDocumentWithoutChildren();

    bool importContainerNodeChildren(ContainerNode* oldContainerNode, PassRefPtr<ContainerNode> newContainerNode, ExceptionState&);

private:
    friend class Node;

    bool isDocumentFragment() const = delete; // This will catch anyone doing an unnecessary check.
    bool isDocumentNode() const = delete; // This will catch anyone doing an unnecessary check.
    bool isElementNode() const = delete; // This will catch anyone doing an unnecessary check.

    ScriptedAnimationController& ensureScriptedAnimationController();
    virtual EventQueue* eventQueue() const override final;

    // FIXME: Rename the StyleRecalc state to RenderTreeUpdate.
    bool hasPendingStyleRecalc() const { return m_lifecycle.state() == DocumentLifecycle::VisualUpdatePending; }

    bool shouldScheduleRenderTreeUpdate() const;
    void scheduleRenderTreeUpdate();

    bool needsFullRenderTreeUpdate() const;

    bool dirtyElementsForLayerUpdate();
    void updateDistributionIfNeeded();
    void evaluateMediaQueryListIfNeeded();

    void updateRenderTree(StyleRecalcChange);
    void updateStyle(StyleRecalcChange);

    void detachParser();

    virtual bool isDocument() const override final { return true; }

    virtual void childrenChanged(const ChildrenChange&) override;

    virtual String nodeName() const override final;
    virtual NodeType nodeType() const override final;
    virtual PassRefPtr<Node> cloneNode(bool deep = true) override final;

#if !ENABLE(OILPAN)
    virtual void refExecutionContext() override final { ref(); }
    virtual void derefExecutionContext() override final { deref(); }
#endif

    virtual const KURL& virtualURL() const override final; // Same as url(), but needed for ExecutionContext to implement it without a performance loss for direct calls.
    virtual KURL virtualCompleteURL(const String&) const override final; // Same as completeURL() for the same reason as above.

    virtual void reportBlockedScriptExecutionToInspector(const String& directiveText) override final;

    virtual double timerAlignmentInterval() const override final;

    void updateTitle(const String&);
    void updateBaseURL();

    void executeScriptsWaitingForResourcesTimerFired(Timer<Document>*);

    void loadEventDelayTimerFired(Timer<Document>*);

    PageVisibilityState pageVisibilityState() const;

    // Note that dispatching a window load event may cause the LocalDOMWindow to be detached from
    // the LocalFrame, so callers should take a reference to the LocalDOMWindow (which owns us) to
    // prevent the Document from getting blown away from underneath them.
    void dispatchWindowLoadEvent();

    void addListenerType(ListenerType listenerType) { m_listenerTypes |= listenerType; }

    void clearFocusedElementSoon();
    void clearFocusedElementTimerFired(Timer<Document>*);
    void focusAutofocusElementTimerFired(Timer<Document>*);

    void setHoverNode(PassRefPtr<Node>);
    Node* hoverNode() const { return m_hoverNode.get(); }

    typedef HashSet<OwnPtr<EventFactoryBase> > EventFactorySet;
    static EventFactorySet& eventFactories();

    DocumentLifecycle m_lifecycle;

    AbstractModule* m_module;

    bool m_hasNodesWithPlaceholderStyle;
    bool m_evaluateMediaQueriesOnStyleRecalc;

    // If we do ignore the pending stylesheet count, then we need to add a boolean
    // to track that this happened so that we can do a full repaint when the stylesheets
    // do eventually load.
    PendingSheetLayout m_pendingSheetLayout;

    LocalFrame* m_frame;
    RawPtr<LocalDOMWindow> m_domWindow;
    // FIXME: oilpan: when we get rid of the transition types change the
    // HTMLImportsController to not be a DocumentSupplement since it is
    // redundant with oilpan.
    RawPtr<HTMLImportsController> m_importsController;

    RefPtr<ResourceFetcher> m_fetcher;
    RefPtr<DocumentParser> m_parser;
    unsigned m_activeParserCount;

    // Document URLs.
    KURL m_url; // Document.URL: The URL from which this document was retrieved.
    KURL m_baseURL; // Node.baseURI: The URL to use when resolving relative URLs.

    // Mime-type of the document in case it was cloned or created by XHR.
    AtomicString m_mimeType;

    RefPtr<CSSStyleSheet> m_elemSheet;

    Timer<Document> m_executeScriptsWaitingForResourcesTimer;

    bool m_hasAutofocused;
    Timer<Document> m_clearFocusedElementTimer;
    Timer<Document> m_focusAutofocusElementTimer;
    RefPtr<Element> m_autofocusElement;
    RefPtr<Element> m_focusedElement;
    RefPtr<Node> m_hoverNode;
    RefPtr<Element> m_activeHoverElement;
    RefPtr<Element> m_documentElement;
    UserActionElementSet m_userActionElements;

    typedef HashSet<RawPtr<Range> > AttachedRangeSet;
    AttachedRangeSet m_ranges;

    unsigned short m_listenerTypes;

    MutationObserverOptions m_mutationObserverTypes;

    OwnPtr<StyleEngine> m_styleEngine;
    RefPtr<StyleSheetList> m_styleSheetList;

    TextLinkColors m_textLinkColors;

    ReadyState m_readyState;
    bool m_isParsing;

    bool m_containsValidityStyleRules;

    String m_title;
    String m_rawTitle;
    RefPtr<Element> m_titleElement;

    OwnPtr<DocumentMarkerController> m_markers;

    LoadEventProgress m_loadEventProgress;

    double m_startTime;

    Vector<RefPtr<HTMLScriptElement> > m_currentScriptStack;

    AtomicString m_contentLanguage;

    HashMap<String, RefPtr<HTMLCanvasElement> > m_cssCanvasElements;

    OwnPtr<SelectorQueryCache> m_selectorQueryCache;

    DocumentClassFlags m_documentClasses;

    RenderView* m_renderView;

#if !ENABLE(OILPAN)
    WeakPtrFactory<Document> m_weakFactory;
#endif
    WeakPtr<Document> m_contextDocument;

    int m_loadEventDelayCount;
    Timer<Document> m_loadEventDelayTimer;

    Length m_viewportDefaultMinWidth;

    bool m_didSetReferrerPolicy;
    ReferrerPolicy m_referrerPolicy;

    bool m_directionSetOnDocumentElement;

    RefPtr<MediaQueryMatcher> m_mediaQueryMatcher;

    RefPtr<ScriptedAnimationController> m_scriptedAnimationController;

    RefPtr<CustomElementRegistrationContext> m_registrationContext;
    RefPtr<CustomElementMicrotaskRunQueue> m_customElementMicrotaskRunQueue;

    void elementDataCacheClearTimerFired(Timer<Document>*);
    Timer<Document> m_elementDataCacheClearTimer;

    OwnPtr<ElementDataCache> m_elementDataCache;

    AnimationClock m_animationClock;
    RefPtr<AnimationTimeline> m_timeline;
    CompositorPendingAnimations m_compositorPendingAnimations;

    RefPtr<Document> m_templateDocument;
    // With Oilpan the templateDocument and the templateDocumentHost
    // live and die together. Without Oilpan, the templateDocumentHost
    // is a manually managed backpointer from m_templateDocument.
    RawPtr<Document> m_templateDocumentHost;

    bool m_hasViewportUnits;

    int m_styleRecalcElementCounter;
    mutable DocumentLoadTiming m_documentLoadTiming;
};

inline void Document::scheduleRenderTreeUpdateIfNeeded()
{
    // Inline early out to avoid the function calls below.
    if (hasPendingStyleRecalc())
        return;
    if (shouldScheduleRenderTreeUpdate() && needsRenderTreeUpdate())
        scheduleRenderTreeUpdate();
}

DEFINE_TYPE_CASTS(Document, ExecutionContextClient, client, client->isDocument(), client.isDocument());
DEFINE_TYPE_CASTS(Document, ExecutionContext, context, context->isDocument(), context.isDocument());
DEFINE_NODE_TYPE_CASTS(Document, isDocumentNode());

#define DEFINE_DOCUMENT_TYPE_CASTS(thisType) \
    DEFINE_TYPE_CASTS(thisType, Document, document, document->is##thisType(), document.is##thisType())

// This is needed to avoid ambiguous overloads with the Node and TreeScope versions.
DEFINE_COMPARISON_OPERATORS_WITH_REFERENCES(Document)

// Put these methods here, because they require the Document definition, but we really want to inline them.

inline bool Node::isDocumentNode() const
{
    return this == document();
}

Node* eventTargetNodeForDocument(Document*);

} // namespace blink

#ifndef NDEBUG
// Outside the WebCore namespace for ease of invocation from gdb.
void showLiveDocumentInstances();
#endif

#endif  // SKY_ENGINE_CORE_DOM_DOCUMENT_H_
