/*
 * Copyright © 2013-2015 Canonical Ltd.
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

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <tuple>
#include <unistd.h>

#include <dbus/dbus.h>

#include "track_list_implementation.h"

#include "engine.h"

namespace dbus = core::dbus;
namespace media = core::ubuntu::media;

struct media::TrackListImplementation::Private
{
    typedef std::map<Track::Id, std::tuple<Track::UriType, Track::MetaData>> MetaDataCache;

    dbus::Object::Ptr object;
    size_t track_counter;
    MetaDataCache meta_data_cache;
    std::shared_ptr<media::Engine::MetaDataExtractor> extractor;
    // Used for caching the original tracklist order to be used to restore the order
    // to the live TrackList after shuffle is turned off
    media::TrackList::Container original_tracklist;

    void updateCachedTrackMetadata(const media::Track::Id& id, const media::Track::UriType& uri)
    {
        if (meta_data_cache.count(id) == 0)
        {
            // FIXME: This code seems to conflict badly when called multiple times in a row: causes segfaults
#if 0
            try {
                meta_data_cache[id] = std::make_tuple(
                            uri,
                            extractor->meta_data_for_track_with_uri(uri));
            } catch (const std::runtime_error &e) {
                std::cerr << "Failed to retrieve metadata for track '" << uri << "' (" << e.what() << ")" << std::endl;
            }
#else
            meta_data_cache[id] = std::make_tuple(
                    uri,
                    core::ubuntu::media::Track::MetaData{});
#endif
        } else
        {
            std::get<0>(meta_data_cache[id]) = uri;
        }
    }
};

media::TrackListImplementation::TrackListImplementation(
        const dbus::Bus::Ptr& bus,
        const dbus::Object::Ptr& object,
        const std::shared_ptr<media::Engine::MetaDataExtractor>& extractor,
        const media::apparmor::ubuntu::RequestContextResolver::Ptr& request_context_resolver,
        const media::apparmor::ubuntu::RequestAuthenticator::Ptr& request_authenticator)
    : media::TrackListSkeleton(bus, object, request_context_resolver, request_authenticator),
      d(new Private{object, 0, Private::MetaDataCache{}, extractor, media::TrackList::Container{}})
{
    can_edit_tracks().set(true);
}

media::TrackListImplementation::~TrackListImplementation()
{
}

media::Track::UriType media::TrackListImplementation::query_uri_for_track(const media::Track::Id& id)
{
    const auto it = d->meta_data_cache.find(id);

    if (it == d->meta_data_cache.end())
        return Track::UriType{};

    return std::get<0>(it->second);
}

media::Track::MetaData media::TrackListImplementation::query_meta_data_for_track(const media::Track::Id& id)
{
    const auto it = d->meta_data_cache.find(id);

    if (it == d->meta_data_cache.end())
        return Track::MetaData{};

    return std::get<1>(it->second);
}

void media::TrackListImplementation::add_track_with_uri_at(
        const media::Track::UriType& uri,
        const media::Track::Id& position,
        bool make_current)
{
    std::cout << __PRETTY_FUNCTION__ << std::endl;

    std::stringstream ss;
    ss << d->object->path().as_string() << "/" << d->track_counter++;
    Track::Id id{ss.str()};

    std::cout << "Adding Track::Id: " << id << std::endl;
    std::cout << "\tURI: " << uri << std::endl;

    auto result = tracks().update([this, id, position, make_current](TrackList::Container& container)
    {
        auto it = std::find(container.begin(), container.end(), position);
        container.insert(it, id);
        std::cout << "container.size(): " << container.size() << std::endl;

        return true;
    });

    if (result)
    {
        d->updateCachedTrackMetadata(id, uri);

        if (make_current)
        {
            // Don't automatically call stop() and play() in player_implementation.cpp on_go_to_track()
            // since this breaks video playback when using open_uri() (stop() and play() are unwanted in
            // this scenario since the qtubuntu-media will handle this automatically)
            const bool toggle_player_state = false;
            go_to(id, toggle_player_state);
        }

        // Signal to the client that the current track has changed for the first track added to the TrackList
        if (tracks().get().size() == 1)
            on_track_changed()(id);

        std::cout << "Signaling that we just added track id: " << id << std::endl;
        // Signal to the client that a track was added to the TrackList
        on_track_added()(id);
    }
}

void media::TrackListImplementation::add_tracks_with_uri_at(const ContainerURI& uris, const Track::Id& position)
{
    std::cout << __PRETTY_FUNCTION__ << std::endl;

    ContainerURI tmp;
    for (const auto uri : uris)
    {
        // TODO: Refactor this code to use a smaller common function shared with add_track_with_uri_at()
        std::stringstream ss;
        ss << d->object->path().as_string() << "/" << d->track_counter++;
        Track::Id id{ss.str()};
        std::cout << "Adding Track::Id: " << id << std::endl;
        std::cout << "\tURI: " << uri << std::endl;

        tmp.push_back(id);

        Track::Id insert_position = position;

        auto it = std::find(tracks().get().begin(), tracks().get().end(), insert_position);
        auto result = tracks().update([this, id, position, it, &insert_position](TrackList::Container& container)
        {
            container.insert(it, id);
            // Make sure the next insert position is after the current insert position
            // Update the Track::Id after which to insert the next one from uris
            insert_position = id;

            return true;
        });

        if (result)
        {
            d->updateCachedTrackMetadata(id, uri);

            // Signal to the client that the current track has changed for the first track added to the TrackList
            if (tracks().get().size() == 1)
                on_track_changed()(id);
        }
    }

    std::cout << "Signaling that we just added " << tmp.size() << " tracks to the TrackList" << std::endl;
    on_tracks_added()(tmp);
}

void media::TrackListImplementation::remove_track(const media::Track::Id& id)
{
    auto result = tracks().update([id](TrackList::Container& container)
    {
        container.erase(std::find(container.begin(), container.end(), id));
        return true;
    });

    reset_current_iterator_if_needed();

    if (result)
    {
        d->meta_data_cache.erase(id);

        on_track_removed()(id);
    }

}

void media::TrackListImplementation::go_to(const media::Track::Id& track, bool toggle_player_state)
{
    std::cout << __PRETTY_FUNCTION__ << std::endl;
    std::pair<const media::Track::Id, bool> p = std::make_pair(track, toggle_player_state);
    // Signal the Player instance to go to a specific track for playback
    on_go_to_track()(p);
    on_track_changed()(track);
}

void media::TrackListImplementation::shuffle_tracks()
{
    std::cout << __PRETTY_FUNCTION__ << std::endl;

    if (tracks().get().empty())
        return;

    auto result = tracks().update([this](TrackList::Container& container)
    {
        // Save off the original TrackList ordering
        d->original_tracklist.assign(container.begin(), container.end());
        std::random_shuffle(container.begin(), container.end());
        return true;
    });

    if (result)
    {
        media::TrackList::ContainerTrackIdTuple t{std::make_tuple(tracks().get(), current())};
        on_track_list_replaced()(t);
    }
}

void media::TrackListImplementation::unshuffle_tracks()
{
    std::cout << __PRETTY_FUNCTION__ << std::endl;

    if (tracks().get().empty() or d->original_tracklist.empty())
        return;

    auto result = tracks().update([this](TrackList::Container& container)
    {
        // Restore the original TrackList ordering
        container.assign(d->original_tracklist.begin(), d->original_tracklist.end());
        return true;
    });

    if (result)
    {
        media::TrackList::ContainerTrackIdTuple t{std::make_tuple(tracks().get(), current())};
        on_track_list_replaced()(t);
    }
}

void media::TrackListImplementation::reset()
{
    // Make sure playback stops
    on_end_of_tracklist()();
    // And make sure there is no "current" track
    media::TrackListSkeleton::reset();

    auto result = tracks().update([this](TrackList::Container& container)
    {
        TrackList::Container ids = container;

        for (auto it = container.begin(); it != container.end(); ) {
            auto id = *it;
            it = container.erase(it);
            on_track_removed()(id);
        }

        container.resize(0);
        d->track_counter = 0;

        return true;
    });

    (void) result;
}
