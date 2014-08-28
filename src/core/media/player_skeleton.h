/**
 * Copyright (C) 2013-2014 Canonical Ltd
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

#ifndef CORE_UBUNTU_MEDIA_PLAYER_SKELETON_H_
#define CORE_UBUNTU_MEDIA_PLAYER_SKELETON_H_

#include <core/media/player.h>

#include "player_traits.h"

#include "mpris/player.h"

#include <core/dbus/skeleton.h>
#include <core/dbus/types/object_path.h>

#include <memory>

namespace core
{
namespace ubuntu
{
namespace media
{
class Service;

class PlayerSkeleton : public core::ubuntu::media::Player
{
  public:
    ~PlayerSkeleton();

    virtual const core::Property<bool>& can_play() const;
    virtual const core::Property<bool>& can_pause() const;
    virtual const core::Property<bool>& can_seek() const;
    virtual const core::Property<bool>& can_go_previous() const;
    virtual const core::Property<bool>& can_go_next() const;
    virtual const core::Property<bool>& is_video_source() const;
    virtual const core::Property<bool>& is_audio_source() const;
    virtual const core::Property<PlaybackStatus>& playback_status() const;
    virtual const core::Property<LoopStatus>& loop_status() const;
    virtual const core::Property<PlaybackRate>& playback_rate() const;
    virtual const core::Property<bool>& is_shuffle() const;
    virtual const core::Property<Track::MetaData>& meta_data_for_current_track() const;
    virtual const core::Property<Volume>& volume() const;
    virtual const core::Property<PlaybackRate>& minimum_playback_rate() const;
    virtual const core::Property<PlaybackRate>& maximum_playback_rate() const;
    virtual const core::Property<int64_t>& position() const;
    virtual const core::Property<int64_t>& duration() const;

    virtual core::Property<LoopStatus>& loop_status();
    virtual core::Property<PlaybackRate>& playback_rate();
    virtual core::Property<bool>& is_shuffle();
    virtual core::Property<Volume>& volume();

    virtual const core::Signal<int64_t>& seeked_to() const;
    virtual const core::Signal<void>& end_of_stream() const;
    virtual core::Signal<PlaybackStatus>& playback_status_changed();

  protected:
    // Functional modelling a helper to resolve artist/album names to
    // cover art.
    typedef std::function
    <
        std::string             // Returns a URL pointing to the album art
        (
            const std::string&, // The title of the track
            const std::string&, // The name of the album
            const std::string&  // The name of the artist
        )
    > CoverArtResolver;

    // Return a CoverArtResolver that always resolves to
    // file:///usr/share/unity/icons/album_missing.png
    static CoverArtResolver always_missing_cover_art_resolver();

    PlayerSkeleton(
            const std::shared_ptr<core::dbus::Bus>& bus,
            const std::shared_ptr<core::dbus::Object>& session,
            CoverArtResolver cover_art_resolver = always_missing_cover_art_resolver());

    virtual core::Property<PlaybackStatus>& playback_status();
    virtual core::Property<bool>& can_play();
    virtual core::Property<bool>& can_pause();
    virtual core::Property<bool>& can_seek();
    virtual core::Property<bool>& can_go_previous();
    virtual core::Property<bool>& can_go_next();
    virtual core::Property<bool>& is_video_source();
    virtual core::Property<bool>& is_audio_source();
    virtual core::Property<Track::MetaData>& meta_data_for_current_track();
    virtual core::Property<PlaybackRate>& minimum_playback_rate();
    virtual core::Property<PlaybackRate>& maximum_playback_rate();
    virtual core::Property<int64_t>& position();
    virtual core::Property<int64_t>& duration();

    virtual core::Signal<int64_t>& seeked_to();
    virtual core::Signal<void>& end_of_stream();

  private:
    struct Private;
    std::shared_ptr<Private> d;
};
}
}
}

#endif // CORE_UBUNTU_MEDIA_PLAYER_SKELETON_H_
