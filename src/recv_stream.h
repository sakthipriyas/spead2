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

#ifndef SPEAD2_RECV_STREAM_H
#define SPEAD2_RECV_STREAM_H

#include <cstddef>
#include <memory>
#include <utility>
#include <functional>
#include <mutex>
#include <type_traits>
#include <deque>
#include <boost/asio.hpp>
#include "recv_live_heap.h"
#include "recv_reader.h"
#include "common_memory_pool.h"
#include "common_bind.h"

namespace spead2
{

class thread_pool;

namespace recv
{

struct packet_header;

/**
 * Encapsulation of a SPEAD stream. Packets are fed in through @ref add_packet.
 * The base class does nothing with heaps; subclasses will typically override
 * @ref heap_ready and @ref stop_received to do further processing.
 *
 * A collection of partial heaps is kept. Heaps are removed from this collection
 * and passed to @ref heap_ready when
 * - They are known to be complete (a heap length header is present and all the
 *   corresponding payload has been received); or
 * - Too many heaps are live: the one seen the earliest is aged out, even if
 *   incomplete
 * - The stream is stopped
 *
 * This class is @em not thread-safe. Almost all use cases (possibly excluding
 * testing) will derive from @ref stream.
 *
 * @internal
 *
 * The live heaps are stored in a circular queue (this has fewer pointer
 * indirections than @c std::deque). The heap cnts are stored in another
 * circular queue with the same indexing. The heap cnt queue is redundant, but
 * having a separate queue of heap cnts reduces the number of cache lines
 * touched to find the right heap.
 *
 * When a heap is removed from the circular queue, the queue is not shifted
 * up. Instead, a hole is left. The queue thus only needs a head and not a
 * tail. When adding a new heap, any heap stored in the head position is
 * evicted. This means that heaps may be evicted before it is strictly
 * necessary from the point of view of available storage, but this prevents
 * heaps with lost packets from hanging around forever.
 */
class stream_base
{
private:
    typedef typename std::aligned_storage<sizeof(live_heap), alignof(live_heap)>::type storage_type;
    /**
     * Circular queue for heaps.
     *
     * A particular heap is in a constructed state iff the corresponding
     * element of @a heap_cnt is non-negative.
     */
    std::unique_ptr<storage_type[]> heap_storage;
    /// Circular queue for heap cnts, with -1 indicating a hole.
    std::unique_ptr<s_item_pointer_t[]> heap_cnts;
    /// Position of the most recently added heap
    std::size_t head;
    /**
     * Emergency queue of heaps that could not be pushed downstream. There
     * should never be more than two entries (which can happen if a packet
     * evicts an old heap and is itself a valid heap), so this could probably
     * be optimised. The exception is that flush() will transfer all heaps
     * in the storage to that can't be immediately pushed to this deque.
     *
     * The stream is paused iff this list is non-empty.
     */
    std::deque<live_heap> resume_heaps;

    /// Maximum number of live heaps permitted.
    std::size_t max_heaps;
    /// @ref stop_received has been called, either externally or by stream control
    bool stopped = false;
    /// Protocol bugs to be compatible with
    bug_compat_mask bug_compat;

    /// Function used to copy heap payloads
    memcpy_function memcpy = std::memcpy;

    /// Memory pool used by heaps.
    std::shared_ptr<memory_pool> pool;

    /**
     * Callback called when a heap is being ejected from the live list.
     * The heap might or might not be complete. This function should return
     * true on success and false if it was not ready to consume the heap. If
     * false is returned, the callee must arrange for @ref resume to be
     * called once it is possibly ready to consume the heap.
     */
    virtual bool heap_ready(live_heap &&) { return true; }

protected:
    /**
     * Subclasses must be call this after @ref heap_ready returns false, to
     * indicate that the consumer might be ready to consume again.
     */
    void resume();

public:
    static constexpr std::size_t default_max_heaps = 4;

    /**
     * Constructor.
     *
     * @param bug_compat   Protocol bugs to have compatibility with
     * @param max_heaps    Maximum number of live (in-flight) heaps held in the stream
     */
    explicit stream_base(bug_compat_mask bug_compat = 0, std::size_t max_heaps = default_max_heaps);
    virtual ~stream_base();

    /// Set a pool to use for allocating heap memory.
    void set_memory_pool(std::shared_ptr<memory_pool> pool);

    /// Set an alternative memcpy function for copying heap payload
    void set_memcpy(memcpy_function memcpy);

    /// Set builtin memcpy function to use for copying payload
    void set_memcpy(memcpy_function_id id);

