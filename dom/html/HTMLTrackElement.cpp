/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLTrackElement.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLMediaElement.h"
#include "WebVTTListener.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/dom/HTMLTrackElementBinding.h"
#include "mozilla/dom/HTMLUnknownElement.h"
#include "nsAttrValueInlines.h"
#include "nsCOMPtr.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionParticipant.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsICachingChannel.h"
#include "nsIChannelEventSink.h"
#include "nsIContentPolicy.h"
#include "nsIContentSecurityPolicy.h"
#include "mozilla/dom/Document.h"
#include "nsIHttpChannel.h"
#include "nsIInterfaceRequestor.h"
#include "nsILoadGroup.h"
#include "nsIObserver.h"
#include "nsIStreamListener.h"
#include "nsISupportsImpl.h"
#include "nsISupportsPrimitives.h"
#include "nsMappedAttributes.h"
#include "nsNetUtil.h"
#include "nsStyleConsts.h"
#include "nsThreadUtils.h"
#include "nsVideoFrame.h"

static mozilla::LazyLogModule gTrackElementLog("nsTrackElement");
#define LOG(msg, ...)                          \
  MOZ_LOG(gTrackElementLog, LogLevel::Verbose, \
          ("TextTrackElement=%p, " msg, this, ##__VA_ARGS__))

// Replace the usual NS_IMPL_NS_NEW_HTML_ELEMENT(Track) so
// we can return an UnknownElement instead when pref'd off.
nsGenericHTMLElement* NS_NewHTMLTrackElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,
    mozilla::dom::FromParser aFromParser) {
  return new mozilla::dom::HTMLTrackElement(std::move(aNodeInfo));
}

namespace mozilla {
namespace dom {

// Map html attribute string values to TextTrackKind enums.
static constexpr nsAttrValue::EnumTable kKindTable[] = {
    {"subtitles", static_cast<int16_t>(TextTrackKind::Subtitles)},
    {"captions", static_cast<int16_t>(TextTrackKind::Captions)},
    {"descriptions", static_cast<int16_t>(TextTrackKind::Descriptions)},
    {"chapters", static_cast<int16_t>(TextTrackKind::Chapters)},
    {"metadata", static_cast<int16_t>(TextTrackKind::Metadata)},
    {nullptr, 0}};

// Invalid values are treated as "metadata" in ParseAttribute, but if no value
// at all is specified, it's treated as "subtitles" in GetKind
static const nsAttrValue::EnumTable* const kKindTableInvalidValueDefault =
    &kKindTable[4];

class WindowDestroyObserver final : public nsIObserver {
  NS_DECL_ISUPPORTS

 public:
  explicit WindowDestroyObserver(HTMLTrackElement* aElement, uint64_t aWinID)
      : mTrackElement(aElement), mInnerID(aWinID) {
    RegisterWindowDestroyObserver();
  }
  void RegisterWindowDestroyObserver() {
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (obs) {
      obs->AddObserver(this, "inner-window-destroyed", false);
    }
  }
  void UnRegisterWindowDestroyObserver() {
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (obs) {
      obs->RemoveObserver(this, "inner-window-destroyed");
    }
    mTrackElement = nullptr;
  }
  NS_IMETHODIMP Observe(nsISupports* aSubject, const char* aTopic,
                        const char16_t* aData) override {
    MOZ_ASSERT(NS_IsMainThread());
    if (strcmp(aTopic, "inner-window-destroyed") == 0) {
      nsCOMPtr<nsISupportsPRUint64> wrapper = do_QueryInterface(aSubject);
      NS_ENSURE_TRUE(wrapper, NS_ERROR_FAILURE);
      uint64_t innerID;
      nsresult rv = wrapper->GetData(&innerID);
      NS_ENSURE_SUCCESS(rv, rv);
      if (innerID == mInnerID) {
        if (mTrackElement) {
          mTrackElement->NotifyShutdown();
        }
        UnRegisterWindowDestroyObserver();
      }
    }
    return NS_OK;
  }

 private:
  ~WindowDestroyObserver(){};
  HTMLTrackElement* mTrackElement;
  uint64_t mInnerID;
};
NS_IMPL_ISUPPORTS(WindowDestroyObserver, nsIObserver);

/** HTMLTrackElement */
HTMLTrackElement::HTMLTrackElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
    : nsGenericHTMLElement(std::move(aNodeInfo)),
      mLoadResourceDispatched(false),
      mWindowDestroyObserver(nullptr) {
  nsISupports* parentObject = OwnerDoc()->GetParentObject();
  NS_ENSURE_TRUE_VOID(parentObject);
  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(parentObject);
  if (window) {
    mWindowDestroyObserver =
        new WindowDestroyObserver(this, window->WindowID());
  }
}

HTMLTrackElement::~HTMLTrackElement() {
  if (mWindowDestroyObserver) {
    mWindowDestroyObserver->UnRegisterWindowDestroyObserver();
  }
  NotifyShutdown();
}

NS_IMPL_ELEMENT_CLONE(HTMLTrackElement)

NS_IMPL_CYCLE_COLLECTION_INHERITED(HTMLTrackElement, nsGenericHTMLElement,
                                   mTrack, mMediaParent, mListener)

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(HTMLTrackElement,
                                               nsGenericHTMLElement)

void HTMLTrackElement::GetKind(DOMString& aKind) const {
  GetEnumAttr(nsGkAtoms::kind, kKindTable[0].tag, aKind);
}

void HTMLTrackElement::OnChannelRedirect(nsIChannel* aChannel,
                                         nsIChannel* aNewChannel,
                                         uint32_t aFlags) {
  NS_ASSERTION(aChannel == mChannel, "Channels should match!");
  mChannel = aNewChannel;
}

JSObject* HTMLTrackElement::WrapNode(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return HTMLTrackElement_Binding::Wrap(aCx, this, aGivenProto);
}

TextTrack* HTMLTrackElement::GetTrack() {
  if (!mTrack) {
    CreateTextTrack();
  }

  return mTrack;
}

void HTMLTrackElement::CreateTextTrack() {
  nsString label, srcLang;
  GetSrclang(srcLang);
  GetLabel(label);

  TextTrackKind kind;
  if (const nsAttrValue* value = GetParsedAttr(nsGkAtoms::kind)) {
    kind = static_cast<TextTrackKind>(value->GetEnumValue());
  } else {
    kind = TextTrackKind::Subtitles;
  }

  nsISupports* parentObject = OwnerDoc()->GetParentObject();

  NS_ENSURE_TRUE_VOID(parentObject);

  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(parentObject);
  mTrack =
      new TextTrack(window, kind, label, srcLang, TextTrackMode::Disabled,
                    TextTrackReadyState::NotLoaded, TextTrackSource::Track);
  mTrack->SetTrackElement(this);

  if (mMediaParent) {
    mMediaParent->AddTextTrack(mTrack);
  }
}

bool HTMLTrackElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                      const nsAString& aValue,
                                      nsIPrincipal* aMaybeScriptedPrincipal,
                                      nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None && aAttribute == nsGkAtoms::kind) {
    // Case-insensitive lookup, with the first element as the default.
    return aResult.ParseEnumValue(aValue, kKindTable, false,
                                  kKindTableInvalidValueDefault);
  }

  // Otherwise call the generic implementation.
  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aMaybeScriptedPrincipal, aResult);
}

