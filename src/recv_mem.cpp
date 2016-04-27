/* Copyright 2015 SKA South Africa
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file
 */

#include <cstdint>
#include <cassert>
#include <mutex>
#include "recv_reader.h"
#include "recv_mem.h"
#include "recv_stream.h"

namespace spead2
{
namespace recv
{

mem_reader::mem_reader(
    stream &owner,
    const std::uint8_t *ptr, std::size_t length)
    : reader(owner), ptr(ptr), length(length)
{
    assert(ptr != nullptr);
    update_state();
}

void mem_reader::run()
{
    std::lock_guard<std::mutex> lock(get_stream_mutex());
    const std::uint8_t *new_ptr = mem_to_stream(get_stream_base(), ptr, length);
    length -= new_ptr - ptr;
    ptr = new_ptr;
    update_state();
    if (!get_stream_base().is_stopped())
    {
        if (get_stream_base().is_paused())
            pause();
        else if (length == 0)
            get_stream_base().stop_received();
    }
    update_state();
}

void mem_reader::update_state()
{
    if (get_stream_base().is_stopped())
    {
        if (state != state_t::STOPPED)
        {
            state = state_t::STOPPED;
            stopped_promise.set_value();
        }
    }
    else if (get_stream_base().is_paused())
    {
        state = state_t::PAUSED;
    }
    else
    {
        state = state_t::RUNNING;
        get_io_service().post([this] { run(); });
    }
}

void mem_reader::state_change()
{
    // If we're running, update_state will be called by the callback
    if (state != state_t::RUNNING)
        update_state();
}

void mem_reader::join()
{
    stopped_promise.get_future().get();
}

} // namespace recv
} // namespace spead2
