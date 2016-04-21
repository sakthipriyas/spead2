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
 * Common code to support socket bypass technologies. See @ref recv_bypass.h
 * for details.
 */

#include <functional>
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <unordered_map>
#include <mutex>
#include <arpa/inet.h>
#include "recv_bypass.h"
#include "recv_reader.h"
#include "recv_packet.h"
#include "recv_stream.h"
#include "common_logging.h"
#if SPEAD2_USE_NETMAP
# include "recv_netmap.h"
#endif


namespace spead2
{
namespace recv
{
namespace detail
{

bypass_service::~bypass_service()
{
    assert(readers.empty());
}

void bypass_service::add_endpoint(const boost::asio::ip::udp::endpoint &endpoint, bypass_reader *reader)
{
    if (!endpoint.address().is_v4())
        throw std::invalid_argument("only IPv4 addresses can be used with bypass");
    std::lock_guard<std::mutex> lock(mutex);
    if (!readers.emplace(endpoint, reader).second)
        throw std::invalid_argument("endpoint is already registered");
}

void bypass_service::remove_endpoint(const boost::asio::ip::udp::endpoint &endpoint)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto pos = readers.find(endpoint);
    if (pos == readers.end())
        throw std::invalid_argument("endpoint is not registered");
    readers.erase(pos);
}

bool bypass_service::process_packet(const std::uint8_t *data, std::size_t length)
{
    struct header
    {
        struct
        {
            std::uint8_t ether_dhost[6];
            std::uint8_t ether_shost[6];
            std::uint16_t ether_type;
        } eth __attribute__((packed));
        struct
        {
            std::uint8_t ihl_version;
            std::uint8_t tos;
            std::uint16_t tot_len;
            std::uint16_t id;
            std::uint16_t frag_off;
            std::uint8_t ttl;
            std::uint8_t protocol;
            std::uint16_t check;
            std::uint32_t saddr;
            std::uint32_t daddr;
        } ip  __attribute__((packed));
        struct
        {
            std::uint16_t source;
            std::uint16_t dest;
            std::uint16_t len;
            std::uint16_t check;
        } udp __attribute__((packed));
    } __attribute__((packed));

    if (length < sizeof(header))
        return false;
    const header *ph = (const header *) data;
    /* Checks that this packet is
     * - big enough
     * - IPv4, UDP
     * - unfragmented
     * - on the right port
     * It also requires that there are no IP options, since
     * otherwise the UDP header is at an unknown offset.
     */
    if (ph->eth.ether_type == htons(0x0800)  // ETHERTYPE_IP
        && ph->ip.ihl_version == 0x45        // version 4, IHL 5 => 20 byte header
        && ph->ip.protocol == 17             // IPPROTO_UDP
        && (ph->ip.frag_off & 0x3f) == 0)    // more fragments bit clear, zero offset
    {
        // It's the sort of packet we want, so match it up to a stream
        std::uint16_t port = ntohs(ph->udp.dest);
        std::uint32_t address = ntohl(ph->ip.daddr);
        boost::asio::ip::udp::endpoint endpoint(boost::asio::ip::address_v4(address), port);
        auto pos = readers.find(endpoint);
        if (pos == readers.end())
        {
            // Check if someone is listening on the port for all addresses
            boost::asio::ip::udp::endpoint endpoint2(boost::asio::ip::address_v4::any(), port);
            pos = readers.find(endpoint2);
        }
        if (pos != readers.end())
        {
            pos->second->process_packet(data + sizeof(header), length - sizeof(header));
            return true;
        }
    }
    return false;
}

} // namespace detail

/////////////////////////////////////////////////////////////////////////////
// Reader
/////////////////////////////////////////////////////////////////////////////

bypass_reader::bypass_reader(stream &owner,
                             const std::string &type,
                             const std::string &interface,
                             const boost::asio::ip::udp::endpoint &endpoint)
    : reader(owner), endpoint(endpoint)
{
    service = detail::bypass_service::get_instance(type, interface);
    service->add_endpoint(endpoint, this);
}

void bypass_reader::stop()
{
    service->remove_endpoint(endpoint);
}

void bypass_reader::process_packet(const std::uint8_t *data, std::size_t length)
{
    packet_header packet;
    std::size_t size = decode_packet(packet, data, length);
    if (size == length)
    {
        std::lock_guard<std::mutex> lock(get_stream_mutex());
        get_stream_base().add_packet(packet);
        if (get_stream_base().is_stopped())
            log_debug("netmap_udp_reader: end of stream detected");
    }
    else if (size != 0)
    {
        log_info("discarding packet due to size mismatch (%1% != %2%)",
                 size, length);
    }
}

/////////////////////////////////////////////////////////////////////////////
// Registration
/////////////////////////////////////////////////////////////////////////////

namespace detail
{

class bypass_service_type
{
public:
    virtual ~bypass_service_type() = default;
    virtual std::shared_ptr<bypass_service> factory(const std::string &interface) = 0;

    std::unordered_map<std::string, std::weak_ptr<bypass_service>> services;
    std::mutex services_mutex;
};

template<typename T>
class bypass_service_type_inst : public bypass_service_type
{
public:
    virtual std::shared_ptr<bypass_service> factory(const std::string &interface) override
    {
        return std::make_shared<T>(interface);
    }
};

static std::unordered_map<std::string, std::shared_ptr<bypass_service_type>> service_types
{
#if SPEAD2_USE_NETMAP
    std::make_pair(std::string("netmap"), std::make_shared<bypass_service_type_inst<bypass_service_netmap>>()),
#endif
};

std::shared_ptr<bypass_service> bypass_service::get_instance(const std::string &type, const std::string &interface)
{
    auto inst = service_types.find(type);
    if (inst == service_types.end())
    {
        throw std::invalid_argument("bypass type `" + type + "' not implemented");
    }
    std::lock_guard<std::mutex> lock(inst->second->services_mutex);
    auto service = inst->second->services.find(interface);
    std::shared_ptr<bypass_service> strong;
    if (service != inst->second->services.end())
    {
        strong = service->second.lock();
        if (!strong)
        {
            strong = inst->second->factory(interface);
            service->second = strong;
        }
    }
    else
    {
        strong = inst->second->factory(interface);
        inst->second->services[interface] = strong;
    }
    return strong;
}

} // namespace detail

std::vector<std::string> bypass_types()
{
    std::vector<std::string> ans;
    for (const auto &type : detail::service_types)
        ans.push_back(type.first);
    std::sort(ans.begin(), ans.end());
    return ans;
}

} // namespace recv
} // namespace spead2