void HTMLTrackElement::SetSrc(const nsAString& aSrc, ErrorResult& aError) {
  LOG("Set src=%s", NS_ConvertUTF16toUTF8(aSrc).get());

  nsAutoString src;
  if (GetAttr(kNameSpaceID_None, nsGkAtoms::src, src) && src == aSrc) {
    LOG("No need to reload for same src url");
    return;
  }

  SetHTMLAttr(nsGkAtoms::src, aSrc, aError);
  SetReadyState(TextTrackReadyState::NotLoaded);
  if (!mMediaParent) {
    return;
  }

  // Stop WebVTTListener.
  mListener = nullptr;
  if (mChannel) {
    mChannel->Cancel(NS_BINDING_ABORTED);
    mChannel = nullptr;
  }

  MaybeDispatchLoadResource();
}

void HTMLTrackElement::MaybeClearAllCues() {
  // Empty track's cue list whenever the track element's `src` attribute set,
  // changed, or removed,
  // https://html.spec.whatwg.org/multipage/media.html#sourcing-out-of-band-text-tracks:attr-track-src
  if (!mTrack) {
    return;
  }
  mTrack->ClearAllCues();
}

// This function will run partial steps from `start-the-track-processing-model`
// and finish the rest of steps in `LoadResource()` during the stable state.
// https://html.spec.whatwg.org/multipage/media.html#start-the-track-processing-model
void HTMLTrackElement::MaybeDispatchLoadResource() {
  MOZ_ASSERT(mTrack, "Should have already created text track!");

  // step2, if the text track's text track mode is not set to one of hidden or
  // showing, then return.
  if (mTrack->Mode() == TextTrackMode::Disabled) {
    LOG("Do not load resource for disable track");
    return;
  }

  // step3, if the text track's track element does not have a media element as a
  // parent, return.
  if (!mMediaParent) {
    LOG("Do not load resource for track without media element");
    return;
  }

  if (ReadyState() == TextTrackReadyState::Loaded) {
    LOG("Has already loaded resource");
    return;
  }

  // step5, await a stable state and run the rest of steps.
  if (!mLoadResourceDispatched) {
    RefPtr<WebVTTListener> listener = new WebVTTListener(this);
    RefPtr<Runnable> r = NewRunnableMethod<RefPtr<WebVTTListener>>(
        "dom::HTMLTrackElement::LoadResource", this,
        &HTMLTrackElement::LoadResource, std::move(listener));
    nsContentUtils::RunInStableState(r.forget());
    mLoadResourceDispatched = true;
  }
}

