/*
 * Copyright © 2014 Canonical Ltd.
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

#include "core/media/logger/logger.h"

#include <core/media/video/platform_default_sink.h>
#include <core/media/video/egl_sink.h>
#include <core/media/video/hybris_gl_sink.h>

namespace media = core::ubuntu::media;
namespace video = core::ubuntu::media::video;

namespace
{
struct NullSink : public video::Sink
{
    // The signal is emitted whenever a new frame is available and a subsequent
    // call to swap_buffers will not block and return true.
    const core::Signal<void>& frame_available() const
    {
        return signal_frame_available;
    }

    // Queries the transformation matrix for the current frame, placing the data into 'matrix'
    // and returns true or returns false and leaves 'matrix' unchanged in case
    // of issues.
    bool transformation_matrix(float*) const
    {
        return true;
    }

    // Releases the current buffer, and consumes the next buffer in the queue,
    // making it available for consumption by consumers of this API in an
    // implementation-specific way. Clients will usually rely on a GL texture
    // to receive the latest buffer.
    bool swap_buffers() const
    {
        return true;
    }

    core::Signal<void> signal_frame_available;
};
}

video::SinkFactory video::make_platform_default_sink_factory(const media::Player::PlayerKey& key,
                                                             const media::AVBackend::Backend b)
{
    switch (b)
    {
        case media::AVBackend::Backend::hybris:
            MH_DEBUG("Using hybris video sink");
            return video::HybrisGlSink::factory_for_key(key);
        case media::AVBackend::Backend::mir:
            MH_DEBUG("Using mir/egl video sink");
            return video::EglSink::factory_for_key(key);
        case media::AVBackend::Backend::none:
            MH_WARNING(
                "No video backend selected. Video rendering functionality won't work."
            );
            return [](std::uint32_t) { return video::Sink::Ptr{}; };
        default:
            MH_INFO("Invalid or no A/V backend specified, using \"hybris\" as a default.");
            return video::HybrisGlSink::factory_for_key(key);
    }
}
