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

#ifndef SPEAD2_RECV_RING_STREAM
#define SPEAD2_RECV_RING_STREAM

#include <boost/asio.hpp>
#include <functional>
#include "common_ringbuffer.h"
#include "common_logging.h"
#include "common_thread_pool.h"
#include "recv_live_heap.h"
#include "recv_heap.h"
#include "recv_stream.h"

namespace spead2
{
namespace recv
{

/**
 * Base class for ring_stream containing only the parts that are independent of
 * the ringbuffer class.
 */
class ring_stream_base : public stream
{
public:
    static constexpr std::size_t default_ring_heaps = 4;

    using stream::stream;
};

/**
 * Specialisation of @ref stream that pushes its results into a ringbuffer.
 * The ringbuffer class may be replaced, but must provide the same interface as
 * @ref ringbuffer. If the ring buffer fills up, @ref add_packet will block the
 * reader.
 *
 * On the consumer side, heaps are automatically frozen as they are
 * extracted.
 *
 * This class is thread-safe.
 */
template<typename Ringbuffer = ringbuffer<live_heap, semaphore, semaphore_fd> >
class ring_stream : public ring_stream_base
{
private:
    Ringbuffer ready_heaps;
    bool contiguous_only;
    /**
     * Duplicate of the file descriptor from the free-space semaphore in the
     * ring-buffer, suitable for use with asio.
     */
    boost::asio::posix::stream_descriptor space_fd_handle;

    virtual bool heap_ready(live_heap &&) override;

    void resume_handler(const boost::system::error_code &error);
public:
    /**
     * Constructor.
     *
     * @param io_service       I/O service (also used by the readers)
     * @param bug_compat       Bug compatibility flags for interpreting heaps
     * @param max_heaps        Number of partial heaps to keep around
     * @param ring_heaps       Capacity of the ringbuffer
     * @param contiguous_only  If true, only contiguous heaps are pushed to the ring buffer
     */
    explicit ring_stream(
        boost::asio::io_service &io_service,
        bug_compat_mask bug_compat = 0,
        std::size_t max_heaps = default_max_heaps,
        std::size_t ring_heaps = default_ring_heaps,
        bool contiguous_only = true);
    /**
     * Constructor.
     *
     * @param pool             Used only to find the I/O service
     * @param bug_compat       Bug compatibility flags for interpreting heaps
     * @param max_heaps        Number of partial heaps
     * @param ring_heaps       Capacity of the ringbuffer
     * @param contiguous_only  If true, only contiguous heaps are pushed to the ring buffer
     */
    explicit ring_stream(
        thread_pool &pool,
        bug_compat_mask bug_compat = 0,
        std::size_t max_heaps = default_max_heaps,
        std::size_t ring_heaps = default_ring_heaps,
        bool contiguous_only = true)
        : ring_stream(pool.get_io_service(), bug_compat, max_heaps, ring_heaps, contiguous_only) {}

    virtual ~ring_stream() override;

    /**
     * Wait until a contiguous heap is available, freeze it, and
     * return it; or until the stream is stopped.
     *
     * @throw ringbuffer_stopped if @ref stop has been called and
     * there are no more contiguous heaps.
     */
    heap pop();

    /**
     * Like @ref pop, but if no contiguous heap is available,
     * throws @ref spead2::ringbuffer_empty.
     *
     * @throw ringbuffer_empty if there is no contiguous heap available, but the
     * stream has not been stopped
     * @throw ringbuffer_stopped if @ref stop has been called and
     * there are no more contiguous heaps.
     */
    heap try_pop();

    virtual void stop_received() override;
    virtual void stop_impl() override;

