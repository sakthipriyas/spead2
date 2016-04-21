/* Copyright 2016 SKA South Africa
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
 *
 * Common code to support socket bypass technologies such as pcap, netmap,
 * pf_ring, verbs, DPDK etc.
 *
 * These techniques have a few common features:
 * - They provide complete packets, including headers for OSI layers 2 and up.
 * - They provide all packets arriving at an interface (although pcap allows
 *   kernel-side filtering).
 * - In most cases, it is necessary to just have one receiver per interface,
 *   rather than one per stream.
 */

#ifndef SPEAD2_RECV_BYPASS_H
#define SPEAD2_RECV_BYPASS_H

#include <vector>
#include <string>
#include <utility>
#include <memory>
#include <map>
#include <cstdint>
#include <cstddef>
#include <future>
#include <boost/asio.hpp>
#include "recv_stream.h"
#include "recv_reader.h"

namespace spead2
{
namespace recv
{

class bypass_reader;

namespace detail
{

/**
 * Base class for the service that reads packets from an interface and inserts
 * them into streams. This service always runs its own thread, independent of
 * boost::asio. This simplifies implementation, because not all bypass
 * technologies integrate neatly with epoll and similar functions.
 */
class bypass_service
{
    // Prevent copying
    bypass_service(const bypass_service &) = delete;
    bypass_service &operator=(const bypass_service &) = delete;
private:
    const std::string type;
    const std::string interface;
    std::map<boost::asio::ip::udp::endpoint, bypass_reader *> readers;
    bool stopping = false;   ///< Set to true once last reader is removed

    /**
     * In normal cases this is called immediately after the constructor, but
     * only once registration data structures have been safely organised.
     * Once this function returns, you can assume that destruction will be
     * via stop(). In particular, this function may store a self-reference that
     * will be dropped by stop().
     *
     * This function is called without the mutex held, but with the guarantee
     * that no other thread has a reference (so the mutex need not be taken
     * either).
     */
    virtual void start() = 0;

    /**
     * Called when the last reader is removed. This should
     * (a) immediately make it safe to create a new instance of the
     *     bypass_service.
     * (b) either immediately or asynchronously arrange for any resources
     *     (such as threads) to be released and any internally-held shared_ptr
     *     references to be dropped.
     * This function is called with the mutex held.
     */
    virtual void stop() = 0;

protected:
    /**
     * Mutex protecting @ref readers, @ref stopping, and whatever subclasses
     * want it to protect.
     */
    std::mutex mutex;

    /**
     * Process a single packet. The caller must hold the mutex when calling this.
     *
     * @retval true if the packet is consumed
     * @retval false if the packet is not handled and should be passed on to the host stack
     */
    bool process_packet(const std::uint8_t *data, std::size_t length);

public:
    bypass_service(const std::string &type, const std::string &interface);
    virtual ~bypass_service();

    static std::shared_ptr<bypass_service> get_instance(const std::string &type, const std::string &interface);

    /**
     * Add a reader to the list of readers.
     *
     * @retval true if the reader was added successfully
     * @retval false if the service is stopping
     * @throw std::invalid_argument if the endpoint is already registered
     */
    bool add_endpoint(const boost::asio::ip::udp::endpoint &endpoint, bypass_reader *reader);
    void remove_endpoint(const boost::asio::ip::udp::endpoint &endpoint);
};

} // namespace detail

class bypass_reader : public reader
{
    friend class detail::bypass_service;
private:
    std::shared_ptr<detail::bypass_service> service;
    boost::asio::ip::udp::endpoint endpoint;
    std::future<void> stop_future;

    /**
     * Handle a single packet. The point is to the start of the SPEAD packet,
     * not the L2 headers.
     */
    void process_packet(const std::uint8_t *data, std::size_t length);

public:
    /**
     * Constructor.
     *
     * @param owner        Owning stream
     * @param type         Bypass method e.g. @c netmap
     * @param interface    Name of the network interface e.g., @c eth0
     * @param endpoint     Address on which to listen (IPv4 only)
     */
    bypass_reader(stream &owner,
                  const std::string &type,
                  const std::string &interface,
                  const boost::asio::ip::udp::endpoint &endpoint);

    virtual void stop() override;
    virtual void join() override;
};

/// Obtain a list of names of compiled-in bypass types.
std::vector<std::string> bypass_types();

} // namespace recv
} // namespace spead2

#endif // SPEAD2_RECV_BYPASS_H
