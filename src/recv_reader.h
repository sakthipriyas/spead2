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

#ifndef SPEAD2_RECV_READER_H
#define SPEAD2_RECV_READER_H

#include <boost/asio.hpp>
#include <future>
#include <mutex>

namespace spead2
{
namespace recv
{

class stream;
class stream_base;

/**
 * Abstract base class for asynchronously reading data and passing it into
 * a stream.
 *
 * The lifecycle of a reader is:
 * - construction (stream mutex held)
 * - start (stream mutex held)
 * - state_change with stream stopped (stream mutex held)
 * - join (stream mutex not held)
 * - destruction (stream mutex held)
 */
class reader
{
private:
    stream &owner;          ///< Owning stream

protected:
    /// Convenience enum for subclasses
    enum class state_t
    {
        RUNNING,      ///< Asynchronous read has been queued
        PAUSED,       ///< No asynchronous read queued, but not yet stopped; may be ready packets
        STOPPED       ///< No asynchronous read queued, and stopped promise set
    };

    /**
     * Retrieve the wrapped stream's base class. This must only be used when
     * the stream's mutex is held.
     */
    stream_base &get_stream_base() const;

    /**
     * Retrieve the wrapped stream's @c reader_mutex.
     */
    std::mutex &get_stream_mutex() const;

public:
    explicit reader(stream &owner) : owner(owner) {}
    virtual ~reader() = default;

    /// Retrieve the wrapped stream
    stream &get_stream() const { return owner; }

    /// Retrieve the io_service corresponding to the owner
    boost::asio::io_service &get_io_service();

    /**
     * Second phase initialization. In most cases this doesn't need to be
     * overloaded. It is used where an initialization step needs to happen
     * without the stream lock held. This function is @em called with the
     * stream lock held, and should arrange for the initialization to happen
     * asynchronously. The future it returns will only be waited on once the
     * stream lock has been dropped.
     *
     * It may return an invalid future, in which case no wait occurs.
     */
    virtual std::future<void> start() { return std::future<void>(); }

    /**
     * Notify the reader that the stream may have changed state, either
     * because it is resumed from pause or because it has stopped. There is
     * currently no explicit notification of a pause (instead, the packet
     * handler should check for pause before passing on the packet), but this
     * may change in future.
     *
     * At present, a stop received from the network will not necessarily result
     * in a call to this function. However, a stop request from the user
     * (including implicitly via the destructor) guarantees that the reader
     * will have received a call with the stream stopped.
     *
     * Called with the stream lock held.
     */
    virtual void state_change() = 0;

    /**
     * Block until the last completion handler has finished. This is
     * guaranteed to only happen once, and only after a call to
     * @ref state_change with the stream stopped.
     */
    virtual void join() = 0;
};

} // namespace recv
} // namespace spead2

#endif // SPEAD2_RECV_READER_H
