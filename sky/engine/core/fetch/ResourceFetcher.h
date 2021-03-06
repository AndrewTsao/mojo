/*
    Copyright (C) 1998 Lars Knoll (knoll@mpi-hd.mpg.de)
    Copyright (C) 2001 Dirk Mueller <mueller@kde.org>
    Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc. All rights reserved.
    Copyright (C) 2009 Torch Mobile Inc. http://www.torchmobile.com/

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.

    This class provides all functionality needed for loading images, style sheets and html
    pages from the web. It has a memory cache for these objects.
*/

#ifndef SKY_ENGINE_CORE_FETCH_RESOURCEFETCHER_H_
#define SKY_ENGINE_CORE_FETCH_RESOURCEFETCHER_H_

#include "sky/engine/core/fetch/CachePolicy.h"
#include "sky/engine/core/fetch/FetchInitiatorInfo.h"
#include "sky/engine/core/fetch/FetchRequest.h"
#include "sky/engine/core/fetch/Resource.h"
#include "sky/engine/core/fetch/ResourceLoaderHost.h"
#include "sky/engine/core/fetch/ResourceLoaderOptions.h"
#include "sky/engine/core/fetch/ResourcePtr.h"
#include "sky/engine/platform/Timer.h"
#include "sky/engine/wtf/Deque.h"
#include "sky/engine/wtf/HashMap.h"
#include "sky/engine/wtf/HashSet.h"
#include "sky/engine/wtf/ListHashSet.h"
#include "sky/engine/wtf/text/StringHash.h"

namespace blink {

class FetchContext;
class FontResource;
class ImageResource;
class RawResource;
class Document;
class LocalFrame;
class FrameLoader;
class ImageLoader;
class KURL;
class ResourceLoaderSet;

// The ResourceFetcher provides a per-context interface to the MemoryCache
// and enforces a bunch of security checks and rules for resource revalidation.
// Its lifetime is roughly per-DocumentLoader, in that it is generally created
// in the DocumentLoader constructor and loses its ability to generate network
// requests when the DocumentLoader is destroyed. Documents also hold a
// RefPtr<ResourceFetcher> for their lifetime (and will create one if they
// are initialized without a LocalFrame), so a Document can keep a ResourceFetcher
// alive past detach if scripts still reference the Document.
class ResourceFetcher final : public RefCounted<ResourceFetcher>, public ResourceLoaderHost {
    WTF_MAKE_NONCOPYABLE(ResourceFetcher); WTF_MAKE_FAST_ALLOCATED;
friend class ImageLoader;
friend class ResourceCacheValidationSuppressor;

public:
    static PassRefPtr<ResourceFetcher> create(Document* document) { return adoptRef(new ResourceFetcher(document)); }
    virtual ~ResourceFetcher();

#if !ENABLE(OILPAN)
    using RefCounted<ResourceFetcher>::ref;
    using RefCounted<ResourceFetcher>::deref;
#endif

    ResourcePtr<ImageResource> fetchImage(FetchRequest&);
    ResourcePtr<FontResource> fetchFont(FetchRequest&);

    // Logs an access denied message to the console for the specified URL.
    void printAccessDeniedMessage(const KURL&) const;

    Resource* cachedResource(const KURL&) const;

    typedef HashMap<String, ResourcePtr<Resource> > DocumentResourceMap;
    const DocumentResourceMap& allResources() const { return m_documentResources; }

    bool autoLoadImages() const { return m_autoLoadImages; }
    void setAutoLoadImages(bool);

    void setImagesEnabled(bool);

    bool shouldDeferImageLoad(const KURL&) const;

    LocalFrame* frame() const; // Can be null
    FetchContext& context() const;
    Document* document() const { return m_document; } // Can be null

    void garbageCollectDocumentResources();

    int requestCount() const { return m_requestCount; }

    void stopFetching();
    bool isFetching() const;

