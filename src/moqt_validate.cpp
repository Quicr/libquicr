#include <quicr/detail/messages.h>

namespace quicr::messages {
    bool SubscribeUpdate::Validate() const
    {
        return true;
    }

    bool Subscribe::Validate() const
    {
        return true;
    }

    bool SubscribeOk::Validate() const
    {
        return true;
    }

    bool SubscribeError::Validate() const
    {
        return true;
    }

    bool Announce::Validate() const
    {
        return true;
    }

    bool AnnounceOk::Validate() const
    {
        return true;
    }

    bool AnnounceError::Validate() const
    {
        return true;
    }

    bool Unannounce::Validate() const
    {
        return true;
    }

    bool Unsubscribe::Validate() const
    {
        return true;
    }

    bool SubscribeDone::Validate() const
    {
        return true;
    }

    bool AnnounceCancel::Validate() const
    {
        return true;
    }

    bool TrackStatusRequest::Validate() const
    {
        return true;
    }

    bool TrackStatus::Validate() const
    {
        return true;
    }

    bool Goaway::Validate() const
    {
        return true;
    }

    bool SubscribeAnnounces::Validate() const
    {
        return true;
    }

    bool SubscribeAnnouncesOk::Validate() const
    {
        return true;
    }

    bool SubscribeAnnouncesError::Validate() const
    {
        return true;
    }

    bool UnsubscribeAnnounces::Validate() const
    {
        return true;
    }

    bool MaxRequestId::Validate() const
    {
        return true;
    }

    bool Fetch::Validate() const
    {
        return true;
    }
    bool FetchCancel::Validate() const
    {
        return true;
    }

    bool FetchOk::Validate() const
    {
        return true;
    }

    bool FetchError::Validate() const
    {
        return true;
    }

    bool RequestsBlocked::Validate() const
    {
        return true;
    }

    bool Publish::Validate() const
    {
        return true;
    }

    bool PublishOk::Validate() const
    {
        return true;
    }

    bool PublishError::Validate() const
    {
        return true;
    }

    bool ClientSetup::Validate() const
    {
        return true;
    }

    bool ServerSetup::Validate() const
    {
        return true;
    }

    bool NewGroupRequest::Validate() const
    {
        return true;
    }
}