void HTMLTrackElement::LoadResource(RefPtr<WebVTTListener>&& aWebVTTListener) {
  LOG("LoadResource");
  mLoadResourceDispatched = false;

  nsAutoString src;
  if (!GetAttr(kNameSpaceID_None, nsGkAtoms::src, src) || src.IsEmpty()) {
    LOG("Fail to load because no src");
    SetReadyState(TextTrackReadyState::FailedToLoad);
    return;
  }

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NewURIFromString(src, getter_AddRefs(uri));
  NS_ENSURE_TRUE_VOID(NS_SUCCEEDED(rv));
  LOG("Trying to load from src=%s", NS_ConvertUTF16toUTF8(src).get());

  if (mChannel) {
    mChannel->Cancel(NS_BINDING_ABORTED);
    mChannel = nullptr;
  }

  // According to
  // https://www.w3.org/TR/html5/embedded-content-0.html#sourcing-out-of-band-text-tracks
  //
  // "8: If the track element's parent is a media element then let CORS mode
  // be the state of the parent media element's crossorigin content attribute.
  // Otherwise, let CORS mode be No CORS."
  //
  CORSMode corsMode =
      mMediaParent ? AttrValueToCORSMode(
                         mMediaParent->GetParsedAttr(nsGkAtoms::crossorigin))
                   : CORS_NONE;

  // Determine the security flag based on corsMode.
  nsSecurityFlags secFlags;
  if (CORS_NONE == corsMode) {
    // Same-origin is required for track element.
    secFlags = nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_DATA_INHERITS;
  } else {
    secFlags = nsILoadInfo::SEC_REQUIRE_CORS_DATA_INHERITS;
    if (CORS_ANONYMOUS == corsMode) {
      secFlags |= nsILoadInfo::SEC_COOKIES_SAME_ORIGIN;
    } else if (CORS_USE_CREDENTIALS == corsMode) {
      secFlags |= nsILoadInfo::SEC_COOKIES_INCLUDE;
    } else {
      NS_WARNING("Unknown CORS mode.");
      secFlags = nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_DATA_INHERITS;
    }
  }

  mListener = std::move(aWebVTTListener);
  // This will do 6. Set the text track readiness state to loading.
  rv = mListener->LoadResource();
  NS_ENSURE_TRUE_VOID(NS_SUCCEEDED(rv));

  Document* doc = OwnerDoc();
  if (!doc) {
    return;
  }

  // 9. End the synchronous section, continuing the remaining steps in parallel.
  nsCOMPtr<nsIRunnable> runnable = NS_NewRunnableFunction(
      "dom::HTMLTrackElement::LoadResource",
      [self = RefPtr<HTMLTrackElement>(this), this, uri, secFlags]() {
        if (!mListener) {
          // Shutdown got called, abort.
          return;
        }
        nsCOMPtr<nsIChannel> channel;
        nsCOMPtr<nsILoadGroup> loadGroup = OwnerDoc()->GetDocumentLoadGroup();
        nsresult rv = NS_NewChannel(getter_AddRefs(channel), uri,
                                    static_cast<Element*>(this), secFlags,
                                    nsIContentPolicy::TYPE_INTERNAL_TRACK,
                                    nullptr,  // PerformanceStorage
                                    loadGroup);

        if (NS_FAILED(rv)) {
          LOG("create channel failed.");
          SetReadyState(TextTrackReadyState::FailedToLoad);
          return;
        }

        channel->SetNotificationCallbacks(mListener);

        LOG("opening webvtt channel");
        rv = channel->AsyncOpen(mListener);

        if (NS_FAILED(rv)) {
          SetReadyState(TextTrackReadyState::FailedToLoad);
          return;
        }
        mChannel = channel;
      });
  doc->Dispatch(TaskCategory::Other, runnable.forget());
}

