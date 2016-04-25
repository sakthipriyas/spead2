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
#include <type_traits>
#include <boost/asio.hpp>
#include "recv_stream.h"
#include "recv_reader.h"
#include "common_thread_pool.h"

namespace spead2
{
namespace recv
{

class bypass_reader;

/**
 * Base class for the service that reads packets from an interface and inserts
 * them into streams. The service should run using boost::asio on the provided
 * strand, allowing regular breaks in processing. These breaks are required to
 * register new readers with the service or to expire old ones.
 *
 * The user must not destroy a bypass service while any stream that referenced
 * it still exists.
 */
class bypass_service
{
    // Prevent copying
    bypass_service(const bypass_service &) = delete;
    bypass_service &operator=(const bypass_service &) = delete;
private:
    std::map<boost::asio::ip::udp::endpoint, bypass_reader *> readers;

    void add_endpoint_strand(const boost::asio::ip::udp::endpoint &endpoint, bypass_reader *reader);
    void remove_endpoint_strand(const boost::asio::ip::udp::endpoint &endpoint);

    template<typename F>
    std::future<typename std::result_of<F()>::type> run_in_strand(F &&func);

protected:
    boost::asio::io_service::strand strand;

    /**
     * Process a single packet. This must only be called from the strand.
     *
     * @retval true if the packet is consumed
     * @retval false if the packet is not handled and should be passed on to the host stack
     */
    bool process_packet(const std::uint8_t *data, std::size_t length);

public:
    explicit bypass_service(boost::asio::io_service &io_service);
    virtual ~bypass_service();

    /**
     * Create a bypass service by named type.
     */
    static std::unique_ptr<bypass_service> get_instance(
        boost::asio::io_service &io_service,
        const std::string &type, const std::string &interface);

    static std::unique_ptr<bypass_service> get_instance(
        thread_pool &pool,
        const std::string &type, const std::string &interface);

    /**
     * Add a reader to the list of readers, asynchronously. The caller must not
     * wait for the future while holding a lock that could prevent the strand
     * from progressing (this includes any stream only).
     */
    std::future<void> add_endpoint(const boost::asio::ip::udp::endpoint &endpoint, bypass_reader *reader);
    /**
     * Remove a reader from the list of readers, asynchronously. The same
     * restrictions as for @ref add_endpoint apply.
     */
    std::future<void> remove_endpoint(const boost::asio::ip::udp::endpoint &endpoint);
};

class bypass_reader : public reader
{
    friend class bypass_service;
private:
    bypass_service &service;
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
     * @param service      Running bypass service
     * @param endpoint     Address on which to listen (IPv4 only)
     */
    bypass_reader(stream &owner,
                  bypass_service &service,
                  const boost::asio::ip::udp::endpoint &endpoint);

    virtual std::future<void> start() override;
    virtual void stop() override;
    virtual void join() override;
};

/// Obtain a list of names of compiled-in bypass types.
std::vector<std::string> bypass_types();

} // namespace recv
} // namespace spead2

#endif // SPEAD2_RECV_BYPASS_H