    // ResourceLoaderHost
    virtual void incrementRequestCount(const Resource*) override;
    virtual void decrementRequestCount(const Resource*) override;
    virtual void didLoadResource(Resource*) override;
    virtual void didFinishLoading(const Resource*, double finishTime, int64_t encodedDataLength) override;
    virtual void didChangeLoadingPriority(const Resource*, ResourceLoadPriority, int intraPriorityValue) override;
    virtual void didFailLoading(const Resource*, const ResourceError&) override;
    virtual void willSendRequest(unsigned long identifier, ResourceRequest&, const ResourceResponse& redirectResponse, const FetchInitiatorInfo&) override;
    virtual void didReceiveResponse(const Resource*, const ResourceResponse&) override;
    virtual void didReceiveData(const Resource*, const char* data, int dataLength, int encodedDataLength) override;
    virtual void didDownloadData(const Resource*, int dataLength, int encodedDataLength) override;
    virtual void subresourceLoaderFinishedLoadingOnePart(ResourceLoader*) override;
    virtual void didInitializeResourceLoader(ResourceLoader*) override;
    virtual void willTerminateResourceLoader(ResourceLoader*) override;
    virtual void willStartLoadingResource(Resource*, ResourceRequest&) override;
    virtual bool isLoadedBy(ResourceLoaderHost*) const override;

#if !ENABLE(OILPAN)
    virtual void refResourceLoaderHost() override;
    virtual void derefResourceLoaderHost() override;
#endif

    enum ResourceLoadStartType {
        ResourceLoadingFromNetwork,
        ResourceLoadingFromCache
    };
    void requestLoadStarted(Resource*, const FetchRequest&, ResourceLoadStartType);
    static const ResourceLoaderOptions& defaultResourceOptions();
private:

    explicit ResourceFetcher(Document*);

    bool shouldLoadNewResource(Resource::Type) const;

    ResourcePtr<Resource> requestResource(Resource::Type, FetchRequest&);
    ResourcePtr<Resource> createResourceForRevalidation(const FetchRequest&, Resource*);
    ResourcePtr<Resource> createResourceForLoading(Resource::Type, FetchRequest&, const String& charset);

    enum RevalidationPolicy { Use, Revalidate, Reload, Load };
    RevalidationPolicy determineRevalidationPolicy(Resource::Type, const FetchRequest&, Resource* existingResource) const;

    void determineRequestContext(ResourceRequest&, Resource::Type);
    ResourceRequestCachePolicy resourceRequestCachePolicy(const ResourceRequest&, Resource::Type);
    void addAdditionalRequestHeaders(ResourceRequest&, Resource::Type);

    bool canRequest(Resource::Type, const KURL&, const ResourceLoaderOptions&, FetchRequest::OriginRestriction) const;

    static bool resourceNeedsLoad(Resource*, const FetchRequest&, RevalidationPolicy);

    void notifyLoadedFromMemoryCache(Resource*);

    void garbageCollectDocumentResourcesTimerFired(Timer<ResourceFetcher>*);
    void scheduleDocumentResourcesGC();

    bool clientDefersImage(const KURL&) const;
    void reloadImagesIfNotDeferred();

    HashSet<String> m_validatedURLs;
    mutable DocumentResourceMap m_documentResources;
    Document* m_document;

    int m_requestCount;

    Timer<ResourceFetcher> m_garbageCollectDocumentResourcesTimer;

    OwnPtr<ResourceLoaderSet> m_loaders;
    OwnPtr<ResourceLoaderSet> m_multipartLoaders;

    // Used in hit rate histograms.
    class DeadResourceStatsRecorder {
    public:
        DeadResourceStatsRecorder();
        ~DeadResourceStatsRecorder();

        void update(RevalidationPolicy);

    private:
        int m_useCount;
        int m_revalidateCount;
        int m_loadCount;
    };
    DeadResourceStatsRecorder m_deadStatsRecorder;

    // 29 bits left
    bool m_autoLoadImages : 1;
    bool m_imagesEnabled : 1;
    bool m_allowStaleResources : 1;
};

class ResourceCacheValidationSuppressor {
    WTF_MAKE_NONCOPYABLE(ResourceCacheValidationSuppressor);
    WTF_MAKE_FAST_ALLOCATED;
public:
    ResourceCacheValidationSuppressor(ResourceFetcher* loader)
        : m_loader(loader)
        , m_previousState(false)
    {
        if (m_loader) {
            m_previousState = m_loader->m_allowStaleResources;
            m_loader->m_allowStaleResources = true;
        }
    }
    ~ResourceCacheValidationSuppressor()
    {
        if (m_loader)
            m_loader->m_allowStaleResources = m_previousState;
    }
private:
    ResourceFetcher* m_loader;
    bool m_previousState;
};

} // namespace blink

#endif  // SKY_ENGINE_CORE_FETCH_RESOURCEFETCHER_H_