nsresult HTMLTrackElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  nsresult rv = nsGenericHTMLElement::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  LOG("Track Element bound to tree.");
  auto* parent = HTMLMediaElement::FromNode(aParent);
  if (!parent) {
    return NS_OK;
  }

  // Store our parent so we can look up its frame for display.
  if (!mMediaParent) {
    mMediaParent = parent;

    // TODO: separate notification for 'alternate' tracks?
    mMediaParent->NotifyAddedSource();
    LOG("Track element sent notification to parent.");

    // We may already have a TextTrack at this point if GetTrack() has already
    // been called. This happens, for instance, if script tries to get the
    // TextTrack before its mTrackElement has been bound to the DOM tree.
    if (!mTrack) {
      CreateTextTrack();
    }
    MaybeDispatchLoadResource();
  }

  return NS_OK;
}

void HTMLTrackElement::UnbindFromTree(bool aNullParent) {
  if (mMediaParent && aNullParent) {
    // mTrack can be null if HTMLTrackElement::LoadResource has never been
    // called.
    if (mTrack) {
      mMediaParent->RemoveTextTrack(mTrack);
      mMediaParent->UpdateReadyState();
    }
    mMediaParent = nullptr;
  }

  nsGenericHTMLElement::UnbindFromTree(aNullParent);
}

uint16_t HTMLTrackElement::ReadyState() const {
  if (!mTrack) {
    return TextTrackReadyState::NotLoaded;
  }

  return mTrack->ReadyState();
}

void HTMLTrackElement::SetReadyState(uint16_t aReadyState) {
  if (ReadyState() == aReadyState) {
    return;
  }

  if (mTrack) {
    switch (aReadyState) {
      case TextTrackReadyState::Loaded:
        LOG("dispatch 'load' event");
        DispatchTrackRunnable(NS_LITERAL_STRING("load"));
        break;
      case TextTrackReadyState::FailedToLoad:
        LOG("dispatch 'error' event");
        DispatchTrackRunnable(NS_LITERAL_STRING("error"));
        break;
    }
    mTrack->SetReadyState(aReadyState);
  }
}

void HTMLTrackElement::DispatchTrackRunnable(const nsString& aEventName) {
  Document* doc = OwnerDoc();
  if (!doc) {
    return;
  }
  nsCOMPtr<nsIRunnable> runnable = NewRunnableMethod<const nsString>(
      "dom::HTMLTrackElement::DispatchTrustedEvent", this,
      &HTMLTrackElement::DispatchTrustedEvent, aEventName);
  doc->Dispatch(TaskCategory::Other, runnable.forget());
}

void HTMLTrackElement::DispatchTrustedEvent(const nsAString& aName) {
  Document* doc = OwnerDoc();
  if (!doc) {
    return;
  }
  nsContentUtils::DispatchTrustedEvent(doc, static_cast<nsIContent*>(this),
                                       aName, CanBubble::eNo, Cancelable::eNo);
}

void HTMLTrackElement::DropChannel() { mChannel = nullptr; }

void HTMLTrackElement::NotifyShutdown() {
  if (mChannel) {
    mChannel->Cancel(NS_BINDING_ABORTED);
  }
  mChannel = nullptr;
  mListener = nullptr;
}

nsresult HTMLTrackElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                        const nsAttrValue* aValue,
                                        const nsAttrValue* aOldValue,
                                        nsIPrincipal* aMaybeScriptedPrincipal,
                                        bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None && aName == nsGkAtoms::src) {
    MaybeClearAllCues();
    // In spec, `start the track processing model` step10, while fetching is
    // ongoing, if the track URL changes, then we have to set the `FailedToLoad`
    // state.
    // https://html.spec.whatwg.org/multipage/media.html#sourcing-out-of-band-text-tracks:text-track-failed-to-load-3
    if (ReadyState() == TextTrackReadyState::Loading && aValue != aOldValue) {
      SetReadyState(TextTrackReadyState::FailedToLoad);
    }
  }
  return nsGenericHTMLElement::AfterSetAttr(
      aNameSpaceID, aName, aValue, aOldValue, aMaybeScriptedPrincipal, aNotify);
}

}  // namespace dom
}  // namespace mozilla
