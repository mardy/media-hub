/*
 * Copyright © 2013 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Thomas Voß <thomas.voss@canonical.com>
 */
#ifndef CORE_UBUNTU_MEDIA_TRACK_LIST_SKELETON_H_
#define CORE_UBUNTU_MEDIA_TRACK_LIST_SKELETON_H_

#include "apparmor/ubuntu.h"

#include <core/media/track_list.h>

#include <core/media/player.h>

#include <core/dbus/object.h>
#include <core/dbus/skeleton.h>

namespace core
{
namespace ubuntu
{
namespace media
{
class TrackListSkeleton : public core::ubuntu::media::TrackList
{
public:
    TrackListSkeleton(const core::dbus::Bus::Ptr& bus, const core::dbus::Object::Ptr& object,
        const core::ubuntu::media::apparmor::ubuntu::RequestContextResolver::Ptr& request_context_resolver,
        const core::ubuntu::media::apparmor::ubuntu::RequestAuthenticator::Ptr& request_authenticator);
    ~TrackListSkeleton();

    bool has_next();
    bool has_previous();
    Track::Id next();
    Track::Id previous();
    const Track::Id& current();

    const core::Property<bool>& can_edit_tracks() const;
    const core::Property<Container>& tracks() const;

    const core::Signal<ContainerTrackIdTuple>& on_track_list_replaced() const;
    core::Signal<ContainerTrackIdTuple>& on_track_list_replaced();
    const core::Signal<Track::Id>& on_track_added() const;
    core::Signal<Track::Id>& on_track_added();
    const core::Signal<ContainerURI>& on_tracks_added() const;
    core::Signal<ContainerURI>& on_tracks_added();
    const core::Signal<TrackIdTuple>& on_track_moved() const;
    core::Signal<TrackIdTuple>& on_track_moved();
    const core::Signal<Track::Id>& on_track_removed() const;
    const core::Signal<void>& on_track_list_reset() const;
    const core::Signal<Track::Id>& on_track_changed() const;
    core::Signal<Track::Id>& on_track_changed();
    const core::Signal<Track::Id>& on_go_to_track() const;
    core::Signal<Track::Id>& on_go_to_track();
    const core::Signal<void>& on_end_of_tracklist() const;
    core::Signal<void>& on_end_of_tracklist();
    core::Signal<Track::Id>& on_track_removed();
    core::Signal<void>& on_track_list_reset();

    core::Property<Container>& tracks();
    void on_loop_status_changed(const core::ubuntu::media::Player::LoopStatus& loop_status);
    core::ubuntu::media::Player::LoopStatus loop_status() const;

    void on_position_changed(uint64_t position);

    /** Gets called when the shuffle property on the Player interface is changed
     * by the client */
    void on_shuffle_changed(bool shuffle);

    virtual void set_shuffle(bool shuffle) = 0;
    virtual bool shuffle() = 0;
    virtual const media::TrackList::Container& shuffled_tracks() = 0;

protected:
    inline bool is_first_track(const ConstIterator &it)
    { return it == std::begin(tracks().get()); }
    inline bool is_last_track(const ConstIterator &it)
    { return it == std::end(tracks().get()); }
    const TrackList::ConstIterator& current_iterator();
    bool update_current_iterator(const TrackList::ConstIterator &it);
    void reset_current_iterator_if_needed();
    media::Track::Id get_current_track(void);
    void set_current_track(const media::Track::Id& id);
    TrackList::ConstIterator get_current_shuffled();

    core::Property<bool>& can_edit_tracks();

    void emit_on_end_of_tracklist();

    void reset();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

// operator<< pretty prints the given TrackList status to the given output stream.
std::ostream& operator<<(std::ostream& out, const core::ubuntu::media::TrackList& tracklist);

}
}
}
#endif // CORE_UBUNTU_MEDIA_PROPERTY_H_
