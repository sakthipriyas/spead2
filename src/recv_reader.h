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
 * a stream. Subclasses will usually override @ref stop.
 *
 * The lifecycle of a reader is:
 * - construction (stream mutex held)
 * - stop (stream mutex held)
 * - join (stream mutex not held)
 * - destruction (stream mutex held)
 */
class reader
{
private:
    stream &owner;          ///< Owning stream
    /**
     * Set to indicate that the reader has bailed out of its packet processing
     * when it noticed that the owning stream was paused. This is set by @ref
     * pause and cleared by @ref resume. It is protected by the stream's mutex.
     */
    bool paused = false;

protected:
    /**
     * Called after the stream is paused, to indicate that packet reception
     * should be restarted. The stream calls @ref resume, which takes care of
     * checking whether this reader was paused. This will be called without
     * the stream mutex held, and from the @ref io_service.
     */
    virtual void resume_handler() = 0;

    /**
     * Retrieve the wrapped stream's base class. This must only be used when
     * the stream's mutex is held.
     */
    stream_base &get_stream_base() const;

    /**
     * Retrieve the wrapped stream's @c reader_mutex.
     */
    std::mutex &get_stream_mutex() const;

    /**
     * Check whether the reader has noted the pausing of the stream.
     */
    bool is_paused() const;

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
     */
    virtual std::future<void> start() { return std::future<void>(); }

    /**
     * Cancel any pending asynchronous operations. This is called with the
     * owner's mutex held. This function does not need to wait for
     * completion handlers to run.
     */
    virtual void stop() = 0;

    /**
     * Block until the last completion handler has finished.
     */
    virtual void join() = 0;

    /**
     * Called by the stream when the stream has resumed. Note that if no
     * packets arrived for this reader after the stream was paused, the
     * reader might not be paused. This function checks that before posting
     * a call to @ref resume_handler on the io_service. It is called with
     * the stream mutex held.
     */
    void resume();
};

} // namespace recv
} // namespace spead2

#endif // SPEAD2_RECV_READER_H