    /**
     * Add a packet that was received, and which has been examined by @a
     * decode_packet, and returns @c true if it is consumed. Even though @a
     * decode_packet does some basic sanity-checking, it may still be rejected
     * by @ref live_heap::add_packet e.g., because it is a duplicate.
     *
     * It is an error to call this after the stream has been stopped.
     */
    bool add_packet(const packet_header &packet);
    /**
     * Shut down the stream. This calls @ref flush.  Subclasses may override
     * this to achieve additional effects, but must chain to the base
     * implementation.
     *
     * It is undefined what happens if @ref add_packet is called after a stream
     * is stopped.
     */
    virtual void stop_received();

    bool is_stopped() const { return stopped; }
    bool is_paused() const { return !resume_heaps.empty(); }

    bug_compat_mask get_bug_compat() const { return bug_compat; }

    /// Flush the collection of live heaps, passing them to @ref heap_ready.
    void flush();

    /**
     * Throw away contents of @ref resume_heaps. Note that this does not call
     * @ref resume; this function is only intended for stopping the stream
     * externally.
     */
    void discard_resume_heaps();
};

/**
 * Stream that is fed by subclasses of @ref reader. Unlike @ref stream_base, it
 * is thread-safe, using a mutex to protect concurrent access.
 *
 * Readers may call functions from the base class directly. Unless otherwise
 * stated, they must hold the mutex to do so.
 */
class stream : protected stream_base
{
    friend class reader;
    friend class bypass_reader;
private:
    /**
     * io_service provided for readers.
     *
     * @todo Eliminate this, pass directly to readers' constructor.
     */
    boost::asio::io_service &io_service;
    /**
     * Readers providing the stream data.
     */
    std::vector<std::unique_ptr<reader> > readers;

    /// Ensure that @ref stop is only run once
    std::once_flag stop_once;

    /* Prevent moving (copying is already impossible). Moving is not safe
     * because readers refer back to *this (it could potentially be added if
     * there is a good reason for it, but it would require adding a new
     * function to the reader interface).
     */
    stream(stream_base &&) = delete;
    stream &operator=(stream_base &&) = delete;

protected:
    /**
     * Serialization of access. It does not apply to @c memcpy and @c pool in
     * the base class, which have their own serialization.
     */
    std::mutex mutex;

    virtual void stop_received() override;

    /// Actual implementation of @ref stop
    virtual void stop_impl();

    /**
     * Subclasses must be call this after @ref heap_ready returns false, to
     * indicate that the consumer might be ready to consume again. The caller
     * must hold the mutex.
     */
    void resume();

public:
    using stream_base::get_bug_compat;
    using stream_base::default_max_heaps;

    boost::asio::io_service &get_io_service() const { return io_service; }

    explicit stream(boost::asio::io_service &service, bug_compat_mask bug_compat = 0, std::size_t max_heaps = default_max_heaps);
    explicit stream(thread_pool &pool, bug_compat_mask bug_compat = 0, std::size_t max_heaps = default_max_heaps);
    virtual ~stream() override;

    // Override to hold the mutex
    void set_memory_pool(std::shared_ptr<memory_pool> pool);
    void set_memcpy(memcpy_function memcpy);
    void set_memcpy(memcpy_function_id id);

    /**
     * Add a new reader by passing its constructor arguments, excluding
     * the initial @a stream argument.
     */
    template<typename T, typename... Args>
    void emplace_reader(Args&&... args)
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!is_stopped())
        {
            readers.reserve(readers.size() + 1);
            reader *r = new T(*this, std::forward<Args>(args)...);
            std::unique_ptr<reader> ptr(r);
            std::future<void> start_future = r->start();
            readers.push_back(std::move(ptr));
            lock.unlock();
            if (start_future.valid())
                start_future.get();
        }
    }

    /**
     * Stop the stream and block until all the readers have wound up. After
     * calling this there should be no more outstanding completion handlers
     * in the thread pool.
     *
     * Subclasses should override @ref stop_received if they need to do
     * handling regardless of whether the stop came from the network or the
     * application. They should override @ref stop_impl for cleanup only
     * needed for a stop triggered from the application.
     */
    void stop();
};

/**
 * Push packets found in a block of memory to a stream. Returns a pointer to
 * after the last packet found in the stream. Processing stops as soon as
 * after @ref decode_packet fails (because there is no way to find the next
 * packet after a corrupt one), but packets may still be rejected by the stream.
 *
 * The stream is @em not stopped.
 */
const std::uint8_t *mem_to_stream(stream_base &s, const std::uint8_t *ptr, std::size_t length);

} // namespace recv
} // namespace spead2

#endif // SPEAD2_RECV_STREAM_H