    const Ringbuffer &get_ringbuffer() const { return ready_heaps; }
};

template<typename Ringbuffer>
ring_stream<Ringbuffer>::ring_stream(
    boost::asio::io_service &io_service,
    bug_compat_mask bug_compat,
    std::size_t max_heaps,
    std::size_t ring_heaps,
    bool contiguous_only)
    : ring_stream_base(io_service, bug_compat, max_heaps),
    ready_heaps(ring_heaps),
    contiguous_only(contiguous_only),
    space_fd_handle(io_service, dup(ready_heaps.get_space_sem().get_fd()))
{
}

template<typename Ringbuffer>
ring_stream<Ringbuffer>::~ring_stream()
{
    /* Need to ensure that we call stop_impl while still a ring_stream. If
     * we leave it to the base class destructor, it is too late and we will
     * call the base class's stop_impl.
     */
    stop();
}

template<typename Ringbuffer>
void ring_stream<Ringbuffer>::resume_handler(const boost::system::error_code &error)
{
    if (error)
    {
        // operation_aborted is expected as part of stop
        if (error != boost::asio::error::operation_aborted)
        {
            log_warning("Error waiting for space in ringbuffer: %1%", error.message());
        }
    }
    else
    {
        std::lock_guard<std::mutex> lock(mutex);
        stream::resume();
        if (is_stopped() && !is_paused())
            ready_heaps.stop();
    }
}

template<typename Ringbuffer>
bool ring_stream<Ringbuffer>::heap_ready(live_heap &&h)
{
    if (!contiguous_only || h.is_contiguous())
    {
        try
        {
            ready_heaps.try_push(std::move(h));
        }
        catch (ringbuffer_full &e)
        {
            space_fd_handle.async_read_some(
                boost::asio::null_buffers(),
                std::bind(&ring_stream<Ringbuffer>::resume_handler, this, std::placeholders::_1));
            return false;
        }
        catch (ringbuffer_stopped &e)
        {
            // Suppress the error, drop the heap
            log_info("dropped heap %d due to external stop",
                     h.get_cnt());
        }
    }
    else
    {
        log_warning("dropped incomplete heap %d (%d/%d bytes of payload)",
                    h.get_cnt(), h.get_received_length(), h.get_heap_length());
    }
    return true;
}

template<typename Ringbuffer>
heap ring_stream<Ringbuffer>::pop()
{
    while (true)
    {
        live_heap h = ready_heaps.pop();
        if (h.is_contiguous())
            return heap(std::move(h));
        else
            log_info("received incomplete heap %d", h.get_cnt());
    }
}

template<typename Ringbuffer>
heap ring_stream<Ringbuffer>::try_pop()
{
    while (true)
    {
        live_heap h = ready_heaps.try_pop();
        if (h.is_contiguous())
            return heap(std::move(h));
        else
            log_info("received incomplete heap %d", h.get_cnt());
    }
}

template<typename Ringbuffer>
void ring_stream<Ringbuffer>::stop_received()
{
    /* Note: the order here is important: stream::stop_received flushes the
     * stream's internal buffer to the ringbuffer before the ringbuffer is
     * stopped.
     *
     * This only applies to a stop received from the network. A stop received
     * by calling stop() will first stop the ringbuffer to prevent a
     * deadlock.
     */
    stream::stop_received();
    if (!is_paused())
        ready_heaps.stop();
}

template<typename Ringbuffer>
void ring_stream<Ringbuffer>::stop_impl()
{
    /* Prevent resume_handler running after we're stopped (more importantly,
     * after we're destroyed). It would also be correct to call this after
     * ready_heaps.stop, but this order gives more predictable results: any
     * heaps that didn't make it into the ring buffer are always discarded
     * within the stream class, whereas closing it later might cause some of
     * them to be pushed and discarded within heap_ready.
     */
    space_fd_handle.close();
    /* Make sure the ringbuffer is stopped *before* the base implementation
     * takes the mutex. Without this, a heap_ready call could be blocking the
     * strand, waiting for space in the ring buffer. This will call the
     * heap_ready call to abort, allowing the strand to be accessed for the
     * rest of the shutdown.
     */
    ready_heaps.stop();
    stream::stop_impl();
}

} // namespace recv
} // namespace spead2

#endif // SPEAD2_RECV_RING_STREAM
